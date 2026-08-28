// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "test_support.h"

#include <string.h>

#include "test_reference_env.h"
#include "test_target_env.h"

namespace meshcore_mesh_tdd {

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

void init_reference_packet(mesh::Packet *packet, uint8_t route_type, uint8_t seed,
			   uint8_t payload_len)
{
	*packet = mesh::Packet();
	packet->header = (PAYLOAD_VER_1 << PH_VER_SHIFT) |
			 (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT) | route_type;
	packet->path_len = 0U;
	packet->payload_len = payload_len;
	for (uint8_t i = 0; i < payload_len; i++) {
		packet->payload[i] = (uint8_t)(seed + i);
	}
}

void init_target_packet(struct meshcore_packet *packet, uint8_t route_type,
			uint8_t seed, uint8_t payload_len)
{
	memset(packet, 0, sizeof(*packet));
	packet->header = (PAYLOAD_VER_1 << PH_VER_SHIFT) |
			 (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT) | route_type;
	packet->path_len = 0U;
	packet->payload_len = payload_len;
	for (uint8_t i = 0; i < payload_len; i++) {
		packet->payload[i] = (uint8_t)(seed + i);
	}
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

void clone_target_packet_from_reference(
	struct meshcore_packet *target, const mesh::Packet *reference)
{
	uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t raw_len = reference->writeTo(raw);

	memset(target, 0, sizeof(*target));
	zassert_true(meshcore_packet_read_from(target, raw, raw_len),
		     "target packet parse from reference raw failed");
}

void expect_packet_state_match(const mesh::Packet *expected,
			       const struct meshcore_packet *actual,
			       const char *label)
{
	uint8_t expected_path_len = expected->getPathByteLen();
	uint8_t actual_path_len = meshcore_packet_get_path_byte_len(actual);

	zassert_true(expected->header == actual->header,
		     "%s header mismatch exp=%u act=%u", label, expected->header,
		     actual->header);
	if (expected->header != actual->header) {
		return;
	}
	zassert_equal(expected->path_len, actual->path_len, "%s path_len mismatch",
		      label);
	if (expected->path_len != actual->path_len) {
		return;
	}
	zassert_equal(expected->payload_len, actual->payload_len,
		      "%s payload_len mismatch", label);
	if (expected->payload_len != actual->payload_len) {
		return;
	}
	zassert_equal(expected_path_len, actual_path_len, "%s path byte len mismatch",
		      label);
	if (expected_path_len != actual_path_len) {
		return;
	}
	if (expected_path_len > 0U) {
		zassert_mem_equal(expected->path, actual->path, expected_path_len,
				  "%s path mismatch", label);
	}
	if (expected->payload_len > 0U) {
		zassert_mem_equal(expected->payload, actual->payload,
				  expected->payload_len, "%s payload mismatch", label);
	}
}

void expect_created_packet_raw_match(const mesh::Packet *expected_packet,
				     const struct meshcore_packet *actual_packet,
				     const char *label)
{
	uint8_t expected_raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t actual_raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t expected_len;
	uint8_t actual_len;

	zassert_not_null(expected_packet, "%s expected packet null", label);
	zassert_not_null(actual_packet, "%s actual packet null", label);
	if (expected_packet == nullptr || actual_packet == NULL) {
		return;
	}

	expected_len = expected_packet->writeTo(expected_raw);
	actual_len = meshcore_packet_write_to(actual_packet, actual_raw);
	zassert_equal(expected_len, actual_len, "%s raw len mismatch", label);
	zassert_mem_equal(expected_raw, actual_raw, expected_len,
			  "%s raw bytes mismatch", label);
}

void expect_create_result_match_and_free(
	ReferenceEnv &expected, mesh::Packet *expected_packet, TargetEnv &actual,
	struct meshcore_packet *actual_packet, const char *label)
{
	bool expected_null = (expected_packet == nullptr);
	bool actual_null = (actual_packet == NULL);
	bool nullability_match = (expected_null == actual_null);

	if (!nullability_match) {
		if (expected_packet != nullptr) {
			expected.manager.free(expected_packet);
		}
		if (actual_packet != NULL) {
			meshcore_packet_queue_manager_free(&actual.manager, actual_packet);
		}
		zassert_equal(expected_null, actual_null, "%s nullability mismatch",
			      label);
		return;
	}

	if (expected_null) {
		return;
	}

	uint8_t expected_raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t actual_raw[MESHCORE_MAX_TRANS_UNIT_LEN] = { 0 };
	uint8_t expected_len = expected_packet->writeTo(expected_raw);
	uint8_t actual_len = meshcore_packet_write_to(actual_packet, actual_raw);

	expected.manager.free(expected_packet);
	meshcore_packet_queue_manager_free(&actual.manager, actual_packet);

	zassert_equal(expected_len, actual_len, "%s raw len mismatch", label);
	if (expected_len != actual_len) {
		return;
	}
	zassert_mem_equal(expected_raw, actual_raw, expected_len,
			  "%s raw bytes mismatch", label);
}

void expect_snapshot_match(const MeshPolicySnapshot &expected,
			   const MeshPolicySnapshot &actual,
			   const char *label)
{
	zassert_equal(expected.filter_recv_flood_packet, actual.filter_recv_flood_packet,
		      "%s filter_recv_flood_packet mismatch", label);
	zassert_equal(expected.allow_packet_forward, actual.allow_packet_forward,
		      "%s allow_packet_forward mismatch", label);
	zassert_equal(expected.retransmit_delay, actual.retransmit_delay,
		      "%s retransmit_delay mismatch", label);
	zassert_equal(expected.direct_retransmit_delay,
		      actual.direct_retransmit_delay,
		      "%s direct_retransmit_delay mismatch", label);
	zassert_equal(expected.extra_ack_transmit_count,
		      actual.extra_ack_transmit_count,
		      "%s extra_ack_transmit_count mismatch", label);
	zassert_equal(expected.cad_fail_retry_delay, actual.cad_fail_retry_delay,
		      "%s cad_fail_retry_delay mismatch", label);
	zassert_equal(expected.search_peers_by_hash, actual.search_peers_by_hash,
		      "%s search_peers_by_hash mismatch", label);
	zassert_equal(expected.search_channels_by_hash, actual.search_channels_by_hash,
		      "%s search_channels_by_hash mismatch", label);
}

void expect_script_observation_match(const MeshScript &expected,
				     const MeshScript &actual,
				     const char *label)
{
	zassert_equal(expected.on_peer_data_recv_count, actual.on_peer_data_recv_count,
		      "%s on_peer_data_recv_count mismatch", label);
	zassert_equal(expected.on_trace_recv_count, actual.on_trace_recv_count,
		      "%s on_trace_recv_count mismatch", label);
	zassert_equal(expected.on_peer_path_recv_count, actual.on_peer_path_recv_count,
		      "%s on_peer_path_recv_count mismatch", label);
	zassert_equal(expected.on_advert_recv_count, actual.on_advert_recv_count,
		      "%s on_advert_recv_count mismatch", label);
	zassert_equal(expected.on_anon_data_recv_count, actual.on_anon_data_recv_count,
		      "%s on_anon_data_recv_count mismatch", label);
	zassert_equal(expected.on_control_data_recv_count,
		      actual.on_control_data_recv_count,
		      "%s on_control_data_recv_count mismatch", label);
	zassert_equal(expected.on_raw_data_recv_count, actual.on_raw_data_recv_count,
		      "%s on_raw_data_recv_count mismatch", label);
	zassert_equal(expected.on_group_data_recv_count, actual.on_group_data_recv_count,
		      "%s on_group_data_recv_count mismatch exp=%u act=%u", label,
		      expected.on_group_data_recv_count,
		      actual.on_group_data_recv_count);
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
	zassert_equal(expected.last_trace_auth_code, actual.last_trace_auth_code,
		      "%s last_trace_auth_code mismatch", label);
	zassert_equal(expected.last_trace_flags, actual.last_trace_flags,
		      "%s last_trace_flags mismatch", label);
}

} // namespace meshcore_mesh_tdd
