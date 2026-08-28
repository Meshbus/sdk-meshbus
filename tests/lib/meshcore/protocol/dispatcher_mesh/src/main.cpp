// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "test_reference_env.h"
#include "test_support.h"
#include "test_target_env.h"

#include <string.h>

namespace meshcore_dispatcher_mesh_tdd {

static uint8_t build_ack_zero_hop_raw(uint32_t ack_crc,
				      uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN])
{
	ReferenceEnv &source = reference_env_get();
	mesh::Packet *packet;

	reference_env_before_each(&source);
	begin_reference_env(source);
	packet = source.mesh.createAck(ack_crc);
	zassert_not_null(packet, "createAck source packet missing");
	source.mesh.sendZeroHop(packet, 0U);
	return capture_reference_outbound_raw(source, raw, "ack zero hop source");
}

static uint8_t build_flood_txt_raw(mesh::LocalIdentity &receiver,
				   const uint8_t *secret, const uint8_t *data,
				   size_t data_len,
				   uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN])
{
	ReferenceEnv &source = reference_env_get();
	mesh::Identity dest(receiver.pub_key);
	mesh::Packet *packet;

	reference_env_before_each(&source);
	begin_reference_env(source);
	packet = source.mesh.createDatagram(PAYLOAD_TYPE_TXT_MSG, dest, secret, data,
					     data_len);
	zassert_not_null(packet, "createDatagram source packet missing");
	source.mesh.sendFlood(packet, static_cast<uint32_t>(0U),
			      static_cast<uint8_t>(1U));
	return capture_reference_outbound_raw(source, raw, "flood txt source");
}

static uint8_t build_flood_path_raw(mesh::LocalIdentity &receiver,
				    const uint8_t *secret, const uint8_t *path,
				    uint8_t path_len, const uint8_t *rng_bytes,
				    size_t rng_len,
				    uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN])
{
	ReferenceEnv &source = reference_env_get();
	mesh::Packet *packet;

	reference_env_before_each(&source);
	begin_reference_env(source);
	meshcore_hal_test_rng_set_bytes(rng_bytes, rng_len);
	packet = source.mesh.createPathReturn(receiver, secret, path, path_len, 0U,
					       NULL, 0U);
	zassert_not_null(packet, "createPathReturn source packet missing");
	source.mesh.sendFlood(packet, static_cast<uint32_t>(0U),
			      static_cast<uint8_t>(1U));
	return capture_reference_outbound_raw(source, raw, "flood path source");
}

static uint8_t build_direct_ack_extra_raw(uint8_t self_hash, uint32_t ack_crc,
					  uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN])
{
	mesh::Packet packet;

	packet = mesh::Packet();
	packet.header = (PAYLOAD_VER_1 << PH_VER_SHIFT) |
			(PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT;
	packet.setPathHashSizeAndCount(1U, 2U);
	packet.path[0] = self_hash;
	packet.path[1] = 0x4CU;
	memcpy(packet.payload, &ack_crc, sizeof(ack_crc));
	packet.payload_len = sizeof(ack_crc);
	return packet.writeTo(raw);
}

static uint8_t build_direct_trace_raw(uint32_t tag, uint32_t auth_code,
				      uint8_t flags,
				      uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN])
{
	ReferenceEnv &source = reference_env_get();
	mesh::Packet *packet;
	uint8_t dummy_path[1] = { 0 };

	reference_env_before_each(&source);
	begin_reference_env(source);
	packet = source.mesh.createTrace(tag, auth_code, flags);
	zassert_not_null(packet, "createTrace source packet missing");
	source.mesh.sendDirect(packet, dummy_path, 0U, 0U);
	return capture_reference_outbound_raw(source, raw, "direct trace source");
}

static uint8_t build_control_zero_hop_raw(const uint8_t *data, size_t data_len,
					  uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN])
{
	ReferenceEnv &source = reference_env_get();
	mesh::Packet *packet;

	reference_env_before_each(&source);
	begin_reference_env(source);
	packet = source.mesh.createControlData(data, data_len);
	zassert_not_null(packet, "createControlData source packet missing");
	source.mesh.sendZeroHop(packet, 0U);
	return capture_reference_outbound_raw(source, raw, "control zero hop source");
}

static uint8_t build_direct_multi_ack_raw(mesh::LocalIdentity &receiver,
					  uint32_t ack_crc, uint8_t remaining,
					  uint8_t next_hop_hash,
					  uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN])
{
	ReferenceEnv &source = reference_env_get();
	mesh::Packet *packet;
	uint8_t path[2] = { 0 };

	receiver.copyHashTo(path);
	path[1] = next_hop_hash;

	reference_env_before_each(&source);
	begin_reference_env(source);
	packet = source.mesh.createMultiAck(ack_crc, remaining);
	zassert_not_null(packet, "createMultiAck source packet missing");
	source.mesh.sendDirect(packet, path, sizeof(path), 0U);
	return capture_reference_outbound_raw(source, raw, "direct multi-ack source");
}

ZTEST(meshcore_dispatcher_mesh_tdd,
      test_recv_direct_ack_zero_hop_matches_reference)
{
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t raw_len = build_ack_zero_hop_raw(0x1234ABCDU, raw);
	ReferenceEnv &expected = reference_env_get();
	TargetEnv actual;

	reference_env_before_each(&expected);
	run_reference_receive_once(expected, raw, raw_len, -35, 12);
	JointSnapshot expected_snapshot = capture_reference_snapshot(expected);

	reset_fake_runtime();
	run_target_receive_once(actual, raw, raw_len, -35, 12);
	JointSnapshot actual_snapshot = capture_target_snapshot(actual);

	expect_joint_snapshot_match(expected_snapshot, actual_snapshot,
				    "recv direct ack zero hop");
}

ZTEST(meshcore_dispatcher_mesh_tdd,
      test_recv_flood_txt_delayed_then_decrypt_matches_reference)
{
	static const uint8_t kSecret[PUB_KEY_SIZE] = {
		0x10, 0x20, 0x30, 0x40, 0x11, 0x21, 0x31, 0x41,
		0x12, 0x22, 0x32, 0x42, 0x13, 0x23, 0x33, 0x43,
		0x14, 0x24, 0x34, 0x44, 0x15, 0x25, 0x35, 0x45,
		0x16, 0x26, 0x36, 0x46, 0x17, 0x27, 0x37, 0x47,
	};
	static const uint8_t kData[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t raw_len;
	mesh::LocalIdentity receiver;
	ReferenceEnv &expected = reference_env_get();
	TargetEnv actual;
	JointSnapshot expected_before;
	JointSnapshot expected_after;
	JointSnapshot actual_before;
	JointSnapshot actual_after;

	make_reference_local_identity(&receiver, kIdentitySeedA, sizeof(kIdentitySeedA));
	raw_len = build_flood_txt_raw(receiver, kSecret, kData, sizeof(kData), raw);

	reference_env_before_each(&expected);
	prepare_peer_secret_script(&expected.script, kSecret, 1);
	expected.mesh.self_id = receiver;
	begin_reference_env(expected);
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, raw_len, -40, 9, 0U),
		     "reference inject failed");
	expected.mesh.loop();
	expected_before = capture_reference_snapshot(expected);
	meshcore_hal_test_millis_advance(5000U);
	expected.mesh.loop();
	expected_after = capture_reference_snapshot(expected);

	reset_fake_runtime();
	prepare_peer_secret_script(&actual.script, kSecret, 1);
	actual.sync_script_to_hal();
	sync_target_self_identity(actual, receiver);
	begin_target_env(actual);
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, raw_len, -40, 9, 0U),
		     "target inject failed");
	target_mesh_loop(actual);
	actual_before = capture_target_snapshot(actual);
	meshcore_hal_test_millis_advance(5000U);
	target_mesh_loop(actual);
	actual_after = capture_target_snapshot(actual);

	expect_joint_snapshot_match(expected_before, actual_before,
				    "recv flood txt delayed before");
	expect_joint_snapshot_match(expected_after, actual_after,
				    "recv flood txt delayed after");
}

ZTEST(meshcore_dispatcher_mesh_tdd,
      test_recv_flood_path_accept_sends_direct_return_matches_reference)
{
	static const uint8_t kSecret[PUB_KEY_SIZE] = {
		0x91, 0x82, 0x73, 0x64, 0x55, 0x46, 0x37, 0x28,
		0x19, 0x0A, 0x1B, 0x2C, 0x3D, 0x4E, 0x5F, 0x60,
		0x70, 0x61, 0x52, 0x43, 0x34, 0x25, 0x16, 0x07,
		0xF8, 0xE9, 0xDA, 0xCB, 0xBC, 0xAD, 0x9E, 0x8F,
	};
	static const uint8_t kSourceRng[] = { 0x3A, 0x6C, 0x15, 0xE7 };
	static const uint8_t kSinkRng[] = { 0x41, 0x52, 0x63, 0x74 };
	static const uint8_t kPath[] = { 0x6BU };
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	mesh::LocalIdentity receiver;
	ReferenceEnv &expected = reference_env_get();
	TargetEnv actual;
	JointSnapshot expected_snapshot;
	JointSnapshot actual_snapshot;
	uint8_t raw_len;

	make_reference_local_identity(&receiver, kIdentitySeedA, sizeof(kIdentitySeedA));
	raw_len = build_flood_path_raw(receiver, kSecret, kPath, sizeof(kPath),
				       kSourceRng, sizeof(kSourceRng), raw);

	reference_env_before_each(&expected);
	meshcore_hal_test_rng_set_bytes(kSinkRng, sizeof(kSinkRng));
	prepare_path_script(&expected.script, kSecret, 1);
	expected.mesh.self_id = receiver;
	run_reference_receive_once(expected, raw, raw_len, -38, 11);
	expected_snapshot = capture_reference_snapshot(expected);

	reset_fake_runtime();
	meshcore_hal_test_rng_set_bytes(kSinkRng, sizeof(kSinkRng));
	prepare_path_script(&actual.script, kSecret, 1);
	actual.sync_script_to_hal();
	sync_target_self_identity(actual, receiver);
	run_target_receive_once(actual, raw, raw_len, -38, 11);
	actual_snapshot = capture_target_snapshot(actual);

	expect_joint_snapshot_match(expected_snapshot, actual_snapshot,
				    "recv flood path accept");
	expect_reference_outbound_matches_target(expected, actual,
						 "recv flood path accept");
}

ZTEST(meshcore_dispatcher_mesh_tdd,
      test_recv_direct_ack_routes_extra_acks_matches_reference)
{
	static const uint32_t kAckCrc = 0x89ABCDEFU;
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t self_hash[PATH_HASH_SIZE] = { 0 };
	mesh::LocalIdentity receiver;
	ReferenceEnv &expected = reference_env_get();
	TargetEnv actual;
	JointSnapshot expected_snapshot;
	JointSnapshot actual_snapshot;
	uint8_t raw_len;

	make_reference_local_identity(&receiver, kIdentitySeedB, sizeof(kIdentitySeedB));
	receiver.copyHashTo(self_hash);
	raw_len = build_direct_ack_extra_raw(self_hash[0], kAckCrc, raw);

	reference_env_before_each(&expected);
	expected.mesh.self_id = receiver;
	expected.script.override_allow_packet_forward = true;
	expected.script.allow_packet_forward_value = true;
	expected.script.override_get_extra_ack_transmit_count = true;
	expected.script.get_extra_ack_transmit_count_value = 2U;
	expected.script.override_get_direct_retransmit_delay = true;
	expected.script.get_direct_retransmit_delay_value = 50U;
	run_reference_receive_once(expected, raw, raw_len, -32, 10);
	expected_snapshot = capture_reference_snapshot(expected);

	reset_fake_runtime();
	actual.script.override_allow_packet_forward = true;
	actual.script.allow_packet_forward_value = true;
	actual.script.override_get_extra_ack_transmit_count = true;
	actual.script.get_extra_ack_transmit_count_value = 2U;
	actual.script.override_get_direct_retransmit_delay = true;
	actual.script.get_direct_retransmit_delay_value = 50U;
	actual.sync_script_to_hal();
	sync_target_self_identity(actual, receiver);
	run_target_receive_once(actual, raw, raw_len, -32, 10);
	actual_snapshot = capture_target_snapshot(actual);

	expect_joint_snapshot_match(expected_snapshot, actual_snapshot,
				    "recv direct ack extra");
	expect_reference_outbound_matches_target(expected, actual,
						 "recv direct ack extra");
}

ZTEST(meshcore_dispatcher_mesh_tdd,
      test_recv_direct_trace_full_loop_matches_reference)
{
	static const uint32_t kTraceTag = 0x01020304U;
	static const uint32_t kAuthCode = 0xA1B2C3D4U;
	static const uint8_t kFlags = 0U;
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t raw_len = build_direct_trace_raw(kTraceTag, kAuthCode, kFlags, raw);
	ReferenceEnv &expected = reference_env_get();
	TargetEnv actual;
	JointSnapshot expected_snapshot;
	JointSnapshot actual_snapshot;

	reference_env_before_each(&expected);
	run_reference_receive_once(expected, raw, raw_len, -29, 6);
	expected_snapshot = capture_reference_snapshot(expected);

	reset_fake_runtime();
	run_target_receive_once(actual, raw, raw_len, -29, 6);
	actual_snapshot = capture_target_snapshot(actual);

	expect_joint_snapshot_match(expected_snapshot, actual_snapshot,
				    "recv direct trace");
}

ZTEST(meshcore_dispatcher_mesh_tdd,
      test_recv_direct_control_zero_hop_full_loop_matches_reference)
{
	static const uint8_t kData[] = { 0x80, 0x44, 0x55, 0x66 };
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t raw_len = build_control_zero_hop_raw(kData, sizeof(kData), raw);
	ReferenceEnv &expected = reference_env_get();
	TargetEnv actual;
	JointSnapshot expected_snapshot;
	JointSnapshot actual_snapshot;

	reference_env_before_each(&expected);
	run_reference_receive_once(expected, raw, raw_len, -31, 7);
	expected_snapshot = capture_reference_snapshot(expected);

	reset_fake_runtime();
	run_target_receive_once(actual, raw, raw_len, -31, 7);
	actual_snapshot = capture_target_snapshot(actual);

	expect_joint_snapshot_match(expected_snapshot, actual_snapshot,
				    "recv direct control zero hop");
}

ZTEST(meshcore_dispatcher_mesh_tdd,
      test_recv_direct_multipart_ack_routes_back_full_loop_matches_reference)
{
	static const uint32_t kAckCrc = 0x5566AABBU;
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	mesh::LocalIdentity receiver;
	ReferenceEnv &expected = reference_env_get();
	TargetEnv actual;
	JointSnapshot expected_snapshot;
	JointSnapshot actual_snapshot;
	uint8_t raw_len;

	make_reference_local_identity(&receiver, kIdentitySeedB, sizeof(kIdentitySeedB));
	raw_len =
		build_direct_multi_ack_raw(receiver, kAckCrc, 2U, 0x22U, raw);

	reference_env_before_each(&expected);
	expected.mesh.self_id = receiver;
	expected.script.override_allow_packet_forward = true;
	expected.script.allow_packet_forward_value = true;
	run_reference_receive_once(expected, raw, raw_len, -33, 8);
	expected_snapshot = capture_reference_snapshot(expected);

	reset_fake_runtime();
	actual.script.override_allow_packet_forward = true;
	actual.script.allow_packet_forward_value = true;
	actual.sync_script_to_hal();
	sync_target_self_identity(actual, receiver);
	run_target_receive_once(actual, raw, raw_len, -33, 8);
	actual_snapshot = capture_target_snapshot(actual);

	expect_joint_snapshot_match(expected_snapshot, actual_snapshot,
				    "recv direct multipart ack");
	expect_reference_outbound_matches_target(expected, actual,
						 "recv direct multipart ack");
}

ZTEST(meshcore_dispatcher_mesh_tdd,
      test_send_local_advert_full_loop_matches_reference)
{
	static const uint8_t kAppData[] = { 0x21, 0x32, 0x43, 0x54, 0x65 };
	mesh::LocalIdentity identity;
	ReferenceEnv &expected = reference_env_get();
	TargetEnv actual;
	JointSnapshot expected_before;
	JointSnapshot expected_after;
	JointSnapshot actual_before;
	JointSnapshot actual_after;
	mesh::Packet *expected_packet;
	struct meshcore_packet *actual_packet;
	uint8_t expected_raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t actual_raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t expected_raw_len;
	uint8_t actual_raw_len;

	make_reference_local_identity(&identity, kIdentitySeedA, sizeof(kIdentitySeedA));

	reference_env_before_each(&expected);
	begin_reference_env(expected);
	meshcore_hal_test_radio_set_send_delay_per_byte(4U);
	expected_packet =
		expected.mesh.createAdvert(identity, kAppData, sizeof(kAppData));
	zassert_not_null(expected_packet, "reference createAdvert failed");
	expected.mesh.sendFlood(expected_packet, static_cast<uint32_t>(0U),
				static_cast<uint8_t>(1U));
	expected_raw_len =
		capture_reference_outbound_raw(expected, expected_raw, "advert ref");
	expected.mesh.loop();
	expected_before = capture_reference_snapshot(expected);
	meshcore_hal_test_millis_advance((unsigned long)expected_raw_len * 4U);
	expected.mesh.loop();
	expected_after = capture_reference_snapshot(expected);

	reset_fake_runtime();
	begin_target_env(actual);
	meshcore_hal_test_radio_set_send_delay_per_byte(4U);
	sync_target_self_identity(actual, identity);
	actual_packet = meshcore_mesh_create_advert(&actual.mesh, &actual.mesh.self_id,
						       kAppData, sizeof(kAppData));
	zassert_not_null(actual_packet, "target createAdvert failed");
	meshcore_mesh_send_flood(&actual.mesh, actual_packet, 0U, 1U);
	actual_raw_len =
		capture_target_outbound_raw(actual, actual_raw, "advert target");
	target_mesh_loop(actual);
	actual_before = capture_target_snapshot(actual);
	meshcore_hal_test_millis_advance((unsigned long)actual_raw_len * 4U);
	target_mesh_loop(actual);
	actual_after = capture_target_snapshot(actual);

	zassert_equal(expected_raw_len, actual_raw_len, "advert raw len mismatch");
	zassert_mem_equal(expected_raw, actual_raw, expected_raw_len,
			  "advert raw mismatch");
	expect_joint_snapshot_match(expected_before, actual_before,
				    "send local advert before");
	expect_joint_snapshot_match(expected_after, actual_after,
				    "send local advert after");
}

ZTEST(meshcore_dispatcher_mesh_tdd,
      test_send_local_control_full_loop_matches_reference)
{
	static const uint8_t kData[] = { 0x80, 0x55, 0x66, 0x77 };
	ReferenceEnv &expected = reference_env_get();
	TargetEnv actual;
	JointSnapshot expected_before;
	JointSnapshot expected_after;
	JointSnapshot actual_before;
	JointSnapshot actual_after;
	mesh::Packet *expected_packet;
	struct meshcore_packet *actual_packet;
	uint8_t expected_raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t actual_raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t expected_raw_len;
	uint8_t actual_raw_len;

	reference_env_before_each(&expected);
	begin_reference_env(expected);
	meshcore_hal_test_radio_set_send_delay_per_byte(3U);
	expected_packet =
		expected.mesh.createControlData(kData, sizeof(kData));
	zassert_not_null(expected_packet, "reference createControlData failed");
	expected.mesh.sendZeroHop(expected_packet, 0U);
	expected_raw_len =
		capture_reference_outbound_raw(expected, expected_raw, "control ref");
	expected.mesh.loop();
	expected_before = capture_reference_snapshot(expected);
	meshcore_hal_test_millis_advance((unsigned long)expected_raw_len * 3U);
	expected.mesh.loop();
	expected_after = capture_reference_snapshot(expected);

	reset_fake_runtime();
	begin_target_env(actual);
	meshcore_hal_test_radio_set_send_delay_per_byte(3U);
	actual_packet =
		meshcore_mesh_create_control_data(&actual.mesh, kData, sizeof(kData));
	zassert_not_null(actual_packet, "target createControlData failed");
	meshcore_mesh_send_zero_hop(&actual.mesh, actual_packet, 0U);
	actual_raw_len =
		capture_target_outbound_raw(actual, actual_raw, "control target");
	target_mesh_loop(actual);
	actual_before = capture_target_snapshot(actual);
	meshcore_hal_test_millis_advance((unsigned long)actual_raw_len * 3U);
	target_mesh_loop(actual);
	actual_after = capture_target_snapshot(actual);

	zassert_equal(expected_raw_len, actual_raw_len, "control raw len mismatch");
	zassert_mem_equal(expected_raw, actual_raw, expected_raw_len,
			  "control raw mismatch");
	expect_joint_snapshot_match(expected_before, actual_before,
				    "send local control before");
	expect_joint_snapshot_match(expected_after, actual_after,
				    "send local control after");
}

ZTEST_SUITE(meshcore_dispatcher_mesh_tdd, NULL, reference_env_suite_setup,
	    reference_env_before_each, NULL, NULL);

} // namespace meshcore_dispatcher_mesh_tdd
