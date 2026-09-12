// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "meshcore/platform.h"
#include "meshcore_advert_data.h"
#include "meshcore_clock.h"
#include "meshcore_identity.h"
#include "meshcore_test_runtime.h"
#include "meshcore_group_channel.h"
#include "meshcore_packet.h"

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <psa/crypto.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

/*
 * Reusable Zephyr test host shim for meshcore module tests.
 *
 * Despite the filename, this file provides both the private legacy host shim
 * used by upstream reference tests and the singleton meshcore_platform_* hooks
 * used by target runtime tests.
 */

#define MESHCORE_HAL_SHA256_SIZE 32U
#define MESHCORE_HAL_AES128_KEY_SIZE 16U
#define MESHCORE_HAL_AES128_BLOCK_SIZE 16U
#define MESHCORE_HAL_TEST_RNG_MAX_LEN 64U
#define MESHCORE_HAL_TEST_RADIO_QUEUE_CAP 8
#define MESHCORE_HAL_TEST_RADIO_FREQUENCY_HZ 918000000ULL

#if defined(__GNUC__)
#define MESHCORE_TEST_WEAK __attribute__((weak))
#else
#define MESHCORE_TEST_WEAK
#endif

/*
 * The shared test host is linked by focused protocol targets that intentionally
 * omit unrelated core/support sources. Weak local defaults keep the test port
 * linkable in those small targets; targets that exercise these helpers link
 * the real implementations, which override these definitions.
 */
MESHCORE_TEST_WEAK bool meshcore_clock_millis_override_is_enabled(void)
{
	return false;
}

MESHCORE_TEST_WEAK unsigned long meshcore_clock_millis_override_value_get(void)
{
	return 0UL;
}

MESHCORE_TEST_WEAK uint8_t
meshcore_packet_get_path_hash_size(const struct meshcore_packet *packet)
{
	ARG_UNUSED(packet);
	return 1U;
}

MESHCORE_TEST_WEAK int
meshcore_packet_get_raw_length(const struct meshcore_packet *packet)
{
	ARG_UNUSED(packet);
	return 0;
}

MESHCORE_TEST_WEAK void meshcore_advert_data_parser_init(
	struct meshcore_advert_data_parser *parser, const uint8_t app_data[],
	uint8_t app_data_len)
{
	ARG_UNUSED(app_data);
	ARG_UNUSED(app_data_len);
	if (parser != NULL) {
		memset(parser, 0, sizeof(*parser));
	}
}

MESHCORE_TEST_WEAK bool meshcore_advert_data_parser_is_valid(
	const struct meshcore_advert_data_parser *parser)
{
	ARG_UNUSED(parser);
	return false;
}

MESHCORE_TEST_WEAK uint8_t meshcore_advert_data_parser_get_type(
	const struct meshcore_advert_data_parser *parser)
{
	ARG_UNUSED(parser);
	return ADV_TYPE_NONE;
}

MESHCORE_TEST_WEAK bool meshcore_advert_data_parser_has_name(
	const struct meshcore_advert_data_parser *parser)
{
	ARG_UNUSED(parser);
	return false;
}

MESHCORE_TEST_WEAK const char *meshcore_advert_data_parser_get_name(
	const struct meshcore_advert_data_parser *parser)
{
	ARG_UNUSED(parser);
	return "";
}

MESHCORE_TEST_WEAK bool meshcore_advert_data_parser_has_lat_lon(
	const struct meshcore_advert_data_parser *parser)
{
	ARG_UNUSED(parser);
	return false;
}

MESHCORE_TEST_WEAK int32_t meshcore_advert_data_parser_get_int_lat(
	const struct meshcore_advert_data_parser *parser)
{
	ARG_UNUSED(parser);
	return 0;
}

MESHCORE_TEST_WEAK int32_t meshcore_advert_data_parser_get_int_lon(
	const struct meshcore_advert_data_parser *parser)
{
	ARG_UNUSED(parser);
	return 0;
}

static atomic_t g_psa_inited = ATOMIC_INIT(0);
struct meshcore_hal_test_radio_packet {
	uint8_t data[MESHCORE_MAX_TRANS_UNIT_LEN];
	int len;
	int16_t rssi_dbm;
	int8_t snr_db;
	unsigned long ready_at_ms;
};

struct meshcore_hal_test_radio_state {
	struct meshcore_hal_test_radio_packet
		rx_queue[MESHCORE_HAL_TEST_RADIO_QUEUE_CAP];
	int rx_head;
	int rx_tail;
	int rx_count;
	uint32_t send_delay_per_byte_ms;
	unsigned long send_done_at_ms;
	bool send_in_progress;
	bool send_completed;
	bool send_result;
	bool send_never_complete;
	bool force_in_rx_mode_enabled;
	bool force_in_rx_mode_value;
	bool force_receiving_enabled;
	bool force_receiving_value;
	float last_rssi;
	float last_snr;
	uint32_t packets_recv;
	uint32_t packets_sent;
	uint32_t noise_floor_calibrate_count;
	uint32_t agc_reset_count;
	uint32_t on_send_finished_count;
	int last_send_len;
	uint8_t last_send_data[MESHCORE_MAX_TRANS_UNIT_LEN];
};

static struct {
	bool active;
	uint8_t bytes[MESHCORE_HAL_TEST_RNG_MAX_LEN];
	size_t len;
	size_t idx;
} g_test_rng;
static struct {
	bool active;
	unsigned long value;
} g_test_millis;
static struct {
	bool valid;
	uint8_t public_key[MESHCORE_PUBLIC_KEY_SIZE];
	char name[MESHCORE_NODE_NAME_MAX_LEN];
	meshcore_common_node_role_t role;
	uint8_t flags;
} g_test_peer_identity;
static struct {
	bool valid;
	bool known;
	uint8_t public_key[MESHCORE_PUBLIC_KEY_SIZE];
	meshcore_common_peer_path_t path;
} g_test_peer_path;
static struct {
	bool valid;
	uint8_t hash[MESHCORE_CHANNEL_HASH_BYTES];
	meshcore_common_channel_secret_t secret;
} g_test_channel_secret;
static struct {
	bool valid;
	meshcore_common_node_identity_t identity;
	meshcore_common_node_runtime_policy_t policy;
	meshcore_common_node_advert_profile_t advert_profile;
} g_test_node;
static struct {
	bool message_valid;
	meshcore_test_message_event_t message;
	bool ack_valid;
	meshcore_test_message_ack_event_t ack;
	bool advert_valid;
	meshcore_test_advert_event_t advert;
	bool peer_path_valid;
	meshcore_test_peer_path_event_t peer_path;
	bool trace_valid;
	meshcore_test_trace_event_t trace;
	bool telemetry_valid;
	meshcore_test_telemetry_event_t telemetry;
	bool binary_request_valid;
	meshcore_test_binary_request_event_t binary_request;
	bool binary_response_valid;
	meshcore_test_binary_response_event_t binary_response;
	bool node_discover_valid;
	meshcore_test_node_discover_event_t node_discover;
} g_test_publish_events;
static uint32_t g_test_rtc_current_time;
static struct meshcore_hal_test_radio_state g_test_radio;
static struct {
	uint32_t arm_count;
	uint32_t cancel_count;
	uint32_t last_deadline_ms;
} g_test_platform_timer;
static meshcore_hal_test_dispatcher_script_t g_test_dispatcher_script;
static meshcore_hal_test_mesh_script_t g_test_mesh_script;
static meshcore_hal_test_dispatcher_script_t *g_test_dispatcher_script_mirror;
static meshcore_hal_test_mesh_script_t *g_test_mesh_script_mirror;
static struct {
	bool force_encrypt_fail;
	bool force_hmac_fail;
} g_test_crypto_fail;

static float g_snr_threshold[] = {
	-7.5f,
	-10.0f,
	-12.5f,
	-15.0f,
	-17.5f,
	-20.0f,
};

static uint8_t meshcore_hal_test_normalize_path_hash_size(uint8_t hash_size)
{
	if (hash_size == 0U || hash_size > 3U) {
		return 1U;
	}

	return hash_size;
}

static void meshcore_hal_psa_init_once(void)
{
	if (atomic_cas(&g_psa_inited, 0, 1)) {
		(void)psa_crypto_init();
	}
}

static void meshcore_hal_test_dispatcher_script_sync_mirror(void)
{
	if (g_test_dispatcher_script_mirror != NULL) {
		*g_test_dispatcher_script_mirror = g_test_dispatcher_script;
	}
}

static void meshcore_hal_test_mesh_script_sync_mirror(void)
{
	if (g_test_mesh_script_mirror != NULL) {
		*g_test_mesh_script_mirror = g_test_mesh_script;
	}
}

static struct meshcore_hal_test_radio_packet *meshcore_hal_test_radio_peek_ready(
	unsigned long now)
{
	struct meshcore_hal_test_radio_packet *packet;

	if (g_test_radio.rx_count == 0) {
		return NULL;
	}

	packet = &g_test_radio.rx_queue[g_test_radio.rx_head];
	if ((long)(now - packet->ready_at_ms) >= 0) {
		return packet;
	}
	return NULL;
}

static void meshcore_hal_test_radio_pop_ready(void)
{
	if (g_test_radio.rx_count == 0) {
		return;
	}

	g_test_radio.rx_head =
		(g_test_radio.rx_head + 1) % MESHCORE_HAL_TEST_RADIO_QUEUE_CAP;
	g_test_radio.rx_count--;
}

static float meshcore_hal_test_radio_packet_score_internal(float snr, int sf,
							   int packet_len)
{
	float success_rate_based_on_snr;
	float collision_penalty;
	float score;

	if (sf < 7 || sf > 12) {
		return 0.0f;
	}
	if (snr < g_snr_threshold[sf - 7]) {
		return 0.0f;
	}

	success_rate_based_on_snr = (snr - g_snr_threshold[sf - 7]) / 10.0f;
	collision_penalty = 1.0f - ((float)packet_len / 256.0f);
	score = success_rate_based_on_snr * collision_penalty;
	if (score < 0.0f) {
		return 0.0f;
	}
	if (score > 1.0f) {
		return 1.0f;
	}
	return score;
}

void meshcore_hal_test_host_state_reset(void)
{
	memset(&g_test_dispatcher_script, 0, sizeof(g_test_dispatcher_script));
	g_test_dispatcher_script.recv_action = 0U;
	g_test_dispatcher_script.airtime_budget_factor = 1.0f;
	g_test_dispatcher_script.cad_fail_max_duration_ms = 4000U;
	g_test_dispatcher_script.duty_cycle_window_ms = 3600000UL;

	memset(&g_test_mesh_script, 0, sizeof(g_test_mesh_script));
	memset(&g_test_peer_identity, 0, sizeof(g_test_peer_identity));
	memset(&g_test_peer_path, 0, sizeof(g_test_peer_path));
	memset(&g_test_channel_secret, 0, sizeof(g_test_channel_secret));
	memset(&g_test_node, 0, sizeof(g_test_node));
	memset(&g_test_publish_events, 0, sizeof(g_test_publish_events));
	memset(&g_test_crypto_fail, 0, sizeof(g_test_crypto_fail));
	memset(&g_test_platform_timer, 0, sizeof(g_test_platform_timer));
}

void meshcore_hal_test_crypto_set_encrypt_fail(bool enabled)
{
	g_test_crypto_fail.force_encrypt_fail = enabled;
}

void meshcore_hal_test_crypto_set_hmac_fail(bool enabled)
{
	g_test_crypto_fail.force_hmac_fail = enabled;
}

void meshcore_hal_test_peer_identity_set(const uint8_t *public_key, const char *name,
					 meshcore_common_node_role_t role, uint8_t flags)
{
	memset(&g_test_peer_identity, 0, sizeof(g_test_peer_identity));
	if (public_key == NULL) {
		return;
	}

	g_test_peer_identity.valid = true;
	memcpy(g_test_peer_identity.public_key, public_key,
	       sizeof(g_test_peer_identity.public_key));
	if (name != NULL) {
		memcpy(g_test_peer_identity.name, name,
		       MIN(strlen(name), sizeof(g_test_peer_identity.name) - 1U));
	}
	g_test_peer_identity.role = role;
	g_test_peer_identity.flags = flags;
}

void meshcore_hal_test_peer_path_set(const uint8_t *public_key, const uint8_t *path,
				     uint8_t path_len, uint8_t path_hash_size)
{
	memset(&g_test_peer_path, 0, sizeof(g_test_peer_path));
	if (public_key == NULL) {
		return;
	}

	g_test_peer_path.valid = true;
	g_test_peer_path.known = true;
	memcpy(g_test_peer_path.public_key, public_key,
	       sizeof(g_test_peer_path.public_key));
	g_test_peer_path.path.has_out_path = true;
	g_test_peer_path.path.out_path_byte_len =
		(uint8_t)MIN((size_t)path_len,
			     sizeof(g_test_peer_path.path.out_path));
	if (g_test_peer_path.path.out_path_byte_len > 0U && path != NULL) {
		memcpy(g_test_peer_path.path.out_path, path,
		       g_test_peer_path.path.out_path_byte_len);
	}
	g_test_peer_path.path.path_hash_size =
		meshcore_hal_test_normalize_path_hash_size(path_hash_size);
}

void meshcore_hal_test_peer_path_unknown_set(const uint8_t *public_key)
{
	memset(&g_test_peer_path, 0, sizeof(g_test_peer_path));
	if (public_key == NULL) {
		return;
	}

	g_test_peer_path.valid = true;
	g_test_peer_path.known = false;
	memcpy(g_test_peer_path.public_key, public_key,
	       sizeof(g_test_peer_path.public_key));
}

void meshcore_hal_test_channel_secret_set(const uint8_t *secret, uint8_t secret_len)
{
	memset(&g_test_channel_secret, 0, sizeof(g_test_channel_secret));
	if (secret == NULL) {
		return;
	}

	g_test_channel_secret.valid = true;
	g_test_channel_secret.secret.secret_len =
		(uint8_t)MIN((size_t)secret_len,
			     sizeof(g_test_channel_secret.secret.secret));
	memcpy(g_test_channel_secret.secret.secret, secret,
	       g_test_channel_secret.secret.secret_len);
	(void)meshcore_channel_secret_hash(
		g_test_channel_secret.secret.secret,
		g_test_channel_secret.secret.secret_len,
		g_test_channel_secret.hash);
}

void meshcore_hal_test_node_identity_set(const char *name,
					 meshcore_common_node_role_t role,
					 const uint8_t *public_key,
					 const uint8_t *private_key)
{
	memset(&g_test_node.identity, 0, sizeof(g_test_node.identity));
	g_test_node.valid = true;
	if (name != NULL) {
		memcpy(g_test_node.identity.name, name,
		       MIN(strlen(name), sizeof(g_test_node.identity.name) - 1U));
	}
	g_test_node.identity.role = role;
	if (public_key != NULL) {
		memcpy(g_test_node.identity.public_key, public_key,
		       sizeof(g_test_node.identity.public_key));
	}
	if (private_key != NULL) {
		memcpy(g_test_node.identity.private_key, private_key,
		       sizeof(g_test_node.identity.private_key));
	}
}

void meshcore_hal_test_node_runtime_policy_set(
	const meshcore_common_node_runtime_policy_t *policy)
{
	if (policy == NULL) {
		memset(&g_test_node.policy, 0, sizeof(g_test_node.policy));
		return;
	}

	g_test_node.policy = *policy;
	g_test_node.policy.path_hash_size =
		meshcore_hal_test_normalize_path_hash_size(policy->path_hash_size);
}

void meshcore_hal_test_node_advert_profile_set(
	const meshcore_common_node_advert_profile_t *profile)
{
	if (profile == NULL) {
		memset(&g_test_node.advert_profile, 0,
		       sizeof(g_test_node.advert_profile));
		return;
	}

	g_test_node.advert_profile = *profile;
}

void meshcore_hal_test_publish_events_clear(void)
{
	memset(&g_test_publish_events, 0, sizeof(g_test_publish_events));
}

const meshcore_test_message_event_t *meshcore_hal_test_message_event_last(void)
{
	return &g_test_publish_events.message;
}

const meshcore_test_message_ack_event_t *meshcore_hal_test_ack_event_last(void)
{
	return &g_test_publish_events.ack;
}

const meshcore_test_peer_path_event_t *meshcore_hal_test_peer_path_event_last(void)
{
	return &g_test_publish_events.peer_path;
}

const meshcore_test_trace_event_t *meshcore_hal_test_trace_event_last(void)
{
	return &g_test_publish_events.trace;
}

const meshcore_test_telemetry_event_t *meshcore_hal_test_telemetry_event_last(void)
{
	return &g_test_publish_events.telemetry;
}

const meshcore_test_binary_request_event_t *
meshcore_hal_test_binary_request_event_last(void)
{
	return &g_test_publish_events.binary_request;
}

const meshcore_test_binary_response_event_t *
meshcore_hal_test_binary_response_event_last(void)
{
	return &g_test_publish_events.binary_response;
}

const meshcore_test_node_discover_event_t *
meshcore_hal_test_node_discover_event_last(void)
{
	return &g_test_publish_events.node_discover;
}

const meshcore_test_advert_event_t *meshcore_hal_test_advert_event_last(void)
{
	return &g_test_publish_events.advert;
}

bool meshcore_hal_test_message_event_take(meshcore_test_message_event_t *out)
{
	if (!g_test_publish_events.message_valid) {
		return false;
	}
	if (out != NULL) {
		*out = g_test_publish_events.message;
	}
	g_test_publish_events.message_valid = false;
	return true;
}

bool meshcore_hal_test_ack_event_take(meshcore_test_message_ack_event_t *out)
{
	if (!g_test_publish_events.ack_valid) {
		return false;
	}
	if (out != NULL) {
		*out = g_test_publish_events.ack;
	}
	g_test_publish_events.ack_valid = false;
	return true;
}

bool meshcore_hal_test_peer_path_event_take(meshcore_test_peer_path_event_t *out)
{
	if (!g_test_publish_events.peer_path_valid) {
		return false;
	}
	if (out != NULL) {
		*out = g_test_publish_events.peer_path;
	}
	g_test_publish_events.peer_path_valid = false;
	return true;
}

bool meshcore_hal_test_trace_event_take(meshcore_test_trace_event_t *out)
{
	if (!g_test_publish_events.trace_valid) {
		return false;
	}
	if (out != NULL) {
		*out = g_test_publish_events.trace;
	}
	g_test_publish_events.trace_valid = false;
	return true;
}

bool meshcore_hal_test_telemetry_event_take(meshcore_test_telemetry_event_t *out)
{
	if (!g_test_publish_events.telemetry_valid) {
		return false;
	}
	if (out != NULL) {
		*out = g_test_publish_events.telemetry;
	}
	g_test_publish_events.telemetry_valid = false;
	return true;
}

bool meshcore_hal_test_binary_request_event_take(
	meshcore_test_binary_request_event_t *out)
{
	if (!g_test_publish_events.binary_request_valid) {
		return false;
	}
	if (out != NULL) {
		*out = g_test_publish_events.binary_request;
	}
	g_test_publish_events.binary_request_valid = false;
	return true;
}

bool meshcore_hal_test_binary_response_event_take(
	meshcore_test_binary_response_event_t *out)
{
	if (!g_test_publish_events.binary_response_valid) {
		return false;
	}
	if (out != NULL) {
		*out = g_test_publish_events.binary_response;
	}
	g_test_publish_events.binary_response_valid = false;
	return true;
}

bool meshcore_hal_test_node_discover_event_take(
	meshcore_test_node_discover_event_t *out)
{
	if (!g_test_publish_events.node_discover_valid) {
		return false;
	}
	if (out != NULL) {
		*out = g_test_publish_events.node_discover;
	}
	g_test_publish_events.node_discover_valid = false;
	return true;
}

bool meshcore_hal_test_advert_event_take(meshcore_test_advert_event_t *out)
{
	if (!g_test_publish_events.advert_valid) {
		return false;
	}
	if (out != NULL) {
		*out = g_test_publish_events.advert;
	}
	g_test_publish_events.advert_valid = false;
	return true;
}

int meshcore_node_identity_get(meshcore_common_node_identity_t *out)
{
	if (out == NULL) {
		return -EINVAL;
	}
	if (!g_test_node.valid) {
		return -ENOENT;
	}

	*out = g_test_node.identity;
	return 0;
}

int meshcore_node_config_last_modify_get(uint32_t *out_timestamp)
{
	if (out_timestamp == NULL) {
		return -EINVAL;
	}

	*out_timestamp = g_test_rtc_current_time == 0U ? 1U : g_test_rtc_current_time;
	return 0;
}

int meshcore_node_runtime_policy_get(
meshcore_common_node_runtime_policy_t *out)
{
	if (out == NULL) {
		return -EINVAL;
	}
	if (!g_test_node.valid) {
		return -ENOENT;
	}

	*out = g_test_node.policy;
	return 0;
}

int meshcore_node_advert_profile_get(
meshcore_common_node_advert_profile_t *out)
{
	if (out == NULL) {
		return -EINVAL;
	}
	if (!g_test_node.valid) {
		return -ENOENT;
	}

	*out = g_test_node.advert_profile;
	return 0;
}

int meshcore_peer_path_get_by_key(const uint8_t *public_key,
				      meshcore_common_peer_path_t *out)
{
	if (public_key == NULL || out == NULL) {
		return -EINVAL;
	}
	if (g_test_peer_path.valid &&
	    memcmp(g_test_peer_path.public_key, public_key,
		   sizeof(g_test_peer_path.public_key)) == 0) {
		if (!g_test_peer_path.known) {
			memset(out, 0, sizeof(*out));
			return 0;
		}
		*out = g_test_peer_path.path;
		return 0;
	}
	return -ENOENT;
}

int meshcore_channel_secret_get_by_hash(uint8_t channel_hash,
					    meshcore_common_channel_secret_t *out,
					    size_t out_capacity)
{
	if (out == NULL || out_capacity == 0U) {
		return -EINVAL;
	}
	if (g_test_channel_secret.valid &&
	    g_test_channel_secret.hash[0] == channel_hash) {
		memset(out, 0, out_capacity * sizeof(*out));
		out[0] = g_test_channel_secret.secret;
		return 1;
	}
	return -ENOENT;
}

int meshcore_channel_secret_match_exists(uint8_t channel_hash,
						const uint8_t *secret,
						size_t secret_len)
{
	meshcore_common_channel_secret_t matches[4];
	int count;

	if (secret == NULL ||
	    (secret_len != MESHCORE_CHANNEL_SECRET_LEN_16 &&
	     secret_len != MESHCORE_CHANNEL_SECRET_LEN_32)) {
		return -EINVAL;
	}

	count = meshcore_channel_secret_get_by_hash(channel_hash, matches,
							   ARRAY_SIZE(matches));
	if (count <= 0) {
		return count;
	}

	for (int i = 0; i < count; i++) {
		if (matches[i].secret_len == secret_len &&
		    memcmp(matches[i].secret, secret, secret_len) == 0) {
			return 1;
		}
	}

	return 0;
}

int meshcore_channel_secret_hash(const uint8_t *secret, size_t secret_len,
					 uint8_t *out_hash)
{
	if (secret == NULL || out_hash == NULL) {
		return -EINVAL;
	}
	return meshcore_hal_sha256(out_hash, MESHCORE_CHANNEL_HASH_BYTES, secret,
				   (int)secret_len) ?
		       0 :
		       -EINVAL;
}

int meshcore_peer_identity_get_by_prefix(const uint8_t *key_prefix,
					     meshcore_common_peer_identity_t *out)
{
	if (key_prefix == NULL || out == NULL) {
		return -EINVAL;
	}
	if (g_test_peer_identity.valid &&
	    memcmp(g_test_peer_identity.public_key, key_prefix,
		   MESHCORE_NODE_KEY_PREFIX_BYTES) == 0) {
		memset(out, 0, sizeof(*out));
		memcpy(out->name, g_test_peer_identity.name, sizeof(out->name) - 1U);
		out->role = g_test_peer_identity.role;
		out->flags = g_test_peer_identity.flags;
		memcpy(out->public_key, g_test_peer_identity.public_key,
		       sizeof(out->public_key));
		return 0;
	}
	return -ENOENT;
}

int meshcore_message_handler(const meshcore_common_message_t *message)
{
	if (message == NULL) {
		return -EINVAL;
	}

	memset(&g_test_publish_events.message, 0,
	       sizeof(g_test_publish_events.message));
	g_test_publish_events.message.type = message->type;
	g_test_publish_events.message.route = message->route;
	memcpy(g_test_publish_events.message.target, message->target,
	       MIN(sizeof(g_test_publish_events.message.target),
		   sizeof(message->target)));
	memcpy(g_test_publish_events.message.sender_name, message->sender_name,
	       sizeof(g_test_publish_events.message.sender_name) - 1U);
	g_test_publish_events.message.payload_len =
		(uint16_t)MIN((size_t)message->payload_len,
			      sizeof(g_test_publish_events.message.payload));
	if (g_test_publish_events.message.payload_len > 0U) {
		memcpy(g_test_publish_events.message.payload, message->payload,
		       g_test_publish_events.message.payload_len);
	}
	g_test_publish_events.message.sender_timestamp = message->sender_timestamp;
	g_test_publish_events.message.has_rx_snr = message->has_rx_snr;
	g_test_publish_events.message.rx_snr = message->rx_snr;
	g_test_publish_events.message_valid = true;
	return 0;
}

int meshcore_peer_seen_update(const uint8_t *public_key, bool has_snr,
				     int8_t snr_q4)
{
	if (public_key == NULL) {
		return -EINVAL;
	}
	ARG_UNUSED(has_snr);
	ARG_UNUSED(snr_q4);
	return 0;
}

meshcore_hal_test_dispatcher_script_t *meshcore_hal_test_dispatcher_script_get(void)
{
	return &g_test_dispatcher_script;
}

meshcore_hal_test_mesh_script_t *meshcore_hal_test_mesh_script_get(void)
{
	return &g_test_mesh_script;
}

void meshcore_hal_test_dispatcher_script_mirror_set(
	meshcore_hal_test_dispatcher_script_t *mirror)
{
	g_test_dispatcher_script_mirror = mirror;
	meshcore_hal_test_dispatcher_script_sync_mirror();
}

void meshcore_hal_test_mesh_script_mirror_set(
	meshcore_hal_test_mesh_script_t *mirror)
{
	g_test_mesh_script_mirror = mirror;
	meshcore_hal_test_mesh_script_sync_mirror();
}

void meshcore_hal_test_radio_reset(void)
{
	memset(&g_test_radio, 0, sizeof(g_test_radio));
	g_test_radio.send_delay_per_byte_ms = 1U;
	g_test_radio.send_result = true;
	g_test_radio.last_rssi = -50.0f;
	g_test_radio.last_snr = 10.0f;
}

void meshcore_dispatcher_log_rx_raw(float snr, float rssi,
					const uint8_t raw[], int len)
{
	(void)snr;
	(void)rssi;
	(void)raw;
	(void)len;
	g_test_dispatcher_script.log_rx_raw_count++;
	meshcore_hal_test_dispatcher_script_sync_mirror();
}

void meshcore_dispatcher_log_rx(const meshcore_common_packet_view_t *packet,
				int len, float score)
{
	(void)packet;
	(void)len;
	(void)score;
	g_test_dispatcher_script.log_rx_count++;
	meshcore_hal_test_dispatcher_script_sync_mirror();
}

void meshcore_dispatcher_log_tx(const meshcore_common_packet_view_t *packet,
				int len)
{
	(void)packet;
	(void)len;
	g_test_dispatcher_script.log_tx_count++;
	meshcore_hal_test_dispatcher_script_sync_mirror();
}

void meshcore_dispatcher_log_tx_fail(
	const meshcore_common_packet_view_t *packet, int len)
{
	(void)packet;
	(void)len;
	g_test_dispatcher_script.log_tx_fail_count++;
	meshcore_hal_test_dispatcher_script_sync_mirror();
}

void meshcore_runtime_request_error(uint8_t request_type, int err_code)
{
	ARG_UNUSED(request_type);
	ARG_UNUSED(err_code);
}

float meshcore_dispatcher_get_airtime_budget_factor(void)
{
	if (!g_test_dispatcher_script.override_airtime_budget_factor) {
		return 1.0f;
	}

	return g_test_dispatcher_script.airtime_budget_factor;
}

int meshcore_dispatcher_calc_rx_delay(float score, uint32_t air_time)
{
	if (!g_test_dispatcher_script.override_rx_delay_ms) {
		return (int)((powf(10.0f, 0.85f - score) - 1.0f) * (float)air_time);
	}

	return g_test_dispatcher_script.rx_delay_ms;
}

uint32_t meshcore_dispatcher_get_cad_fail_max_duration(void)
{
	return g_test_dispatcher_script.cad_fail_max_duration_ms;
}

int meshcore_dispatcher_get_interference_threshold(void)
{
	return g_test_dispatcher_script.interference_threshold;
}

int meshcore_dispatcher_get_agc_reset_interval(void)
{
	return g_test_dispatcher_script.agc_reset_interval_ms;
}

unsigned long meshcore_dispatcher_get_duty_cycle_window_ms(void)
{
	return g_test_dispatcher_script.duty_cycle_window_ms;
}

void meshcore_hal_test_rng_set_bytes(const uint8_t *src, size_t len)
{
	if (src == NULL || len == 0U || len > sizeof(g_test_rng.bytes)) {
		memset(&g_test_rng, 0, sizeof(g_test_rng));
		return;
	}

	memcpy(g_test_rng.bytes, src, len);
	g_test_rng.len = len;
	g_test_rng.idx = 0U;
	g_test_rng.active = true;
}

void meshcore_hal_test_rng_clear(void)
{
	memset(&g_test_rng, 0, sizeof(g_test_rng));
}

void meshcore_hal_test_millis_set(unsigned long value)
{
	g_test_millis.active = true;
	g_test_millis.value = value;
}

void meshcore_hal_test_millis_advance(unsigned long delta)
{
	g_test_millis.active = true;
	g_test_millis.value += delta;
}

void meshcore_hal_test_millis_clear(void)
{
	memset(&g_test_millis, 0, sizeof(g_test_millis));
}

void meshcore_hal_test_timer_reset(void)
{
	memset(&g_test_platform_timer, 0, sizeof(g_test_platform_timer));
}

uint32_t meshcore_hal_test_timer_arm_count_get(void)
{
	return g_test_platform_timer.arm_count;
}

uint32_t meshcore_hal_test_timer_cancel_count_get(void)
{
	return g_test_platform_timer.cancel_count;
}

uint32_t meshcore_hal_test_timer_last_deadline_get(void)
{
	return g_test_platform_timer.last_deadline_ms;
}

void meshcore_hal_test_rtc_set_current_time(uint32_t value)
{
	g_test_rtc_current_time = value;
}

void meshcore_hal_test_rtc_advance(uint32_t delta)
{
	g_test_rtc_current_time += delta;
}

uint32_t meshcore_hal_test_rtc_get_current_time(void)
{
	return g_test_rtc_current_time;
}

bool meshcore_hal_test_radio_inject_receive(const uint8_t *data, int len,
					    int16_t rssi_dbm, int8_t snr_db,
					    uint32_t delay_ms)
{
	struct meshcore_hal_test_radio_packet *packet;
	int tail;

	if (data == NULL || len <= 0 || len > (int)MESHCORE_MAX_TRANS_UNIT_LEN ||
	    g_test_radio.rx_count >= MESHCORE_HAL_TEST_RADIO_QUEUE_CAP) {
		return false;
	}

	tail = g_test_radio.rx_tail;
	packet = &g_test_radio.rx_queue[tail];
	memset(packet, 0, sizeof(*packet));
	memcpy(packet->data, data, (size_t)len);
	packet->len = len;
	packet->rssi_dbm = rssi_dbm;
	packet->snr_db = snr_db;
	packet->ready_at_ms = meshcore_hal_millis_get() + delay_ms;

	g_test_radio.rx_tail =
		(g_test_radio.rx_tail + 1) % MESHCORE_HAL_TEST_RADIO_QUEUE_CAP;
	g_test_radio.rx_count++;
	return true;
}

void meshcore_hal_test_radio_set_send_delay_per_byte(uint32_t per_byte_ms)
{
	g_test_radio.send_delay_per_byte_ms = per_byte_ms == 0U ? 1U : per_byte_ms;
}

void meshcore_hal_test_radio_set_send_result(bool success)
{
	g_test_radio.send_result = success;
}

void meshcore_hal_test_radio_set_send_never_complete(bool enabled)
{
	g_test_radio.send_never_complete = enabled;
}

void meshcore_hal_test_radio_force_in_rx_mode(bool enabled, bool value)
{
	g_test_radio.force_in_rx_mode_enabled = enabled;
	g_test_radio.force_in_rx_mode_value = value;
}

void meshcore_hal_test_radio_force_receiving(bool enabled, bool value)
{
	g_test_radio.force_receiving_enabled = enabled;
	g_test_radio.force_receiving_value = value;
}

uint32_t meshcore_hal_test_radio_get_packets_recv(void)
{
	return g_test_radio.packets_recv;
}

uint32_t meshcore_hal_test_radio_get_packets_sent(void)
{
	return g_test_radio.packets_sent;
}

uint32_t meshcore_hal_test_radio_get_noise_floor_calibrate_count(void)
{
	return g_test_radio.noise_floor_calibrate_count;
}

uint32_t meshcore_hal_test_radio_get_agc_reset_count(void)
{
	return g_test_radio.agc_reset_count;
}

uint32_t meshcore_hal_test_radio_get_on_send_finished_count(void)
{
	return g_test_radio.on_send_finished_count;
}

int meshcore_hal_test_radio_get_last_send_len(void)
{
	return g_test_radio.last_send_len;
}

int meshcore_hal_test_radio_get_last_send(uint8_t *out, size_t capacity)
{
	size_t copy_len;

	if (out == NULL || capacity == 0U || g_test_radio.last_send_len <= 0) {
		return 0;
	}

	copy_len = MIN((size_t)g_test_radio.last_send_len, capacity);
	memcpy(out, g_test_radio.last_send_data, copy_len);
	return (int)copy_len;
}

unsigned long meshcore_hal_millis_get(void)
{
	if (meshcore_clock_millis_override_is_enabled()) {
		return meshcore_clock_millis_override_value_get();
	}
	if (g_test_millis.active) {
		return g_test_millis.value;
	}

	return k_uptime_get_32();
}

void meshcore_hal_radio_begin(void)
{
	meshcore_hal_test_radio_reset();
}

int meshcore_hal_test_radio_recv_raw(uint8_t *data, size_t capacity)
{
	struct meshcore_hal_test_radio_packet *packet;
	unsigned long now = meshcore_hal_millis_get();
	int copy_len;

	if (data == NULL || capacity == 0U) {
		return 0;
	}

	packet = meshcore_hal_test_radio_peek_ready(now);
	if (packet == NULL) {
		return 0;
	}

	copy_len = packet->len;
	if ((size_t)copy_len > capacity) {
		copy_len = (int)capacity;
	}
	memcpy(data, packet->data, (size_t)copy_len);
	g_test_radio.last_rssi = (float)packet->rssi_dbm;
	g_test_radio.last_snr = (float)packet->snr_db;
	g_test_radio.packets_recv++;
	meshcore_hal_test_radio_pop_ready();
	return copy_len;
}

uint32_t meshcore_hal_radio_airtime(size_t len)
{
	return (uint32_t)len * g_test_radio.send_delay_per_byte_ms;
}

float meshcore_hal_radio_packet_score(int8_t snr_q4, size_t len)
{
	return meshcore_hal_test_radio_packet_score_internal(
		((float)snr_q4) / 4.0f, 10, (int)len);
}

bool meshcore_hal_test_radio_tx_complete_get(void)
{
	unsigned long now = meshcore_hal_millis_get();

	if (!g_test_radio.send_in_progress) {
		return true;
	}
	if (g_test_radio.send_never_complete) {
		return false;
	}
	if (!g_test_radio.send_completed &&
	    (long)(now - g_test_radio.send_done_at_ms) >= 0) {
		g_test_radio.send_completed = true;
	}
	return g_test_radio.send_completed;
}

bool meshcore_hal_radio_in_rx_mode_get(void)
{
	if (g_test_radio.force_in_rx_mode_enabled) {
		return g_test_radio.force_in_rx_mode_value;
	}
	return true;
}

bool meshcore_hal_radio_receiving_get(void)
{
	if (g_test_radio.force_receiving_enabled) {
		return g_test_radio.force_receiving_value;
	}
	return meshcore_hal_radio_channel_active_get();
}

bool meshcore_hal_radio_channel_active_get(void)
{
	unsigned long now = meshcore_hal_millis_get();

	if (g_test_radio.force_receiving_enabled &&
	    g_test_radio.force_receiving_value) {
		return true;
	}
	return meshcore_hal_test_radio_peek_ready(now) != NULL;
}

float meshcore_hal_test_radio_last_rssi_get(void)
{
	return g_test_radio.last_rssi;
}

float meshcore_hal_test_radio_last_snr_get(void)
{
	return g_test_radio.last_snr;
}

bool meshcore_hal_radio_frequency_get(uint64_t *freq_hz)
{
	if (freq_hz == NULL) {
		return false;
	}

	*freq_hz = MESHCORE_HAL_TEST_RADIO_FREQUENCY_HZ;
	return true;
}

void meshcore_hal_radio_noise_floor_calibrate(int threshold)
{
	(void)threshold;
	g_test_radio.noise_floor_calibrate_count++;
}

void meshcore_hal_radio_agc_reset(void)
{
	g_test_radio.agc_reset_count++;
}

void meshcore_hal_test_radio_loop(void)
{
}

int meshcore_hal_radio_packet_send(const uint8_t *data, size_t len)
{
	if (data == NULL || len == 0U || !g_test_radio.send_result) {
		return 0;
	}

	g_test_radio.last_send_len = (int)len;
	if (len <= sizeof(g_test_radio.last_send_data)) {
		memcpy(g_test_radio.last_send_data, data, len);
	}
	g_test_radio.send_in_progress = true;
	g_test_radio.send_completed = false;
	g_test_radio.send_done_at_ms =
		meshcore_hal_millis_get() +
		(unsigned long)(g_test_radio.send_delay_per_byte_ms * len);
	return 1;
}

uint32_t meshcore_mesh_get_cad_fail_retry_delay(void)
{
	uint32_t value;

	if (g_test_mesh_script.override_get_cad_fail_retry_delay) {
		return g_test_mesh_script.get_cad_fail_retry_delay_value;
	}

	meshcore_hal_rng_random((uint8_t *)&value, sizeof(value));
	return (value % 3U + 1U) * 120U;
}

bool meshcore_mesh_filter_recv_flood_packet(
	const meshcore_common_packet_view_t *packet)
{
	(void)packet;
	if (g_test_mesh_script.override_filter_recv_flood_packet) {
		return g_test_mesh_script.filter_recv_flood_packet_value;
	}
	return false;
}

bool meshcore_mesh_allow_packet_forward(
	const meshcore_common_packet_view_t *packet)
{
	(void)packet;
	if (g_test_mesh_script.override_allow_packet_forward) {
		return g_test_mesh_script.allow_packet_forward_value;
	}
	return false;
}

uint32_t meshcore_mesh_get_retransmit_delay(
	const meshcore_common_packet_view_t *packet)
{
	uint32_t value;
	uint32_t t;

	if (g_test_mesh_script.override_get_retransmit_delay) {
		return g_test_mesh_script.get_retransmit_delay_value;
	}
	if (packet == NULL) {
		return 0U;
	}

	t = (meshcore_hal_radio_airtime((size_t)packet->raw_len) * 52U / 50U) /
	    2U;
	meshcore_hal_rng_random((uint8_t *)&value, sizeof(value));
	return (value % 5U) * t;
}

uint32_t meshcore_mesh_get_direct_retransmit_delay(
	const meshcore_common_packet_view_t *packet)
{
	(void)packet;
	if (g_test_mesh_script.override_get_direct_retransmit_delay) {
		return g_test_mesh_script.get_direct_retransmit_delay_value;
	}
	return 0U;
}

uint8_t meshcore_mesh_get_extra_ack_transmit_count(void)
{
	if (g_test_mesh_script.override_get_extra_ack_transmit_count) {
		return g_test_mesh_script.get_extra_ack_transmit_count_value;
	}
	return 0U;
}

int meshcore_mesh_next_peer_shared_secret_by_hash(const uint8_t *hash, size_t start_slot,
						  size_t *slot_id, uint8_t *dest_secret,
						  meshcore_common_peer_identity_t *peer_identity)
{
	(void)hash;
	if (slot_id == NULL || dest_secret == NULL || peer_identity == NULL) {
		return -EINVAL;
	}

	if (!g_test_mesh_script.override_search_peers_by_hash ||
	    start_slot >= (size_t)g_test_mesh_script.search_peers_by_hash_value) {
		memset(dest_secret, 0, MESHCORE_PUBLIC_KEY_SIZE);
		memset(peer_identity, 0, sizeof(*peer_identity));
		return -ENOENT;
	}

	*slot_id = start_slot;
	memset(peer_identity, 0, sizeof(*peer_identity));
	if (g_test_peer_identity.valid) {
		memcpy(peer_identity->name, g_test_peer_identity.name,
		       sizeof(peer_identity->name) - 1U);
		peer_identity->role = g_test_peer_identity.role;
		peer_identity->flags = g_test_peer_identity.flags;
		memcpy(peer_identity->public_key, g_test_peer_identity.public_key,
		       sizeof(peer_identity->public_key));
	}
	if (g_test_mesh_script.override_get_peer_shared_secret) {
		memcpy(dest_secret, g_test_mesh_script.peer_shared_secret,
		       sizeof(g_test_mesh_script.peer_shared_secret));
	} else {
		memset(dest_secret, 0, MESHCORE_PUBLIC_KEY_SIZE);
	}

	return 0;
}

int meshcore_mesh_search_channels_by_hash(
	const uint8_t *hash, meshcore_common_channel_view_t channels[],
	int max_matches)
{
	(void)hash;
	if (g_test_mesh_script.override_search_channels_fill && channels != NULL &&
	    max_matches > 0) {
		memcpy(channels[0].hash, g_test_mesh_script.search_channel_hash,
		       sizeof(channels[0].hash));
		memcpy(channels[0].secret, g_test_mesh_script.search_channel_secret,
		       sizeof(channels[0].secret));
	}
	if (g_test_mesh_script.override_search_channels_by_hash) {
		return g_test_mesh_script.search_channels_by_hash_value;
	}
	return 0;
}

int meshcore_message_ack_handler(const uint8_t *target, uint8_t attempt)
{
	if (target == NULL) {
		return -EINVAL;
	}

	memset(&g_test_publish_events.ack, 0, sizeof(g_test_publish_events.ack));
	memcpy(g_test_publish_events.ack.target, target,
	       sizeof(g_test_publish_events.ack.target));
	g_test_publish_events.ack.attempt = attempt;
	g_test_publish_events.ack_valid = true;
	return 0;
}

int meshcore_peer_path_publish(
	const meshcore_common_peer_path_event_t *peer_path, bool is_discover)
{
	if (peer_path == NULL) {
		return -EINVAL;
	}

	memset(&g_test_publish_events.peer_path, 0,
	       sizeof(g_test_publish_events.peer_path));
	g_test_publish_events.peer_path.is_discover = is_discover;
	g_test_publish_events.peer_path.tag = peer_path->tag;
	memcpy(g_test_publish_events.peer_path.key_prefix, peer_path->key_prefix,
	       sizeof(g_test_publish_events.peer_path.key_prefix));
	g_test_publish_events.peer_path.timestamp = peer_path->timestamp;
	g_test_publish_events.peer_path.has_response_snr = true;
	g_test_publish_events.peer_path.response_snr = peer_path->last_seen_snr;
	g_test_publish_events.peer_path.has_out_path = true;
	g_test_publish_events.peer_path.out_path_len = (uint8_t)MIN(
		(size_t)peer_path->out_path_len,
		sizeof(g_test_publish_events.peer_path.out_path));
	g_test_publish_events.peer_path.path_hash_size = peer_path->path_hash_size;
	if (g_test_publish_events.peer_path.out_path_len > 0U) {
		memcpy(g_test_publish_events.peer_path.out_path,
		       peer_path->out_path,
		       g_test_publish_events.peer_path.out_path_len);
	}
	g_test_publish_events.peer_path.out_path_snr_count = (uint8_t)MIN(
		(size_t)peer_path->out_path_snr_count,
		sizeof(g_test_publish_events.peer_path.out_path_snr));
	if (g_test_publish_events.peer_path.out_path_snr_count > 0U) {
		memcpy(g_test_publish_events.peer_path.out_path_snr,
		       peer_path->out_path_snr,
		       g_test_publish_events.peer_path.out_path_snr_count);
	}
	g_test_publish_events.peer_path.return_path_snr_count = (uint8_t)MIN(
		(size_t)peer_path->return_path_snr_count,
		sizeof(g_test_publish_events.peer_path.return_path_snr));
	if (g_test_publish_events.peer_path.return_path_snr_count > 0U) {
		memcpy(g_test_publish_events.peer_path.return_path_snr,
		       peer_path->return_path_snr,
		       g_test_publish_events.peer_path.return_path_snr_count);
	}
	g_test_publish_events.peer_path_valid = true;
	return 0;
}

int meshcore_peer_path_handler(
	const meshcore_common_peer_path_event_t *peer_path)
{
	return meshcore_peer_path_publish(peer_path, false);
}

int meshcore_trace_path_handler(uint8_t state, uint32_t tag,
				    const int8_t *out_path_snr,
				    uint8_t out_count,
				    const int8_t *return_path_snr,
				    uint8_t return_count,
				    bool has_response_snr,
				    int8_t response_snr,
				    uint32_t timestamp)
{
	memset(&g_test_publish_events.trace, 0,
	       sizeof(g_test_publish_events.trace));
	g_test_publish_events.trace.timestamp = timestamp;
	g_test_publish_events.trace.tag = tag;
	g_test_publish_events.trace.state = state;
	g_test_publish_events.trace.has_response_snr = has_response_snr;
	g_test_publish_events.trace.response_snr = response_snr;
	g_test_publish_events.trace.out_path_snr_count =
		(uint8_t)MIN((size_t)out_count,
			     sizeof(g_test_publish_events.trace.out_path_snr));
	if (g_test_publish_events.trace.out_path_snr_count > 0U &&
	    out_path_snr != NULL) {
		memcpy(g_test_publish_events.trace.out_path_snr, out_path_snr,
		       g_test_publish_events.trace.out_path_snr_count);
	}
	g_test_publish_events.trace.return_path_snr_count =
		(uint8_t)MIN((size_t)return_count,
			     sizeof(g_test_publish_events.trace.return_path_snr));
	if (g_test_publish_events.trace.return_path_snr_count > 0U &&
	    return_path_snr != NULL) {
		memcpy(g_test_publish_events.trace.return_path_snr, return_path_snr,
		       g_test_publish_events.trace.return_path_snr_count);
	}
	g_test_publish_events.trace_valid = true;
	return 0;
}

int meshcore_telemetry_handler(const uint8_t *key_prefix,
						   uint32_t timestamp,
						   uint32_t tag,
						   const uint8_t *payload,
						   size_t payload_len)
{
	if (key_prefix == NULL || payload == NULL ||
	    payload_len > sizeof(g_test_publish_events.telemetry.payload)) {
		return -EINVAL;
	}

	memset(&g_test_publish_events.telemetry, 0,
	       sizeof(g_test_publish_events.telemetry));
	memcpy(g_test_publish_events.telemetry.key_prefix, key_prefix,
	       sizeof(g_test_publish_events.telemetry.key_prefix));
	g_test_publish_events.telemetry.timestamp = timestamp;
	g_test_publish_events.telemetry.tag = tag;
	g_test_publish_events.telemetry.payload_len = (uint8_t)payload_len;
	if (payload_len > 0U) {
		memcpy(g_test_publish_events.telemetry.payload, payload, payload_len);
	}
	g_test_publish_events.telemetry_valid = true;
	return 0;
}

int meshcore_binary_request_handler(
	const meshcore_common_binary_request_event_t *event)
{
	if (event == NULL) {
		return -EINVAL;
	}

	g_test_publish_events.binary_request = *event;
	g_test_publish_events.binary_request_valid = true;
	return 0;
}

int meshcore_binary_response_handler(const uint8_t *key_prefix,
				     uint32_t timestamp, uint32_t tag,
				     const uint8_t *payload,
				     size_t payload_len)
{
	if (key_prefix == NULL || (payload == NULL && payload_len > 0U) ||
	    payload_len > MESHCORE_MAX_SERVICE_RESPONSE_PAYLOAD_LEN) {
		return -EINVAL;
	}

	memset(&g_test_publish_events.binary_response, 0,
	       sizeof(g_test_publish_events.binary_response));
	memcpy(g_test_publish_events.binary_response.key_prefix, key_prefix,
	       sizeof(g_test_publish_events.binary_response.key_prefix));
	g_test_publish_events.binary_response.timestamp = timestamp;
	g_test_publish_events.binary_response.tag = tag;
	g_test_publish_events.binary_response.payload_len = (uint8_t)payload_len;
	if (payload_len > 0U) {
		memcpy(g_test_publish_events.binary_response.payload, payload,
		       payload_len);
	}
	g_test_publish_events.binary_response_valid = true;
	return 0;
}

int meshcore_node_discover_handler(
	const meshcore_common_node_discover_event_t *event)
{
	if (event == NULL ||
	    event->public_key_len > sizeof(g_test_publish_events.node_discover.public_key) ||
	    event->path_len > sizeof(g_test_publish_events.node_discover.path)) {
		return -EINVAL;
	}

	memset(&g_test_publish_events.node_discover, 0,
	       sizeof(g_test_publish_events.node_discover));
	g_test_publish_events.node_discover.role = event->role;
	g_test_publish_events.node_discover.tag = event->tag;
	g_test_publish_events.node_discover.public_key_len = event->public_key_len;
	if (event->public_key_len > 0U) {
		memcpy(g_test_publish_events.node_discover.public_key,
		       event->public_key, event->public_key_len);
	}
	g_test_publish_events.node_discover.path_len = event->path_len;
	if (event->path_len > 0U) {
		memcpy(g_test_publish_events.node_discover.path, event->path,
		       event->path_len);
	}
	g_test_publish_events.node_discover.uplink_snr = event->uplink_snr;
	g_test_publish_events.node_discover.downlink_snr = event->downlink_snr;
	g_test_publish_events.node_discover_valid = true;
	return 0;
}

int meshcore_channel_data_handler(
	const meshcore_common_channel_data_event_t *event)
{
	if (event == NULL ||
	    event->payload_len > MESHCORE_MAX_CHANNEL_DATA_PAYLOAD_LEN) {
		return -EINVAL;
	}

	return 0;
}

int meshcore_raw_data_handler(const meshcore_common_raw_data_event_t *event)
{
	if (event == NULL || event->payload_len == 0U ||
	    event->payload_len > MESHCORE_MAX_RAW_DATA_PAYLOAD_LEN) {
		return -EINVAL;
	}

	return 0;
}

int meshcore_control_data_handler(
	const meshcore_common_control_data_event_t *event)
{
	if (event == NULL || event->payload_len == 0U ||
	    event->payload_len > MESHCORE_MAX_CONTROL_DATA_PAYLOAD_LEN) {
		return -EINVAL;
	}

	return 0;
}

int meshcore_hal_node_telemetry_get(
	const meshcore_platform_request_source_t *requester,
	uint8_t permission_mask, meshcore_platform_telemetry_payload_t *out)
{
	ARG_UNUSED(requester);

	if (out == NULL) {
		return -EINVAL;
	}

	memset(out, 0, sizeof(*out));
	out->payload[0] = permission_mask;
	out->payload_len = 1U;
	if ((permission_mask & MESHCORE_TELEM_PERM_LOCATION) != 0U) {
		out->has_latitude = true;
		out->latitude = 31000000;
		out->has_longitude = true;
		out->longitude = 121000000;
	}
	return 0;
}

void meshcore_mesh_on_peer_data_recv(
	const meshcore_common_packet_view_t *packet, uint8_t type,
	const meshcore_common_peer_identity_t *sender, const uint8_t *secret,
	uint8_t *data, size_t len)
{
	(void)packet;
	(void)sender;
	(void)secret;
	(void)data;
	g_test_mesh_script.on_peer_data_recv_count++;
	g_test_mesh_script.last_peer_data_type = type;
	g_test_mesh_script.last_peer_data_len = (uint16_t)len;
	meshcore_hal_test_mesh_script_sync_mirror();
}

void meshcore_mesh_on_trace_recv(
	const meshcore_common_packet_view_t *packet, uint32_t tag,
	uint32_t auth_code, uint8_t flags, const uint8_t *path_snrs,
	const uint8_t *path_hashes, uint8_t path_len)
{
	(void)packet;
	(void)path_snrs;
	(void)path_hashes;
	(void)path_len;
	g_test_mesh_script.on_trace_recv_count++;
	g_test_mesh_script.last_trace_tag = tag;
	g_test_mesh_script.last_trace_auth_code = auth_code;
	g_test_mesh_script.last_trace_flags = flags;
	meshcore_hal_test_mesh_script_sync_mirror();
}

bool meshcore_mesh_on_peer_path_recv(
	const meshcore_common_packet_view_t *packet,
	const meshcore_common_peer_identity_t *sender, const uint8_t *secret,
	uint8_t *path, uint8_t path_len, uint8_t extra_type, uint8_t *extra,
	uint8_t extra_len)
{
	(void)packet;
	(void)sender;
	(void)secret;
	(void)path;
	(void)extra;
	g_test_mesh_script.on_peer_path_recv_count++;
	g_test_mesh_script.last_peer_path_len = path_len;
	g_test_mesh_script.last_peer_path_extra_type = extra_type;
	g_test_mesh_script.last_peer_path_extra_len = extra_len;
	meshcore_hal_test_mesh_script_sync_mirror();
	if (g_test_mesh_script.override_on_peer_path_recv) {
		return g_test_mesh_script.on_peer_path_recv_value;
	}
	return false;
}

void meshcore_mesh_on_advert_recv(
	const meshcore_common_packet_view_t *packet,
	const meshcore_common_identity_view_t *identity, uint32_t timestamp,
	const uint8_t *app_data, size_t app_data_len)
{
	struct meshcore_advert_data_parser parser;

	memset(&g_test_publish_events.advert, 0,
	       sizeof(g_test_publish_events.advert));
	g_test_publish_events.advert.advert_timestamp = timestamp;
	if (identity != NULL) {
		memcpy(g_test_publish_events.advert.public_key, identity->public_key,
		       sizeof(g_test_publish_events.advert.public_key));
	}
	if (packet != NULL) {
		g_test_publish_events.advert.has_out_path =
			packet->path_len > 0U;
		g_test_publish_events.advert.out_path_len =
			MIN(packet->path_byte_len,
			    sizeof(g_test_publish_events.advert.out_path));
		g_test_publish_events.advert.path_hash_size = packet->path_hash_size;
		if (g_test_publish_events.advert.out_path_len > 0U) {
			memcpy(g_test_publish_events.advert.out_path, packet->path,
			       g_test_publish_events.advert.out_path_len);
		}
	}
	if (app_data != NULL && app_data_len > 0U) {
		g_test_publish_events.advert.raw_advert_len =
			(uint8_t)MIN(app_data_len,
				     sizeof(g_test_publish_events.advert.raw_advert));
		memcpy(g_test_publish_events.advert.raw_advert, app_data,
		       g_test_publish_events.advert.raw_advert_len);
		meshcore_advert_data_parser_init(
			&parser, app_data,
			g_test_publish_events.advert.raw_advert_len);
		if (meshcore_advert_data_parser_is_valid(&parser)) {
			g_test_publish_events.advert.role =
				(meshcore_common_node_role_t)
					meshcore_advert_data_parser_get_type(&parser);
			if (meshcore_advert_data_parser_has_name(&parser)) {
				(void)snprintf(
					g_test_publish_events.advert.name,
					sizeof(g_test_publish_events.advert.name),
					"%s",
					meshcore_advert_data_parser_get_name(&parser));
			}
			if (meshcore_advert_data_parser_has_lat_lon(&parser)) {
				g_test_publish_events.advert.has_position = true;
				g_test_publish_events.advert.latitude =
					meshcore_advert_data_parser_get_int_lat(&parser);
				g_test_publish_events.advert.longitude =
					meshcore_advert_data_parser_get_int_lon(&parser);
			}
		}
	}
	g_test_publish_events.advert_valid = true;

	g_test_mesh_script.on_advert_recv_count++;
	g_test_mesh_script.last_advert_timestamp = timestamp;
	if (identity != NULL) {
		memcpy(g_test_mesh_script.last_advert_public_key,
		       identity->public_key,
		       sizeof(g_test_mesh_script.last_advert_public_key));
	}
	if (packet != NULL) {
		g_test_mesh_script.last_advert_path_len = packet->path_len;
	}
	g_test_mesh_script.last_advert_app_data_len =
		(uint16_t)MIN(app_data_len, sizeof(g_test_mesh_script.last_advert_app_data));
	if (app_data != NULL && g_test_mesh_script.last_advert_app_data_len > 0U) {
		memcpy(g_test_mesh_script.last_advert_app_data, app_data,
		       g_test_mesh_script.last_advert_app_data_len);
	}
	meshcore_hal_test_mesh_script_sync_mirror();
}

void meshcore_mesh_on_anon_data_recv(
	const meshcore_common_packet_view_t *packet, const uint8_t *secret,
	const meshcore_common_identity_view_t *sender, uint8_t *data, size_t len)
{
	(void)packet;
	(void)secret;
	(void)sender;
	(void)data;
	g_test_mesh_script.on_anon_data_recv_count++;
	g_test_mesh_script.last_anon_data_len = (uint16_t)len;
	meshcore_hal_test_mesh_script_sync_mirror();
}

void meshcore_mesh_on_path_recv(
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

void meshcore_mesh_on_control_data_recv(
	const meshcore_common_packet_view_t *packet)
{
	(void)packet;
	g_test_mesh_script.on_control_data_recv_count++;
	meshcore_hal_test_mesh_script_sync_mirror();
}

void meshcore_mesh_on_raw_data_recv(const meshcore_common_packet_view_t *packet)
{
	(void)packet;
	g_test_mesh_script.on_raw_data_recv_count++;
	meshcore_hal_test_mesh_script_sync_mirror();
}

void meshcore_mesh_on_group_data_recv(
	const meshcore_common_packet_view_t *packet, uint8_t type,
	const meshcore_common_channel_view_t *channel, uint8_t *data, size_t len)
{
	(void)packet;
	(void)channel;
	(void)data;
	g_test_mesh_script.on_group_data_recv_count++;
	g_test_mesh_script.last_group_data_type = type;
	g_test_mesh_script.last_group_data_len = (uint16_t)len;
	meshcore_hal_test_mesh_script_sync_mirror();
}

void meshcore_mesh_on_ack_recv(const meshcore_common_packet_view_t *packet,
			       uint32_t ack_crc)
{
	(void)packet;
	g_test_mesh_script.on_ack_recv_count++;
	g_test_mesh_script.last_ack_crc = ack_crc;
	meshcore_hal_test_mesh_script_sync_mirror();
}

void meshcore_hal_test_radio_on_send_finished(void)
{
	g_test_radio.packets_sent++;
	g_test_radio.send_in_progress = false;
	g_test_radio.send_completed = false;
	g_test_radio.send_done_at_ms = 0U;
}

uint32_t meshcore_hal_rtc_get_current_time(void)
{
	return g_test_rtc_current_time;
}

void meshcore_hal_rng_random(uint8_t *dest, size_t size)
{
	uint32_t value;

	if (dest == NULL) {
		return;
	}

	if (g_test_rng.active && g_test_rng.len > 0U) {
		for (size_t i = 0; i < size; i++) {
			dest[i] = g_test_rng.bytes[g_test_rng.idx % g_test_rng.len];
			g_test_rng.idx++;
		}
		return;
	}

	meshcore_hal_psa_init_once();

	if (psa_generate_random(dest, size) == PSA_SUCCESS) {
		return;
	}

	value = (uint32_t)k_cycle_get_32() ^ (uint32_t)k_uptime_get_32();
	for (size_t i = 0; i < size; i++) {
		value = value * 1664525U + 1013904223U + (uint32_t)i;
		dest[i] = (uint8_t)(value >> 24);
	}
}

bool meshcore_hal_sha256(uint8_t *hash, size_t hash_len, const uint8_t *msg,
			 int msg_len)
{
	uint8_t full_hash[MESHCORE_HAL_SHA256_SIZE];
	size_t full_len = 0U;
	psa_status_t status;

	if (hash == NULL || msg == NULL || hash_len > sizeof(full_hash) || msg_len < 0) {
		return false;
	}

	meshcore_hal_psa_init_once();

	status = psa_hash_compute(PSA_ALG_SHA_256, msg, (size_t)msg_len, full_hash,
				  sizeof(full_hash), &full_len);
	if (status != PSA_SUCCESS || full_len < hash_len) {
		return false;
	}

	memcpy(hash, full_hash, hash_len);
	memset(full_hash, 0, sizeof(full_hash));
	return true;
}

bool meshcore_hal_sha256_two_fragments(uint8_t *hash, size_t hash_len,
				       const uint8_t *frag1, int frag1_len,
				       const uint8_t *frag2, int frag2_len)
{
	uint8_t full_hash[MESHCORE_HAL_SHA256_SIZE];
	size_t full_len = 0U;
	psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
	psa_status_t status;

	if (hash == NULL || frag1 == NULL || frag2 == NULL || hash_len > sizeof(full_hash) ||
	    frag1_len < 0 || frag2_len < 0) {
		return false;
	}

	meshcore_hal_psa_init_once();

	status = psa_hash_setup(&op, PSA_ALG_SHA_256);
	if (status != PSA_SUCCESS) {
		return false;
	}

	status = psa_hash_update(&op, frag1, (size_t)frag1_len);
	if (status == PSA_SUCCESS) {
		status = psa_hash_update(&op, frag2, (size_t)frag2_len);
	}
	if (status == PSA_SUCCESS) {
		status = psa_hash_finish(&op, full_hash, sizeof(full_hash), &full_len);
	}

	psa_hash_abort(&op);

	if (status != PSA_SUCCESS || full_len < hash_len) {
		memset(full_hash, 0, sizeof(full_hash));
		return false;
	}

	memcpy(hash, full_hash, hash_len);
	memset(full_hash, 0, sizeof(full_hash));
	return true;
}

static bool meshcore_hal_aes128_crypt_block(const uint8_t *key_bytes, uint8_t *dest,
					    const uint8_t *src, bool encrypt)
{
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key = PSA_KEY_ID_NULL;
	psa_status_t status;
	size_t out_len = 0U;

	if (key_bytes == NULL || dest == NULL || src == NULL) {
		return false;
	}

	meshcore_hal_psa_init_once();

	psa_set_key_usage_flags(&attributes,
				encrypt ? PSA_KEY_USAGE_ENCRYPT : PSA_KEY_USAGE_DECRYPT);
	psa_set_key_algorithm(&attributes, PSA_ALG_ECB_NO_PADDING);
	psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
	psa_set_key_bits(&attributes, MESHCORE_HAL_AES128_KEY_SIZE * 8U);

	status = psa_import_key(&attributes, key_bytes, MESHCORE_HAL_AES128_KEY_SIZE, &key);
	if (status != PSA_SUCCESS) {
		goto out;
	}

	if (encrypt) {
		status = psa_cipher_encrypt(key, PSA_ALG_ECB_NO_PADDING, src,
					    MESHCORE_HAL_AES128_BLOCK_SIZE, dest,
					    MESHCORE_HAL_AES128_BLOCK_SIZE, &out_len);
	} else {
		status = psa_cipher_decrypt(key, PSA_ALG_ECB_NO_PADDING, src,
					    MESHCORE_HAL_AES128_BLOCK_SIZE, dest,
					    MESHCORE_HAL_AES128_BLOCK_SIZE, &out_len);
	}

out:
	if (key != PSA_KEY_ID_NULL) {
		psa_destroy_key(key);
	}
	psa_reset_key_attributes(&attributes);

	return status == PSA_SUCCESS && out_len == MESHCORE_HAL_AES128_BLOCK_SIZE;
}

bool meshcore_hal_aes128_encrypt_block(const uint8_t *key, uint8_t *dest,
				       const uint8_t *src)
{
	if (g_test_crypto_fail.force_encrypt_fail) {
		return false;
	}

	return meshcore_hal_aes128_crypt_block(key, dest, src, true);
}

bool meshcore_hal_aes128_decrypt_block(const uint8_t *key, uint8_t *dest,
				       const uint8_t *src)
{
	return meshcore_hal_aes128_crypt_block(key, dest, src, false);
}

bool meshcore_hal_hmac_sha256(uint8_t *mac, size_t mac_len, const uint8_t *key_bytes,
			      size_t key_len, const uint8_t *msg, size_t msg_len)
{
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key = PSA_KEY_ID_NULL;
	const psa_algorithm_t mac_alg = PSA_ALG_HMAC(PSA_ALG_SHA_256);
	uint8_t full_mac[MESHCORE_HAL_SHA256_SIZE];
	size_t full_len = 0U;
	psa_status_t status;

	if (mac == NULL || key_bytes == NULL || msg == NULL || mac_len > sizeof(full_mac)) {
		return false;
	}
	if (g_test_crypto_fail.force_hmac_fail) {
		return false;
	}

	meshcore_hal_psa_init_once();

	psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
	psa_set_key_algorithm(&attributes, mac_alg);
	psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
	psa_set_key_bits(&attributes, key_len * 8U);

	status = psa_import_key(&attributes, key_bytes, key_len, &key);
	if (status != PSA_SUCCESS) {
		goto out;
	}

	status = psa_mac_compute(key, mac_alg, msg, msg_len, full_mac, sizeof(full_mac),
				 &full_len);
	if (status == PSA_SUCCESS && full_len >= mac_len) {
		memcpy(mac, full_mac, mac_len);
	}

out:
	if (key != PSA_KEY_ID_NULL) {
		psa_destroy_key(key);
	}
	psa_reset_key_attributes(&attributes);
	memset(full_mac, 0, sizeof(full_mac));

	return status == PSA_SUCCESS && full_len >= mac_len;
}

int meshcore_platform_timer_arm(uint32_t deadline_ms)
{
	g_test_platform_timer.arm_count++;
	g_test_platform_timer.last_deadline_ms = deadline_ms;
	return 0;
}

void meshcore_platform_timer_cancel(void)
{
	g_test_platform_timer.cancel_count++;
}

unsigned long meshcore_platform_millis_get(void)
{
	return meshcore_hal_millis_get();
}

uint32_t meshcore_platform_rtc_get_current_time(void)
{
	return meshcore_hal_rtc_get_current_time();
}

void meshcore_platform_radio_begin(void)
{
	meshcore_hal_radio_begin();
}

int meshcore_platform_radio_packet_send(const uint8_t *data, size_t len)
{
	return meshcore_hal_radio_packet_send(data, len);
}

uint32_t meshcore_platform_radio_airtime(size_t len)
{
	return meshcore_hal_radio_airtime(len);
}

float meshcore_platform_radio_packet_score(int8_t snr_q4, size_t len)
{
	return meshcore_hal_radio_packet_score(snr_q4, len);
}

bool meshcore_platform_radio_in_rx_mode_get(void)
{
	return meshcore_hal_radio_in_rx_mode_get();
}

bool meshcore_platform_radio_receiving_get(void)
{
	return meshcore_hal_radio_receiving_get();
}

void meshcore_platform_radio_noise_floor_calibrate(int threshold)
{
	meshcore_hal_radio_noise_floor_calibrate(threshold);
}

void meshcore_platform_radio_agc_reset(void)
{
	meshcore_hal_radio_agc_reset();
}

void meshcore_platform_rng_random(uint8_t *dest, size_t size)
{
	meshcore_hal_rng_random(dest, size);
}

bool meshcore_platform_crypto_sha256(uint8_t *hash, size_t hash_len,
				     const uint8_t *msg, int msg_len)
{
	return meshcore_hal_sha256(hash, hash_len, msg, msg_len);
}

bool meshcore_platform_crypto_sha256_two_fragments(
	uint8_t *hash, size_t hash_len, const uint8_t *frag1, int frag1_len,
	const uint8_t *frag2, int frag2_len)
{
	return meshcore_hal_sha256_two_fragments(hash, hash_len, frag1,
						 frag1_len, frag2, frag2_len);
}

bool meshcore_platform_crypto_aes128_encrypt_block(const uint8_t *key,
						   uint8_t *dest,
						   const uint8_t *src)
{
	return meshcore_hal_aes128_encrypt_block(key, dest, src);
}

bool meshcore_platform_crypto_aes128_decrypt_block(const uint8_t *key,
						   uint8_t *dest,
						   const uint8_t *src)
{
	return meshcore_hal_aes128_decrypt_block(key, dest, src);
}

bool meshcore_platform_crypto_hmac_sha256(uint8_t *mac, size_t mac_len,
					  const uint8_t *key, size_t key_len,
					  const uint8_t *msg, size_t msg_len)
{
	return meshcore_hal_hmac_sha256(mac, mac_len, key, key_len, msg,
					msg_len);
}

int meshcore_platform_cli_receive(const meshcore_common_cli_event_t *event,
				 char *reply, size_t reply_capacity)
{
	/* This test host has no native CLI command executor. */
	ARG_UNUSED(event);
	ARG_UNUSED(reply);
	ARG_UNUSED(reply_capacity);

	return -ENOTSUP;
}

int meshcore_platform_node_identity_get(meshcore_common_node_identity_t *out)
{
	return meshcore_node_identity_get(out);
}

int meshcore_platform_node_config_last_modify_get(uint32_t *out_timestamp)
{
	return meshcore_node_config_last_modify_get(out_timestamp);
}

int meshcore_platform_node_runtime_policy_get(
	meshcore_common_node_runtime_policy_t *out)
{
	return meshcore_node_runtime_policy_get(out);
}

int meshcore_platform_node_advert_profile_get(
	meshcore_common_node_advert_profile_t *out)
{
	return meshcore_node_advert_profile_get(out);
}

int meshcore_platform_peer_path_get_by_key(
	const uint8_t *public_key, meshcore_common_peer_path_t *out)
{
	return meshcore_peer_path_get_by_key(public_key, out);
}

int meshcore_platform_peer_seen_update(const uint8_t *public_key, bool has_snr,
				       int8_t snr_q4)
{
	return meshcore_peer_seen_update(public_key, has_snr, snr_q4);
}

int meshcore_platform_peer_next_shared_secret_by_hash(
	const uint8_t *hash, size_t start_slot, size_t *slot_id,
	uint8_t *dest_secret, meshcore_common_peer_identity_t *peer_identity)
{
	return meshcore_mesh_next_peer_shared_secret_by_hash(
		hash, start_slot, slot_id, dest_secret, peer_identity);
}

int meshcore_platform_channel_secret_match_exists(uint8_t channel_hash,
						  const uint8_t *secret,
						  size_t secret_len)
{
	return meshcore_channel_secret_match_exists(channel_hash, secret,
						    secret_len);
}

int meshcore_platform_channel_secret_hash(const uint8_t *secret,
					  size_t secret_len, uint8_t *out_hash)
{
	return meshcore_channel_secret_hash(secret, secret_len, out_hash);
}

int meshcore_platform_channel_search_by_hash(
	const uint8_t *hash, meshcore_common_channel_view_t *channels,
	int max_matches)
{
	return meshcore_mesh_search_channels_by_hash(hash, channels, max_matches);
}

void meshcore_platform_dispatcher_log_rx_raw(float snr, float rssi,
					     const uint8_t raw[], int len)
{
	meshcore_dispatcher_log_rx_raw(snr, rssi, raw, len);
}

void meshcore_platform_dispatcher_log_rx(
	const meshcore_common_packet_view_t *packet, int len, float score)
{
	meshcore_dispatcher_log_rx(packet, len, score);
}

void meshcore_platform_dispatcher_log_tx(
	const meshcore_common_packet_view_t *packet, int len)
{
	meshcore_dispatcher_log_tx(packet, len);
}

void meshcore_platform_dispatcher_log_tx_fail(
	const meshcore_common_packet_view_t *packet, int len)
{
	meshcore_dispatcher_log_tx_fail(packet, len);
}

float meshcore_platform_dispatcher_airtime_budget_factor_get(void)
{
	return meshcore_dispatcher_get_airtime_budget_factor();
}

int meshcore_platform_dispatcher_rx_delay_calc(float score, uint32_t air_time)
{
	return meshcore_dispatcher_calc_rx_delay(score, air_time);
}

uint32_t meshcore_platform_dispatcher_cad_fail_max_duration_get(void)
{
	return meshcore_dispatcher_get_cad_fail_max_duration();
}

int meshcore_platform_dispatcher_interference_threshold_get(void)
{
	return meshcore_dispatcher_get_interference_threshold();
}

int meshcore_platform_dispatcher_agc_reset_interval_get(void)
{
	return meshcore_dispatcher_get_agc_reset_interval();
}

unsigned long meshcore_platform_dispatcher_duty_cycle_window_ms_get(void)
{
	return meshcore_dispatcher_get_duty_cycle_window_ms();
}

uint32_t meshcore_platform_mesh_cad_fail_retry_delay_get(void)
{
	return meshcore_mesh_get_cad_fail_retry_delay();
}

bool meshcore_platform_mesh_filter_recv_flood_packet(
	const meshcore_common_packet_view_t *packet)
{
	return meshcore_mesh_filter_recv_flood_packet(packet);
}

bool meshcore_platform_mesh_allow_packet_forward(
	const meshcore_common_packet_view_t *packet)
{
	return meshcore_mesh_allow_packet_forward(packet);
}

uint32_t meshcore_platform_mesh_retransmit_delay_get(
	const meshcore_common_packet_view_t *packet)
{
	return meshcore_mesh_get_retransmit_delay(packet);
}

uint32_t meshcore_platform_mesh_direct_retransmit_delay_get(
	const meshcore_common_packet_view_t *packet)
{
	return meshcore_mesh_get_direct_retransmit_delay(packet);
}

uint8_t meshcore_platform_mesh_extra_ack_transmit_count_get(void)
{
	return meshcore_mesh_get_extra_ack_transmit_count();
}

void meshcore_platform_mesh_on_peer_data_recv(
	const meshcore_common_packet_view_t *packet, uint8_t type,
	const meshcore_common_peer_identity_t *sender, const uint8_t *secret,
	uint8_t *data, size_t len)
{
	meshcore_mesh_on_peer_data_recv(packet, type, sender, secret, data, len);
}

void meshcore_platform_mesh_on_trace_recv(
	const meshcore_common_packet_view_t *packet, uint32_t tag,
	uint32_t auth_code, uint8_t flags, const uint8_t *path_snrs,
	const uint8_t *path_hashes, uint8_t path_len)
{
	meshcore_mesh_on_trace_recv(packet, tag, auth_code, flags, path_snrs,
				    path_hashes, path_len);
}

bool meshcore_platform_mesh_on_peer_path_recv(
	const meshcore_common_packet_view_t *packet,
	const meshcore_common_peer_identity_t *sender, const uint8_t *secret,
	uint8_t *path, uint8_t path_len, uint8_t extra_type, uint8_t *extra,
	uint8_t extra_len)
{
	return meshcore_mesh_on_peer_path_recv(packet, sender, secret, path,
					       path_len, extra_type, extra,
					       extra_len);
}

void meshcore_platform_mesh_on_advert_recv(
	const meshcore_common_packet_view_t *packet,
	const meshcore_common_identity_view_t *identity, uint32_t timestamp,
	const uint8_t *app_data, size_t app_data_len)
{
	meshcore_mesh_on_advert_recv(packet, identity, timestamp, app_data,
				     app_data_len);
}

void meshcore_platform_mesh_on_anon_data_recv(
	const meshcore_common_packet_view_t *packet, const uint8_t *secret,
	const meshcore_common_identity_view_t *sender, uint8_t *data, size_t len)
{
	meshcore_mesh_on_anon_data_recv(packet, secret, sender, data, len);
}

void meshcore_platform_mesh_on_path_recv(
	const meshcore_common_packet_view_t *packet,
	const meshcore_common_identity_view_t *sender, uint8_t *path,
	uint8_t path_len, uint8_t extra_type, uint8_t *extra, uint8_t extra_len)
{
	meshcore_mesh_on_path_recv(packet, sender, path, path_len, extra_type,
				   extra, extra_len);
}

void meshcore_platform_mesh_on_control_data_recv(
	const meshcore_common_packet_view_t *packet)
{
	meshcore_mesh_on_control_data_recv(packet);
}

void meshcore_platform_mesh_on_raw_data_recv(
	const meshcore_common_packet_view_t *packet)
{
	meshcore_mesh_on_raw_data_recv(packet);
}

void meshcore_platform_mesh_on_group_data_recv(
	const meshcore_common_packet_view_t *packet, uint8_t type,
	const meshcore_common_channel_view_t *channel, uint8_t *data, size_t len)
{
	meshcore_mesh_on_group_data_recv(packet, type, channel, data, len);
}

void meshcore_platform_mesh_on_ack_recv(
	const meshcore_common_packet_view_t *packet, uint32_t ack_crc)
{
	meshcore_mesh_on_ack_recv(packet, ack_crc);
}

void meshcore_platform_runtime_request_error(uint8_t request_type, int err_code)
{
	meshcore_runtime_request_error(request_type, err_code);
}

int meshcore_platform_event_message(const meshcore_common_message_t *message)
{
	return meshcore_message_handler(message);
}

int meshcore_platform_event_message_ack(const uint8_t *target, uint8_t attempt)
{
	return meshcore_message_ack_handler(target, attempt);
}

int meshcore_platform_event_advert(const meshcore_common_advert_event_t *advert)
{
	if (advert == NULL) {
		return -EINVAL;
	}

	strncpy(g_test_publish_events.advert.name, advert->name,
		sizeof(g_test_publish_events.advert.name) - 1U);
	g_test_publish_events.advert.name[sizeof(g_test_publish_events.advert.name) -
					  1U] = '\0';
	g_test_publish_events.advert.role = advert->role;
	memcpy(g_test_publish_events.advert.public_key, advert->public_key,
	       sizeof(g_test_publish_events.advert.public_key));
	g_test_publish_events.advert.is_new = advert->is_new;
	g_test_publish_events.advert.advert_timestamp = advert->advert_timestamp;
	g_test_publish_events.advert.has_position = advert->has_position;
	g_test_publish_events.advert.latitude = advert->latitude;
	g_test_publish_events.advert.longitude = advert->longitude;
	g_test_publish_events.advert.has_out_path = advert->has_out_path;
	g_test_publish_events.advert.out_path_len = advert->out_path_len;
	g_test_publish_events.advert.path_hash_size = advert->path_hash_size;
	memcpy(g_test_publish_events.advert.out_path, advert->out_path,
	       sizeof(g_test_publish_events.advert.out_path));
	g_test_publish_events.advert.raw_advert_len = advert->raw_advert_len;
	memcpy(g_test_publish_events.advert.raw_advert, advert->raw_advert,
	       sizeof(g_test_publish_events.advert.raw_advert));
	g_test_publish_events.advert_valid = true;

	g_test_mesh_script.on_advert_recv_count++;
	g_test_mesh_script.last_advert_timestamp = advert->advert_timestamp;
	memcpy(g_test_mesh_script.last_advert_public_key, advert->public_key,
	       sizeof(g_test_mesh_script.last_advert_public_key));
	g_test_mesh_script.last_advert_path_len = advert->out_path_len;
	g_test_mesh_script.last_advert_app_data_len =
		(uint16_t)MIN((size_t)advert->raw_advert_len,
			      sizeof(g_test_mesh_script.last_advert_app_data));
	if (g_test_mesh_script.last_advert_app_data_len > 0U) {
		memcpy(g_test_mesh_script.last_advert_app_data, advert->raw_advert,
		       g_test_mesh_script.last_advert_app_data_len);
	}
	meshcore_hal_test_mesh_script_sync_mirror();
	return 0;
}

int meshcore_platform_event_peer_path_publish(
	const meshcore_common_peer_path_event_t *peer_path, bool is_discover)
{
	return meshcore_peer_path_publish(peer_path, is_discover);
}

int meshcore_platform_event_peer_path(
	const meshcore_common_peer_path_event_t *peer_path)
{
	return meshcore_peer_path_handler(peer_path);
}

int meshcore_platform_event_trace_path(
	uint8_t state, uint32_t tag, const int8_t *out_path_snr,
	uint8_t out_count, const int8_t *return_path_snr, uint8_t return_count,
	bool has_response_snr, int8_t response_snr, uint32_t timestamp)
{
	return meshcore_trace_path_handler(state, tag, out_path_snr,
					   out_count, return_path_snr,
					   return_count, has_response_snr,
					   response_snr, timestamp);
}

int meshcore_platform_event_telemetry(const uint8_t *key_prefix,
					      uint32_t timestamp,
					      uint32_t tag,
					      const uint8_t *payload,
					      size_t payload_len)
{
	return meshcore_telemetry_handler(key_prefix, timestamp, tag, payload,
					  payload_len);
}

int meshcore_platform_event_binary_request(
	const meshcore_common_binary_request_event_t *event)
{
	return meshcore_binary_request_handler(event);
}

int meshcore_platform_event_binary_response(const uint8_t *key_prefix,
					    uint32_t timestamp, uint32_t tag,
					    const uint8_t *payload,
					    size_t payload_len)
{
	return meshcore_binary_response_handler(key_prefix, timestamp, tag,
						payload, payload_len);
}

int meshcore_platform_event_node_discover(
	const meshcore_common_node_discover_event_t *event)
{
	return meshcore_node_discover_handler(event);
}

int meshcore_platform_event_channel_data(
	const meshcore_common_channel_data_event_t *event)
{
	return meshcore_channel_data_handler(event);
}

int meshcore_platform_event_raw_data(
	const meshcore_common_raw_data_event_t *event)
{
	return meshcore_raw_data_handler(event);
}

int meshcore_platform_event_control_data(
	const meshcore_common_control_data_event_t *event)
{
	return meshcore_control_data_handler(event);
}

int meshcore_platform_telemetry_node_get(
	const meshcore_platform_request_source_t *requester,
	uint8_t permission_mask, meshcore_platform_telemetry_payload_t *out)
{
	return meshcore_hal_node_telemetry_get(requester, permission_mask, out);
}
