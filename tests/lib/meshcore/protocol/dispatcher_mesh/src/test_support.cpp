// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "test_support.h"

#include <string.h>

#include "test_reference_env.h"
#include "test_target_env.h"

namespace meshcore_dispatcher_mesh_tdd {

class FixedRNG : public mesh::RNG {
public:
	FixedRNG(const uint8_t *src, size_t len) : data_(src), len_(len), idx_(0U)
	{
	}

	void random(uint8_t *dest, size_t sz) override
	{
		for (size_t i = 0; i < sz; i++) {
			dest[i] = data_[idx_ % len_];
			idx_++;
		}
	}

private:
	const uint8_t *data_;
	size_t len_;
	size_t idx_;
};

const uint8_t kIdentitySeedA[16] = {
	0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
	0x98, 0xA9, 0xBA, 0xCB, 0xDC, 0xED, 0xFE, 0x0F,
};

const uint8_t kIdentitySeedB[16] = {
	0x7E, 0x6D, 0x5C, 0x4B, 0x3A, 0x29, 0x18, 0x07,
	0xF6, 0xE5, 0xD4, 0xC3, 0xB2, 0xA1, 0x90, 0x8F,
};

MeshScript default_script(void)
{
	MeshScript script = {};

	script.override_filter_recv_flood_packet = false;
	script.filter_recv_flood_packet_value = false;
	script.override_allow_packet_forward = false;
	script.allow_packet_forward_value = false;
	script.override_get_retransmit_delay = false;
	script.get_retransmit_delay_value = 0U;
	script.override_get_direct_retransmit_delay = false;
	script.get_direct_retransmit_delay_value = 0U;
	script.override_get_extra_ack_transmit_count = false;
	script.get_extra_ack_transmit_count_value = 0U;
	script.override_get_cad_fail_retry_delay = false;
	script.get_cad_fail_retry_delay_value = 0U;
	script.override_search_peers_by_hash = false;
	script.search_peers_by_hash_value = 0;
	script.override_search_channels_by_hash = false;
	script.search_channels_by_hash_value = 0;
	script.override_get_peer_shared_secret = false;
	memset(script.peer_shared_secret, 0, sizeof(script.peer_shared_secret));
	script.override_search_channels_fill = false;
	memset(script.search_channel_hash, 0, sizeof(script.search_channel_hash));
	memset(script.search_channel_secret, 0, sizeof(script.search_channel_secret));
	script.override_on_peer_path_recv = false;
	script.on_peer_path_recv_value = false;
	script.on_peer_data_recv_count = 0U;
	script.on_trace_recv_count = 0U;
	script.on_peer_path_recv_count = 0U;
	script.on_advert_recv_count = 0U;
	script.on_anon_data_recv_count = 0U;
	script.on_control_data_recv_count = 0U;
	script.on_raw_data_recv_count = 0U;
	script.on_group_data_recv_count = 0U;
	script.on_ack_recv_count = 0U;
	script.last_ack_crc = 0U;
	script.last_peer_data_type = 0U;
	script.last_peer_data_len = 0U;
	script.last_group_data_type = 0U;
	script.last_group_data_len = 0U;
	script.last_anon_data_len = 0U;
	script.last_peer_path_len = 0U;
	script.last_peer_path_extra_type = 0U;
	script.last_peer_path_extra_len = 0U;
	script.last_trace_tag = 0U;
	script.last_trace_auth_code = 0U;
	script.last_trace_flags = 0U;
	return script;
}

void reset_fake_runtime(void)
{
	meshcore_hal_test_host_state_reset();
	meshcore_hal_test_radio_reset();
	meshcore_hal_test_millis_clear();
	meshcore_hal_test_millis_set(kStartMillis);
	meshcore_hal_test_rtc_set_current_time(kStartRtc);
	meshcore_hal_test_rng_clear();
}

void make_reference_local_identity(mesh::LocalIdentity *dest,
				   const uint8_t *rng_data, size_t rng_len)
{
	FixedRNG rng(rng_data, rng_len);
	mesh::LocalIdentity identity(&rng);

	*dest = identity;
}

void sync_target_self_identity(TargetEnv &target,
			       mesh::LocalIdentity &reference_identity)
{
	uint8_t full_layout[PRV_KEY_SIZE + PUB_KEY_SIZE] = { 0 };
	size_t len = reference_identity.writeTo(full_layout, sizeof(full_layout));

	zassert_equal(sizeof(full_layout), len,
		      "reference identity serialize length mismatch");
	meshcore_local_identity_read_from(&target.mesh.self_id, full_layout, len);
}

void prepare_peer_secret_script(MeshScript *script, const uint8_t *secret,
				int sender_idx)
{
	script->override_search_peers_by_hash = true;
	script->search_peers_by_hash_value = sender_idx;
	script->override_get_peer_shared_secret = true;
	memcpy(script->peer_shared_secret, secret, PUB_KEY_SIZE);
}

void prepare_path_script(MeshScript *script, const uint8_t *secret,
			 int sender_idx)
{
	prepare_peer_secret_script(script, secret, sender_idx);
	script->override_on_peer_path_recv = true;
	script->on_peer_path_recv_value = true;
}

void begin_reference_env(ReferenceEnv &env)
{
	env.mesh.begin();
}

void begin_target_env(TargetEnv &env)
{
	meshcore_mesh_begin(&env.mesh);
}

void run_reference_receive_once(ReferenceEnv &env, const uint8_t *raw,
				uint8_t raw_len, int16_t rssi_dbm,
				int8_t snr_db)
{
	begin_reference_env(env);
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, raw_len, rssi_dbm,
							    snr_db, 0U),
		     "reference inject failed");
	env.mesh.loop();
}

void run_target_receive_once(TargetEnv &env, const uint8_t *raw,
			     uint8_t raw_len, int16_t rssi_dbm,
			     int8_t snr_db)
{
	begin_target_env(env);
	zassert_true(meshcore_hal_test_radio_inject_receive(raw, raw_len, rssi_dbm,
							    snr_db, 0U),
		     "target inject failed");
	target_mesh_loop(env);
}

void drive_reference_send_until_complete(ReferenceEnv &env)
{
	env.mesh.loop();
	if (meshcore_hal_test_radio_get_last_send_len() > 0) {
		meshcore_hal_test_millis_advance(
			(unsigned long)meshcore_hal_test_radio_get_last_send_len());
		env.mesh.loop();
	}
}

void drive_target_send_until_complete(TargetEnv &env)
{
	target_mesh_loop(env);
	if (meshcore_hal_test_radio_get_last_send_len() > 0) {
		meshcore_hal_test_millis_advance(
			(unsigned long)meshcore_hal_test_radio_get_last_send_len());
		target_mesh_loop(env);
	}
}

uint8_t capture_reference_outbound_raw(ReferenceEnv &env,
				       uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN],
				       const char *label)
{
	mesh::Packet *packet = env.manager.getOutboundByIdx(0);

	zassert_not_null(packet, "%s reference outbound packet missing", label);
	return packet->writeTo(raw);
}

uint8_t capture_target_outbound_raw(TargetEnv &env,
				    uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN],
				    const char *label)
{
	struct meshcore_packet *packet =
		meshcore_packet_queue_manager_get_outbound_by_idx(&env.manager, 0);

	zassert_not_null(packet, "%s target outbound packet missing", label);
	return meshcore_packet_write_to(packet, raw);
}

void expect_reference_outbound_matches_target(ReferenceEnv &expected,
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

void expect_target_outbound_matches_reference(TargetEnv &expected,
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

void expect_joint_snapshot_match(const JointSnapshot &expected,
				 const JointSnapshot &actual,
				 const char *label)
{
	zassert_equal(expected.total_air_time, actual.total_air_time,
		      "%s total_air_time mismatch", label);
	zassert_equal(expected.rx_air_time, actual.rx_air_time,
		      "%s rx_air_time mismatch", label);
	zassert_equal(expected.remaining_tx_budget, actual.remaining_tx_budget,
		      "%s remaining_tx_budget mismatch", label);
	zassert_equal(expected.num_sent_flood, actual.num_sent_flood,
		      "%s num_sent_flood mismatch", label);
	zassert_equal(expected.num_sent_direct, actual.num_sent_direct,
		      "%s num_sent_direct mismatch", label);
	zassert_equal(expected.num_recv_flood, actual.num_recv_flood,
		      "%s num_recv_flood mismatch", label);
	zassert_equal(expected.num_recv_direct, actual.num_recv_direct,
		      "%s num_recv_direct mismatch", label);
	zassert_equal(expected.err_flags, actual.err_flags,
		      "%s err_flags mismatch", label);
	zassert_equal(expected.free_count, actual.free_count,
		      "%s free_count mismatch", label);
	zassert_equal(expected.outbound_total, actual.outbound_total,
		      "%s outbound_total mismatch", label);
	zassert_equal(expected.on_peer_data_recv_count, actual.on_peer_data_recv_count,
		      "%s on_peer_data_recv_count mismatch expected=%u actual=%u",
		      label, expected.on_peer_data_recv_count,
		      actual.on_peer_data_recv_count);
	zassert_equal(expected.on_trace_recv_count, actual.on_trace_recv_count,
		      "%s on_trace_recv_count mismatch", label);
	zassert_equal(expected.on_peer_path_recv_count,
		      actual.on_peer_path_recv_count,
		      "%s on_peer_path_recv_count mismatch", label);
	zassert_equal(expected.on_advert_recv_count, actual.on_advert_recv_count,
		      "%s on_advert_recv_count mismatch", label);
	zassert_equal(expected.on_anon_data_recv_count,
		      actual.on_anon_data_recv_count,
		      "%s on_anon_data_recv_count mismatch", label);
	zassert_equal(expected.on_control_data_recv_count,
		      actual.on_control_data_recv_count,
		      "%s on_control_data_recv_count mismatch", label);
	zassert_equal(expected.on_raw_data_recv_count,
		      actual.on_raw_data_recv_count,
		      "%s on_raw_data_recv_count mismatch", label);
	zassert_equal(expected.on_group_data_recv_count,
		      actual.on_group_data_recv_count,
		      "%s on_group_data_recv_count mismatch", label);
	zassert_equal(expected.on_ack_recv_count, actual.on_ack_recv_count,
		      "%s on_ack_recv_count mismatch", label);
	zassert_equal(expected.last_ack_crc, actual.last_ack_crc,
		      "%s last_ack_crc mismatch", label);
	zassert_equal(expected.last_peer_data_type, actual.last_peer_data_type,
		      "%s last_peer_data_type mismatch", label);
	zassert_equal(expected.last_peer_data_len, actual.last_peer_data_len,
		      "%s last_peer_data_len mismatch", label);
	zassert_equal(expected.last_group_data_type, actual.last_group_data_type,
		      "%s last_group_data_type mismatch", label);
	zassert_equal(expected.last_group_data_len, actual.last_group_data_len,
		      "%s last_group_data_len mismatch", label);
	zassert_equal(expected.last_anon_data_len, actual.last_anon_data_len,
		      "%s last_anon_data_len mismatch", label);
	zassert_equal(expected.last_peer_path_len, actual.last_peer_path_len,
		      "%s last_peer_path_len mismatch", label);
	zassert_equal(expected.last_peer_path_extra_type,
		      actual.last_peer_path_extra_type,
		      "%s last_peer_path_extra_type mismatch", label);
	zassert_equal(expected.last_peer_path_extra_len,
		      actual.last_peer_path_extra_len,
		      "%s last_peer_path_extra_len mismatch", label);
	zassert_equal(expected.last_trace_tag, actual.last_trace_tag,
		      "%s last_trace_tag mismatch", label);
	zassert_equal(expected.last_trace_auth_code,
		      actual.last_trace_auth_code,
		      "%s last_trace_auth_code mismatch", label);
	zassert_equal(expected.last_trace_flags, actual.last_trace_flags,
		      "%s last_trace_flags mismatch", label);
	zassert_equal(expected.radio_packets_recv, actual.radio_packets_recv,
		      "%s radio_packets_recv mismatch", label);
	zassert_equal(expected.radio_packets_sent, actual.radio_packets_sent,
		      "%s radio_packets_sent mismatch", label);
	zassert_equal(expected.noise_floor_calibrate_count,
		      actual.noise_floor_calibrate_count,
		      "%s noise_floor_calibrate_count mismatch", label);
	zassert_equal(expected.agc_reset_count, actual.agc_reset_count,
		      "%s agc_reset_count mismatch", label);
	zassert_equal(expected.on_send_finished_count,
		      actual.on_send_finished_count,
		      "%s on_send_finished_count mismatch", label);
	zassert_equal(expected.last_send_len, actual.last_send_len,
		      "%s last_send_len mismatch", label);
	zassert_equal(expected.first_outbound_len, actual.first_outbound_len,
		      "%s first_outbound_len mismatch", label);
	if (expected.first_outbound_len > 0U || actual.first_outbound_len > 0U) {
		zassert_mem_equal(expected.first_outbound_raw, actual.first_outbound_raw,
				  expected.first_outbound_len,
				  "%s first_outbound_raw mismatch", label);
	}
}

} // namespace meshcore_dispatcher_mesh_tdd
