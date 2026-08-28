// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "test_target_env.h"

#include <string.h>

namespace meshcore_dispatcher_tdd {

static void sync_input_script_to_hal(const DispatcherInputScript &src)
{
	meshcore_hal_test_dispatcher_script_t *dest =
		meshcore_hal_test_dispatcher_script_get();

	*dest = src;
}

TargetEnv::TargetEnv()
	: script(default_script()),
	  manager(),
	  owner_mesh(),
	  dispatcher()
{
	meshcore_packet_queue_manager_prepare(&manager, kPoolSize);
	memset(&owner_mesh, 0, sizeof(owner_mesh));
	sync_driver_inputs();
	meshcore_dispatcher_init(&dispatcher, &manager, &owner_mesh);
}

TargetEnv::~TargetEnv()
{
	meshcore_hal_test_dispatcher_script_mirror_set(NULL);
	meshcore_packet_queue_manager_deinit(&manager);
}

void TargetEnv::sync_driver_inputs()
{
	sync_input_script_to_hal(script);
	meshcore_hal_test_dispatcher_script_mirror_set(&script);
	owner_mesh.script = &script;
}

static void complete_tx_if_ready(TargetEnv &env)
{
	if (env.dispatcher.outbound != NULL &&
	    meshcore_hal_test_radio_tx_complete_get()) {
		meshcore_hal_test_radio_on_send_finished();
		(void)meshcore_dispatcher_tx_done(&env.dispatcher, true);
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
		&env.dispatcher, raw, (size_t)len,
		(int16_t)meshcore_hal_test_radio_last_rssi_get(),
		(int8_t)(meshcore_hal_test_radio_last_snr_get() * 4.0f));
	zassert_true(rc == 0 || rc == -EINVAL,
		     "target receive_raw returned unexpected rc=%d", rc);
}

void target_dispatcher_loop(TargetEnv &env)
{
	bool had_active_outbound;

	env.sync_driver_inputs();
	complete_tx_if_ready(env);
	receive_injected_raw(env);

	had_active_outbound = env.dispatcher.outbound != NULL;
	meshcore_dispatcher_loop(&env.dispatcher);
	if (had_active_outbound && env.dispatcher.outbound == NULL) {
		meshcore_hal_test_radio_on_send_finished();
	}
}

DispatcherSnapshot capture_target_snapshot(TargetEnv &env)
{
	DispatcherSnapshot snapshot = {};
	struct meshcore_packet *outbound;

	env.sync_driver_inputs();
	outbound = meshcore_packet_queue_manager_get_outbound_total(&env.manager) > 0
			   ? meshcore_packet_queue_manager_get_outbound_by_idx(
				 &env.manager, 0)
			   : nullptr;

	snapshot.total_air_time =
		meshcore_dispatcher_get_total_air_time(&env.dispatcher);
	snapshot.rx_air_time =
		meshcore_dispatcher_get_receive_air_time(&env.dispatcher);
	snapshot.remaining_tx_budget =
		meshcore_dispatcher_get_remaining_tx_budget(&env.dispatcher);
	snapshot.num_sent_flood =
		meshcore_dispatcher_get_num_sent_flood(&env.dispatcher);
	snapshot.num_sent_direct =
		meshcore_dispatcher_get_num_sent_direct(&env.dispatcher);
	snapshot.num_recv_flood =
		meshcore_dispatcher_get_num_recv_flood(&env.dispatcher);
	snapshot.num_recv_direct =
		meshcore_dispatcher_get_num_recv_direct(&env.dispatcher);
	snapshot.err_flags = meshcore_dispatcher_get_err_flags(&env.dispatcher);
	snapshot.free_count =
		meshcore_packet_queue_manager_get_free_count(&env.manager);
	snapshot.outbound_total =
		meshcore_packet_queue_manager_get_outbound_total(&env.manager);
	snapshot.log_rx_raw_count = env.script.log_rx_raw_count;
	snapshot.log_rx_count = env.script.log_rx_count;
	snapshot.log_tx_count = env.script.log_tx_count;
	snapshot.log_tx_fail_count = env.script.log_tx_fail_count;
	snapshot.radio_packets_recv = meshcore_hal_test_radio_get_packets_recv();
	snapshot.radio_packets_sent = meshcore_hal_test_radio_get_packets_sent();
	snapshot.noise_floor_calibrate_count =
		meshcore_hal_test_radio_get_noise_floor_calibrate_count();
	snapshot.agc_reset_count =
		meshcore_hal_test_radio_get_agc_reset_count();
	snapshot.on_send_finished_count =
		meshcore_hal_test_radio_get_on_send_finished_count();
	snapshot.last_send_len = meshcore_hal_test_radio_get_last_send_len();
	if (outbound != nullptr) {
		snapshot.first_outbound_len =
			meshcore_packet_write_to(outbound, snapshot.first_outbound_raw);
	}
	return snapshot;
}

} // namespace meshcore_dispatcher_tdd

extern "C" {

meshcore_dispatcher_action meshcore_mesh_on_recv_packet(
	struct meshcore_mesh *mesh, struct meshcore_packet *packet)
{
	meshcore_hal_test_dispatcher_script_t *script;

	if (mesh == NULL || packet == NULL) {
		return MESHCORE_ACTION_RELEASE;
	}

	script = mesh->script;
	if (script == NULL) {
		return MESHCORE_ACTION_RELEASE;
	}

	/* Keep owner behavior minimal: dispatcher tests only care about the action. */
	script->handled_count++;
	return script->recv_action;
}

uint32_t meshcore_mesh_runtime_get_cad_fail_retry_delay(
	struct meshcore_mesh *mesh)
{
	if (mesh == NULL || mesh->script == NULL) {
		return 200U;
	}

	return mesh->script->cad_fail_retry_delay_ms;
}

} // extern "C"
