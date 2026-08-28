// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "case_helpers.h"

ZTEST(meshcore_runtime_oracle, test_telemetry_response_publish_matches_oracle)
{
	static const uint8_t k_rng[] = {0x81, 0x82, 0x83, 0x84};
	static const uint8_t k_payload[] = {0xaa, 0xbb, 0xcc};
	const uint8_t permission_mask =
		MESHCORE_TELEM_PERM_BASE | MESHCORE_TELEM_PERM_LOCATION;
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_telemetry_event_t target_event = {};
	ReferenceChatHarness::observed_telemetry_event oracle_event = {};
	uint32_t target_tag = 0U;

	setup_reference_fixture(&oracle, &fixture);
	target_set_request_rng_bytes(k_rng, sizeof(k_rng));
	oracle.set_random_bytes(k_rng, sizeof(k_rng));

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_telemetry_request(
			   fixture.peer_identity.identity.pub_key, permission_mask, NULL),
		   "target telemetry enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(meshcore_test_runtime_pending_telemetry_get(
			     &target_tag, nullptr, nullptr, nullptr),
		     "target pending telemetry missing");
	zassert_true(oracle.send_telemetry_request(
			     fixture.peer_identity.identity.pub_key, permission_mask),
		     "oracle telemetry failed");
	zassert_true(oracle.drive_until_packets_sent(1U),
		     "oracle telemetry send failed");

	clear_target_publish_events();
	zassert_true(meshcore_test_runtime_simulate_telemetry_response_recv(
			     fixture.peer_identity.identity.pub_key, target_tag,
			     k_payload, sizeof(k_payload)),
		     "target telemetry simulation failed");
	zassert_true(oracle.inject_telemetry_response(
			     fixture.peer_identity.identity.pub_key, k_payload,
			     sizeof(k_payload), 0),
		     "oracle telemetry inject failed");

	zassert_true(target_await_telemetry_event(&target_event, 100),
		     "target telemetry publish missing");
	zassert_true(oracle.capture_last_telemetry_event(&oracle_event),
		     "oracle telemetry publish missing");
	zassert_false(meshcore_test_runtime_pending_telemetry_is_valid(),
		      "target pending telemetry should clear");
	zassert_false(oracle.pending_telemetry_active(),
		      "oracle pending telemetry should clear");
	assert_telemetry_events_equal(&target_event, &oracle_event,
				      "telemetry response publish");
}

ZTEST(meshcore_runtime_oracle, test_pending_timeout_clears_all_like_oracle)
{
	static const uint8_t k_rng[] = {0x11, 0x22, 0x33, 0x44};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	unsigned long discover_expires_at = 0U;
	unsigned long trace_expires_at = 0U;
	unsigned long telemetry_expires_at = 0U;
	unsigned long target_advance_ms = 0U;
	unsigned long oracle_advance_ms = 0U;

	setup_reference_fixture(&oracle, &fixture);
	target_set_peer_out_path(fixture.peer_identity.identity.pub_key, k_trace_out_path,
				 sizeof(k_trace_out_path), 1U);
	zassert_true(oracle.set_contact_out_path(fixture.peer_identity.identity.pub_key,
						 k_trace_out_path,
						 sizeof(k_trace_out_path), 1U),
		     "oracle trace path install failed");
	target_set_request_rng_bytes(k_rng, sizeof(k_rng));
	oracle.set_random_bytes(k_rng, sizeof(k_rng));

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_discover_path_request(
				   fixture.peer_identity.identity.pub_key, nullptr),
			   "target discover enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_ok(target_trace_request_from_out_path(
			   fixture.peer_identity.identity.pub_key,
			   k_trace_out_path, sizeof(k_trace_out_path), 1U,
			   nullptr),
		   "target trace enqueue failed");
	target_process_until_packets_sent(2U);
	target_set_request_rng_bytes(k_rng, sizeof(k_rng));
	zassert_ok(meshcore_node_telemetry_request(
				   fixture.peer_identity.identity.pub_key,
				   MESHCORE_TELEM_PERM_BASE, NULL),
			   "target telemetry enqueue failed");
	target_process_until_packets_sent(3U);

	zassert_true(meshcore_test_runtime_pending_discovery_get(
			     nullptr, nullptr, &discover_expires_at),
		     "target pending discovery missing");
	zassert_true(meshcore_test_runtime_pending_trace_get(
			     nullptr, &trace_expires_at),
		     "target pending trace missing");
	zassert_true(meshcore_test_runtime_pending_telemetry_get(
			     nullptr, nullptr, nullptr, &telemetry_expires_at),
		     "target pending telemetry missing");

	zassert_true(oracle.send_discover_request(
			     fixture.peer_identity.identity.pub_key),
		     "oracle discover failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle discover send failed");
	zassert_true(oracle.send_trace_request(fixture.peer_identity.identity.pub_key),
		     "oracle trace failed");
	zassert_true(oracle.drive_until_packets_sent(2U), "oracle trace send failed");
	zassert_true(oracle.send_telemetry_request(
			     fixture.peer_identity.identity.pub_key,
			     MESHCORE_TELEM_PERM_BASE),
		     "oracle telemetry failed");
	zassert_true(oracle.drive_until_packets_sent(3U),
		     "oracle telemetry send failed");

	zassert_true(oracle.pending_discovery_active(),
		     "oracle pending discovery missing");
	zassert_true(oracle.pending_trace_active(), "oracle pending trace missing");
	zassert_true(oracle.pending_telemetry_active(),
		     "oracle pending telemetry missing");

	target_advance_ms = discover_expires_at;
	if (trace_expires_at > target_advance_ms) {
		target_advance_ms = trace_expires_at;
	}
	if (telemetry_expires_at > target_advance_ms) {
		target_advance_ms = telemetry_expires_at;
	}
	target_advance_ms += 1U;
	meshcore_hal_test_millis_set(target_advance_ms);
	zassert_ok(meshcore_timer_fired(target_advance_ms), "target timeout process failed");

	oracle_advance_ms = oracle.pending_discovery_expires_at();
	if (oracle.pending_trace_expires_at() > oracle_advance_ms) {
		oracle_advance_ms = oracle.pending_trace_expires_at();
	}
	if (oracle.pending_telemetry_expires_at() > oracle_advance_ms) {
		oracle_advance_ms = oracle.pending_telemetry_expires_at();
	}
	oracle_advance_ms += 1U;
	oracle.advance_time(oracle_advance_ms);

	zassert_false(meshcore_test_runtime_pending_discovery_is_valid(),
		      "target pending discovery should timeout clear");
	zassert_false(meshcore_test_runtime_pending_trace_is_valid(),
		      "target pending trace should timeout clear");
	zassert_false(meshcore_test_runtime_pending_telemetry_is_valid(),
		      "target pending telemetry should timeout clear");
	zassert_false(oracle.pending_discovery_active(),
		      "oracle pending discovery should timeout clear");
	zassert_false(oracle.pending_trace_active(),
		      "oracle pending trace should timeout clear");
	zassert_false(oracle.pending_telemetry_active(),
		      "oracle pending telemetry should timeout clear");
	zassert_equal(oracle.peer_path_publish_count(), 0U,
		      "oracle timeout should not publish peer-path");
	zassert_equal(oracle.trace_publish_count(), 0U,
		      "oracle timeout should not publish trace");
	zassert_equal(oracle.telemetry_publish_count(), 0U,
		      "oracle timeout should not publish telemetry");
}

ZTEST(meshcore_runtime_oracle,
      test_pending_overlap_responses_publish_independently_like_oracle)
{
	static const uint8_t k_discover_rng[] = {0x13, 0x23, 0x33, 0x43};
	static const uint8_t k_telemetry_rng[] = {0x53, 0x63, 0x73, 0x83};
	static const uint8_t k_telemetry_payload[] = {0x99, 0x88, 0x77};
	static const int8_t k_trace_snrs[] = {3, 4, 5};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_telemetry_event_t target_telemetry_event = {};
	meshcore_test_trace_event_t target_trace_event = {};
	meshcore_test_peer_path_event_t target_peer_path_event = {};
	ReferenceChatHarness::observed_telemetry_event oracle_telemetry_event = {};
	ReferenceChatHarness::observed_trace_event oracle_trace_event = {};
	ReferenceChatHarness::observed_peer_path_event oracle_peer_path_event = {};
	uint32_t target_discover_tag = 0U;
	uint32_t target_trace_tag = 0U;
	uint32_t target_telemetry_tag = 0U;

	setup_reference_fixture(&oracle, &fixture);
	target_set_peer_out_path(fixture.peer_identity.identity.pub_key,
				 k_trace_out_path, sizeof(k_trace_out_path), 1U);
	zassert_true(oracle.set_contact_out_path(fixture.peer_identity.identity.pub_key,
						 k_trace_out_path,
						 sizeof(k_trace_out_path), 1U),
		     "oracle trace path install failed");

	target_set_request_rng_bytes(k_discover_rng, sizeof(k_discover_rng));
	oracle.set_random_bytes(k_discover_rng, sizeof(k_discover_rng));
	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_discover_path_request(
			   fixture.peer_identity.identity.pub_key, nullptr),
		   "target discover enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(oracle.send_discover_request(
			     fixture.peer_identity.identity.pub_key),
		     "oracle discover failed");
	zassert_true(oracle.drive_until_packets_sent(1U),
		     "oracle discover send failed");

	zassert_ok(target_trace_request_from_out_path(
			   fixture.peer_identity.identity.pub_key,
			   k_trace_out_path, sizeof(k_trace_out_path), 1U,
			   nullptr),
		   "target trace enqueue failed");
	target_process_until_packets_sent(2U);
	zassert_true(meshcore_test_runtime_pending_trace_get(
			     &target_trace_tag, nullptr),
		     "target trace pending missing");
	zassert_true(oracle.send_trace_request(fixture.peer_identity.identity.pub_key),
		     "oracle trace failed");
	zassert_true(oracle.drive_until_packets_sent(2U), "oracle trace send failed");

	target_set_request_rng_bytes(k_telemetry_rng, sizeof(k_telemetry_rng));
	oracle.set_random_bytes(k_telemetry_rng, sizeof(k_telemetry_rng));
	zassert_ok(meshcore_node_telemetry_request(
			   fixture.peer_identity.identity.pub_key,
			   MESHCORE_TELEM_PERM_BASE, NULL),
		   "target telemetry enqueue failed");
	target_process_until_packets_sent(3U);
	zassert_true(meshcore_test_runtime_pending_telemetry_get(
			     &target_telemetry_tag, nullptr, nullptr, nullptr),
		     "target telemetry pending missing");
	zassert_true(oracle.send_telemetry_request(
			     fixture.peer_identity.identity.pub_key,
			     MESHCORE_TELEM_PERM_BASE),
		     "oracle telemetry failed");
	zassert_true(oracle.drive_until_packets_sent(3U),
		     "oracle telemetry send failed");

	zassert_true(meshcore_test_runtime_pending_discovery_is_valid(),
		     "target discovery pending missing");
	zassert_true(meshcore_test_runtime_pending_discovery_get(
			     &target_discover_tag, nullptr, nullptr),
		     "target discovery tag missing");
	zassert_true(meshcore_test_runtime_pending_trace_is_valid(),
		     "target trace pending missing after telemetry enqueue");
	zassert_true(meshcore_test_runtime_pending_telemetry_is_valid(),
		     "target telemetry pending missing after enqueue");
	zassert_true(oracle.pending_discovery_active(),
		     "oracle discovery pending missing");
	zassert_true(oracle.pending_trace_active(), "oracle trace pending missing");
	zassert_true(oracle.pending_telemetry_active(),
		     "oracle telemetry pending missing");

	clear_target_publish_events();
	zassert_true(meshcore_test_runtime_simulate_telemetry_response_recv(
			     fixture.peer_identity.identity.pub_key,
			     target_telemetry_tag, k_telemetry_payload,
			     sizeof(k_telemetry_payload)),
		     "target telemetry response simulation failed");
	zassert_true(oracle.inject_telemetry_response(
			     fixture.peer_identity.identity.pub_key,
			     k_telemetry_payload, sizeof(k_telemetry_payload), 0),
		     "oracle telemetry response inject failed");
	zassert_true(target_await_telemetry_event(&target_telemetry_event, 100),
		     "target telemetry publish missing");
	zassert_true(oracle.capture_last_telemetry_event(&oracle_telemetry_event),
		     "oracle telemetry publish missing");
	assert_telemetry_events_equal(&target_telemetry_event,
				      &oracle_telemetry_event,
				      "overlap telemetry publish");
	zassert_true(meshcore_test_runtime_pending_discovery_is_valid(),
		     "target discovery pending should remain");
	zassert_true(meshcore_test_runtime_pending_trace_is_valid(),
		     "target trace pending should remain");
	zassert_false(meshcore_test_runtime_pending_telemetry_is_valid(),
		      "target telemetry pending should clear");
	zassert_true(oracle.pending_discovery_active(),
		     "oracle discovery pending should remain");
	zassert_true(oracle.pending_trace_active(),
		     "oracle trace pending should remain");
	zassert_false(oracle.pending_telemetry_active(),
		      "oracle telemetry pending should clear");

	clear_target_publish_events();
	zassert_true(meshcore_test_runtime_simulate_trace_recv(
			     target_trace_tag, 0U, k_trace_snrs,
			     ARRAY_SIZE(k_trace_snrs), 4),
		     "target trace response simulation failed");
	zassert_true(oracle.inject_trace_result(
			     0U, k_trace_snrs, ARRAY_SIZE(k_trace_snrs), 4),
		     "oracle trace response inject failed");
	zassert_true(target_await_trace_event(&target_trace_event, 100),
		     "target trace publish missing");
	zassert_true(oracle.capture_last_trace_event(&oracle_trace_event),
		     "oracle trace publish missing");
	assert_trace_events_equal(&target_trace_event, &oracle_trace_event,
				  "overlap trace publish");
	zassert_true(meshcore_test_runtime_pending_discovery_is_valid(),
		     "target discovery pending should remain after trace");
	zassert_false(meshcore_test_runtime_pending_trace_is_valid(),
		      "target trace pending should clear");
	zassert_true(oracle.pending_discovery_active(),
		     "oracle discovery pending should remain after trace");
	zassert_false(oracle.pending_trace_active(),
		      "oracle trace pending should clear");

	clear_target_publish_events();
	zassert_true(meshcore_test_runtime_simulate_peer_path_recv(
			     fixture.peer_identity.identity.pub_key,
			     k_trace_out_path, sizeof(k_trace_out_path), 2U,
			     PAYLOAD_TYPE_RESPONSE,
			     (const int8_t *)&target_discover_tag,
			     sizeof(target_discover_tag), nullptr, 0U, 5),
		     "target discover response simulation failed");
	zassert_true(oracle.inject_discover_response(
			     fixture.peer_identity.identity.pub_key,
			     k_trace_out_path, sizeof(k_trace_out_path), 2U,
			     nullptr, 0U, nullptr, 0U, 5),
		     "oracle discover response inject failed");
	zassert_true(target_await_peer_path_event(&target_peer_path_event, 100),
		     "target discover publish missing");
	zassert_true(oracle.capture_last_peer_path_event(&oracle_peer_path_event),
		     "oracle discover publish missing");
	assert_peer_path_events_equal(&target_peer_path_event,
				      &oracle_peer_path_event,
				      "overlap discover publish");
	zassert_false(meshcore_test_runtime_pending_discovery_is_valid(),
		      "target discovery pending should clear");
	zassert_false(oracle.pending_discovery_active(),
		      "oracle discovery pending should clear");
}

ZTEST(meshcore_runtime_oracle,
      test_telemetry_same_surface_overwrites_like_oracle)
{
	static const uint8_t k_rng[] = {0x21, 0x31, 0x41, 0x51, 0x61, 0x71, 0x81, 0x91};
	static const uint8_t k_old_payload[] = {0xaa};
	static const uint8_t k_new_payload[] = {0xbb, 0xcc, 0xdd};
	const uint8_t permission_first = MESHCORE_TELEM_PERM_BASE;
	const uint8_t permission_second =
		MESHCORE_TELEM_PERM_BASE | MESHCORE_TELEM_PERM_LOCATION;
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_telemetry_event_t target_event = {};
	ReferenceChatHarness::observed_telemetry_event oracle_event = {};
	uint32_t target_tag_first = 0U;
	uint32_t target_tag_second = 0U;
	uint32_t target_tag_after = 0U;
	uint32_t oracle_tag_first = 0U;
	uint32_t oracle_tag_second = 0U;
	uint32_t oracle_tag_after = 0U;
	uint8_t target_prefix[MESHCORE_NODE_KEY_PREFIX_BYTES] = {0};
	uint8_t oracle_prefix[MESHCORE_NODE_KEY_PREFIX_BYTES] = {0};
	uint8_t target_permission = 0U;
	uint8_t oracle_permission = 0U;

	setup_reference_fixture(&oracle, &fixture);
	target_set_request_rng_bytes(k_rng, sizeof(k_rng));
	oracle.set_random_bytes(k_rng, sizeof(k_rng));

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_telemetry_request(
			   fixture.peer_identity.identity.pub_key, permission_first, NULL),
		   "target telemetry #1 enqueue failed");
	target_process_until_packets_sent_lenient(1U);
	zassert_true(meshcore_test_runtime_pending_telemetry_get(
			     &target_tag_first, target_prefix, &target_permission,
			     nullptr),
		     "target telemetry #1 pending missing");
	zassert_true(oracle.send_telemetry_request(
			     fixture.peer_identity.identity.pub_key, permission_first),
		     "oracle telemetry #1 failed");
	zassert_true(oracle.drive_until_packets_sent(1U),
		     "oracle telemetry #1 send failed");
	zassert_true(oracle.pending_telemetry_get(&oracle_tag_first, oracle_prefix,
						  &oracle_permission),
		     "oracle telemetry #1 pending missing");
	zassert_mem_equal(target_prefix, fixture.peer_identity.identity.pub_key,
			  sizeof(target_prefix), "target telemetry #1 prefix mismatch");
	zassert_mem_equal(oracle_prefix, fixture.peer_identity.identity.pub_key,
			  sizeof(oracle_prefix), "oracle telemetry #1 prefix mismatch");
	zassert_equal(target_permission, permission_first,
		      "target telemetry #1 permission mismatch");
	zassert_equal(oracle_permission, permission_first,
		      "oracle telemetry #1 permission mismatch");

	zassert_ok(meshcore_node_telemetry_request(
			   fixture.peer_identity.identity.pub_key,
			   permission_second, NULL),
		   "target telemetry #2 enqueue failed");
	target_process_until_packets_sent_lenient(2U);
	zassert_true(meshcore_test_runtime_pending_telemetry_get(
			     &target_tag_second, target_prefix, &target_permission,
			     nullptr),
		     "target telemetry #2 pending missing");
	zassert_true(oracle.send_telemetry_request(
			     fixture.peer_identity.identity.pub_key,
			     permission_second),
		     "oracle telemetry #2 failed");
	zassert_true(oracle.drive_until_packets_sent(2U),
		     "oracle telemetry #2 send failed");
	zassert_true(oracle.pending_telemetry_get(&oracle_tag_second, oracle_prefix,
						  &oracle_permission),
		     "oracle telemetry #2 pending missing");
	zassert_mem_equal(target_prefix, fixture.peer_identity.identity.pub_key,
			  sizeof(target_prefix), "target telemetry #2 prefix mismatch");
	zassert_mem_equal(oracle_prefix, fixture.peer_identity.identity.pub_key,
			  sizeof(oracle_prefix), "oracle telemetry #2 prefix mismatch");
	zassert_not_equal(target_tag_second, target_tag_first,
			  "target telemetry #2 should replace the tag");
	zassert_equal(target_permission, permission_second,
		      "target telemetry #2 permission mismatch");
	zassert_equal(oracle_permission, permission_second,
		      "oracle telemetry #2 permission mismatch");
	zassert_not_equal(oracle_tag_second, oracle_tag_first,
			  "oracle telemetry #2 should replace the tag");

	clear_target_publish_events();
	zassert_false(meshcore_test_runtime_simulate_telemetry_response_recv(
			      fixture.peer_identity.identity.pub_key,
			      target_tag_first, k_old_payload,
			      sizeof(k_old_payload)),
		      "target telemetry #1 response should be ignored");
	zassert_true(oracle.inject_telemetry_response_for(
			     fixture.peer_identity.identity.pub_key, oracle_tag_first,
			     k_old_payload, sizeof(k_old_payload), 0),
		     "oracle telemetry #1 response inject failed");

	zassert_false(target_await_telemetry_event(&target_event, 20),
		      "target telemetry #1 response should not publish");
	zassert_false(oracle.capture_last_telemetry_event(&oracle_event),
		      "oracle telemetry #1 response should not publish");
	zassert_true(meshcore_test_runtime_pending_telemetry_get(
			     &target_tag_after, target_prefix, &target_permission,
			     nullptr),
		     "target telemetry pending should remain #2");
	zassert_equal(target_tag_after, target_tag_second,
		      "target telemetry pending tag should stay #2");
	zassert_equal(target_permission, permission_second,
		      "target telemetry pending permission mismatch");
	zassert_true(oracle.pending_telemetry_get(&oracle_tag_after, oracle_prefix,
						  &oracle_permission),
		     "oracle telemetry pending should remain #2");
	zassert_equal(oracle_tag_after, oracle_tag_second,
		      "oracle telemetry pending tag should stay #2");
	zassert_mem_equal(oracle_prefix, fixture.peer_identity.identity.pub_key,
			  sizeof(oracle_prefix),
			  "oracle telemetry pending prefix mismatch");
	zassert_equal(oracle_permission, permission_second,
		      "oracle telemetry pending permission mismatch");

	clear_target_publish_events();
	zassert_true(meshcore_test_runtime_simulate_telemetry_response_recv(
			     fixture.peer_identity.identity.pub_key,
			     target_tag_second, k_new_payload,
			     sizeof(k_new_payload)),
		     "target telemetry #2 response should match");
	zassert_true(oracle.inject_telemetry_response(
			     fixture.peer_identity.identity.pub_key, k_new_payload,
			     sizeof(k_new_payload), 0),
		     "oracle telemetry #2 response inject failed");

	zassert_true(target_await_telemetry_event(&target_event, 100),
		     "target telemetry #2 publish missing");
	zassert_true(oracle.capture_last_telemetry_event(&oracle_event),
		     "oracle telemetry #2 publish missing");
	zassert_false(meshcore_test_runtime_pending_telemetry_is_valid(),
		      "target telemetry pending should clear");
	zassert_false(oracle.pending_telemetry_active(),
		      "oracle telemetry pending should clear");
	assert_telemetry_events_equal(&target_event, &oracle_event,
				      "telemetry overwrite response");
}

ZTEST(meshcore_runtime_oracle,
      test_telemetry_response_wrong_peer_is_ignored_like_oracle)
{
	static const uint8_t k_rng[] = {0x71, 0x72, 0x73, 0x74};
	static const uint8_t k_payload[] = {0xca, 0xfe, 0xba, 0xbe};
	const uint8_t permission_mask = MESHCORE_TELEM_PERM_BASE;
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_telemetry_event_t target_event = {};
	ReferenceChatHarness::observed_telemetry_event oracle_event = {};
	uint32_t target_tag = 0U;

	setup_reference_fixture(&oracle, &fixture);
	zassert_true(oracle.upsert_contact(fixture.inbound_identity.identity.pub_key,
					   "runtime_other", ADV_TYPE_CHAT),
		     "oracle secondary contact install failed");
	target_set_request_rng_bytes(k_rng, sizeof(k_rng));
	oracle.set_random_bytes(k_rng, sizeof(k_rng));

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_telemetry_request(
			   fixture.peer_identity.identity.pub_key, permission_mask, NULL),
		   "target telemetry enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(meshcore_test_runtime_pending_telemetry_get(
			     &target_tag, nullptr, nullptr, nullptr),
		     "target pending telemetry missing");
	zassert_true(oracle.send_telemetry_request(
			     fixture.peer_identity.identity.pub_key, permission_mask),
		     "oracle telemetry failed");
	zassert_true(oracle.drive_until_packets_sent(1U),
		     "oracle telemetry send failed");

	clear_target_publish_events();
	zassert_false(meshcore_test_runtime_simulate_telemetry_response_recv(
			      fixture.inbound_identity.identity.pub_key, target_tag,
			      k_payload, sizeof(k_payload)),
		      "target wrong-peer telemetry should be ignored");
	zassert_true(oracle.inject_telemetry_response_for(
			     fixture.inbound_identity.identity.pub_key, target_tag,
			     k_payload, sizeof(k_payload), 0),
		     "oracle wrong-peer telemetry inject failed");

	zassert_false(target_await_telemetry_event(&target_event, 20),
		      "target wrong-peer telemetry should not publish");
	zassert_false(oracle.capture_last_telemetry_event(&oracle_event),
		      "oracle wrong-peer telemetry should not publish");
	zassert_true(meshcore_test_runtime_pending_telemetry_is_valid(),
		     "target pending telemetry should remain");
	zassert_true(oracle.pending_telemetry_active(),
		     "oracle pending telemetry should remain");
	zassert_equal(oracle.telemetry_publish_count(), 0U,
		      "oracle telemetry publish count mismatch");
}

ZTEST(meshcore_runtime_oracle,
      test_telemetry_response_wrong_tag_is_ignored_like_oracle)
{
	static const uint8_t k_rng[] = {0x91, 0x92, 0x93, 0x94};
	static const uint8_t k_payload[] = {0xde, 0xad};
	const uint8_t permission_mask = MESHCORE_TELEM_PERM_BASE;
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_telemetry_event_t target_event = {};
	ReferenceChatHarness::observed_telemetry_event oracle_event = {};
	uint32_t target_tag = 0U;

	setup_reference_fixture(&oracle, &fixture);
	target_set_request_rng_bytes(k_rng, sizeof(k_rng));
	oracle.set_random_bytes(k_rng, sizeof(k_rng));

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_telemetry_request(
			   fixture.peer_identity.identity.pub_key, permission_mask, NULL),
		   "target telemetry enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(meshcore_test_runtime_pending_telemetry_get(
			     &target_tag, nullptr, nullptr, nullptr),
		     "target pending telemetry missing");
	zassert_true(oracle.send_telemetry_request(
			     fixture.peer_identity.identity.pub_key, permission_mask),
		     "oracle telemetry failed");
	zassert_true(oracle.drive_until_packets_sent(1U),
		     "oracle telemetry send failed");

	clear_target_publish_events();
	zassert_false(meshcore_test_runtime_simulate_telemetry_response_recv(
			      fixture.peer_identity.identity.pub_key, target_tag + 1U,
			      k_payload, sizeof(k_payload)),
		      "target wrong-tag telemetry should be ignored");
	zassert_true(oracle.inject_telemetry_response_for(
			     fixture.peer_identity.identity.pub_key, target_tag + 1U,
			     k_payload, sizeof(k_payload), 0),
		     "oracle wrong-tag telemetry inject failed");

	zassert_false(target_await_telemetry_event(&target_event, 20),
		      "target wrong-tag telemetry should not publish");
	zassert_false(oracle.capture_last_telemetry_event(&oracle_event),
		      "oracle wrong-tag telemetry should not publish");
	zassert_true(meshcore_test_runtime_pending_telemetry_is_valid(),
		     "target pending telemetry should remain");
	zassert_true(oracle.pending_telemetry_active(),
		     "oracle pending telemetry should remain");
	zassert_equal(oracle.telemetry_publish_count(), 0U,
		      "oracle telemetry publish count mismatch");
}

ZTEST(meshcore_runtime_oracle,
      test_telemetry_duplicate_response_after_completion_is_ignored_like_oracle)
{
	static const uint8_t k_rng[] = {0x51, 0x52, 0x53, 0x54};
	static const uint8_t k_payload[] = {0x01, 0x02, 0x03};
	const uint8_t permission_mask =
		MESHCORE_TELEM_PERM_BASE | MESHCORE_TELEM_PERM_LOCATION;
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_telemetry_event_t target_event = {};
	ReferenceChatHarness::observed_telemetry_event oracle_event = {};
	uint32_t target_tag = 0U;
	uint32_t oracle_publish_count = 0U;

	setup_reference_fixture(&oracle, &fixture);
	target_set_request_rng_bytes(k_rng, sizeof(k_rng));
	oracle.set_random_bytes(k_rng, sizeof(k_rng));

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_telemetry_request(
			   fixture.peer_identity.identity.pub_key, permission_mask, NULL),
		   "target telemetry enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(meshcore_test_runtime_pending_telemetry_get(
			     &target_tag, nullptr, nullptr, nullptr),
		     "target pending telemetry missing");
	zassert_true(oracle.send_telemetry_request(
			     fixture.peer_identity.identity.pub_key, permission_mask),
		     "oracle telemetry failed");
	zassert_true(oracle.drive_until_packets_sent(1U),
		     "oracle telemetry send failed");

	clear_target_publish_events();
	zassert_true(meshcore_test_runtime_simulate_telemetry_response_recv(
			     fixture.peer_identity.identity.pub_key, target_tag,
			     k_payload, sizeof(k_payload)),
		     "target telemetry simulation failed");
	zassert_true(oracle.inject_telemetry_response(
			     fixture.peer_identity.identity.pub_key, k_payload,
			     sizeof(k_payload), 0),
		     "oracle telemetry inject failed");
	zassert_true(target_await_telemetry_event(&target_event, 100),
		     "target telemetry publish missing");
	zassert_true(oracle.capture_last_telemetry_event(&oracle_event),
		     "oracle telemetry publish missing");
	assert_telemetry_events_equal(&target_event, &oracle_event,
				      "telemetry duplicate first publish");

	clear_target_publish_events();
	oracle_publish_count = oracle.telemetry_publish_count();
	zassert_false(meshcore_test_runtime_simulate_telemetry_response_recv(
			      fixture.peer_identity.identity.pub_key, target_tag,
			      k_payload, sizeof(k_payload)),
		      "target duplicate telemetry should be ignored");
	zassert_true(oracle.inject_telemetry_response_for(
			     fixture.peer_identity.identity.pub_key, target_tag,
			     k_payload, sizeof(k_payload), 0),
		     "oracle duplicate telemetry inject failed");

	zassert_false(target_await_telemetry_event(&target_event, 20),
		      "target duplicate telemetry should not republish");
	zassert_false(meshcore_test_runtime_pending_telemetry_is_valid(),
		      "target pending telemetry should stay cleared");
	zassert_false(oracle.pending_telemetry_active(),
		      "oracle pending telemetry should stay cleared");
	zassert_equal(oracle.telemetry_publish_count(), oracle_publish_count,
		      "oracle duplicate telemetry should not republish");
}
