// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "case_helpers.h"

ZTEST(meshcore_runtime_oracle,
      test_no_local_identity_discover_emits_no_outbound_like_oracle)
{
	static const uint8_t k_rng[] = {0x61, 0x62, 0x63, 0x64};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	int target_init_rc;
	int target_req_rc;

	oracle.reset();
	oracle.set_require_local_identity_for_send(true);
	oracle.set_random_bytes(k_rng, sizeof(k_rng));

	meshcore_hal_test_host_state_reset();
	clear_target_publish_events();
	meshcore_hal_test_millis_set(0U);
	meshcore_hal_test_rtc_set_current_time(0U);
	target_init_rc = meshcore_init();
	meshcore_hal_test_radio_reset();
	target_req_rc = meshcore_node_discover_path_request(
		fixture.peer_identity.identity.pub_key, nullptr);
	if (target_init_rc == 0 && target_req_rc == 0) {
		zassert_ok(meshcore_timer_fired(0U), "target discover process failed");
	}

	zassert_false(oracle.send_discover_request(
			      fixture.peer_identity.identity.pub_key),
		      "oracle discover should fail without local identity");
	zassert_equal(meshcore_hal_test_radio_get_packets_sent(), 0U,
		      "target should not send without local identity");
	zassert_equal(oracle.sent_packets(), 0U,
		      "oracle should not send without local identity");
}

ZTEST(meshcore_runtime_oracle, test_no_path_trace_emits_no_outbound_like_oracle)
{
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	int target_req_rc;

	setup_reference_fixture(&oracle, &fixture);
	zassert_ok(meshcore_init(), "init failed");
	meshcore_hal_test_radio_reset();
	target_req_rc = meshcore_node_trace_path_request(
		fixture.peer_identity.identity.pub_key, nullptr);
	if (target_req_rc == 0) {
		zassert_ok(meshcore_timer_fired(0U), "target trace process failed");
	}

	zassert_false(oracle.send_trace_request(fixture.peer_identity.identity.pub_key),
		      "oracle trace should fail without out-path");
	zassert_equal(meshcore_hal_test_radio_get_packets_sent(), 0U,
		      "target should not send trace without out-path");
	zassert_equal(oracle.sent_packets(), 0U,
		      "oracle should not send trace without out-path");
	zassert_false(meshcore_test_runtime_pending_trace_is_valid(),
		      "target should not register pending trace");
	zassert_false(oracle.pending_trace_active(),
		      "oracle should not register pending trace");
}

ZTEST(meshcore_runtime_oracle,
      test_no_channel_send_group_emits_no_outbound_like_oracle)
{
	static const uint8_t k_unknown_secret_16[16] = {
		0xf1, 0xe2, 0xd3, 0xc4, 0xb5, 0xa6, 0x97, 0x88,
		0x79, 0x6a, 0x5b, 0x4c, 0x3d, 0x2e, 0x1f, 0x10,
	};
	static const uint8_t k_payload[] = {'n', 'o', '_', 'c', 'h'};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	int target_req_rc;

	setup_reference_fixture(&oracle, &fixture);
	zassert_ok(meshcore_init(), "init failed");
	meshcore_hal_test_radio_reset();
	target_req_rc = meshcore_message_send_to_channel(
		k_unknown_secret_16, sizeof(k_unknown_secret_16),
		k_payload, sizeof(k_payload));
	if (target_req_rc == 0) {
		zassert_ok(meshcore_timer_fired(0U), "target group send process failed");
	}

	zassert_false(oracle.send_group_message(
			      k_unknown_secret_16, sizeof(k_unknown_secret_16),
			      k_payload, sizeof(k_payload)),
		      "oracle should reject unknown group channel secret");
	zassert_equal(meshcore_hal_test_radio_get_packets_sent(), 0U,
		      "target should not send without known channel");
	zassert_equal(oracle.sent_packets(), 0U,
		      "oracle should not send without known channel");
}

ZTEST(meshcore_runtime_oracle,
      test_zz_encrypted_path_ack_extra_keeps_discover_pending_like_oracle)
{
	static const uint8_t k_rng[] = {0x81, 0x82, 0x83, 0x84};
	static const uint8_t k_ack_extra[] = {0x78, 0x56, 0x34, 0x12};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	ReferenceChatHarness::observed_packet inbound_packet = {};
	meshcore_test_peer_path_event_t target_event = {};
	ReferenceChatHarness::observed_peer_path_event oracle_event = {};
	uint32_t target_sent_packets = 0U;
	uint32_t oracle_sent_packets = 0U;

	setup_reference_fixture(&oracle, &fixture);
	target_set_request_rng_bytes(k_rng, sizeof(k_rng));
	oracle.set_random_bytes(k_rng, sizeof(k_rng));

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_discover_path_request(
			   fixture.peer_identity.identity.pub_key, nullptr),
		   "target discover enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(meshcore_test_runtime_pending_discovery_is_valid(),
		     "target discover pending missing");
	zassert_true(oracle.send_discover_request(
			     fixture.peer_identity.identity.pub_key),
		     "oracle discover send failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle discover loop failed");
	zassert_true(oracle.pending_discovery_active(),
		     "oracle discover pending missing");

	build_inbound_path_raw_from_peer(&fixture, PAYLOAD_TYPE_ACK, k_ack_extra,
					 sizeof(k_ack_extra), &inbound_packet);

	clear_target_publish_events();
	target_sent_packets = meshcore_hal_test_radio_get_packets_sent();
	oracle_sent_packets = oracle.sent_packets();
	zassert_ok(target_radio_packet_inject(inbound_packet.raw,
					      (size_t)inbound_packet.raw_len, -40,
					      8, 0U),
		   "target encrypted path inject failed");
	(void)meshcore_timer_fired(0U);
	zassert_true(oracle.inject_raw_packet(inbound_packet.raw,
					      (size_t)inbound_packet.raw_len),
		     "oracle encrypted path inject failed");

	zassert_false(target_await_peer_path_event(&target_event, 20),
		      "target ack-extra path should not publish peer-path");
	zassert_false(oracle.capture_last_peer_path_event(&oracle_event),
		      "oracle ack-extra path should not publish peer-path");
	zassert_true(meshcore_test_runtime_pending_discovery_is_valid(),
		     "target discover pending should remain");
	zassert_true(oracle.pending_discovery_active(),
		     "oracle discover pending should remain");
	zassert_equal(meshcore_hal_test_radio_get_packets_sent(), target_sent_packets,
		      "target encrypted path should not emit follow-up packets");
	zassert_equal(oracle.sent_packets(), oracle_sent_packets,
		      "oracle encrypted path should not emit follow-up packets");
}

ZTEST(meshcore_runtime_oracle, test_invalid_inbound_is_ignored_like_oracle)
{
	static const uint8_t k_invalid_raw[] = {0xff};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_advert_event_t target_advert_event = {};

	setup_reference_fixture(&oracle, &fixture);
	zassert_ok(meshcore_init(), "init failed");
	clear_target_publish_events();
	meshcore_hal_test_radio_reset();
	zassert_ok(target_radio_packet_inject(k_invalid_raw, sizeof(k_invalid_raw),
					      -35, 7, 0U),
		   "target invalid packet inject failed");
	zassert_ok(meshcore_timer_fired(0U), "target invalid packet process failed");

	zassert_false(oracle.inject_advert(k_invalid_raw, sizeof(k_invalid_raw)),
		      "oracle invalid advert should be ignored");
	zassert_equal(target_advert_recv_count(), 0U,
		      "target should not publish invalid advert");
	zassert_false(target_await_advert_event(&target_advert_event, 20),
		      "target invalid advert should not emit publish");
	zassert_equal(oracle.advert_observed_count(), 0U,
		      "oracle should not observe invalid advert");
	zassert_equal(meshcore_hal_test_radio_get_packets_sent(), 0U,
		      "target invalid inbound should not emit follow-up packets");
	zassert_equal(oracle.sent_packets(), 0U,
		      "oracle invalid inbound should not emit follow-up packets");
}

ZTEST(meshcore_runtime_oracle,
      test_invalid_inbound_req_is_ignored_like_oracle)
{
	static const uint8_t k_invalid_req_raw[] = {
		(uint8_t)((PAYLOAD_TYPE_REQ << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT),
		0x00,
	};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_advert_event_t target_advert_event = {};
	meshcore_test_peer_path_event_t target_peer_path_event = {};
	meshcore_test_trace_event_t target_trace_event = {};
	meshcore_test_telemetry_event_t target_telemetry_event = {};

	setup_reference_fixture(&oracle, &fixture);
	zassert_ok(meshcore_init(), "init failed");
	clear_target_publish_events();
	meshcore_hal_test_radio_reset();
	zassert_ok(target_radio_packet_inject(k_invalid_req_raw,
					      sizeof(k_invalid_req_raw), -44, 6, 0U),
		   "target invalid req inject failed");
	(void)meshcore_timer_fired(0U);

	zassert_false(oracle.inject_raw_packet(k_invalid_req_raw,
					       sizeof(k_invalid_req_raw)),
		      "oracle invalid req should be ignored");
	zassert_false(target_await_advert_event(&target_advert_event, 20),
		      "target invalid req should not publish advert");
	zassert_false(target_await_peer_path_event(&target_peer_path_event, 20),
		      "target invalid req should not publish peer-path");
	zassert_false(target_await_trace_event(&target_trace_event, 20),
		      "target invalid req should not publish trace");
	zassert_false(target_await_telemetry_event(&target_telemetry_event, 20),
		      "target invalid req should not publish telemetry");
	zassert_equal(meshcore_hal_test_radio_get_packets_sent(), 0U,
		      "target invalid req should not emit follow-up packets");
	zassert_equal(oracle.sent_packets(), 0U,
		      "oracle invalid req should not emit follow-up packets");
}

ZTEST(meshcore_runtime_oracle,
      test_invalid_inbound_response_keeps_telemetry_pending_like_oracle)
{
	static const uint8_t k_rng[] = {0x8a, 0x8b, 0x8c, 0x8d};
	static const uint8_t k_invalid_response_raw[] = {
		(uint8_t)((PAYLOAD_TYPE_RESPONSE << PH_TYPE_SHIFT) |
			  ROUTE_TYPE_DIRECT),
		0x00,
	};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_telemetry_event_t target_telemetry_event = {};
	uint32_t target_sent_packets = 0U;
	uint32_t oracle_sent_packets = 0U;

	setup_reference_fixture(&oracle, &fixture);
	target_set_request_rng_bytes(k_rng, sizeof(k_rng));
	oracle.set_random_bytes(k_rng, sizeof(k_rng));

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_telemetry_request(
			   fixture.peer_identity.identity.pub_key,
			   MESHCORE_TELEM_PERM_BASE, NULL),
		   "target telemetry enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(meshcore_test_runtime_pending_telemetry_is_valid(),
		     "target pending telemetry missing");
	zassert_true(oracle.send_telemetry_request(
			     fixture.peer_identity.identity.pub_key,
			     MESHCORE_TELEM_PERM_BASE),
		     "oracle telemetry send failed");
	zassert_true(oracle.drive_until_packets_sent(1U),
		     "oracle telemetry loop failed");
	zassert_true(oracle.pending_telemetry_active(),
		     "oracle pending telemetry missing");

	clear_target_publish_events();
	target_sent_packets = meshcore_hal_test_radio_get_packets_sent();
	oracle_sent_packets = oracle.sent_packets();
	zassert_ok(target_radio_packet_inject(k_invalid_response_raw,
					      sizeof(k_invalid_response_raw), -47,
					      5, 0U),
		   "target invalid response inject failed");
	(void)meshcore_timer_fired(0U);
	zassert_false(oracle.inject_raw_packet(k_invalid_response_raw,
					       sizeof(k_invalid_response_raw)),
		      "oracle invalid response should be ignored");

	zassert_false(target_await_telemetry_event(&target_telemetry_event, 20),
		      "target invalid response should not publish telemetry");
	zassert_true(meshcore_test_runtime_pending_telemetry_is_valid(),
		     "target pending telemetry should remain");
	zassert_true(oracle.pending_telemetry_active(),
		     "oracle pending telemetry should remain");
	zassert_equal(meshcore_hal_test_radio_get_packets_sent(), target_sent_packets,
		      "target invalid response should not emit follow-up packets");
	zassert_equal(oracle.sent_packets(), oracle_sent_packets,
		      "oracle invalid response should not emit follow-up packets");
	zassert_equal(oracle.telemetry_publish_count(), 0U,
		      "oracle invalid response should not publish");
}

ZTEST(meshcore_runtime_oracle,
      test_invalid_inbound_path_is_ignored_like_oracle)
{
	static const uint8_t k_invalid_path_raw[] = {
		(uint8_t)((PAYLOAD_TYPE_PATH << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT),
		0x00,
	};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_peer_path_event_t target_peer_path_event = {};
	uint32_t target_sent_packets = 0U;
	uint32_t oracle_sent_packets = 0U;

	setup_reference_fixture(&oracle, &fixture);
	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_discover_path_request(
			   fixture.peer_identity.identity.pub_key, nullptr),
		   "target discover enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(meshcore_test_runtime_pending_discovery_is_valid(),
		     "target pending discovery missing");
	zassert_true(oracle.send_discover_request(
			     fixture.peer_identity.identity.pub_key),
		     "oracle discover send failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle discover loop failed");
	zassert_true(oracle.pending_discovery_active(),
		     "oracle pending discovery missing");

	clear_target_publish_events();
	target_sent_packets = meshcore_hal_test_radio_get_packets_sent();
	oracle_sent_packets = oracle.sent_packets();
	zassert_ok(target_radio_packet_inject(k_invalid_path_raw,
					      sizeof(k_invalid_path_raw), -46, 5, 0U),
		   "target invalid path inject failed");
	(void)meshcore_timer_fired(0U);
	zassert_false(oracle.inject_raw_packet(k_invalid_path_raw,
					       sizeof(k_invalid_path_raw)),
		      "oracle invalid path should be ignored");

	zassert_false(target_await_peer_path_event(&target_peer_path_event, 20),
		      "target invalid path should not publish peer-path");
	zassert_true(meshcore_test_runtime_pending_discovery_is_valid(),
		     "target pending discovery should remain");
	zassert_true(oracle.pending_discovery_active(),
		     "oracle pending discovery should remain");
	zassert_equal(meshcore_hal_test_radio_get_packets_sent(), target_sent_packets,
		      "target invalid path should not emit follow-up packets");
	zassert_equal(oracle.sent_packets(), oracle_sent_packets,
		      "oracle invalid path should not emit follow-up packets");
	zassert_equal(oracle.peer_path_publish_count(), 0U,
		      "oracle invalid path should not publish");
}

ZTEST(meshcore_runtime_oracle,
      test_invalid_inbound_trace_is_ignored_like_oracle)
{
	static const uint8_t k_invalid_trace_raw[] = {
		(uint8_t)((PAYLOAD_TYPE_TRACE << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT),
		0x00,
	};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	meshcore_test_trace_event_t target_trace_event = {};
	uint32_t target_sent_packets = 0U;
	uint32_t oracle_sent_packets = 0U;

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
	zassert_true(meshcore_test_runtime_pending_trace_is_valid(),
		     "target pending trace missing");
	zassert_true(oracle.send_trace_request(fixture.peer_identity.identity.pub_key),
		     "oracle trace send failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle trace loop failed");
	zassert_true(oracle.pending_trace_active(), "oracle pending trace missing");

	clear_target_publish_events();
	target_sent_packets = meshcore_hal_test_radio_get_packets_sent();
	oracle_sent_packets = oracle.sent_packets();
	zassert_ok(target_radio_packet_inject(k_invalid_trace_raw,
					      sizeof(k_invalid_trace_raw), -49, 4,
					      0U),
		   "target invalid trace inject failed");
	(void)meshcore_timer_fired(0U);
	zassert_false(oracle.inject_raw_packet(k_invalid_trace_raw,
					       sizeof(k_invalid_trace_raw)),
		      "oracle invalid trace should be ignored");

	zassert_false(target_await_trace_event(&target_trace_event, 20),
		      "target invalid trace should not publish");
	zassert_true(meshcore_test_runtime_pending_trace_is_valid(),
		     "target pending trace should remain");
	zassert_true(oracle.pending_trace_active(),
		     "oracle pending trace should remain");
	zassert_equal(meshcore_hal_test_radio_get_packets_sent(), target_sent_packets,
		      "target invalid trace should not emit follow-up packets");
	zassert_equal(oracle.sent_packets(), oracle_sent_packets,
		      "oracle invalid trace should not emit follow-up packets");
	zassert_equal(oracle.trace_publish_count(), 0U,
		      "oracle invalid trace should not publish");
}

ZTEST(meshcore_runtime_oracle,
      test_inbound_advert_is_observed_without_followup_like_oracle)
{
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	ReferenceChatHarness::observed_packet inbound_packet = {};
	meshcore_test_advert_event_t target_event = {};
	ReferenceChatHarness::observed_advert_event oracle_event = {};

	setup_reference_fixture(&oracle, &fixture);
	build_inbound_advert_from_oracle(&fixture, &inbound_packet);

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(target_radio_packet_inject(inbound_packet.raw,
					      (size_t)inbound_packet.raw_len, -45,
					      12, 0U),
		   "target advert inject failed");
	zassert_ok(meshcore_timer_fired(0U), "target advert process failed");
	zassert_true(oracle.inject_advert(inbound_packet.raw,
					  (size_t)inbound_packet.raw_len),
		     "oracle advert inject failed");

	zassert_equal(target_advert_recv_count(), 1U,
		      "target advert receive count mismatch");
	zassert_equal(oracle.advert_observed_count(), 1U,
		      "oracle advert observe count mismatch");
	zassert_true(oracle.capture_last_advert_event(&oracle_event),
		     "oracle advert event missing");
	zassert_true(target_await_advert_event(&target_event, 20),
		     "target advert event missing");
	assert_advert_events_equal(&target_event, &oracle_event,
				   "inbound advert");
	zassert_equal(meshcore_hal_test_radio_get_packets_sent(), 0U,
		      "target advert should not emit follow-up packets");
	zassert_equal(oracle.sent_packets(), 0U,
		      "oracle advert should not emit follow-up packets");
	zassert_equal(oracle.getNumContacts(), 2, "oracle contact count mismatch");
	zassert_mem_equal(oracle_event.public_key,
			  fixture.inbound_identity.identity.pub_key,
			  sizeof(oracle_event.public_key),
			  "oracle inbound public key mismatch");
}
