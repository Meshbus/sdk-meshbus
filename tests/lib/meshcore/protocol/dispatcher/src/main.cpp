// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "test_reference_env.h"
#include "test_support.h"
#include "test_target_env.h"

namespace meshcore_dispatcher_tdd {

ZTEST(meshcore_dispatcher_tdd, test_default_dispatcher_policy_matches_upstream)
{
	DispatcherInputScript script = default_script();

	zassert_true(script.override_airtime_budget_factor,
		     "airtime_budget_factor override default mismatch");
	zassert_equal(script.airtime_budget_factor, 2.0f,
		      "airtime_budget_factor default mismatch");
	zassert_true(script.override_rx_delay_ms,
		     "rx_delay override default mismatch");
	zassert_equal(script.cad_fail_max_duration_ms, 4000U,
		      "cad_fail_max_duration default mismatch");
	zassert_equal(script.interference_threshold, 0,
		      "interference_threshold default mismatch");
	zassert_equal(script.agc_reset_interval_ms, 0,
		      "agc_reset_interval default mismatch");
	zassert_equal(script.duty_cycle_window_ms, 3600000UL,
		      "duty_cycle_window default mismatch");
}

ZTEST(meshcore_dispatcher_tdd,
      test_begin_obtain_release_and_time_helpers_match_reference)
{
	ReferenceEnv &expected = reference_env_get();
	reset_fake_runtime();
	expected.script.interference_threshold = 7;
	expected.dispatcher.begin();

	unsigned long expected_future = expected.dispatcher.futureMillis(123);
	bool expected_passed_past = expected.dispatcher.millisHasNowPassed(900U);
	bool expected_passed_equal =
		expected.dispatcher.millisHasNowPassed(kStartMillis);
	mesh::Packet *expected_packet = expected.dispatcher.obtainNewPacket();
	int expected_free_after_alloc = expected.manager.getFreeCount();
	expected.dispatcher.releasePacket(expected_packet);
	DispatcherSnapshot expected_snapshot = capture_reference_snapshot(expected);

	reset_fake_runtime();
	TargetEnv actual;
	actual.script.interference_threshold = 7;
	actual.sync_driver_inputs();
	meshcore_dispatcher_begin(&actual.dispatcher);

	unsigned long actual_future =
		meshcore_dispatcher_future_millis(&actual.dispatcher, 123);
	bool actual_passed_past =
		meshcore_dispatcher_millis_has_now_passed(&actual.dispatcher, 900U);
	bool actual_passed_equal = meshcore_dispatcher_millis_has_now_passed(
		&actual.dispatcher, kStartMillis);
	struct meshcore_packet *actual_packet =
		meshcore_dispatcher_obtain_new_packet(&actual.dispatcher);
	int actual_free_after_alloc =
		meshcore_packet_queue_manager_get_free_count(&actual.manager);
	meshcore_dispatcher_release_packet(&actual.dispatcher, actual_packet);
	DispatcherSnapshot actual_snapshot = capture_target_snapshot(actual);

	zassert_equal(expected_future, actual_future, "futureMillis mismatch");
	zassert_equal(expected_passed_past, actual_passed_past,
		      "millisHasNowPassed(past) mismatch");
	zassert_equal(expected_passed_equal, actual_passed_equal,
		      "millisHasNowPassed(equal) mismatch");
	zassert_equal(expected_free_after_alloc, actual_free_after_alloc,
		      "free_count after alloc mismatch");
	expect_snapshot_match(expected_snapshot, actual_snapshot,
			      "begin/obtain/release");
}

ZTEST(meshcore_dispatcher_tdd, test_direct_receive_release_matches_reference)
{
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN];
	uint8_t raw_len = build_raw_packet(ROUTE_TYPE_DIRECT, 0x11U, raw, 4U);

	ReferenceEnv &expected = reference_env_get();
	reset_fake_runtime();
	expected.script.interference_threshold = 5;
	expected.dispatcher.begin();
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, raw_len, -35, 12,
							    0U),
		     "reference inject failed");
	expected.dispatcher.loop();
	DispatcherSnapshot expected_snapshot = capture_reference_snapshot(expected);

	reset_fake_runtime();
	TargetEnv actual;
	actual.script.interference_threshold = 5;
	actual.sync_driver_inputs();
	meshcore_dispatcher_begin(&actual.dispatcher);
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, raw_len, -35, 12,
							    0U),
		     "actual inject failed");
	target_dispatcher_loop(actual);
	DispatcherSnapshot actual_snapshot = capture_target_snapshot(actual);

	expect_snapshot_match(expected_snapshot, actual_snapshot, "direct receive");
}

ZTEST(meshcore_dispatcher_tdd,
      test_zero_payload_direct_receive_matches_reference)
{
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN];
	uint8_t raw_len = build_raw_packet(ROUTE_TYPE_DIRECT, 0x1AU, raw, 0U);

	ReferenceEnv &expected = reference_env_get();
	reset_fake_runtime();
	expected.dispatcher.begin();
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, raw_len, -38, 10,
							    0U),
		     "reference inject failed");
	expected.dispatcher.loop();
	DispatcherSnapshot expected_snapshot = capture_reference_snapshot(expected);

	reset_fake_runtime();
	TargetEnv actual;
	actual.sync_driver_inputs();
	meshcore_dispatcher_begin(&actual.dispatcher);
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, raw_len, -38, 10,
							    0U),
		     "actual inject failed");
	target_dispatcher_loop(actual);
	DispatcherSnapshot actual_snapshot = capture_target_snapshot(actual);

	expect_snapshot_match(expected_snapshot, actual_snapshot,
			      "zero payload direct receive");
}

ZTEST(meshcore_dispatcher_tdd,
      test_invalid_version_receive_is_dropped_matches_reference)
{
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };

	raw[0] = (uint8_t)(((PAYLOAD_VER_1 + 1U) << PH_VER_SHIFT) |
			   (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT) |
			   ROUTE_TYPE_DIRECT);

	ReferenceEnv &expected = reference_env_get();
	reset_fake_runtime();
	expected.dispatcher.begin();
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, 1U, -36, 11, 0U),
		     "reference inject failed");
	expected.dispatcher.loop();
	DispatcherSnapshot expected_snapshot = capture_reference_snapshot(expected);

	reset_fake_runtime();
	TargetEnv actual;
	actual.sync_driver_inputs();
	meshcore_dispatcher_begin(&actual.dispatcher);
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, 1U, -36, 11, 0U),
		     "actual inject failed");
	target_dispatcher_loop(actual);
	DispatcherSnapshot actual_snapshot = capture_target_snapshot(actual);

	expect_snapshot_match(expected_snapshot, actual_snapshot,
			      "invalid version receive");
}

ZTEST(meshcore_dispatcher_tdd,
      test_reserved_path_mode_receive_is_dropped_matches_reference)
{
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };

	raw[0] = (uint8_t)((PAYLOAD_VER_1 << PH_VER_SHIFT) |
			   (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT) |
			   ROUTE_TYPE_DIRECT);
	raw[1] = 0xC0U;

	ReferenceEnv &expected = reference_env_get();
	reset_fake_runtime();
	expected.dispatcher.begin();
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, 2U, -37, 10, 0U),
		     "reference inject failed");
	expected.dispatcher.loop();
	DispatcherSnapshot expected_snapshot = capture_reference_snapshot(expected);

	reset_fake_runtime();
	TargetEnv actual;
	actual.sync_driver_inputs();
	meshcore_dispatcher_begin(&actual.dispatcher);
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, 2U, -37, 10, 0U),
		     "actual inject failed");
	target_dispatcher_loop(actual);
	DispatcherSnapshot actual_snapshot = capture_target_snapshot(actual);

	expect_snapshot_match(expected_snapshot, actual_snapshot,
			      "reserved path mode receive");
}

ZTEST(meshcore_dispatcher_tdd,
      test_truncated_path_receive_is_dropped_matches_reference)
{
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };

	raw[0] = (uint8_t)((PAYLOAD_VER_1 << PH_VER_SHIFT) |
			   (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT) |
			   ROUTE_TYPE_FLOOD);
	raw[1] = 0x02U;
	raw[2] = 0xABU;

	ReferenceEnv &expected = reference_env_get();
	reset_fake_runtime();
	expected.dispatcher.begin();
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, 3U, -39, 9, 0U),
		     "reference inject failed");
	expected.dispatcher.loop();
	DispatcherSnapshot expected_snapshot = capture_reference_snapshot(expected);

	reset_fake_runtime();
	TargetEnv actual;
	actual.sync_driver_inputs();
	meshcore_dispatcher_begin(&actual.dispatcher);
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, 3U, -39, 9, 0U),
		     "actual inject failed");
	target_dispatcher_loop(actual);
	DispatcherSnapshot actual_snapshot = capture_target_snapshot(actual);

	expect_snapshot_match(expected_snapshot, actual_snapshot,
			      "truncated path receive");
}

ZTEST(meshcore_dispatcher_tdd, test_flood_receive_delayed_matches_reference)
{
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN];
	uint8_t raw_len = build_raw_packet(ROUTE_TYPE_FLOOD, 0x22U, raw, 5U);

	ReferenceEnv &expected = reference_env_get();
	reset_fake_runtime();
	expected.script.rx_delay_ms = 120;
	expected.dispatcher.begin();
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, raw_len, -40, 9,
							    0U),
		     "reference inject failed");
	expected.dispatcher.loop();
	DispatcherSnapshot expected_before = capture_reference_snapshot(expected);
	meshcore_hal_test_millis_advance(120U);
	expected.dispatcher.loop();
	DispatcherSnapshot expected_after = capture_reference_snapshot(expected);

	reset_fake_runtime();
	TargetEnv actual;
	actual.script.rx_delay_ms = 120;
	actual.sync_driver_inputs();
	meshcore_dispatcher_begin(&actual.dispatcher);
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, raw_len, -40, 9,
							    0U),
		     "actual inject failed");
	target_dispatcher_loop(actual);
	DispatcherSnapshot actual_before = capture_target_snapshot(actual);
	meshcore_hal_test_millis_advance(120U);
	target_dispatcher_loop(actual);
	DispatcherSnapshot actual_after = capture_target_snapshot(actual);

	expect_snapshot_match(expected_before, actual_before, "flood delayed before");
	expect_snapshot_match(expected_after, actual_after, "flood delayed after");
}

ZTEST(meshcore_dispatcher_tdd,
      test_on_recv_retransmit_queues_outbound_matches_reference)
{
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN];
	uint8_t raw_len = build_raw_packet(ROUTE_TYPE_DIRECT, 0x33U, raw, 3U);

	ReferenceEnv &expected = reference_env_get();
	reset_fake_runtime();
	expected.script.recv_action = MESHCORE_ACTION_RETRANSMIT_DELAYED(2U, 75U);
	expected.dispatcher.begin();
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, raw_len, -32, 11,
							    0U),
		     "reference inject failed");
	expected.dispatcher.loop();
	DispatcherSnapshot expected_snapshot = capture_reference_snapshot(expected);

	reset_fake_runtime();
	TargetEnv actual;
	actual.script.recv_action = MESHCORE_ACTION_RETRANSMIT_DELAYED(2U, 75U);
	actual.sync_driver_inputs();
	meshcore_dispatcher_begin(&actual.dispatcher);
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, raw_len, -32, 11,
							    0U),
		     "actual inject failed");
	target_dispatcher_loop(actual);
	DispatcherSnapshot actual_snapshot = capture_target_snapshot(actual);

	expect_snapshot_match(expected_snapshot, actual_snapshot,
			      "recv retransmit queue");
}

/* This case stays in protocol/dispatcher because it compares dispatcher's
 * handling of a returned retransmit action, not transport-direct owner logic. */
ZTEST(meshcore_dispatcher_tdd,
      test_transport_direct_receive_retransmit_matches_reference)
{
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN];
	uint8_t raw_len = build_raw_transport_packet(ROUTE_TYPE_TRANSPORT_DIRECT,
						     0x1234U, 0xABCDU, 0x3CU,
						     raw, 4U);

	ReferenceEnv &expected = reference_env_get();
	reset_fake_runtime();
	expected.script.recv_action = MESHCORE_ACTION_RETRANSMIT_DELAYED(1U, 25U);
	expected.dispatcher.begin();
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, raw_len, -28, 14,
							    0U),
		     "reference inject failed");
	expected.dispatcher.loop();
	DispatcherSnapshot expected_snapshot = capture_reference_snapshot(expected);

	reset_fake_runtime();
	TargetEnv actual;
	actual.script.recv_action = MESHCORE_ACTION_RETRANSMIT_DELAYED(1U, 25U);
	actual.sync_driver_inputs();
	meshcore_dispatcher_begin(&actual.dispatcher);
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, raw_len, -28, 14,
							    0U),
		     "actual inject failed");
	target_dispatcher_loop(actual);
	DispatcherSnapshot actual_snapshot = capture_target_snapshot(actual);

	expect_snapshot_match(expected_snapshot, actual_snapshot,
			      "transport direct receive retransmit");
}

ZTEST(meshcore_dispatcher_tdd, test_send_packet_success_matches_reference)
{
	ReferenceEnv &expected = reference_env_get();
	reset_fake_runtime();
	expected.dispatcher.begin();
	meshcore_hal_test_radio_set_send_delay_per_byte(5U);
	mesh::Packet *expected_packet = expected.dispatcher.obtainNewPacket();
	zassert_not_null(expected_packet, "reference obtainNewPacket failed");
	init_reference_packet(expected_packet, ROUTE_TYPE_DIRECT, 0x44U, 4U);
	uint8_t expected_raw_len = expected_packet->getRawLength();
	expected.dispatcher.sendPacket(expected_packet, 0U, 0U);
	expected.dispatcher.loop();
	DispatcherSnapshot expected_before = capture_reference_snapshot(expected);
	meshcore_hal_test_millis_advance((unsigned long)expected_raw_len * 5U);
	expected.dispatcher.loop();
	DispatcherSnapshot expected_after = capture_reference_snapshot(expected);

	reset_fake_runtime();
	TargetEnv actual;
	actual.sync_driver_inputs();
	meshcore_dispatcher_begin(&actual.dispatcher);
	meshcore_hal_test_radio_set_send_delay_per_byte(5U);
	struct meshcore_packet *actual_packet =
		meshcore_dispatcher_obtain_new_packet(&actual.dispatcher);
	zassert_not_null(actual_packet, "actual obtainNewPacket failed");
	init_target_packet(actual_packet, ROUTE_TYPE_DIRECT, 0x44U, 4U);
	uint8_t actual_raw_len = (uint8_t)meshcore_packet_get_raw_length(actual_packet);
	meshcore_dispatcher_send_packet(&actual.dispatcher, actual_packet, 0U, 0U);
	target_dispatcher_loop(actual);
	DispatcherSnapshot actual_before = capture_target_snapshot(actual);
	meshcore_hal_test_millis_advance((unsigned long)actual_raw_len * 5U);
	target_dispatcher_loop(actual);
	DispatcherSnapshot actual_after = capture_target_snapshot(actual);

	zassert_equal(expected_raw_len, actual_raw_len, "raw length mismatch");
	expect_snapshot_match(expected_before, actual_before, "send success before");
	expect_snapshot_match(expected_after, actual_after, "send success after");
}

ZTEST(meshcore_dispatcher_tdd, test_send_start_fail_matches_reference)
{
	ReferenceEnv &expected = reference_env_get();
	reset_fake_runtime();
	expected.dispatcher.begin();
	meshcore_hal_test_radio_set_send_result(false);
	mesh::Packet *expected_packet = expected.dispatcher.obtainNewPacket();
	zassert_not_null(expected_packet, "reference obtainNewPacket failed");
	init_reference_packet(expected_packet, ROUTE_TYPE_DIRECT, 0x55U, 3U);
	expected.dispatcher.sendPacket(expected_packet, 0U, 0U);
	expected.dispatcher.loop();
	DispatcherSnapshot expected_snapshot = capture_reference_snapshot(expected);

	reset_fake_runtime();
	TargetEnv actual;
	actual.sync_driver_inputs();
	meshcore_dispatcher_begin(&actual.dispatcher);
	meshcore_hal_test_radio_set_send_result(false);
	struct meshcore_packet *actual_packet =
		meshcore_dispatcher_obtain_new_packet(&actual.dispatcher);
	zassert_not_null(actual_packet, "actual obtainNewPacket failed");
	init_target_packet(actual_packet, ROUTE_TYPE_DIRECT, 0x55U, 3U);
	meshcore_dispatcher_send_packet(&actual.dispatcher, actual_packet, 0U, 0U);
	target_dispatcher_loop(actual);
	DispatcherSnapshot actual_snapshot = capture_target_snapshot(actual);

	expect_snapshot_match(expected_snapshot, actual_snapshot, "send start fail");
}

ZTEST(meshcore_dispatcher_tdd, test_send_timeout_matches_reference)
{
	ReferenceEnv &expected = reference_env_get();
	reset_fake_runtime();
	expected.dispatcher.begin();
	meshcore_hal_test_radio_set_send_delay_per_byte(10U);
	meshcore_hal_test_radio_set_send_never_complete(true);
	mesh::Packet *expected_packet = expected.dispatcher.obtainNewPacket();
	zassert_not_null(expected_packet, "reference obtainNewPacket failed");
	init_reference_packet(expected_packet, ROUTE_TYPE_DIRECT, 0x66U, 5U);
	uint8_t expected_raw_len = expected_packet->getRawLength();
	expected.dispatcher.sendPacket(expected_packet, 0U, 0U);
	expected.dispatcher.loop();
	meshcore_hal_test_millis_advance((unsigned long)expected_raw_len * 20U);
	expected.dispatcher.loop();
	DispatcherSnapshot expected_snapshot = capture_reference_snapshot(expected);

	reset_fake_runtime();
	TargetEnv actual;
	actual.sync_driver_inputs();
	meshcore_dispatcher_begin(&actual.dispatcher);
	meshcore_hal_test_radio_set_send_delay_per_byte(10U);
	meshcore_hal_test_radio_set_send_never_complete(true);
	struct meshcore_packet *actual_packet =
		meshcore_dispatcher_obtain_new_packet(&actual.dispatcher);
	zassert_not_null(actual_packet, "actual obtainNewPacket failed");
	init_target_packet(actual_packet, ROUTE_TYPE_DIRECT, 0x66U, 5U);
	uint8_t actual_raw_len = (uint8_t)meshcore_packet_get_raw_length(actual_packet);
	meshcore_dispatcher_send_packet(&actual.dispatcher, actual_packet, 0U, 0U);
	target_dispatcher_loop(actual);
	meshcore_hal_test_millis_advance((unsigned long)actual_raw_len * 20U);
	target_dispatcher_loop(actual);
	DispatcherSnapshot actual_snapshot = capture_target_snapshot(actual);

	zassert_equal(expected_raw_len, actual_raw_len, "raw length mismatch");
	expect_snapshot_match(expected_snapshot, actual_snapshot, "send timeout");
}

ZTEST(meshcore_dispatcher_tdd, test_cad_busy_timeout_matches_reference)
{
	ReferenceEnv &expected = reference_env_get();
	reset_fake_runtime();
	expected.script.cad_fail_retry_delay_ms = 100U;
	expected.script.cad_fail_max_duration_ms = 300U;
	expected.dispatcher.begin();
	meshcore_hal_test_radio_force_receiving(true, true);
	mesh::Packet *expected_packet = expected.dispatcher.obtainNewPacket();
	zassert_not_null(expected_packet, "reference obtainNewPacket failed");
	init_reference_packet(expected_packet, ROUTE_TYPE_DIRECT, 0x77U, 4U);
	uint8_t expected_raw_len = expected_packet->getRawLength();
	expected.dispatcher.sendPacket(expected_packet, 0U, 0U);
	expected.dispatcher.loop();
	meshcore_hal_test_millis_advance(301U);
	expected.dispatcher.loop();
	meshcore_hal_test_millis_advance((unsigned long)expected_raw_len);
	expected.dispatcher.loop();
	DispatcherSnapshot expected_snapshot = capture_reference_snapshot(expected);

	reset_fake_runtime();
	TargetEnv actual;
	actual.script.cad_fail_retry_delay_ms = 100U;
	actual.script.cad_fail_max_duration_ms = 300U;
	actual.sync_driver_inputs();
	meshcore_dispatcher_begin(&actual.dispatcher);
	meshcore_hal_test_radio_force_receiving(true, true);
	struct meshcore_packet *actual_packet =
		meshcore_dispatcher_obtain_new_packet(&actual.dispatcher);
	zassert_not_null(actual_packet, "actual obtainNewPacket failed");
	init_target_packet(actual_packet, ROUTE_TYPE_DIRECT, 0x77U, 4U);
	uint8_t actual_raw_len = (uint8_t)meshcore_packet_get_raw_length(actual_packet);
	meshcore_dispatcher_send_packet(&actual.dispatcher, actual_packet, 0U, 0U);
	target_dispatcher_loop(actual);
	meshcore_hal_test_millis_advance(301U);
	target_dispatcher_loop(actual);
	meshcore_hal_test_millis_advance((unsigned long)actual_raw_len);
	target_dispatcher_loop(actual);
	DispatcherSnapshot actual_snapshot = capture_target_snapshot(actual);

	zassert_equal(expected_raw_len, actual_raw_len, "raw length mismatch");
	expect_snapshot_match(expected_snapshot, actual_snapshot, "cad busy timeout");
}

ZTEST(meshcore_dispatcher_tdd, test_radio_nonrx_timeout_matches_reference)
{
	ReferenceEnv &expected = reference_env_get();
	reset_fake_runtime();
	expected.dispatcher.begin();
	meshcore_hal_test_radio_force_in_rx_mode(true, false);
	expected.dispatcher.loop();
	meshcore_hal_test_millis_advance(8001U);
	expected.dispatcher.loop();
	DispatcherSnapshot expected_snapshot = capture_reference_snapshot(expected);

	reset_fake_runtime();
	TargetEnv actual;
	actual.sync_driver_inputs();
	meshcore_dispatcher_begin(&actual.dispatcher);
	meshcore_hal_test_radio_force_in_rx_mode(true, false);
	target_dispatcher_loop(actual);
	meshcore_hal_test_millis_advance(8001U);
	target_dispatcher_loop(actual);
	DispatcherSnapshot actual_snapshot = capture_target_snapshot(actual);

	expect_snapshot_match(expected_snapshot, actual_snapshot,
			      "radio non-rx timeout");
}

ZTEST(meshcore_dispatcher_tdd, test_reset_stats_preserves_queue_matches_reference)
{
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN];
	uint8_t raw_len = build_raw_packet(ROUTE_TYPE_DIRECT, 0x88U, raw, 3U);

	ReferenceEnv &expected = reference_env_get();
	reset_fake_runtime();
	expected.dispatcher.begin();
	mesh::Packet *expected_packet = expected.dispatcher.obtainNewPacket();
	zassert_not_null(expected_packet, "reference obtainNewPacket failed");
	init_reference_packet(expected_packet, ROUTE_TYPE_DIRECT, 0x89U, 3U);
	expected.dispatcher.sendPacket(expected_packet, 0U, 1000U);
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, raw_len, -25, 13,
							    0U),
		     "reference inject failed");
	expected.dispatcher.loop();
	expected.dispatcher.resetStats();
	DispatcherSnapshot expected_snapshot = capture_reference_snapshot(expected);

	reset_fake_runtime();
	TargetEnv actual;
	actual.sync_driver_inputs();
	meshcore_dispatcher_begin(&actual.dispatcher);
	struct meshcore_packet *actual_packet =
		meshcore_dispatcher_obtain_new_packet(&actual.dispatcher);
	zassert_not_null(actual_packet, "actual obtainNewPacket failed");
	init_target_packet(actual_packet, ROUTE_TYPE_DIRECT, 0x89U, 3U);
	meshcore_dispatcher_send_packet(&actual.dispatcher, actual_packet, 0U, 1000U);
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, raw_len, -25, 13,
							    0U),
		     "actual inject failed");
	target_dispatcher_loop(actual);
	meshcore_dispatcher_reset_stats(&actual.dispatcher);
	DispatcherSnapshot actual_snapshot = capture_target_snapshot(actual);

	expect_snapshot_match(expected_snapshot, actual_snapshot,
			      "resetStats preserves queue");
}

ZTEST(meshcore_dispatcher_tdd, test_invalid_send_packet_is_dropped_matches_reference)
{
	ReferenceEnv &expected = reference_env_get();
	reset_fake_runtime();
	expected.dispatcher.begin();
	mesh::Packet *expected_packet = expected.dispatcher.obtainNewPacket();
	zassert_not_null(expected_packet, "reference obtainNewPacket failed");
	init_reference_packet(expected_packet, ROUTE_TYPE_DIRECT, 0x99U, 3U);
	expected_packet->path_len = 0xC0U;
	expected.dispatcher.sendPacket(expected_packet, 0U, 0U);
	DispatcherSnapshot expected_snapshot = capture_reference_snapshot(expected);

	reset_fake_runtime();
	TargetEnv actual;
	actual.sync_driver_inputs();
	meshcore_dispatcher_begin(&actual.dispatcher);
	struct meshcore_packet *actual_packet =
		meshcore_dispatcher_obtain_new_packet(&actual.dispatcher);
	zassert_not_null(actual_packet, "actual obtainNewPacket failed");
	init_target_packet(actual_packet, ROUTE_TYPE_DIRECT, 0x99U, 3U);
	actual_packet->path_len = 0xC0U;
	meshcore_dispatcher_send_packet(&actual.dispatcher, actual_packet, 0U, 0U);
	DispatcherSnapshot actual_snapshot = capture_target_snapshot(actual);

	expect_snapshot_match(expected_snapshot, actual_snapshot,
			      "invalid send packet");
}

} // namespace meshcore_dispatcher_tdd

ZTEST_SUITE(meshcore_dispatcher_tdd, NULL,
	    meshcore_dispatcher_tdd::reference_env_suite_setup,
	    meshcore_dispatcher_tdd::reference_env_before_each, NULL, NULL);
