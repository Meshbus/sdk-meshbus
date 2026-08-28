// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#ifndef FOBE_TESTS_LIB_MESHCORE_MODULE_DISPATCHER_MESH_SRC_TEST_SUPPORT_H_
#define FOBE_TESTS_LIB_MESHCORE_MODULE_DISPATCHER_MESH_SRC_TEST_SUPPORT_H_

#include <stddef.h>
#include <stdint.h>

#include "test_packet_bridge.h"

namespace meshcore_dispatcher_mesh_tdd {

struct ReferenceEnv;
struct TargetEnv;

constexpr int kPoolSize = 8;
constexpr unsigned long kStartMillis = 1000UL;
constexpr uint32_t kStartRtc = 1700000000U;

extern const uint8_t kIdentitySeedA[16];
extern const uint8_t kIdentitySeedB[16];

using MeshScript = meshcore_hal_test_mesh_script_t;

struct JointSnapshot {
	unsigned long total_air_time;
	unsigned long rx_air_time;
	unsigned long remaining_tx_budget;
	uint32_t num_sent_flood;
	uint32_t num_sent_direct;
	uint32_t num_recv_flood;
	uint32_t num_recv_direct;
	uint16_t err_flags;
	int free_count;
	int outbound_total;
	uint32_t on_peer_data_recv_count;
	uint32_t on_trace_recv_count;
	uint32_t on_peer_path_recv_count;
	uint32_t on_advert_recv_count;
	uint32_t on_anon_data_recv_count;
	uint32_t on_control_data_recv_count;
	uint32_t on_raw_data_recv_count;
	uint32_t on_group_data_recv_count;
	uint32_t on_ack_recv_count;
	uint32_t last_ack_crc;
	uint8_t last_peer_data_type;
	uint16_t last_peer_data_len;
	uint8_t last_group_data_type;
	uint16_t last_group_data_len;
	uint16_t last_anon_data_len;
	uint8_t last_peer_path_len;
	uint8_t last_peer_path_extra_type;
	uint8_t last_peer_path_extra_len;
	uint32_t last_trace_tag;
	uint32_t last_trace_auth_code;
	uint8_t last_trace_flags;
	uint32_t radio_packets_recv;
	uint32_t radio_packets_sent;
	uint32_t noise_floor_calibrate_count;
	uint32_t agc_reset_count;
	uint32_t on_send_finished_count;
	int last_send_len;
	uint8_t first_outbound_len;
	uint8_t first_outbound_raw[MESHCORE_MAX_TRANS_UNIT_LEN];
};

MeshScript default_script(void);
void reset_fake_runtime(void);
void make_reference_local_identity(mesh::LocalIdentity *dest,
				   const uint8_t *rng_data, size_t rng_len);
void sync_target_self_identity(TargetEnv &target,
			       mesh::LocalIdentity &reference_identity);
void prepare_peer_secret_script(MeshScript *script, const uint8_t *secret,
				int sender_idx);
void prepare_path_script(MeshScript *script, const uint8_t *secret,
			 int sender_idx);

void begin_reference_env(ReferenceEnv &env);
void begin_target_env(TargetEnv &env);
void run_reference_receive_once(ReferenceEnv &env, const uint8_t *raw,
				uint8_t raw_len, int16_t rssi_dbm,
				int8_t snr_db);
void run_target_receive_once(TargetEnv &env, const uint8_t *raw,
			     uint8_t raw_len, int16_t rssi_dbm,
			     int8_t snr_db);
void drive_reference_send_until_complete(ReferenceEnv &env);
void drive_target_send_until_complete(TargetEnv &env);
uint8_t capture_reference_outbound_raw(ReferenceEnv &env,
				       uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN],
				       const char *label);
uint8_t capture_target_outbound_raw(TargetEnv &env,
				    uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN],
				    const char *label);

void expect_joint_snapshot_match(const JointSnapshot &expected,
				 const JointSnapshot &actual,
				 const char *label);
void expect_reference_outbound_matches_target(ReferenceEnv &expected,
					      TargetEnv &actual,
					      const char *label);
void expect_target_outbound_matches_reference(TargetEnv &expected,
					      ReferenceEnv &actual,
					      const char *label);

} // namespace meshcore_dispatcher_mesh_tdd

#endif /* FOBE_TESTS_LIB_MESHCORE_MODULE_DISPATCHER_MESH_SRC_TEST_SUPPORT_H_ */
