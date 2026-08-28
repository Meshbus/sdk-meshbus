// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "test_reference_env.h"
#include "test_support.h"
#include "test_target_env.h"

#include <string.h>

namespace meshcore_mesh_tdd {

static const uint8_t kInteropIdentitySeedA[] = {
	0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
	0x98, 0xA9, 0xBA, 0xCB, 0xDC, 0xED, 0xFE, 0x0F,
};

static const uint8_t kInteropIdentitySeedB[] = {
	0x7E, 0x6D, 0x5C, 0x4B, 0x3A, 0x29, 0x18, 0x07,
	0xF6, 0xE5, 0xD4, 0xC3, 0xB2, 0xA1, 0x90, 0x8F,
};

static mesh::Packet *require_reference_outbound(ReferenceEnv &env,
						const char *label)
{
	mesh::Packet *packet = env.manager.getOutboundByIdx(0);

	zassert_not_null(packet, "%s reference outbound packet missing", label);
	return packet;
}

static struct meshcore_packet *require_target_outbound(TargetEnv &env,
						       const char *label)
{
	struct meshcore_packet *packet =
		meshcore_packet_queue_manager_get_outbound_by_idx(&env.manager, 0);

	zassert_not_null(packet, "%s target outbound packet missing", label);
	return packet;
}

static void expect_reference_outbound_matches_target(ReferenceEnv &expected,
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

static void expect_target_outbound_matches_reference(TargetEnv &expected,
						     ReferenceEnv &actual,
						     const char *label)
{
	int expected_total =
		meshcore_packet_queue_manager_get_outbound_total(&expected.manager);
	int actual_total = actual.manager.getOutboundTotal();

	zassert_equal(expected_total, actual_total, "%s outbound total mismatch",
		      label);
	if (expected_total != actual_total) {
		return;
	}

	for (int i = 0; i < expected_total; i++) {
		const struct meshcore_packet *expected_packet =
			meshcore_packet_queue_manager_get_outbound_by_idx(
				&expected.manager, i);
		mesh::Packet *actual_packet = actual.manager.getOutboundByIdx(i);
		uint8_t expected_raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
		uint8_t actual_raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
		uint8_t expected_len;
		uint8_t actual_len;

		zassert_not_null(expected_packet, "%s expected outbound null", label);
		zassert_not_null(actual_packet, "%s actual outbound null", label);

		expected_len = meshcore_packet_write_to(expected_packet, expected_raw);
		actual_len = actual_packet->writeTo(actual_raw);
		zassert_equal(expected_len, actual_len,
			      "%s outbound raw len mismatch at %d", label, i);
		zassert_mem_equal(expected_raw, actual_raw, expected_len,
				  "%s outbound raw mismatch at %d", label, i);
	}
}

static uint8_t parse_target_from_reference_packet(
	const mesh::Packet *source, struct meshcore_packet *dest,
	const char *label)
{
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t roundtrip_raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t raw_len = source->writeTo(raw);
	uint8_t roundtrip_len;

	memset(dest, 0, sizeof(*dest));
	zassert_true(meshcore_packet_read_from(dest, raw, raw_len),
		     "%s target read_from failed", label);
	expect_packet_state_match(source, dest, label);

	roundtrip_len = meshcore_packet_write_to(dest, roundtrip_raw);
	zassert_equal(raw_len, roundtrip_len, "%s raw len mismatch", label);
	zassert_mem_equal(raw, roundtrip_raw, raw_len, "%s raw bytes mismatch",
			  label);
	return raw_len;
}

static uint8_t parse_reference_from_target_packet(
	const struct meshcore_packet *source, mesh::Packet *dest,
	const char *label)
{
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t roundtrip_raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t raw_len = meshcore_packet_write_to(source, raw);
	uint8_t roundtrip_len;

	*dest = mesh::Packet();
	zassert_true(dest->readFrom(raw, raw_len), "%s reference readFrom failed",
		     label);

	roundtrip_len = dest->writeTo(roundtrip_raw);
	zassert_equal(raw_len, roundtrip_len, "%s raw len mismatch", label);
	zassert_mem_equal(raw, roundtrip_raw, raw_len, "%s raw bytes mismatch",
			  label);
	return raw_len;
}

static void prepare_peer_secret_script(MeshScript *script, const uint8_t *secret,
				       int sender_idx)
{
	script->override_search_peers_by_hash = true;
	script->search_peers_by_hash_value = sender_idx;
	script->override_get_peer_shared_secret = true;
	memcpy(script->peer_shared_secret, secret, PUB_KEY_SIZE);
}

static void prepare_path_script(MeshScript *script, const uint8_t *secret,
				int sender_idx)
{
	prepare_peer_secret_script(script, secret, sender_idx);
	script->override_on_peer_path_recv = true;
	script->on_peer_path_recv_value = true;
}

ZTEST(meshcore_mesh_tdd, test_interop_reference_zero_hop_ack_to_target)
{
	static const uint32_t kAckCrc = 0x1234ABCDU;
	ReferenceEnv &source = reference_env_get();
	TargetEnv sink;
	struct meshcore_packet parsed = {};
	meshcore_dispatcher_action action;

	reset_fake_runtime();
	mesh::Packet *created = source.mesh.createAck(kAckCrc);
	zassert_not_null(created, "reference createAck failed");
	source.mesh.sendZeroHop(created, 0U);

	parse_target_from_reference_packet(
		require_reference_outbound(source, "reference ack source"), &parsed,
		"reference ack -> target");
	action = meshcore_mesh_on_recv_packet(&sink.mesh, &parsed);

	zassert_equal((meshcore_dispatcher_action)ACTION_RELEASE, action,
		      "reference ack -> target action mismatch");
	zassert_equal(1U, sink.script.on_ack_recv_count,
		      "reference ack -> target callback count mismatch");
	zassert_equal(kAckCrc, sink.script.last_ack_crc,
		      "reference ack -> target crc mismatch");
	zassert_equal(0, meshcore_packet_queue_manager_get_outbound_total(&sink.manager),
		      "reference ack -> target should not enqueue outbound");
}

ZTEST(meshcore_mesh_tdd, test_interop_target_zero_hop_ack_to_reference)
{
	static const uint32_t kAckCrc = 0x89ABCDEFU;
	TargetEnv source;
	ReferenceEnv &sink = reference_env_get();
	mesh::Packet parsed;
	mesh::DispatcherAction action;

	reset_fake_runtime();
	struct meshcore_packet *created =
		meshcore_mesh_create_ack(&source.mesh, kAckCrc);
	zassert_not_null(created, "target createAck failed");
	meshcore_mesh_send_zero_hop(&source.mesh, created, 0U);

	parse_reference_from_target_packet(
		require_target_outbound(source, "target ack source"), &parsed,
		"target ack -> reference");
	action = sink.mesh.onRecvForTest(&parsed);

	zassert_equal((mesh::DispatcherAction)ACTION_RELEASE, action,
		      "target ack -> reference action mismatch");
	zassert_equal(1U, sink.script.on_ack_recv_count,
		      "target ack -> reference callback count mismatch");
	zassert_equal(kAckCrc, sink.script.last_ack_crc,
		      "target ack -> reference crc mismatch");
	zassert_equal(0, sink.manager.getOutboundTotal(),
		      "target ack -> reference should not enqueue outbound");
}

ZTEST(meshcore_mesh_tdd, test_interop_reference_flood_txt_to_target)
{
	static const uint8_t kSecret[PUB_KEY_SIZE] = {
		0x10, 0x20, 0x30, 0x40, 0x11, 0x21, 0x31, 0x41,
		0x12, 0x22, 0x32, 0x42, 0x13, 0x23, 0x33, 0x43,
		0x14, 0x24, 0x34, 0x44, 0x15, 0x25, 0x35, 0x45,
		0x16, 0x26, 0x36, 0x46, 0x17, 0x27, 0x37, 0x47,
	};
	static const uint8_t kData[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
	ReferenceEnv &source = reference_env_get();
	ReferenceEnv expected_sink;
	TargetEnv sink;
	mesh::LocalIdentity receiver;
	mesh::Identity dest;
	mesh::Packet expected_packet;
	struct meshcore_packet parsed = {};
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t raw_len;
	mesh::DispatcherAction expected_action;
	meshcore_dispatcher_action action;

	reset_fake_runtime();
	make_reference_local_identity(&receiver, kInteropIdentitySeedA,
				      sizeof(kInteropIdentitySeedA));
	dest = mesh::Identity(receiver.pub_key);
	prepare_peer_secret_script(&expected_sink.script, kSecret, 1);
	expected_sink.mesh.self_id = receiver;
	prepare_peer_secret_script(&sink.script, kSecret, 1);
	sink.sync_script_to_hal();
	sync_target_self_identity(sink, receiver);

	mesh::Packet *created = source.mesh.createDatagram(
		PAYLOAD_TYPE_TXT_MSG, dest, kSecret, kData, sizeof(kData));
	zassert_not_null(created, "reference createDatagram failed");
	source.mesh.sendFlood(created, 5U, 1U);

	raw_len = require_reference_outbound(source, "reference txt source")->writeTo(raw);
	expected_packet = mesh::Packet();
	zassert_true(expected_packet.readFrom(raw, raw_len),
		     "reference txt -> reference sink parse failed");
	expected_action = expected_sink.mesh.onRecvForTest(&expected_packet);

	parse_target_from_reference_packet(
		require_reference_outbound(source, "reference txt source"), &parsed,
		"reference txt -> target");
	action = meshcore_mesh_on_recv_packet(&sink.mesh, &parsed);

	zassert_equal(expected_action, action,
		      "reference txt -> target action mismatch");
	expect_script_observation_match(expected_sink.script, sink.script,
					"reference txt interop callbacks");
}

ZTEST(meshcore_mesh_tdd, test_interop_target_flood_txt_to_reference)
{
	static const uint8_t kSecret[PUB_KEY_SIZE] = {
		0x10, 0x20, 0x30, 0x40, 0x11, 0x21, 0x31, 0x41,
		0x12, 0x22, 0x32, 0x42, 0x13, 0x23, 0x33, 0x43,
		0x14, 0x24, 0x34, 0x44, 0x15, 0x25, 0x35, 0x45,
		0x16, 0x26, 0x36, 0x46, 0x17, 0x27, 0x37, 0x47,
	};
	static const uint8_t kData[] = { 0xA1, 0xB2, 0xC3, 0xD4 };
	TargetEnv source;
	TargetEnv expected_sink;
	ReferenceEnv &sink = reference_env_get();
	mesh::LocalIdentity receiver;
	mesh::Identity dest;
	struct meshcore_identity actual_dest;
	struct meshcore_packet expected_packet = {};
	mesh::Packet parsed;
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t raw_len;
	meshcore_dispatcher_action expected_action;
	mesh::DispatcherAction action;

	reset_fake_runtime();
	make_reference_local_identity(&receiver, kInteropIdentitySeedB,
				      sizeof(kInteropIdentitySeedB));
	dest = mesh::Identity(receiver.pub_key);
	meshcore_identity_init_from_pub_key(&actual_dest, dest.pub_key);
	prepare_peer_secret_script(&expected_sink.script, kSecret, 1);
	expected_sink.sync_script_to_hal();
	sync_target_self_identity(expected_sink, receiver);
	prepare_peer_secret_script(&sink.script, kSecret, 1);
	sink.mesh.self_id = receiver;

	struct meshcore_packet *created = meshcore_mesh_create_datagram(
		&source.mesh, PAYLOAD_TYPE_TXT_MSG, &actual_dest, kSecret, kData,
		sizeof(kData));
	zassert_not_null(created, "target createDatagram failed");
	meshcore_mesh_send_flood(&source.mesh, created, 0U, 1U);

	raw_len = meshcore_packet_write_to(
		require_target_outbound(source, "target txt source"), raw);
	memset(&expected_packet, 0, sizeof(expected_packet));
	zassert_true(meshcore_packet_read_from(&expected_packet, raw, raw_len),
		     "target txt -> target sink parse failed");
	expected_action = meshcore_mesh_on_recv_packet(&expected_sink.mesh,
						       &expected_packet);

	parse_reference_from_target_packet(
		require_target_outbound(source, "target txt source"), &parsed,
		"target txt -> reference");
	action = sink.mesh.onRecvForTest(&parsed);

	zassert_equal(expected_action, action,
		      "target txt -> reference action mismatch");
	expect_script_observation_match(expected_sink.script, sink.script,
					"target txt interop callbacks");
}

ZTEST(meshcore_mesh_tdd, test_interop_reference_flood_advert_to_target)
{
	static const uint8_t kAppData[] = { 0xA1, 0xB2, 0xC3, 0xD4 };
	ReferenceEnv &source = reference_env_get();
	TargetEnv sink;
	mesh::LocalIdentity sender;
	struct meshcore_packet parsed = {};
	meshcore_dispatcher_action action;

	reset_fake_runtime();
	make_reference_local_identity(&sender, kInteropIdentitySeedB,
				      sizeof(kInteropIdentitySeedB));

	mesh::Packet *created =
		source.mesh.createAdvert(sender, kAppData, sizeof(kAppData));
	zassert_not_null(created, "reference createAdvert failed");
	source.mesh.sendFlood(created, 5U, 1U);

	parse_target_from_reference_packet(
		require_reference_outbound(source, "reference advert source"), &parsed,
		"reference advert -> target");
	action = meshcore_mesh_on_recv_packet(&sink.mesh, &parsed);

	zassert_equal((meshcore_dispatcher_action)ACTION_RELEASE, action,
		      "reference advert -> target action mismatch");
	zassert_equal(1U, sink.script.on_advert_recv_count,
		      "reference advert -> target callback count mismatch");
}

ZTEST(meshcore_mesh_tdd, test_interop_target_flood_advert_to_reference)
{
	static const uint8_t kAppData[] = { 0x19, 0x28, 0x37 };
	TargetEnv source;
	ReferenceEnv &sink = reference_env_get();
	mesh::LocalIdentity sender;
	mesh::Packet parsed;
	mesh::DispatcherAction action;

	reset_fake_runtime();
	make_reference_local_identity(&sender, kInteropIdentitySeedA,
				      sizeof(kInteropIdentitySeedA));
	sync_target_self_identity(source, sender);

	struct meshcore_packet *created = meshcore_mesh_create_advert(
		&source.mesh, &source.mesh.self_id, kAppData, sizeof(kAppData));
	zassert_not_null(created, "target createAdvert failed");
	meshcore_mesh_send_flood(&source.mesh, created, 0U, 1U);

	parse_reference_from_target_packet(
		require_target_outbound(source, "target advert source"), &parsed,
		"target advert -> reference");
	action = sink.mesh.onRecvForTest(&parsed);

	zassert_equal((mesh::DispatcherAction)ACTION_RELEASE, action,
		      "target advert -> reference action mismatch");
	zassert_equal(1U, sink.script.on_advert_recv_count,
		      "target advert -> reference callback count mismatch");
}

ZTEST(meshcore_mesh_tdd,
      test_interop_reference_flood_path_return_with_extra_to_target)
{
	static const uint8_t kSecret[PUB_KEY_SIZE] = {
		0x91, 0x82, 0x73, 0x64, 0x55, 0x46, 0x37, 0x28,
		0x19, 0x0A, 0x1B, 0x2C, 0x3D, 0x4E, 0x5F, 0x60,
		0x70, 0x61, 0x52, 0x43, 0x34, 0x25, 0x16, 0x07,
		0xF8, 0xE9, 0xDA, 0xCB, 0xBC, 0xAD, 0x9E, 0x8F,
	};
	static const uint8_t kPath[] = { 0x6BU };
	static const uint8_t kExtra[] = { 0xD1, 0xE2, 0xF3 };
	ReferenceEnv &source = reference_env_get();
	ReferenceEnv expected_sink;
	TargetEnv sink;
	mesh::LocalIdentity receiver;
	mesh::Packet expected_packet;
	struct meshcore_packet parsed = {};
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t raw_len;
	mesh::DispatcherAction expected_action;
	meshcore_dispatcher_action action;

	reset_fake_runtime();
	make_reference_local_identity(&receiver, kInteropIdentitySeedA,
				      sizeof(kInteropIdentitySeedA));
	mesh::Packet *created = source.mesh.createPathReturn(
		receiver, kSecret, kPath, 1U, PAYLOAD_TYPE_REQ, kExtra,
		sizeof(kExtra));
	zassert_not_null(created, "reference createPathReturn(with extra) failed");
	source.mesh.sendFlood(created, 5U, 1U);
	raw_len =
		require_reference_outbound(source, "reference path extra source")
			->writeTo(raw);

	reset_fake_runtime();
	prepare_path_script(&expected_sink.script, kSecret, 1);
	expected_sink.mesh.self_id = receiver;
	meshcore_hal_test_rng_set_bytes(kExtra, sizeof(kExtra));
	expected_packet = mesh::Packet();
	zassert_true(expected_packet.readFrom(raw, raw_len),
		     "reference path extra -> reference sink parse failed");
	expected_action = expected_sink.mesh.onRecvForTest(&expected_packet);

	reset_fake_runtime();
	prepare_path_script(&sink.script, kSecret, 1);
	sink.sync_script_to_hal();
	sync_target_self_identity(sink, receiver);
	meshcore_hal_test_rng_set_bytes(kExtra, sizeof(kExtra));
	memset(&parsed, 0, sizeof(parsed));
	zassert_true(meshcore_packet_read_from(&parsed, raw, raw_len),
		     "reference path extra -> target parse failed");
	action = meshcore_mesh_on_recv_packet(&sink.mesh, &parsed);

	zassert_equal(expected_action, action,
		      "reference path extra -> target action mismatch");
	expect_script_observation_match(expected_sink.script, sink.script,
					"reference path extra callbacks");
	expect_packet_state_match(&expected_packet, &parsed,
				  "reference path extra packet state");
	expect_reference_outbound_matches_target(expected_sink, sink,
						 "reference path extra outbound");
}

ZTEST(meshcore_mesh_tdd,
      test_interop_reference_flood_path_return_without_extra_to_target)
{
	static const uint8_t kSecret[PUB_KEY_SIZE] = {
		0x91, 0x82, 0x73, 0x64, 0x55, 0x46, 0x37, 0x28,
		0x19, 0x0A, 0x1B, 0x2C, 0x3D, 0x4E, 0x5F, 0x60,
		0x70, 0x61, 0x52, 0x43, 0x34, 0x25, 0x16, 0x07,
		0xF8, 0xE9, 0xDA, 0xCB, 0xBC, 0xAD, 0x9E, 0x8F,
	};
	static const uint8_t kPath[] = { 0x6BU };
	static const uint8_t kSourceRng[] = { 0x3A, 0x6C, 0x15, 0xE7 };
	static const uint8_t kSinkRng[] = { 0x41, 0x52, 0x63, 0x74 };
	ReferenceEnv &source = reference_env_get();
	ReferenceEnv expected_sink;
	TargetEnv sink;
	mesh::LocalIdentity receiver;
	mesh::Packet expected_packet;
	struct meshcore_packet parsed = {};
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t raw_len;
	mesh::DispatcherAction expected_action;
	meshcore_dispatcher_action action;

	reset_fake_runtime();
	make_reference_local_identity(&receiver, kInteropIdentitySeedA,
				      sizeof(kInteropIdentitySeedA));
	meshcore_hal_test_rng_set_bytes(kSourceRng, sizeof(kSourceRng));
	mesh::Packet *created = source.mesh.createPathReturn(
		receiver, kSecret, kPath, 1U, 0U, NULL, 0U);
	zassert_not_null(created, "reference createPathReturn(no extra) failed");
	source.mesh.sendFlood(created, 5U, 1U);
	raw_len =
		require_reference_outbound(source, "reference path no-extra source")
			->writeTo(raw);

	reset_fake_runtime();
	prepare_path_script(&expected_sink.script, kSecret, 1);
	expected_sink.mesh.self_id = receiver;
	meshcore_hal_test_rng_set_bytes(kSinkRng, sizeof(kSinkRng));
	expected_packet = mesh::Packet();
	zassert_true(expected_packet.readFrom(raw, raw_len),
		     "reference path no-extra -> reference sink parse failed");
	expected_action = expected_sink.mesh.onRecvForTest(&expected_packet);

	reset_fake_runtime();
	prepare_path_script(&sink.script, kSecret, 1);
	sink.sync_script_to_hal();
	sync_target_self_identity(sink, receiver);
	meshcore_hal_test_rng_set_bytes(kSinkRng, sizeof(kSinkRng));
	memset(&parsed, 0, sizeof(parsed));
	zassert_true(meshcore_packet_read_from(&parsed, raw, raw_len),
		     "reference path no-extra -> target parse failed");
	action = meshcore_mesh_on_recv_packet(&sink.mesh, &parsed);

	zassert_equal(expected_action, action,
		      "reference path no-extra -> target action mismatch");
	expect_script_observation_match(expected_sink.script, sink.script,
					"reference path no-extra callbacks");
	expect_packet_state_match(&expected_packet, &parsed,
				  "reference path no-extra packet state");
	expect_reference_outbound_matches_target(expected_sink, sink,
						 "reference path no-extra outbound");
}

ZTEST(meshcore_mesh_tdd,
      test_interop_target_flood_path_return_with_extra_to_reference)
{
	static const uint8_t kSecret[PUB_KEY_SIZE] = {
		0x91, 0x82, 0x73, 0x64, 0x55, 0x46, 0x37, 0x28,
		0x19, 0x0A, 0x1B, 0x2C, 0x3D, 0x4E, 0x5F, 0x60,
		0x70, 0x61, 0x52, 0x43, 0x34, 0x25, 0x16, 0x07,
		0xF8, 0xE9, 0xDA, 0xCB, 0xBC, 0xAD, 0x9E, 0x8F,
	};
	static const uint8_t kPath[] = { 0x6BU };
	static const uint8_t kExtra[] = { 0xD1, 0xE2, 0xF3 };
	TargetEnv source;
	TargetEnv expected_sink;
	ReferenceEnv &sink = reference_env_get();
	mesh::LocalIdentity receiver;
	struct meshcore_identity actual_dest;
	struct meshcore_packet expected_packet = {};
	mesh::Packet parsed;
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t raw_len;
	meshcore_dispatcher_action expected_action;
	mesh::DispatcherAction action;

	reset_fake_runtime();
	make_reference_local_identity(&receiver, kInteropIdentitySeedB,
				      sizeof(kInteropIdentitySeedB));
	meshcore_identity_init_from_pub_key(&actual_dest, receiver.pub_key);
	struct meshcore_packet *created =
		meshcore_mesh_create_path_return_by_identity(
			&source.mesh, &actual_dest, kSecret, kPath, 1U,
			PAYLOAD_TYPE_REQ, kExtra, sizeof(kExtra));
	zassert_not_null(created, "target createPathReturn(with extra) failed");
	meshcore_mesh_send_flood(&source.mesh, created, 0U, 1U);
	raw_len = meshcore_packet_write_to(
		require_target_outbound(source, "target path extra source"), raw);

	reset_fake_runtime();
	prepare_path_script(&expected_sink.script, kSecret, 1);
	expected_sink.sync_script_to_hal();
	sync_target_self_identity(expected_sink, receiver);
	meshcore_hal_test_rng_set_bytes(kExtra, sizeof(kExtra));
	memset(&expected_packet, 0, sizeof(expected_packet));
	zassert_true(meshcore_packet_read_from(&expected_packet, raw, raw_len),
		     "target path extra -> target sink parse failed");
	expected_action = meshcore_mesh_on_recv_packet(&expected_sink.mesh,
						       &expected_packet);

	reset_fake_runtime();
	prepare_path_script(&sink.script, kSecret, 1);
	sink.mesh.self_id = receiver;
	meshcore_hal_test_rng_set_bytes(kExtra, sizeof(kExtra));
	parsed = mesh::Packet();
	zassert_true(parsed.readFrom(raw, raw_len),
		     "target path extra -> reference parse failed");
	action = sink.mesh.onRecvForTest(&parsed);

	zassert_equal(expected_action, action,
		      "target path extra -> reference action mismatch");
	expect_script_observation_match(expected_sink.script, sink.script,
					"target path extra callbacks");
	expect_target_outbound_matches_reference(expected_sink, sink,
						 "target path extra outbound");
}

ZTEST(meshcore_mesh_tdd,
      test_interop_target_flood_path_return_without_extra_to_reference)
{
	static const uint8_t kSecret[PUB_KEY_SIZE] = {
		0x91, 0x82, 0x73, 0x64, 0x55, 0x46, 0x37, 0x28,
		0x19, 0x0A, 0x1B, 0x2C, 0x3D, 0x4E, 0x5F, 0x60,
		0x70, 0x61, 0x52, 0x43, 0x34, 0x25, 0x16, 0x07,
		0xF8, 0xE9, 0xDA, 0xCB, 0xBC, 0xAD, 0x9E, 0x8F,
	};
	static const uint8_t kPath[] = { 0x6BU };
	static const uint8_t kSourceRng[] = { 0x3A, 0x6C, 0x15, 0xE7 };
	static const uint8_t kSinkRng[] = { 0x41, 0x52, 0x63, 0x74 };
	TargetEnv source;
	TargetEnv expected_sink;
	ReferenceEnv &sink = reference_env_get();
	mesh::LocalIdentity receiver;
	struct meshcore_identity actual_dest;
	struct meshcore_packet expected_packet = {};
	mesh::Packet parsed;
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t raw_len;
	meshcore_dispatcher_action expected_action;
	mesh::DispatcherAction action;

	reset_fake_runtime();
	make_reference_local_identity(&receiver, kInteropIdentitySeedB,
				      sizeof(kInteropIdentitySeedB));
	meshcore_identity_init_from_pub_key(&actual_dest, receiver.pub_key);
	meshcore_hal_test_rng_set_bytes(kSourceRng, sizeof(kSourceRng));
	struct meshcore_packet *created =
		meshcore_mesh_create_path_return_by_identity(
			&source.mesh, &actual_dest, kSecret, kPath, 1U, 0U, NULL, 0U);
	zassert_not_null(created, "target createPathReturn(no extra) failed");
	meshcore_mesh_send_flood(&source.mesh, created, 0U, 1U);
	raw_len = meshcore_packet_write_to(
		require_target_outbound(source, "target path no-extra source"), raw);

	reset_fake_runtime();
	prepare_path_script(&expected_sink.script, kSecret, 1);
	expected_sink.sync_script_to_hal();
	sync_target_self_identity(expected_sink, receiver);
	meshcore_hal_test_rng_set_bytes(kSinkRng, sizeof(kSinkRng));
	memset(&expected_packet, 0, sizeof(expected_packet));
	zassert_true(meshcore_packet_read_from(&expected_packet, raw, raw_len),
		     "target path no-extra -> target sink parse failed");
	expected_action = meshcore_mesh_on_recv_packet(&expected_sink.mesh,
						       &expected_packet);

	reset_fake_runtime();
	prepare_path_script(&sink.script, kSecret, 1);
	sink.mesh.self_id = receiver;
	meshcore_hal_test_rng_set_bytes(kSinkRng, sizeof(kSinkRng));
	parsed = mesh::Packet();
	zassert_true(parsed.readFrom(raw, raw_len),
		     "target path no-extra -> reference parse failed");
	action = sink.mesh.onRecvForTest(&parsed);

	zassert_equal(expected_action, action,
		      "target path no-extra -> reference action mismatch");
	expect_script_observation_match(expected_sink.script, sink.script,
					"target path no-extra callbacks");
	expect_target_outbound_matches_reference(expected_sink, sink,
						 "target path no-extra outbound");
}

} // namespace meshcore_mesh_tdd
