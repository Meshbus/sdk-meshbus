// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

extern "C" {
#include "meshcore/runtime.h"
#include "meshcore_identity.h"
#include "meshcore_packet.h"
#include "meshcore_runtime_internal.h"
#include "meshcore_test_runtime.h"
#include "meshcore/types.h"
}

struct packet_capture {
	uint8_t bytes[MESHCORE_MAX_TRANS_UNIT_LEN];
	size_t len;
};

struct test_node {
	const char *name;
	meshcore_common_node_role_t role;
	struct meshcore_local_identity identity;
	struct meshcore_runtime runtime;
	struct packet_capture tx;
	meshcore_test_peer_path_event_t peer_path_event;
	bool peer_path_event_valid;
	meshcore_test_trace_event_t trace_event;
	bool trace_event_valid;
	meshcore_test_telemetry_event_t telemetry_event;
	bool telemetry_event_valid;
	meshcore_test_advert_event_t advert_event;
	bool advert_event_valid;
	uint8_t peer_path[MESHCORE_MAX_PATH_LEN];
	uint8_t peer_path_len;
	uint8_t peer_path_hash_size;
	uint32_t now_ms;
};

static constexpr uint8_t kPathHashSize = 1U;
static constexpr int8_t kRxSnrQ4 = 24;
static constexpr int16_t kRxRssiDbm = -45;

static const uint8_t kSeedA[MESHCORE_PUBLIC_KEY_SIZE] = {
	0x91, 0x83, 0x75, 0x67, 0x59, 0x4b, 0x3d, 0x2f,
	0x10, 0x22, 0x34, 0x46, 0x58, 0x6a, 0x7c, 0x8e,
	0x9f, 0xaf, 0xbf, 0xcf, 0xdf, 0xef, 0xfe, 0xed,
	0xdc, 0xcb, 0xba, 0xa9, 0x98, 0x87, 0x76, 0x65,
};

static const uint8_t kSeedB[MESHCORE_PUBLIC_KEY_SIZE] = {
	0x13, 0x24, 0x35, 0x46, 0x57, 0x68, 0x79, 0x8a,
	0x9b, 0xac, 0xbd, 0xce, 0xdf, 0xe0, 0xf1, 0x02,
	0x14, 0x26, 0x38, 0x4a, 0x5c, 0x6e, 0x70, 0x82,
	0x94, 0xa6, 0xb8, 0xca, 0xdc, 0xee, 0xf0, 0x11,
};

static const uint8_t kSeedR[MESHCORE_PUBLIC_KEY_SIZE] = {
	0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc,
	0xdd, 0xee, 0xff, 0x10, 0x20, 0x30, 0x40, 0x50,
	0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0,
	0xe0, 0xf0, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67,
};

static struct test_node client_a;
static struct test_node client_b;
static struct test_node repeater_a;
static struct test_node *active_node;

static void generate_identity(const uint8_t *seed,
			      struct meshcore_local_identity *identity)
{
	meshcore_local_identity_init(identity);
	meshcore_hal_test_rng_set_bytes(seed, MESHCORE_PUBLIC_KEY_SIZE);
	meshcore_local_identity_generate(identity);
	meshcore_hal_test_rng_clear();
}

static void runtime_context_rebind(struct meshcore_runtime *runtime)
{
	runtime->mesh.packet_manager = &runtime->packet_manager;
	runtime->mesh.tables = &runtime->tables;
	runtime->mesh.runtime_user_data = runtime;
	runtime->mesh.dispatcher.packet_manager = &runtime->packet_manager;
	runtime->mesh.dispatcher.owner_mesh = &runtime->mesh;
}

static void snapshot_active_node(void)
{
	if (active_node == NULL) {
		return;
	}

	active_node->runtime = *meshcore_runtime_context_get();
	active_node->now_ms = meshcore_test_runtime_last_now_ms_get();
	if (meshcore_hal_test_radio_get_last_send_len() > 0) {
		active_node->tx.len = (size_t)meshcore_hal_test_radio_get_last_send(
			active_node->tx.bytes, sizeof(active_node->tx.bytes));
	}
	if (meshcore_hal_test_peer_path_event_take(&active_node->peer_path_event)) {
		active_node->peer_path_event_valid = true;
	}
	if (meshcore_hal_test_trace_event_take(&active_node->trace_event)) {
		active_node->trace_event_valid = true;
	}
	if (meshcore_hal_test_telemetry_event_take(&active_node->telemetry_event)) {
		active_node->telemetry_event_valid = true;
	}
	if (meshcore_hal_test_advert_event_take(&active_node->advert_event)) {
		active_node->advert_event_valid = true;
	}
}

static void install_node_platform(const struct test_node *node,
				  const struct test_node *peer)
{
	meshcore_common_node_runtime_policy_t policy = {};
	meshcore_common_node_advert_profile_t advert_profile = {};
	meshcore_hal_test_mesh_script_t *mesh_script;
	uint8_t secret[MESHCORE_PUBLIC_KEY_SIZE];

	meshcore_hal_test_host_state_reset();
	meshcore_hal_test_node_identity_set(node->name, node->role,
					    node->identity.identity.pub_key,
					    node->identity.prv_key);

	policy.path_hash_size = kPathHashSize;
	policy.loop_detect = MESHCORE_COMMON_LOOP_DETECT_OFF;
	policy.client_repeat = false;
	policy.disable_fwd = false;
	policy.flood_max = 64U;
	policy.tx_delay_factor = 0.0f;
	policy.direct_tx_delay_factor = 0.0f;
	meshcore_hal_test_node_runtime_policy_set(&policy);

	advert_profile.has_position = true;
	advert_profile.latitude = node == &client_a ? 31000000 : 32000000;
	advert_profile.longitude = node == &client_b ? 121000000 : 122000000;
	meshcore_hal_test_node_advert_profile_set(&advert_profile);

	mesh_script = meshcore_hal_test_mesh_script_get();

	if (peer != NULL) {
		meshcore_local_identity_calc_shared_secret(&node->identity, secret,
							   peer->identity.identity.pub_key);
		meshcore_hal_test_peer_identity_set(peer->identity.identity.pub_key,
						    peer->name, peer->role, 0U);
		mesh_script->override_search_peers_by_hash = true;
		mesh_script->search_peers_by_hash_value = 1;
		mesh_script->override_get_peer_shared_secret = true;
		memcpy(mesh_script->peer_shared_secret, secret, sizeof(secret));
		memset(secret, 0, sizeof(secret));
		if (node->peer_path_len > 0U) {
			meshcore_hal_test_peer_path_set(peer->identity.identity.pub_key,
						       node->peer_path,
						       node->peer_path_len,
						       node->peer_path_hash_size);
		}
	}
}

static void activate_node(struct test_node *node, const struct test_node *peer)
{
	snapshot_active_node();
	active_node = node;
	*meshcore_runtime_context_get() = node->runtime;
	runtime_context_rebind(meshcore_runtime_context_get());
	install_node_platform(node, peer);
	meshcore_hal_test_millis_set(node->now_ms);
	meshcore_hal_test_radio_reset();
	meshcore_hal_test_publish_events_clear();
}

static void init_node(struct test_node *node, const char *name,
		      meshcore_common_node_role_t role, const uint8_t *seed,
		      const struct test_node *peer)
{
	memset(node, 0, sizeof(*node));
	node->name = name;
	node->role = role;
	generate_identity(seed, &node->identity);

	active_node = NULL;
	activate_node(node, peer);
	zassert_ok(meshcore_init(), "%s init failed", name);
	snapshot_active_node();
	active_node = NULL;
}

static void reset_topology(void)
{
	meshcore_deinit();
	active_node = NULL;
	init_node(&client_b, "clientB", MESHCORE_COMMON_NODE_ROLE_CHAT,
		  kSeedB, NULL);
	init_node(&repeater_a, "repeaterA", MESHCORE_COMMON_NODE_ROLE_REPEATER,
		  kSeedR, NULL);
	init_node(&client_a, "clientA", MESHCORE_COMMON_NODE_ROLE_CHAT,
		  kSeedA, &client_b);
}

static void install_client_a_relay_path_from_discover(void)
{
	zassert_true(client_a.peer_path_event_valid,
		     "discover did not produce a peer path event");
	zassert_true(client_a.peer_path_event.out_path_len <= sizeof(client_a.peer_path),
		     "discover path too large");
	memcpy(client_a.peer_path, client_a.peer_path_event.out_path,
	       client_a.peer_path_event.out_path_len);
	client_a.peer_path_len = client_a.peer_path_event.out_path_len;
	client_a.peer_path_hash_size = client_a.peer_path_event.path_hash_size;
}

static int client_a_trace_request(const uint8_t *public_key, uint32_t *tag)
{
	uint8_t trace_path[MESHCORE_MAX_PATH_LEN] = {};
	uint8_t trace_len;

	ARG_UNUSED(public_key);

	if (client_a.peer_path_len == 0U || client_a.peer_path_hash_size == 0U ||
	    client_a.peer_path_hash_size > 3U ||
	    (client_a.peer_path_len % client_a.peer_path_hash_size) != 0U) {
		return -EINVAL;
	}

	memcpy(trace_path, client_a.peer_path, client_a.peer_path_len);
	trace_len = client_a.peer_path_len;
	for (uint8_t hop = client_a.peer_path_len;
	     hop >= (uint8_t)(2U * client_a.peer_path_hash_size);
	     hop = (uint8_t)(hop - client_a.peer_path_hash_size)) {
		if ((size_t)trace_len + client_a.peer_path_hash_size >
		    sizeof(trace_path)) {
			return -EINVAL;
		}
		memcpy(&trace_path[trace_len],
		       &client_a.peer_path[hop - (uint8_t)(2U * client_a.peer_path_hash_size)],
		       client_a.peer_path_hash_size);
		trace_len = (uint8_t)(trace_len + client_a.peer_path_hash_size);
	}

	return meshcore_node_trace_request(trace_path, trace_len,
					   client_a.peer_path_hash_size, tag);
}

static const char *route_name(uint8_t header)
{
	switch (header & PH_ROUTE_MASK) {
	case ROUTE_TYPE_FLOOD:
		return "flood";
	case ROUTE_TYPE_DIRECT:
		return "direct";
	case ROUTE_TYPE_TRANSPORT_DIRECT:
		return "transport";
	default:
		return "unknown";
	}
}

static void log_packet(const char *label, const struct test_node *from,
		       const struct test_node *to, const uint8_t *raw, size_t len)
{
	struct meshcore_packet packet;

	meshcore_packet_init(&packet);
	if (len > 0U && len <= UINT8_MAX &&
	    meshcore_packet_read_from(&packet, raw, (uint8_t)len)) {
		TC_PRINT("topology %s: %s -> %s len=%u route=%s payload=%u "
			 "hash_size=%u hash_count=%u path_len=%u\n",
			 label, from->name, to->name, (unsigned int)len,
			 route_name(packet.header),
			 (unsigned int)meshcore_packet_get_payload_type(&packet),
			 (unsigned int)meshcore_packet_get_path_hash_size(&packet),
			 (unsigned int)meshcore_packet_get_path_hash_count(&packet),
			 (unsigned int)packet.path_len);
		return;
	}

	TC_PRINT("topology %s: %s -> %s len=%u undecodable\n", label,
		 from->name, to->name, (unsigned int)len);
}

static void log_hex_payload(const char *label, const uint8_t *payload, size_t len)
{
	TC_PRINT("topology %s: len=%u data=", label, (unsigned int)len);
	for (size_t i = 0; i < len; i++) {
		TC_PRINT("%02x", payload[i]);
		if ((i + 1U) < len) {
			TC_PRINT(" ");
		}
	}
	TC_PRINT("\n");
}

static void process_until_packets_sent(struct test_node *node,
				       const struct test_node *peer,
				       uint32_t expected_packets)
{
	uint32_t now_ms;

	activate_node(node, peer);
	now_ms = node->now_ms;
	zassert_ok(meshcore_timer_fired(now_ms), "%s timer failed", node->name);
	for (int i = 0; i < 24 &&
		    meshcore_hal_test_radio_get_packets_sent() < expected_packets;
	     i++) {
		if (meshcore_test_runtime_dispatcher_has_active_outbound()) {
			meshcore_hal_test_radio_on_send_finished();
			zassert_ok(meshcore_radio_tx_done(now_ms, true),
				   "%s tx done failed", node->name);
		}
		now_ms += 100U;
		meshcore_hal_test_millis_set(now_ms);
		zassert_ok(meshcore_timer_fired(now_ms), "%s timer tick failed",
			   node->name);
	}
	if (meshcore_test_runtime_dispatcher_has_active_outbound()) {
		meshcore_hal_test_radio_on_send_finished();
		zassert_ok(meshcore_radio_tx_done(now_ms, true),
			   "%s final tx done failed", node->name);
	}
	node->now_ms = now_ms;
	snapshot_active_node();
	zassert_equal(node->tx.len > 0U ? 1U : 0U, expected_packets > 0U ? 1U : 0U,
		      "%s captured packet mismatch", node->name);
}

static void deliver_packet(const char *label, struct test_node *from,
			   struct test_node *to, const struct test_node *to_peer)
{
	zassert_true(from->tx.len > 0U, "%s had no packet to deliver", from->name);
	log_packet(label, from, to, from->tx.bytes, from->tx.len);
	activate_node(to, to_peer);
	zassert_ok(meshcore_radio_rx_inject(from->tx.bytes, from->tx.len,
					    kRxRssiDbm, kRxSnrQ4, to->now_ms),
		   "%s rx inject failed", to->name);
	to->tx.len = 0U;
	snapshot_active_node();
}

ZTEST(meshcore_runtime_topology, test_discover_path_returns_relay_out_path)
{
	uint32_t tag = 0x10203040U;

	reset_topology();

	activate_node(&client_a, &client_b);
	meshcore_hal_test_rng_set_bytes((const uint8_t *)&tag, sizeof(tag));
	zassert_ok(meshcore_node_discover_path_request(
			   client_b.identity.identity.pub_key, &tag),
		   "clientA discover request failed");
	process_until_packets_sent(&client_a, &client_b, 1U);

	deliver_packet("discover-request", &client_a, &repeater_a, NULL);
	process_until_packets_sent(&repeater_a, NULL, 1U);

	deliver_packet("discover-forward", &repeater_a, &client_b, &client_a);
	process_until_packets_sent(&client_b, &client_a, 1U);

	deliver_packet("discover-response", &client_b, &repeater_a, NULL);
	process_until_packets_sent(&repeater_a, NULL, 1U);

	deliver_packet("discover-return", &repeater_a, &client_a, &client_b);

	zassert_true(client_a.peer_path_event_valid,
		     "clientA peer path event missing");
	zassert_true(client_a.peer_path_event.has_out_path,
		     "clientA peer path should have out_path");
	zassert_true(client_a.peer_path_event.out_path_len > 0U,
		     "clientA out_path should not be empty");
	zassert_equal(client_a.peer_path_event.path_hash_size, kPathHashSize,
		      "clientA path hash size mismatch");
	TC_PRINT("topology discover result: tag=0x%08x out_path_len=%u "
		 "path_hash_size=%u first_hop=0x%02x\n",
		 client_a.peer_path_event.tag,
		 (unsigned int)client_a.peer_path_event.out_path_len,
		 (unsigned int)client_a.peer_path_event.path_hash_size,
		 client_a.peer_path_event.out_path[0]);
}

static void run_discover_and_install_relay_path(void)
{
	uint32_t tag = 0x10203040U;

	reset_topology();

	activate_node(&client_a, &client_b);
	zassert_ok(meshcore_node_discover_path_request(
			   client_b.identity.identity.pub_key, &tag),
		   "clientA discover request failed");
	process_until_packets_sent(&client_a, &client_b, 1U);
	deliver_packet("discover-request", &client_a, &repeater_a, NULL);
	process_until_packets_sent(&repeater_a, NULL, 1U);
	deliver_packet("discover-forward", &repeater_a, &client_b, &client_a);
	process_until_packets_sent(&client_b, &client_a, 1U);
	deliver_packet("discover-response", &client_b, &repeater_a, NULL);
	process_until_packets_sent(&repeater_a, NULL, 1U);
	deliver_packet("discover-return", &repeater_a, &client_a, &client_b);

	install_client_a_relay_path_from_discover();
	TC_PRINT("topology installed clientA relay path: len=%u hash_size=%u\n",
		 (unsigned int)client_a.peer_path_len,
		 (unsigned int)client_a.peer_path_hash_size);
}

ZTEST(meshcore_runtime_topology, test_trace_path_traverses_relay_path)
{
	uint32_t tag = 0x20304050U;

	run_discover_and_install_relay_path();
	client_a.trace_event_valid = false;

	activate_node(&client_a, &client_b);
	zassert_ok(client_a_trace_request(client_b.identity.identity.pub_key, &tag),
		   "clientA trace request failed");
	process_until_packets_sent(&client_a, &client_b, 1U);

	deliver_packet("trace-request", &client_a, &repeater_a, NULL);
	process_until_packets_sent(&repeater_a, NULL, 1U);

	deliver_packet("trace-forward", &repeater_a, &client_b, &client_a);

	zassert_false(client_a.trace_event_valid,
		      "origin should not receive trace before return");
	static const int8_t trace_snrs[] = {4, 5, 6};
	activate_node(&client_a, &client_b);
	zassert_true(meshcore_test_runtime_simulate_trace_recv(
			     tag, 0U, trace_snrs, ARRAY_SIZE(trace_snrs), kRxSnrQ4),
		     "clientA trace result simulation failed");
	snapshot_active_node();

	zassert_true(client_a.trace_event_valid,
		     "origin should publish trace result after return");
	zassert_equal(client_a.trace_event.tag, tag, "trace tag mismatch");
	TC_PRINT("topology trace result event: tag=0x%08x out_snr_count=%u "
		 "return_snr_count=%u\n",
		 client_a.trace_event.tag,
		 (unsigned int)client_a.trace_event.out_path_snr_count,
		 (unsigned int)client_a.trace_event.return_path_snr_count);
}

ZTEST(meshcore_runtime_topology, test_telemetry_request_returns_over_relay_path)
{
	uint32_t tag = 0x30405060U;

	run_discover_and_install_relay_path();
	client_a.telemetry_event_valid = false;

	activate_node(&client_a, &client_b);
	zassert_ok(meshcore_node_telemetry_request(
			   client_b.identity.identity.pub_key,
			   MESHCORE_TELEM_PERM_BASE, &tag),
		   "clientA telemetry request failed");
	process_until_packets_sent(&client_a, &client_b, 1U);

	deliver_packet("telemetry-request", &client_a, &repeater_a, NULL);
	process_until_packets_sent(&repeater_a, NULL, 1U);

	deliver_packet("telemetry-forward", &repeater_a, &client_b, &client_a);
	process_until_packets_sent(&client_b, &client_a, 1U);

	deliver_packet("telemetry-response", &client_b, &repeater_a, NULL);
	process_until_packets_sent(&repeater_a, NULL, 1U);

	deliver_packet("telemetry-return", &repeater_a, &client_a, &client_b);

	zassert_true(client_a.telemetry_event_valid,
		     "clientA telemetry response missing");
	zassert_equal(client_a.telemetry_event.tag, tag,
		      "telemetry response tag mismatch");
	log_hex_payload("telemetry payload", client_a.telemetry_event.payload,
			client_a.telemetry_event.payload_len);
	zassert_true(client_a.telemetry_event.payload_len > 0U,
		     "telemetry payload should not be empty");
	zassert_equal(client_a.telemetry_event.payload[0], MESHCORE_TELEM_PERM_BASE,
		      "telemetry payload base byte mismatch");
	TC_PRINT("topology telemetry result: tag=0x%08x payload_len=%u\n",
		 client_a.telemetry_event.tag,
		 (unsigned int)client_a.telemetry_event.payload_len);
}

ZTEST(meshcore_runtime_topology, test_advert_flood_traverses_relay_path)
{
	reset_topology();

	activate_node(&client_b, NULL);
	zassert_ok(meshcore_node_advert_request(true),
		   "clientB advert request failed");
	process_until_packets_sent(&client_b, NULL, 1U);

	deliver_packet("advert-flood", &client_b, &repeater_a, NULL);
	process_until_packets_sent(&repeater_a, NULL, 1U);

	deliver_packet("advert-forward", &repeater_a, &client_a, NULL);

	zassert_true(client_a.advert_event_valid, "clientA advert event missing");
	zassert_mem_equal(client_a.advert_event.public_key,
			  client_b.identity.identity.pub_key,
			  sizeof(client_a.advert_event.public_key),
			  "advert public key mismatch");
	zassert_true(client_a.advert_event.has_out_path,
		     "forwarded advert should include path metadata");
	zassert_true(client_a.advert_event.out_path_len > 0U,
		     "forwarded advert path should not be empty");
	TC_PRINT("topology advert result: from=%s out_path_len=%u "
		 "path_hash_size=%u first_hop=0x%02x\n",
		 client_b.name, (unsigned int)client_a.advert_event.out_path_len,
		 (unsigned int)client_a.advert_event.path_hash_size,
		 client_a.advert_event.out_path[0]);
}

ZTEST_SUITE(meshcore_runtime_topology, NULL, NULL, NULL, NULL, NULL);
