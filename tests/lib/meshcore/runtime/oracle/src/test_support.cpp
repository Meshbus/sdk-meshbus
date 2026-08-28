// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "test_support.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>

extern "C" {
#include "meshcore_advert_data.h"
#include "meshcore_clock.h"
#include "meshcore_mesh.h"
#include "meshcore_packet.h"
#include "meshcore_packet_manager.h"
#include "meshcore_tables.h"
#include "meshcore_test_runtime.h"
}

const uint8_t k_local_identity_seed[MESHCORE_PUBLIC_KEY_SIZE] = {
	0x91, 0x83, 0x75, 0x67, 0x59, 0x4b, 0x3d, 0x2f,
	0x10, 0x22, 0x34, 0x46, 0x58, 0x6a, 0x7c, 0x8e,
	0x9f, 0xaf, 0xbf, 0xcf, 0xdf, 0xef, 0xfe, 0xed,
	0xdc, 0xcb, 0xba, 0xa9, 0x98, 0x87, 0x76, 0x65,
};

const uint8_t k_peer_identity_seed[MESHCORE_PUBLIC_KEY_SIZE] = {
	0x13, 0x24, 0x35, 0x46, 0x57, 0x68, 0x79, 0x8a,
	0x9b, 0xac, 0xbd, 0xce, 0xdf, 0xe0, 0xf1, 0x02,
	0x14, 0x26, 0x38, 0x4a, 0x5c, 0x6e, 0x70, 0x82,
	0x94, 0xa6, 0xb8, 0xca, 0xdc, 0xee, 0xf0, 0x11,
};

const uint8_t k_channel_secret_16[16] = {
	0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
	0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
};

const uint8_t k_direct_out_path[1] = {0x44};
const uint8_t k_trace_out_path[2] = {0x44, 0x55};
const uint8_t k_trace_hash3_out_path[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
static const uint8_t k_inbound_identity_seed[MESHCORE_PUBLIC_KEY_SIZE] = {
	0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc,
	0xdd, 0xee, 0xff, 0x10, 0x20, 0x30, 0x40, 0x50,
	0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0,
	0xe0, 0xf0, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67,
};

static void generate_identity(const uint8_t *seed, size_t seed_len,
			      struct meshcore_local_identity *identity)
{
	zassert_not_null(seed, "seed is null");
	zassert_not_null(identity, "identity is null");

	meshcore_local_identity_init(identity);
	meshcore_hal_test_rng_set_bytes(seed, seed_len);
	meshcore_local_identity_generate(identity);
	meshcore_hal_test_rng_clear();
}

void clear_target_publish_events(void)
{
	meshcore_hal_test_publish_events_clear();
}

static void install_node_config(const struct meshcore_local_identity *identity,
				const char *name)
{
	meshcore_common_node_runtime_policy_t policy = {};
	meshcore_common_node_advert_profile_t advert_profile = {};

	zassert_not_null(identity, "local identity is null");
	zassert_not_null(name, "node name is null");

	meshcore_hal_test_node_identity_set(name, MESHCORE_COMMON_NODE_ROLE_CHAT,
					    identity->identity.pub_key,
					    identity->prv_key);
	policy.path_hash_size = 1U;
	policy.loop_detect = MESHCORE_COMMON_LOOP_DETECT_OFF;
	policy.client_repeat = false;
	policy.disable_fwd = false;
	policy.flood_max = 64U;
	policy.tx_delay_factor = 0.5f;
	policy.direct_tx_delay_factor = 0.2f;
	meshcore_hal_test_node_runtime_policy_set(&policy);

	advert_profile.has_position = true;
	advert_profile.latitude = 31000000;
	advert_profile.longitude = 121000000;
	meshcore_hal_test_node_advert_profile_set(&advert_profile);
}

static ReferenceChatHarness::route_kind classify_packet_route(
	const struct meshcore_packet *packet)
{
	if (meshcore_packet_is_route_flood(packet)) {
		return ReferenceChatHarness::ROUTE_FLOOD;
	}
	if (meshcore_packet_is_route_direct(packet) &&
	    meshcore_packet_get_payload_type(packet) == PAYLOAD_TYPE_TRACE) {
		return ReferenceChatHarness::ROUTE_DIRECT;
	}
	if (meshcore_packet_is_route_direct(packet) && packet->path_len == 0U) {
		return ReferenceChatHarness::ROUTE_ZERO_HOP;
	}
	if (meshcore_packet_is_route_direct(packet)) {
		return ReferenceChatHarness::ROUTE_DIRECT;
	}
	return ReferenceChatHarness::ROUTE_NONE;
}

static void dump_packet_hex(const char *label, const uint8_t *data, size_t len)
{
	printk("%s (%u bytes):", label, (unsigned int)len);
	for (size_t i = 0U; i < len; i++) {
		printk(" %02x", data[i]);
	}
	printk("\n");
}

static bool decode_observed_packet(const uint8_t *raw, size_t raw_len,
				   ReferenceChatHarness::observed_packet *out)
{
	struct meshcore_packet packet;

	zassert_not_null(raw, "raw is null");
	zassert_not_null(out, "out is null");
	if (raw_len == 0U || raw_len > MESHCORE_MAX_TRANS_UNIT_LEN) {
		return false;
	}

	meshcore_packet_init(&packet);
	if (!meshcore_packet_read_from(&packet, raw, (uint8_t)raw_len)) {
		return false;
	}

	memset(out, 0, sizeof(*out));
	out->sent = true;
	out->raw_len = (int)raw_len;
	memcpy(out->raw, raw, raw_len);
	out->route = classify_packet_route(&packet);
	out->has_transport_codes = meshcore_packet_has_transport_codes(&packet);
	out->payload_type = meshcore_packet_get_payload_type(&packet);
	return true;
}

void reset_target_runtime_state(void)
{
	meshcore_deinit();
	meshcore_hal_test_host_state_reset();
	clear_target_publish_events();
	meshcore_hal_test_millis_set(0U);
	meshcore_hal_test_rtc_set_current_time(0U);
}

runtime_fixture_data setup_target_runtime_fixture(void)
{
	runtime_fixture_data fixture = {};

	generate_identity(k_local_identity_seed, sizeof(k_local_identity_seed),
			  &fixture.local_identity);
	generate_identity(k_peer_identity_seed, sizeof(k_peer_identity_seed),
			  &fixture.peer_identity);
	generate_identity(k_inbound_identity_seed, sizeof(k_inbound_identity_seed),
			  &fixture.inbound_identity);
	install_node_config(&fixture.local_identity, "runtime_node");
	meshcore_hal_test_peer_identity_set(fixture.peer_identity.identity.pub_key,
					    "runtime_peer",
					    MESHCORE_COMMON_NODE_ROLE_CHAT, 0U);
	meshcore_hal_test_peer_path_set(fixture.peer_identity.identity.pub_key, NULL,
					0U, 1U);
	meshcore_hal_test_channel_secret_set(k_channel_secret_16,
					     sizeof(k_channel_secret_16));
	return fixture;
}

void setup_reference_fixture(ReferenceChatHarness *oracle,
			     const runtime_fixture_data *fixture)
{
	zassert_not_null(oracle, "oracle is null");
	zassert_not_null(fixture, "fixture is null");

	oracle->reset();
	oracle->set_self_identity(fixture->local_identity);
	oracle->set_node_name("runtime_node");
	oracle->set_advert_profile(true, 31.0, 121.0);
	oracle->set_local_path_hash_size(1U);
	oracle->set_require_local_identity_for_send(true);
	oracle->set_require_known_group_channel(true);
	zassert_true(oracle->add_known_group_channel_secret(
			     k_channel_secret_16, sizeof(k_channel_secret_16)),
		     "failed to install oracle group channel secret");
	zassert_true(oracle->upsert_contact(fixture->peer_identity.identity.pub_key,
					    "runtime_peer", ADV_TYPE_CHAT),
		     "failed to install oracle contact");
}

void target_set_peer_out_path(const uint8_t *public_key, const uint8_t *path,
			      uint8_t path_len, uint8_t path_hash_size)
{
	meshcore_hal_test_peer_path_set(public_key, path, path_len, path_hash_size);
}

void target_set_peer_path_unknown(const uint8_t *public_key)
{
	meshcore_hal_test_peer_path_unknown_set(public_key);
}

void target_set_request_rng_bytes(const uint8_t *bytes, size_t len)
{
	meshcore_hal_test_rng_set_bytes(bytes, len);
}

void target_process_until_packets_sent(uint32_t expected_packets)
{
	uint32_t now_ms = 0U;
	uint32_t packets_sent;

	zassert_ok(meshcore_timer_fired(now_ms), "process at t=%u failed",
		   (unsigned int)now_ms);

	for (int i = 0; i < 24 &&
			meshcore_hal_test_radio_get_packets_sent() < expected_packets;
	     i++) {
		if (meshcore_test_runtime_dispatcher_has_active_outbound()) {
			meshcore_hal_test_radio_on_send_finished();
			zassert_ok(meshcore_radio_tx_done(now_ms, true),
				   "tx_done at t=%u failed", (unsigned int)now_ms);
		}
		now_ms += 250U;
		meshcore_hal_test_millis_set(now_ms);
		zassert_ok(meshcore_timer_fired(now_ms), "process at t=%u failed",
			   (unsigned int)now_ms);
	}
	if (meshcore_test_runtime_dispatcher_has_active_outbound()) {
		meshcore_hal_test_radio_on_send_finished();
		zassert_ok(meshcore_radio_tx_done(now_ms, true),
			   "final tx_done at t=%u failed", (unsigned int)now_ms);
	}

	packets_sent = meshcore_hal_test_radio_get_packets_sent();
	zassert_equal(packets_sent, expected_packets,
		      "sent packet count mismatch actual=%u expected=%u",
		      (unsigned int)packets_sent, (unsigned int)expected_packets);
	meshcore_hal_test_rng_clear();
}

int target_radio_packet_inject(const uint8_t *raw, size_t raw_len, int16_t rssi_dbm,
			       int8_t snr_db, uint32_t delay_ms)
{
	if (delay_ms != 0U) {
		return -ENOTSUP;
	}

	return meshcore_radio_rx_inject(raw, raw_len, rssi_dbm, snr_db,
					meshcore_test_runtime_last_now_ms_get());
}

bool target_await_advert_event(meshcore_test_advert_event_t *out,
			       int32_t timeout_ms)
{
	int32_t waited_ms = 0;

	do {
		if (meshcore_hal_test_advert_event_take(out)) {
			return true;
		}
		if (timeout_ms <= 0) {
			break;
		}
		k_sleep(K_MSEC(1));
		waited_ms++;
	} while (waited_ms < timeout_ms);

	return false;
}

bool target_await_peer_path_event(meshcore_test_peer_path_event_t *out,
				      int32_t timeout_ms)
{
	int32_t waited_ms = 0;

	do {
		if (meshcore_hal_test_peer_path_event_take(out)) {
			return true;
		}
		if (timeout_ms <= 0) {
			break;
		}
		k_sleep(K_MSEC(1));
		waited_ms++;
	} while (waited_ms < timeout_ms);

	return false;
}

bool target_await_trace_event(meshcore_test_trace_event_t *out,
			      int32_t timeout_ms)
{
	int32_t waited_ms = 0;

	do {
		if (meshcore_hal_test_trace_event_take(out)) {
			return true;
		}
		if (timeout_ms <= 0) {
			break;
		}
		k_sleep(K_MSEC(1));
		waited_ms++;
	} while (waited_ms < timeout_ms);

	return false;
}

bool target_await_telemetry_event(meshcore_test_telemetry_event_t *out,
				   int32_t timeout_ms)
{
	int32_t waited_ms = 0;

	do {
		if (meshcore_hal_test_telemetry_event_take(out)) {
			return true;
		}
		if (timeout_ms <= 0) {
			break;
		}
		k_sleep(K_MSEC(1));
		waited_ms++;
	} while (waited_ms < timeout_ms);

	return false;
}

uint32_t target_advert_recv_count(void)
{
	return meshcore_hal_test_mesh_script_get()->on_advert_recv_count;
}

bool capture_last_target_packet(ReferenceChatHarness::observed_packet *out)
{
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN];
	int len;

	zassert_not_null(out, "out is null");
	len = meshcore_hal_test_radio_get_last_send(raw, sizeof(raw));
	if (len <= 0) {
		return false;
	}

	return decode_observed_packet(raw, (size_t)len, out);
}

size_t build_target_raw_advert_packet(uint8_t *dest, size_t capacity)
{
	static const uint8_t k_rng_seed[MESHCORE_PUBLIC_KEY_SIZE] = {
		0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
		0x98, 0xa9, 0xba, 0xcb, 0xdc, 0xed, 0xfe, 0x0f,
		0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
		0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x12,
	};
	static const uint8_t k_app_data[] = {0x41, 0x42, 0x43};
	struct meshcore_packet_queue_manager packet_manager;
	struct meshcore_tables tables;
	struct meshcore_mesh mesh;
	struct meshcore_local_identity identity;
	struct meshcore_packet *packet;
	size_t raw_len;

	if (dest == NULL || capacity < 1U) {
		return 0U;
	}

	memset(&packet_manager, 0, sizeof(packet_manager));
	meshcore_packet_queue_manager_prepare(&packet_manager, 2);
	if (!packet_manager.initialized) {
		return 0U;
	}

	meshcore_tables_init(&tables);
	meshcore_mesh_init(&mesh, &packet_manager, &tables);
	meshcore_local_identity_init(&identity);
	meshcore_hal_test_rng_set_bytes(k_rng_seed, sizeof(k_rng_seed));
	meshcore_hal_test_rtc_set_current_time(1234U);
	meshcore_local_identity_generate(&identity);

	packet = meshcore_mesh_create_advert(&mesh, &identity, k_app_data,
					     sizeof(k_app_data));
	if (packet == NULL) {
		meshcore_packet_queue_manager_deinit(&packet_manager);
		return 0U;
	}

	raw_len = meshcore_packet_write_to(packet, dest);
	if (raw_len > capacity) {
		raw_len = 0U;
	}

	meshcore_packet_queue_manager_free(&packet_manager, packet);
	meshcore_packet_queue_manager_deinit(&packet_manager);
	meshcore_hal_test_rng_clear();
	return raw_len;
}

void assert_peer_path_events_equal(
	const meshcore_test_peer_path_event_t *target,
	const ReferenceChatHarness::observed_peer_path_event *oracle,
	const char *context)
{
	zassert_not_null(target, "%s: target event missing", context);
	zassert_not_null(oracle, "%s: oracle event missing", context);
	zassert_true(oracle->published, "%s: oracle event not published", context);
	zassert_equal(target->is_discover, oracle->is_discover,
		      "%s: is_discover mismatch", context);
	zassert_mem_equal(target->key_prefix, oracle->key_prefix,
			  sizeof(target->key_prefix), "%s: key_prefix mismatch",
			  context);
	zassert_equal(target->response_snr, oracle->response_snr,
		      "%s: response_snr mismatch", context);
	zassert_equal(target->out_path_len, oracle->out_path_len,
		      "%s: out_path_len mismatch", context);
	zassert_equal(target->path_hash_size, oracle->path_hash_size,
		      "%s: path_hash_size mismatch", context);
	zassert_mem_equal(target->out_path, oracle->out_path, oracle->out_path_len,
			  "%s: out_path mismatch", context);
	zassert_equal(target->out_path_snr_count, oracle->out_path_snr_count,
		      "%s: out_path_snr_count mismatch", context);
	zassert_mem_equal(target->out_path_snr, oracle->out_path_snr,
			  oracle->out_path_snr_count,
			  "%s: out_path_snr mismatch", context);
	if (target->return_path_snr_count != oracle->return_path_snr_count) {
		printk("%s: target return_count=%u oracle return_count=%u\n", context,
		       (unsigned int)target->return_path_snr_count,
		       (unsigned int)oracle->return_path_snr_count);
	}
	zassert_equal(target->return_path_snr_count, oracle->return_path_snr_count,
		      "%s: return_path_snr_count mismatch", context);
	zassert_mem_equal(target->return_path_snr, oracle->return_path_snr,
			  oracle->return_path_snr_count,
			  "%s: return_path_snr mismatch", context);
}

void assert_trace_events_equal(
	const meshcore_test_trace_event_t *target,
	const ReferenceChatHarness::observed_trace_event *oracle,
	const char *context)
{
	zassert_not_null(target, "%s: target event missing", context);
	zassert_not_null(oracle, "%s: oracle event missing", context);
	zassert_true(oracle->published, "%s: oracle event not published", context);
	zassert_equal(target->state, oracle->state, "%s: state mismatch", context);
	zassert_equal(target->response_snr, oracle->response_snr,
		      "%s: response_snr mismatch", context);
	zassert_equal(target->out_path_snr_count, oracle->out_path_snr_count,
		      "%s: out_path_snr_count mismatch", context);
	zassert_mem_equal(target->out_path_snr, oracle->out_path_snr,
			  oracle->out_path_snr_count,
			  "%s: out_path_snr mismatch", context);
	zassert_equal(target->return_path_snr_count, oracle->return_path_snr_count,
		      "%s: return_path_snr_count mismatch", context);
	zassert_mem_equal(target->return_path_snr, oracle->return_path_snr,
			  oracle->return_path_snr_count,
			  "%s: return_path_snr mismatch", context);
}

void assert_telemetry_events_equal(
	const meshcore_test_telemetry_event_t *target,
	const ReferenceChatHarness::observed_telemetry_event *oracle,
	const char *context)
{
	zassert_not_null(target, "%s: target event missing", context);
	zassert_not_null(oracle, "%s: oracle event missing", context);
	zassert_true(oracle->published, "%s: oracle event not published", context);
	zassert_mem_equal(target->key_prefix, oracle->key_prefix,
			  sizeof(target->key_prefix), "%s: key_prefix mismatch",
			  context);
	zassert_equal(target->tag, oracle->tag, "%s: telemetry tag mismatch",
		      context);
	if (target->payload_len != oracle->payload_len) {
		printk("%s: target payload_len=%u oracle payload_len=%u\n", context,
		       (unsigned int)target->payload_len,
		       (unsigned int)oracle->payload_len);
	}
	zassert_equal(target->payload_len, oracle->payload_len,
		      "%s: payload_len mismatch", context);
	zassert_mem_equal(target->payload, oracle->payload, oracle->payload_len,
			  "%s: payload mismatch", context);
}

void assert_advert_events_equal(
	const meshcore_test_advert_event_t *target,
	const ReferenceChatHarness::observed_advert_event *oracle,
	const char *context)
{
	zassert_not_null(target, "%s: target event missing", context);
	zassert_not_null(oracle, "%s: oracle event missing", context);
	zassert_true(oracle->observed, "%s: oracle advert not observed", context);
	/*
	 * BaseChatMesh folds contact auto-add into is_new. The C runtime keeps
	 * auto-add and final is_new adjustment in the host/Node layer.
	 */
	zassert_mem_equal(target->public_key, oracle->public_key,
			  sizeof(target->public_key), "%s: public_key mismatch",
			  context);
	zassert_equal(strcmp(target->name, oracle->name), 0,
		      "%s: name mismatch", context);
	zassert_equal((uint8_t)target->role, oracle->type,
		      "%s: role/type mismatch", context);
	zassert_equal(target->advert_timestamp, oracle->advert_timestamp,
		      "%s: advert timestamp mismatch", context);
	zassert_equal(target->has_position, oracle->has_position,
		      "%s: position presence mismatch", context);
	if (oracle->has_position) {
		zassert_equal(target->latitude, oracle->latitude,
			      "%s: latitude mismatch", context);
		zassert_equal(target->longitude, oracle->longitude,
			      "%s: longitude mismatch", context);
	}
	zassert_equal(target->out_path_len, oracle->path_len,
		      "%s: path length mismatch", context);
	if (oracle->path_len > 0U) {
		zassert_true(target->has_out_path, "%s: path presence mismatch",
			     context);
		zassert_mem_equal(target->out_path, oracle->path, oracle->path_len,
				  "%s: path mismatch", context);
	}
}

void assert_packets_equal(const ReferenceChatHarness::observed_packet *target,
			  const ReferenceChatHarness::observed_packet *oracle,
			  const char *context)
{
	zassert_not_null(target, "%s: target packet missing", context);
	zassert_not_null(oracle, "%s: oracle packet missing", context);
	zassert_true(target->sent, "%s: target packet not sent", context);
	zassert_true(oracle->sent, "%s: oracle packet not sent", context);
	zassert_equal(target->raw_len, oracle->raw_len, "%s: raw_len mismatch",
		      context);
	if (memcmp(target->raw, oracle->raw, target->raw_len) != 0) {
		printk("%s: target route=%d oracle route=%d target transport=%d oracle transport=%d\n",
		       context, (int)target->route, (int)oracle->route,
		       (int)target->has_transport_codes,
		       (int)oracle->has_transport_codes);
		dump_packet_hex("target", target->raw, (size_t)target->raw_len);
		dump_packet_hex("oracle", oracle->raw, (size_t)oracle->raw_len);
	}
	zassert_mem_equal(target->raw, oracle->raw, target->raw_len,
			  "%s: raw bytes mismatch", context);
}

void assert_packet_behavior(const ReferenceChatHarness::observed_packet *packet,
			    ReferenceChatHarness::route_kind route,
			    bool has_transport_codes, uint8_t payload_type,
			    const char *context)
{
	zassert_not_null(packet, "%s: packet missing", context);
	zassert_true(packet->sent, "%s: packet not sent", context);
	if (packet->route != route) {
		printk("%s: route mismatch actual=%d expected=%d\n", context,
		       (int)packet->route, (int)route);
	}
	zassert_equal(packet->route, route, "%s: route mismatch", context);
	zassert_equal(packet->has_transport_codes, has_transport_codes,
		      "%s: transport-code mismatch", context);
	zassert_equal(packet->payload_type, payload_type, "%s: payload_type mismatch",
		      context);
}
