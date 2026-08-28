// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "case_helpers.h"

ZTEST(meshcore_runtime_oracle, test_reference_harness_smoke_emits_raw_advert)
{
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	ReferenceChatHarness::observed_packet packet = {};

	setup_reference_fixture(&oracle, &fixture);
	zassert_true(oracle.send_self_advert(false), "oracle advert request failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle send did not complete");
	zassert_true(oracle.capture_last_packet(&packet), "oracle packet missing");
	assert_packet_behavior(&packet, ReferenceChatHarness::ROUTE_ZERO_HOP, false,
			       PAYLOAD_TYPE_ADVERT, "oracle smoke advert");
}

ZTEST(meshcore_runtime_oracle, test_node_advert_local_matches_oracle)
{
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	ReferenceChatHarness::observed_packet target_packet = {};
	ReferenceChatHarness::observed_packet oracle_packet = {};

	setup_reference_fixture(&oracle, &fixture);
	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_advert_request(false), "target advert enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(capture_last_target_packet(&target_packet), "target packet missing");

	zassert_true(oracle.send_self_advert(false), "oracle advert request failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle send did not complete");
	zassert_true(oracle.capture_last_packet(&oracle_packet), "oracle packet missing");

	assert_packet_behavior(&target_packet, ReferenceChatHarness::ROUTE_ZERO_HOP, false,
			       PAYLOAD_TYPE_ADVERT, "target advert local");
	assert_packets_equal(&target_packet, &oracle_packet, "node advert local");
}

ZTEST(meshcore_runtime_oracle, test_node_advert_flood_matches_oracle)
{
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	ReferenceChatHarness::observed_packet target_packet = {};
	ReferenceChatHarness::observed_packet oracle_packet = {};

	setup_reference_fixture(&oracle, &fixture);
	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_advert_request(true), "target advert enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(capture_last_target_packet(&target_packet), "target packet missing");

	zassert_true(oracle.send_self_advert(true), "oracle advert request failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle send did not complete");
	zassert_true(oracle.capture_last_packet(&oracle_packet), "oracle packet missing");

	assert_packet_behavior(&target_packet, ReferenceChatHarness::ROUTE_FLOOD, false,
			       PAYLOAD_TYPE_ADVERT, "target advert flood");
	assert_packets_equal(&target_packet, &oracle_packet, "node advert flood");
}

ZTEST(meshcore_runtime_oracle, test_peer_advert_replay_matches_oracle)
{
	uint8_t raw[MESHCORE_MAX_RAW_ADVERT_LEN];
	size_t raw_len = build_target_raw_advert_packet(raw, sizeof(raw));
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	ReferenceChatHarness::observed_packet target_packet = {};
	ReferenceChatHarness::observed_packet oracle_packet = {};

	zassert_true(raw_len > 0U, "failed to build raw advert");
	setup_reference_fixture(&oracle, &fixture);
	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_peer_advert_request(raw, raw_len),
		   "target peer advert enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(capture_last_target_packet(&target_packet), "target packet missing");

	zassert_true(oracle.replay_peer_advert_zero_hop(raw, raw_len),
		     "oracle peer advert replay failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle send did not complete");
	zassert_true(oracle.capture_last_packet(&oracle_packet), "oracle packet missing");

	assert_packet_behavior(&target_packet, ReferenceChatHarness::ROUTE_ZERO_HOP, false,
			       PAYLOAD_TYPE_ADVERT, "target peer advert");
	assert_packets_equal(&target_packet, &oracle_packet, "peer advert replay");
}

ZTEST(meshcore_runtime_oracle, test_send_to_node_direct_matches_oracle)
{
	static const uint8_t k_payload[] = {'h', 'i'};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	ReferenceChatHarness::observed_packet target_packet = {};
	ReferenceChatHarness::observed_packet oracle_packet = {};

	setup_reference_fixture(&oracle, &fixture);
	target_set_peer_out_path(fixture.peer_identity.identity.pub_key, k_direct_out_path,
				 sizeof(k_direct_out_path), 1U);
	zassert_true(oracle.set_contact_out_path(fixture.peer_identity.identity.pub_key,
						 k_direct_out_path,
						 sizeof(k_direct_out_path), 1U),
		     "oracle out path install failed");

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_message_send_to_node(fixture.peer_identity.identity.pub_key,
						 false, 1U, k_payload,
						 sizeof(k_payload)),
		   "target send-to-node enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(capture_last_target_packet(&target_packet), "target packet missing");

	zassert_true(oracle.send_message_to_contact(
			     fixture.peer_identity.identity.pub_key, false, 1U,
			     k_payload, sizeof(k_payload)),
		     "oracle send-to-node failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle send did not complete");
	zassert_true(oracle.capture_last_packet(&oracle_packet), "oracle packet missing");

	assert_packet_behavior(&target_packet, ReferenceChatHarness::ROUTE_DIRECT, false,
			       PAYLOAD_TYPE_TXT_MSG, "target send-to-node direct");
	assert_packets_equal(&target_packet, &oracle_packet, "send-to-node direct");
}

ZTEST(meshcore_runtime_oracle, test_send_to_node_flood_attempt_tail_matches_oracle)
{
	static const uint8_t k_payload[] = {'a', 'b', 'c', 'd'};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	ReferenceChatHarness::observed_packet target_packet = {};
	ReferenceChatHarness::observed_packet oracle_packet = {};

	setup_reference_fixture(&oracle, &fixture);
	target_set_peer_out_path(fixture.peer_identity.identity.pub_key, k_direct_out_path,
				 sizeof(k_direct_out_path), 1U);
	zassert_true(oracle.set_contact_out_path(fixture.peer_identity.identity.pub_key,
						 k_direct_out_path,
						 sizeof(k_direct_out_path), 1U),
		     "oracle out path install failed");

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_message_send_to_node(fixture.peer_identity.identity.pub_key,
						 true, 7U, k_payload,
						 sizeof(k_payload)),
		   "target send-to-node enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(capture_last_target_packet(&target_packet), "target packet missing");

	zassert_true(oracle.send_message_to_contact(
			     fixture.peer_identity.identity.pub_key, true, 7U,
			     k_payload, sizeof(k_payload)),
		     "oracle send-to-node failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle send did not complete");
	zassert_true(oracle.capture_last_packet(&oracle_packet), "oracle packet missing");

	assert_packet_behavior(&target_packet, ReferenceChatHarness::ROUTE_FLOOD, false,
			       PAYLOAD_TYPE_TXT_MSG, "target send-to-node flood");
	assert_packets_equal(&target_packet, &oracle_packet,
			     "send-to-node flood attempt tail");
}

ZTEST(meshcore_runtime_oracle, test_send_to_channel_matches_oracle)
{
	uint8_t payload[MESHCORE_MAX_MESSAGE_TX_LEN];
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	ReferenceChatHarness::observed_packet target_packet = {};
	ReferenceChatHarness::observed_packet oracle_packet = {};

	for (size_t i = 0U; i < sizeof(payload); i++) {
		payload[i] = (uint8_t)('a' + (i % 26U));
	}

	setup_reference_fixture(&oracle, &fixture);
	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_message_send_to_channel(k_channel_secret_16,
						    sizeof(k_channel_secret_16),
						    payload, sizeof(payload)),
		   "target send-to-channel enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(capture_last_target_packet(&target_packet), "target packet missing");

	zassert_true(oracle.send_group_message(k_channel_secret_16,
					       sizeof(k_channel_secret_16), payload,
					       sizeof(payload)),
		     "oracle group send failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle send did not complete");
	zassert_true(oracle.capture_last_packet(&oracle_packet), "oracle packet missing");

	assert_packet_behavior(&target_packet, ReferenceChatHarness::ROUTE_FLOOD, false,
			       PAYLOAD_TYPE_GRP_TXT, "target send-to-channel");
	assert_packets_equal(&target_packet, &oracle_packet, "send-to-channel");
}

ZTEST(meshcore_runtime_oracle, test_channel_data_flood_matches_oracle)
{
	static const uint8_t k_payload[] = {0x10, 0x11, 0x12, 0x13};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	ReferenceChatHarness::observed_packet target_packet = {};
	ReferenceChatHarness::observed_packet oracle_packet = {};

	setup_reference_fixture(&oracle, &fixture);
	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_channel_data_send(
			   k_channel_secret_16, sizeof(k_channel_secret_16),
			   NULL, MESHCORE_OUT_PATH_UNKNOWN,
			   MESHCORE_CHANNEL_DATA_TYPE_DEV, k_payload,
			   sizeof(k_payload)),
		   "target channel-data enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(capture_last_target_packet(&target_packet), "target packet missing");

	zassert_true(oracle.send_group_data(
			     k_channel_secret_16, sizeof(k_channel_secret_16),
			     NULL, MESHCORE_OUT_PATH_UNKNOWN,
			     MESHCORE_CHANNEL_DATA_TYPE_DEV, k_payload,
			     sizeof(k_payload)),
		     "oracle channel-data send failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle send did not complete");
	zassert_true(oracle.capture_last_packet(&oracle_packet), "oracle packet missing");

	assert_packet_behavior(&target_packet, ReferenceChatHarness::ROUTE_FLOOD, false,
			       PAYLOAD_TYPE_GRP_DATA, "target channel-data flood");
	assert_packets_equal(&target_packet, &oracle_packet, "channel-data flood");
}

ZTEST(meshcore_runtime_oracle, test_channel_data_direct_matches_oracle)
{
	static const uint8_t k_payload[] = {0x20, 0x21, 0x22};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	ReferenceChatHarness::observed_packet target_packet = {};
	ReferenceChatHarness::observed_packet oracle_packet = {};

	setup_reference_fixture(&oracle, &fixture);
	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_channel_data_send(
			   k_channel_secret_16, sizeof(k_channel_secret_16),
			   k_direct_out_path, 1U, MESHCORE_CHANNEL_DATA_TYPE_DEV,
			   k_payload, sizeof(k_payload)),
		   "target channel-data enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(capture_last_target_packet(&target_packet), "target packet missing");

	zassert_true(oracle.send_group_data(
			     k_channel_secret_16, sizeof(k_channel_secret_16),
			     k_direct_out_path, 1U, MESHCORE_CHANNEL_DATA_TYPE_DEV,
			     k_payload, sizeof(k_payload)),
		     "oracle channel-data send failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle send did not complete");
	zassert_true(oracle.capture_last_packet(&oracle_packet), "oracle packet missing");

	assert_packet_behavior(&target_packet, ReferenceChatHarness::ROUTE_DIRECT, false,
			       PAYLOAD_TYPE_GRP_DATA, "target channel-data direct");
	assert_packets_equal(&target_packet, &oracle_packet, "channel-data direct");
}

ZTEST(meshcore_runtime_oracle, test_discover_path_plain_matches_oracle)
{
	static const uint8_t k_rng[] = {0x10, 0x20, 0x30, 0x40};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	ReferenceChatHarness::observed_packet target_packet = {};
	ReferenceChatHarness::observed_packet oracle_packet = {};

	setup_reference_fixture(&oracle, &fixture);
	target_set_request_rng_bytes(k_rng, sizeof(k_rng));
	oracle.set_random_bytes(k_rng, sizeof(k_rng));

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_discover_path_request(
			   fixture.peer_identity.identity.pub_key, nullptr),
		   "target discover enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(capture_last_target_packet(&target_packet), "target packet missing");

	zassert_true(oracle.send_discover_request(
			     fixture.peer_identity.identity.pub_key),
		     "oracle discover failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle send did not complete");
	zassert_true(oracle.capture_last_packet(&oracle_packet), "oracle packet missing");

	assert_packet_behavior(&target_packet, ReferenceChatHarness::ROUTE_FLOOD, false,
			       PAYLOAD_TYPE_REQ, "target discover");
	assert_packets_equal(&target_packet, &oracle_packet, "discover path");
}

ZTEST(meshcore_runtime_oracle, test_trace_path_matches_oracle)
{
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	ReferenceChatHarness::observed_packet target_packet = {};
	ReferenceChatHarness::observed_packet oracle_packet = {};

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
	zassert_true(capture_last_target_packet(&target_packet), "target packet missing");

	zassert_true(oracle.send_trace_request(fixture.peer_identity.identity.pub_key),
		     "oracle trace failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle send did not complete");
	zassert_true(oracle.capture_last_packet(&oracle_packet), "oracle packet missing");

	assert_packet_behavior(&target_packet, ReferenceChatHarness::ROUTE_DIRECT, false,
			       PAYLOAD_TYPE_TRACE, "target trace");
	assert_packets_equal(&target_packet, &oracle_packet, "trace path");
}

ZTEST(meshcore_runtime_oracle, test_trace_path_hash_size_3_downgrades_like_oracle)
{
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	ReferenceChatHarness::observed_packet target_packet = {};
	ReferenceChatHarness::observed_packet oracle_packet = {};

	setup_reference_fixture(&oracle, &fixture);
	target_set_peer_out_path(fixture.peer_identity.identity.pub_key,
				 k_trace_hash3_out_path,
				 sizeof(k_trace_hash3_out_path), 3U);
	zassert_true(oracle.set_contact_out_path(fixture.peer_identity.identity.pub_key,
						 k_trace_hash3_out_path,
						 sizeof(k_trace_hash3_out_path), 3U),
		     "oracle trace path install failed");

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(target_trace_request_from_out_path(
			   fixture.peer_identity.identity.pub_key,
			   k_trace_hash3_out_path,
			   sizeof(k_trace_hash3_out_path), 3U, nullptr),
		   "target trace enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(capture_last_target_packet(&target_packet), "target packet missing");

	zassert_true(oracle.send_trace_request(fixture.peer_identity.identity.pub_key),
		     "oracle trace failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle send did not complete");
	zassert_true(oracle.capture_last_packet(&oracle_packet), "oracle packet missing");

	assert_packet_behavior(&target_packet, ReferenceChatHarness::ROUTE_DIRECT, false,
			       PAYLOAD_TYPE_TRACE, "target trace hash3");
	assert_packets_equal(&target_packet, &oracle_packet, "trace path hash3");
}

ZTEST(meshcore_runtime_oracle, test_telemetry_request_matches_oracle)
{
	static const uint8_t k_rng[] = {0xa1, 0xa2, 0xa3, 0xa4};
	const uint8_t permission_mask =
		MESHCORE_TELEM_PERM_BASE | MESHCORE_TELEM_PERM_LOCATION;
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	ReferenceChatHarness::observed_packet target_packet = {};
	ReferenceChatHarness::observed_packet oracle_packet = {};

	setup_reference_fixture(&oracle, &fixture);
	target_set_peer_path_unknown(fixture.peer_identity.identity.pub_key);
	target_set_request_rng_bytes(k_rng, sizeof(k_rng));
	oracle.set_random_bytes(k_rng, sizeof(k_rng));

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_telemetry_request(
			   fixture.peer_identity.identity.pub_key, permission_mask, NULL),
		   "target telemetry enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(capture_last_target_packet(&target_packet), "target packet missing");

	zassert_true(oracle.send_telemetry_request(
			     fixture.peer_identity.identity.pub_key, permission_mask),
		     "oracle telemetry failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle send did not complete");
	zassert_true(oracle.capture_last_packet(&oracle_packet), "oracle packet missing");

	assert_packet_behavior(&target_packet, ReferenceChatHarness::ROUTE_FLOOD, false,
			       PAYLOAD_TYPE_REQ, "target telemetry");
	assert_packets_equal(&target_packet, &oracle_packet, "telemetry request");
}

ZTEST(meshcore_runtime_oracle, test_binary_request_flood_matches_oracle)
{
	static const uint8_t k_payload[] = {0x31, 0x32, 0x33, 0x34};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	ReferenceChatHarness::observed_packet target_packet = {};
	ReferenceChatHarness::observed_packet oracle_packet = {};

	setup_reference_fixture(&oracle, &fixture);
	target_set_peer_path_unknown(fixture.peer_identity.identity.pub_key);
	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_binary_request(fixture.peer_identity.identity.pub_key,
						k_payload, sizeof(k_payload)),
		   "target binary enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(capture_last_target_packet(&target_packet), "target packet missing");

	zassert_true(oracle.send_binary_request(fixture.peer_identity.identity.pub_key,
						k_payload, sizeof(k_payload)),
		     "oracle binary failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle send did not complete");
	zassert_true(oracle.capture_last_packet(&oracle_packet), "oracle packet missing");

	assert_packet_behavior(&target_packet, ReferenceChatHarness::ROUTE_FLOOD, false,
			       PAYLOAD_TYPE_REQ, "target binary flood");
	assert_packets_equal(&target_packet, &oracle_packet, "binary request flood");
}

ZTEST(meshcore_runtime_oracle, test_binary_request_direct_matches_oracle)
{
	static const uint8_t k_payload[] = {0x41, 0x42, 0x43, 0x44, 0x45};
	runtime_fixture_data fixture = setup_target_runtime_fixture();
	ReferenceChatHarness &oracle = shared_oracle();
	ReferenceChatHarness::observed_packet target_packet = {};
	ReferenceChatHarness::observed_packet oracle_packet = {};

	setup_reference_fixture(&oracle, &fixture);
	target_set_peer_out_path(fixture.peer_identity.identity.pub_key, k_direct_out_path,
				 sizeof(k_direct_out_path), 1U);
	zassert_true(oracle.set_contact_out_path(fixture.peer_identity.identity.pub_key,
						 k_direct_out_path,
						 sizeof(k_direct_out_path), 1U),
		     "oracle out path install failed");

	zassert_ok(meshcore_init(), "init failed");
	zassert_ok(meshcore_node_binary_request(fixture.peer_identity.identity.pub_key,
						k_payload, sizeof(k_payload)),
		   "target binary enqueue failed");
	target_process_until_packets_sent(1U);
	zassert_true(capture_last_target_packet(&target_packet), "target packet missing");

	zassert_true(oracle.send_binary_request(fixture.peer_identity.identity.pub_key,
						k_payload, sizeof(k_payload)),
		     "oracle binary failed");
	zassert_true(oracle.drive_until_packets_sent(1U), "oracle send did not complete");
	zassert_true(oracle.capture_last_packet(&oracle_packet), "oracle packet missing");

	assert_packet_behavior(&target_packet, ReferenceChatHarness::ROUTE_DIRECT, false,
			       PAYLOAD_TYPE_REQ, "target binary direct");
	assert_packets_equal(&target_packet, &oracle_packet, "binary request direct");
}
