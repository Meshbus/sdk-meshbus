// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "test_target_env.h"

namespace meshcore_dispatcher_mesh_tdd {

static void copy_mesh_script_to_hal(const MeshScript &src)
{
	meshcore_hal_test_mesh_script_t *dest =
		meshcore_hal_test_mesh_script_get();

	*dest = src;
}

static void copy_mesh_script_from_hal(MeshScript *dest)
{
	const meshcore_hal_test_mesh_script_t *src =
		meshcore_hal_test_mesh_script_get();

	dest->on_peer_data_recv_count = src->on_peer_data_recv_count;
	dest->on_trace_recv_count = src->on_trace_recv_count;
	dest->on_peer_path_recv_count = src->on_peer_path_recv_count;
	dest->on_advert_recv_count = src->on_advert_recv_count;
	dest->on_anon_data_recv_count = src->on_anon_data_recv_count;
	dest->on_control_data_recv_count = src->on_control_data_recv_count;
	dest->on_raw_data_recv_count = src->on_raw_data_recv_count;
	dest->on_group_data_recv_count = src->on_group_data_recv_count;
	dest->on_ack_recv_count = src->on_ack_recv_count;
	dest->last_ack_crc = src->last_ack_crc;
	dest->last_peer_data_type = src->last_peer_data_type;
	dest->last_peer_data_len = src->last_peer_data_len;
	dest->last_group_data_type = src->last_group_data_type;
	dest->last_group_data_len = src->last_group_data_len;
	dest->last_anon_data_len = src->last_anon_data_len;
	dest->last_peer_path_len = src->last_peer_path_len;
	dest->last_peer_path_extra_type = src->last_peer_path_extra_type;
	dest->last_peer_path_extra_len = src->last_peer_path_extra_len;
	dest->last_trace_tag = src->last_trace_tag;
	dest->last_trace_auth_code = src->last_trace_auth_code;
	dest->last_trace_flags = src->last_trace_flags;
}

TargetEnv::TargetEnv() : script(default_script()), manager(), tables(), mesh()
{
	meshcore_packet_queue_manager_prepare(&manager, kPoolSize);
	meshcore_tables_init(&tables);
	sync_script_to_hal();
	meshcore_mesh_init(&mesh, &manager, &tables);
}

TargetEnv::~TargetEnv()
{
	meshcore_hal_test_mesh_script_mirror_set(NULL);
	meshcore_packet_queue_manager_deinit(&manager);
}

void TargetEnv::sync_script_to_hal()
{
	copy_mesh_script_to_hal(script);
	meshcore_hal_test_mesh_script_mirror_set(&script);
}

void TargetEnv::sync_script_from_hal()
{
	copy_mesh_script_from_hal(&script);
}

static void complete_tx_if_ready(TargetEnv &env)
{
	if (env.mesh.dispatcher.outbound != NULL &&
	    meshcore_hal_test_radio_tx_complete_get()) {
		meshcore_hal_test_radio_on_send_finished();
		(void)meshcore_dispatcher_tx_done(&env.mesh.dispatcher, true);
	}
}

static void receive_injected_raw(TargetEnv &env)
{
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN];
	int len;
	int rc;

	len = meshcore_hal_test_radio_recv_raw(raw, sizeof(raw));
	if (len <= 0) {
		return;
	}

	rc = meshcore_dispatcher_receive_raw(
		&env.mesh.dispatcher, raw, (size_t)len,
		(int16_t)meshcore_hal_test_radio_last_rssi_get(),
		(int8_t)(meshcore_hal_test_radio_last_snr_get() * 4.0f));
	zassert_true(rc == 0 || rc == -EINVAL,
		     "target receive_raw returned unexpected rc=%d", rc);
}

void target_mesh_loop(TargetEnv &env)
{
	bool had_active_outbound;

	env.sync_script_to_hal();
	complete_tx_if_ready(env);
	receive_injected_raw(env);

	had_active_outbound = env.mesh.dispatcher.outbound != NULL;
	meshcore_mesh_loop(&env.mesh);
	if (had_active_outbound && env.mesh.dispatcher.outbound == NULL) {
		meshcore_hal_test_radio_on_send_finished();
	}
	env.sync_script_from_hal();
}

JointSnapshot capture_target_snapshot(TargetEnv &env)
{
	JointSnapshot snapshot = {};
	struct meshcore_packet *outbound;

	env.sync_script_from_hal();
	outbound = meshcore_packet_queue_manager_get_outbound_total(&env.manager) > 0
			   ? meshcore_packet_queue_manager_get_outbound_by_idx(
				 &env.manager, 0)
			   : NULL;

	snapshot.total_air_time =
		meshcore_dispatcher_get_total_air_time(&env.mesh.dispatcher);
	snapshot.rx_air_time =
		meshcore_dispatcher_get_receive_air_time(&env.mesh.dispatcher);
	snapshot.remaining_tx_budget =
		meshcore_dispatcher_get_remaining_tx_budget(&env.mesh.dispatcher);
	snapshot.num_sent_flood =
		meshcore_dispatcher_get_num_sent_flood(&env.mesh.dispatcher);
	snapshot.num_sent_direct =
		meshcore_dispatcher_get_num_sent_direct(&env.mesh.dispatcher);
	snapshot.num_recv_flood =
		meshcore_dispatcher_get_num_recv_flood(&env.mesh.dispatcher);
	snapshot.num_recv_direct =
		meshcore_dispatcher_get_num_recv_direct(&env.mesh.dispatcher);
	snapshot.err_flags =
		meshcore_dispatcher_get_err_flags(&env.mesh.dispatcher);
	snapshot.free_count =
		meshcore_packet_queue_manager_get_free_count(&env.manager);
	snapshot.outbound_total =
		meshcore_packet_queue_manager_get_outbound_total(&env.manager);
	snapshot.on_peer_data_recv_count = env.script.on_peer_data_recv_count;
	snapshot.on_trace_recv_count = env.script.on_trace_recv_count;
	snapshot.on_peer_path_recv_count = env.script.on_peer_path_recv_count;
	snapshot.on_advert_recv_count = env.script.on_advert_recv_count;
	snapshot.on_anon_data_recv_count = env.script.on_anon_data_recv_count;
	snapshot.on_control_data_recv_count = env.script.on_control_data_recv_count;
	snapshot.on_raw_data_recv_count = env.script.on_raw_data_recv_count;
	snapshot.on_group_data_recv_count = env.script.on_group_data_recv_count;
	snapshot.on_ack_recv_count = env.script.on_ack_recv_count;
	snapshot.last_ack_crc = env.script.last_ack_crc;
	snapshot.last_peer_data_type = env.script.last_peer_data_type;
	snapshot.last_peer_data_len = env.script.last_peer_data_len;
	snapshot.last_group_data_type = env.script.last_group_data_type;
	snapshot.last_group_data_len = env.script.last_group_data_len;
	snapshot.last_anon_data_len = env.script.last_anon_data_len;
	snapshot.last_peer_path_len = env.script.last_peer_path_len;
	snapshot.last_peer_path_extra_type = env.script.last_peer_path_extra_type;
	snapshot.last_peer_path_extra_len = env.script.last_peer_path_extra_len;
	snapshot.last_trace_tag = env.script.last_trace_tag;
	snapshot.last_trace_auth_code = env.script.last_trace_auth_code;
	snapshot.last_trace_flags = env.script.last_trace_flags;
	snapshot.radio_packets_recv = meshcore_hal_test_radio_get_packets_recv();
	snapshot.radio_packets_sent = meshcore_hal_test_radio_get_packets_sent();
	snapshot.noise_floor_calibrate_count =
		meshcore_hal_test_radio_get_noise_floor_calibrate_count();
	snapshot.agc_reset_count =
		meshcore_hal_test_radio_get_agc_reset_count();
	snapshot.on_send_finished_count =
		meshcore_hal_test_radio_get_on_send_finished_count();
	snapshot.last_send_len = meshcore_hal_test_radio_get_last_send_len();
	if (outbound != NULL) {
		snapshot.first_outbound_len =
			meshcore_packet_write_to(outbound, snapshot.first_outbound_raw);
	}
	return snapshot;
}

} // namespace meshcore_dispatcher_mesh_tdd
