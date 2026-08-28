// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#ifndef FOBE_TESTS_LIB_MESHCORE_BLACKBOX_RUNTIME_ORACLE_SRC_TEST_SUPPORT_H_
#define FOBE_TESTS_LIB_MESHCORE_BLACKBOX_RUNTIME_ORACLE_SRC_TEST_SUPPORT_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/ztest.h>

extern "C" {
#include "meshcore/runtime.h"
#include "meshcore_test_runtime.h"
#include "meshcore_packet.h"
#include "meshcore_identity.h"
}

#include "reference_chat_harness.h"

struct runtime_fixture_data {
	struct meshcore_local_identity local_identity;
	struct meshcore_local_identity peer_identity;
	struct meshcore_local_identity inbound_identity;
};

extern const uint8_t k_local_identity_seed[MESHCORE_PUBLIC_KEY_SIZE];
extern const uint8_t k_peer_identity_seed[MESHCORE_PUBLIC_KEY_SIZE];
extern const uint8_t k_channel_secret_16[16];
extern const uint8_t k_direct_out_path[1];
extern const uint8_t k_trace_out_path[2];
extern const uint8_t k_trace_hash3_out_path[6];

void reset_target_runtime_state(void);
runtime_fixture_data setup_target_runtime_fixture(void);
void setup_reference_fixture(ReferenceChatHarness *oracle,
			     const runtime_fixture_data *fixture);
void clear_target_publish_events(void);
void target_set_peer_out_path(const uint8_t *public_key, const uint8_t *path,
			      uint8_t path_len, uint8_t path_hash_size);
void target_set_peer_path_unknown(const uint8_t *public_key);
void target_set_request_rng_bytes(const uint8_t *bytes, size_t len);
void target_process_until_packets_sent(uint32_t expected_packets);
int target_radio_packet_inject(const uint8_t *raw, size_t raw_len, int16_t rssi_dbm,
			       int8_t snr_db, uint32_t delay_ms);
bool target_await_peer_path_event(meshcore_test_peer_path_event_t *out,
				      int32_t timeout_ms);
bool target_await_advert_event(meshcore_test_advert_event_t *out,
			       int32_t timeout_ms);
bool target_await_trace_event(meshcore_test_trace_event_t *out,
			      int32_t timeout_ms);
bool target_await_telemetry_event(meshcore_test_telemetry_event_t *out,
				   int32_t timeout_ms);
uint32_t target_advert_recv_count(void);
bool capture_last_target_packet(ReferenceChatHarness::observed_packet *out);
size_t build_target_raw_advert_packet(uint8_t *dest, size_t capacity);
void assert_peer_path_events_equal(
	const meshcore_test_peer_path_event_t *target,
	const ReferenceChatHarness::observed_peer_path_event *oracle,
	const char *context);
void assert_trace_events_equal(
	const meshcore_test_trace_event_t *target,
	const ReferenceChatHarness::observed_trace_event *oracle,
	const char *context);
void assert_telemetry_events_equal(
	const meshcore_test_telemetry_event_t *target,
	const ReferenceChatHarness::observed_telemetry_event *oracle,
	const char *context);
void assert_advert_events_equal(
	const meshcore_test_advert_event_t *target,
	const ReferenceChatHarness::observed_advert_event *oracle,
	const char *context);
void assert_packets_equal(const ReferenceChatHarness::observed_packet *target,
			  const ReferenceChatHarness::observed_packet *oracle,
			  const char *context);
void assert_packet_behavior(const ReferenceChatHarness::observed_packet *packet,
			    ReferenceChatHarness::route_kind route,
			    bool has_transport_codes, uint8_t payload_type,
			    const char *context);

#endif /* FOBE_TESTS_LIB_MESHCORE_BLACKBOX_RUNTIME_ORACLE_SRC_TEST_SUPPORT_H_ */
