// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/meshbus/meshcore.h>
#include <zephyr/meshbus/contact.h>
#include <zephyr/meshbus/radio.h>
#include <zephyr/meshbus/time.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#if defined(CONFIG_MESHBUS_MESHCORE_CLIENT)
#include <zephyr/meshbus/channel.h>
#include <zephyr/meshbus/message.h>
#endif

#include "meshcore/platform.h"
#include "meshcore/types.h"
#include "meshcore_identity.h"
#include "meshcore_rng.h"

#include "meshcore_prvi.h"

#ifndef CONFIG_MESHBUS_MESHCORE_LOG_LEVEL
#define CONFIG_MESHBUS_MESHCORE_LOG_LEVEL LOG_LEVEL_INF
#endif

LOG_MODULE_REGISTER(meshbus_meshcore_host, CONFIG_MESHBUS_MESHCORE_LOG_LEVEL);

#define MESHCORE_COMMON_ADV_LATLON_MASK 0x10U
#define MESHCORE_COMMON_ADV_NAME_MASK 0x80U

BUILD_ASSERT(MESHBUS_MESHCORE_ANON_DATA_RECEIVED_MAX_LEN >=
		     ROUND_UP(MESHBUS_MESHCORE_ANON_DATA_PAYLOAD_MAX_LEN, 16U),
	     "anonymous receive buffer must hold AES zero padding");

#define MESHCORE_COMMON_TXT_TYPE_PLAIN 0U
#define MESHCORE_COMMON_TXT_TYPE_SIGNED_PLAIN 2U

struct meshcore_common_advert_info {
	bool valid;
	char name[MESHCORE_NODE_NAME_MAX_LEN];
	meshcore_common_node_role_t role;
	bool has_position;
	int32_t latitude;
	int32_t longitude;
};

static unsigned long meshbus_meshcore_host_now_seconds(void);
static void meshbus_meshcore_host_copy_cstr(char *dest, size_t dest_size,
					     const char *src);
#if defined(CONFIG_MESHBUS_CONTACT)
static void meshbus_meshcore_host_contact_to_identity(
	const meshbus_contact *contact, meshcore_common_peer_identity_t *out);
static bool meshbus_meshcore_host_contact_from_identity(
	const meshcore_common_peer_identity_t *identity, meshbus_contact *out);
#endif
static uint8_t meshbus_meshcore_host_normalize_path_hash_size(uint8_t hash_size);
static bool meshbus_meshcore_host_config_sync(void);
#if defined(CONFIG_MESHBUS_CONTACT)
static bool meshbus_meshcore_host_local_identity_sync(void);
static bool meshbus_meshcore_host_calc_contact_shared_secret(
	uint8_t secret[MESHCORE_PUBLIC_KEY_SIZE], const meshbus_contact *contact);
static void meshbus_meshcore_host_update_contact_seen(meshbus_contact *contact, bool has_snr,
						    int8_t snr_q4);
#endif
static bool meshbus_meshcore_host_client_repeat_platform_allowed(void);
#if defined(CONFIG_MESHBUS_CONTACT)
static bool meshbus_meshcore_host_path_len_to_bytes(uint8_t path_len_field,
						     uint8_t *path_bytes,
						     uint8_t *hash_size);
static bool meshbus_meshcore_host_extract_return_path_snrs(
	const meshcore_common_packet_view_t *packet, uint8_t *snrs,
	uint8_t *hop_count);
static void meshbus_meshcore_host_append_snr_with_trunc(uint8_t *snrs, uint8_t *len,
						 uint8_t capacity,
						 int8_t snr_q4);
static bool meshbus_meshcore_host_parse_advert(const uint8_t *app_data,
						size_t app_data_len,
						struct meshcore_common_advert_info *out);
#endif
static uint8_t meshbus_meshcore_host_copy_trace_snrs(int8_t *dst, size_t dst_count,
						     const int8_t *src, uint8_t src_count);

static int meshbus_meshcore_host_node_identity_get(
	meshcore_common_node_identity_t *out);
static int meshbus_meshcore_host_config_last_modify_get(
	uint32_t *out_timestamp);
static int meshbus_meshcore_host_node_runtime_policy_get(
	meshcore_common_node_runtime_policy_t *out);
static int meshbus_meshcore_host_node_advert_profile_get(
	meshcore_common_node_advert_profile_t *out);
static int meshbus_meshcore_host_contact_path_get_by_key(
	const uint8_t *public_key, meshcore_common_peer_path_t *out);
static int meshbus_meshcore_host_contact_seen_update(const uint8_t *public_key,
					   bool has_snr, int8_t snr_q4);
static int meshbus_meshcore_host_channel_secret_match_exists(uint8_t channel_hash,
						      const uint8_t *secret,
						      size_t secret_len);

static void meshbus_meshcore_host_dispatcher_log_rx_raw(float snr, float rssi,
						 const uint8_t raw[], int len);
static void meshbus_meshcore_host_dispatcher_log_rx(
	const meshcore_common_packet_view_t *packet, int len, float score);
static void meshbus_meshcore_host_dispatcher_log_tx(
	const meshcore_common_packet_view_t *packet, int len);
static void meshbus_meshcore_host_dispatcher_log_tx_fail(
	const meshcore_common_packet_view_t *packet, int len);
static void meshbus_meshcore_host_runtime_request_error(uint8_t request_type,
						 int err_code);
static float meshbus_meshcore_host_dispatcher_get_airtime_budget_factor(void);
static int meshbus_meshcore_host_dispatcher_calc_rx_delay(float score,
						   uint32_t air_time);
static uint32_t meshbus_meshcore_host_dispatcher_get_cad_fail_max_duration(void);
static int meshbus_meshcore_host_dispatcher_get_interference_threshold(void);
static int meshbus_meshcore_host_dispatcher_get_agc_reset_interval(void);
static unsigned long meshbus_meshcore_host_dispatcher_get_duty_cycle_window_ms(void);

static int meshbus_meshcore_host_channel_secret_hash(const uint8_t *secret,
					      size_t secret_len,
					      uint8_t *out_hash);

static uint32_t meshbus_meshcore_host_mesh_get_cad_fail_retry_delay(void);
static bool meshbus_meshcore_host_mesh_filter_recv_flood_packet(
	const meshcore_common_packet_view_t *packet);
static bool meshbus_meshcore_host_mesh_allow_packet_forward(
	const meshcore_common_packet_view_t *packet);
static uint32_t meshbus_meshcore_host_mesh_get_retransmit_delay(
	const meshcore_common_packet_view_t *packet);
static uint32_t meshbus_meshcore_host_mesh_get_direct_retransmit_delay(
	const meshcore_common_packet_view_t *packet);
static uint8_t meshbus_meshcore_host_mesh_get_extra_ack_transmit_count(void);
static int meshbus_meshcore_host_mesh_next_peer_shared_secret_by_hash(
	const uint8_t *hash, size_t start_slot, size_t *slot_id,
	uint8_t *dest_secret, meshcore_common_peer_identity_t *peer_identity);
static int meshbus_meshcore_host_mesh_search_channels_by_hash(
	const uint8_t *hash, meshcore_common_channel_view_t *channels,
	int max_matches);
static void meshbus_meshcore_host_mesh_on_peer_data_recv(
	const meshcore_common_packet_view_t *packet, uint8_t type,
	const meshcore_common_peer_identity_t *sender, const uint8_t *secret,
	uint8_t *data, size_t len);
static void meshbus_meshcore_host_mesh_on_trace_recv(
	const meshcore_common_packet_view_t *packet, uint32_t tag,
	uint32_t auth_code, uint8_t flags, const uint8_t *path_snrs,
	const uint8_t *path_hashes, uint8_t path_len);
static bool meshbus_meshcore_host_mesh_on_peer_path_recv(
	const meshcore_common_packet_view_t *packet,
	const meshcore_common_peer_identity_t *sender, const uint8_t *secret,
	uint8_t *path, uint8_t path_len, uint8_t extra_type, uint8_t *extra,
	uint8_t extra_len);
static void meshbus_meshcore_host_mesh_on_advert_recv(
	const meshcore_common_packet_view_t *packet,
	const meshcore_common_identity_view_t *identity, uint32_t timestamp,
	const uint8_t *app_data, size_t app_data_len);
static void meshbus_meshcore_host_mesh_on_anon_data_recv(
	const meshcore_common_packet_view_t *packet, const uint8_t *secret,
	const meshcore_common_identity_view_t *sender, uint8_t *data,
	size_t len);
static void meshbus_meshcore_host_mesh_on_path_recv(
	const meshcore_common_packet_view_t *packet,
	const meshcore_common_identity_view_t *sender, uint8_t *path,
	uint8_t path_len, uint8_t extra_type, uint8_t *extra,
	uint8_t extra_len);
static void meshbus_meshcore_host_mesh_on_control_data_recv(
	const meshcore_common_packet_view_t *packet);
static void meshbus_meshcore_host_mesh_on_raw_data_recv(
	const meshcore_common_packet_view_t *packet);
static void meshbus_meshcore_host_mesh_on_group_data_recv(
	const meshcore_common_packet_view_t *packet, uint8_t type,
	const meshcore_common_channel_view_t *channel, uint8_t *data,
	size_t len);
static void meshbus_meshcore_host_mesh_on_ack_recv(
	const meshcore_common_packet_view_t *packet, uint32_t ack_crc);

static int meshbus_meshcore_host_message_handler(
	const meshcore_common_message_t *message);
static int meshbus_meshcore_host_message_ack_handler(const uint8_t *target,
					      uint8_t attempt);
static int meshbus_meshcore_host_advert_handler(
	const meshcore_common_advert_event_t *advert);
static int meshbus_meshcore_host_peer_path_publish(
	const meshcore_common_peer_path_event_t *peer_path, bool is_discover);
static int meshbus_meshcore_host_peer_path_handler(
	const meshcore_common_peer_path_event_t *peer_path);
static int meshbus_meshcore_host_trace_path_handler(uint8_t state, uint32_t tag,
						    const int8_t *out_path_snr,
						    uint8_t out_count,
						    const int8_t *return_path_snr,
						    uint8_t return_count,
						    bool has_response_snr,
						    int8_t response_snr,
						    uint32_t timestamp);
static int meshbus_meshcore_host_telemetry_handler(const uint8_t *key_prefix,
					    uint32_t timestamp,
					    uint32_t tag,
					    const uint8_t *payload,
					    size_t payload_len);
static int meshbus_meshcore_host_binary_request_handler(
	const meshcore_common_binary_request_event_t *event);
static int meshbus_meshcore_host_binary_response_handler(
	const uint8_t *key_prefix, uint32_t timestamp, uint32_t tag,
	const uint8_t *payload, size_t payload_len);
static int meshbus_meshcore_host_node_discover_handler(
	const meshcore_common_node_discover_event_t *event);
static int meshbus_meshcore_host_channel_data_handler(
	const meshcore_common_channel_data_event_t *event);
static int meshbus_meshcore_host_raw_data_handler(
	const meshcore_common_raw_data_event_t *event);
static int meshbus_meshcore_host_control_data_handler(
	const meshcore_common_control_data_event_t *event);

static bool meshbus_meshcore_host_radio_frequency_get(uint64_t *freq_hz);

/* MeshCore config cache */

static meshbus_meshcore_config meshcore_config_cache =
	meshbus_MeshcoreConfig_init_zero;
static uint32_t meshcore_config_last_modify_s;
static struct meshcore_local_identity local_identity_cache;
static bool local_identity_valid;

static meshbus_meshcore_role meshbus_meshcore_host_local_role(void)
{
	return meshbus_meshcore_firmware_role_get();
}

struct meshcore_common_freq_range_hz {
	uint64_t lower_hz;
	uint64_t upper_hz;
};

static const struct meshcore_common_freq_range_hz
	client_repeat_allowed_freq_ranges_hz[] = {
		{ 433000000ULL, 433000000ULL },
		{ 869000000ULL, 869000000ULL },
		{ 918000000ULL, 918000000ULL },
	};

static bool meshbus_meshcore_host_radio_frequency_get(uint64_t *freq_hz)
{
	meshbus_radio_config radio_cfg = meshbus_RadioConfig_init_zero;

	if (freq_hz == NULL) {
		return false;
	}
	if (meshbus_radio_config_get(&radio_cfg) != 0) {
		return false;
	}

	*freq_hz = radio_cfg.frequency;
	return true;
}

static unsigned long meshbus_meshcore_host_now_seconds(void)
{
	uint32_t now;

	if (meshbus_time_timestamp_s_get(&now) != 0 || now == 0U) {
		return 1UL;
	}

	return (unsigned long)now;
}

static meshcore_common_node_role_t meshbus_meshcore_host_meshcore_role_to_common(uint8_t role)
{
	switch (role) {
	case MESHBUS_MESHCORE_ROLE_CHAT:
		return MESHCORE_COMMON_NODE_ROLE_CHAT;
	case MESHBUS_MESHCORE_ROLE_REPEATER:
		return MESHCORE_COMMON_NODE_ROLE_REPEATER;
	case MESHBUS_MESHCORE_ROLE_ROOM:
		return MESHCORE_COMMON_NODE_ROLE_ROOM;
	case MESHBUS_MESHCORE_ROLE_SENSOR:
		return MESHCORE_COMMON_NODE_ROLE_SENSOR;
	default:
		return MESHCORE_COMMON_NODE_ROLE_NONE;
	}
}

#if defined(CONFIG_MESHBUS_CONTACT)
static meshcore_common_node_role_t
meshbus_meshcore_host_contact_role_to_common(meshbus_contact_role role)
{
	switch (role) {
	case MESHBUS_CONTACT_ROLE_CHAT:
		return MESHCORE_COMMON_NODE_ROLE_CHAT;
	case MESHBUS_CONTACT_ROLE_REPEATER:
		return MESHCORE_COMMON_NODE_ROLE_REPEATER;
	case MESHBUS_CONTACT_ROLE_ROOM:
		return MESHCORE_COMMON_NODE_ROLE_ROOM;
	case MESHBUS_CONTACT_ROLE_SENSOR:
		return MESHCORE_COMMON_NODE_ROLE_SENSOR;
	default:
		return MESHCORE_COMMON_NODE_ROLE_NONE;
	}
}

static bool meshbus_meshcore_host_common_role_to_contact(meshcore_common_node_role_t role,
							meshbus_contact_role *out)
{
	if (out == NULL) {
		return false;
	}

	switch (role) {
	case MESHCORE_COMMON_NODE_ROLE_CHAT:
		*out = MESHBUS_CONTACT_ROLE_CHAT;
		return true;
	case MESHCORE_COMMON_NODE_ROLE_REPEATER:
		*out = MESHBUS_CONTACT_ROLE_REPEATER;
		return true;
	case MESHCORE_COMMON_NODE_ROLE_ROOM:
		*out = MESHBUS_CONTACT_ROLE_ROOM;
		return true;
	case MESHCORE_COMMON_NODE_ROLE_SENSOR:
		*out = MESHBUS_CONTACT_ROLE_SENSOR;
		return true;
	default:
		return false;
	}
}
#endif

static meshcore_common_loop_detect_mode_t
meshbus_meshcore_host_loop_detect_from_meshbus(uint8_t loop_detect)
{
	switch (loop_detect) {
	case MESHBUS_MESHCORE_LOOP_DETECT_MINIMAL:
		return MESHCORE_COMMON_LOOP_DETECT_MINIMAL;
	case MESHBUS_MESHCORE_LOOP_DETECT_MODERATE:
		return MESHCORE_COMMON_LOOP_DETECT_MODERATE;
	case MESHBUS_MESHCORE_LOOP_DETECT_STRICT:
		return MESHCORE_COMMON_LOOP_DETECT_STRICT;
	default:
		return MESHCORE_COMMON_LOOP_DETECT_OFF;
	}
}

#if defined(CONFIG_MESHBUS_CONTACT)
static meshcore_common_node_role_t meshbus_meshcore_host_role_from_advert_type(uint8_t type)
{
	switch (type & 0x0FU) {
	case 1U:
		return MESHCORE_COMMON_NODE_ROLE_CHAT;
	case 2U:
		return MESHCORE_COMMON_NODE_ROLE_REPEATER;
	case 3U:
		return MESHCORE_COMMON_NODE_ROLE_ROOM;
	case 4U:
		return MESHCORE_COMMON_NODE_ROLE_SENSOR;
	default:
		return MESHCORE_COMMON_NODE_ROLE_NONE;
	}
}
#endif

static void meshbus_meshcore_host_copy_cstr(char *dest, size_t dest_size, const char *src)
{
	if (dest == NULL || dest_size == 0U) {
		return;
	}

	if (src == NULL) {
		dest[0] = '\0';
		return;
	}

	(void)snprintf(dest, dest_size, "%s", src);
}

#if defined(CONFIG_MESHBUS_CONTACT)
static void meshbus_meshcore_host_contact_to_identity(const meshbus_contact *contact,
						     meshcore_common_peer_identity_t *out)
{
	if (contact == NULL || out == NULL) {
		return;
	}

	memset(out, 0, sizeof(*out));
	meshbus_meshcore_host_copy_cstr(out->name, sizeof(out->name), contact->name);
	out->role = meshbus_meshcore_host_contact_role_to_common(contact->role);
	out->flags = contact->flags;
	if (contact->public_key.size == MESHCORE_PUBLIC_KEY_SIZE) {
		memcpy(out->public_key, contact->public_key.bytes, sizeof(out->public_key));
	}
}

static bool meshbus_meshcore_host_contact_from_identity(const meshcore_common_peer_identity_t *identity,
					     meshbus_contact *out)
{
	meshbus_contact_role role;

	if (identity == NULL || out == NULL ||
	    !meshbus_meshcore_host_common_role_to_contact(identity->role, &role)) {
		return false;
	}

	*out = (meshbus_contact)meshbus_Contact_init_zero;
	meshbus_meshcore_host_copy_cstr(out->name, sizeof(out->name), identity->name);
	out->role = role;
	out->flags = identity->flags;
	out->public_key.size = MESHCORE_PUBLIC_KEY_SIZE;
	memcpy(out->public_key.bytes, identity->public_key, sizeof(identity->public_key));
	out->path_hash_size = 1U;
	return true;
}
#endif

static uint8_t meshbus_meshcore_host_normalize_path_hash_size(uint8_t hash_size)
{
	if (hash_size == 0U || hash_size > MESHBUS_MESHCORE_PATH_HASH_SIZE_MAX) {
		return 1U;
	}

	return hash_size;
}

#if defined(CONFIG_MESHBUS_CONTACT)
static uint8_t meshbus_meshcore_host_encode_path_len(uint8_t path_bytes, uint8_t hash_size)
{
	uint8_t normalized_hash_size = meshbus_meshcore_host_normalize_path_hash_size(hash_size);
	uint8_t hops;

	if (path_bytes == 0U) {
		return (uint8_t)((normalized_hash_size - 1U) << 6);
	}
	if ((path_bytes % normalized_hash_size) != 0U) {
		return 0U;
	}

	hops = path_bytes / normalized_hash_size;
	if (hops > 0x3fU) {
		return 0U;
	}

	return (uint8_t)(((normalized_hash_size - 1U) << 6) | hops);
}
#endif

static bool meshbus_meshcore_host_config_sync(void)
{
	meshbus_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	unsigned long now_s = meshbus_meshcore_host_now_seconds();
	bool changed;

	if (meshbus_meshcore_config_get(&cfg) != 0) {
		return false;
	}

	if (meshcore_config_last_modify_s == 0U) {
		meshcore_config_last_modify_s = (uint32_t)now_s;
	}

	changed = memcmp(&cfg, &meshcore_config_cache, sizeof(cfg)) != 0;
	meshcore_config_cache = cfg;
	if (changed) {
		if ((uint32_t)now_s > meshcore_config_last_modify_s) {
			meshcore_config_last_modify_s = (uint32_t)now_s;
		} else {
			meshcore_config_last_modify_s++;
			if (meshcore_config_last_modify_s == 0U) {
				meshcore_config_last_modify_s = 1U;
			}
		}
	}

	local_identity_valid =
		(meshcore_config_cache.public_key.size == MESHCORE_PUBLIC_KEY_SIZE) &&
		(meshcore_config_cache.private_key.size == MESHCORE_PRIVATE_KEY_SIZE);
	if (local_identity_valid) {
		meshcore_local_identity_init_from_bytes(
			&local_identity_cache, meshcore_config_cache.private_key.bytes,
			meshcore_config_cache.public_key.bytes);
	} else {
		meshcore_local_identity_init(&local_identity_cache);
	}

	return changed;
}

#if defined(CONFIG_MESHBUS_CONTACT)
static bool meshbus_meshcore_host_local_identity_sync(void)
{
	(void)meshbus_meshcore_host_config_sync();
	return local_identity_valid;
}

static bool meshbus_meshcore_host_calc_contact_shared_secret(
	uint8_t secret[MESHCORE_PUBLIC_KEY_SIZE], const meshbus_contact *contact)
{
	if (secret == NULL || contact == NULL ||
	    contact->public_key.size != MESHCORE_PUBLIC_KEY_SIZE ||
	    !meshbus_meshcore_host_local_identity_sync()) {
		return false;
	}

	meshcore_local_identity_calc_shared_secret(&local_identity_cache, secret,
						   contact->public_key.bytes);
	return true;
}

static void meshbus_meshcore_host_update_contact_seen(meshbus_contact *contact, bool has_snr,
					     int8_t snr_q4)
{
	uint32_t now;

	if (contact == NULL) {
		return;
	}

	now = (uint32_t)meshbus_meshcore_host_now_seconds();
	if (contact->first_seen_timestamp == 0U) {
		contact->first_seen_timestamp = now;
	}
	contact->last_seen_timestamp = now;
	if (has_snr) {
		contact->last_seen_snr = (int32_t)snr_q4;
	}
}
#endif

static bool meshbus_meshcore_host_client_repeat_freq_allowed(uint64_t freq_hz)
{
	size_t i;

	for (i = 0U; i < ARRAY_SIZE(client_repeat_allowed_freq_ranges_hz); i++) {
		const struct meshcore_common_freq_range_hz *range =
			&client_repeat_allowed_freq_ranges_hz[i];

		if (freq_hz >= range->lower_hz && freq_hz <= range->upper_hz) {
			return true;
		}
	}

	return false;
}

static bool meshbus_meshcore_host_client_repeat_platform_allowed(void)
{
	uint64_t freq_hz = 0U;

	(void)meshbus_meshcore_host_config_sync();
	if (!meshcore_config_cache.client_repeat) {
		return false;
	}

	if (!meshbus_meshcore_host_radio_frequency_get(&freq_hz)) {
		LOG_DBG("client_repeat blocked: frequency unavailable");
		return false;
	}

	if (!meshbus_meshcore_host_client_repeat_freq_allowed(freq_hz)) {
		LOG_DBG("client_repeat blocked: unsupported frequency=%llu",
			(unsigned long long)freq_hz);
		return false;
	}

	return true;
}

#if defined(CONFIG_MESHBUS_CONTACT)
static bool meshbus_meshcore_host_path_len_to_bytes(uint8_t path_len_field,
						      uint8_t *path_bytes,
						      uint8_t *hash_size)
{
	size_t bytes;

	if (path_bytes == NULL || hash_size == NULL) {
		return false;
	}
	if (!meshbus_meshcore_path_len_to_bytes(path_len_field, &bytes,
						hash_size) ||
	    bytes > UINT8_MAX) {
		return false;
	}

	*path_bytes = (uint8_t)bytes;
	return true;
}

static bool meshbus_meshcore_host_extract_return_path_snrs(
	const meshcore_common_packet_view_t *packet, uint8_t *snrs,
	uint8_t *hop_count)
{
	uint8_t tuple_size;
	uint8_t hops;
	uint8_t i;

	if (packet == NULL || snrs == NULL || hop_count == NULL ||
	    !packet->has_transport_snr || packet->path == NULL) {
		return false;
	}

	tuple_size = packet->path_hash_size;
	if (tuple_size < 2U || tuple_size == 4U) {
		return false;
	}

	hops = packet->path_hash_count;
	for (i = 0U; i < hops; i++) {
		size_t base = (size_t)i * tuple_size;

		snrs[i] = packet->path[base + tuple_size - 1U];
	}
	*hop_count = hops;
	return true;
}

static void meshbus_meshcore_host_append_snr_with_trunc(uint8_t *snrs, uint8_t *len,
						  uint8_t max_len, int8_t snr_q4)
{
	if (snrs == NULL || len == NULL || *len >= max_len) {
		return;
	}

	snrs[*len] = (uint8_t)snr_q4;
	(*len)++;
}

static bool meshbus_meshcore_host_parse_advert(
	const uint8_t *app_data, size_t app_data_len,
	struct meshcore_common_advert_info *out)
{
	uint8_t flags;
	size_t offset = 1U;
	size_t name_len = 0U;

	if (app_data == NULL || app_data_len == 0U || out == NULL) {
		return false;
	}

	memset(out, 0, sizeof(*out));
	flags = app_data[0];
	out->role = meshbus_meshcore_host_role_from_advert_type(flags);
	if (out->role == MESHCORE_COMMON_NODE_ROLE_NONE) {
		return false;
	}

	if ((flags & MESHCORE_COMMON_ADV_LATLON_MASK) != 0U) {
		if (offset + 8U > app_data_len) {
			return false;
		}
		memcpy(&out->latitude, &app_data[offset], 4U);
		offset += 4U;
		memcpy(&out->longitude, &app_data[offset], 4U);
		offset += 4U;
		out->has_position = true;
	}

	if ((flags & MESHCORE_COMMON_ADV_NAME_MASK) != 0U) {
		name_len = app_data_len - offset;
		if (name_len >= sizeof(out->name)) {
			name_len = sizeof(out->name) - 1U;
		}
		if (name_len > 0U) {
			memcpy(out->name, &app_data[offset], name_len);
			out->name[name_len] = '\0';
		}
	}

	out->valid = true;
	return true;
}
#endif

static int meshbus_meshcore_host_node_identity_get(meshcore_common_node_identity_t *out)
{
	if (out == NULL) {
		return -EINVAL;
	}

	memset(out, 0, sizeof(*out));
	(void)meshbus_meshcore_host_config_sync();
	meshbus_meshcore_host_copy_cstr(out->name, sizeof(out->name), meshcore_config_cache.name);
	out->role = meshbus_meshcore_host_meshcore_role_to_common(meshbus_meshcore_host_local_role());
	if (meshcore_config_cache.public_key.size == MESHCORE_PUBLIC_KEY_SIZE) {
		memcpy(out->public_key, meshcore_config_cache.public_key.bytes,
		       sizeof(out->public_key));
	}
	if (meshcore_config_cache.private_key.size == MESHCORE_PRIVATE_KEY_SIZE) {
		memcpy(out->private_key, meshcore_config_cache.private_key.bytes,
		       sizeof(out->private_key));
	}
	return 0;
}

static int meshbus_meshcore_host_config_last_modify_get(uint32_t *out_timestamp)
{
	if (out_timestamp == NULL) {
		return -EINVAL;
	}

	(void)meshbus_meshcore_host_config_sync();
	*out_timestamp = meshcore_config_last_modify_s;
	return 0;
}

static int meshbus_meshcore_host_node_runtime_policy_get(
	meshcore_common_node_runtime_policy_t *out)
{
	if (out == NULL) {
		return -EINVAL;
	}

	memset(out, 0, sizeof(*out));
	(void)meshbus_meshcore_host_config_sync();
	out->path_hash_size =
		meshbus_meshcore_host_normalize_path_hash_size((uint8_t)meshcore_config_cache.path_hash_size);
	out->loop_detect =
		meshbus_meshcore_host_loop_detect_from_meshbus((uint8_t)meshcore_config_cache.loop_detect);
	out->client_repeat = meshbus_meshcore_host_client_repeat_platform_allowed();
	out->disable_fwd = meshcore_config_cache.disable_fwd;
	out->flood_max = meshcore_config_cache.flood_max;
	out->multi_acks = meshcore_config_cache.multi_acks;
	out->tx_delay_factor = meshcore_config_cache.tx_delay_factor;
	out->direct_tx_delay_factor = meshcore_config_cache.direct_tx_delay_factor;
	return 0;
}

static int meshbus_meshcore_host_node_advert_profile_get(
	meshcore_common_node_advert_profile_t *out)
{
	if (out == NULL) {
		return -EINVAL;
	}

	memset(out, 0, sizeof(*out));
	(void)meshbus_meshcore_host_config_sync();
	out->has_position = meshcore_config_cache.advert_position;
	out->latitude = meshcore_config_cache.latitude;
	out->longitude = meshcore_config_cache.longitude;
	out->advert_interval = meshcore_config_cache.advert_interval;
	out->flood_advert_interval = meshcore_config_cache.flood_advert_interval;
	return 0;
}

/* host/policy.c */

static void meshbus_meshcore_host_dispatcher_log_rx_raw(float snr, float rssi,
					const uint8_t raw[], int len)
{
	(void)snr;
	(void)rssi;
	(void)raw;
	(void)len;
}

static void meshbus_meshcore_host_dispatcher_log_rx(
	const meshcore_common_packet_view_t *packet, int len, float score)
{
	(void)packet;
	(void)len;
	(void)score;
}

static void meshbus_meshcore_host_dispatcher_log_tx(
	const meshcore_common_packet_view_t *packet, int len)
{
	LOG_DBG("MeshCore dispatcher TX: route=%u len=%d",
		packet != NULL ? (unsigned int)packet->route : 0U, len);
}

static void meshbus_meshcore_host_dispatcher_log_tx_fail(
	const meshcore_common_packet_view_t *packet, int len)
{
	LOG_DBG("MeshCore dispatcher TX failed: route=%u len=%d",
		packet != NULL ? (unsigned int)packet->route : 0U, len);
}

static void meshbus_meshcore_host_runtime_request_error(uint8_t request_type, int err_code)
{
	LOG_WRN("runtime request transient failure type=%u err=%d",
		(unsigned int)request_type, err_code);
}

static float meshbus_meshcore_host_dispatcher_get_airtime_budget_factor(void)
{
	return 2.0f;
}

static int meshbus_meshcore_host_dispatcher_calc_rx_delay(float score, uint32_t air_time)
{
	return (int)((powf(10.0f, 0.85f - score) - 1.0f) * (float)air_time);
}

static uint32_t meshbus_meshcore_host_dispatcher_get_cad_fail_max_duration(void)
{
	return 4000U;
}

static int meshbus_meshcore_host_dispatcher_get_interference_threshold(void)
{
	/* mc upstream defines interference_threshold, but never uses it. */
	return 0;
}

static int meshbus_meshcore_host_dispatcher_get_agc_reset_interval(void)
{
	/* Current MeshCore config does not provide agc_reset_interval yet. */
	return 0;
}

static unsigned long meshbus_meshcore_host_dispatcher_get_duty_cycle_window_ms(void)
{
	return 3600000UL;
}

static uint32_t meshbus_meshcore_host_mesh_get_cad_fail_retry_delay(void)
{
	return meshcore_rng_next_int(1U, 4U) * 120U;
}

static bool meshbus_meshcore_host_mesh_filter_recv_flood_packet(
	const meshcore_common_packet_view_t *packet)
{
	(void)packet;
	return false;
}

static bool meshbus_meshcore_host_mesh_allow_packet_forward(
	const meshcore_common_packet_view_t *packet)
{
	(void)packet;
	return false;
}

static uint32_t meshbus_meshcore_host_mesh_get_retransmit_delay(
	const meshcore_common_packet_view_t *packet)
{
	uint32_t t;

	if (packet == NULL) {
		return 0U;
	}

	t = (meshcore_platform_radio_airtime(
		     (size_t)packet->raw_len) *
	     52U / 50U) /
	    2U;
	return meshcore_rng_next_int(0U, 5U) * t;
}

static uint32_t meshbus_meshcore_host_mesh_get_direct_retransmit_delay(
	const meshcore_common_packet_view_t *packet)
{
	(void)packet;
	return 0U;
}

static uint8_t meshbus_meshcore_host_mesh_get_extra_ack_transmit_count(void)
{
	(void)meshbus_meshcore_host_config_sync();
	return meshcore_config_cache.multi_acks;
}

/* host/peers.c */

static int meshbus_meshcore_host_contact_path_get_by_key(const uint8_t *public_key,
					      meshcore_common_peer_path_t *out)
{
#if defined(CONFIG_MESHBUS_CONTACT)
	meshbus_contact contact = meshbus_Contact_init_zero;
	int rc;

	if (public_key == NULL || out == NULL) {
		return -EINVAL;
	}

	rc = meshbus_contact_find_by_key(public_key, &contact);
	if (rc != 0) {
		return rc;
	}

	memset(out, 0, sizeof(*out));
	out->has_out_path =
		contact.out_path.size > 0U || contact.is_neighbor;
	out->out_path_byte_len =
		(uint8_t)MIN((size_t)contact.out_path.size,
			     sizeof(out->out_path));
	if (out->out_path_byte_len > 0U) {
		memcpy(out->out_path, contact.out_path.bytes,
		       out->out_path_byte_len);
	}
	out->path_hash_size =
		meshbus_meshcore_host_normalize_path_hash_size((uint8_t)contact.path_hash_size);
	return 0;
#else
	ARG_UNUSED(public_key);
	ARG_UNUSED(out);

	return -ENOENT;
#endif
}

static int meshbus_meshcore_host_contact_seen_update(const uint8_t *public_key, bool has_snr,
					  int8_t snr_q4)
{
#if defined(CONFIG_MESHBUS_CONTACT)
	meshbus_contact contact = meshbus_Contact_init_zero;
	uint32_t now = (uint32_t)meshbus_meshcore_host_now_seconds();
	int rc;

	if (public_key == NULL) {
		return -EINVAL;
	}

	rc = meshbus_contact_find_by_key(public_key, &contact);
	if (rc != 0) {
		return rc;
	}

	if (contact.first_seen_timestamp == 0U) {
		contact.first_seen_timestamp = now;
	}
	contact.last_seen_timestamp = now;
	if (has_snr) {
		contact.last_seen_snr = snr_q4;
	}

	return meshbus_contact_set(contact.public_key.bytes, &contact);
#else
	ARG_UNUSED(public_key);
	ARG_UNUSED(has_snr);
	ARG_UNUSED(snr_q4);

	return -ENOENT;
#endif
}

static int meshbus_meshcore_host_mesh_next_peer_shared_secret_by_hash(const uint8_t *hash, size_t start_slot,
							  size_t *slot_id, uint8_t *dest_secret,
							  meshcore_common_peer_identity_t *peer_identity)
{
#if defined(CONFIG_MESHBUS_CONTACT)
	meshbus_contact contact = meshbus_Contact_init_zero;
	size_t cursor = start_slot;
	int rc;

	if (hash == NULL || slot_id == NULL || dest_secret == NULL || peer_identity == NULL) {
		return -EINVAL;
	}

	memset(dest_secret, 0, MESHCORE_PUBLIC_KEY_SIZE);
	memset(peer_identity, 0, sizeof(*peer_identity));
	while ((rc = meshbus_contact_next_by_hash(hash, cursor, slot_id, &contact)) == 0) {
		cursor = *slot_id + 1U;

		if (meshbus_meshcore_host_calc_contact_shared_secret(dest_secret, &contact)) {
			meshbus_meshcore_host_contact_to_identity(&contact, peer_identity);
			return 0;
		}

		memset(dest_secret, 0, MESHCORE_PUBLIC_KEY_SIZE);
		memset(peer_identity, 0, sizeof(*peer_identity));
	}

	return rc;
#else
	ARG_UNUSED(hash);
	ARG_UNUSED(start_slot);
	ARG_UNUSED(slot_id);
	ARG_UNUSED(peer_identity);
	if (dest_secret != NULL) {
		memset(dest_secret, 0, MESHCORE_PUBLIC_KEY_SIZE);
	}

	return -ENOENT;
#endif
}

static int meshbus_meshcore_host_channel_secret_hash(const uint8_t *secret, size_t secret_len,
				     uint8_t *out_hash)
{
	if (secret == NULL || out_hash == NULL) {
		return -EINVAL;
	}
	if (!meshcore_platform_crypto_sha256(out_hash, MESHCORE_CHANNEL_HASH_BYTES, secret,
				 (int)secret_len)) {
		return -EINVAL;
	}

	return 0;
}

/* host/channels.c */

#if defined(CONFIG_MESHBUS_MESHCORE_CLIENT)
static int meshbus_meshcore_host_channel_secret_match_exists(uint8_t channel_hash,
						const uint8_t *secret,
						size_t secret_len)
{
	meshbus_channel channel = meshbus_Channel_init_zero;
	uint8_t hash = channel_hash;
	size_t cursor = 0U;
	size_t slot_id = 0U;
	int rc;

	if (secret == NULL ||
	    (secret_len != MESHCORE_CHANNEL_SECRET_LEN_16 &&
	     secret_len != MESHCORE_CHANNEL_SECRET_LEN_32)) {
		return -EINVAL;
	}

	while ((rc = meshbus_channel_next_by_hash(&hash, cursor, &slot_id, &channel)) == 0) {
		cursor = slot_id + 1U;
		if (channel.secret.size == secret_len &&
		    memcmp(channel.secret.bytes, secret, secret_len) == 0) {
			return 1;
		}
	}

	return rc == -ENOENT ? 0 : rc;
}

static int meshbus_meshcore_host_channel_index_from_hash(const uint8_t *hash, uint8_t *out_index)
{
	meshbus_channel channel = meshbus_Channel_init_zero;
	size_t cursor = 0U;
	size_t slot_id = 0U;
	int rc;

	if (hash == NULL || out_index == NULL) {
		return -EINVAL;
	}

	rc = meshbus_channel_next_by_hash(hash, cursor, &slot_id, &channel);
	if (rc != 0) {
		return rc;
	}
	if (slot_id > UINT8_MAX) {
		return -ERANGE;
	}

	*out_index = (uint8_t)slot_id;
	return 0;
}

static int meshbus_meshcore_host_channel_data_handler(const meshcore_common_channel_data_event_t *event)
{
	struct meshbus_meshcore_channel_data_response_event response = {0};
	int rc;

	if (event == NULL || event->payload_len > sizeof(response.payload)) {
		return -EINVAL;
	}

	rc = meshbus_meshcore_host_channel_index_from_hash(event->channel_hash, &response.channel_index);
	if (rc != 0) {
		return rc;
	}

	response.path_len = event->path_len;
	memcpy(response.path, event->path, sizeof(response.path));
	response.data_type = event->data_type;
	response.payload_len = event->payload_len;
	if (event->payload_len > 0U) {
		memcpy(response.payload, event->payload, event->payload_len);
	}
	response.has_rx_snr = event->has_rx_snr;
	response.rx_snr_q4 = event->rx_snr_q4;

	return zbus_chan_pub(&meshbus_meshcore_channel_data_response_chan,
			     &response, K_NO_WAIT);
}

static int meshbus_meshcore_host_mesh_search_channels_by_hash(
	const uint8_t *hash, meshcore_common_channel_view_t channels[],
	int max_matches)
{
	meshbus_channel channel = meshbus_Channel_init_zero;
	size_t cursor = 0U;
	size_t slot_id = 0U;
	int match_count = 0;
	int rc;

	if (hash == NULL || channels == NULL || max_matches <= 0) {
		return 0;
	}

	while (match_count < max_matches) {
		rc = meshbus_channel_next_by_hash(hash, cursor, &slot_id, &channel);
		if (rc == -ENOENT) {
			break;
		}
		if (rc != 0) {
			return rc;
		}
		cursor = slot_id + 1U;

		size_t copy_len = MIN((size_t)channel.secret.size,
					      sizeof(channels[match_count].secret));

		memset(&channels[match_count], 0, sizeof(channels[match_count]));
		if (copy_len > 0U) {
			memcpy(channels[match_count].secret, channel.secret.bytes, copy_len);
		}
		channels[match_count].hash[0] = channel.hash.bytes[0];
		match_count++;
	}

	return match_count;
}
#else
static int meshbus_meshcore_host_channel_secret_match_exists(uint8_t channel_hash,
						const uint8_t *secret,
						size_t secret_len)
{
	ARG_UNUSED(channel_hash);
	ARG_UNUSED(secret);
	ARG_UNUSED(secret_len);

	return 0;
}

static int meshbus_meshcore_host_channel_data_handler(
	const meshcore_common_channel_data_event_t *event)
{
	ARG_UNUSED(event);

	return -ENOTSUP;
}

static int meshbus_meshcore_host_mesh_search_channels_by_hash(
	const uint8_t *hash, meshcore_common_channel_view_t channels[],
	int max_matches)
{
	ARG_UNUSED(hash);
	ARG_UNUSED(channels);
	ARG_UNUSED(max_matches);

	return 0;
}
#endif

/* host/events.c */

#if defined(CONFIG_MESHBUS_MESHCORE_CLIENT)
static meshbus_message_route
meshbus_meshcore_host_message_route_to_meshbus(
	meshcore_common_message_route_t route)
{
	switch (route) {
	case MESHCORE_COMMON_MESSAGE_ROUTE_FLOOD:
		return meshbus_MessageContent_MessageRoute_ROUTE_FLOOD;
	case MESHCORE_COMMON_MESSAGE_ROUTE_DIRECT:
		return meshbus_MessageContent_MessageRoute_ROUTE_DIRECT;
	case MESHCORE_COMMON_MESSAGE_ROUTE_UNSPECIFIED:
	default:
		return meshbus_MessageContent_MessageRoute_ROUTE_UNSPECIFIED;
	}
}

static int meshbus_meshcore_host_message_handler(const meshcore_common_message_t *message)
{
	struct meshbus_message_response_event event = { 0 };
	int rc;

	if (message == NULL || message->payload_len == 0U) {
		return -EINVAL;
	}

	event.type = (message->type == MESHCORE_COMMON_MESSAGE_TYPE_RECEIVE_CHANNEL) ?
		meshbus_MessageContent_MessageType_RECEIVE_CHANNEL :
		meshbus_MessageContent_MessageType_RECEIVE_NODE;
	event.route = meshbus_meshcore_host_message_route_to_meshbus(
		message->route);
	memcpy(event.target, message->target,
	       MIN((size_t)message->target_len, sizeof(event.target)));
	meshbus_meshcore_host_copy_cstr(event.sender_name, sizeof(event.sender_name),
				  message->sender_name);
	event.payload_len =
		(uint16_t)MIN((size_t)message->payload_len, sizeof(event.payload));
	memcpy(event.payload, message->payload, event.payload_len);
	event.sender_timestamp = message->sender_timestamp;
	event.has_rx_snr = message->has_rx_snr;
	if (message->has_rx_snr) {
		event.rx_snr = message->rx_snr;
	}

	rc = zbus_chan_pub(&meshbus_message_response_chan, &event, K_NO_WAIT);
	if (rc != 0) {
		LOG_WRN("meshbus_message_response_chan publish failed: rc=%d type=%d sender=%s payload_len=%u",
			rc, (int)event.type, event.sender_name,
			(unsigned int)event.payload_len);
	}
	return rc;
}

static int meshbus_meshcore_host_message_ack_handler(const uint8_t *target, uint8_t attempt)
{
	struct meshbus_message_ack_response_event event = { 0 };

	if (target == NULL) {
		return -EINVAL;
	}

	memcpy(event.target, target, sizeof(event.target));
	event.attempt = attempt;
	return meshbus_meshcore_ack_handoff_publish(&event);
}
#else
static int meshbus_meshcore_host_message_handler(const meshcore_common_message_t *message)
{
	ARG_UNUSED(message);

	return -ENOTSUP;
}

static int meshbus_meshcore_host_message_ack_handler(const uint8_t *target, uint8_t attempt)
{
	ARG_UNUSED(target);
	ARG_UNUSED(attempt);

	return -ENOTSUP;
}
#endif

static int meshbus_meshcore_host_advert_handler(const meshcore_common_advert_event_t *advert)
{
#if defined(CONFIG_MESHBUS_CONTACT)
	meshbus_contact_response_advert_event event = { 0 };

	if (advert == NULL) {
		return -EINVAL;
	}

	meshbus_meshcore_host_copy_cstr(event.name, sizeof(event.name), advert->name);
	if (!meshbus_meshcore_host_common_role_to_contact(advert->role, &event.role)) {
		return -EINVAL;
	}
	memcpy(event.public_key, advert->public_key, sizeof(event.public_key));
	event.is_new = advert->is_new;
	event.advert_timestamp = advert->advert_timestamp;
	if (advert->has_position) {
		event.has_position = true;
		event.latitude = advert->latitude;
		event.longitude = advert->longitude;
	}
	if (advert->has_out_path) {
		event.has_out_path = true;
		event.out_path_len = MIN((size_t)advert->out_path_len, sizeof(event.out_path));
		event.path_hash_size = advert->path_hash_size;
		if (event.out_path_len > 0U) {
			memcpy(event.out_path, advert->out_path, event.out_path_len);
		}
	}
	if (advert->raw_advert_len > 0U &&
	    advert->raw_advert_len <= sizeof(event.raw_advert)) {
		event.raw_advert_len = advert->raw_advert_len;
		memcpy(event.raw_advert, advert->raw_advert, event.raw_advert_len);
	}

	return zbus_chan_pub(&meshbus_contact_advert_response_chan, &event, K_NO_WAIT);
#else
	ARG_UNUSED(advert);

	return 0;
#endif
}

static int meshbus_meshcore_host_peer_path_publish(
	const meshcore_common_peer_path_event_t *peer_path, bool is_discover)
{
#if defined(CONFIG_MESHBUS_CONTACT)
	meshbus_contact_response_path_event event = { 0 };

	if (peer_path == NULL) {
		return -EINVAL;
	}

	event.is_discover = is_discover;
	memcpy(event.key_prefix, peer_path->key_prefix,
	       MIN(sizeof(event.key_prefix), sizeof(peer_path->key_prefix)));
	event.timestamp = peer_path->timestamp;
	event.tag = peer_path->tag;
	event.has_response_snr = true;
	event.response_snr = peer_path->last_seen_snr;
	event.has_out_path = true;
	event.out_path_len =
		MIN((size_t)peer_path->out_path_len, sizeof(event.out_path));
	event.path_hash_size = meshbus_meshcore_host_normalize_path_hash_size(
		peer_path->path_hash_size);
	event.out_path_len_field =
		meshbus_meshcore_host_encode_path_len(event.out_path_len, event.path_hash_size);
	memcpy(event.out_path, peer_path->out_path, event.out_path_len);
	event.out_path_snr_count = MIN((size_t)peer_path->out_path_snr_count,
				       sizeof(event.out_path_snr));
	memcpy(event.out_path_snr, peer_path->out_path_snr,
	       event.out_path_snr_count);
	event.return_path_snr_count = MIN((size_t)peer_path->return_path_snr_count,
					  sizeof(event.return_path_snr));
	memcpy(event.return_path_snr, peer_path->return_path_snr,
	       event.return_path_snr_count);
	return zbus_chan_pub(&meshbus_contact_path_response_chan, &event,
			     K_NO_WAIT);
#else
	ARG_UNUSED(peer_path);
	ARG_UNUSED(is_discover);

	return 0;
#endif
}

static int meshbus_meshcore_host_peer_path_handler(
	const meshcore_common_peer_path_event_t *peer_path)
{
	return meshbus_meshcore_host_peer_path_publish(peer_path, false);
}

static int meshbus_meshcore_host_trace_path_handler(uint8_t state, uint32_t tag,
						    const int8_t *out_path_snr,
						    uint8_t out_count,
						    const int8_t *return_path_snr,
						    uint8_t return_count,
						    bool has_response_snr,
						    int8_t response_snr,
						    uint32_t timestamp)
{
	meshbus_meshcore_trace_response_event event = {0};

	event.timestamp = timestamp;
	event.tag = tag;
	event.state = state;
	event.has_response_snr = has_response_snr;
	event.response_snr = response_snr;
	event.out_path_snr_count = meshbus_meshcore_host_copy_trace_snrs(
		event.out_path_snr, ARRAY_SIZE(event.out_path_snr),
		out_path_snr, out_count);
	event.return_path_snr_count = meshbus_meshcore_host_copy_trace_snrs(
		event.return_path_snr, ARRAY_SIZE(event.return_path_snr),
		return_path_snr, return_count);

	return zbus_chan_pub(&meshbus_meshcore_trace_response_chan, &event,
			     K_NO_WAIT);
}

static uint8_t meshbus_meshcore_host_copy_trace_snrs(int8_t *dst, size_t dst_count,
						     const int8_t *src, uint8_t src_count)
{
	size_t count;

	if (dst == NULL || dst_count == 0U || src == NULL || src_count == 0U) {
		return 0U;
	}

	count = MIN((size_t)src_count, dst_count);
	memcpy(dst, src, count);
	return (uint8_t)count;
}

static int meshbus_meshcore_host_telemetry_handler(const uint8_t *key_prefix,
						   uint32_t timestamp,
					   uint32_t tag,
					   const uint8_t *payload,
					   size_t payload_len)
{
#if defined(CONFIG_MESHBUS_CONTACT)
	meshbus_contact_response_telemetry_event event = { 0 };

	if (key_prefix == NULL || payload == NULL ||
	    payload_len > sizeof(event.payload)) {
		return -EINVAL;
	}

	memcpy(event.key_prefix, key_prefix, sizeof(event.key_prefix));
	event.timestamp = timestamp == 0U ? (uint32_t)meshbus_meshcore_host_now_seconds() :
					      timestamp;
	event.tag = tag;
	event.payload_len = (uint8_t)payload_len;
	if (payload_len > 0U) {
		memcpy(event.payload, payload, payload_len);
	}

	LOG_DBG("push telemetry event: %u bytes", payload_len);
	return zbus_chan_pub(&meshbus_contact_telemetry_response_chan, &event,
			     K_NO_WAIT);
#else
	ARG_UNUSED(key_prefix);
	ARG_UNUSED(timestamp);
	ARG_UNUSED(tag);
	ARG_UNUSED(payload);
	ARG_UNUSED(payload_len);

	return 0;
#endif
}

static uint8_t meshbus_meshcore_host_route_from_common(
	meshcore_common_message_route_t route)
{
	switch (route) {
	case MESHCORE_COMMON_MESSAGE_ROUTE_FLOOD:
		return MESHBUS_MESHCORE_ROUTE_FLOOD;
	case MESHCORE_COMMON_MESSAGE_ROUTE_DIRECT:
		return MESHBUS_MESHCORE_ROUTE_DIRECT;
	default:
		return MESHBUS_MESHCORE_ROUTE_UNSPECIFIED;
	}
}

static int meshbus_meshcore_host_binary_request_handler(
	const meshcore_common_binary_request_event_t *event)
{
	struct meshbus_meshcore_binary_request_event out = {0};
	size_t path_bytes = 0U;

	if (event == NULL || event->tag == 0U || event->payload_len == 0U ||
	    event->payload_len > sizeof(out.payload) ||
	    !meshbus_meshcore_path_len_to_bytes(event->path_len, &path_bytes, NULL) ||
	    path_bytes > sizeof(out.path)) {
		return -EINVAL;
	}

	out.route = meshbus_meshcore_host_route_from_common(event->route);
	memcpy(out.public_key, event->public_key, sizeof(out.public_key));
	out.tag = event->tag;
	out.path_len = event->path_len;
	if (path_bytes > 0U) {
		memcpy(out.path, event->path, path_bytes);
	}
	out.payload_len = event->payload_len;
	memcpy(out.payload, event->payload, event->payload_len);
	out.has_rx_snr = event->has_rx_snr;
	out.rx_snr_q4 = event->rx_snr_q4;

	return zbus_chan_pub(&meshbus_meshcore_binary_request_chan, &out, K_NO_WAIT);
}

static int meshbus_meshcore_host_binary_response_handler(const uint8_t *key_prefix,
					     uint32_t timestamp, uint32_t tag,
					     const uint8_t *payload,
					     size_t payload_len)
{
#if defined(CONFIG_MESHBUS_CONTACT)
	meshbus_contact_response_binary_event event = {0};

	if (key_prefix == NULL || (payload == NULL && payload_len > 0U) ||
	    payload_len > sizeof(event.payload)) {
		return -EINVAL;
	}

	memcpy(event.key_prefix, key_prefix, sizeof(event.key_prefix));
	event.timestamp = timestamp == 0U ? (uint32_t)meshbus_meshcore_host_now_seconds() : timestamp;
	event.tag = tag;
	event.payload_len = (uint8_t)payload_len;
	if (payload_len > 0U) {
		memcpy(event.payload, payload, payload_len);
	}

	return zbus_chan_pub(&meshbus_contact_binary_response_chan, &event, K_NO_WAIT);
#else
	ARG_UNUSED(key_prefix);
	ARG_UNUSED(timestamp);
	ARG_UNUSED(tag);
	ARG_UNUSED(payload);
	ARG_UNUSED(payload_len);

	return 0;
#endif
}

static int meshbus_meshcore_host_node_discover_handler(
	const meshcore_common_node_discover_event_t *event)
{
#if defined(CONFIG_MESHBUS_CONTACT)
	meshbus_contact_response_discover_event response = {0};

	if (event == NULL ||
	    event->public_key_len != sizeof(response.public_key) ||
	    event->path_len > sizeof(response.path) ||
	    !meshbus_meshcore_host_common_role_to_contact(event->role, &response.role)) {
		return -EINVAL;
	}

	response.tag = event->tag;
	memcpy(response.public_key, event->public_key, sizeof(response.public_key));
	response.path_len = event->path_len;
	if (event->path_len > 0U) {
		memcpy(response.path, event->path, event->path_len);
	}
	response.uplink_snr = event->uplink_snr;
	response.downlink_snr = event->downlink_snr;

	return zbus_chan_pub(&meshbus_contact_discover_response_chan,
			     &response, K_NO_WAIT);
#else
	ARG_UNUSED(event);

	return 0;
#endif
}

static int meshbus_meshcore_host_raw_data_handler(const meshcore_common_raw_data_event_t *event)
{
	struct meshbus_meshcore_raw_data_response_event response = {0};

	if (event == NULL || event->payload_len == 0U ||
	    event->payload_len > sizeof(response.payload)) {
		return -EINVAL;
	}

	response.path_len = event->path_len;
	memcpy(response.path, event->path, sizeof(response.path));
	response.payload_len = event->payload_len;
	memcpy(response.payload, event->payload, event->payload_len);
	response.has_rx_snr = event->has_rx_snr;
	response.rx_snr_q4 = event->rx_snr_q4;

	return zbus_chan_pub(&meshbus_meshcore_raw_data_response_chan,
			     &response, K_NO_WAIT);
}

static int meshbus_meshcore_host_control_data_handler(const meshcore_common_control_data_event_t *event)
{
	struct meshbus_meshcore_control_data_response_event response = {0};

	if (event == NULL || event->payload_len == 0U ||
	    event->payload_len > sizeof(response.payload)) {
		return -EINVAL;
	}

	response.path_len = event->path_len;
	memcpy(response.path, event->path, sizeof(response.path));
	response.payload_len = event->payload_len;
	memcpy(response.payload, event->payload, event->payload_len);
	response.has_rx_snr = event->has_rx_snr;
	response.rx_snr_q4 = event->rx_snr_q4;

	return zbus_chan_pub(&meshbus_meshcore_control_data_response_chan,
			     &response, K_NO_WAIT);
}

/* host/callbacks.c */

static void meshbus_meshcore_host_mesh_on_anon_data_recv(
	const meshcore_common_packet_view_t *packet, const uint8_t *secret,
	const meshcore_common_identity_view_t *sender, uint8_t *data, size_t len)
{
	(void)secret;
	struct meshbus_meshcore_anon_data_response_event event = {0};

	if (packet == NULL || sender == NULL || data == NULL || len == 0U ||
	    len > sizeof(event.payload)) {
		return;
	}

	event.route = meshbus_meshcore_host_route_from_common(packet->route);
	memcpy(event.public_key, sender->public_key, sizeof(event.public_key));
	event.path_len = (uint8_t)packet->path_len;
	if (packet->path_byte_len > 0U && packet->path != NULL) {
		memcpy(event.path, packet->path,
		       MIN((size_t)packet->path_byte_len, sizeof(event.path)));
	}
	event.payload_len = (uint8_t)len;
	memcpy(event.payload, data, len);
	event.has_rx_snr = packet->has_rx_snr;
	event.rx_snr_q4 = packet->rx_snr_q4;

	LOG_DBG("MeshCore anon-data received: sender=%02x%02x%02x%02x len=%u route=%u",
		event.public_key[0], event.public_key[1], event.public_key[2],
		event.public_key[3], (unsigned int)event.payload_len,
		(unsigned int)event.route);

	(void)zbus_chan_pub(&meshbus_meshcore_anon_data_response_chan,
			    &event, K_NO_WAIT);
}

static void meshbus_meshcore_host_mesh_on_path_recv(
	const meshcore_common_packet_view_t *packet,
	const meshcore_common_identity_view_t *sender, uint8_t *path,
	uint8_t path_len, uint8_t extra_type, uint8_t *extra, uint8_t extra_len)
{
	(void)packet;
	(void)sender;
	(void)path;
	(void)path_len;
	(void)extra_type;
	(void)extra;
	(void)extra_len;
}

static void meshbus_meshcore_host_mesh_on_raw_data_recv(
	const meshcore_common_packet_view_t *packet)
{
	/* Runtime owns raw-data event publication; fallback has no side effect. */
	(void)packet;
}

static void meshbus_meshcore_host_mesh_on_peer_data_recv(
	const meshcore_common_packet_view_t *packet, uint8_t type,
	const meshcore_common_peer_identity_t *sender, const uint8_t *secret,
	uint8_t *data, size_t len)
{
#if defined(CONFIG_MESHBUS_CONTACT)
	meshbus_contact contact = meshbus_Contact_init_zero;

	ARG_UNUSED(secret);

	if (packet == NULL || data == NULL || sender == NULL) {
		return;
	}

	(void)meshbus_meshcore_host_config_sync();
	if (meshbus_contact_find_by_key(sender->public_key, &contact) != 0 &&
	    !meshbus_meshcore_host_contact_from_identity(sender, &contact)) {
		return;
	}

	if (type == MESHCORE_PACKET_PAYLOAD_TYPE_TXT_MSG && len > 5U) {
		meshcore_common_message_t message;
		uint8_t flags = data[4] >> 2;
		size_t text_off = 5U;
		size_t text_len;
		const void *nul;
		uint32_t sender_timestamp = 0U;

		if (flags == MESHCORE_COMMON_TXT_TYPE_SIGNED_PLAIN) {
			if (len <= 9U) {
				return;
			}
			text_off = 9U;
		} else if (flags != MESHCORE_COMMON_TXT_TYPE_PLAIN) {
			text_off = 0U;
		}

		if (text_off > 0U) {
			nul = memchr(&data[text_off], 0, len - text_off);
			text_len = (nul != NULL) ?
				(size_t)((const uint8_t *)nul - &data[text_off]) :
				(len - text_off);
			if (text_len > 0U) {
				memset(&message, 0, sizeof(message));
				memcpy(&sender_timestamp, data, sizeof(sender_timestamp));
				message.type = MESHCORE_COMMON_MESSAGE_TYPE_RECEIVE_NODE;
				message.route = packet->route;
				message.target_len = MESHCORE_MESSAGE_TARGET_PREFIX_BYTES;
				memcpy(message.target, sender->public_key,
				       MIN(sizeof(message.target),
					   sizeof(sender->public_key)));
				meshbus_meshcore_host_copy_cstr(message.sender_name,
						   sizeof(message.sender_name),
						   sender->name);
				message.payload_len =
					(uint16_t)MIN(text_len, sizeof(message.payload));
				memcpy(message.payload, &data[text_off],
				       message.payload_len);
				message.sender_timestamp = sender_timestamp;
				message.has_rx_snr = packet->has_rx_snr;
				message.rx_snr = (float)packet->rx_snr_q4;
				(void)meshbus_meshcore_host_message_handler(&message);
			}
		}
	}

	meshbus_meshcore_host_update_contact_seen(&contact, packet->has_rx_snr, packet->rx_snr_q4);
	(void)meshbus_contact_set(contact.public_key.bytes, &contact);
#else
	ARG_UNUSED(packet);
	ARG_UNUSED(type);
	ARG_UNUSED(sender);
	ARG_UNUSED(secret);
	ARG_UNUSED(data);
	ARG_UNUSED(len);
#endif
}

static void meshbus_meshcore_host_mesh_on_trace_recv(
	const meshcore_common_packet_view_t *packet, uint32_t tag,
	uint32_t auth_code, uint8_t flags, const uint8_t *path_snrs,
	const uint8_t *path_hashes, uint8_t path_len)
{
	/*
	 * Runtime owns trace correlation and response publication now. This
	 * direct receive hook remains for protocol-level compatibility tests.
	 */
	ARG_UNUSED(packet);
	ARG_UNUSED(tag);
	ARG_UNUSED(auth_code);
	ARG_UNUSED(flags);
	ARG_UNUSED(path_snrs);
	ARG_UNUSED(path_hashes);
	ARG_UNUSED(path_len);
}

static bool meshbus_meshcore_host_mesh_on_peer_path_recv(
	const meshcore_common_packet_view_t *packet,
	const meshcore_common_peer_identity_t *sender, const uint8_t *secret,
	uint8_t *path, uint8_t path_len, uint8_t extra_type, uint8_t *extra,
	uint8_t extra_len)
{
#if defined(CONFIG_MESHBUS_CONTACT)
	meshcore_common_peer_path_event_t event = { 0 };
	uint8_t path_byte_len;
	uint8_t path_hash_size;
	uint8_t out_snrs[MESHCORE_MAX_PATH_LEN];
	uint8_t return_snrs[MESHCORE_MAX_PATH_LEN];
	uint8_t out_len = 0U;
	uint8_t return_len = 0U;
	ARG_UNUSED(secret);

	if (packet == NULL || sender == NULL) {
		return false;
	}
	(void)meshbus_meshcore_host_config_sync();
	if (!meshbus_meshcore_host_path_len_to_bytes(path_len, &path_byte_len,
					       &path_hash_size) ||
	    (path_byte_len > 0U && path == NULL) ||
	    (extra_len > 0U && extra == NULL)) {
		return false;
	}

	if (extra_type == MESHCORE_PACKET_PATH_EXTRA_TYPE_SNR && extra_len >= 1U) {
		uint8_t snr_len = extra[0];

		if (snr_len > (extra_len - 1U)) {
			snr_len = (uint8_t)(extra_len - 1U);
		}
		out_len = MIN((size_t)snr_len, sizeof(out_snrs));
		if (out_len > 0U) {
			memcpy(out_snrs, &extra[1], out_len);
		}

		if (meshbus_meshcore_host_extract_return_path_snrs(
			    packet, return_snrs, &return_len)) {
			meshbus_meshcore_host_append_snr_with_trunc(
				return_snrs, &return_len,
				sizeof(return_snrs), packet->rx_snr_q4);
		}
	}

	memcpy(event.key_prefix, sender->public_key,
	       MIN(sizeof(event.key_prefix), sizeof(sender->public_key)));
	event.timestamp = (uint32_t)meshbus_meshcore_host_now_seconds();
	event.last_seen_snr = packet->rx_snr_q4;
	event.out_path_len = path_byte_len;
	event.path_hash_size = path_hash_size;
	if (path_byte_len > 0U) {
		memcpy(event.out_path, path, path_byte_len);
	}
	event.out_path_snr_count =
		MIN((size_t)out_len, sizeof(event.out_path_snr));
	memcpy(event.out_path_snr, out_snrs, event.out_path_snr_count);
	event.return_path_snr_count =
		MIN((size_t)return_len, sizeof(event.return_path_snr));
	memcpy(event.return_path_snr, return_snrs,
	       event.return_path_snr_count);

	(void)meshbus_meshcore_host_peer_path_handler(&event);

	/*
	 * Runtime owns role-gated contact-path follow-up behavior. The fallback
	 * platform hook only projects the observed path event.
	 */
	return false;
#else
	ARG_UNUSED(packet);
	ARG_UNUSED(sender);
	ARG_UNUSED(secret);
	ARG_UNUSED(path);
	ARG_UNUSED(path_len);
	ARG_UNUSED(extra_type);
	ARG_UNUSED(extra);
	ARG_UNUSED(extra_len);

	return false;
#endif
}

static void meshbus_meshcore_host_mesh_on_advert_recv(
	const meshcore_common_packet_view_t *packet,
	const meshcore_common_identity_view_t *identity, uint32_t timestamp,
	const uint8_t *app_data, size_t app_data_len)
{
#if defined(CONFIG_MESHBUS_CONTACT)
	struct meshcore_common_advert_info info;
	meshcore_common_advert_event_t event = { 0 };

	if (packet == NULL || identity == NULL ||
	    !meshbus_meshcore_host_parse_advert(app_data, app_data_len, &info) ||
	    !info.valid) {
		return;
	}

	meshbus_meshcore_host_copy_cstr(event.name, sizeof(event.name), info.name);
	event.role = info.role;
	memcpy(event.public_key, identity->public_key,
	       sizeof(identity->public_key));
	event.is_new = true;
	event.advert_timestamp = timestamp;
	if (info.has_position) {
		event.has_position = true;
		event.latitude = info.latitude;
		event.longitude = info.longitude;
	}
	if (packet->path_hash_size > 0U &&
	    packet->path_hash_size <= MESHBUS_MESHCORE_PATH_HASH_SIZE_MAX) {
		event.has_out_path = true;
		event.out_path_len = packet->path_byte_len;
		event.path_hash_size = packet->path_hash_size;
		if (packet->path_byte_len > 0U && packet->path != NULL) {
			memcpy(event.out_path, packet->path, packet->path_byte_len);
		}
	}
	if (packet->raw != NULL &&
	    packet->raw_len <= sizeof(event.raw_advert)) {
		event.raw_advert_len = (uint8_t)packet->raw_len;
		memcpy(event.raw_advert, packet->raw, packet->raw_len);
	}
	(void)meshbus_meshcore_host_advert_handler(&event);
#else
	ARG_UNUSED(packet);
	ARG_UNUSED(identity);
	ARG_UNUSED(timestamp);
	ARG_UNUSED(app_data);
	ARG_UNUSED(app_data_len);
#endif
}

static void meshbus_meshcore_host_mesh_on_control_data_recv(
	const meshcore_common_packet_view_t *packet)
{
	/*
	 * Runtime owns control-data receive behavior, including repeater
	 * discover replies. This low-level hook remains as a compatibility
	 * fallback and intentionally has no side effect.
	 */
	ARG_UNUSED(packet);
}

static void meshbus_meshcore_host_mesh_on_group_data_recv(
	const meshcore_common_packet_view_t *packet, uint8_t type,
	const meshcore_common_channel_view_t *channel, uint8_t *data, size_t len)
{
	meshcore_common_message_t message;
	const uint8_t *colon;
	const uint8_t *payload_ptr;
	size_t name_len;
	size_t payload_len;
	uint32_t sender_timestamp = 0U;

	if (packet == NULL || channel == NULL || data == NULL) {
		return;
	}

	memset(&message, 0, sizeof(message));

	(void)meshbus_meshcore_host_config_sync();
	if (type != MESHCORE_PACKET_PAYLOAD_TYPE_GRP_TXT || len <= 5U ||
	    (data[4] >> 2) != 0U) {
		return;
	}

	memcpy(&sender_timestamp, data, sizeof(sender_timestamp));
	colon = (const uint8_t *)memchr(&data[5], ':', len - 5U);
	if (colon == NULL) {
		return;
	}

	name_len = (size_t)(colon - &data[5]);
	if (name_len == 0U) {
		(void)snprintf(message.sender_name, sizeof(message.sender_name), "%s",
			       "unknown");
	} else if (name_len >= sizeof(message.sender_name)) {
		name_len = sizeof(message.sender_name) - 1U;
		memcpy(message.sender_name, &data[5], name_len);
		message.sender_name[name_len] = '\0';
	} else {
		memcpy(message.sender_name, &data[5], name_len);
		message.sender_name[name_len] = '\0';
	}

	payload_ptr = colon + 1U;
	payload_len = (size_t)(&data[len] - payload_ptr);
	if (payload_len > 0U && payload_ptr[0] == ' ') {
		payload_ptr++;
		payload_len--;
	}
	if (payload_len > 0U) {
		const void *nul = memchr(payload_ptr, 0, payload_len);

		if (nul != NULL) {
			payload_len = (size_t)((const uint8_t *)nul - payload_ptr);
		}
	}
	if (payload_len == 0U) {
		return;
	}

	message.type = MESHCORE_COMMON_MESSAGE_TYPE_RECEIVE_CHANNEL;
	message.route = packet->route;
	message.target_len = MESHCORE_MESSAGE_TARGET_PREFIX_BYTES;
	memcpy(message.target, channel->secret, sizeof(message.target));
	message.payload_len =
		(uint16_t)MIN(payload_len, sizeof(message.payload));
	memcpy(message.payload, payload_ptr, message.payload_len);
	message.sender_timestamp = sender_timestamp;
	message.has_rx_snr = packet->has_rx_snr;
	message.rx_snr = (float)packet->rx_snr_q4;
	(void)meshbus_meshcore_host_message_handler(&message);
}

static void meshbus_meshcore_host_mesh_on_ack_recv(
	const meshcore_common_packet_view_t *packet, uint32_t ack_crc)
{
	/* Runtime owns ACK correlation; direct protocol dispatch has no side effect. */
	ARG_UNUSED(packet);
	ARG_UNUSED(ack_crc);
}

/* MeshCore platform hooks backed by Meshbus host services. */

int meshcore_platform_node_identity_get(meshcore_common_node_identity_t *out)
{
	return meshbus_meshcore_host_node_identity_get(out);
}

int meshcore_platform_node_config_last_modify_get(uint32_t *out_timestamp)
{
	return meshbus_meshcore_host_config_last_modify_get(out_timestamp);
}

int meshcore_platform_node_runtime_policy_get(
	meshcore_common_node_runtime_policy_t *out)
{
	return meshbus_meshcore_host_node_runtime_policy_get(out);
}

int meshcore_platform_node_advert_profile_get(
	meshcore_common_node_advert_profile_t *out)
{
	return meshbus_meshcore_host_node_advert_profile_get(out);
}

int meshcore_platform_peer_path_get_by_key(
	const uint8_t *public_key, meshcore_common_peer_path_t *out)
{
	return meshbus_meshcore_host_contact_path_get_by_key(public_key, out);
}

int meshcore_platform_peer_seen_update(const uint8_t *public_key, bool has_snr,
				       int8_t snr_q4)
{
	return meshbus_meshcore_host_contact_seen_update(public_key, has_snr, snr_q4);
}

int meshcore_platform_peer_next_shared_secret_by_hash(
	const uint8_t *hash, size_t start_slot, size_t *slot_id,
	uint8_t *dest_secret, meshcore_common_peer_identity_t *peer_identity)
{
	return meshbus_meshcore_host_mesh_next_peer_shared_secret_by_hash(
		hash, start_slot, slot_id, dest_secret, peer_identity);
}

int meshcore_platform_channel_secret_match_exists(uint8_t channel_hash,
						  const uint8_t *secret,
						  size_t secret_len)
{
	return meshbus_meshcore_host_channel_secret_match_exists(channel_hash, secret,
							   secret_len);
}

int meshcore_platform_channel_secret_hash(const uint8_t *secret,
					  size_t secret_len, uint8_t *out_hash)
{
	return meshbus_meshcore_host_channel_secret_hash(secret, secret_len, out_hash);
}

int meshcore_platform_channel_search_by_hash(
	const uint8_t *hash, meshcore_common_channel_view_t *channels,
	int max_matches)
{
	return meshbus_meshcore_host_mesh_search_channels_by_hash(hash, channels,
							    max_matches);
}

void meshcore_platform_dispatcher_log_rx_raw(float snr, float rssi,
					     const uint8_t raw[], int len)
{
	meshbus_meshcore_host_dispatcher_log_rx_raw(snr, rssi, raw, len);
}

void meshcore_platform_dispatcher_log_rx(
	const meshcore_common_packet_view_t *packet, int len, float score)
{
	meshbus_meshcore_host_dispatcher_log_rx(packet, len, score);
}

void meshcore_platform_dispatcher_log_tx(
	const meshcore_common_packet_view_t *packet, int len)
{
	meshbus_meshcore_host_dispatcher_log_tx(packet, len);
}

void meshcore_platform_dispatcher_log_tx_fail(
	const meshcore_common_packet_view_t *packet, int len)
{
	meshbus_meshcore_host_dispatcher_log_tx_fail(packet, len);
}

float meshcore_platform_dispatcher_airtime_budget_factor_get(void)
{
	return meshbus_meshcore_host_dispatcher_get_airtime_budget_factor();
}

int meshcore_platform_dispatcher_rx_delay_calc(float score, uint32_t air_time)
{
	return meshbus_meshcore_host_dispatcher_calc_rx_delay(score, air_time);
}

uint32_t meshcore_platform_dispatcher_cad_fail_max_duration_get(void)
{
	return meshbus_meshcore_host_dispatcher_get_cad_fail_max_duration();
}

int meshcore_platform_dispatcher_interference_threshold_get(void)
{
	return meshbus_meshcore_host_dispatcher_get_interference_threshold();
}

int meshcore_platform_dispatcher_agc_reset_interval_get(void)
{
	return meshbus_meshcore_host_dispatcher_get_agc_reset_interval();
}

unsigned long meshcore_platform_dispatcher_duty_cycle_window_ms_get(void)
{
	return meshbus_meshcore_host_dispatcher_get_duty_cycle_window_ms();
}

uint32_t meshcore_platform_mesh_cad_fail_retry_delay_get(void)
{
	return meshbus_meshcore_host_mesh_get_cad_fail_retry_delay();
}

bool meshcore_platform_mesh_filter_recv_flood_packet(
	const meshcore_common_packet_view_t *packet)
{
	return meshbus_meshcore_host_mesh_filter_recv_flood_packet(packet);
}

bool meshcore_platform_mesh_allow_packet_forward(
	const meshcore_common_packet_view_t *packet)
{
	return meshbus_meshcore_host_mesh_allow_packet_forward(packet);
}

uint32_t meshcore_platform_mesh_retransmit_delay_get(
	const meshcore_common_packet_view_t *packet)
{
	return meshbus_meshcore_host_mesh_get_retransmit_delay(packet);
}

uint32_t meshcore_platform_mesh_direct_retransmit_delay_get(
	const meshcore_common_packet_view_t *packet)
{
	return meshbus_meshcore_host_mesh_get_direct_retransmit_delay(packet);
}

uint8_t meshcore_platform_mesh_extra_ack_transmit_count_get(void)
{
	return meshbus_meshcore_host_mesh_get_extra_ack_transmit_count();
}

void meshcore_platform_mesh_on_peer_data_recv(
	const meshcore_common_packet_view_t *packet, uint8_t type,
	const meshcore_common_peer_identity_t *sender, const uint8_t *secret,
	uint8_t *data, size_t len)
{
	meshbus_meshcore_host_mesh_on_peer_data_recv(packet, type, sender, secret,
						data, len);
}

void meshcore_platform_mesh_on_trace_recv(
	const meshcore_common_packet_view_t *packet, uint32_t tag,
	uint32_t auth_code, uint8_t flags, const uint8_t *path_snrs,
	const uint8_t *path_hashes, uint8_t path_len)
{
	meshbus_meshcore_host_mesh_on_trace_recv(packet, tag, auth_code, flags,
					    path_snrs, path_hashes, path_len);
}

bool meshcore_platform_mesh_on_peer_path_recv(
	const meshcore_common_packet_view_t *packet,
	const meshcore_common_peer_identity_t *sender, const uint8_t *secret,
	uint8_t *path, uint8_t path_len, uint8_t extra_type, uint8_t *extra,
	uint8_t extra_len)
{
	return meshbus_meshcore_host_mesh_on_peer_path_recv(
		packet, sender, secret, path, path_len, extra_type, extra,
		extra_len);
}

void meshcore_platform_mesh_on_advert_recv(
	const meshcore_common_packet_view_t *packet,
	const meshcore_common_identity_view_t *identity, uint32_t timestamp,
	const uint8_t *app_data, size_t app_data_len)
{
	meshbus_meshcore_host_mesh_on_advert_recv(packet, identity, timestamp,
					     app_data, app_data_len);
}

void meshcore_platform_mesh_on_anon_data_recv(
	const meshcore_common_packet_view_t *packet, const uint8_t *secret,
	const meshcore_common_identity_view_t *sender, uint8_t *data, size_t len)
{
	meshbus_meshcore_host_mesh_on_anon_data_recv(packet, secret, sender, data,
						len);
}

void meshcore_platform_mesh_on_path_recv(
	const meshcore_common_packet_view_t *packet,
	const meshcore_common_identity_view_t *sender, uint8_t *path,
	uint8_t path_len, uint8_t extra_type, uint8_t *extra, uint8_t extra_len)
{
	meshbus_meshcore_host_mesh_on_path_recv(packet, sender, path, path_len,
					   extra_type, extra, extra_len);
}

void meshcore_platform_mesh_on_control_data_recv(
	const meshcore_common_packet_view_t *packet)
{
	meshbus_meshcore_host_mesh_on_control_data_recv(packet);
}

void meshcore_platform_mesh_on_raw_data_recv(
	const meshcore_common_packet_view_t *packet)
{
	meshbus_meshcore_host_mesh_on_raw_data_recv(packet);
}

void meshcore_platform_mesh_on_group_data_recv(
	const meshcore_common_packet_view_t *packet, uint8_t type,
	const meshcore_common_channel_view_t *channel, uint8_t *data, size_t len)
{
	meshbus_meshcore_host_mesh_on_group_data_recv(packet, type, channel, data,
						 len);
}

void meshcore_platform_mesh_on_ack_recv(
	const meshcore_common_packet_view_t *packet, uint32_t ack_crc)
{
	meshbus_meshcore_host_mesh_on_ack_recv(packet, ack_crc);
}

void meshcore_platform_runtime_request_error(uint8_t request_type, int err_code)
{
	meshbus_meshcore_host_runtime_request_error(request_type, err_code);
}

int meshcore_platform_event_message(const meshcore_common_message_t *message)
{
	return meshbus_meshcore_host_message_handler(message);
}

int meshcore_platform_event_message_ack(const uint8_t *target, uint8_t attempt)
{
	return meshbus_meshcore_host_message_ack_handler(target, attempt);
}

int meshcore_platform_event_advert(const meshcore_common_advert_event_t *advert)
{
	return meshbus_meshcore_host_advert_handler(advert);
}

int meshcore_platform_event_peer_path_publish(
	const meshcore_common_peer_path_event_t *peer_path, bool is_discover)
{
	return meshbus_meshcore_host_peer_path_publish(peer_path, is_discover);
}

int meshcore_platform_event_peer_path(
	const meshcore_common_peer_path_event_t *peer_path)
{
	return meshbus_meshcore_host_peer_path_handler(peer_path);
}

int meshcore_platform_event_trace_path(
	uint8_t state, uint32_t tag,
	const int8_t *out_path_snr, uint8_t out_count,
	const int8_t *return_path_snr, uint8_t return_count,
	bool has_response_snr, int8_t response_snr, uint32_t timestamp)
{
	return meshbus_meshcore_host_trace_path_handler(
		state, tag, out_path_snr, out_count, return_path_snr, return_count,
		has_response_snr, response_snr, timestamp);
}

int meshcore_platform_event_telemetry(const uint8_t *key_prefix,
				      uint32_t timestamp,
				      uint32_t tag,
				      const uint8_t *payload,
				      size_t payload_len)
{
	return meshbus_meshcore_host_telemetry_handler(key_prefix, timestamp, tag,
						 payload, payload_len);
}

int meshcore_platform_event_binary_request(
	const meshcore_common_binary_request_event_t *event)
{
	return meshbus_meshcore_host_binary_request_handler(event);
}

int meshcore_platform_event_binary_response(const uint8_t *key_prefix,
					    uint32_t timestamp, uint32_t tag,
					    const uint8_t *payload,
					    size_t payload_len)
{
	return meshbus_meshcore_host_binary_response_handler(
		key_prefix, timestamp, tag, payload, payload_len);
}

int meshcore_platform_event_node_discover(
	const meshcore_common_node_discover_event_t *event)
{
	return meshbus_meshcore_host_node_discover_handler(event);
}

int meshcore_platform_event_channel_data(
	const meshcore_common_channel_data_event_t *event)
{
	return meshbus_meshcore_host_channel_data_handler(event);
}

int meshcore_platform_event_raw_data(
	const meshcore_common_raw_data_event_t *event)
{
	return meshbus_meshcore_host_raw_data_handler(event);
}

int meshcore_platform_event_control_data(
	const meshcore_common_control_data_event_t *event)
{
	return meshbus_meshcore_host_control_data_handler(event);
}
