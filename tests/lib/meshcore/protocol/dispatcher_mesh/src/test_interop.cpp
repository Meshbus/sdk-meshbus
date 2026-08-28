// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "test_reference_env.h"
#include "test_support.h"
#include "test_target_env.h"

#include <stddef.h>

inline void *operator new(size_t, void *ptr) noexcept
{
	return ptr;
}

namespace meshcore_dispatcher_mesh_tdd {

alignas(TargetEnv) static unsigned char g_target_env_storage[sizeof(TargetEnv)];

static TargetEnv *make_target_env(void)
{
	return new (g_target_env_storage) TargetEnv();
}

static void destroy_target_env(TargetEnv *env)
{
	if (env != nullptr) {
		env->~TargetEnv();
	}
}

ZTEST(meshcore_dispatcher_mesh_tdd,
      test_reference_stack_send_ack_to_target_stack)
{
	static const uint32_t kAckCrc = 0x1234ABCDU;
	JointSnapshot expected_snapshot;
	JointSnapshot actual_snapshot;
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t raw_len;

	{
		ReferenceEnv &source = reference_env_get();

		reference_env_before_each(&source);
		begin_reference_env(source);
		mesh::Packet *created = source.mesh.createAck(kAckCrc);
		zassert_not_null(created, "reference createAck failed");
		source.mesh.sendZeroHop(created, 0U);
		raw_len = capture_reference_outbound_raw(source, raw,
							"reference ack source");
		drive_reference_send_until_complete(source);
	}

	{
		ReferenceEnv &expected_sink = reference_env_get();

		reference_env_before_each(&expected_sink);
		run_reference_receive_once(expected_sink, raw, raw_len, -35, 12);
		expected_snapshot = capture_reference_snapshot(expected_sink);
	}

	{
		TargetEnv *actual_sink = make_target_env();

		reset_fake_runtime();
		run_target_receive_once(*actual_sink, raw, raw_len, -35, 12);
		actual_snapshot = capture_target_snapshot(*actual_sink);
		destroy_target_env(actual_sink);
	}

	expect_joint_snapshot_match(expected_snapshot, actual_snapshot,
				    "interop reference ack -> target");
}

ZTEST(meshcore_dispatcher_mesh_tdd,
      test_target_stack_send_ack_to_reference_stack)
{
	static const uint32_t kAckCrc = 0x89ABCDEFU;
	JointSnapshot expected_snapshot;
	JointSnapshot actual_snapshot;
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t raw_len;

	{
		TargetEnv *source = make_target_env();

		reset_fake_runtime();
		begin_target_env(*source);
		struct meshcore_packet *created =
			meshcore_mesh_create_ack(&source->mesh, kAckCrc);
		zassert_not_null(created, "target createAck failed");
		meshcore_mesh_send_zero_hop(&source->mesh, created, 0U);
		raw_len =
			capture_target_outbound_raw(*source, raw, "target ack source");
		drive_target_send_until_complete(*source);
		destroy_target_env(source);
	}

	{
		TargetEnv *expected_sink = make_target_env();

		reset_fake_runtime();
		run_target_receive_once(*expected_sink, raw, raw_len, -35, 12);
		expected_snapshot = capture_target_snapshot(*expected_sink);
		destroy_target_env(expected_sink);
	}

	{
		ReferenceEnv &actual_sink = reference_env_get();

		reference_env_before_each(&actual_sink);
		run_reference_receive_once(actual_sink, raw, raw_len, -35, 12);
		actual_snapshot = capture_reference_snapshot(actual_sink);
	}

	expect_joint_snapshot_match(expected_snapshot, actual_snapshot,
				    "interop target ack -> reference");
}

ZTEST(meshcore_dispatcher_mesh_tdd,
      test_reference_stack_send_flood_txt_to_target_stack)
{
	static const uint8_t kSecret[PUB_KEY_SIZE] = {
		0x10, 0x20, 0x30, 0x40, 0x11, 0x21, 0x31, 0x41,
		0x12, 0x22, 0x32, 0x42, 0x13, 0x23, 0x33, 0x43,
		0x14, 0x24, 0x34, 0x44, 0x15, 0x25, 0x35, 0x45,
		0x16, 0x26, 0x36, 0x46, 0x17, 0x27, 0x37, 0x47,
	};
	static const uint8_t kData[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
	mesh::LocalIdentity receiver;
	mesh::Identity dest;
	JointSnapshot expected_before;
	JointSnapshot expected_after;
	JointSnapshot actual_before;
	JointSnapshot actual_after;
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t raw_len;

	make_reference_local_identity(&receiver, kIdentitySeedA, sizeof(kIdentitySeedA));
	dest = mesh::Identity(receiver.pub_key);

	{
		ReferenceEnv &source = reference_env_get();

		reference_env_before_each(&source);
		begin_reference_env(source);
		mesh::Packet *created = source.mesh.createDatagram(
			PAYLOAD_TYPE_TXT_MSG, dest, kSecret, kData, sizeof(kData));
		zassert_not_null(created, "reference createDatagram failed");
		source.mesh.sendFlood(created, static_cast<uint32_t>(0U),
				       static_cast<uint8_t>(1U));
		raw_len = capture_reference_outbound_raw(source, raw,
							 "reference txt source");
		drive_reference_send_until_complete(source);
	}

	{
		ReferenceEnv &expected_sink = reference_env_get();

		reference_env_before_each(&expected_sink);
		prepare_peer_secret_script(&expected_sink.script, kSecret, 1);
		expected_sink.mesh.self_id = receiver;
		run_reference_receive_once(expected_sink, raw, raw_len, -40, 9);
		expected_before = capture_reference_snapshot(expected_sink);
		meshcore_hal_test_millis_advance(5000U);
		expected_sink.mesh.loop();
		expected_after = capture_reference_snapshot(expected_sink);
	}

	{
		TargetEnv *actual_sink = make_target_env();

		reset_fake_runtime();
		prepare_peer_secret_script(&actual_sink->script, kSecret, 1);
		actual_sink->sync_script_to_hal();
		sync_target_self_identity(*actual_sink, receiver);
		run_target_receive_once(*actual_sink, raw, raw_len, -40, 9);
		actual_before = capture_target_snapshot(*actual_sink);
		meshcore_hal_test_millis_advance(5000U);
		target_mesh_loop(*actual_sink);
		actual_after = capture_target_snapshot(*actual_sink);
		destroy_target_env(actual_sink);
	}

	expect_joint_snapshot_match(expected_before, actual_before,
				    "interop reference txt -> target before");
	expect_joint_snapshot_match(expected_after, actual_after,
				    "interop reference txt -> target after");
}

ZTEST(meshcore_dispatcher_mesh_tdd,
      test_reference_stack_send_flood_advert_to_target_stack)
{
	static const uint8_t kAppData[] = { 0xA1, 0xB2, 0xC3, 0xD4 };
	mesh::LocalIdentity sender;
	JointSnapshot expected_snapshot;
	JointSnapshot actual_snapshot;
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t raw_len;

	make_reference_local_identity(&sender, kIdentitySeedB, sizeof(kIdentitySeedB));

	{
		ReferenceEnv &source = reference_env_get();

		reference_env_before_each(&source);
		begin_reference_env(source);
		mesh::Packet *created =
			source.mesh.createAdvert(sender, kAppData, sizeof(kAppData));
		zassert_not_null(created, "reference createAdvert failed");
		source.mesh.sendFlood(created, static_cast<uint32_t>(0U),
				       static_cast<uint8_t>(1U));
		raw_len = capture_reference_outbound_raw(source, raw,
							 "reference advert source");
		drive_reference_send_until_complete(source);
	}

	{
		ReferenceEnv &expected_sink = reference_env_get();

		reference_env_before_each(&expected_sink);
		run_reference_receive_once(expected_sink, raw, raw_len, -34, 10);
		expected_snapshot = capture_reference_snapshot(expected_sink);
	}

	{
		TargetEnv *actual_sink = make_target_env();

		reset_fake_runtime();
		run_target_receive_once(*actual_sink, raw, raw_len, -34, 10);
		actual_snapshot = capture_target_snapshot(*actual_sink);
		destroy_target_env(actual_sink);
	}

	expect_joint_snapshot_match(expected_snapshot, actual_snapshot,
				    "interop reference advert -> target");
}

ZTEST(meshcore_dispatcher_mesh_tdd,
      test_target_stack_send_flood_advert_to_reference_stack)
{
	static const uint8_t kAppData[] = { 0x19, 0x28, 0x37 };
	mesh::LocalIdentity sender;
	JointSnapshot expected_snapshot;
	JointSnapshot actual_snapshot;
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t raw_len;

	make_reference_local_identity(&sender, kIdentitySeedA, sizeof(kIdentitySeedA));

	{
		TargetEnv *source = make_target_env();

		reset_fake_runtime();
		begin_target_env(*source);
		sync_target_self_identity(*source, sender);
		struct meshcore_packet *created = meshcore_mesh_create_advert(
			&source->mesh, &source->mesh.self_id, kAppData, sizeof(kAppData));
		zassert_not_null(created, "target createAdvert failed");
		meshcore_mesh_send_flood(&source->mesh, created, 0U, 1U);
		raw_len = capture_target_outbound_raw(*source, raw, "target advert source");
		drive_target_send_until_complete(*source);
		destroy_target_env(source);
	}

	{
		TargetEnv *expected_sink = make_target_env();

		reset_fake_runtime();
		run_target_receive_once(*expected_sink, raw, raw_len, -34, 10);
		expected_snapshot = capture_target_snapshot(*expected_sink);
		destroy_target_env(expected_sink);
	}

	{
		ReferenceEnv &actual_sink = reference_env_get();

		reference_env_before_each(&actual_sink);
		run_reference_receive_once(actual_sink, raw, raw_len, -34, 10);
		actual_snapshot = capture_reference_snapshot(actual_sink);
	}

	expect_joint_snapshot_match(expected_snapshot, actual_snapshot,
				    "interop target advert -> reference");
}

ZTEST(meshcore_dispatcher_mesh_tdd,
      test_reference_stack_send_direct_multipart_ack_to_target_stack)
{
	static const uint32_t kAckCrc = 0x5566AABBU;
	mesh::LocalIdentity receiver;
	ReferenceEnv &expected_sink = reference_env_get();
	JointSnapshot expected_snapshot;
	JointSnapshot actual_snapshot;
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t raw_len;
	uint8_t path[2] = { 0 };

	make_reference_local_identity(&receiver, kIdentitySeedB, sizeof(kIdentitySeedB));
	receiver.copyHashTo(path);
	path[1] = 0x22U;

	{
		ReferenceEnv &source = reference_env_get();

		reference_env_before_each(&source);
		begin_reference_env(source);
		mesh::Packet *created = source.mesh.createMultiAck(kAckCrc, 2U);
		zassert_not_null(created, "reference createMultiAck failed");
		source.mesh.sendDirect(created, path, sizeof(path), 0U);
		raw_len = capture_reference_outbound_raw(source, raw,
							 "reference multi-ack source");
		drive_reference_send_until_complete(source);
	}

	reference_env_before_each(&expected_sink);
	expected_sink.mesh.self_id = receiver;
	expected_sink.script.override_allow_packet_forward = true;
	expected_sink.script.allow_packet_forward_value = true;
	run_reference_receive_once(expected_sink, raw, raw_len, -33, 8);
	expected_snapshot = capture_reference_snapshot(expected_sink);

	{
		TargetEnv *actual_sink = make_target_env();

		reset_fake_runtime();
		actual_sink->script.override_allow_packet_forward = true;
		actual_sink->script.allow_packet_forward_value = true;
		actual_sink->sync_script_to_hal();
		sync_target_self_identity(*actual_sink, receiver);
		run_target_receive_once(*actual_sink, raw, raw_len, -33, 8);
		actual_snapshot = capture_target_snapshot(*actual_sink);
		expect_joint_snapshot_match(expected_snapshot, actual_snapshot,
					    "interop reference multi-ack -> target");
		expect_reference_outbound_matches_target(expected_sink, *actual_sink,
							 "interop reference multi-ack -> target");
		destroy_target_env(actual_sink);
	}
}

ZTEST(meshcore_dispatcher_mesh_tdd,
      test_target_stack_send_flood_path_to_reference_stack)
{
	static const uint8_t kSecret[PUB_KEY_SIZE] = {
		0x91, 0x82, 0x73, 0x64, 0x55, 0x46, 0x37, 0x28,
		0x19, 0x0A, 0x1B, 0x2C, 0x3D, 0x4E, 0x5F, 0x60,
		0x70, 0x61, 0x52, 0x43, 0x34, 0x25, 0x16, 0x07,
		0xF8, 0xE9, 0xDA, 0xCB, 0xBC, 0xAD, 0x9E, 0x8F,
	};
	static const uint8_t kPath[] = { 0x6BU };
	static const uint8_t kExtra[] = { 0xD1, 0xE2, 0xF3 };
	static const uint8_t kSinkRng[] = { 0x41, 0x52, 0x63, 0x74 };
	mesh::LocalIdentity receiver;
	struct meshcore_identity actual_dest;
	JointSnapshot expected_snapshot;
	JointSnapshot actual_snapshot;
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t raw_len;

	make_reference_local_identity(&receiver, kIdentitySeedB, sizeof(kIdentitySeedB));
	meshcore_identity_init_from_pub_key(&actual_dest, receiver.pub_key);

	{
		TargetEnv *source = make_target_env();

		reset_fake_runtime();
		begin_target_env(*source);
		struct meshcore_packet *created =
			meshcore_mesh_create_path_return_by_identity(
				&source->mesh, &actual_dest, kSecret, kPath,
				sizeof(kPath), PAYLOAD_TYPE_REQ, kExtra,
				sizeof(kExtra));
		zassert_not_null(created, "target createPathReturn failed");
		meshcore_mesh_send_flood(&source->mesh, created, 0U, 1U);
		raw_len = capture_target_outbound_raw(*source, raw, "target path source");
		drive_target_send_until_complete(*source);
		destroy_target_env(source);
	}

	{
		TargetEnv *expected_sink = make_target_env();

		reset_fake_runtime();
		meshcore_hal_test_rng_set_bytes(kSinkRng, sizeof(kSinkRng));
		prepare_path_script(&expected_sink->script, kSecret, 1);
		expected_sink->sync_script_to_hal();
		sync_target_self_identity(*expected_sink, receiver);
		run_target_receive_once(*expected_sink, raw, raw_len, -38, 11);
		expected_snapshot = capture_target_snapshot(*expected_sink);
		ReferenceEnv &actual_sink = reference_env_get();

		reference_env_before_each(&actual_sink);
		meshcore_hal_test_rng_set_bytes(kSinkRng, sizeof(kSinkRng));
		prepare_path_script(&actual_sink.script, kSecret, 1);
		actual_sink.mesh.self_id = receiver;
		run_reference_receive_once(actual_sink, raw, raw_len, -38, 11);
		actual_snapshot = capture_reference_snapshot(actual_sink);

		expect_joint_snapshot_match(expected_snapshot, actual_snapshot,
					    "interop target path -> reference");
		expect_target_outbound_matches_reference(
			*expected_sink, actual_sink,
			"interop target path -> reference");
		destroy_target_env(expected_sink);
	}
}

ZTEST(meshcore_dispatcher_mesh_tdd,
      test_target_stack_send_direct_multipart_ack_to_reference_stack)
{
	static const uint32_t kAckCrc = 0x90ABCDEFU;
	mesh::LocalIdentity receiver;
	JointSnapshot expected_snapshot;
	JointSnapshot actual_snapshot;
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t raw_len;
	uint8_t path[2] = { 0 };

	make_reference_local_identity(&receiver, kIdentitySeedA, sizeof(kIdentitySeedA));
	receiver.copyHashTo(path);
	path[1] = 0x35U;

	{
		TargetEnv *source = make_target_env();

		reset_fake_runtime();
		begin_target_env(*source);
		struct meshcore_packet *created =
			meshcore_mesh_create_multi_ack(&source->mesh, kAckCrc, 2U);
		zassert_not_null(created, "target createMultiAck failed");
		meshcore_mesh_send_direct(&source->mesh, created, path, sizeof(path), 0U);
		raw_len = capture_target_outbound_raw(*source, raw,
						      "target multi-ack source");
		drive_target_send_until_complete(*source);
		destroy_target_env(source);
	}

	{
		TargetEnv *expected_sink = make_target_env();

		reset_fake_runtime();
		expected_sink->script.override_allow_packet_forward = true;
		expected_sink->script.allow_packet_forward_value = true;
		expected_sink->sync_script_to_hal();
		sync_target_self_identity(*expected_sink, receiver);
		run_target_receive_once(*expected_sink, raw, raw_len, -33, 8);
		expected_snapshot = capture_target_snapshot(*expected_sink);
		ReferenceEnv &actual_sink = reference_env_get();

		reference_env_before_each(&actual_sink);
		actual_sink.mesh.self_id = receiver;
		actual_sink.script.override_allow_packet_forward = true;
		actual_sink.script.allow_packet_forward_value = true;
		run_reference_receive_once(actual_sink, raw, raw_len, -33, 8);
		actual_snapshot = capture_reference_snapshot(actual_sink);

		expect_joint_snapshot_match(expected_snapshot, actual_snapshot,
					    "interop target multi-ack -> reference");
		expect_target_outbound_matches_reference(
			*expected_sink, actual_sink,
			"interop target multi-ack -> reference");
		destroy_target_env(expected_sink);
	}
}

} // namespace meshcore_dispatcher_mesh_tdd
