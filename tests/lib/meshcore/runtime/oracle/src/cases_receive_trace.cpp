// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "case_helpers.h"

ZTEST(meshcore_runtime_oracle, test_trace_result_publish_matches_oracle)
{
	static const int8_t k_trace_snrs[] = {1, 2, 3};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_trace_event_t target_event = {};
	ReferenceChatHarness::observed_trace_event oracle_event = {};
	uint32_t target_tag = 0U;

	setup_reference_fixture(&oracle, &fixture);
	target_set_peer_out_path(fixture.peer_identity.identity.pub_key, k_trace_out_path,
				 sizeof(k_trace_out_path), 1U);
	zassert_true(oracle.set_contact_out_path(fixture.peer_identity.identity.pub_key,
						 k_trace_out_path,
						 sizeof(k_trace_out_path), 1U),
		     "oracle trace path install failed");

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(target_trace_request_from_out_path(
			   fixture.peer_identity.identity.pub_key,
			   k_trace_out_path, sizeof(k_trace_out_path), 1U,
			   nullptr),
		   "target trace enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(meshcore_test_runtime_pending_trace_get(&target_tag, nullptr),
		     "target pending trace missing");
	zassert_true(oracle.send_trace_request(fixture.peer_identity.identity.pub_key),
		     "oracle trace failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle trace send failed");

	clear_target_publish_events();
	zassert_true(meshcore_test_runtime_simulate_trace_recv(
			     target_tag, 0U, k_trace_snrs, ARRAY_SIZE(k_trace_snrs), 4),
		     "target trace simulation failed");
	zassert_true(oracle.inject_trace_result(
			     0U, k_trace_snrs, ARRAY_SIZE(k_trace_snrs), 4),
		     "oracle trace inject failed");

	zassert_true(target_await_trace_event(&target_event, 100),
		     "target trace publish missing");
	zassert_true(oracle.capture_last_trace_event(&oracle_event),
		     "oracle trace publish missing");
	zassert_false(meshcore_test_runtime_pending_trace_is_valid(),
		      "target pending trace should clear");
	zassert_false(oracle.pending_trace_active(),
		      "oracle pending trace should clear");
	assert_trace_events_equal(&target_event, &oracle_event,
				  "trace result publish");
}

ZTEST(meshcore_runtime_oracle,
      test_trace_invalid_result_clears_pending_like_oracle)
{
	static const int8_t k_invalid_trace_snrs[] = {7, 8};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_trace_event_t target_event = {};
	ReferenceChatHarness::observed_trace_event oracle_event = {};
	uint32_t target_tag = 0U;

	setup_reference_fixture(&oracle, &fixture);
	target_set_peer_out_path(fixture.peer_identity.identity.pub_key, k_trace_out_path,
				 sizeof(k_trace_out_path), 1U);
	zassert_true(oracle.set_contact_out_path(fixture.peer_identity.identity.pub_key,
						 k_trace_out_path,
						 sizeof(k_trace_out_path), 1U),
		     "oracle trace path install failed");

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(target_trace_request_from_out_path(
			   fixture.peer_identity.identity.pub_key,
			   k_trace_out_path, sizeof(k_trace_out_path), 1U,
			   nullptr),
		   "target trace enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(meshcore_test_runtime_pending_trace_get(&target_tag, nullptr),
		     "target pending trace missing");
	zassert_true(oracle.send_trace_request(fixture.peer_identity.identity.pub_key),
		     "oracle trace failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle trace send failed");

	clear_target_publish_events();
	zassert_false(meshcore_test_runtime_simulate_trace_recv(
			      target_tag, 0U, k_invalid_trace_snrs,
			      ARRAY_SIZE(k_invalid_trace_snrs), 3),
		      "target invalid trace should be rejected");
	zassert_true(oracle.inject_trace_result_for(
			     target_tag, 0U, k_invalid_trace_snrs,
			     ARRAY_SIZE(k_invalid_trace_snrs), 3),
		     "oracle invalid trace inject failed");

	zassert_false(target_await_trace_event(&target_event, 20),
		      "target invalid trace should not publish");
	zassert_false(oracle.capture_last_trace_event(&oracle_event),
		      "oracle invalid trace should not publish");
	zassert_false(meshcore_test_runtime_pending_trace_is_valid(),
		      "target pending trace should clear");
	zassert_false(oracle.pending_trace_active(),
		      "oracle pending trace should clear");
	zassert_equal(oracle.trace_publish_count(), 0U,
		      "oracle trace publish count mismatch");
}

ZTEST(meshcore_runtime_oracle, test_trace_wrong_tag_is_ignored_like_oracle)
{
	static const int8_t k_trace_snrs[] = {3, 4, 5};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_trace_event_t target_event = {};
	ReferenceChatHarness::observed_trace_event oracle_event = {};
	uint32_t target_tag = 0U;

	setup_reference_fixture(&oracle, &fixture);
	target_set_peer_out_path(fixture.peer_identity.identity.pub_key, k_trace_out_path,
				 sizeof(k_trace_out_path), 1U);
	zassert_true(oracle.set_contact_out_path(fixture.peer_identity.identity.pub_key,
						 k_trace_out_path,
						 sizeof(k_trace_out_path), 1U),
		     "oracle trace path install failed");

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(target_trace_request_from_out_path(
			   fixture.peer_identity.identity.pub_key,
			   k_trace_out_path, sizeof(k_trace_out_path), 1U,
			   nullptr),
		   "target trace enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(meshcore_test_runtime_pending_trace_get(&target_tag, nullptr),
		     "target pending trace missing");
	zassert_true(oracle.send_trace_request(fixture.peer_identity.identity.pub_key),
		     "oracle trace failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle trace send failed");

	clear_target_publish_events();
	zassert_false(meshcore_test_runtime_simulate_trace_recv(
			      target_tag + 1U, 0U, k_trace_snrs,
			      ARRAY_SIZE(k_trace_snrs), 2),
		      "target wrong-tag trace should be ignored");
	zassert_true(oracle.inject_trace_result_for(
			     target_tag + 1U, 0U, k_trace_snrs,
			     ARRAY_SIZE(k_trace_snrs), 2),
		     "oracle wrong-tag trace inject failed");

	zassert_false(target_await_trace_event(&target_event, 20),
		      "target wrong-tag trace should not publish");
	zassert_false(oracle.capture_last_trace_event(&oracle_event),
		      "oracle wrong-tag trace should not publish");
	zassert_true(meshcore_test_runtime_pending_trace_is_valid(),
		     "target pending trace should remain");
	zassert_true(oracle.pending_trace_active(),
		     "oracle pending trace should remain");
	zassert_equal(oracle.trace_publish_count(), 0U,
		      "oracle trace publish count mismatch");
}

ZTEST(meshcore_runtime_oracle,
      test_trace_duplicate_result_after_completion_is_ignored_like_oracle)
{
	static const int8_t k_trace_snrs[] = {1, 2, 3};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_trace_event_t target_event = {};
	ReferenceChatHarness::observed_trace_event oracle_event = {};
	uint32_t target_tag = 0U;
	uint32_t oracle_publish_count = 0U;

	setup_reference_fixture(&oracle, &fixture);
	target_set_peer_out_path(fixture.peer_identity.identity.pub_key, k_trace_out_path,
				 sizeof(k_trace_out_path), 1U);
	zassert_true(oracle.set_contact_out_path(fixture.peer_identity.identity.pub_key,
						 k_trace_out_path,
						 sizeof(k_trace_out_path), 1U),
		     "oracle trace path install failed");

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(target_trace_request_from_out_path(
			   fixture.peer_identity.identity.pub_key,
			   k_trace_out_path, sizeof(k_trace_out_path), 1U,
			   nullptr),
		   "target trace enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(meshcore_test_runtime_pending_trace_get(&target_tag, nullptr),
		     "target pending trace missing");
	zassert_true(oracle.send_trace_request(fixture.peer_identity.identity.pub_key),
		     "oracle trace failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle trace send failed");

	clear_target_publish_events();
	zassert_true(meshcore_test_runtime_simulate_trace_recv(
			     target_tag, 0U, k_trace_snrs, ARRAY_SIZE(k_trace_snrs), 4),
		     "target trace simulation failed");
	zassert_true(oracle.inject_trace_result(
			     0U, k_trace_snrs, ARRAY_SIZE(k_trace_snrs), 4),
		     "oracle trace inject failed");
	zassert_true(target_await_trace_event(&target_event, 100),
		     "target trace publish missing");
	zassert_true(oracle.capture_last_trace_event(&oracle_event),
		     "oracle trace publish missing");
	assert_trace_events_equal(&target_event, &oracle_event,
				  "trace duplicate first publish");

	clear_target_publish_events();
	oracle_publish_count = oracle.trace_publish_count();
	zassert_false(meshcore_test_runtime_simulate_trace_recv(
			      target_tag, 0U, k_trace_snrs,
			      ARRAY_SIZE(k_trace_snrs), 4),
		      "target duplicate trace should be ignored");
	zassert_true(oracle.inject_trace_result_for(
			     target_tag, 0U, k_trace_snrs,
			     ARRAY_SIZE(k_trace_snrs), 4),
		     "oracle duplicate trace inject failed");

	zassert_false(target_await_trace_event(&target_event, 20),
		      "target duplicate trace should not republish");
	zassert_false(meshcore_test_runtime_pending_trace_is_valid(),
		      "target pending trace should stay cleared");
	zassert_false(oracle.pending_trace_active(),
		      "oracle pending trace should stay cleared");
	zassert_equal(oracle.trace_publish_count(), oracle_publish_count,
		      "oracle duplicate trace should not republish");
}

ZTEST(meshcore_runtime_oracle,
      test_trace_same_surface_overwrites_like_oracle)
{
	static const int8_t k_trace_snrs_first[] = {1, 2, 3};
	static const int8_t k_trace_snrs_second[] = {4, 5, 6};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_trace_event_t target_event = {};
	ReferenceChatHarness::observed_trace_event oracle_event = {};
	uint32_t target_tag_first = 0U;
	uint32_t target_tag_second = 0U;
	uint32_t target_tag_after = 0U;
	uint32_t oracle_tag_first = 0U;
	uint32_t oracle_tag_second = 0U;
	uint32_t oracle_tag_after = 0U;
	uint8_t oracle_prefix[MESHCORE_NODE_KEY_PREFIX_BYTES] = {0};

	setup_reference_fixture(&oracle, &fixture);
	target_set_peer_out_path(fixture.peer_identity.identity.pub_key, k_trace_out_path,
				 sizeof(k_trace_out_path), 1U);
	zassert_true(oracle.set_contact_out_path(fixture.peer_identity.identity.pub_key,
						 k_trace_out_path,
						 sizeof(k_trace_out_path), 1U),
		     "oracle trace #1 path install failed");

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(target_trace_request_from_out_path(
			   fixture.peer_identity.identity.pub_key,
			   k_trace_out_path, sizeof(k_trace_out_path), 1U,
			   nullptr),
		   "target trace #1 enqueue failed");
	target_process_until_packets_sent_lenient(1U);
	zassert_true(meshcore_test_runtime_pending_trace_get(&target_tag_first,
							     nullptr),
		     "target trace #1 pending missing");
	zassert_true(oracle.send_trace_request(fixture.peer_identity.identity.pub_key),
		     "oracle trace #1 failed");
	zassert_true(oracle.drive_until_packets_sent(1U),
		     "oracle trace #1 send failed");
	zassert_true(oracle.pending_trace_get(&oracle_tag_first, oracle_prefix),
		     "oracle trace #1 pending missing");
	zassert_mem_equal(oracle_prefix, fixture.peer_identity.identity.pub_key,
			  sizeof(oracle_prefix), "oracle trace #1 prefix mismatch");

	zassert_ok(target_trace_request_from_out_path(
			   fixture.peer_identity.identity.pub_key,
			   k_trace_out_path, sizeof(k_trace_out_path), 1U,
			   nullptr),
		   "target trace #2 enqueue failed");
	target_process_until_packets_sent_lenient(2U);
	zassert_true(meshcore_test_runtime_pending_trace_get(&target_tag_second,
							     nullptr),
		     "target trace #2 pending missing");
	zassert_not_equal(target_tag_second, target_tag_first,
			  "target trace #2 should replace the tag");
	zassert_true(oracle.send_trace_request(fixture.peer_identity.identity.pub_key),
		     "oracle trace #2 failed");
	zassert_true(oracle.drive_until_packets_sent(2U),
		     "oracle trace #2 send failed");
	zassert_true(oracle.pending_trace_get(&oracle_tag_second, oracle_prefix),
		     "oracle trace #2 pending missing");
	zassert_mem_equal(oracle_prefix, fixture.peer_identity.identity.pub_key,
			  sizeof(oracle_prefix), "oracle trace #2 prefix mismatch");
	zassert_not_equal(oracle_tag_second, oracle_tag_first,
			  "oracle trace #2 should replace the tag");

	clear_target_publish_events();
	zassert_false(meshcore_test_runtime_simulate_trace_recv(
			      target_tag_first, 0U, k_trace_snrs_first,
			      ARRAY_SIZE(k_trace_snrs_first), 5),
		      "target trace #1 response should be ignored");
	zassert_true(oracle.inject_trace_result_for(
			     oracle_tag_first, 0U, k_trace_snrs_first,
			     ARRAY_SIZE(k_trace_snrs_first), 5),
		     "oracle trace #1 response inject failed");

	zassert_false(target_await_trace_event(&target_event, 20),
		      "target trace #1 response should not publish");
	zassert_false(oracle.capture_last_trace_event(&oracle_event),
		      "oracle trace #1 response should not publish");
	zassert_true(meshcore_test_runtime_pending_trace_get(
			     &target_tag_after, nullptr),
		     "target trace pending should remain #2");
	zassert_equal(target_tag_after, target_tag_second,
		      "target trace pending tag should stay #2");
	zassert_true(oracle.pending_trace_get(&oracle_tag_after, oracle_prefix),
		     "oracle trace pending should remain #2");
	zassert_equal(oracle_tag_after, oracle_tag_second,
		      "oracle trace pending tag should stay #2");
	zassert_mem_equal(oracle_prefix, fixture.peer_identity.identity.pub_key,
			  sizeof(oracle_prefix), "oracle trace pending prefix mismatch");

	clear_target_publish_events();
	zassert_true(meshcore_test_runtime_simulate_trace_recv(
			     target_tag_second, 0U, k_trace_snrs_second,
			     ARRAY_SIZE(k_trace_snrs_second), 7),
		     "target trace #2 response should match");
	zassert_true(oracle.inject_trace_result_for(
			     oracle_tag_second, 0U, k_trace_snrs_second,
			     ARRAY_SIZE(k_trace_snrs_second), 7),
		     "oracle trace #2 response inject failed");

	zassert_true(target_await_trace_event(&target_event, 100),
		     "target trace #2 publish missing");
	zassert_true(oracle.capture_last_trace_event(&oracle_event),
		     "oracle trace #2 publish missing");
	zassert_false(meshcore_test_runtime_pending_trace_is_valid(),
		      "target trace pending should clear");
	zassert_false(oracle.pending_trace_active(),
		      "oracle trace pending should clear");
	assert_trace_events_equal(&target_event, &oracle_event,
				  "trace overwrite response");
}
