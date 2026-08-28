// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#ifndef FOBE_TESTS_LIB_MESHCORE_MODULE_MESH_SRC_TEST_SUPPORT_H_
#define FOBE_TESTS_LIB_MESHCORE_MODULE_MESH_SRC_TEST_SUPPORT_H_

#include <stddef.h>
#include <stdint.h>

#include "test_packet_bridge.h"

namespace meshcore_mesh_tdd {

struct ReferenceEnv;
struct TargetEnv;

constexpr int kPoolSize = 8;
constexpr unsigned long kStartMillis = 1000UL;
constexpr uint32_t kStartRtc = 1700000000U;

using MeshScript = meshcore_hal_test_mesh_script_t;

struct MeshPolicySnapshot {
	bool filter_recv_flood_packet;
	bool allow_packet_forward;
	uint32_t retransmit_delay;
	uint32_t direct_retransmit_delay;
	uint8_t extra_ack_transmit_count;
	uint32_t cad_fail_retry_delay;
	int search_peers_by_hash;
	int search_channels_by_hash;
};

MeshScript default_script(void);
void reset_fake_runtime(void);

void init_reference_packet(mesh::Packet *packet, uint8_t route_type, uint8_t seed,
			   uint8_t payload_len = 5U);
void init_target_packet(struct meshcore_packet *packet, uint8_t route_type,
			uint8_t seed, uint8_t payload_len = 5U);
void make_reference_local_identity(mesh::LocalIdentity *dest,
				   const uint8_t *rng_data, size_t rng_len);
void sync_target_self_identity(TargetEnv &target,
			       mesh::LocalIdentity &reference_identity);
void clone_target_packet_from_reference(
	struct meshcore_packet *target, const mesh::Packet *reference);
void expect_packet_state_match(const mesh::Packet *expected,
			       const struct meshcore_packet *actual,
			       const char *label);
void expect_created_packet_raw_match(const mesh::Packet *expected_packet,
				     const struct meshcore_packet *actual_packet,
				     const char *label);
void expect_create_result_match_and_free(
	ReferenceEnv &expected, mesh::Packet *expected_packet, TargetEnv &actual,
	struct meshcore_packet *actual_packet, const char *label);

void expect_snapshot_match(const MeshPolicySnapshot &expected,
			   const MeshPolicySnapshot &actual,
			   const char *label);
void expect_script_observation_match(const MeshScript &expected,
				     const MeshScript &actual,
				     const char *label);

} // namespace meshcore_mesh_tdd

#endif /* FOBE_TESTS_LIB_MESHCORE_MODULE_MESH_SRC_TEST_SUPPORT_H_ */
