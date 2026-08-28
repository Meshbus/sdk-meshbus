// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "case_helpers.h"

ZTEST(meshcore_runtime_oracle, test_discover_response_publish_matches_oracle)
{
	static const uint8_t k_rng[] = {0x44, 0x45, 0x46, 0x47};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_peer_path_event_t target_event = {};
	ReferenceChatHarness::observed_peer_path_event oracle_event = {};
	uint32_t target_discover_tag = 0U;

	setup_reference_fixture(&oracle, &fixture);
	target_set_request_rng_bytes(k_rng, sizeof(k_rng));
	oracle.set_random_bytes(k_rng, sizeof(k_rng));

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_discover_path_request(
			   fixture.peer_identity.identity.pub_key, nullptr),
		   "target discover enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(meshcore_test_runtime_pending_discovery_get(
			     &target_discover_tag, nullptr, nullptr),
		     "target pending discovery missing");
	zassert_true(oracle.send_discover_request(
			     fixture.peer_identity.identity.pub_key),
		     "oracle discover failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle discover send failed");
	zassert_true(oracle.pending_discovery_active(),
		     "oracle pending discovery missing");

	clear_target_publish_events();
	zassert_true(meshcore_test_runtime_simulate_peer_path_recv(
			     fixture.peer_identity.identity.pub_key, k_trace_out_path,
			     sizeof(k_trace_out_path), 2U, PAYLOAD_TYPE_RESPONSE,
			     (const int8_t *)&target_discover_tag,
			     sizeof(target_discover_tag), nullptr, 0U, 9),
		     "target discover response simulation failed");
	zassert_true(oracle.inject_discover_response(
			     fixture.peer_identity.identity.pub_key, k_trace_out_path,
			     sizeof(k_trace_out_path), 2U, nullptr, 0U, nullptr,
			     0U, 9),
		     "oracle discover response inject failed");

	zassert_true(target_await_peer_path_event(&target_event, 100),
		     "target peer-path publish missing");
	zassert_true(oracle.capture_last_peer_path_event(&oracle_event),
		     "oracle peer-path publish missing");
	zassert_false(meshcore_test_runtime_pending_discovery_is_valid(),
		      "target pending discovery should clear");
	zassert_false(oracle.pending_discovery_active(),
		      "oracle pending discovery should clear");
	assert_peer_path_events_equal(&target_event, &oracle_event,
				      "discover response publish");
}

ZTEST(meshcore_runtime_oracle,
      test_discover_response_without_snr_publish_matches_oracle)
{
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_peer_path_event_t target_event = {};
	ReferenceChatHarness::observed_peer_path_event oracle_event = {};
	uint32_t target_discover_tag = 0U;

	setup_reference_fixture(&oracle, &fixture);

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_discover_path_request(
			   fixture.peer_identity.identity.pub_key, nullptr),
		   "target discover enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(meshcore_test_runtime_pending_discovery_get(
			     &target_discover_tag, nullptr, nullptr),
		     "target pending discovery missing");
	zassert_true(oracle.send_discover_request(
			     fixture.peer_identity.identity.pub_key),
		     "oracle discover failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle discover send failed");
	zassert_true(oracle.pending_discovery_active(),
		     "oracle pending discovery missing");

	clear_target_publish_events();
	zassert_true(meshcore_test_runtime_simulate_peer_path_recv(
			     fixture.peer_identity.identity.pub_key, k_trace_out_path,
			     sizeof(k_trace_out_path), 2U, PAYLOAD_TYPE_RESPONSE,
			     (const int8_t *)&target_discover_tag,
			     sizeof(target_discover_tag), nullptr, 0U, 5),
		     "target discover plain response simulation failed");
	zassert_true(oracle.inject_discover_response(
			     fixture.peer_identity.identity.pub_key, k_trace_out_path,
			     sizeof(k_trace_out_path), 2U, nullptr, 0U, nullptr,
			     0U, 5),
		     "oracle discover plain response inject failed");

	zassert_true(target_await_peer_path_event(&target_event, 100),
		     "target peer-path publish missing");
	zassert_true(oracle.capture_last_peer_path_event(&oracle_event),
		     "oracle peer-path publish missing");
	zassert_false(meshcore_test_runtime_pending_discovery_is_valid(),
		      "target pending discovery should clear");
	zassert_false(oracle.pending_discovery_active(),
		      "oracle pending discovery should clear");
	assert_peer_path_events_equal(&target_event, &oracle_event,
				      "discover response plain publish");
}

ZTEST(meshcore_runtime_oracle,
      test_discover_response_wrong_peer_is_ignored_like_oracle)
{
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_peer_path_event_t target_event = {};
	ReferenceChatHarness::observed_peer_path_event oracle_event = {};

	setup_reference_fixture(&oracle, &fixture);
	zassert_true(oracle.upsert_contact(fixture.inbound_identity.identity.pub_key,
					   "runtime_other", ADV_TYPE_CHAT),
		     "oracle secondary contact install failed");

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_discover_path_request(
			   fixture.peer_identity.identity.pub_key, nullptr),
		   "target discover enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(oracle.send_discover_request(
			     fixture.peer_identity.identity.pub_key),
		     "oracle discover failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle discover send failed");

	clear_target_publish_events();
	zassert_false(meshcore_test_runtime_simulate_peer_path_recv(
			      fixture.inbound_identity.identity.pub_key,
			      k_trace_out_path, sizeof(k_trace_out_path), 2U,
			      PAYLOAD_TYPE_RESPONSE, nullptr, 0U, nullptr, 0U, 6),
		      "target wrong-peer discover should be ignored");
	zassert_true(oracle.inject_discover_response_for(
			     fixture.inbound_identity.identity.pub_key,
			     k_trace_out_path, sizeof(k_trace_out_path), 2U,
			     PAYLOAD_TYPE_RESPONSE, nullptr, 0U, 6),
		     "oracle wrong-peer discover inject failed");

	zassert_false(target_await_peer_path_event(&target_event, 20),
		      "target wrong-peer discover should not publish");
	zassert_false(oracle.capture_last_peer_path_event(&oracle_event),
		      "oracle wrong-peer discover should not publish");
	zassert_true(meshcore_test_runtime_pending_discovery_is_valid(),
		     "target pending discovery should remain");
	zassert_true(oracle.pending_discovery_active(),
		     "oracle pending discovery should remain");
	zassert_equal(oracle.peer_path_publish_count(), 0U,
		      "oracle peer-path publish count mismatch");
}

ZTEST(meshcore_runtime_oracle,
      test_discover_duplicate_response_after_completion_is_ignored_like_oracle)
{
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_peer_path_event_t target_event = {};
	ReferenceChatHarness::observed_peer_path_event oracle_event = {};
	uint32_t oracle_publish_count = 0U;
	uint32_t target_discover_tag = 0U;

	setup_reference_fixture(&oracle, &fixture);

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_discover_path_request(
			   fixture.peer_identity.identity.pub_key, nullptr),
		   "target discover enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(meshcore_test_runtime_pending_discovery_get(
			     &target_discover_tag, nullptr, nullptr),
		     "target pending discovery missing");
	zassert_true(oracle.send_discover_request(
			     fixture.peer_identity.identity.pub_key),
		     "oracle discover failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle discover send failed");
	zassert_true(oracle.pending_discovery_active(),
		     "oracle pending discovery missing");

	clear_target_publish_events();
	zassert_true(meshcore_test_runtime_simulate_peer_path_recv(
			     fixture.peer_identity.identity.pub_key, k_trace_out_path,
			     sizeof(k_trace_out_path), 2U, PAYLOAD_TYPE_RESPONSE,
			     (const int8_t *)&target_discover_tag,
			     sizeof(target_discover_tag), nullptr, 0U, 5),
		     "target discover first response simulation failed");
	zassert_true(oracle.inject_discover_response(
			     fixture.peer_identity.identity.pub_key, k_trace_out_path,
			     sizeof(k_trace_out_path), 2U, nullptr, 0U, nullptr,
			     0U, 5),
		     "oracle discover first response inject failed");

	zassert_true(target_await_peer_path_event(&target_event, 100),
		     "target discover publish missing");
	zassert_true(oracle.capture_last_peer_path_event(&oracle_event),
		     "oracle discover publish missing");
	assert_peer_path_events_equal(&target_event, &oracle_event,
				      "discover duplicate first publish");
	zassert_false(meshcore_test_runtime_pending_discovery_is_valid(),
		      "target pending discovery should clear");
	zassert_false(oracle.pending_discovery_active(),
		      "oracle pending discovery should clear");

	clear_target_publish_events();
	oracle_publish_count = oracle.peer_path_publish_count();
	zassert_false(meshcore_test_runtime_simulate_peer_path_recv(
			      fixture.peer_identity.identity.pub_key,
			      k_trace_out_path, sizeof(k_trace_out_path), 2U,
			      PAYLOAD_TYPE_RESPONSE, nullptr, 0U, nullptr, 0U, 5),
		      "target duplicate discover should be ignored");
	zassert_true(oracle.inject_discover_response_for(
			     fixture.peer_identity.identity.pub_key,
			     k_trace_out_path, sizeof(k_trace_out_path), 2U,
			     PAYLOAD_TYPE_RESPONSE, nullptr, 0U, 5),
		     "oracle duplicate discover inject failed");

	zassert_false(target_await_peer_path_event(&target_event, 20),
		      "target duplicate discover should not republish");
	zassert_false(meshcore_test_runtime_pending_discovery_is_valid(),
		      "target pending discovery should stay cleared");
	zassert_false(oracle.pending_discovery_active(),
		      "oracle pending discovery should stay cleared");
	zassert_equal(oracle.peer_path_publish_count(), oracle_publish_count,
		      "oracle duplicate discover should not republish");
}

ZTEST(meshcore_runtime_oracle,
      test_discover_no_response_timeout_clears_pending_like_oracle)
{
	static const uint8_t k_rng[] = {0x35, 0x36, 0x37, 0x38};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_peer_path_event_t target_event = {};
	unsigned long target_expires_at = 0U;
	unsigned long oracle_advance_ms = 0U;
	uint32_t target_process_ms = 0U;

	setup_reference_fixture(&oracle, &fixture);
	target_set_request_rng_bytes(k_rng, sizeof(k_rng));
	oracle.set_random_bytes(k_rng, sizeof(k_rng));

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_discover_path_request(
			   fixture.peer_identity.identity.pub_key, nullptr),
		   "target discover enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(meshcore_test_runtime_pending_discovery_get(
			     nullptr, nullptr, &target_expires_at),
		     "target pending discovery missing");
	zassert_true(oracle.send_discover_request(
			     fixture.peer_identity.identity.pub_key),
		     "oracle discover failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle discover send failed");
	zassert_true(oracle.pending_discovery_active(),
		     "oracle pending discovery missing");

	clear_target_publish_events();
	target_process_ms = (uint32_t)(target_expires_at + 1U);
	meshcore_hal_test_millis_set(target_expires_at + 1U);
	zassert_ok(meshcore_timer_fired(target_process_ms),
		   "target discover timeout process failed");

	oracle_advance_ms = oracle.pending_discovery_expires_at() + 1U;
	oracle.advance_time(oracle_advance_ms);

	zassert_false(target_await_peer_path_event(&target_event, 20),
		      "target discover timeout should not publish");
	zassert_false(meshcore_test_runtime_pending_discovery_is_valid(),
		      "target pending discovery should timeout clear");
	zassert_false(oracle.pending_discovery_active(),
		      "oracle pending discovery should timeout clear");
	zassert_equal(oracle.peer_path_publish_count(), 0U,
		      "oracle discover timeout should not publish");
}

ZTEST(meshcore_runtime_oracle,
      test_discover_same_surface_overwrites_like_oracle)
{
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_peer_path_event_t target_event = {};
	ReferenceChatHarness::observed_peer_path_event oracle_event = {};
	uint8_t target_pending_prefix[MESHCORE_NODE_KEY_PREFIX_BYTES] = {0};
	uint8_t oracle_pending_prefix[MESHCORE_NODE_KEY_PREFIX_BYTES] = {0};
	uint8_t old_oracle_extra[sizeof(uint32_t)] = {0};
	uint32_t target_tag_first = 0U;
	uint32_t target_tag_second = 0U;
	uint32_t oracle_tag_first = 0U;
	uint32_t oracle_tag_second = 0U;

	setup_reference_fixture(&oracle, &fixture);

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_discover_path_request(
			   fixture.peer_identity.identity.pub_key, nullptr),
		   "target discover #1 enqueue failed");
	target_process_until_packets_sent_lenient(1U);
	zassert_true(oracle.send_discover_request(
			     fixture.peer_identity.identity.pub_key),
		     "oracle discover #1 failed");
	zassert_true(oracle.drive_until_packets_sent(1U),
		     "oracle discover #1 send failed");

	zassert_true(meshcore_test_runtime_pending_discovery_get(
			     &target_tag_first, target_pending_prefix, nullptr),
		     "target discover #1 pending missing");
	zassert_true(oracle.pending_discovery_get(oracle_pending_prefix,
						  &oracle_tag_first),
		     "oracle discover #1 pending missing");
	zassert_mem_equal(target_pending_prefix, fixture.peer_identity.identity.pub_key,
			  sizeof(target_pending_prefix),
			  "target discover #1 prefix mismatch");
	zassert_mem_equal(oracle_pending_prefix, fixture.peer_identity.identity.pub_key,
			  sizeof(oracle_pending_prefix),
			  "oracle discover #1 prefix mismatch");
	zassert_ok(meshcore_node_discover_path_request(
			   fixture.peer_identity.identity.pub_key, nullptr),
		   "target discover #2 enqueue failed");
	target_process_until_packets_sent_lenient(2U);
	zassert_true(oracle.send_discover_request(
			     fixture.peer_identity.identity.pub_key),
		     "oracle discover #2 failed");
	zassert_true(oracle.drive_until_packets_sent(2U),
		     "oracle discover #2 send failed");

	memset(target_pending_prefix, 0, sizeof(target_pending_prefix));
	memset(oracle_pending_prefix, 0, sizeof(oracle_pending_prefix));
	zassert_true(meshcore_test_runtime_pending_discovery_get(
			     &target_tag_second, target_pending_prefix, nullptr),
		     "target discover #2 pending missing");
	zassert_true(oracle.pending_discovery_get(oracle_pending_prefix,
						  &oracle_tag_second),
		     "oracle discover #2 pending missing");
	zassert_mem_equal(target_pending_prefix, fixture.peer_identity.identity.pub_key,
			  sizeof(target_pending_prefix),
			  "target discover #2 prefix mismatch");
	zassert_mem_equal(oracle_pending_prefix, fixture.peer_identity.identity.pub_key,
			  sizeof(oracle_pending_prefix),
			  "oracle discover #2 prefix mismatch");
	zassert_not_equal(target_tag_second, target_tag_first,
			  "target discover #2 should replace the tag");
	zassert_not_equal(oracle_tag_second, oracle_tag_first,
			  "oracle discover #2 should replace the tag");

	memcpy(old_oracle_extra, &oracle_tag_first, sizeof(oracle_tag_first));
	clear_target_publish_events();
	zassert_false(meshcore_test_runtime_simulate_peer_path_recv(
			      fixture.peer_identity.identity.pub_key,
			      k_trace_out_path, sizeof(k_trace_out_path), 2U,
			      PAYLOAD_TYPE_RESPONSE,
			      (const int8_t *)&target_tag_first,
			      sizeof(target_tag_first), nullptr, 0U, 8),
		      "target discover #1 response should be ignored");
	zassert_true(oracle.inject_discover_response_for(
			     fixture.peer_identity.identity.pub_key,
			     k_trace_out_path, sizeof(k_trace_out_path), 2U,
			     PAYLOAD_TYPE_RESPONSE, old_oracle_extra,
			     sizeof(old_oracle_extra), 8),
		     "oracle discover #1 response inject failed");
	zassert_false(target_await_peer_path_event(&target_event, 20),
		      "target discover #1 response should not publish");
	zassert_false(oracle.capture_last_peer_path_event(&oracle_event),
		      "oracle discover #1 response should not publish");
	zassert_true(meshcore_test_runtime_pending_discovery_is_valid(),
		     "target discover #2 pending should remain");
	zassert_true(oracle.pending_discovery_active(),
		     "oracle discover #2 pending should remain");

	clear_target_publish_events();
	zassert_true(meshcore_test_runtime_simulate_peer_path_recv(
			     fixture.peer_identity.identity.pub_key,
			     k_trace_out_path, sizeof(k_trace_out_path), 2U,
			     PAYLOAD_TYPE_RESPONSE,
			     (const int8_t *)&target_tag_second,
			     sizeof(target_tag_second), nullptr, 0U, 8),
		     "target discover #2 response simulation failed");
	zassert_true(oracle.inject_discover_response(
			     fixture.peer_identity.identity.pub_key,
			     k_trace_out_path, sizeof(k_trace_out_path), 2U,
			     nullptr, 0U, nullptr, 0U, 8),
		     "oracle discover #2 response inject failed");

	zassert_true(target_await_peer_path_event(&target_event, 100),
		     "target discover #2 publish missing");
	zassert_true(oracle.capture_last_peer_path_event(&oracle_event),
		     "oracle discover #2 publish missing");
	zassert_false(meshcore_test_runtime_pending_discovery_is_valid(),
		      "target discover pending should clear");
	zassert_false(oracle.pending_discovery_active(),
		      "oracle discover pending should clear");
	assert_peer_path_events_equal(&target_event, &oracle_event,
				      "discover overwrite response");
}
