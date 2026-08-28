// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Projects
 */

#include <errno.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

extern "C" {
#include "meshcore/runtime.h"
#include "meshcore_identity.h"
#include "meshcore_mesh.h"
#include "meshcore_packet.h"
#include "meshcore_packet_manager.h"
#include "meshcore/types.h"
#include "meshcore_runtime_bridge.h"
#include "meshcore_runtime_internal.h"
#include "meshcore_tables.h"
#include "meshcore_test_runtime.h"
#include "meshcore_utils.h"
}

static const uint8_t k_test_public_key[MESHCORE_PUBLIC_KEY_SIZE] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
	0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
	0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
	0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
};

static const uint8_t k_test_channel_secret_16[MESHCORE_CHANNEL_SECRET_LEN_16] = {
	0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
	0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
};

static const uint8_t k_test_binary_payload[] = {
	0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
};

static const uint8_t k_local_identity_seed[MESHCORE_PUBLIC_KEY_SIZE] = {
	0x91, 0x83, 0x75, 0x67, 0x59, 0x4b, 0x3d, 0x2f,
	0x10, 0x22, 0x34, 0x46, 0x58, 0x6a, 0x7c, 0x8e,
	0x9f, 0xaf, 0xbf, 0xcf, 0xdf, 0xef, 0xfe, 0xed,
	0xdc, 0xcb, 0xba, 0xa9, 0x98, 0x87, 0x76, 0x65,
};

static const uint8_t k_peer_identity_seed[MESHCORE_PUBLIC_KEY_SIZE] = {
	0x13, 0x24, 0x35, 0x46, 0x57, 0x68, 0x79, 0x8a,
	0x9b, 0xac, 0xbd, 0xce, 0xdf, 0xe0, 0xf1, 0x02,
	0x14, 0x26, 0x38, 0x4a, 0x5c, 0x6e, 0x70, 0x82,
	0x94, 0xa6, 0xb8, 0xca, 0xdc, 0xee, 0xf0, 0x11,
};

static const uint8_t k_trace_out_path[] = {
	0x44, 0x55,
};

static const uint8_t k_trace_route_path[] = {
	0x44, 0x55, 0x44,
};

static constexpr uint8_t kTxtTypePlain = 0U;
static constexpr uint8_t kCtlTypeNodeDiscoverReq = 0x80U;
static constexpr uint8_t kCtlTypeNodeDiscoverResp = 0x90U;
static constexpr uint8_t kReqTypeTelemetry = 0x03U;
static constexpr uint8_t kAdvTypeRepeater = 2U;
static constexpr uint8_t kAdvTypeSensor = 4U;

struct runtime_fixture_data {
	struct meshcore_local_identity local_identity;
	struct meshcore_local_identity peer_identity;
};

static void clear_runtime_response_events(void);

static void clear_runtime_response_events(void)
{
	meshcore_hal_test_publish_events_clear();
}

static void reset_runtime_state(void)
{
	meshcore_deinit();
	meshcore_hal_test_host_state_reset();
	clear_runtime_response_events();
	meshcore_hal_test_millis_set(0U);
	meshcore_hal_test_timer_reset();
	meshcore_hal_test_rtc_set_current_time(0U);
}

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

static void set_node_role(meshcore_common_node_role_t role)
{
	meshcore_common_node_identity_t identity = {};

	zassert_ok(meshcore_node_identity_get(&identity), "node identity get failed");
	identity.role = role;
	meshcore_hal_test_node_identity_set(identity.name, role, identity.public_key,
					    identity.private_key);
}

static void set_runtime_policy(bool client_repeat, bool disable_fwd,
			       uint8_t flood_max, float tx_delay_factor,
			       float direct_tx_delay_factor)
{
	meshcore_common_node_runtime_policy_t policy = {};

	policy.path_hash_size = 1U;
	policy.loop_detect = MESHCORE_COMMON_LOOP_DETECT_OFF;
	policy.client_repeat = client_repeat;
	policy.disable_fwd = disable_fwd;
	policy.flood_max = flood_max;
	policy.tx_delay_factor = tx_delay_factor;
	policy.direct_tx_delay_factor = direct_tx_delay_factor;
	meshcore_hal_test_node_runtime_policy_set(&policy);
}

static void set_peer_out_path(const uint8_t *public_key, const uint8_t *path,
			      uint8_t path_len, uint8_t path_hash_size)
{
	meshcore_hal_test_peer_path_set(public_key, path, path_len, path_hash_size);
}

static void set_peer_unknown_path(const uint8_t *public_key)
{
	meshcore_hal_test_peer_path_unknown_set(public_key);
}

static void calc_shared_secret(const struct meshcore_local_identity *local_identity,
			       const uint8_t *peer_public_key, uint8_t *out_secret)
{
	zassert_not_null(local_identity, "local identity is null");
	zassert_not_null(peer_public_key, "peer public key is null");
	zassert_not_null(out_secret, "out_secret is null");

	meshcore_local_identity_calc_shared_secret(local_identity, out_secret,
						   peer_public_key);
}

static runtime_fixture_data setup_runtime_host_fixture(void)
{
	runtime_fixture_data fixture = {};

	generate_identity(k_local_identity_seed, sizeof(k_local_identity_seed),
			  &fixture.local_identity);
	generate_identity(k_peer_identity_seed, sizeof(k_peer_identity_seed),
			  &fixture.peer_identity);
	install_node_config(&fixture.local_identity, "runtime_node");
	meshcore_hal_test_peer_identity_set(fixture.peer_identity.identity.pub_key,
					    "runtime_peer",
					    MESHCORE_COMMON_NODE_ROLE_CHAT, 0U);
	meshcore_hal_test_peer_path_set(fixture.peer_identity.identity.pub_key, NULL,
					0U, 1U);
	meshcore_hal_test_channel_secret_set(k_test_channel_secret_16,
					     sizeof(k_test_channel_secret_16));
	return fixture;
}

static meshcore_common_peer_identity_t fixture_peer_identity(
	const runtime_fixture_data &fixture)
{
	meshcore_common_peer_identity_t peer = {};

	snprintf(peer.name, sizeof(peer.name), "runtime_peer");
	peer.role = MESHCORE_COMMON_NODE_ROLE_CHAT;
	memcpy(peer.public_key, fixture.peer_identity.identity.pub_key,
	       sizeof(peer.public_key));
	return peer;
}

static bool read_last_sent_packet(struct meshcore_packet *packet)
{
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN];
	int len;

	zassert_not_null(packet, "packet is null");
	len = meshcore_hal_test_radio_get_last_send(raw, sizeof(raw));
	if (len <= 0) {
		return false;
	}

	meshcore_packet_init(packet);
	return meshcore_packet_read_from(packet, raw, (uint8_t)len);
}

static int decrypt_peer_datagram_payload(const runtime_fixture_data &fixture,
					 const struct meshcore_packet *packet,
					 uint8_t *out, size_t capacity)
{
	uint8_t secret[MESHCORE_PUBLIC_KEY_SIZE];
	int len;

	zassert_not_null(packet, "packet is null");
	zassert_not_null(out, "out is null");
	zassert_true(packet->payload_len >= 2U, "packet payload too short");

	calc_shared_secret(&fixture.local_identity, fixture.peer_identity.identity.pub_key,
			   secret);
	len = meshcore_utils_mac_then_decrypt(
		secret, out, &packet->payload[2], (int)(packet->payload_len - 2U));
	memset(secret, 0, sizeof(secret));
	zassert_true(len >= 0, "decrypt failed len=%d", len);
	zassert_true((size_t)len <= capacity, "decrypt overflow len=%d cap=%zu", len,
		     capacity);
	return len;
}

static int decrypt_anon_datagram_payload(const runtime_fixture_data &fixture,
					 const struct meshcore_packet *packet,
					 uint8_t *out, size_t capacity)
{
	const size_t encrypted_offset =
		MESHCORE_CHANNEL_HASH_BYTES + MESHCORE_PUBLIC_KEY_SIZE;
	uint8_t secret[MESHCORE_PUBLIC_KEY_SIZE];
	int len;

	zassert_not_null(packet, "packet is null");
	zassert_not_null(out, "out is null");
	zassert_true(packet->payload_len > encrypted_offset,
		     "anon packet payload too short");
	zassert_mem_equal(packet->payload,
			  fixture.peer_identity.identity.pub_key,
			  MESHCORE_CHANNEL_HASH_BYTES,
			  "anon packet dest hash mismatch");
	zassert_mem_equal(&packet->payload[MESHCORE_CHANNEL_HASH_BYTES],
			  fixture.local_identity.identity.pub_key,
			  MESHCORE_PUBLIC_KEY_SIZE,
			  "anon packet sender public key mismatch");

	calc_shared_secret(&fixture.peer_identity, fixture.local_identity.identity.pub_key,
			   secret);
	len = meshcore_utils_mac_then_decrypt(
		secret, out, &packet->payload[encrypted_offset],
		(int)(packet->payload_len - encrypted_offset));
	memset(secret, 0, sizeof(secret));
	zassert_true(len >= 0, "anon decrypt failed len=%d", len);
	zassert_true((size_t)len <= capacity, "anon decrypt overflow len=%d cap=%zu",
		     len, capacity);
	return len;
}

static void process_until_packets_sent(uint32_t expected_packets)
{
	uint32_t now_ms = meshcore_test_runtime_last_now_ms_get();
	int i;

	meshcore_hal_test_millis_set(now_ms);
	zassert_ok(meshcore_timer_fired(now_ms), "timer at t=%u failed",
		   (unsigned int)now_ms);

	for (i = 0; i < 24 &&
		    meshcore_hal_test_radio_get_packets_sent() < expected_packets;
	     i++) {
		if (meshcore_test_runtime_dispatcher_has_active_outbound()) {
			meshcore_hal_test_radio_on_send_finished();
			zassert_ok(meshcore_radio_tx_done(now_ms, true),
				   "tx_done at t=%u failed", (unsigned int)now_ms);
		}
		now_ms += 250U;
		meshcore_hal_test_millis_set(now_ms);
		zassert_ok(meshcore_timer_fired(now_ms), "timer at t=%u failed",
			   (unsigned int)now_ms);
	}
	if (meshcore_test_runtime_dispatcher_has_active_outbound()) {
		meshcore_hal_test_radio_on_send_finished();
		zassert_ok(meshcore_radio_tx_done(now_ms, true),
			   "final tx_done at t=%u failed", (unsigned int)now_ms);
	}

	zassert_equal(meshcore_hal_test_radio_get_packets_sent(), expected_packets,
		      "sent packet count mismatch sent=%u flood=%u direct=%u queued=%d active=%d last_len=%d",
		      (unsigned int)meshcore_hal_test_radio_get_packets_sent(),
		      (unsigned int)meshcore_test_runtime_dispatcher_num_sent_flood_get(),
		      (unsigned int)meshcore_test_runtime_dispatcher_num_sent_direct_get(),
		      meshcore_test_runtime_dispatcher_outbound_total_get(),
		      meshcore_test_runtime_dispatcher_has_active_outbound() ? 1 : 0,
		      meshcore_hal_test_radio_get_last_send_len());
}

static size_t build_raw_advert_packet_with_app_data(uint8_t *dest, size_t capacity,
						    const uint8_t *app_data,
						    size_t app_data_len,
						    uint8_t *out_public_key,
						    uint32_t timestamp)
{
	static const uint8_t k_rng_seed[MESHCORE_PUBLIC_KEY_SIZE] = {
		0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
		0x98, 0xa9, 0xba, 0xcb, 0xdc, 0xed, 0xfe, 0x0f,
		0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
		0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x12,
	};
	struct meshcore_packet_queue_manager packet_manager;
	struct meshcore_tables tables;
	struct meshcore_mesh mesh;
	struct meshcore_local_identity identity;
	struct meshcore_packet *packet;
	size_t raw_len;

	if (dest == NULL || capacity < 1U || app_data == NULL) {
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
	meshcore_hal_test_rtc_set_current_time(timestamp);
	meshcore_local_identity_generate(&identity);
	if (out_public_key != NULL) {
		memcpy(out_public_key, identity.identity.pub_key, MESHCORE_PUBLIC_KEY_SIZE);
	}

	packet = meshcore_mesh_create_advert(&mesh, &identity, app_data, app_data_len);
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
	return raw_len;
}

static size_t rewrite_raw_advert_route(uint8_t *raw, size_t raw_len,
				       uint8_t route_type, bool share)
{
	struct meshcore_packet packet;

	if (raw == nullptr || raw_len == 0U || raw_len > UINT8_MAX) {
		return 0U;
	}
	meshcore_packet_init(&packet);
	if (!meshcore_packet_read_from(&packet, raw, (uint8_t)raw_len) ||
	    meshcore_packet_get_payload_type(&packet) != PAYLOAD_TYPE_ADVERT) {
		return 0U;
	}

	packet.header &= (uint8_t)~PH_ROUTE_MASK;
	packet.header |= (route_type & PH_ROUTE_MASK);
	if (route_type == ROUTE_TYPE_TRANSPORT_FLOOD ||
	    route_type == ROUTE_TYPE_TRANSPORT_DIRECT) {
		packet.transport_codes[0] = share ? 0U : 0x1234U;
		packet.transport_codes[1] = share ? 0U : 0x5678U;
	}

	return meshcore_packet_write_to(&packet, raw);
}

static size_t build_raw_advert_packet(uint8_t *dest, size_t capacity)
{
	static const uint8_t k_app_data[] = { 0x41, 0x42, 0x43 };

	return build_raw_advert_packet_with_app_data(dest, capacity, k_app_data,
						    sizeof(k_app_data), nullptr, 1234U);
}

static void assert_runtime_skeleton_empty(void)
{
	zassert_equal(meshcore_test_runtime_expected_ack_used_count_get(), 0,
		      "expected ack table should be empty");
	zassert_false(meshcore_test_runtime_pending_discovery_is_valid(),
		      "pending discovery should be clear");
	zassert_false(meshcore_test_runtime_pending_trace_is_valid(),
		      "pending trace should be clear");
	zassert_false(meshcore_test_runtime_pending_telemetry_is_valid(),
		      "pending telemetry should be clear");
}

static void *meshcore_runtime_setup(void)
{
	reset_runtime_state();
	return NULL;
}

static void meshcore_runtime_before(void *fixture)
{
	ARG_UNUSED(fixture);
	reset_runtime_state();
}

static void meshcore_runtime_after(void *fixture)
{
	ARG_UNUSED(fixture);
	reset_runtime_state();
}

ZTEST(meshcore_runtime, test_timer_fired_fails_before_init)
{
	zassert_equal(meshcore_timer_fired(123U), -ENODEV,
		      "timer_fired should require init");
}

ZTEST(meshcore_runtime, test_init_rejects_double_init_and_deinit_allows_reinit)
{
	(void)setup_runtime_host_fixture();

	zassert_ok(meshcore_init(), "first init failed");
	zassert_true(meshcore_test_runtime_is_initialized(), "runtime should be initialized");
	zassert_true(meshcore_test_runtime_context_is_default(),
		     "singleton facade should use default context");
	assert_runtime_skeleton_empty();
	zassert_equal(meshcore_init(), -EALREADY, "double init should fail");

	meshcore_deinit();
	zassert_false(meshcore_test_runtime_is_initialized(),
		      "runtime should be deinitialized");
	zassert_true(meshcore_test_runtime_context_is_default(),
		     "deinit should keep singleton context selected");

	zassert_ok(meshcore_init(), "reinit after deinit failed");
	zassert_true(meshcore_test_runtime_is_initialized(),
		     "runtime should reinitialize cleanly");
	zassert_true(meshcore_test_runtime_context_is_default(),
		     "reinit should still use singleton context");
	assert_runtime_skeleton_empty();
}

ZTEST(meshcore_runtime, test_packet_manager_rejects_pool_larger_than_fixed_arena)
{
	struct meshcore_packet_queue_manager packet_manager = {};

	meshcore_packet_queue_manager_prepare(
		&packet_manager,
		(int)MESHCORE_PACKET_QUEUE_MANAGER_MAX_POOL_SIZE + 1);

	zassert_false(packet_manager.initialized,
		      "oversized fixed arena request must not initialize");
	zassert_equal(meshcore_packet_queue_manager_get_free_count(&packet_manager),
		      0, "uninitialized manager should not expose free packets");
	meshcore_packet_queue_manager_deinit(&packet_manager);
}

ZTEST(meshcore_runtime, test_timer_fired_accepts_non_decreasing_time_and_tracks_last_now)
{
	(void)setup_runtime_host_fixture();

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_timer_fired(10U), "timer at t=10 failed");
	zassert_equal(meshcore_test_runtime_last_now_ms_get(), 10U,
			      "last now mismatch after first timer");
	zassert_ok(meshcore_timer_fired(10U), "timer at same time failed");
	zassert_equal(meshcore_test_runtime_last_now_ms_get(), 10U,
		      "last now should remain same for equal time");
	zassert_ok(meshcore_timer_fired(25U), "timer at later time failed");
	zassert_equal(meshcore_test_runtime_last_now_ms_get(), 25U,
		      "last now mismatch after later process");
}

ZTEST(meshcore_runtime, test_timer_fired_accepts_32bit_tick_rollover)
{
	(void)setup_runtime_host_fixture();

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_timer_fired(UINT32_MAX - 5U), "timer before wrap failed");
	zassert_equal(meshcore_test_runtime_last_now_ms_get(), UINT32_MAX - 5U,
		      "last now mismatch before wrap");
	zassert_ok(meshcore_timer_fired(10U), "timer after wrap failed");
	zassert_equal(meshcore_test_runtime_last_now_ms_get(), 10U,
		      "last now should track wrapped time");
}

ZTEST(meshcore_runtime, test_timer_fired_uses_runtime_now_for_dispatcher_timebase)
{
	unsigned long next_floor_a;
	unsigned long next_floor_b;

	(void)setup_runtime_host_fixture();
	zassert_ok(meshcore_init(), "init failed");

	meshcore_hal_test_millis_set(5U);
	zassert_ok(meshcore_timer_fired(1000U), "timer failed");
	next_floor_a = meshcore_test_runtime_dispatcher_next_floor_calib_time_get();
	zassert_true(next_floor_a > 1000U,
		     "dispatcher should schedule a future floor calib time");

	meshcore_deinit();
	zassert_ok(meshcore_init(), "re-init failed");

	meshcore_hal_test_millis_set(123456U);
	zassert_ok(meshcore_timer_fired(1000U), "timer failed with different HAL millis");
	next_floor_b = meshcore_test_runtime_dispatcher_next_floor_calib_time_get();
	zassert_equal(next_floor_b, next_floor_a,
		      "dispatcher should schedule against process(now_ms), not HAL millis");
}

ZTEST(meshcore_runtime, test_init_uses_platform_timer_and_radio_send)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	uint8_t payload[] = { 0x61, 0x62, 0x63 };

	zassert_ok(meshcore_init(), "init failed");
	zassert_true(meshcore_hal_test_timer_arm_count_get() > 0U,
		     "init should arm initial runtime timer");

	zassert_equal(meshcore_message_send_to_node(
			      fixture.peer_identity.identity.pub_key, true, 1U,
			      payload, sizeof(payload)),
		      0, "send-to-node failed");
	zassert_ok(meshcore_timer_fired(1U), "timer_fired failed");
	zassert_true(meshcore_hal_test_radio_get_last_send_len() > 0,
		     "radio send hook should be used");
	zassert_ok(meshcore_radio_tx_done(25U, true), "tx_done failed");
}

ZTEST(meshcore_runtime, test_radio_rx_inject_requires_init)
{
	uint8_t raw[8] = { 0 };

	zassert_equal(meshcore_radio_rx_inject(raw, sizeof(raw), -45, 12, 0U),
		      -ENODEV, "rx inject should require init");
}

ZTEST(meshcore_runtime, test_radio_rx_inject_delivers_immediately_without_process)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN];
	static const uint8_t app_data[] = {
		(uint8_t)(ADV_NAME_MASK | ADV_TYPE_CHAT), 'r', 'x'
	};
	uint8_t advert_public_key[MESHCORE_PUBLIC_KEY_SIZE] = {0};
	size_t raw_len = build_raw_advert_packet_with_app_data(
		raw, sizeof(raw), app_data, sizeof(app_data), advert_public_key, 1775000600U);
	meshcore_hal_test_mesh_script_t *script = meshcore_hal_test_mesh_script_get();

	ARG_UNUSED(fixture);
	zassert_true(raw_len > 0U, "failed to build raw advert");
	zassert_ok(meshcore_init(), "init failed");

	zassert_ok(meshcore_radio_rx_inject(raw, raw_len, -45, 12, 0U),
		   "rx inject failed");
	zassert_equal(script->on_advert_recv_count, 1U,
		      "advert should be delivered by rx inject");
	zassert_equal(script->last_advert_timestamp, 1775000600U,
		      "advert timestamp mismatch");
	zassert_mem_equal(script->last_advert_public_key, advert_public_key,
			  sizeof(advert_public_key), "advert public key mismatch");
}

ZTEST(meshcore_runtime, test_chat_advert_receive_normalizes_raw_advert)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN];
	static const uint8_t app_data[] = {
		(uint8_t)(ADV_NAME_MASK | ADV_TYPE_CHAT), 'r', 'x'
	};
	uint8_t advert_public_key[MESHCORE_PUBLIC_KEY_SIZE] = {0};
	size_t raw_len = build_raw_advert_packet_with_app_data(
		raw, sizeof(raw), app_data, sizeof(app_data), advert_public_key,
		1775000600U);
	meshcore_test_advert_event_t event = {};
	struct meshcore_packet normalized;

	ARG_UNUSED(fixture);
	zassert_true(raw_len > 0U, "failed to build raw advert");
	zassert_ok(meshcore_init(), "init failed");

	zassert_ok(meshcore_radio_rx_inject(raw, raw_len, -45, 12, 0U),
		   "rx inject failed");
	zassert_true(meshcore_hal_test_advert_event_take(&event),
		     "advert event missing");
	zassert_equal(event.role, MESHCORE_COMMON_NODE_ROLE_CHAT,
		      "advert role mismatch");
	zassert_mem_equal(event.public_key, advert_public_key,
			  sizeof(advert_public_key), "advert public key mismatch");
	zassert_true(event.raw_advert_len > 0U, "raw advert missing");
	meshcore_packet_init(&normalized);
	zassert_true(meshcore_packet_read_from(&normalized, event.raw_advert,
					       event.raw_advert_len),
		     "normalized raw advert parse failed");
	zassert_equal(meshcore_packet_get_payload_type(&normalized),
		      PAYLOAD_TYPE_ADVERT, "normalized payload mismatch");
	zassert_equal(meshcore_packet_get_route_type(&normalized),
		      ROUTE_TYPE_FLOOD, "normalized raw advert route mismatch");
	zassert_false(meshcore_packet_has_transport_codes(&normalized),
		      "normalized raw advert should not keep transport codes");
}

ZTEST(meshcore_runtime, test_room_role_ignores_inbound_advert)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN];
	static const uint8_t app_data[] = {
		(uint8_t)(ADV_NAME_MASK | ADV_TYPE_CHAT), 'r', 'x'
	};
	size_t raw_len = build_raw_advert_packet_with_app_data(
		raw, sizeof(raw), app_data, sizeof(app_data), nullptr,
		1775000600U);
	meshcore_test_advert_event_t event = {};

	ARG_UNUSED(fixture);
	zassert_true(raw_len > 0U, "failed to build raw advert");
	set_node_role(MESHCORE_COMMON_NODE_ROLE_ROOM);
	zassert_ok(meshcore_init(), "init failed");

	zassert_ok(meshcore_radio_rx_inject(raw, raw_len, -45, 12, 0U),
		   "rx inject failed");
	zassert_equal(meshcore_hal_test_mesh_script_get()->on_advert_recv_count, 0U,
		      "room role should ignore advert");
	zassert_false(meshcore_hal_test_advert_event_take(&event),
		      "room role should not publish advert event");
}

ZTEST(meshcore_runtime, test_repeater_ignores_share_advert)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN];
	static const uint8_t app_data[] = {
		(uint8_t)(ADV_NAME_MASK | ADV_TYPE_REPEATER), 'r', 'p'
	};
	size_t raw_len = build_raw_advert_packet_with_app_data(
		raw, sizeof(raw), app_data, sizeof(app_data), nullptr,
		1775000600U);
	meshcore_test_advert_event_t event = {};

	ARG_UNUSED(fixture);
	zassert_true(raw_len > 0U, "failed to build raw advert");
	raw_len = rewrite_raw_advert_route(raw, raw_len,
					   ROUTE_TYPE_TRANSPORT_FLOOD, true);
	zassert_true(raw_len > 0U, "failed to rewrite share advert");
	set_node_role(MESHCORE_COMMON_NODE_ROLE_REPEATER);
	zassert_ok(meshcore_init(), "init failed");

	zassert_ok(meshcore_radio_rx_inject(raw, raw_len, -45, 12, 0U),
		   "rx inject failed");
	zassert_equal(meshcore_hal_test_mesh_script_get()->on_advert_recv_count, 0U,
		      "repeater should ignore Share advert");
	zassert_false(meshcore_hal_test_advert_event_take(&event),
		      "repeater Share should not publish advert event");
}

ZTEST(meshcore_runtime, test_repeater_accepts_zero_hop_repeater_advert)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN];
	static const uint8_t app_data[] = {
		(uint8_t)(ADV_NAME_MASK | ADV_TYPE_REPEATER), 'r', 'p'
	};
	uint8_t advert_public_key[MESHCORE_PUBLIC_KEY_SIZE] = {0};
	size_t raw_len = build_raw_advert_packet_with_app_data(
		raw, sizeof(raw), app_data, sizeof(app_data), advert_public_key,
		1775000600U);
	meshcore_test_advert_event_t event = {};

	ARG_UNUSED(fixture);
	zassert_true(raw_len > 0U, "failed to build raw advert");
	raw_len = rewrite_raw_advert_route(raw, raw_len, ROUTE_TYPE_DIRECT, false);
	zassert_true(raw_len > 0U, "failed to rewrite zero-hop advert");
	set_node_role(MESHCORE_COMMON_NODE_ROLE_REPEATER);
	zassert_ok(meshcore_init(), "init failed");

	zassert_ok(meshcore_radio_rx_inject(raw, raw_len, -45, 12, 0U),
		   "rx inject failed");
	zassert_true(meshcore_hal_test_advert_event_take(&event),
		     "repeater advert event missing");
	zassert_equal(event.role, MESHCORE_COMMON_NODE_ROLE_REPEATER,
		      "advert role mismatch");
	zassert_mem_equal(event.public_key, advert_public_key,
			  sizeof(advert_public_key), "advert public key mismatch");
	zassert_true(event.has_out_path, "zero-hop advert path metadata missing");
	zassert_equal(event.out_path_len, 0U, "zero-hop advert path mismatch");
}

ZTEST(meshcore_runtime, test_repeater_forwarding_policy_is_runtime_owned)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	meshcore_hal_test_mesh_script_t *script = meshcore_hal_test_mesh_script_get();
	struct meshcore_packet packet;

	ARG_UNUSED(fixture);

	set_node_role(MESHCORE_COMMON_NODE_ROLE_REPEATER);
	set_runtime_policy(false, false, 64U, 0.0f, 0.0f);
	zassert_ok(meshcore_init(), "init failed");

	meshcore_packet_init(&packet);
	packet.header = (uint8_t)((PAYLOAD_TYPE_RAW_CUSTOM << PH_TYPE_SHIFT) |
				  ROUTE_TYPE_FLOOD);
	meshcore_packet_set_path_hash_size_and_count(&packet, 1U, 1U);
	packet.path[0] = 0x42U;
	packet.payload[0] = 0xa5U;
	packet.payload_len = 1U;

	script->override_allow_packet_forward = true;
	script->allow_packet_forward_value = false;
	script->override_get_retransmit_delay = true;
	script->get_retransmit_delay_value = 999U;
	script->override_get_direct_retransmit_delay = true;
	script->get_direct_retransmit_delay_value = 999U;

	zassert_true(meshcore_mesh_runtime_allow_packet_forward(
			     &meshcore_runtime_context_get()->mesh, &packet),
		     "repeater should allow forwarding from runtime policy");
	zassert_equal(meshcore_mesh_runtime_get_retransmit_delay(
			      &meshcore_runtime_context_get()->mesh, &packet),
		      0U, "repeater flood delay should be runtime-owned");
	zassert_equal(meshcore_mesh_runtime_get_direct_retransmit_delay(
			      &meshcore_runtime_context_get()->mesh, &packet),
		      0U, "repeater direct delay should be runtime-owned");
}

ZTEST(meshcore_runtime, test_repeater_forwarding_policy_honors_flood_limit)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	meshcore_hal_test_mesh_script_t *script = meshcore_hal_test_mesh_script_get();
	struct meshcore_packet packet;

	ARG_UNUSED(fixture);

	set_node_role(MESHCORE_COMMON_NODE_ROLE_REPEATER);
	set_runtime_policy(false, false, 1U, 0.0f, 0.0f);
	zassert_ok(meshcore_init(), "init failed");

	meshcore_packet_init(&packet);
	packet.header = (uint8_t)((PAYLOAD_TYPE_RAW_CUSTOM << PH_TYPE_SHIFT) |
				  ROUTE_TYPE_FLOOD);
	meshcore_packet_set_path_hash_size_and_count(&packet, 1U, 1U);
	packet.path[0] = 0x42U;
	packet.payload[0] = 0xa5U;
	packet.payload_len = 1U;

	script->override_allow_packet_forward = true;
	script->allow_packet_forward_value = true;

	zassert_false(meshcore_mesh_runtime_allow_packet_forward(
			      &meshcore_runtime_context_get()->mesh, &packet),
		      "repeater should block flood packets at flood_max");
}

ZTEST(meshcore_runtime,
      test_repeater_loop_detect_treats_generic_transport_path_as_hashes)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	meshcore_common_node_runtime_policy_t policy = {};
	meshcore_hal_test_mesh_script_t *script = meshcore_hal_test_mesh_script_get();
	struct meshcore_packet packet;

	set_node_role(MESHCORE_COMMON_NODE_ROLE_REPEATER);
	policy.path_hash_size = 1U;
	policy.loop_detect = MESHCORE_COMMON_LOOP_DETECT_STRICT;
	policy.flood_max = 64U;
	meshcore_hal_test_node_runtime_policy_set(&policy);
	zassert_ok(meshcore_init(), "init failed");

	meshcore_packet_init(&packet);
	packet.header = (uint8_t)((PAYLOAD_TYPE_RAW_CUSTOM << PH_TYPE_SHIFT) |
				  ROUTE_TYPE_TRANSPORT_FLOOD);
	packet.transport_codes[0] = 0x1234U;
	packet.transport_codes[1] = 0x5678U;
	meshcore_packet_set_path_hash_size_and_count(&packet, 2U, 1U);
	packet.path[0] = fixture.local_identity.identity.pub_key[0];
	packet.path[1] = 0x7fU;
	packet.payload[0] = 0xa5U;
	packet.payload_len = 1U;

	script->override_allow_packet_forward = true;
	script->allow_packet_forward_value = true;

	zassert_true(meshcore_mesh_runtime_allow_packet_forward(
			     &meshcore_runtime_context_get()->mesh, &packet),
		     "generic transport path bytes should all participate in hash matching");
}

ZTEST(meshcore_runtime, test_chat_client_repeat_policy_is_role_gated_in_runtime)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	meshcore_hal_test_mesh_script_t *script = meshcore_hal_test_mesh_script_get();
	struct meshcore_packet packet;

	ARG_UNUSED(fixture);

	set_runtime_policy(true, false, 64U, 0.0f, 0.0f);
	zassert_ok(meshcore_init(), "init failed");

	meshcore_packet_init(&packet);
	packet.header = (uint8_t)((PAYLOAD_TYPE_RAW_CUSTOM << PH_TYPE_SHIFT) |
				  ROUTE_TYPE_DIRECT);
	packet.payload[0] = 0xa5U;
	packet.payload_len = 1U;

	script->override_allow_packet_forward = true;
	script->allow_packet_forward_value = false;
	script->override_get_direct_retransmit_delay = true;
	script->get_direct_retransmit_delay_value = 999U;

	zassert_true(meshcore_mesh_runtime_allow_packet_forward(
			     &meshcore_runtime_context_get()->mesh, &packet),
		     "chat client_repeat should allow forwarding");
	zassert_equal(meshcore_mesh_runtime_get_direct_retransmit_delay(
			      &meshcore_runtime_context_get()->mesh, &packet),
		      0U, "client-repeat direct delay should be runtime-owned");

	set_node_role(MESHCORE_COMMON_NODE_ROLE_ROOM);
	zassert_false(meshcore_mesh_runtime_allow_packet_forward(
			      &meshcore_runtime_context_get()->mesh, &packet),
		      "non-chat role should not client-repeat");
}

ZTEST(meshcore_runtime, test_request_apis_require_init)
{
	uint8_t raw_advert[8] = { 0x41, 0x42, 0x43, 0x44 };
	uint8_t payload[] = { 0x51, 0x52, 0x53 };

	zassert_equal(meshcore_node_advert_request(false), -ENODEV,
		      "node advert should require init");
	zassert_equal(meshcore_node_peer_advert_request(raw_advert, sizeof(raw_advert)),
		      -ENODEV, "peer advert should require init");
	zassert_equal(meshcore_message_send_to_node(k_test_public_key, false, 1U,
						      payload, sizeof(payload)),
		      -ENODEV, "send-to-node should require init");
	zassert_equal(meshcore_message_send_to_channel(k_test_channel_secret_16,
						       sizeof(k_test_channel_secret_16),
						       payload, sizeof(payload)),
		      -ENODEV, "send-to-channel should require init");
	zassert_equal(meshcore_node_discover_path_request(k_test_public_key, nullptr),
		      -ENODEV, "discover-path should require init");
	zassert_equal(meshcore_node_discover_request(MESHCORE_NODE_DISCOVER_FILTER_ALL,
						     true, 0U, nullptr),
		      -ENODEV, "node discover should require init");
	zassert_equal(meshcore_node_trace_request(k_trace_route_path,
						  sizeof(k_trace_route_path),
						  1U, nullptr),
		      -ENODEV, "trace should require init");
	zassert_equal(meshcore_node_telemetry_request(k_test_public_key, 0U, NULL), -ENODEV,
		      "telemetry should require init");
	zassert_equal(meshcore_node_binary_request(k_test_public_key, payload,
						     sizeof(payload)),
		      -ENODEV, "binary request should require init");
	zassert_equal(meshcore_node_binary_request_with_tag(k_test_public_key, payload,
							    sizeof(payload), 1U),
		      -ENODEV, "binary request with tag should require init");
	zassert_equal(meshcore_node_anon_data_send(k_test_public_key, payload,
					   sizeof(payload)),
		      -ENODEV, "anon data send should require init");
	zassert_equal(meshcore_node_anon_data_send_delayed(
			      k_test_public_key, payload, sizeof(payload), 300U),
		      -ENODEV, "delayed anon data should require init");
	zassert_equal(meshcore_node_anon_data_send_direct(
			      k_test_public_key, payload, sizeof(payload)),
		      -ENODEV, "direct anon data should require init");
	zassert_equal(meshcore_node_anon_data_send_direct_delayed(
			      k_test_public_key, payload, sizeof(payload), 300U),
		      -ENODEV, "delayed direct anon data should require init");
	zassert_equal(meshcore_node_anon_data_send_via_path(
			      k_test_public_key, payload, sizeof(payload),
			      k_trace_out_path, sizeof(k_trace_out_path), 1U),
		      -ENODEV, "explicit-path anon data should require init");
	zassert_equal(meshcore_node_anon_data_send_via_path_delayed(
			      k_test_public_key, payload, sizeof(payload),
			      k_trace_out_path, sizeof(k_trace_out_path), 1U, 300U),
		      -ENODEV, "delayed explicit-path anon data should require init");
	zassert_equal(meshcore_channel_data_send(k_test_channel_secret_16,
						 sizeof(k_test_channel_secret_16),
						 NULL, MESHCORE_OUT_PATH_UNKNOWN,
						 MESHCORE_CHANNEL_DATA_TYPE_DEV,
						 payload, sizeof(payload)),
		      -ENODEV, "channel data should require init");
	zassert_equal(meshcore_raw_data_send(k_trace_out_path, sizeof(k_trace_out_path),
					     payload, sizeof(payload)),
		      -ENODEV, "raw data should require init");
	zassert_equal(meshcore_control_data_send(payload, sizeof(payload)),
		      -ENODEV, "control data should require init");
}

ZTEST(meshcore_runtime, test_request_apis_validate_arguments_after_init)
{
	uint8_t raw_advert[8] = { 0x41, 0x42, 0x43, 0x44 };
	uint8_t text_payload[] = { 0x61, 0x62, 0x63 };
	uint8_t long_attempt_payload[MESHCORE_MAX_MESSAGE_TX_LEN - 1U] = { 0 };
	uint8_t binary_payload[] = { 0x71, 0x72, 0x73 };
	uint8_t overlong_path[MESHCORE_MAX_PATH_LEN] = { 0 };
	uint8_t invalid_permission_mask =
		(uint8_t)(MESHCORE_TELEM_PERM_ENVIRONMENT << 1);

	(void)setup_runtime_host_fixture();
	zassert_ok(meshcore_init(), "init failed");

	zassert_equal(meshcore_node_peer_advert_request(NULL, sizeof(raw_advert)), -EINVAL,
		      "NULL raw advert should fail");
	zassert_equal(meshcore_node_peer_advert_request(raw_advert, 0U), -EINVAL,
		      "empty raw advert should fail");
	zassert_equal(meshcore_node_peer_advert_request(raw_advert, sizeof(raw_advert)),
		      -EINVAL, "non-advert raw advert should fail");
	zassert_equal(meshcore_message_send_to_node(NULL, false, 1U,
						      text_payload, sizeof(text_payload)),
		      -EINVAL, "NULL public key should fail");
	zassert_equal(meshcore_message_send_to_node(k_test_public_key, false, 1U,
						      NULL, sizeof(text_payload)),
		      -EINVAL, "NULL text payload should fail");
	zassert_equal(meshcore_message_send_to_node(
			      k_test_public_key, false, 4U, long_attempt_payload,
			      sizeof(long_attempt_payload)),
		      -EINVAL, "extended attempt payload without tail room should fail");
	zassert_equal(meshcore_message_send_to_channel(NULL,
						       sizeof(k_test_channel_secret_16),
						       text_payload, sizeof(text_payload)),
		      -EINVAL, "NULL channel secret should fail");
	zassert_equal(meshcore_message_send_to_channel(k_test_channel_secret_16, 8U,
						       text_payload, sizeof(text_payload)),
		      -EINVAL, "invalid channel secret len should fail");
	zassert_equal(meshcore_message_send_to_channel(k_test_channel_secret_16,
						       sizeof(k_test_channel_secret_16),
						       NULL, sizeof(text_payload)),
		      -EINVAL, "NULL channel payload should fail");
	zassert_equal(meshcore_node_discover_path_request(NULL, nullptr), -EINVAL,
		      "NULL discover public key should fail");
	zassert_equal(meshcore_node_discover_request(0U, true, 0U, NULL),
		      -EINVAL, "empty node-discover filter should fail");
	zassert_equal(meshcore_node_trace_request(NULL, sizeof(k_trace_route_path),
						  1U, nullptr), -EINVAL,
		      "NULL trace path should fail");
	zassert_equal(meshcore_node_trace_request(k_trace_route_path, 0U, 1U, nullptr), -EINVAL,
		      "empty trace path should fail");
	zassert_equal(meshcore_node_trace_request(k_trace_route_path, 2U, 1U, nullptr), -EINVAL,
		      "even trace route should fail");
	zassert_equal(meshcore_node_trace_path_request(k_test_public_key, nullptr), -ENOTSUP,
		      "deprecated trace-path should not build host path");
	zassert_equal(meshcore_node_telemetry_request(NULL, 0U, NULL), -EINVAL,
		      "NULL telemetry public key should fail");
	zassert_equal(meshcore_node_telemetry_request(k_test_public_key,
						      invalid_permission_mask, NULL),
		      -EINVAL, "unsupported telemetry permission should fail");
	zassert_equal(meshcore_node_binary_request(NULL, binary_payload,
						     sizeof(binary_payload)),
		      -EINVAL, "NULL binary public key should fail");
	zassert_equal(meshcore_node_binary_request(k_test_public_key, NULL,
						     sizeof(binary_payload)),
		      -EINVAL, "NULL binary payload should fail");
	zassert_equal(meshcore_node_binary_request(k_test_public_key, binary_payload, 0U),
		      -EINVAL, "empty binary payload should fail");
	zassert_equal(meshcore_node_binary_request_with_tag(k_test_public_key, NULL,
							    sizeof(binary_payload), 1U),
		      -EINVAL, "NULL binary-with-tag payload should fail");
	zassert_equal(meshcore_node_anon_data_send(NULL, binary_payload,
						   sizeof(binary_payload)),
		      -EINVAL, "NULL anon public key should fail");
	zassert_equal(meshcore_node_anon_data_send(k_test_public_key, NULL,
						   sizeof(binary_payload)),
		      -EINVAL, "NULL anon payload should fail");
	zassert_equal(meshcore_node_anon_data_send(k_test_public_key, binary_payload, 0U),
		      -EINVAL, "empty anon payload should fail");
	zassert_equal(meshcore_node_anon_data_send_via_path(
			      k_test_public_key, binary_payload,
			      sizeof(binary_payload), overlong_path,
			      sizeof(overlong_path), 1U),
		      -EINVAL, "64 one-byte path hops should fail");
	zassert_equal(meshcore_node_anon_data_send_via_path(
			      k_test_public_key, binary_payload,
			      sizeof(binary_payload), NULL, 1U, 1U),
		      -EINVAL, "non-empty NULL explicit path should fail");
	zassert_equal(meshcore_node_anon_data_send_via_path(
			      k_test_public_key, binary_payload,
			      sizeof(binary_payload), k_trace_out_path,
			      sizeof(k_trace_out_path), 0U),
		      -EINVAL, "zero explicit path hash width should fail");
	zassert_equal(meshcore_channel_data_send(NULL, sizeof(k_test_channel_secret_16),
						 NULL, MESHCORE_OUT_PATH_UNKNOWN,
						 MESHCORE_CHANNEL_DATA_TYPE_DEV,
						 binary_payload, sizeof(binary_payload)),
		      -EINVAL, "NULL channel data secret should fail");
	zassert_equal(meshcore_raw_data_send(NULL, sizeof(k_trace_out_path),
					     binary_payload, sizeof(binary_payload)),
		      -EINVAL, "NULL raw data path should fail");
	zassert_equal(meshcore_control_data_send(binary_payload, sizeof(binary_payload)),
		      -EINVAL, "control data without high-bit type should fail");

	assert_runtime_skeleton_empty();
}

ZTEST(meshcore_runtime, test_request_apis_execute_successfully)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	uint8_t raw_advert[MESHCORE_MAX_TRANS_UNIT_LEN];
	uint8_t text_payload[] = { 0x61, 0x62, 0x63 };
	size_t raw_len;
	uint32_t discover_tag = 0x10203040U;
	uint32_t trace_tag = 0x50607080U;

	raw_len = build_raw_advert_packet(raw_advert, sizeof(raw_advert));
	zassert_true(raw_len > 0U, "failed to build raw advert");
	set_peer_out_path(fixture.peer_identity.identity.pub_key, k_trace_out_path,
			  sizeof(k_trace_out_path), 1U);

	zassert_ok(meshcore_init(), "init failed");
	zassert_equal(meshcore_node_advert_request(false), 0,
		      "node advert enqueue failed");
	zassert_equal(meshcore_node_peer_advert_request(raw_advert, raw_len), 0,
		      "peer advert enqueue failed");
	zassert_equal(meshcore_message_send_to_node(
			      fixture.peer_identity.identity.pub_key, false, 1U,
			      text_payload, sizeof(text_payload)),
		      0, "send-to-node enqueue failed");
	zassert_equal(meshcore_message_send_to_channel(
			      k_test_channel_secret_16,
			      sizeof(k_test_channel_secret_16), text_payload,
			      sizeof(text_payload)),
		      0, "send-to-channel enqueue failed");
	zassert_equal(meshcore_node_discover_path_request(
			      fixture.peer_identity.identity.pub_key, &discover_tag),
		      0, "discover-path enqueue failed");
	zassert_equal(discover_tag, 0x10203040U,
		      "discover request should preserve the exact caller tag");
	zassert_equal(meshcore_node_discover_request(MESHCORE_NODE_DISCOVER_FILTER_ALL,
						     true, 0U, nullptr),
		      0, "node discover enqueue failed");
	zassert_equal(meshcore_node_trace_request(
			      k_trace_route_path, sizeof(k_trace_route_path), 1U, &trace_tag),
		      0, "trace-path enqueue failed");
	zassert_equal(trace_tag, 0x50607080U,
		      "trace request should preserve the exact caller tag");
	zassert_equal(meshcore_node_telemetry_request(
			      fixture.peer_identity.identity.pub_key,
			      MESHCORE_TELEM_PERM_BASE |
				      MESHCORE_TELEM_PERM_LOCATION, NULL),
		      0, "telemetry enqueue failed");
	zassert_equal(meshcore_node_binary_request(
			      fixture.peer_identity.identity.pub_key,
			      k_test_binary_payload,
			      sizeof(k_test_binary_payload)),
		      0, "binary request enqueue failed");
	zassert_equal(meshcore_node_anon_data_send(
			      fixture.peer_identity.identity.pub_key,
			      k_test_binary_payload,
			      sizeof(k_test_binary_payload)),
		      0, "anon data enqueue failed");
	zassert_true(meshcore_test_runtime_dispatcher_outbound_total_get() > 0,
	     "requests should enter protocol-owned outbound queues");
}

ZTEST(meshcore_runtime, test_node_binary_request_sends_peer_req_datagram)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	struct meshcore_packet sent_packet;
	uint8_t decrypted[MESHCORE_PACKET_PAYLOAD_MAX_LEN];
	uint32_t tag = 0x01020304U;
	int decrypted_len;

	set_peer_out_path(fixture.peer_identity.identity.pub_key, k_trace_out_path,
			  sizeof(k_trace_out_path), 1U);
	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_binary_request(fixture.peer_identity.identity.pub_key,
						k_test_binary_payload,
						sizeof(k_test_binary_payload)),
		   "binary request enqueue failed");
	process_until_packets_sent(1U);

	zassert_true(read_last_sent_packet(&sent_packet), "failed to read sent packet");
	zassert_equal(meshcore_packet_get_payload_type(&sent_packet), PAYLOAD_TYPE_REQ,
		      "binary request should use REQ payload type");
	zassert_true(meshcore_packet_is_route_direct(&sent_packet),
		     "binary request should use direct route when out_path exists");
	zassert_equal(sent_packet.path_len, 2U, "binary request path_len mismatch");
	zassert_mem_equal(sent_packet.path, k_trace_out_path,
			  sizeof(k_trace_out_path), "binary request path mismatch");

	decrypted_len = decrypt_peer_datagram_payload(fixture, &sent_packet, decrypted,
						      sizeof(decrypted));
	zassert_true((size_t)decrypted_len >=
		     sizeof(tag) + sizeof(k_test_binary_payload),
		     "binary request decrypted len mismatch");
	memcpy(&tag, decrypted, sizeof(tag));
	zassert_equal(tag, 1U, "binary request tag should use unique RTC clock");
	zassert_mem_equal(&decrypted[sizeof(tag)], k_test_binary_payload,
			  sizeof(k_test_binary_payload),
			  "binary request payload mismatch");
	zassert_false(meshcore_test_runtime_pending_telemetry_is_valid(),
		      "generic binary request should not reuse telemetry pending state");
}

ZTEST(meshcore_runtime, test_node_anon_data_send_sends_anon_req_datagram)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	struct meshcore_packet sent_packet;
	uint8_t decrypted[MESHCORE_PACKET_PAYLOAD_MAX_LEN];
	int decrypted_len;

	set_peer_out_path(fixture.peer_identity.identity.pub_key, k_trace_out_path,
			  sizeof(k_trace_out_path), 1U);
	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_anon_data_send(fixture.peer_identity.identity.pub_key,
						k_test_binary_payload,
						sizeof(k_test_binary_payload)),
		   "anon data send failed");
	process_until_packets_sent(1U);

	zassert_true(read_last_sent_packet(&sent_packet), "failed to read sent packet");
	zassert_equal(meshcore_packet_get_payload_type(&sent_packet),
		      PAYLOAD_TYPE_ANON_REQ,
		      "anon data should use ANON_REQ payload type");
	zassert_true(meshcore_packet_is_route_direct(&sent_packet),
		     "anon data should use direct route when out_path exists");
	zassert_equal(sent_packet.path_len, sizeof(k_trace_out_path),
		      "anon data path_len mismatch");
	zassert_mem_equal(sent_packet.path, k_trace_out_path,
			  sizeof(k_trace_out_path), "anon data path mismatch");

	decrypted_len = decrypt_anon_datagram_payload(fixture, &sent_packet,
						      decrypted,
						      sizeof(decrypted));
	zassert_true((size_t)decrypted_len >= sizeof(k_test_binary_payload),
		      "anon data decrypted len mismatch");
	zassert_mem_equal(decrypted, k_test_binary_payload,
			  sizeof(k_test_binary_payload),
			  "anon data payload mismatch");
}

ZTEST(meshcore_runtime, test_node_anon_data_direct_and_explicit_route_policy)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	struct meshcore_packet sent_packet;
	static const uint8_t explicit_path[] = { 0x62U, 0x52U };

	set_peer_unknown_path(fixture.peer_identity.identity.pub_key);
	zassert_ok(meshcore_init(), "init failed");
	zassert_equal(meshcore_node_anon_data_send_direct(
			      fixture.peer_identity.identity.pub_key,
			      k_test_binary_payload,
			      sizeof(k_test_binary_payload)),
		      -ENOENT, "direct-only anon send must not flood");
	zassert_equal(meshcore_test_runtime_dispatcher_outbound_total_get(), 0,
		      "failed direct-only send queued a packet");

	zassert_ok(meshcore_node_anon_data_send_via_path(
			   fixture.peer_identity.identity.pub_key,
			   k_test_binary_payload, sizeof(k_test_binary_payload),
			   explicit_path, sizeof(explicit_path), 1U),
		   "explicit-path anon send failed");
	process_until_packets_sent(1U);
	zassert_true(read_last_sent_packet(&sent_packet),
		     "failed to read explicit-path packet");
	zassert_true(meshcore_packet_is_route_direct(&sent_packet),
		     "explicit path did not use direct routing");
	zassert_equal(meshcore_packet_get_path_hash_count(&sent_packet), 2U,
		      "explicit path hop count mismatch");
	zassert_mem_equal(sent_packet.path, explicit_path, sizeof(explicit_path),
			  "explicit path bytes mismatch");
}

ZTEST(meshcore_runtime, test_node_anon_data_delayed_variants_preserve_deadline)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	struct meshcore_packet sent_packet;
	static const uint8_t explicit_path[] = { 0x72U, 0x62U };

	set_peer_unknown_path(fixture.peer_identity.identity.pub_key);
	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_anon_data_send_delayed(
			   fixture.peer_identity.identity.pub_key,
			   k_test_binary_payload, sizeof(k_test_binary_payload), 300U),
		   "delayed flood anon send failed");
	meshcore_hal_test_millis_set(299U);
	zassert_ok(meshcore_timer_fired(299U), "timer before delay boundary failed");
	zassert_false(meshcore_test_runtime_dispatcher_has_active_outbound(),
		      "delayed flood started before deadline");
	meshcore_hal_test_millis_set(300U);
	zassert_ok(meshcore_timer_fired(300U), "timer at delay boundary failed");
	zassert_true(meshcore_test_runtime_dispatcher_has_active_outbound(),
		     "delayed flood did not start at deadline");
	meshcore_hal_test_radio_on_send_finished();
	zassert_ok(meshcore_radio_tx_done(300U, true),
		   "delayed flood completion failed");
	zassert_equal(meshcore_hal_test_radio_get_packets_sent(), 1U,
		      "delayed flood was not sent");
	zassert_true(read_last_sent_packet(&sent_packet), "missing delayed flood");
	zassert_true(meshcore_packet_is_route_flood(&sent_packet),
		     "unknown peer did not use flood fallback");

	reset_runtime_state();
	fixture = setup_runtime_host_fixture();
	set_peer_out_path(fixture.peer_identity.identity.pub_key, k_trace_out_path,
			  sizeof(k_trace_out_path), 1U);
	zassert_ok(meshcore_init(), "direct-delay init failed");
	zassert_ok(meshcore_node_anon_data_send_direct_delayed(
			   fixture.peer_identity.identity.pub_key,
			   k_test_binary_payload, sizeof(k_test_binary_payload), 300U),
		   "delayed direct anon send failed");
	meshcore_hal_test_millis_set(300U);
	zassert_ok(meshcore_timer_fired(300U), "direct delay timer failed");
	zassert_true(meshcore_test_runtime_dispatcher_has_active_outbound(),
		     "delayed direct packet did not start");
	meshcore_hal_test_radio_on_send_finished();
	zassert_ok(meshcore_radio_tx_done(300U, true),
		   "delayed direct completion failed");
	zassert_equal(meshcore_hal_test_radio_get_packets_sent(), 1U,
		      "delayed direct packet was not sent");
	zassert_true(read_last_sent_packet(&sent_packet), "missing delayed direct");
	zassert_true(meshcore_packet_is_route_direct(&sent_packet),
		     "known path did not use direct route");

	reset_runtime_state();
	fixture = setup_runtime_host_fixture();
	set_peer_unknown_path(fixture.peer_identity.identity.pub_key);
	zassert_ok(meshcore_init(), "explicit-delay init failed");
	zassert_ok(meshcore_node_anon_data_send_via_path_delayed(
			   fixture.peer_identity.identity.pub_key,
			   k_test_binary_payload, sizeof(k_test_binary_payload),
			   explicit_path, sizeof(explicit_path), 1U, 300U),
		   "delayed explicit-path anon send failed");
	meshcore_hal_test_millis_set(300U);
	zassert_ok(meshcore_timer_fired(300U), "explicit delay timer failed");
	zassert_true(meshcore_test_runtime_dispatcher_has_active_outbound(),
		     "delayed explicit-path packet did not start");
	meshcore_hal_test_radio_on_send_finished();
	zassert_ok(meshcore_radio_tx_done(300U, true),
		   "delayed explicit-path completion failed");
	zassert_equal(meshcore_hal_test_radio_get_packets_sent(), 1U,
		      "delayed explicit-path packet was not sent");
	zassert_true(read_last_sent_packet(&sent_packet),
		     "missing delayed explicit-path packet");
	zassert_mem_equal(sent_packet.path, explicit_path, sizeof(explicit_path),
			  "delayed explicit path bytes mismatch");
}

ZTEST(meshcore_runtime, test_node_discover_request_sends_zero_hop_control)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	struct meshcore_packet sent_packet;
	uint32_t tag = 0x11223344U;
	uint32_t since = 0x01020304U;

	ARG_UNUSED(fixture);
	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_discover_request(
			   MESHCORE_NODE_DISCOVER_FILTER_REPEATER, true, since, &tag),
		   "node discover request enqueue failed");
	process_until_packets_sent(1U);

	zassert_true(read_last_sent_packet(&sent_packet), "failed to read sent packet");
	zassert_equal(meshcore_packet_get_payload_type(&sent_packet),
		      PAYLOAD_TYPE_CONTROL, "node discover should use CONTROL");
	zassert_true(meshcore_packet_is_route_direct(&sent_packet),
		     "node discover should be zero-hop/direct");
	zassert_equal(meshcore_packet_get_path_hash_count(&sent_packet), 0U,
		      "node discover should be zero-hop");
	zassert_equal(sent_packet.payload_len, 10U,
		      "node discover payload len mismatch");
	zassert_equal(sent_packet.payload[0],
		      (uint8_t)(kCtlTypeNodeDiscoverReq | 0x01U),
		      "node discover request type mismatch");
	zassert_equal(sent_packet.payload[1],
		      MESHCORE_NODE_DISCOVER_FILTER_REPEATER,
		      "node discover filter mismatch");
	zassert_mem_equal(&sent_packet.payload[2], &tag, sizeof(tag),
			  "node discover tag mismatch");
	zassert_mem_equal(&sent_packet.payload[6], &since, sizeof(since),
			  "node discover since mismatch");
}

ZTEST(meshcore_runtime, test_send_to_node_zero_hop_peer_uses_direct_route)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	struct meshcore_packet sent_packet;
	uint8_t payload[] = { 0x61, 0x62, 0x63 };

	zassert_ok(meshcore_init(), "init failed");
	zassert_equal(meshcore_message_send_to_node(
			      fixture.peer_identity.identity.pub_key, false, 1U,
			      payload, sizeof(payload)),
		      0, "send-to-node enqueue failed");
	process_until_packets_sent(1U);

	zassert_true(read_last_sent_packet(&sent_packet), "failed to read sent packet");
	zassert_equal(meshcore_packet_get_payload_type(&sent_packet), PAYLOAD_TYPE_TXT_MSG,
		      "send-to-node should use TXT payload type");
	zassert_true(meshcore_packet_is_route_direct(&sent_packet),
		     "zero-hop peer should use direct route");
	zassert_equal(meshcore_packet_get_path_hash_count(&sent_packet), 0U,
		      "zero-hop direct should have no path hashes");
}

ZTEST(meshcore_runtime, test_send_to_node_known_routed_peer_uses_direct_route)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	struct meshcore_packet sent_packet;
	uint8_t payload[] = { 0x61, 0x62, 0x63 };

	set_peer_out_path(fixture.peer_identity.identity.pub_key, k_trace_out_path,
			  sizeof(k_trace_out_path), 1U);
	zassert_ok(meshcore_init(), "init failed");
	zassert_equal(meshcore_message_send_to_node(
			      fixture.peer_identity.identity.pub_key, false, 1U,
			      payload, sizeof(payload)),
		      0, "send-to-node enqueue failed");
	process_until_packets_sent(1U);

	zassert_true(read_last_sent_packet(&sent_packet), "failed to read sent packet");
	zassert_equal(meshcore_packet_get_payload_type(&sent_packet), PAYLOAD_TYPE_TXT_MSG,
		      "send-to-node should use TXT payload type");
	zassert_true(meshcore_packet_is_route_direct(&sent_packet),
		     "known routed peer should use direct route");
	zassert_false(meshcore_packet_has_transport_codes(&sent_packet),
		      "known routed peer message should not use transport direct");
	zassert_equal(meshcore_packet_get_path_hash_count(&sent_packet), 2U,
		      "known routed direct should keep path hashes");
	zassert_equal(meshcore_packet_get_path_hash_size(&sent_packet), 1U,
		      "known routed direct path hash size mismatch");
	zassert_mem_equal(sent_packet.path, k_trace_out_path,
			  sizeof(k_trace_out_path), "known routed direct path mismatch");
}

ZTEST(meshcore_runtime, test_send_to_node_unknown_empty_path_falls_back_to_flood)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	struct meshcore_packet sent_packet;
	uint8_t payload[] = { 0x61, 0x62, 0x63 };

	set_peer_unknown_path(fixture.peer_identity.identity.pub_key);
	zassert_ok(meshcore_init(), "init failed");
	zassert_equal(meshcore_message_send_to_node(
			      fixture.peer_identity.identity.pub_key, false, 1U,
			      payload, sizeof(payload)),
		      0, "send-to-node enqueue failed");
	process_until_packets_sent(1U);

	zassert_true(read_last_sent_packet(&sent_packet), "failed to read sent packet");
	zassert_equal(meshcore_packet_get_payload_type(&sent_packet), PAYLOAD_TYPE_TXT_MSG,
		      "send-to-node should use TXT payload type");
	zassert_true(meshcore_packet_is_route_flood(&sent_packet),
		     "unknown empty path should fall back to flood");
}

ZTEST(meshcore_runtime, test_node_binary_request_zero_hop_peer_uses_direct_route)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	struct meshcore_packet sent_packet;

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_binary_request(fixture.peer_identity.identity.pub_key,
						k_test_binary_payload,
						sizeof(k_test_binary_payload)),
		   "binary request enqueue failed");
	process_until_packets_sent(1U);

	zassert_true(read_last_sent_packet(&sent_packet), "failed to read sent packet");
	zassert_equal(meshcore_packet_get_payload_type(&sent_packet), PAYLOAD_TYPE_REQ,
		      "binary request should use REQ payload type");
	zassert_true(meshcore_packet_is_route_direct(&sent_packet),
		     "zero-hop peer should use direct route");
	zassert_equal(meshcore_packet_get_path_hash_count(&sent_packet), 0U,
		      "zero-hop direct should have no path hashes");
}

ZTEST(meshcore_runtime, test_node_binary_request_unknown_empty_path_falls_back_to_flood)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	struct meshcore_packet sent_packet;

	set_peer_unknown_path(fixture.peer_identity.identity.pub_key);
	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_binary_request(fixture.peer_identity.identity.pub_key,
						k_test_binary_payload,
						sizeof(k_test_binary_payload)),
		   "binary request enqueue failed");
	process_until_packets_sent(1U);

	zassert_true(read_last_sent_packet(&sent_packet), "failed to read sent packet");
	zassert_equal(meshcore_packet_get_payload_type(&sent_packet), PAYLOAD_TYPE_REQ,
		      "binary request should use REQ payload type");
	zassert_true(meshcore_packet_is_route_flood(&sent_packet),
		     "unknown empty path should fall back to flood");
}

ZTEST(meshcore_runtime, test_node_telemetry_request_uses_request_tag_and_direct_route)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	struct meshcore_packet sent_packet;
	uint8_t decrypted[MESHCORE_PACKET_PAYLOAD_MAX_LEN];
	uint32_t expected_tag = 0x55667788U;
	uint32_t actual_tag = 0U;
	int decrypted_len;

	set_peer_out_path(fixture.peer_identity.identity.pub_key, k_trace_out_path,
			  sizeof(k_trace_out_path), 1U);
	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_telemetry_request(
			   fixture.peer_identity.identity.pub_key,
			   MESHCORE_TELEM_PERM_BASE | MESHCORE_TELEM_PERM_LOCATION,
			   &expected_tag),
		   "telemetry request failed");
	process_until_packets_sent(1U);

	zassert_true(read_last_sent_packet(&sent_packet), "failed to read sent packet");
	zassert_equal(meshcore_packet_get_payload_type(&sent_packet), PAYLOAD_TYPE_REQ,
		      "telemetry request should use REQ payload type");
	zassert_true(meshcore_packet_is_route_direct(&sent_packet),
		     "telemetry request should use direct route when out_path exists");
	zassert_equal(sent_packet.path_len, 2U, "telemetry request path_len mismatch");
	zassert_mem_equal(sent_packet.path, k_trace_out_path,
			  sizeof(k_trace_out_path), "telemetry request path mismatch");

	decrypted_len = decrypt_peer_datagram_payload(fixture, &sent_packet, decrypted,
						      sizeof(decrypted));
	zassert_true((size_t)decrypted_len >= sizeof(actual_tag) + 9U,
		     "telemetry request decrypted len mismatch");
	memcpy(&actual_tag, decrypted, sizeof(actual_tag));
	zassert_equal(actual_tag, expected_tag,
		      "telemetry request should use request tag");
	zassert_equal(decrypted[sizeof(actual_tag)], kReqTypeTelemetry,
		      "telemetry request type mismatch");
	zassert_true(meshcore_test_runtime_pending_telemetry_is_valid(),
		     "telemetry request should register pending state");
}

ZTEST(meshcore_runtime, test_node_telemetry_request_unknown_empty_path_falls_back_to_flood)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	struct meshcore_packet sent_packet;

	set_peer_unknown_path(fixture.peer_identity.identity.pub_key);
	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_telemetry_request(
			   fixture.peer_identity.identity.pub_key,
			   MESHCORE_TELEM_PERM_BASE, NULL),
		   "telemetry request enqueue failed");
	process_until_packets_sent(1U);

	zassert_true(read_last_sent_packet(&sent_packet), "failed to read sent packet");
	zassert_equal(meshcore_packet_get_payload_type(&sent_packet), PAYLOAD_TYPE_REQ,
		      "telemetry request should use REQ payload type");
	zassert_true(meshcore_packet_is_route_flood(&sent_packet),
		     "unknown empty path should fall back to flood");
}

ZTEST(meshcore_runtime, test_node_binary_request_with_tag_uses_caller_tag)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	struct meshcore_packet sent_packet;
	uint8_t decrypted[MESHCORE_PACKET_PAYLOAD_MAX_LEN];
	const uint32_t expected_tag = 0x55667788U;
	uint32_t actual_tag = 0U;
	int decrypted_len;

	set_peer_out_path(fixture.peer_identity.identity.pub_key, k_trace_out_path,
			  sizeof(k_trace_out_path), 1U);
	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_binary_request_with_tag(
			   fixture.peer_identity.identity.pub_key,
			   k_test_binary_payload, sizeof(k_test_binary_payload),
			   expected_tag),
		   "binary request with tag failed");
	process_until_packets_sent(1U);

	zassert_true(read_last_sent_packet(&sent_packet), "failed to read sent packet");
	decrypted_len = decrypt_peer_datagram_payload(fixture, &sent_packet, decrypted,
						      sizeof(decrypted));
	zassert_true((size_t)decrypted_len >=
		     sizeof(actual_tag) + sizeof(k_test_binary_payload),
		     "binary request decrypted len mismatch");
	memcpy(&actual_tag, decrypted, sizeof(actual_tag));
	zassert_equal(actual_tag, expected_tag, "binary request should use caller tag");
	zassert_mem_equal(&decrypted[sizeof(actual_tag)], k_test_binary_payload,
			  sizeof(k_test_binary_payload),
			  "binary request payload mismatch");
}

ZTEST(meshcore_runtime, test_zero_request_tags_generate_like_reference)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	uint32_t request_tag = 0U;
	uint32_t pending_tag = 0U;

	set_peer_out_path(fixture.peer_identity.identity.pub_key, k_trace_out_path,
			  sizeof(k_trace_out_path), 1U);
	zassert_ok(meshcore_init(), "init failed");

	zassert_ok(meshcore_node_discover_path_request(
			   fixture.peer_identity.identity.pub_key, &request_tag),
		   "zero-tag discover request failed");
	zassert_not_equal(request_tag, 0U,
			  "discover should write back a generated tag");
	zassert_true(meshcore_test_runtime_pending_discovery_get(
			     &pending_tag, nullptr, nullptr),
		     "discover pending missing");
	zassert_equal(pending_tag, request_tag,
		      "discover pending tag mismatch");

	request_tag = 0U;
	zassert_ok(meshcore_node_trace_request(
			   k_trace_route_path, sizeof(k_trace_route_path), 1U,
			   &request_tag),
		   "zero-tag trace request failed");
	zassert_not_equal(request_tag, 0U,
			  "trace should write back a generated tag");
	zassert_true(meshcore_test_runtime_pending_trace_get(
			     &pending_tag, nullptr),
		     "trace pending missing");
	zassert_equal(pending_tag, request_tag, "trace pending tag mismatch");

	request_tag = 0U;
	zassert_ok(meshcore_node_telemetry_request(
			   fixture.peer_identity.identity.pub_key,
			   MESHCORE_TELEM_PERM_BASE, &request_tag),
		   "zero-tag telemetry request failed");
	zassert_not_equal(request_tag, 0U,
			  "telemetry should write back a generated tag");
	zassert_true(meshcore_test_runtime_pending_telemetry_get(
			     &pending_tag, nullptr, nullptr, nullptr),
		     "telemetry pending missing");
	zassert_equal(pending_tag, request_tag,
		      "telemetry pending tag mismatch");

	request_tag = 0U;
	zassert_ok(meshcore_node_discover_request(
			   MESHCORE_NODE_DISCOVER_FILTER_ALL, true, 0U,
			   &request_tag),
		   "zero-tag node discover request failed");
	zassert_not_equal(request_tag, 0U,
			  "node discover should write back a generated tag");

	zassert_ok(meshcore_node_binary_request_with_tag(
			   fixture.peer_identity.identity.pub_key,
			   k_test_binary_payload, sizeof(k_test_binary_payload), 0U),
		   "zero-tag binary request failed");
	zassert_true(meshcore_test_runtime_pending_binary_get(
			     &pending_tag, nullptr, nullptr),
		     "binary pending missing");
	zassert_not_equal(pending_tag, 0U,
			  "binary request should generate a non-zero tag");
}

ZTEST(meshcore_runtime, test_node_binary_response_matches_pending_tag)
{
	static const uint8_t k_response_payload[] = {0xa1, 0xa2, 0xa3};
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	struct meshcore_packet packet;
	meshcore_common_peer_identity_t sender = fixture_peer_identity(fixture);
	meshcore_test_binary_response_event_t event = {};
	uint8_t secret[MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t data[sizeof(uint32_t) + sizeof(k_response_payload)] = {};
	uint32_t expected_tag = 0x10203040U;
	uint32_t wrong_tag = 0x55667788U;

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_binary_request_with_tag(
			   fixture.peer_identity.identity.pub_key,
			   k_test_binary_payload, sizeof(k_test_binary_payload),
			   expected_tag),
		   "binary request with tag failed");
	process_until_packets_sent(1U);

	meshcore_packet_init(&packet);
	packet.header = (uint8_t)((PAYLOAD_TYPE_RESPONSE << PH_TYPE_SHIFT) |
				  ROUTE_TYPE_DIRECT);
	packet.snr_q4 = 6;
	calc_shared_secret(&fixture.local_identity,
			   fixture.peer_identity.identity.pub_key, secret);

	memcpy(data, &wrong_tag, sizeof(wrong_tag));
	memcpy(&data[sizeof(wrong_tag)], k_response_payload,
	       sizeof(k_response_payload));
	meshcore_runtime_on_peer_data_recv(&packet, PAYLOAD_TYPE_RESPONSE, &sender,
					   secret, data, sizeof(data));
	zassert_false(meshcore_hal_test_binary_response_event_take(NULL),
		      "wrong tag should not publish binary response");

	meshcore_hal_test_rtc_set_current_time(700U);
	memcpy(data, &expected_tag, sizeof(expected_tag));
	meshcore_runtime_on_peer_data_recv(&packet, PAYLOAD_TYPE_RESPONSE, &sender,
					   secret, data, sizeof(data));

	zassert_true(meshcore_hal_test_binary_response_event_take(&event),
		     "binary response publish missing");
	zassert_mem_equal(event.key_prefix, fixture.peer_identity.identity.pub_key,
			  sizeof(event.key_prefix), "binary key prefix mismatch");
	zassert_equal(event.timestamp, 700U, "binary response timestamp mismatch");
	zassert_equal(event.tag, expected_tag, "binary response tag mismatch");
	zassert_equal(event.payload_len, sizeof(k_response_payload),
		      "binary response payload len mismatch");
	zassert_mem_equal(event.payload, k_response_payload,
			  sizeof(k_response_payload),
			  "binary response payload mismatch");
}

ZTEST(meshcore_runtime, test_low_level_data_requests_emit_packets)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	struct meshcore_packet sent_packet;
	uint8_t payload[] = { 0x61, 0x62, 0x63 };
	uint8_t control_payload[] = { 0x80U, 0x01U, 0x02U };

	zassert_ok(meshcore_init(), "init failed");

	zassert_ok(meshcore_channel_data_send(
			   k_test_channel_secret_16, sizeof(k_test_channel_secret_16),
			   NULL, MESHCORE_OUT_PATH_UNKNOWN,
			   MESHCORE_CHANNEL_DATA_TYPE_DEV, payload, sizeof(payload)),
		   "channel data send failed");
	process_until_packets_sent(1U);
	zassert_true(read_last_sent_packet(&sent_packet), "failed to read channel packet");
	zassert_equal(meshcore_packet_get_payload_type(&sent_packet), PAYLOAD_TYPE_GRP_DATA,
		      "channel data should use GRP_DATA");
	zassert_true(meshcore_packet_is_route_flood(&sent_packet),
		     "channel data without path should flood");

	zassert_ok(meshcore_raw_data_send(k_trace_out_path, sizeof(k_trace_out_path),
					  payload, sizeof(payload)),
		   "raw data send failed");
	process_until_packets_sent(2U);
	zassert_true(read_last_sent_packet(&sent_packet), "failed to read raw packet");
	zassert_equal(meshcore_packet_get_payload_type(&sent_packet),
		      PAYLOAD_TYPE_RAW_CUSTOM, "raw data payload type mismatch");
	zassert_true(meshcore_packet_is_route_direct(&sent_packet),
		     "raw data should use direct route");
	zassert_equal(sent_packet.path_len, sizeof(k_trace_out_path), "raw path len");
	zassert_mem_equal(sent_packet.path, k_trace_out_path, sizeof(k_trace_out_path),
			  "raw path mismatch");
	zassert_mem_equal(sent_packet.payload, payload, sizeof(payload),
			  "raw payload mismatch");

	zassert_ok(meshcore_control_data_send(control_payload, sizeof(control_payload)),
		   "control data send failed");
	process_until_packets_sent(3U);
	zassert_true(read_last_sent_packet(&sent_packet), "failed to read control packet");
	zassert_equal(meshcore_packet_get_payload_type(&sent_packet), PAYLOAD_TYPE_CONTROL,
		      "control payload type mismatch");
	zassert_true(meshcore_packet_is_route_direct(&sent_packet),
		     "control data should use direct route");
	zassert_equal(meshcore_packet_get_path_hash_count(&sent_packet), 0U,
		      "control data should be zero-hop");
	zassert_mem_equal(sent_packet.payload, control_payload, sizeof(control_payload),
			  "control payload mismatch");
}

ZTEST(meshcore_runtime, test_packet_path_copy_returns_copied_byte_len)
{
	struct meshcore_packet packet;
	uint8_t path[MESHCORE_MAX_PATH_LEN] = {};

	meshcore_packet_init(&packet);
	meshcore_packet_set_path_hash_size_and_count(&packet, 2U, 1U);
	packet.path[0] = 0x12U;
	packet.path[1] = 0x34U;

	zassert_equal(meshcore_runtime_packet_path_copy(&packet, path, sizeof(path)), 2U,
		      "path copy should return copied byte length");
	zassert_mem_equal(path, packet.path, 2U, "path copy mismatch");
}

ZTEST(meshcore_runtime, test_packet_pool_full_returns_enobufs_without_request_queue)
{
	(void)setup_runtime_host_fixture();
	int i;

	zassert_ok(meshcore_init(), "init failed");

	for (i = 0; i < 10; i++) {
		zassert_ok(meshcore_node_advert_request(true),
			   "advert enqueue %d failed", i);
	}

	zassert_equal(meshcore_node_advert_request(true), -ENOBUFS,
	      "packet-pool exhaustion should fail");
}

ZTEST(meshcore_runtime, test_request_api_enters_protocol_queue_synchronously)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	uint8_t payload[] = { 0x61, 0x62, 0x63 };

	zassert_ok(meshcore_init(), "init failed");
	zassert_equal(meshcore_message_send_to_node(
			      fixture.peer_identity.identity.pub_key, true, 1U,
			      payload, sizeof(payload)),
		      0, "send-to-node enqueue failed");
	zassert_true(meshcore_test_runtime_dispatcher_outbound_total_get() > 0,
	     "protocol outbound queue should contain the request packet");

	zassert_ok(meshcore_timer_fired(0U), "timer failed");
}

ZTEST(meshcore_runtime,
      test_sync_request_recovers_after_packet_pool_is_released)
{
	(void)setup_runtime_host_fixture();
	uint32_t now_ms = 1U;
	int i;

	zassert_ok(meshcore_init(), "init failed");
	for (i = 0; i < 10; i++) {
		zassert_ok(meshcore_node_advert_request(true),
			   "advert enqueue %d failed", i);
	}

	zassert_equal(meshcore_node_advert_request(true),
		      -ENOBUFS, "pool exhaustion should reject sync request");

	zassert_ok(meshcore_timer_fired(now_ms), "timer failed at t=%u",
	   (unsigned int)now_ms);

	zassert_true(meshcore_hal_test_radio_get_last_send_len() > 0,
		      "one packet should be transmitted after radio recovers");
	meshcore_hal_test_radio_on_send_finished();
	zassert_ok(meshcore_radio_tx_done(now_ms, true), "tx_done failed");
	zassert_equal(meshcore_hal_test_radio_get_packets_sent(), 1U,
		      "one packet should complete after radio recovers");
	zassert_equal(meshcore_node_advert_request(true),
	      0, "sync request should succeed after pool recovers");
}

ZTEST(meshcore_runtime, test_active_tx_expires_on_timer_fired)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	uint8_t payload[] = { 0x61, 0x62, 0x63 };

	zassert_ok(meshcore_init(), "init failed");
	zassert_equal(meshcore_message_send_to_node(
	     fixture.peer_identity.identity.pub_key, true, 1U,
	     payload, sizeof(payload)),
	      0, "send-to-node enqueue failed");

	zassert_ok(meshcore_timer_fired(1U), "timer failed");
	zassert_true(meshcore_test_runtime_dispatcher_has_active_outbound(),
	     "send should be active");
	zassert_true(meshcore_hal_test_timer_arm_count_get() > 0U,
	     "runtime should arm at least one timer");
	zassert_ok(meshcore_timer_fired(1000U), "expiry timer failed");
	zassert_false(meshcore_test_runtime_dispatcher_has_active_outbound(),
	     "expired TX should be cleared");
}

ZTEST(meshcore_runtime, test_expected_ack_registers_hits_and_clears)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	uint8_t payload[] = { 0x61, 0x62, 0x63 };
	uint8_t target[MESHCORE_NODE_KEY_PREFIX_BYTES] = { 0 };
	meshcore_test_message_ack_event_t ack_event = {};
	uint32_t ack_crc = 0U;
	uint8_t attempt = 0U;

	zassert_ok(meshcore_init(), "init failed");
	zassert_equal(meshcore_message_send_to_node(
			      fixture.peer_identity.identity.pub_key, true, 7U,
			      payload, sizeof(payload)),
		      0, "send-to-node enqueue failed");
	zassert_ok(meshcore_timer_fired(0U), "process failed");
	zassert_equal(meshcore_test_runtime_expected_ack_used_count_get(), 1,
		      "expected ack should register");
	zassert_true(meshcore_test_runtime_expected_ack_peek(
			     &ack_crc, target, &attempt, nullptr),
		     "expected ack peek failed");
	zassert_mem_equal(target, fixture.peer_identity.identity.pub_key,
			  sizeof(target), "ack target mismatch");
	zassert_equal(attempt, 7U, "ack attempt mismatch");
	zassert_true(meshcore_test_runtime_simulate_ack_recv(ack_crc),
		     "ack hit should match");
	zassert_true(meshcore_hal_test_ack_event_take(&ack_event),
		     "ack publish not observed");
	zassert_mem_equal(ack_event.target,
			  fixture.peer_identity.identity.pub_key,
			  sizeof(ack_event.target),
			  "published ack target mismatch");
	zassert_equal(ack_event.attempt, 7U,
		      "published ack attempt mismatch");
	zassert_equal(meshcore_test_runtime_expected_ack_used_count_get(), 0,
		      "expected ack should clear after hit");
}

ZTEST(meshcore_runtime, test_expected_ack_timeout_clears_on_process)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	uint8_t payload[] = { 0x61, 0x62, 0x63 };
	meshcore_test_message_ack_event_t ack_event = {};
	unsigned long expires_at_ms = 0U;

	zassert_ok(meshcore_init(), "init failed");
	zassert_equal(meshcore_message_send_to_node(
			      fixture.peer_identity.identity.pub_key, true, 5U,
			      payload, sizeof(payload)),
		      0, "send-to-node enqueue failed");
	zassert_ok(meshcore_timer_fired(0U), "process failed");
	zassert_equal(meshcore_test_runtime_expected_ack_used_count_get(), 1,
		      "expected ack should register");
	zassert_true(meshcore_test_runtime_expected_ack_peek(
			     nullptr, nullptr, nullptr, &expires_at_ms),
		     "expected ack peek failed");

	meshcore_hal_test_millis_set(expires_at_ms + 1U);
	zassert_ok(meshcore_timer_fired((uint32_t)(expires_at_ms + 1U)),
		   "timeout process failed");
	zassert_equal(meshcore_test_runtime_expected_ack_used_count_get(), 0,
		      "expired ack should clear");
	zassert_false(meshcore_hal_test_ack_event_take(&ack_event),
		      "timeout should not publish ack");
}

ZTEST(meshcore_runtime, test_pending_discovery_hits_publish_and_clear)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	uint32_t tag = 0x01020304U;
	uint8_t key_prefix[MESHCORE_NODE_KEY_PREFIX_BYTES] = { 0 };
	meshcore_test_peer_path_event_t peer_path_event = {};
	static const int8_t k_invalid_extra[] = { 11, 12 };

	zassert_ok(meshcore_init(), "init failed");
	zassert_equal(meshcore_node_discover_path_request(
			      fixture.peer_identity.identity.pub_key, &tag),
		      0, "discover request enqueue failed");
	zassert_equal(tag, 0x01020304U,
		      "discover request should preserve the exact caller tag");
	zassert_ok(meshcore_timer_fired(0U), "process failed");
	zassert_true(meshcore_test_runtime_pending_discovery_get(
			     &tag, key_prefix, nullptr),
		     "pending discovery missing");
	zassert_false(meshcore_test_runtime_simulate_peer_path_recv(
			     key_prefix, k_trace_out_path, sizeof(k_trace_out_path),
			     1U, PAYLOAD_TYPE_ACK, k_invalid_extra,
			     ARRAY_SIZE(k_invalid_extra), nullptr, 0U, 9),
		      "discover should not accept an unrelated path extra");
	zassert_false(meshcore_hal_test_peer_path_event_take(nullptr),
		      "unrelated path extra should not publish");
	zassert_true(meshcore_test_runtime_pending_discovery_is_valid(),
		     "pending discovery should remain after unrelated path extra");
	zassert_true(meshcore_test_runtime_simulate_peer_path_recv(
			     key_prefix, k_trace_out_path, sizeof(k_trace_out_path),
			     1U, PAYLOAD_TYPE_RESPONSE, (const int8_t *)&tag,
			     sizeof(tag), nullptr, 0U, 9),
		     "tagged discover response should match");
	zassert_true(meshcore_hal_test_peer_path_event_take(&peer_path_event),
		     "peer-path publish not observed");
	zassert_true(peer_path_event.is_discover,
		     "peer-path event should be discover");
	zassert_equal(peer_path_event.tag, tag, "peer-path event tag mismatch");
	zassert_equal(peer_path_event.out_path_len,
		      sizeof(k_trace_out_path), "out path len mismatch");
	zassert_equal(peer_path_event.out_path_snr_count, 0U,
		      "discover should not publish out snrs");
	zassert_equal(peer_path_event.return_path_snr_count, 0U,
		      "discover should not publish return snrs");
	zassert_equal(meshcore_test_runtime_pending_discovery_is_valid(), false,
		      "pending discovery should clear");
}

ZTEST(meshcore_runtime, test_pending_trace_hits_publish_and_clear)
{
	uint32_t tag = 0xA1B2C3D4U;
	meshcore_test_trace_event_t trace_event = {};
	static const int8_t k_trace_snrs[] = { 1, 2, 3 };

	(void)setup_runtime_host_fixture();
	zassert_ok(meshcore_init(), "init failed");
	zassert_equal(meshcore_node_trace_request(
			      k_trace_route_path, sizeof(k_trace_route_path), 1U, &tag),
		      0, "trace request enqueue failed");
	zassert_equal(tag, 0xA1B2C3D4U,
		      "trace request should preserve the exact caller tag");
	zassert_ok(meshcore_timer_fired(0U), "process failed");
	zassert_true(meshcore_test_runtime_pending_trace_get(&tag, nullptr),
		     "pending trace missing");
	zassert_true(meshcore_test_runtime_simulate_trace_recv(
			     tag, 0U, k_trace_snrs, ARRAY_SIZE(k_trace_snrs), 4),
		     "trace hit should match");
	zassert_true(meshcore_hal_test_trace_event_take(&trace_event),
		     "trace publish not observed");
	zassert_equal(trace_event.state, 1U, "trace state mismatch");
	zassert_equal(trace_event.tag, tag, "trace event tag mismatch");
	zassert_equal(trace_event.out_path_snr_count, 2U,
		      "trace out snr count mismatch");
	zassert_equal(trace_event.out_path_snr[0], 1,
		      "trace out snr[0] mismatch");
	zassert_equal(trace_event.out_path_snr[1], 2,
		      "trace out snr[1] mismatch");
	zassert_equal(trace_event.return_path_snr_count, 2U,
		      "trace return snr count mismatch");
	zassert_equal(trace_event.return_path_snr[0], 3,
		      "trace return snr[0] mismatch");
	zassert_equal(trace_event.return_path_snr[1], 4,
		      "trace return snr[1] mismatch");
	zassert_false(meshcore_test_runtime_pending_trace_is_valid(),
		      "pending trace should clear");
}

ZTEST(meshcore_runtime, test_pending_telemetry_hits_timeout_and_clear)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	uint32_t tag = 0U;
	uint8_t key_prefix[MESHCORE_NODE_KEY_PREFIX_BYTES] = { 0 };
	uint8_t permission_mask = 0U;
	meshcore_test_telemetry_event_t telemetry_event = {};
	unsigned long expires_at_ms = 0U;
	uint8_t payload[] = { 0xaa, 0xbb, 0xcc };

	zassert_ok(meshcore_init(), "init failed");
	zassert_equal(meshcore_node_telemetry_request(
			      fixture.peer_identity.identity.pub_key,
			      MESHCORE_TELEM_PERM_BASE |
				      MESHCORE_TELEM_PERM_LOCATION, NULL),
		      0, "telemetry request enqueue failed");
	zassert_ok(meshcore_timer_fired(0U), "process failed");
	zassert_true(meshcore_test_runtime_pending_telemetry_get(
			     &tag, key_prefix, &permission_mask, nullptr),
		     "pending telemetry missing");
	zassert_equal(permission_mask,
		      (uint8_t)(MESHCORE_TELEM_PERM_BASE |
				MESHCORE_TELEM_PERM_LOCATION),
		      "permission mask mismatch");
	zassert_true(meshcore_test_runtime_simulate_telemetry_response_recv(
			     key_prefix, tag, payload, sizeof(payload)),
		     "telemetry hit should match");
	zassert_true(meshcore_hal_test_telemetry_event_take(&telemetry_event),
		     "telemetry publish not observed");
	zassert_mem_equal(telemetry_event.key_prefix, key_prefix,
			  sizeof(telemetry_event.key_prefix),
			  "telemetry key_prefix mismatch");
	zassert_equal(telemetry_event.tag, tag, "telemetry event tag mismatch");
	zassert_equal(telemetry_event.payload_len, sizeof(payload),
		      "telemetry payload_len mismatch");
	zassert_mem_equal(telemetry_event.payload, payload, sizeof(payload),
			  "telemetry payload mismatch");
	zassert_false(meshcore_test_runtime_pending_telemetry_is_valid(),
		      "pending telemetry should clear after hit");

	zassert_equal(meshcore_node_telemetry_request(
			      fixture.peer_identity.identity.pub_key,
			      MESHCORE_TELEM_PERM_BASE, NULL),
		      0, "second telemetry request enqueue failed");
	zassert_ok(meshcore_timer_fired(1U), "second process failed");
	zassert_true(meshcore_test_runtime_pending_telemetry_is_valid(),
		     "second telemetry pending should register");
	zassert_true(meshcore_test_runtime_pending_telemetry_get(
			     nullptr, nullptr, nullptr, &expires_at_ms),
		     "second telemetry pending peek failed");
	meshcore_hal_test_millis_set(expires_at_ms + 1U);
	zassert_ok(meshcore_timer_fired((uint32_t)(expires_at_ms + 1U)),
		   "telemetry timeout process failed");
	zassert_false(meshcore_test_runtime_pending_telemetry_is_valid(),
		      "telemetry timeout should clear pending");
}

ZTEST(meshcore_runtime,
      test_peer_plain_text_recv_publishes_message_and_sends_path_return_ack)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	struct meshcore_packet packet;
	struct meshcore_packet sent_packet;
	meshcore_test_message_event_t message_event = {};
	meshcore_test_message_ack_event_t ack_event = {};
	uint8_t secret[MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t data[16] = { 0 };
	uint8_t decrypted[MESHCORE_PACKET_PAYLOAD_MAX_LEN];
	uint32_t timestamp = 0x12345678U;
	meshcore_common_peer_identity_t sender = fixture_peer_identity(fixture);
	int decrypted_len;

	zassert_ok(meshcore_init(), "init failed");
	meshcore_packet_init(&packet);
	packet.header = (uint8_t)((PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT) |
				  ROUTE_TYPE_FLOOD);
	meshcore_packet_set_path_hash_size_and_count(&packet, 1U, 1U);
	packet.path[0] = 0x55U;
	packet.snr_q4 = 28;

	memcpy(data, &timestamp, sizeof(timestamp));
	data[4] = (uint8_t)(kTxtTypePlain << 2);
	memcpy(&data[5], "hello", 5U);
	calc_shared_secret(&fixture.local_identity, fixture.peer_identity.identity.pub_key,
			   secret);

	meshcore_runtime_on_peer_data_recv(&packet, PAYLOAD_TYPE_TXT_MSG, &sender, secret, data,
					   10U);
	process_until_packets_sent(1U);

	zassert_true(meshcore_hal_test_message_event_take(&message_event),
		     "message publish not observed");
	zassert_mem_equal(message_event.target,
			  fixture.peer_identity.identity.pub_key,
			  sizeof(message_event.target),
			  "message target mismatch");
	zassert_equal(message_event.route, MESHCORE_COMMON_MESSAGE_ROUTE_FLOOD,
		      "message route mismatch");
	zassert_equal(message_event.payload_len, 5U,
		      "message payload len mismatch");
	zassert_mem_equal(message_event.payload, "hello", 5U,
			  "message payload mismatch");
	zassert_false(meshcore_hal_test_ack_event_take(&ack_event),
		      "text receive should not publish host ACK event");
	zassert_true(read_last_sent_packet(&sent_packet), "failed to read sent packet");
	zassert_equal(meshcore_packet_get_payload_type(&sent_packet), PAYLOAD_TYPE_PATH,
		      "text reply should be PATH");
	zassert_true(meshcore_packet_is_route_flood(&sent_packet),
		     "text reply should flood");

	decrypted_len = decrypt_peer_datagram_payload(fixture, &sent_packet, decrypted,
						      sizeof(decrypted));
	zassert_equal(decrypted[0], 0x01U, "path-len field mismatch");
	zassert_equal(decrypted[1], 0x55U, "return path hash mismatch");
	zassert_equal(decrypted[2] & 0x0FU, PAYLOAD_TYPE_ACK,
		      "path extra type should be ACK");
	zassert_true(decrypted_len >= 7, "unexpected decrypted path payload len");
}

ZTEST(meshcore_runtime, test_peer_direct_text_recv_publishes_direct_route)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	struct meshcore_packet packet;
	struct meshcore_packet sent_packet;
	meshcore_test_message_event_t message_event = {};
	uint8_t secret[MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t data[16] = { 0 };
	uint32_t timestamp = 0x12345679U;
	meshcore_common_peer_identity_t sender = fixture_peer_identity(fixture);

	zassert_ok(meshcore_init(), "init failed");
	meshcore_packet_init(&packet);
	packet.header = (uint8_t)((PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT) |
				  ROUTE_TYPE_DIRECT);
	packet.snr_q4 = 20;

	memcpy(data, &timestamp, sizeof(timestamp));
	data[4] = (uint8_t)(kTxtTypePlain << 2);
	memcpy(&data[5], "direct", 6U);
	calc_shared_secret(&fixture.local_identity, fixture.peer_identity.identity.pub_key,
			   secret);

	meshcore_runtime_on_peer_data_recv(&packet, PAYLOAD_TYPE_TXT_MSG, &sender, secret, data,
					   11U);
	process_until_packets_sent(1U);

	zassert_true(meshcore_hal_test_message_event_take(&message_event),
		     "message publish not observed");
	zassert_equal(message_event.route, MESHCORE_COMMON_MESSAGE_ROUTE_DIRECT,
		      "message route mismatch");
	zassert_equal(message_event.payload_len, 6U,
		      "message payload len mismatch");
	zassert_mem_equal(message_event.payload, "direct", 6U,
			  "message payload mismatch");
	zassert_true(read_last_sent_packet(&sent_packet), "failed to read sent packet");
	zassert_true(meshcore_packet_is_route_direct(&sent_packet),
		     "direct text ACK should use direct route");
}

ZTEST(meshcore_runtime,
      test_telemetry_request_recv_sends_flood_path_return_response)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	struct meshcore_packet packet;
	struct meshcore_packet sent_packet;
	uint8_t secret[MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t data[6] = { 0 };
	uint8_t decrypted[MESHCORE_PACKET_PAYLOAD_MAX_LEN];
	uint32_t tag = 0x11223344U;
	meshcore_common_peer_identity_t sender = fixture_peer_identity(fixture);
	int decrypted_len;

	zassert_ok(meshcore_init(), "init failed");
	meshcore_packet_init(&packet);
	packet.header = (uint8_t)((PAYLOAD_TYPE_REQ << PH_TYPE_SHIFT) |
				  ROUTE_TYPE_FLOOD);
	meshcore_packet_set_path_hash_size_and_count(&packet, 1U, 1U);
	packet.path[0] = 0x66U;
	packet.snr_q4 = 16;

	memcpy(data, &tag, sizeof(tag));
	data[4] = 0x03U;
	data[5] = (uint8_t)~MESHCORE_TELEM_PERM_BASE;
	calc_shared_secret(&fixture.local_identity, fixture.peer_identity.identity.pub_key,
			   secret);

	meshcore_runtime_on_peer_data_recv(&packet, PAYLOAD_TYPE_REQ, &sender, secret, data,
					   sizeof(data));
	process_until_packets_sent(1U);

	zassert_true(read_last_sent_packet(&sent_packet), "failed to read sent packet");
	zassert_equal(meshcore_packet_get_payload_type(&sent_packet), PAYLOAD_TYPE_PATH,
		      "telemetry reply should be PATH");
	zassert_true(meshcore_packet_is_route_flood(&sent_packet),
		     "telemetry reply should flood");

	decrypted_len = decrypt_peer_datagram_payload(fixture, &sent_packet, decrypted,
						      sizeof(decrypted));
	zassert_equal(decrypted[0], 0x01U, "path-len field mismatch");
	zassert_equal(decrypted[1], 0x66U, "return path hash mismatch");
	zassert_equal(decrypted[2] & 0x0FU, PAYLOAD_TYPE_RESPONSE,
		      "path extra type should be RESPONSE");
	zassert_true(decrypted_len >= 8, "telemetry response payload too short");
	zassert_mem_equal(&decrypted[3], &tag, sizeof(tag), "telemetry tag mismatch");
	zassert_equal(decrypted[7], MESHCORE_TELEM_PERM_BASE,
		      "telemetry payload should come from shim");
}

static void assert_telemetry_request_recv_sends_response_for_role(
	meshcore_common_node_role_t role)
{
	runtime_fixture_data fixture;
	struct meshcore_packet packet;
	struct meshcore_packet sent_packet;
	uint8_t secret[MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t data[6] = { 0 };
	uint8_t decrypted[MESHCORE_PACKET_PAYLOAD_MAX_LEN];
	uint32_t tag = 0x21222324U;
	meshcore_common_peer_identity_t sender;
	int decrypted_len;

	reset_runtime_state();
	fixture = setup_runtime_host_fixture();
	sender = fixture_peer_identity(fixture);
	set_node_role(role);
	zassert_ok(meshcore_init(), "init failed");

	meshcore_packet_init(&packet);
	packet.header = (uint8_t)((PAYLOAD_TYPE_REQ << PH_TYPE_SHIFT) |
				  ROUTE_TYPE_FLOOD);
	meshcore_packet_set_path_hash_size_and_count(&packet, 1U, 1U);
	packet.path[0] = 0x88U;
	packet.snr_q4 = 12;

	memcpy(data, &tag, sizeof(tag));
	data[4] = kReqTypeTelemetry;
	data[5] = (uint8_t)~MESHCORE_TELEM_PERM_BASE;
	calc_shared_secret(&fixture.local_identity, fixture.peer_identity.identity.pub_key,
			   secret);

	meshcore_runtime_on_peer_data_recv(&packet, PAYLOAD_TYPE_REQ, &sender, secret, data,
					   sizeof(data));
	process_until_packets_sent(1U);

	zassert_true(read_last_sent_packet(&sent_packet),
		     "role should send telemetry response");
	zassert_equal(meshcore_packet_get_payload_type(&sent_packet), PAYLOAD_TYPE_PATH,
		      "telemetry reply should be PATH");
	zassert_true(meshcore_packet_is_route_flood(&sent_packet),
		     "telemetry reply should flood");

	decrypted_len = decrypt_peer_datagram_payload(fixture, &sent_packet, decrypted,
						      sizeof(decrypted));
	zassert_true(decrypted_len >= 8, "telemetry response payload too short");
	zassert_equal(decrypted[2] & 0x0FU, PAYLOAD_TYPE_RESPONSE,
		      "telemetry path extra should be RESPONSE");
	zassert_mem_equal(&decrypted[3], &tag, sizeof(tag), "telemetry tag mismatch");
	zassert_equal(decrypted[7], MESHCORE_TELEM_PERM_BASE,
		      "telemetry payload should come from shim");
}

ZTEST(meshcore_runtime,
      test_non_chat_telemetry_responder_roles_send_response)
{
	assert_telemetry_request_recv_sends_response_for_role(
		MESHCORE_COMMON_NODE_ROLE_REPEATER);
	assert_telemetry_request_recv_sends_response_for_role(
		MESHCORE_COMMON_NODE_ROLE_ROOM);
	assert_telemetry_request_recv_sends_response_for_role(
		MESHCORE_COMMON_NODE_ROLE_SENSOR);
}

ZTEST(meshcore_runtime,
      test_discover_style_req_without_snr_routes_as_telemetry_response)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	struct meshcore_packet packet;
	struct meshcore_packet sent_packet;
	uint8_t secret[MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t data[6] = { 0 };
	uint8_t decrypted[MESHCORE_PACKET_PAYLOAD_MAX_LEN];
	uint32_t tag = 0x55667788U;
	uint8_t req_wire_permission_mask = (uint8_t)~MESHCORE_TELEM_PERM_BASE;
	uint8_t expected_permission_mask = MESHCORE_TELEM_PERM_BASE;
	meshcore_common_peer_identity_t sender = fixture_peer_identity(fixture);
	int decrypted_len;

	zassert_ok(meshcore_init(), "init failed");
	meshcore_packet_init(&packet);
	packet.header = (uint8_t)((PAYLOAD_TYPE_REQ << PH_TYPE_SHIFT) |
				  ROUTE_TYPE_FLOOD);
	meshcore_packet_set_path_hash_size_and_count(&packet, 1U, 1U);
	packet.path[0] = 0x77U;
	packet.snr_q4 = 8;

	memcpy(data, &tag, sizeof(tag));
	data[4] = 0x03U;
	data[5] = req_wire_permission_mask;
	calc_shared_secret(&fixture.local_identity,
			   fixture.peer_identity.identity.pub_key, secret);

	meshcore_runtime_on_peer_data_recv(&packet, PAYLOAD_TYPE_REQ, &sender, secret, data,
					   sizeof(data));
	process_until_packets_sent(1U);

	zassert_true(read_last_sent_packet(&sent_packet), "failed to read sent packet");
	zassert_equal(meshcore_packet_get_payload_type(&sent_packet), PAYLOAD_TYPE_PATH,
		      "discover-style req reply should be PATH");
	zassert_true(meshcore_packet_is_route_flood(&sent_packet),
		     "discover-style req reply should flood");

	decrypted_len = decrypt_peer_datagram_payload(fixture, &sent_packet, decrypted,
						      sizeof(decrypted));
	zassert_true(decrypted_len >= 8, "response payload too short");
	zassert_equal(decrypted[0], 0x01U, "path-len field mismatch");
	zassert_equal(decrypted[1], 0x77U, "return path hash mismatch");
	zassert_equal(decrypted[2] & 0x0FU, PAYLOAD_TYPE_RESPONSE,
		      "non-SNR discover-style req should encode RESPONSE extra");
	zassert_mem_equal(&decrypted[3], &tag, sizeof(tag), "response tag mismatch");
	zassert_equal(decrypted[7], expected_permission_mask,
		      "response payload should keep decoded permission mask");
}

ZTEST(meshcore_runtime,
      test_control_discover_request_recv_sends_zero_hop_local_role_response)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	struct meshcore_packet packet;
	struct meshcore_packet sent_packet;
	uint32_t tag = 0x55667788U;

	meshcore_hal_test_rtc_set_current_time(600U);
	set_node_role(MESHCORE_COMMON_NODE_ROLE_SENSOR);
	zassert_ok(meshcore_init(), "init failed");

	meshcore_packet_init(&packet);
	packet.header = (uint8_t)((PAYLOAD_TYPE_CONTROL << PH_TYPE_SHIFT) |
				  ROUTE_TYPE_DIRECT);
	packet.payload[0] = (uint8_t)(kCtlTypeNodeDiscoverReq | 0x01U);
	packet.payload[1] = MESHCORE_NODE_DISCOVER_FILTER_SENSOR;
	memcpy(&packet.payload[2], &tag, sizeof(tag));
	packet.payload_len = 10U;
	packet.snr_q4 = 12;

	meshcore_runtime_on_control_data_recv(&packet);
	process_until_packets_sent(1U);

	zassert_true(read_last_sent_packet(&sent_packet), "failed to read sent packet");
	zassert_equal(meshcore_packet_get_payload_type(&sent_packet), PAYLOAD_TYPE_CONTROL,
		      "discover reply should be CONTROL");
	zassert_true(meshcore_packet_is_route_direct(&sent_packet),
		     "discover reply should be zero-hop/direct");
	zassert_equal(meshcore_packet_get_path_hash_count(&sent_packet), 0U,
		      "discover reply should be zero-hop");
	zassert_true(sent_packet.payload_len >= 14U, "discover reply payload too short");
	zassert_equal(sent_packet.payload[0],
		      (uint8_t)(kCtlTypeNodeDiscoverResp | kAdvTypeSensor),
		      "discover reply type mismatch");
	zassert_equal(sent_packet.payload[1], (uint8_t)packet.snr_q4,
		      "discover reply snr mismatch");
	zassert_mem_equal(&sent_packet.payload[2], &tag, sizeof(tag),
			  "discover reply tag mismatch");
	zassert_mem_equal(&sent_packet.payload[6], fixture.local_identity.identity.pub_key,
			  8U, "discover reply prefix mismatch");
}

ZTEST(meshcore_runtime, test_control_discover_response_publishes_typed_event)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	struct meshcore_packet packet;
	meshcore_test_node_discover_event_t event = {};
	uint32_t tag = 0x55667788U;
	static const uint8_t k_prefix[MESHCORE_NODE_DISCOVER_PUBLIC_KEY_PREFIX_BYTES] = {
		0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8,
	};

	ARG_UNUSED(fixture);
	zassert_ok(meshcore_init(), "init failed");

	meshcore_packet_init(&packet);
	packet.header = (uint8_t)((PAYLOAD_TYPE_CONTROL << PH_TYPE_SHIFT) |
				  ROUTE_TYPE_DIRECT);
	meshcore_packet_set_path_hash_size_and_count(&packet, 1U, 1U);
	packet.path[0] = 0x42U;
	packet.payload[0] = (uint8_t)(kCtlTypeNodeDiscoverResp | kAdvTypeRepeater);
	packet.payload[1] = (uint8_t)0xf4U;
	memcpy(&packet.payload[2], &tag, sizeof(tag));
	memcpy(&packet.payload[6], k_prefix, sizeof(k_prefix));
	packet.payload_len = 6U + sizeof(k_prefix);
	packet.snr_q4 = 9;

	meshcore_runtime_on_control_data_recv(&packet);

	zassert_true(meshcore_hal_test_node_discover_event_take(&event),
		     "node discover event missing");
	zassert_equal(event.role, MESHCORE_COMMON_NODE_ROLE_REPEATER,
		      "node discover role mismatch");
	zassert_equal(event.tag, tag, "node discover tag mismatch");
	zassert_equal(event.public_key_len, sizeof(k_prefix),
		      "node discover key len mismatch");
	zassert_mem_equal(event.public_key, k_prefix, sizeof(k_prefix),
			  "node discover key prefix mismatch");
	zassert_equal(event.path_len, 1U, "node discover path len mismatch");
	zassert_equal(event.path[0], 0x42U, "node discover path mismatch");
	zassert_equal(event.uplink_snr, (int8_t)0xf4,
		      "node discover uplink snr mismatch");
	zassert_equal(event.downlink_snr, packet.snr_q4,
		      "node discover downlink snr mismatch");
}

ZTEST(meshcore_runtime, test_deinit_and_reinit_clear_runtime_skeleton_state)
{
	runtime_fixture_data fixture = setup_runtime_host_fixture();
	uint8_t payload[] = { 0x61, 0x62, 0x63 };

	zassert_ok(meshcore_init(), "init failed");
	zassert_equal(meshcore_message_send_to_node(
			      fixture.peer_identity.identity.pub_key, true, 1U,
			      payload, sizeof(payload)),
		      0, "send-to-node failed");
	zassert_ok(meshcore_timer_fired(0U), "timer failed");
	zassert_equal(meshcore_test_runtime_expected_ack_used_count_get(), 1,
		      "expected ack should be registered before deinit");

	meshcore_deinit();
	zassert_false(meshcore_test_runtime_is_initialized(),
		      "runtime should be deinitialized");

	zassert_ok(meshcore_init(), "reinit failed");
	assert_runtime_skeleton_empty();
}

ZTEST_SUITE(meshcore_runtime, NULL, meshcore_runtime_setup, meshcore_runtime_before,
	    meshcore_runtime_after, NULL);
