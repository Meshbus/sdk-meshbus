// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "test_reference_env.h"
#include "test_support.h"
#include "test_target_env.h"

#include <string.h>

namespace meshcore_mesh_tdd {

static void init_reference_ack_packet(mesh::Packet *packet, uint8_t route_type,
				      uint32_t ack_crc)
{
	*packet = mesh::Packet();
	packet->header = (PAYLOAD_VER_1 << PH_VER_SHIFT) |
			 (PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT) | route_type;
	packet->path_len = 0U;
	memcpy(packet->payload, &ack_crc, sizeof(ack_crc));
	packet->payload_len = sizeof(ack_crc);
}

static void init_target_ack_packet(struct meshcore_packet *packet,
				   uint8_t route_type, uint32_t ack_crc)
{
	memset(packet, 0, sizeof(*packet));
	packet->header = (PAYLOAD_VER_1 << PH_VER_SHIFT) |
			 (PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT) | route_type;
	packet->path_len = 0U;
	memcpy(packet->payload, &ack_crc, sizeof(ack_crc));
	packet->payload_len = sizeof(ack_crc);
}

static void expect_outbound_queue_match(ReferenceEnv &expected,
					TargetEnv &actual,
					const char *label)
{
	int expected_total = expected.manager.getOutboundTotal();
	int actual_total =
		meshcore_packet_queue_manager_get_outbound_total(&actual.manager);

	zassert_equal(expected_total, actual_total, "%s outbound total mismatch",
		      label);
	if (expected_total != actual_total) {
		return;
	}

	for (int i = 0; i < expected_total; i++) {
		mesh::Packet *expected_packet = expected.manager.getOutboundByIdx(i);
		const struct meshcore_packet *actual_packet =
			meshcore_packet_queue_manager_get_outbound_by_idx(
				&actual.manager, i);
		uint8_t expected_raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
		uint8_t actual_raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
		uint8_t expected_len;
		uint8_t actual_len;

		zassert_not_null(expected_packet, "%s expected outbound null", label);
		zassert_not_null(actual_packet, "%s actual outbound null", label);

		expected_len = expected_packet->writeTo(expected_raw);
		actual_len = meshcore_packet_write_to(actual_packet, actual_raw);
		zassert_equal(expected_len, actual_len,
			      "%s outbound raw len mismatch at %d", label, i);
		zassert_mem_equal(expected_raw, actual_raw, expected_len,
				  "%s outbound raw mismatch at %d", label, i);
	}
}

static const uint8_t kIdentitySeedA[] = {
	0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
	0x98, 0xA9, 0xBA, 0xCB, 0xDC, 0xED, 0xFE, 0x0F,
};

static const uint8_t kIdentitySeedB[] = {
	0x7E, 0x6D, 0x5C, 0x4B, 0x3A, 0x29, 0x18, 0x07,
	0xF6, 0xE5, 0xD4, 0xC3, 0xB2, 0xA1, 0x90, 0x8F,
};

ZTEST(meshcore_mesh_tdd, test_default_protected_policy_methods_match_reference)
{
	/* This case only checks protected policy defaults, not packet create/send encoding. */
	static const uint8_t kRngBytes[] = { 0x11, 0x22, 0x33, 0x44 };
	static const uint8_t kHash[] = { 0x5A };

	ReferenceEnv &expected = reference_env_get();
	mesh::Packet expected_packet;
	MeshPolicySnapshot expected_snapshot;

	reset_fake_runtime();
	meshcore_hal_test_rng_set_bytes(kRngBytes, sizeof(kRngBytes));
	meshcore_hal_test_radio_set_send_delay_per_byte(3U);
	init_reference_packet(&expected_packet, ROUTE_TYPE_FLOOD, 0x20U, 6U);
	expected_snapshot =
		capture_reference_snapshot(expected, &expected_packet, kHash);

	TargetEnv actual;
	struct meshcore_packet actual_packet;
	MeshPolicySnapshot actual_snapshot;

	reset_fake_runtime();
	meshcore_hal_test_rng_set_bytes(kRngBytes, sizeof(kRngBytes));
	meshcore_hal_test_radio_set_send_delay_per_byte(3U);
	init_target_packet(&actual_packet, ROUTE_TYPE_FLOOD, 0x20U, 6U);
	actual_snapshot = capture_target_snapshot(actual, &actual_packet, kHash);

	expect_snapshot_match(expected_snapshot, actual_snapshot, "default policy");
}

ZTEST(meshcore_mesh_tdd, test_hook_overrides_match_reference)
{
	static const uint8_t kHash[] = { 0x2A };

	ReferenceEnv &expected = reference_env_get();
	mesh::Packet expected_packet;
	MeshPolicySnapshot expected_snapshot;

	reset_fake_runtime();
	expected.script.override_filter_recv_flood_packet = true;
	expected.script.filter_recv_flood_packet_value = true;
	expected.script.override_allow_packet_forward = true;
	expected.script.allow_packet_forward_value = true;
	expected.script.override_get_retransmit_delay = true;
	expected.script.get_retransmit_delay_value = 321U;
	expected.script.override_get_direct_retransmit_delay = true;
	expected.script.get_direct_retransmit_delay_value = 17U;
	expected.script.override_get_extra_ack_transmit_count = true;
	expected.script.get_extra_ack_transmit_count_value = 4U;
	expected.script.override_get_cad_fail_retry_delay = true;
	expected.script.get_cad_fail_retry_delay_value = 999U;
	expected.script.override_search_peers_by_hash = true;
	expected.script.search_peers_by_hash_value = 3;
	expected.script.override_search_channels_by_hash = true;
	expected.script.search_channels_by_hash_value = 2;
	init_reference_packet(&expected_packet, ROUTE_TYPE_DIRECT, 0x71U, 4U);
	expected_snapshot =
		capture_reference_snapshot(expected, &expected_packet, kHash);

	TargetEnv actual;
	struct meshcore_packet actual_packet;
	MeshPolicySnapshot actual_snapshot;

	reset_fake_runtime();
	actual.script.override_filter_recv_flood_packet = true;
	actual.script.filter_recv_flood_packet_value = true;
	actual.script.override_allow_packet_forward = true;
	actual.script.allow_packet_forward_value = true;
	actual.script.override_get_retransmit_delay = true;
	actual.script.get_retransmit_delay_value = 321U;
	actual.script.override_get_direct_retransmit_delay = true;
	actual.script.get_direct_retransmit_delay_value = 17U;
	actual.script.override_get_extra_ack_transmit_count = true;
	actual.script.get_extra_ack_transmit_count_value = 4U;
	actual.script.override_get_cad_fail_retry_delay = true;
	actual.script.get_cad_fail_retry_delay_value = 999U;
	actual.script.override_search_peers_by_hash = true;
	actual.script.search_peers_by_hash_value = 3;
	actual.script.override_search_channels_by_hash = true;
	actual.script.search_channels_by_hash_value = 2;
	actual.sync_script_to_hal();
	init_target_packet(&actual_packet, ROUTE_TYPE_DIRECT, 0x71U, 4U);
	actual_snapshot = capture_target_snapshot(actual, &actual_packet, kHash);

	expect_snapshot_match(expected_snapshot, actual_snapshot,
			      "hook overrides");
}

ZTEST(meshcore_mesh_tdd, test_create_datagram_all_types_match_reference)
{
	static const uint8_t kSecret[PUB_KEY_SIZE] = {
		0x10, 0x20, 0x30, 0x40, 0x11, 0x21, 0x31, 0x41,
		0x12, 0x22, 0x32, 0x42, 0x13, 0x23, 0x33, 0x43,
		0x14, 0x24, 0x34, 0x44, 0x15, 0x25, 0x35, 0x45,
		0x16, 0x26, 0x36, 0x46, 0x17, 0x27, 0x37, 0x47,
	};
	static const uint8_t kData[] = { 0xA1, 0xB2, 0xC3, 0xD4, 0xE5 };
	static const uint8_t kTypes[] = {
		PAYLOAD_TYPE_TXT_MSG,
		PAYLOAD_TYPE_REQ,
		PAYLOAD_TYPE_RESPONSE,
	};
	ReferenceEnv &expected = reference_env_get();
	mesh::LocalIdentity peer;
	mesh::Identity dest;
	struct meshcore_identity actual_dest;
	TargetEnv actual;

	reset_fake_runtime();
	make_reference_local_identity(&peer, kIdentitySeedB, sizeof(kIdentitySeedB));
	dest = mesh::Identity(peer.pub_key);
	meshcore_identity_init_from_pub_key(&actual_dest, dest.pub_key);

	for (size_t i = 0; i < sizeof(kTypes); i++) {
		mesh::Packet *expected_packet = expected.mesh.createDatagram(
			kTypes[i], dest, kSecret, kData, sizeof(kData));
		struct meshcore_packet *actual_packet = meshcore_mesh_create_datagram(
			&actual.mesh, kTypes[i], &actual_dest, kSecret, kData,
			sizeof(kData));

		expect_create_result_match_and_free(expected, expected_packet, actual,
						    actual_packet, "createDatagram");
	}
}

ZTEST(meshcore_mesh_tdd, test_create_anon_datagram_matches_reference)
{
	static const uint8_t kSecret[PUB_KEY_SIZE] = {
		0x51, 0x52, 0x53, 0x54, 0x61, 0x62, 0x63, 0x64,
		0x71, 0x72, 0x73, 0x74, 0x81, 0x82, 0x83, 0x84,
		0x91, 0x92, 0x93, 0x94, 0xA1, 0xA2, 0xA3, 0xA4,
		0xB1, 0xB2, 0xB3, 0xB4, 0xC1, 0xC2, 0xC3, 0xC4,
	};
	static const uint8_t kData[] = { 0x31, 0x32, 0x33, 0x34 };
	ReferenceEnv &expected = reference_env_get();
	mesh::LocalIdentity sender;
	mesh::LocalIdentity receiver;
	mesh::Identity dest;
	struct meshcore_identity actual_dest;
	TargetEnv actual;

	reset_fake_runtime();
	make_reference_local_identity(&sender, kIdentitySeedA, sizeof(kIdentitySeedA));
	make_reference_local_identity(&receiver, kIdentitySeedB, sizeof(kIdentitySeedB));
	dest = mesh::Identity(receiver.pub_key);
	meshcore_identity_init_from_pub_key(&actual_dest, dest.pub_key);
	sync_target_self_identity(actual, sender);

	mesh::Packet *expected_packet = expected.mesh.createAnonDatagram(
		PAYLOAD_TYPE_ANON_REQ, sender, dest, kSecret, kData, sizeof(kData));
	struct meshcore_packet *actual_packet = meshcore_mesh_create_anon_datagram(
		&actual.mesh, PAYLOAD_TYPE_ANON_REQ, &actual.mesh.self_id,
		&actual_dest, kSecret, kData, sizeof(kData));

	expect_create_result_match_and_free(expected, expected_packet, actual,
					    actual_packet, "createAnonDatagram");
}

ZTEST(meshcore_mesh_tdd, test_create_group_datagram_all_types_match_reference)
{
	static const uint8_t kData[] = { 0x07, 0x18, 0x29, 0x3A, 0x4B };
	static const uint8_t kTypes[] = {
		PAYLOAD_TYPE_GRP_TXT,
		PAYLOAD_TYPE_GRP_DATA,
	};
	ReferenceEnv &expected = reference_env_get();
	mesh::GroupChannel channel = {};
	struct meshcore_group_channel actual_channel = {};
	TargetEnv actual;

	reset_fake_runtime();
	channel.hash[0] = 0x5EU;
	for (size_t i = 0; i < PUB_KEY_SIZE; i++) {
		channel.secret[i] = (uint8_t)(0x70U + i);
	}
	memcpy(actual_channel.hash, channel.hash, sizeof(channel.hash));
	memcpy(actual_channel.secret, channel.secret, sizeof(channel.secret));

	for (size_t i = 0; i < sizeof(kTypes); i++) {
		mesh::Packet *expected_packet = expected.mesh.createGroupDatagram(
			kTypes[i], channel, kData, sizeof(kData));
		struct meshcore_packet *actual_packet =
			meshcore_mesh_create_group_datagram(
				&actual.mesh, kTypes[i], &actual_channel, kData,
				sizeof(kData));

		expect_create_result_match_and_free(expected, expected_packet, actual,
						    actual_packet,
						    "createGroupDatagram");
	}
}

ZTEST(meshcore_mesh_tdd, test_create_ack_and_multi_ack_match_reference)
{
	ReferenceEnv &expected = reference_env_get();
	TargetEnv actual;

	reset_fake_runtime();
	{
		mesh::Packet *expected_packet = expected.mesh.createAck(0xA1B2C3D4U);
		struct meshcore_packet *actual_packet =
			meshcore_mesh_create_ack(&actual.mesh, 0xA1B2C3D4U);
		expect_create_result_match_and_free(expected, expected_packet, actual,
						    actual_packet, "createAck");
	}
	{
		mesh::Packet *expected_packet =
			expected.mesh.createMultiAck(0x01020304U, 5U);
		struct meshcore_packet *actual_packet =
			meshcore_mesh_create_multi_ack(&actual.mesh, 0x01020304U, 5U);
		expect_create_result_match_and_free(expected, expected_packet, actual,
						    actual_packet, "createMultiAck");
	}
}

ZTEST(meshcore_mesh_tdd, test_create_advert_matches_reference)
{
	static const uint8_t kAppData[] = { 0xA9, 0xB8, 0xC7, 0xD6 };
	static const uint32_t kRtc = 1700123456U;
	ReferenceEnv &expected = reference_env_get();
	mesh::LocalIdentity sender;
	TargetEnv actual;

	reset_fake_runtime();
	make_reference_local_identity(&sender, kIdentitySeedA, sizeof(kIdentitySeedA));
	sync_target_self_identity(actual, sender);

	meshcore_hal_test_rtc_set_current_time(kRtc);
	mesh::Packet *expected_packet =
		expected.mesh.createAdvert(sender, kAppData, sizeof(kAppData));
	meshcore_hal_test_rtc_set_current_time(kRtc);
	struct meshcore_packet *actual_packet = meshcore_mesh_create_advert(
		&actual.mesh, &actual.mesh.self_id, kAppData, sizeof(kAppData));

	expect_create_result_match_and_free(expected, expected_packet, actual,
					    actual_packet, "createAdvert");
}

ZTEST(meshcore_mesh_tdd,
      test_create_path_return_by_dest_hash_with_extra_matches_reference)
{
	static const uint8_t kSecret[PUB_KEY_SIZE] = {
		0x01, 0x03, 0x05, 0x07, 0x11, 0x13, 0x15, 0x17,
		0x21, 0x23, 0x25, 0x27, 0x31, 0x33, 0x35, 0x37,
		0x41, 0x43, 0x45, 0x47, 0x51, 0x53, 0x55, 0x57,
		0x61, 0x63, 0x65, 0x67, 0x71, 0x73, 0x75, 0x77,
	};
	static const uint8_t kPath[] = { 0x12, 0x34, 0x56 };
	static const uint8_t kExtra[] = { 0xD1, 0xE2, 0xF3 };
	ReferenceEnv &expected = reference_env_get();
	mesh::LocalIdentity receiver;
	mesh::Identity dest;
	uint8_t dest_hash[PATH_HASH_SIZE] = { 0 };
	struct meshcore_identity actual_dest;
	uint8_t actual_dest_hash[MESHCORE_CHANNEL_HASH_BYTES] = { 0 };
	TargetEnv actual;

	reset_fake_runtime();
	make_reference_local_identity(&receiver, kIdentitySeedB, sizeof(kIdentitySeedB));
	dest = mesh::Identity(receiver.pub_key);
	dest.copyHashTo(dest_hash);
	meshcore_identity_init_from_pub_key(&actual_dest, dest.pub_key);
	meshcore_identity_copy_hash_to(&actual_dest, actual_dest_hash);

	mesh::Packet *expected_packet = expected.mesh.createPathReturn(
		dest_hash, kSecret, kPath, 3U, PAYLOAD_TYPE_REQ, kExtra,
		sizeof(kExtra));
	struct meshcore_packet *actual_packet =
		meshcore_mesh_create_path_return_by_dest_hash(
			&actual.mesh, actual_dest_hash, kSecret, kPath, 3U,
			PAYLOAD_TYPE_REQ, kExtra, sizeof(kExtra));

	expect_create_result_match_and_free(expected, expected_packet, actual,
					    actual_packet,
					    "createPathReturnByDestHash");
}

ZTEST(meshcore_mesh_tdd,
      test_create_path_return_by_identity_without_extra_matches_reference)
{
	static const uint8_t kSecret[PUB_KEY_SIZE] = {
		0x91, 0x92, 0x93, 0x94, 0x81, 0x82, 0x83, 0x84,
		0x71, 0x72, 0x73, 0x74, 0x61, 0x62, 0x63, 0x64,
		0x51, 0x52, 0x53, 0x54, 0x41, 0x42, 0x43, 0x44,
		0x31, 0x32, 0x33, 0x34, 0x21, 0x22, 0x23, 0x24,
	};
	static const uint8_t kPath[] = { 0x10, 0x11, 0x20, 0x21 };
	static const uint8_t kRngBytes[] = { 0x99, 0x88, 0x77, 0x66 };
	ReferenceEnv &expected = reference_env_get();
	mesh::LocalIdentity receiver;
	mesh::Identity dest;
	struct meshcore_identity actual_dest;
	TargetEnv actual;

	reset_fake_runtime();
	make_reference_local_identity(&receiver, kIdentitySeedB, sizeof(kIdentitySeedB));
	dest = mesh::Identity(receiver.pub_key);
	meshcore_identity_init_from_pub_key(&actual_dest, dest.pub_key);

	meshcore_hal_test_rng_set_bytes(kRngBytes, sizeof(kRngBytes));
	mesh::Packet *expected_packet =
		expected.mesh.createPathReturn(dest, kSecret, kPath, 66U, 0U, NULL, 0U);
	meshcore_hal_test_rng_set_bytes(kRngBytes, sizeof(kRngBytes));
	struct meshcore_packet *actual_packet =
		meshcore_mesh_create_path_return_by_identity(
			&actual.mesh, &actual_dest, kSecret, kPath, 66U, 0U, NULL, 0U);

	expect_create_result_match_and_free(expected, expected_packet, actual,
					    actual_packet, "createPathReturnByIdentity");
}

ZTEST(meshcore_mesh_tdd, test_create_raw_control_trace_match_reference)
{
	static const uint8_t kRawData[] = { 0x01, 0x23, 0x45, 0x67, 0x89 };
	static const uint8_t kControlData[] = { 0x80, 0x55, 0x66 };
	ReferenceEnv &expected = reference_env_get();
	TargetEnv actual;

	reset_fake_runtime();
	{
		mesh::Packet *expected_packet =
			expected.mesh.createRawData(kRawData, sizeof(kRawData));
		struct meshcore_packet *actual_packet =
			meshcore_mesh_create_raw_data(&actual.mesh, kRawData, sizeof(kRawData));
		expect_create_result_match_and_free(expected, expected_packet, actual,
						    actual_packet, "createRawData");
	}
	{
		mesh::Packet *expected_packet = expected.mesh.createTrace(
			0x12345678U, 0xABCDEF00U, 0x05U);
		struct meshcore_packet *actual_packet = meshcore_mesh_create_trace(
			&actual.mesh, 0x12345678U, 0xABCDEF00U, 0x05U);
		expect_create_result_match_and_free(expected, expected_packet, actual,
						    actual_packet, "createTrace");
	}
	{
		mesh::Packet *expected_packet = expected.mesh.createControlData(
			kControlData, sizeof(kControlData));
		struct meshcore_packet *actual_packet = meshcore_mesh_create_control_data(
			&actual.mesh, kControlData, sizeof(kControlData));
		expect_create_result_match_and_free(expected, expected_packet, actual,
						    actual_packet, "createControlData");
	}
}

ZTEST(meshcore_mesh_tdd, test_create_zero_length_payload_variants_match_reference)
{
	static const uint8_t kSecret[PUB_KEY_SIZE] = {
		0x10, 0x20, 0x30, 0x40, 0x11, 0x21, 0x31, 0x41,
		0x12, 0x22, 0x32, 0x42, 0x13, 0x23, 0x33, 0x43,
		0x14, 0x24, 0x34, 0x44, 0x15, 0x25, 0x35, 0x45,
		0x16, 0x26, 0x36, 0x46, 0x17, 0x27, 0x37, 0x47,
	};
	ReferenceEnv &expected = reference_env_get();
	mesh::LocalIdentity sender;
	mesh::LocalIdentity receiver;
	mesh::Identity dest;
	mesh::GroupChannel channel = {};
	struct meshcore_local_identity actual_sender;
	struct meshcore_identity actual_dest;
	struct meshcore_group_channel actual_channel = {};
	TargetEnv actual;

	reset_fake_runtime();
	make_reference_local_identity(&sender, kIdentitySeedA, sizeof(kIdentitySeedA));
	make_reference_local_identity(&receiver, kIdentitySeedB, sizeof(kIdentitySeedB));
	dest = mesh::Identity(receiver.pub_key);
	{
		uint8_t full_layout[PRV_KEY_SIZE + PUB_KEY_SIZE] = { 0 };
		size_t len = sender.writeTo(full_layout, sizeof(full_layout));

		zassert_equal(sizeof(full_layout), len,
			      "reference identity serialize length mismatch");
		memset(&actual_sender, 0, sizeof(actual_sender));
		meshcore_local_identity_read_from(&actual_sender, full_layout, len);
	}
	meshcore_identity_init_from_pub_key(&actual_dest, dest.pub_key);

	channel.hash[0] = 0x5EU;
	for (size_t i = 0; i < PUB_KEY_SIZE; i++) {
		channel.secret[i] = (uint8_t)(0x70U + i);
	}
	memcpy(actual_channel.hash, channel.hash, sizeof(channel.hash));
	memcpy(actual_channel.secret, channel.secret, sizeof(channel.secret));

	{
		mesh::Packet *expected_packet = expected.mesh.createDatagram(
			PAYLOAD_TYPE_TXT_MSG, dest, kSecret, nullptr, 0U);
		struct meshcore_packet *actual_packet = meshcore_mesh_create_datagram(
			&actual.mesh, PAYLOAD_TYPE_TXT_MSG, &actual_dest, kSecret, NULL, 0U);
		expect_create_result_match_and_free(expected, expected_packet, actual,
						    actual_packet,
						    "createDatagram empty");
	}
	{
		mesh::Packet *expected_packet = expected.mesh.createAnonDatagram(
			PAYLOAD_TYPE_ANON_REQ, sender, dest, kSecret, nullptr, 0U);
		struct meshcore_packet *actual_packet =
			meshcore_mesh_create_anon_datagram(&actual.mesh,
							 PAYLOAD_TYPE_ANON_REQ,
							 &actual_sender,
							 &actual_dest, kSecret, NULL,
							 0U);
		expect_create_result_match_and_free(expected, expected_packet, actual,
						    actual_packet,
						    "createAnonDatagram empty");
	}
	{
		mesh::Packet *expected_packet = expected.mesh.createGroupDatagram(
			PAYLOAD_TYPE_GRP_DATA, channel, nullptr, 0U);
		struct meshcore_packet *actual_packet =
			meshcore_mesh_create_group_datagram(
				&actual.mesh, PAYLOAD_TYPE_GRP_DATA, &actual_channel, NULL,
				0U);
		expect_create_result_match_and_free(expected, expected_packet, actual,
						    actual_packet,
						    "createGroupDatagram empty");
	}
	{
		mesh::Packet *expected_packet = expected.mesh.createRawData(nullptr, 0U);
		struct meshcore_packet *actual_packet =
			meshcore_mesh_create_raw_data(&actual.mesh, NULL, 0U);
		expect_create_result_match_and_free(expected, expected_packet, actual,
						    actual_packet, "createRawData empty");
	}
	{
		mesh::Packet *expected_packet =
			expected.mesh.createControlData(nullptr, 0U);
		struct meshcore_packet *actual_packet =
			meshcore_mesh_create_control_data(&actual.mesh, NULL, 0U);
		expect_create_result_match_and_free(expected, expected_packet, actual,
						    actual_packet,
						    "createControlData empty");
	}
}

ZTEST(meshcore_mesh_tdd, test_create_datagram_invalid_type_returns_null)
{
	static const uint8_t kSecret[PUB_KEY_SIZE] = {
		0x10, 0x20, 0x30, 0x40, 0x11, 0x21, 0x31, 0x41,
		0x12, 0x22, 0x32, 0x42, 0x13, 0x23, 0x33, 0x43,
		0x14, 0x24, 0x34, 0x44, 0x15, 0x25, 0x35, 0x45,
		0x16, 0x26, 0x36, 0x46, 0x17, 0x27, 0x37, 0x47,
	};
	static const uint8_t kData[] = { 0x01, 0x02, 0x03 };
	ReferenceEnv &expected = reference_env_get();
	mesh::LocalIdentity receiver;
	mesh::Identity dest;
	struct meshcore_identity actual_dest;
	TargetEnv actual;

	reset_fake_runtime();
	make_reference_local_identity(&receiver, kIdentitySeedB, sizeof(kIdentitySeedB));
	dest = mesh::Identity(receiver.pub_key);
	meshcore_identity_init_from_pub_key(&actual_dest, dest.pub_key);

	mesh::Packet *expected_packet =
		expected.mesh.createDatagram(PAYLOAD_TYPE_ACK, dest, kSecret, kData,
					    sizeof(kData));
	struct meshcore_packet *actual_packet = meshcore_mesh_create_datagram(
		&actual.mesh, PAYLOAD_TYPE_ACK, &actual_dest, kSecret, kData,
		sizeof(kData));
	expect_create_result_match_and_free(expected, expected_packet, actual,
					    actual_packet, "createDatagramInvalidType");
}

ZTEST(meshcore_mesh_tdd, test_create_datagram_too_long_returns_null)
{
	static uint8_t kTooLongData[MESHCORE_PACKET_PAYLOAD_MAX_LEN + 1U];
	static const uint8_t kSecret[PUB_KEY_SIZE] = {
		0x01, 0x11, 0x21, 0x31, 0x41, 0x51, 0x61, 0x71,
		0x81, 0x91, 0xA1, 0xB1, 0xC1, 0xD1, 0xE1, 0xF1,
		0x02, 0x12, 0x22, 0x32, 0x42, 0x52, 0x62, 0x72,
		0x82, 0x92, 0xA2, 0xB2, 0xC2, 0xD2, 0xE2, 0xF2,
	};
	ReferenceEnv &expected = reference_env_get();
	mesh::LocalIdentity receiver;
	mesh::Identity dest;
	struct meshcore_identity actual_dest;
	TargetEnv actual;

	reset_fake_runtime();
	for (size_t i = 0; i < sizeof(kTooLongData); i++) {
		kTooLongData[i] = (uint8_t)(i & 0xFFU);
	}
	make_reference_local_identity(&receiver, kIdentitySeedA, sizeof(kIdentitySeedA));
	dest = mesh::Identity(receiver.pub_key);
	meshcore_identity_init_from_pub_key(&actual_dest, dest.pub_key);

	mesh::Packet *expected_packet = expected.mesh.createDatagram(
		PAYLOAD_TYPE_TXT_MSG, dest, kSecret, kTooLongData, sizeof(kTooLongData));
	struct meshcore_packet *actual_packet = meshcore_mesh_create_datagram(
		&actual.mesh, PAYLOAD_TYPE_TXT_MSG, &actual_dest, kSecret, kTooLongData,
		sizeof(kTooLongData));
	expect_create_result_match_and_free(expected, expected_packet, actual,
					    actual_packet, "createDatagramTooLong");
}

ZTEST(meshcore_mesh_tdd, test_create_path_return_too_long_returns_null)
{
	static const uint8_t kSecret[PUB_KEY_SIZE] = {
		0x55, 0x56, 0x57, 0x58, 0x65, 0x66, 0x67, 0x68,
		0x75, 0x76, 0x77, 0x78, 0x85, 0x86, 0x87, 0x88,
		0x95, 0x96, 0x97, 0x98, 0xA5, 0xA6, 0xA7, 0xA8,
		0xB5, 0xB6, 0xB7, 0xB8, 0xC5, 0xC6, 0xC7, 0xC8,
	};
	static uint8_t kExtra[MESHCORE_PACKET_PAYLOAD_MAX_LEN];
	static uint8_t kPath[63];
	ReferenceEnv &expected = reference_env_get();
	mesh::LocalIdentity receiver;
	mesh::Identity dest;
	uint8_t dest_hash[PATH_HASH_SIZE] = { 0 };
	struct meshcore_identity actual_dest;
	uint8_t actual_dest_hash[MESHCORE_CHANNEL_HASH_BYTES] = { 0 };
	TargetEnv actual;

	reset_fake_runtime();
	for (size_t i = 0; i < sizeof(kExtra); i++) {
		kExtra[i] = (uint8_t)(0xA0U + (i & 0x0FU));
	}
	for (size_t i = 0; i < sizeof(kPath); i++) {
		kPath[i] = (uint8_t)(0x20U + i);
	}
	make_reference_local_identity(&receiver, kIdentitySeedB, sizeof(kIdentitySeedB));
	dest = mesh::Identity(receiver.pub_key);
	dest.copyHashTo(dest_hash);
	meshcore_identity_init_from_pub_key(&actual_dest, dest.pub_key);
	meshcore_identity_copy_hash_to(&actual_dest, actual_dest_hash);

	mesh::Packet *expected_packet = expected.mesh.createPathReturn(
		dest_hash, kSecret, kPath, 63U, PAYLOAD_TYPE_RESPONSE, kExtra,
		sizeof(kExtra));
	struct meshcore_packet *actual_packet =
		meshcore_mesh_create_path_return_by_dest_hash(
			&actual.mesh, actual_dest_hash, kSecret, kPath, 63U,
			PAYLOAD_TYPE_RESPONSE, kExtra, sizeof(kExtra));
	expect_create_result_match_and_free(expected, expected_packet, actual,
					    actual_packet, "createPathReturnTooLong");
}

ZTEST(meshcore_mesh_tdd, test_create_pool_exhausted_returns_null)
{
	ReferenceEnv &expected = reference_env_get();
	TargetEnv actual;
	mesh::Packet *expected_packets[kPoolSize] = { nullptr };
	struct meshcore_packet *actual_packets[kPoolSize] = { 0 };

	reset_fake_runtime();
	for (int i = 0; i < kPoolSize; i++) {
		expected_packets[i] =
			expected.mesh.createAck((uint32_t)(0x10000000U + (uint32_t)i));
		actual_packets[i] =
			meshcore_mesh_create_ack(&actual.mesh,
						(uint32_t)(0x10000000U + (uint32_t)i));
		zassert_not_null(expected_packets[i], "reference pool fill failed at %d",
				 i);
		zassert_not_null(actual_packets[i], "target pool fill failed at %d", i);
	}

	{
		mesh::Packet *expected_overflow = expected.mesh.createAck(0xDEADBEEFU);
		struct meshcore_packet *actual_overflow =
			meshcore_mesh_create_ack(&actual.mesh, 0xDEADBEEFU);
		expect_create_result_match_and_free(expected, expected_overflow, actual,
						    actual_overflow,
						    "createPoolExhausted");
	}

	for (int i = 0; i < kPoolSize; i++) {
		expected.manager.free(expected_packets[i]);
		meshcore_packet_queue_manager_free(&actual.manager, actual_packets[i]);
	}
}

ZTEST(meshcore_mesh_tdd,
      test_send_flood_invalid_path_hash_size_releases_packet_target_only)
{
	TargetEnv actual;
	int free_before;
	struct meshcore_packet *packet;

	reset_fake_runtime();
	free_before = meshcore_packet_queue_manager_get_free_count(&actual.manager);
	packet = meshcore_mesh_create_ack(&actual.mesh, 0x12345678U);
	zassert_not_null(packet, "target createAck failed");
	zassert_equal(free_before - 1,
		      meshcore_packet_queue_manager_get_free_count(&actual.manager),
		      "packet allocation should consume one free slot");

	meshcore_mesh_send_flood(&actual.mesh, packet, 0U, 0U);

	zassert_equal(free_before,
		      meshcore_packet_queue_manager_get_free_count(&actual.manager),
		      "invalid sendFlood should release consumed packet");
	zassert_equal(0,
		      meshcore_packet_queue_manager_get_outbound_total(&actual.manager),
		      "invalid sendFlood should not queue outbound");
}

ZTEST(meshcore_mesh_tdd,
      test_send_direct_trace_overflow_releases_packet_target_only)
{
	TargetEnv actual;
	int free_before;
	struct meshcore_packet *packet;
	uint8_t path[MESHCORE_PACKET_PAYLOAD_MAX_LEN] = { 0 };

	reset_fake_runtime();
	free_before = meshcore_packet_queue_manager_get_free_count(&actual.manager);
	packet = meshcore_mesh_create_trace(&actual.mesh, 0x12345678U, 0xABCDEF00U,
					     0x05U);
	zassert_not_null(packet, "target createTrace failed");
	zassert_equal(free_before - 1,
		      meshcore_packet_queue_manager_get_free_count(&actual.manager),
		      "packet allocation should consume one free slot");

	meshcore_mesh_send_direct(&actual.mesh, packet, path, sizeof(path), 0U);

	zassert_equal(free_before,
		      meshcore_packet_queue_manager_get_free_count(&actual.manager),
		      "overflow TRACE sendDirect should release consumed packet");
	zassert_equal(0,
		      meshcore_packet_queue_manager_get_outbound_total(&actual.manager),
		      "overflow TRACE sendDirect should not queue outbound");
}

ZTEST(meshcore_mesh_tdd,
      test_send_zero_hop_null_transport_codes_releases_packet_target_only)
{
	TargetEnv actual;
	int free_before;
	struct meshcore_packet *packet;

	reset_fake_runtime();
	free_before = meshcore_packet_queue_manager_get_free_count(&actual.manager);
	packet = meshcore_mesh_create_ack(&actual.mesh, 0xCAFEBABEU);
	zassert_not_null(packet, "target createAck failed");
	zassert_equal(free_before - 1,
		      meshcore_packet_queue_manager_get_free_count(&actual.manager),
		      "packet allocation should consume one free slot");

	meshcore_mesh_send_zero_hop_by_transport_codes(&actual.mesh, packet, nullptr,
						       0U);

	zassert_equal(free_before,
		      meshcore_packet_queue_manager_get_free_count(&actual.manager),
		      "invalid sendZeroHop transport should release consumed packet");
	zassert_equal(0,
		      meshcore_packet_queue_manager_get_outbound_total(&actual.manager),
		      "invalid sendZeroHop transport should not queue outbound");
}

ZTEST(meshcore_mesh_tdd,
      test_send_transport_flood_preserves_hash_size_1_target_only)
{
	static const uint16_t kTransportCodes[2] = {
		0x534eU,
		0x0001U,
	};
	TargetEnv actual;
	struct meshcore_packet *packet;
	const struct meshcore_packet *queued;

	reset_fake_runtime();
	packet = meshcore_mesh_create_ack(&actual.mesh, 0x01020304U);
	zassert_not_null(packet, "target createAck failed");
	meshcore_mesh_send_flood_by_transport_codes(&actual.mesh, packet, kTransportCodes,
						    0U, 1U);

	zassert_equal(1,
		      meshcore_packet_queue_manager_get_outbound_total(&actual.manager),
		      "transport flood should queue exactly one packet");
	queued = meshcore_packet_queue_manager_get_outbound_by_idx(&actual.manager, 0);
	zassert_not_null(queued, "queued transport flood packet missing");
	zassert_equal(1U, meshcore_packet_get_path_hash_size(queued),
		      "transport flood should preserve hash-size 1");
	zassert_true(meshcore_packet_has_transport_codes(queued),
		     "transport codes should be present");
}

ZTEST(meshcore_mesh_tdd,
      test_send_transport_flood_preserves_hash_size_2_target_only)
{
	static const uint16_t kTransportCodes[2] = {
		0x534eU,
		0x0001U,
	};
	TargetEnv actual;
	struct meshcore_packet *packet;
	const struct meshcore_packet *queued;

	reset_fake_runtime();
	packet = meshcore_mesh_create_ack(&actual.mesh, 0x11121314U);
	zassert_not_null(packet, "target createAck failed");
	meshcore_mesh_send_flood_by_transport_codes(&actual.mesh, packet, kTransportCodes,
						    0U, 2U);

	zassert_equal(1,
		      meshcore_packet_queue_manager_get_outbound_total(&actual.manager),
		      "transport flood should queue exactly one packet");
	queued = meshcore_packet_queue_manager_get_outbound_by_idx(&actual.manager, 0);
	zassert_not_null(queued, "queued transport flood packet missing");
	zassert_equal(2U, meshcore_packet_get_path_hash_size(queued),
		      "transport flood should preserve hash-size 2");
	zassert_true(meshcore_packet_has_transport_codes(queued),
		     "transport codes should be present");
}

ZTEST(meshcore_mesh_tdd,
      test_send_transport_flood_preserves_hash_size_3_target_only)
{
	static const uint16_t kTransportCodes[2] = {
		0x534eU,
		0x0001U,
	};
	TargetEnv actual;
	struct meshcore_packet *packet;
	const struct meshcore_packet *queued;

	reset_fake_runtime();
	packet = meshcore_mesh_create_ack(&actual.mesh, 0x21222324U);
	zassert_not_null(packet, "target createAck failed");
	meshcore_mesh_send_flood_by_transport_codes(&actual.mesh, packet, kTransportCodes,
						    0U, 3U);

	zassert_equal(1,
		      meshcore_packet_queue_manager_get_outbound_total(&actual.manager),
		      "transport flood should queue exactly one packet");
	queued = meshcore_packet_queue_manager_get_outbound_by_idx(&actual.manager, 0);
	zassert_not_null(queued, "queued transport flood packet missing");
	zassert_equal(3U, meshcore_packet_get_path_hash_size(queued),
		      "transport flood should preserve hash-size 3");
	zassert_true(meshcore_packet_has_transport_codes(queued),
		     "transport codes should be present");
}

ZTEST(meshcore_mesh_tdd, test_route_recv_packet_flood_forward_matches_reference)
{
	ReferenceEnv &expected = reference_env_get();
	mesh::Packet expected_packet;
	mesh::DispatcherAction expected_action;

	reset_fake_runtime();
	expected.script.override_allow_packet_forward = true;
	expected.script.allow_packet_forward_value = true;
	expected.script.override_get_retransmit_delay = true;
	expected.script.get_retransmit_delay_value = 77U;
	for (int i = 0; i < PUB_KEY_SIZE; i++) {
		expected.mesh.self_id.pub_key[i] = (uint8_t)(0xA0 + i);
	}
	init_reference_packet(&expected_packet, ROUTE_TYPE_FLOOD, 0x33U, 5U);
	expected_packet.setPathHashSizeAndCount(1U, 0U);
	expected_action = expected.mesh.routeRecvForTest(&expected_packet);

	TargetEnv actual;
	struct meshcore_packet actual_packet;
	meshcore_dispatcher_action actual_action;

	reset_fake_runtime();
	actual.script.override_allow_packet_forward = true;
	actual.script.allow_packet_forward_value = true;
	actual.script.override_get_retransmit_delay = true;
	actual.script.get_retransmit_delay_value = 77U;
	actual.sync_script_to_hal();
	for (size_t i = 0; i < MESHCORE_PUBLIC_KEY_SIZE; i++) {
		actual.mesh.self_id.identity.pub_key[i] = (uint8_t)(0xA0 + i);
	}
	init_target_packet(&actual_packet, ROUTE_TYPE_FLOOD, 0x33U, 5U);
	meshcore_packet_set_path_hash_size_and_count(&actual_packet, 1U, 0U);
	actual_action = meshcore_mesh_route_recv_packet(&actual.mesh, &actual_packet);

	zassert_equal(expected_action, actual_action, "route action mismatch");
	zassert_equal(expected_packet.path_len, actual_packet.path_len,
		      "path_len mismatch");
	zassert_equal(expected_packet.path[0], actual_packet.path[0],
		      "first path hash mismatch");
	zassert_equal(expected_packet.path[0], (uint8_t)0xA0,
		      "expected appended self hash mismatch");
}

ZTEST(meshcore_mesh_tdd,
      test_route_recv_packet_path_overflow_release_matches_reference)
{
	ReferenceEnv &expected = reference_env_get();
	mesh::Packet expected_packet;
	mesh::DispatcherAction expected_action;

	reset_fake_runtime();
	expected.script.override_allow_packet_forward = true;
	expected.script.allow_packet_forward_value = true;
	expected.script.override_get_retransmit_delay = true;
	expected.script.get_retransmit_delay_value = 91U;
	init_reference_packet(&expected_packet, ROUTE_TYPE_FLOOD, 0x44U, 6U);
	expected_packet.setPathHashSizeAndCount(2U, 32U);
	for (int i = 0; i < MAX_PATH_SIZE; i++) {
		expected_packet.path[i] = (uint8_t)(0x40 + i);
	}
	expected_action = expected.mesh.routeRecvForTest(&expected_packet);

	TargetEnv actual;
	struct meshcore_packet actual_packet;
	meshcore_dispatcher_action actual_action;

	reset_fake_runtime();
	actual.script.override_allow_packet_forward = true;
	actual.script.allow_packet_forward_value = true;
	actual.script.override_get_retransmit_delay = true;
	actual.script.get_retransmit_delay_value = 91U;
	actual.sync_script_to_hal();
	init_target_packet(&actual_packet, ROUTE_TYPE_FLOOD, 0x44U, 6U);
	meshcore_packet_set_path_hash_size_and_count(&actual_packet, 2U, 32U);
	for (size_t i = 0; i < MESHCORE_MAX_PATH_LEN; i++) {
		actual_packet.path[i] = (uint8_t)(0x40 + i);
	}
	actual_action = meshcore_mesh_route_recv_packet(&actual.mesh, &actual_packet);

	zassert_equal(expected_action, actual_action, "route action mismatch");
	zassert_equal((mesh::DispatcherAction)ACTION_RELEASE, expected_action,
		      "expected reference action should be release");
	zassert_equal(expected_packet.path_len, actual_packet.path_len,
		      "path_len mismatch");
	zassert_mem_equal(expected_packet.path, actual_packet.path, MAX_PATH_SIZE,
			  "path bytes should remain unchanged");
}

ZTEST(meshcore_mesh_tdd,
      test_on_recv_packet_flood_ack_routes_when_not_seen_matches_reference)
{
	static const uint32_t kAckCrc = 0x11223344U;
	ReferenceEnv &expected = reference_env_get();
	mesh::Packet expected_packet;
	mesh::DispatcherAction expected_action;

	reset_fake_runtime();
	expected.script.override_allow_packet_forward = true;
	expected.script.allow_packet_forward_value = true;
	expected.script.override_get_retransmit_delay = true;
	expected.script.get_retransmit_delay_value = 66U;
	for (int i = 0; i < PUB_KEY_SIZE; i++) {
		expected.mesh.self_id.pub_key[i] = (uint8_t)(0xB0 + i);
	}
	init_reference_ack_packet(&expected_packet, ROUTE_TYPE_FLOOD, kAckCrc);
	expected_action = expected.mesh.onRecvForTest(&expected_packet);

	TargetEnv actual;
	struct meshcore_packet actual_packet;
	meshcore_dispatcher_action actual_action;

	reset_fake_runtime();
	actual.script.override_allow_packet_forward = true;
	actual.script.allow_packet_forward_value = true;
	actual.script.override_get_retransmit_delay = true;
	actual.script.get_retransmit_delay_value = 66U;
	actual.sync_script_to_hal();
	for (size_t i = 0; i < MESHCORE_PUBLIC_KEY_SIZE; i++) {
		actual.mesh.self_id.identity.pub_key[i] = (uint8_t)(0xB0 + i);
	}
	init_target_ack_packet(&actual_packet, ROUTE_TYPE_FLOOD, kAckCrc);
	actual_action = meshcore_mesh_on_recv_packet(&actual.mesh, &actual_packet);

	zassert_equal(expected_action, actual_action, "onRecv action mismatch");
	zassert_equal(expected_packet.path_len, actual_packet.path_len,
		      "path_len mismatch");
	zassert_equal(expected_packet.path[0], actual_packet.path[0],
		      "first path hash mismatch");
}

ZTEST(meshcore_mesh_tdd,
      test_on_recv_packet_flood_ack_duplicate_release_matches_reference)
{
	static const uint32_t kAckCrc = 0x55667788U;
	ReferenceEnv &expected = reference_env_get();
	mesh::Packet expected_packet1;
	mesh::Packet expected_packet2;
	mesh::DispatcherAction expected_first;
	mesh::DispatcherAction expected_second;

	reset_fake_runtime();
	expected.script.override_allow_packet_forward = true;
	expected.script.allow_packet_forward_value = true;
	expected.script.override_get_retransmit_delay = true;
	expected.script.get_retransmit_delay_value = 42U;
	init_reference_ack_packet(&expected_packet1, ROUTE_TYPE_FLOOD, kAckCrc);
	expected_first = expected.mesh.onRecvForTest(&expected_packet1);
	init_reference_ack_packet(&expected_packet2, ROUTE_TYPE_FLOOD, kAckCrc);
	expected_second = expected.mesh.onRecvForTest(&expected_packet2);

	TargetEnv actual;
	struct meshcore_packet actual_packet1;
	struct meshcore_packet actual_packet2;
	meshcore_dispatcher_action actual_first;
	meshcore_dispatcher_action actual_second;

	reset_fake_runtime();
	actual.script.override_allow_packet_forward = true;
	actual.script.allow_packet_forward_value = true;
	actual.script.override_get_retransmit_delay = true;
	actual.script.get_retransmit_delay_value = 42U;
	actual.sync_script_to_hal();
	init_target_ack_packet(&actual_packet1, ROUTE_TYPE_FLOOD, kAckCrc);
	actual_first = meshcore_mesh_on_recv_packet(&actual.mesh, &actual_packet1);
	init_target_ack_packet(&actual_packet2, ROUTE_TYPE_FLOOD, kAckCrc);
	actual_second = meshcore_mesh_on_recv_packet(&actual.mesh, &actual_packet2);

	zassert_equal(expected_first, actual_first, "first onRecv action mismatch");
	zassert_equal(expected_second, actual_second, "second onRecv action mismatch");
	zassert_equal((mesh::DispatcherAction)ACTION_RELEASE, expected_second,
		      "duplicate ACK should release");
}

ZTEST(meshcore_mesh_tdd,
      test_on_recv_packet_filter_short_circuit_matches_reference)
{
	static const uint32_t kAckCrc = 0xA1B2C3D4U;
	ReferenceEnv &expected = reference_env_get();
	mesh::Packet expected_packet;
	mesh::DispatcherAction expected_action;

	reset_fake_runtime();
	expected.script.override_filter_recv_flood_packet = true;
	expected.script.filter_recv_flood_packet_value = true;
	expected.script.override_allow_packet_forward = true;
	expected.script.allow_packet_forward_value = true;
	init_reference_ack_packet(&expected_packet, ROUTE_TYPE_FLOOD, kAckCrc);
	expected_action = expected.mesh.onRecvForTest(&expected_packet);

	TargetEnv actual;
	struct meshcore_packet actual_packet;
	meshcore_dispatcher_action actual_action;

	reset_fake_runtime();
	actual.script.override_filter_recv_flood_packet = true;
	actual.script.filter_recv_flood_packet_value = true;
	actual.script.override_allow_packet_forward = true;
	actual.script.allow_packet_forward_value = true;
	actual.sync_script_to_hal();
	init_target_ack_packet(&actual_packet, ROUTE_TYPE_FLOOD, kAckCrc);
	actual_action = meshcore_mesh_on_recv_packet(&actual.mesh, &actual_packet);

	zassert_equal(expected_action, actual_action, "filtered onRecv action mismatch");
	zassert_equal((mesh::DispatcherAction)ACTION_RELEASE, expected_action,
		      "filtered packet should release");
	zassert_equal(expected_packet.path_len, actual_packet.path_len,
		      "filtered packet should not update path");
}

ZTEST(meshcore_mesh_tdd,
      test_on_recv_packet_direct_trace_end_path_matches_reference)
{
	ReferenceEnv &expected = reference_env_get();
	mesh::Packet input_packet;
	mesh::Packet expected_packet;
	mesh::DispatcherAction expected_action;

	reset_fake_runtime();
	init_reference_packet(&input_packet, ROUTE_TYPE_DIRECT, 0x31U, 9U);
	input_packet.header = (PAYLOAD_VER_1 << PH_VER_SHIFT) |
			      (PAYLOAD_TYPE_TRACE << PH_TYPE_SHIFT) |
			      ROUTE_TYPE_DIRECT;
	input_packet.path_len = 1U;
	input_packet.payload_len = 9U;
	input_packet.payload[8] = 0U;
	expected_packet = input_packet;
	expected_action = expected.mesh.onRecvForTest(&expected_packet);

	TargetEnv actual;
	struct meshcore_packet actual_packet;
	meshcore_dispatcher_action actual_action;

	reset_fake_runtime();
	actual.sync_script_to_hal();
	clone_target_packet_from_reference(&actual_packet, &input_packet);
	actual_action = meshcore_mesh_on_recv_packet(&actual.mesh, &actual_packet);

	zassert_equal(expected_action, actual_action, "direct trace action mismatch");
	expect_script_observation_match(expected.script, actual.script,
					"direct trace callbacks");
}

ZTEST(meshcore_mesh_tdd,
      test_on_recv_packet_direct_trace_large_offset_matches_reference)
{
	static const uint32_t kTraceTag = 0x01020304U;
	static const uint32_t kAuthCode = 0xA0B0C0D0U;
	static const uint8_t kTraceFlagsHashSize8 = 0x03U;
	ReferenceEnv &expected = reference_env_get();
	mesh::Packet input_packet;
	mesh::Packet expected_packet;
	mesh::DispatcherAction expected_action;

	reset_fake_runtime();
	input_packet = mesh::Packet();
	input_packet.header = (PAYLOAD_VER_1 << PH_VER_SHIFT) |
			      (PAYLOAD_TYPE_TRACE << PH_TYPE_SHIFT) |
			      ROUTE_TYPE_DIRECT;
	input_packet.path_len = 33U;
	input_packet.payload_len = sizeof(input_packet.payload);
	memset(input_packet.path, 0x44, sizeof(input_packet.path));
	memset(input_packet.payload, 0xA5, sizeof(input_packet.payload));
	memcpy(&input_packet.payload[0], &kTraceTag, sizeof(kTraceTag));
	memcpy(&input_packet.payload[4], &kAuthCode, sizeof(kAuthCode));
	input_packet.payload[8] = kTraceFlagsHashSize8;
	expected_packet = input_packet;
	expected_action = expected.mesh.onRecvForTest(&expected_packet);

	TargetEnv actual;
	struct meshcore_packet actual_packet;
	meshcore_dispatcher_action actual_action;

	reset_fake_runtime();
	actual.sync_script_to_hal();
	clone_target_packet_from_reference(&actual_packet, &input_packet);
	actual_action = meshcore_mesh_on_recv_packet(&actual.mesh, &actual_packet);

	zassert_equal(expected_action, actual_action,
		      "large-offset direct trace action mismatch");
	expect_script_observation_match(expected.script, actual.script,
					"large-offset direct trace callbacks");
}

ZTEST(meshcore_mesh_tdd,
      test_on_recv_packet_direct_control_zero_hop_matches_reference)
{
	ReferenceEnv &expected = reference_env_get();
	mesh::Packet expected_packet;
	mesh::DispatcherAction expected_action;

	reset_fake_runtime();
	init_reference_packet(&expected_packet, ROUTE_TYPE_DIRECT, 0x28U, 1U);
	expected_packet.header = (PAYLOAD_VER_1 << PH_VER_SHIFT) |
				 (PAYLOAD_TYPE_CONTROL << PH_TYPE_SHIFT) |
				 ROUTE_TYPE_DIRECT;
	expected_packet.path_len = 0U;
	expected_packet.payload_len = 1U;
	expected_packet.payload[0] = 0x80U;
	expected_action = expected.mesh.onRecvForTest(&expected_packet);

	TargetEnv actual;
	struct meshcore_packet actual_packet;
	meshcore_dispatcher_action actual_action;

	reset_fake_runtime();
	actual.sync_script_to_hal();
	clone_target_packet_from_reference(&actual_packet, &expected_packet);
	actual_action = meshcore_mesh_on_recv_packet(&actual.mesh, &actual_packet);

	zassert_equal(expected_action, actual_action, "direct control action mismatch");
	expect_script_observation_match(expected.script, actual.script,
					"direct control callbacks");
}

ZTEST(meshcore_mesh_tdd,
      test_on_recv_packet_direct_ack_routes_back_matches_reference)
{
	static const uint32_t kAckCrc = 0x10203040U;
	ReferenceEnv &expected = reference_env_get();
	mesh::Packet input_packet;
	mesh::Packet expected_packet;
	mesh::DispatcherAction expected_action;

	reset_fake_runtime();
	expected.script.override_allow_packet_forward = true;
	expected.script.allow_packet_forward_value = true;
	expected.script.override_get_extra_ack_transmit_count = true;
	expected.script.get_extra_ack_transmit_count_value = 1U;
	expected.script.override_get_direct_retransmit_delay = true;
	expected.script.get_direct_retransmit_delay_value = 17U;
	input_packet = mesh::Packet();
	input_packet.header = (PAYLOAD_VER_1 << PH_VER_SHIFT) |
			      (PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT) |
			      ROUTE_TYPE_DIRECT;
	input_packet.setPathHashSizeAndCount(1U, 2U);
	input_packet.path[0] = expected.mesh.self_id.pub_key[0];
	input_packet.path[1] = 0x4CU;
	memcpy(input_packet.payload, &kAckCrc, sizeof(kAckCrc));
	input_packet.payload_len = sizeof(kAckCrc);
	expected_packet = input_packet;
	expected_action = expected.mesh.onRecvForTest(&expected_packet);

	TargetEnv actual;
	struct meshcore_packet actual_packet;
	meshcore_dispatcher_action actual_action;

	reset_fake_runtime();
	actual.script.override_allow_packet_forward = true;
	actual.script.allow_packet_forward_value = true;
	actual.script.override_get_extra_ack_transmit_count = true;
	actual.script.get_extra_ack_transmit_count_value = 1U;
	actual.script.override_get_direct_retransmit_delay = true;
	actual.script.get_direct_retransmit_delay_value = 17U;
	actual.sync_script_to_hal();
	actual.mesh.self_id.identity.pub_key[0] = expected.mesh.self_id.pub_key[0];
	clone_target_packet_from_reference(&actual_packet, &input_packet);
	actual_action = meshcore_mesh_on_recv_packet(&actual.mesh, &actual_packet);

	zassert_equal(expected_action, actual_action, "direct ack action mismatch");
	expect_packet_state_match(&expected_packet, &actual_packet,
				  "direct ack packet state");
	expect_outbound_queue_match(expected, actual, "direct ack outbound");
	expect_script_observation_match(expected.script, actual.script,
					"direct ack callbacks");
}

ZTEST(meshcore_mesh_tdd,
      test_on_recv_packet_direct_multipart_ack_routes_back_matches_reference)
{
	static const uint32_t kAckCrc = 0x5566AABB;
	ReferenceEnv &expected = reference_env_get();
	mesh::Packet input_packet;
	mesh::Packet expected_packet;
	mesh::DispatcherAction expected_action;

	reset_fake_runtime();
	expected.script.override_allow_packet_forward = true;
	expected.script.allow_packet_forward_value = true;
	expected.script.override_get_extra_ack_transmit_count = true;
	expected.script.get_extra_ack_transmit_count_value = 0U;
	input_packet = mesh::Packet();
	input_packet.header = (PAYLOAD_VER_1 << PH_VER_SHIFT) |
			      (PAYLOAD_TYPE_MULTIPART << PH_TYPE_SHIFT) |
			      ROUTE_TYPE_DIRECT;
	input_packet.setPathHashSizeAndCount(1U, 2U);
	input_packet.path[0] = expected.mesh.self_id.pub_key[0];
	input_packet.path[1] = 0x22U;
	input_packet.payload[0] = (2U << 4) | PAYLOAD_TYPE_ACK;
	memcpy(&input_packet.payload[1], &kAckCrc, sizeof(kAckCrc));
	input_packet.payload_len = 5U;
	expected_packet = input_packet;
	expected_action = expected.mesh.onRecvForTest(&expected_packet);

	TargetEnv actual;
	struct meshcore_packet actual_packet;
	meshcore_dispatcher_action actual_action;

	reset_fake_runtime();
	actual.script.override_allow_packet_forward = true;
	actual.script.allow_packet_forward_value = true;
	actual.script.override_get_extra_ack_transmit_count = true;
	actual.script.get_extra_ack_transmit_count_value = 0U;
	actual.sync_script_to_hal();
	actual.mesh.self_id.identity.pub_key[0] = expected.mesh.self_id.pub_key[0];
	clone_target_packet_from_reference(&actual_packet, &input_packet);
	actual_action = meshcore_mesh_on_recv_packet(&actual.mesh, &actual_packet);

	zassert_equal(expected_action, actual_action,
		      "direct multipart action mismatch");
	expect_packet_state_match(&expected_packet, &actual_packet,
				  "direct multipart packet state");
	expect_outbound_queue_match(expected, actual,
				    "direct multipart outbound");
	expect_script_observation_match(expected.script, actual.script,
					"direct multipart callbacks");
}

ZTEST(meshcore_mesh_tdd,
      test_on_recv_packet_txt_decrypt_callback_matches_reference)
{
	static const uint8_t kSecret[PUB_KEY_SIZE] = {
		0x10, 0x20, 0x30, 0x40, 0x11, 0x21, 0x31, 0x41,
		0x12, 0x22, 0x32, 0x42, 0x13, 0x23, 0x33, 0x43,
		0x14, 0x24, 0x34, 0x44, 0x15, 0x25, 0x35, 0x45,
		0x16, 0x26, 0x36, 0x46, 0x17, 0x27, 0x37, 0x47,
	};
	static const uint8_t kData[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
	ReferenceEnv &expected = reference_env_get();
	mesh::LocalIdentity receiver;
	mesh::Packet input_packet;
	mesh::Packet expected_packet;
	mesh::DispatcherAction expected_action;

	reset_fake_runtime();
	make_reference_local_identity(&receiver, kIdentitySeedA, sizeof(kIdentitySeedA));
	expected.mesh.self_id = receiver;
	expected.script.override_search_peers_by_hash = true;
	expected.script.search_peers_by_hash_value = 1;
	expected.script.override_get_peer_shared_secret = true;
	memcpy(expected.script.peer_shared_secret, kSecret, sizeof(kSecret));
	mesh::Identity dest(expected.mesh.self_id.pub_key);
	mesh::Packet *created =
		expected.mesh.createDatagram(PAYLOAD_TYPE_TXT_MSG, dest, kSecret, kData,
					     sizeof(kData));
	zassert_not_null(created, "reference createDatagram failed");
	input_packet = *created;
	expected.manager.free(created);
	input_packet.header &= ~PH_ROUTE_MASK;
	input_packet.header |= ROUTE_TYPE_FLOOD;
	input_packet.setPathHashSizeAndCount(1U, 0U);
	expected_packet = input_packet;
	expected_action = expected.mesh.onRecvForTest(&expected_packet);

	TargetEnv actual;
	struct meshcore_packet actual_packet;
	meshcore_dispatcher_action actual_action;

	reset_fake_runtime();
	actual.script.override_search_peers_by_hash = true;
	actual.script.search_peers_by_hash_value = 1;
	actual.script.override_get_peer_shared_secret = true;
	memcpy(actual.script.peer_shared_secret, kSecret, sizeof(kSecret));
	actual.sync_script_to_hal();
	sync_target_self_identity(actual, expected.mesh.self_id);
	zassert_mem_equal(expected.mesh.self_id.pub_key,
			  actual.mesh.self_id.identity.pub_key, PUB_KEY_SIZE,
			  "txt test self identity mismatch");
	clone_target_packet_from_reference(&actual_packet, &input_packet);
	zassert_equal(input_packet.payload_len, actual_packet.payload_len,
		      "txt payload_len clone mismatch");
	zassert_mem_equal(input_packet.payload, actual_packet.payload,
			  input_packet.payload_len, "txt payload clone mismatch");
	actual_action = meshcore_mesh_on_recv_packet(&actual.mesh, &actual_packet);

	zassert_equal(expected_action, actual_action, "txt decrypt action mismatch");
	expect_script_observation_match(expected.script, actual.script,
					"txt decrypt callbacks");
	expect_packet_state_match(&expected_packet, &actual_packet,
				  "txt decrypt packet state");
}

ZTEST(meshcore_mesh_tdd,
      test_on_recv_packet_path_accept_sends_return_path_matches_reference)
{
	static const uint8_t kSecret[PUB_KEY_SIZE] = {
		0x91, 0x82, 0x73, 0x64, 0x55, 0x46, 0x37, 0x28,
		0x19, 0x0A, 0x1B, 0x2C, 0x3D, 0x4E, 0x5F, 0x60,
		0x70, 0x61, 0x52, 0x43, 0x34, 0x25, 0x16, 0x07,
		0xF8, 0xE9, 0xDA, 0xCB, 0xBC, 0xAD, 0x9E, 0x8F,
	};
	static const uint8_t kRngBytes[] = { 0x3A, 0x6C, 0x15, 0xE7 };
	ReferenceEnv &expected = reference_env_get();
	mesh::LocalIdentity receiver;
	mesh::Packet input_packet;
	mesh::Packet expected_packet;
	mesh::DispatcherAction expected_action;
	uint8_t direct_path[1] = { 0x6BU };

	reset_fake_runtime();
	meshcore_hal_test_rng_set_bytes(kRngBytes, sizeof(kRngBytes));
	make_reference_local_identity(&receiver, kIdentitySeedA, sizeof(kIdentitySeedA));
	expected.mesh.self_id = receiver;
	expected.script.override_search_peers_by_hash = true;
	expected.script.search_peers_by_hash_value = 1;
	expected.script.override_get_peer_shared_secret = true;
	memcpy(expected.script.peer_shared_secret, kSecret, sizeof(kSecret));
	expected.script.override_on_peer_path_recv = true;
	expected.script.on_peer_path_recv_value = true;
	mesh::Packet *created = expected.mesh.createPathReturn(
		expected.mesh.self_id, kSecret, direct_path, 1U, 0U, NULL,
		0U);
	zassert_not_null(created, "reference createPathReturn failed");
	input_packet = *created;
	expected.manager.free(created);
	input_packet.header &= ~PH_ROUTE_MASK;
	input_packet.header |= ROUTE_TYPE_FLOOD;
	input_packet.setPathHashSizeAndCount(1U, 1U);
	input_packet.path[0] = 0x35U;
	expected_packet = input_packet;
	expected_action = expected.mesh.onRecvForTest(&expected_packet);

	TargetEnv actual;
	struct meshcore_packet actual_packet;
	meshcore_dispatcher_action actual_action;

	reset_fake_runtime();
	meshcore_hal_test_rng_set_bytes(kRngBytes, sizeof(kRngBytes));
	actual.script.override_search_peers_by_hash = true;
	actual.script.search_peers_by_hash_value = 1;
	actual.script.override_get_peer_shared_secret = true;
	memcpy(actual.script.peer_shared_secret, kSecret, sizeof(kSecret));
	actual.script.override_on_peer_path_recv = true;
	actual.script.on_peer_path_recv_value = true;
	actual.sync_script_to_hal();
	sync_target_self_identity(actual, expected.mesh.self_id);
	zassert_mem_equal(expected.mesh.self_id.pub_key,
			  actual.mesh.self_id.identity.pub_key, PUB_KEY_SIZE,
			  "path test self identity mismatch");
	clone_target_packet_from_reference(&actual_packet, &input_packet);
	zassert_equal(input_packet.payload_len, actual_packet.payload_len,
		      "path payload_len clone mismatch");
	zassert_mem_equal(input_packet.payload, actual_packet.payload,
			  input_packet.payload_len, "path payload clone mismatch");
	actual_action = meshcore_mesh_on_recv_packet(&actual.mesh, &actual_packet);

	zassert_equal(expected_action, actual_action, "path recv action mismatch");
	expect_script_observation_match(expected.script, actual.script,
					"path recv callbacks");
	expect_packet_state_match(&expected_packet, &actual_packet,
				  "path recv packet state");
	expect_outbound_queue_match(expected, actual, "path recv outbound");
}

ZTEST(meshcore_mesh_tdd,
      test_on_recv_packet_anon_req_decrypt_callback_matches_reference)
{
	static const uint8_t kData[] = { 0x41, 0x42, 0x43, 0x44 };
	ReferenceEnv &expected = reference_env_get();
	mesh::LocalIdentity receiver;
	mesh::LocalIdentity sender;
	uint8_t secret[PUB_KEY_SIZE] = { 0 };
	mesh::Packet input_packet;
	mesh::Packet expected_packet;
	mesh::DispatcherAction expected_action;

	reset_fake_runtime();
	make_reference_local_identity(&receiver, kIdentitySeedA, sizeof(kIdentitySeedA));
	make_reference_local_identity(&sender, kIdentitySeedB, sizeof(kIdentitySeedB));
	expected.mesh.self_id = receiver;
	expected.mesh.self_id.calcSharedSecret(secret, sender.pub_key);
	mesh::Identity dest(expected.mesh.self_id.pub_key);
	mesh::Packet *created = expected.mesh.createAnonDatagram(
		PAYLOAD_TYPE_ANON_REQ, sender, dest, secret, kData, sizeof(kData));
	zassert_not_null(created, "reference createAnonDatagram failed");
	input_packet = *created;
	expected.manager.free(created);
	input_packet.header &= ~PH_ROUTE_MASK;
	input_packet.header |= ROUTE_TYPE_FLOOD;
	input_packet.setPathHashSizeAndCount(1U, 0U);
	expected_packet = input_packet;
	expected_action = expected.mesh.onRecvForTest(&expected_packet);

	TargetEnv actual;
	struct meshcore_packet actual_packet;
	meshcore_dispatcher_action actual_action;
	uint8_t actual_secret[PUB_KEY_SIZE] = { 0 };

	reset_fake_runtime();
	actual.sync_script_to_hal();
	sync_target_self_identity(actual, expected.mesh.self_id);
	zassert_mem_equal(expected.mesh.self_id.pub_key,
			  actual.mesh.self_id.identity.pub_key, PUB_KEY_SIZE,
			  "anon test self identity mismatch");
	meshcore_local_identity_calc_shared_secret(&actual.mesh.self_id, actual_secret,
						  sender.pub_key);
	zassert_mem_equal(secret, actual_secret, PUB_KEY_SIZE,
			  "anon req shared secret mismatch");
	clone_target_packet_from_reference(&actual_packet, &input_packet);
	zassert_equal(input_packet.payload_len, actual_packet.payload_len,
		      "anon payload_len clone mismatch");
	zassert_mem_equal(input_packet.payload, actual_packet.payload,
			  input_packet.payload_len, "anon payload clone mismatch");
	actual_action = meshcore_mesh_on_recv_packet(&actual.mesh, &actual_packet);

	zassert_equal(expected_action, actual_action, "anon req action mismatch");
	expect_script_observation_match(expected.script, actual.script,
					"anon req callbacks");
	expect_packet_state_match(&expected_packet, &actual_packet,
				  "anon req packet state");
}

ZTEST(meshcore_mesh_tdd,
      test_on_recv_packet_group_data_decrypt_callback_matches_reference)
{
	static const uint8_t kGroupData[] = { 0x31, 0x32, 0x33, 0x34, 0x35 };
	ReferenceEnv &expected = reference_env_get();
	mesh::GroupChannel channel = {};
	mesh::Packet input_packet;
	mesh::Packet expected_packet;
	mesh::DispatcherAction expected_action;

	reset_fake_runtime();
	channel.hash[0] = 0x5EU;
	for (size_t i = 0; i < PUB_KEY_SIZE; i++) {
		channel.secret[i] = (uint8_t)(0x70U + i);
	}
	expected.script.override_search_channels_by_hash = true;
	expected.script.search_channels_by_hash_value = 1;
	expected.script.override_search_channels_fill = true;
	memcpy(expected.script.search_channel_hash, channel.hash,
	       sizeof(expected.script.search_channel_hash));
	memcpy(expected.script.search_channel_secret, channel.secret,
	       sizeof(expected.script.search_channel_secret));
	mesh::Packet *created = expected.mesh.createGroupDatagram(
		PAYLOAD_TYPE_GRP_DATA, channel, kGroupData, sizeof(kGroupData));
	zassert_not_null(created, "reference createGroupDatagram failed");
	input_packet = *created;
	expected.manager.free(created);
	input_packet.header &= ~PH_ROUTE_MASK;
	input_packet.header |= ROUTE_TYPE_FLOOD;
	input_packet.setPathHashSizeAndCount(1U, 0U);
	expected_packet = input_packet;
	expected_action = expected.mesh.onRecvForTest(&expected_packet);

	TargetEnv actual;
	struct meshcore_packet actual_packet;
	meshcore_dispatcher_action actual_action;

	reset_fake_runtime();
	actual.script.override_search_channels_by_hash = true;
	actual.script.search_channels_by_hash_value = 1;
	actual.script.override_search_channels_fill = true;
	memcpy(actual.script.search_channel_hash, channel.hash,
	       sizeof(actual.script.search_channel_hash));
	memcpy(actual.script.search_channel_secret, channel.secret,
	       sizeof(actual.script.search_channel_secret));
	actual.sync_script_to_hal();
	clone_target_packet_from_reference(&actual_packet, &input_packet);
	zassert_equal(input_packet.payload_len, actual_packet.payload_len,
		      "group payload_len clone mismatch");
	zassert_mem_equal(input_packet.payload, actual_packet.payload,
			  input_packet.payload_len, "group payload clone mismatch");
	actual_action = meshcore_mesh_on_recv_packet(&actual.mesh, &actual_packet);

	zassert_equal(expected_action, actual_action, "group data action mismatch");
	expect_packet_state_match(&expected_packet, &actual_packet,
				  "group data packet state");
	expect_script_observation_match(expected.script, actual.script,
					"group data callbacks");
}

ZTEST(meshcore_mesh_tdd,
      test_on_recv_packet_advert_valid_signature_matches_reference)
{
	static const uint8_t kAdvertData[] = { 0xA1, 0xB2, 0xC3 };
	ReferenceEnv &expected = reference_env_get();
	mesh::LocalIdentity receiver;
	mesh::LocalIdentity sender;
	mesh::Packet input_packet;
	mesh::Packet expected_packet;
	mesh::DispatcherAction expected_action;

	reset_fake_runtime();
	make_reference_local_identity(&receiver, kIdentitySeedA, sizeof(kIdentitySeedA));
	make_reference_local_identity(&sender, kIdentitySeedB, sizeof(kIdentitySeedB));
	expected.mesh.self_id = receiver;
	mesh::Packet *created =
		expected.mesh.createAdvert(sender, kAdvertData, sizeof(kAdvertData));
	zassert_not_null(created, "reference createAdvert failed");
	input_packet = *created;
	expected.manager.free(created);
	input_packet.header &= ~PH_ROUTE_MASK;
	input_packet.header |= ROUTE_TYPE_FLOOD;
	input_packet.setPathHashSizeAndCount(1U, 0U);
	expected_packet = input_packet;
	expected_action = expected.mesh.onRecvForTest(&expected_packet);

	TargetEnv actual;
	struct meshcore_packet actual_packet;
	meshcore_dispatcher_action actual_action;

	reset_fake_runtime();
	actual.sync_script_to_hal();
	sync_target_self_identity(actual, expected.mesh.self_id);
	clone_target_packet_from_reference(&actual_packet, &input_packet);
	actual_action = meshcore_mesh_on_recv_packet(&actual.mesh, &actual_packet);

	zassert_equal(expected_action, actual_action, "advert action mismatch");
	expect_packet_state_match(&expected_packet, &actual_packet,
				  "advert packet state");
	expect_script_observation_match(expected.script, actual.script,
					"advert callbacks");
}

ZTEST(meshcore_mesh_tdd,
      test_on_recv_packet_raw_custom_direct_callback_matches_reference)
{
	ReferenceEnv &expected = reference_env_get();
	mesh::Packet expected_packet;
	mesh::DispatcherAction expected_action;

	reset_fake_runtime();
	init_reference_packet(&expected_packet, ROUTE_TYPE_DIRECT, 0x55U, 5U);
	expected_packet.header = (PAYLOAD_VER_1 << PH_VER_SHIFT) |
				 (PAYLOAD_TYPE_RAW_CUSTOM << PH_TYPE_SHIFT) |
				 ROUTE_TYPE_DIRECT;
	expected_packet.path_len = 0U;
	expected_action = expected.mesh.onRecvForTest(&expected_packet);

	TargetEnv actual;
	struct meshcore_packet actual_packet;
	meshcore_dispatcher_action actual_action;

	reset_fake_runtime();
	actual.sync_script_to_hal();
	clone_target_packet_from_reference(&actual_packet, &expected_packet);
	actual_action = meshcore_mesh_on_recv_packet(&actual.mesh, &actual_packet);

	zassert_equal(expected_action, actual_action, "raw custom action mismatch");
	expect_script_observation_match(expected.script, actual.script,
					"raw custom callbacks");
}

ZTEST(meshcore_mesh_tdd,
      test_on_recv_packet_multipart_flood_ack_callback_matches_reference)
{
	static const uint32_t kAckCrc = 0xCAFEBABEU;
	ReferenceEnv &expected = reference_env_get();
	mesh::Packet expected_packet;
	mesh::DispatcherAction expected_action;

	reset_fake_runtime();
	expected_packet = mesh::Packet();
	expected_packet.header = (PAYLOAD_VER_1 << PH_VER_SHIFT) |
				 (PAYLOAD_TYPE_MULTIPART << PH_TYPE_SHIFT) |
				 ROUTE_TYPE_FLOOD;
	expected_packet.setPathHashSizeAndCount(1U, 0U);
	expected_packet.payload[0] = (1U << 4) | PAYLOAD_TYPE_ACK;
	memcpy(&expected_packet.payload[1], &kAckCrc, sizeof(kAckCrc));
	expected_packet.payload_len = 5U;
	expected_action = expected.mesh.onRecvForTest(&expected_packet);

	TargetEnv actual;
	struct meshcore_packet actual_packet;
	meshcore_dispatcher_action actual_action;

	reset_fake_runtime();
	actual.sync_script_to_hal();
	clone_target_packet_from_reference(&actual_packet, &expected_packet);
	actual_action = meshcore_mesh_on_recv_packet(&actual.mesh, &actual_packet);

	zassert_equal(expected_action, actual_action,
		      "multipart flood ack action mismatch");
	expect_script_observation_match(expected.script, actual.script,
					"multipart flood ack callbacks");
}

ZTEST_SUITE(meshcore_mesh_tdd, NULL, reference_env_suite_setup,
	    reference_env_before_each, NULL, NULL);

} // namespace meshcore_mesh_tdd
