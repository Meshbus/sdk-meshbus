// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "test_target_env.h"

#include <string.h>

namespace meshcore_mesh_tdd {

struct TargetEnvStorage {
	bool used;
	struct meshcore_packet_queue_manager manager;
	struct meshcore_tables tables;
	struct meshcore_mesh mesh;
};

static TargetEnvStorage g_target_env_storage[4];

static TargetEnvStorage &target_env_storage_acquire()
{
	for (size_t i = 0U;
	     i < sizeof(g_target_env_storage) / sizeof(g_target_env_storage[0]);
	     i++) {
		if (!g_target_env_storage[i].used) {
			memset(&g_target_env_storage[i], 0,
			       sizeof(g_target_env_storage[i]));
			g_target_env_storage[i].used = true;
			return g_target_env_storage[i];
		}
	}

	zassert_unreachable("too many concurrent TargetEnv instances");
	g_target_env_storage[0].used = true;
	return g_target_env_storage[0];
}

static void target_env_storage_release(TargetEnvStorage &storage)
{
	memset(&storage, 0, sizeof(storage));
}

static void copy_mesh_script_to_hal(const MeshScript &src)
{
	meshcore_hal_test_mesh_script_t *dest =
		meshcore_hal_test_mesh_script_get();

	memset(dest, 0, sizeof(*dest));
	dest->override_filter_recv_flood_packet =
		src.override_filter_recv_flood_packet;
	dest->filter_recv_flood_packet_value = src.filter_recv_flood_packet_value;
	dest->override_allow_packet_forward = src.override_allow_packet_forward;
	dest->allow_packet_forward_value = src.allow_packet_forward_value;
	dest->override_get_retransmit_delay = src.override_get_retransmit_delay;
	dest->get_retransmit_delay_value = src.get_retransmit_delay_value;
	dest->override_get_direct_retransmit_delay =
		src.override_get_direct_retransmit_delay;
	dest->get_direct_retransmit_delay_value =
		src.get_direct_retransmit_delay_value;
	dest->override_get_extra_ack_transmit_count =
		src.override_get_extra_ack_transmit_count;
	dest->get_extra_ack_transmit_count_value =
		src.get_extra_ack_transmit_count_value;
	dest->override_get_cad_fail_retry_delay =
		src.override_get_cad_fail_retry_delay;
	dest->get_cad_fail_retry_delay_value =
		src.get_cad_fail_retry_delay_value;
	dest->override_search_peers_by_hash = src.override_search_peers_by_hash;
	dest->search_peers_by_hash_value = src.search_peers_by_hash_value;
	dest->override_search_channels_by_hash =
		src.override_search_channels_by_hash;
	dest->search_channels_by_hash_value = src.search_channels_by_hash_value;
	dest->override_get_peer_shared_secret =
		src.override_get_peer_shared_secret;
	memcpy(dest->peer_shared_secret, src.peer_shared_secret,
	       sizeof(dest->peer_shared_secret));
	dest->override_search_channels_fill = src.override_search_channels_fill;
	memcpy(dest->search_channel_hash, src.search_channel_hash,
	       sizeof(dest->search_channel_hash));
	memcpy(dest->search_channel_secret, src.search_channel_secret,
	       sizeof(dest->search_channel_secret));
	dest->override_on_peer_path_recv = src.override_on_peer_path_recv;
	dest->on_peer_path_recv_value = src.on_peer_path_recv_value;
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

TargetEnv::TargetEnv()
	: storage(target_env_storage_acquire()), script(default_script()),
	  manager(storage.manager), tables(storage.tables), mesh(storage.mesh)
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
	target_env_storage_release(storage);
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

MeshPolicySnapshot capture_target_snapshot(TargetEnv &env,
					   struct meshcore_packet *packet,
					   const uint8_t *hash)
{
	MeshPolicySnapshot snapshot = {};
	struct meshcore_group_channel channels[4] = {};
	uint8_t secret[MESHCORE_PUBLIC_KEY_SIZE] = {};
	meshcore_common_peer_identity_t peer = {};
	size_t cursor = 0U;
	size_t slot_id = 0U;

	env.sync_script_to_hal();
	snapshot.filter_recv_flood_packet =
		meshcore_mesh_runtime_filter_recv_flood_packet(&env.mesh, packet);
	snapshot.allow_packet_forward =
		meshcore_mesh_runtime_allow_packet_forward(&env.mesh, packet);
	snapshot.retransmit_delay =
		meshcore_mesh_runtime_get_retransmit_delay(&env.mesh, packet);
	snapshot.direct_retransmit_delay =
		meshcore_mesh_runtime_get_direct_retransmit_delay(&env.mesh, packet);
	snapshot.extra_ack_transmit_count =
		meshcore_mesh_runtime_get_extra_ack_transmit_count(&env.mesh);
	snapshot.cad_fail_retry_delay =
		meshcore_mesh_runtime_get_cad_fail_retry_delay(&env.mesh);
	while (meshcore_mesh_runtime_next_peer_shared_secret_by_hash(
		       &env.mesh, hash, cursor, &slot_id, secret, &peer) == 0) {
		cursor = slot_id + 1U;
		snapshot.search_peers_by_hash++;
	}
	snapshot.search_channels_by_hash =
		meshcore_mesh_runtime_search_channels_by_hash(&env.mesh, hash,
							      channels, 4);
	env.sync_script_from_hal();
	return snapshot;
}

} // namespace meshcore_mesh_tdd
