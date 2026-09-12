/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "protocol.h"

#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <pb_decode.h>
#include <pb_encode.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/version.h>

#include <channel/channel.h>
#include <clock/clock.h>
#include <management/management.h>
#include <meshcore/meshcore.h>
#include <message/message.h>
#include <contact/contact.h>

#include <zephyr/mgmt/mcumgr/mgmt/mgmt_defines.h>

#include "meshbus/clock.pb.h"
#include "meshbus/management.pb.h"
#include "meshbus/meshcore.pb.h"
#include "meshbus/power.pb.h"
#include "meshbus/radio.pb.h"
#include "meshcore/platform.h"
#include "meshcore/types.h"
#include "meshcore_identity.h"

#if defined(CONFIG_MBS_BLUETOOTH)
#include <bluetooth/bluetooth.h>
#endif

#if defined(CONFIG_MBS_POWER)
#include <power/power.h>
#endif

#if defined(CONFIG_MBS_RADIO)
#include <radio/radio.h>
#endif

LOG_MODULE_REGISTER(meshcore_companion_adapter, CONFIG_MBS_MESHCORE_LOG_LEVEL);

enum companion_command_code {
	COMPANION_CMD_APP_START = 1,
	COMPANION_CMD_SEND_TXT_MSG = 2,
	COMPANION_CMD_SEND_CHANNEL_TXT_MSG = 3,
	COMPANION_CMD_GET_CONTACTS = 4,
	COMPANION_CMD_GET_DEVICE_TIME = 5,
	COMPANION_CMD_SET_DEVICE_TIME = 6,
	COMPANION_CMD_SEND_SELF_ADVERT = 7,
	COMPANION_CMD_SET_NAME = 8,
	COMPANION_CMD_ADD_UPDATE_CONTACT = 9,
	COMPANION_CMD_SYNC_NEXT_MESSAGE = 10,
	COMPANION_CMD_SET_RADIO = 11,
	COMPANION_CMD_SET_TX_POWER = 12,
	COMPANION_CMD_RESET_PATH = 13,
	COMPANION_CMD_SET_COORDINATES = 14,
	COMPANION_CMD_REMOVE_CONTACT = 15,
	COMPANION_CMD_GET_BATT_AND_STORAGE = 20,
	COMPANION_CMD_SET_TUNING = 21,
	COMPANION_CMD_DEVICE_QEURY = 22,
	COMPANION_CMD_SEND_RAW_DATA = 25,
	COMPANION_CMD_SEND_LOGIN = 26,
	COMPANION_CMD_SEND_STATUS_REQ = 27,
	COMPANION_CMD_GET_CONTACT_BY_KEY = 30,
	COMPANION_CMD_GET_CHANNEL = 31,
	COMPANION_CMD_SET_CHANNEL = 32,
	COMPANION_CMD_SEND_TRACE_PATH = 36,
	COMPANION_CMD_SET_DEVICE_PIN = 37,
	COMPANION_CMD_SET_OTHER_PARAMS = 38,
	COMPANION_CMD_SEND_TELEMETRY_REQ = 39,
	COMPANION_CMD_SEND_BINARY_REQ = 50,
	COMPANION_CMD_SEND_PATH_DISCOVERY_REQ = 52,
	COMPANION_CMD_SEND_CONTROL_DATA = 55,
	COMPANION_CMD_SET_AUTOADD_CONFIG = 58,
	COMPANION_CMD_GET_AUTOADD_CONFIG = 59,
	COMPANION_CMD_GET_ALLOWED_REPEAT_FREQ = 60,
	COMPANION_CMD_SET_PATH_HASH_MODE = 61,
	COMPANION_CMD_SEND_CHANNEL_DATA = 62,
};

enum companion_response_code {
	COMPANION_RESP_CODE_OK = 0,
	COMPANION_RESP_CODE_ERR = 1,
	COMPANION_RESP_CODE_CONTACTS_START = 2,
	COMPANION_RESP_CODE_CONTACT = 3,
	COMPANION_RESP_CODE_END_OF_CONTACTS = 4,
	COMPANION_RESP_CODE_SELF_INFO = 5,
	COMPANION_RESP_CODE_SENT = 6,
	COMPANION_RESP_CODE_CURR_TIME = 9,
	COMPANION_RESP_CODE_NO_MORE_MESSAGES = 10,
	COMPANION_RESP_CODE_BATT_AND_STORAGE = 12,
	COMPANION_RESP_CODE_DEVICE_INFO = 13,
	COMPANION_RESP_CODE_CONTACT_MSG_RECV_V3 = 16,
	COMPANION_RESP_CODE_CHANNEL_MSG_RECV_V3 = 17,
	COMPANION_RESP_CODE_CHANNEL_INFO = 18,
	COMPANION_RESP_CODE_AUTOADD_CONFIG = 25,
	COMPANION_RESP_CODE_ALLOWED_REPEAT_FREQ = 26,
	COMPANION_RESP_CODE_CHANNEL_DATA_RECV = 27,
};

enum companion_push_code {
	COMPANION_PUSH_CODE_ADVERT = 0x80,
	COMPANION_PUSH_CODE_PATH_UPDATED = 0x81,
	COMPANION_PUSH_CODE_SEND_CONFIRMED = 0x82,
	COMPANION_PUSH_CODE_MSG_WAITING = 0x83,
	COMPANION_PUSH_CODE_RAW_DATA = 0x84,
	COMPANION_PUSH_CODE_LOGIN_SUCCESS = 0x85,
	COMPANION_PUSH_CODE_LOGIN_FAIL = 0x86,
	COMPANION_PUSH_CODE_STATUS_RESPONSE = 0x87,
	COMPANION_PUSH_CODE_NEW_ADVERT = 0x8a,
	COMPANION_PUSH_CODE_TRACE_DATA = 0x89,
	COMPANION_PUSH_CODE_TELEMETRY_RESPONSE = 0x8b,
	COMPANION_PUSH_CODE_BINARY_RESPONSE = 0x8c,
	COMPANION_PUSH_CODE_PATH_DISCOVERY_RESPONSE = 0x8d,
	COMPANION_PUSH_CODE_CONTROL_DATA = 0x8e,
};

enum companion_error_code {
	COMPANION_ERR_CODE_UNSUPPORTED_CMD = 1,
	COMPANION_ERR_CODE_NOT_FOUND = 2,
	COMPANION_ERR_CODE_TABLE_FULL = 3,
	COMPANION_ERR_CODE_BAD_STATE = 4,
	COMPANION_ERR_CODE_ILLEGAL_ARG = 6,
};

enum companion_protocol_value {
	COMPANION_FIRMWARE_VER_CODE = 11,
	COMPANION_DEFAULT_BLE_PIN = 0,
	COMPANION_MAX_LORA_TX_POWER_DBM = 22,
	COMPANION_ADV_LOC_NONE = 0,
	COMPANION_ADV_LOC_SHARE = 1,
	COMPANION_PUBLIC_KEY_SIZE = 32,
	COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE = 6,
	COMPANION_CONTACT_FRAME_SIZE = 147,
	COMPANION_CONTACT_PATH_SIZE = 64,
	COMPANION_CONTACT_NAME_SIZE = 32,
	COMPANION_CONTACT_UPDATE_MIN_SIZE = 1 + COMPANION_PUBLIC_KEY_SIZE + 1 + 1 + 1 +
					    COMPANION_CONTACT_PATH_SIZE +
					    COMPANION_CONTACT_NAME_SIZE + 4 + 4 + 4,
	COMPANION_CHANNEL_NAME_SIZE = 32,
	COMPANION_CHANNEL_SECRET_SIZE = 16,
	COMPANION_DEVICE_INFO_BUILD_DATE_SIZE = 12,
	COMPANION_DEVICE_INFO_MANUFACTURER_SIZE = 40,
	COMPANION_DEVICE_INFO_VERSION_SIZE = 20,
	COMPANION_TXT_TYPE_PLAIN = 0,
	COMPANION_TXT_TYPE_CLI_DATA = 1,
	COMPANION_EST_SEND_TIMEOUT_MS = 0,
	COMPANION_TRACE_TIMEOUT_BASE_MS = 2000,
	COMPANION_TRACE_TIMEOUT_PER_HOP_MS = 1000,
	COMPANION_OUT_PATH_UNKNOWN = 0xff,
	COMPANION_AUTOADD_MAX_HOPS = 64,
	COMPANION_CONTACT_SYNC_FRAME_DELAY_MS = 20,
	COMPANION_PENDING_ACK_SLOTS = 16,
	COMPANION_PENDING_TRACE_SLOTS = 4,
	COMPANION_PENDING_STATUS_SLOTS = 4,
	COMPANION_LOGIN_SESSION_SLOTS = 4,
	COMPANION_PENDING_CLI_SLOTS = 4,
	COMPANION_STATUS_REQUEST_PAYLOAD_LEN = 9,
	COMPANION_STATUS_REQUEST_TYPE = 0x01,
	COMPANION_TRACE_REQUEST_HEADER_LEN = 10,
	COMPANION_BINARY_REQUEST_MIN_PAYLOAD_LEN = 1,
	COMPANION_RAW_DATA_MIN_PAYLOAD_LEN = 4,
	COMPANION_CHANNEL_DATA_OVERHEAD = 9,
	COMPANION_RAW_CONTROL_OVERHEAD = 4,
	COMPANION_BINARY_RESPONSE_HEADER_LEN = 6,
	COMPANION_STATUS_RESPONSE_HEADER_LEN = 2 + COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE,
	COMPANION_MGMT_HDR_SIZE = 8,
	COMPANION_TEXT_LOG_PREVIEW_SIZE = 64,
	COMPANION_CLI_REQUEST_ID_MAX_LEN = 8,
	COMPANION_CLI_COMMAND_MAX_LEN = 160,
	COMPANION_CLI_RESPONSE_TIMEOUT_MS = 60000,
	COMPANION_LOGIN_RESPONSE_TIMEOUT_MS = 60000,
	COMPANION_LOGIN_PERMISSION_ADMIN = 1,
	COMPANION_LOGIN_ACL_PERM_ADMIN = 3,
	COMPANION_STATUS_RESPONSE_TIMEOUT_MS = CONFIG_MBS_CONTACT_REQUEST_TIMEOUT_MS,
	COMPANION_MGMT_CBOR_STATES = 4,
	COMPANION_MGMT_PROTO_MAX_SIZE =
		MAX(MAX(meshbus_MeshcoreConfigGetResponse_size,
			meshbus_MeshcoreConfigSetRequest_size),
		    MAX(MAX(meshbus_RadioConfigGetResponse_size,
			    meshbus_RadioConfigSetRequest_size),
			MAX(MAX(meshbus_ManagementSecretSetRequest_size,
				meshbus_ClockTimeSetRequest_size),
			    meshbus_ClockTimeSetResponse_size))),
};

#define COMPANION_DEFAULT_TELEMETRY_PERMISSION_MASK \
	(MESHCORE_TELEM_PERM_BASE | MESHCORE_TELEM_PERM_LOCATION | \
	 MESHCORE_TELEM_PERM_ENVIRONMENT)

#if defined(APP_VERSION_STRING)
#define COMPANION_APP_BUILD_VERSION APP_VERSION_STRING
#else
#define COMPANION_APP_BUILD_VERSION STRINGIFY(BUILD_VERSION)
#endif

struct companion_repeat_freq_range {
	uint32_t lower_khz;
	uint32_t upper_khz;
};

static const struct companion_repeat_freq_range companion_repeat_freq_ranges[] = {
	{ 433000U, 433000U },
	{ 869000U, 869000U },
	{ 918000U, 918000U },
};

struct companion_pending_ack {
	bool in_use;
	uint8_t attempt;
	uint32_t ack_id;
	uint32_t sent_uptime_ms;
};

struct companion_contact_sync_state {
	bool active;
	uint32_t since;
	uint32_t last_modified;
	uint32_t emitted;
	size_t cursor;
};

struct companion_trace_pending {
	bool in_use;
	uint32_t local_tag;
	uint32_t app_tag;
	uint32_t auth_code;
	uint32_t sent_uptime_ms;
	uint8_t flags;
	uint8_t path_len;
	uint8_t path[MBS_MESHCORE_PATH_MAX_LEN];
};

struct companion_status_pending {
	bool in_use;
	uint32_t tag;
	uint32_t sent_uptime_ms;
	uint8_t public_key_prefix[COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE];
};

struct companion_login_pending {
	bool in_use;
	uint32_t tag;
	uint32_t sent_uptime_ms;
	uint8_t contact_prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES];
	uint8_t public_key_prefix[COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE];
	uint8_t secret[MBS_MANAGEMENT_SECRET_MAX_LEN];
	size_t secret_len;
};

struct companion_login_start {
	bool in_use;
	uint8_t contact_prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES];
	uint8_t public_key_prefix[COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE];
	uint8_t secret[MBS_MANAGEMENT_SECRET_MAX_LEN];
	size_t secret_len;
};

struct companion_login_session {
	bool in_use;
	uint32_t established_uptime_ms;
	uint8_t contact_prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES];
	uint8_t public_key_prefix[COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE];
	uint8_t secret[MBS_MANAGEMENT_SECRET_MAX_LEN];
	size_t secret_len;
};

struct companion_cli_start {
	bool in_use;
	bool processing;
	size_t len;
	uint8_t frame[MESHCORE_COMPANION_MAX_FRAME_SIZE];
};

struct companion_contact_update_start {
	bool in_use;
	bool processing;
	size_t len;
	uint8_t frame[MESHCORE_COMPANION_MAX_FRAME_SIZE];
};

enum companion_cli_op {
	COMPANION_CLI_OP_NONE = 0,
	COMPANION_CLI_OP_GET_NAME,
	COMPANION_CLI_OP_SET_NAME,
	COMPANION_CLI_OP_GET_RADIO,
	COMPANION_CLI_OP_SET_RADIO,
	COMPANION_CLI_OP_ADVERT,
	COMPANION_CLI_OP_GET_ADVERT_INTERVAL,
	COMPANION_CLI_OP_GET_FLOOD_ADVERT_INTERVAL,
	COMPANION_CLI_OP_SET_ADVERT_INTERVAL,
	COMPANION_CLI_OP_SET_FLOOD_ADVERT_INTERVAL,
	COMPANION_CLI_OP_GET_LAT,
	COMPANION_CLI_OP_SET_LAT,
	COMPANION_CLI_OP_GET_LON,
	COMPANION_CLI_OP_SET_LON,
	COMPANION_CLI_OP_TIME,
	COMPANION_CLI_OP_PASSWORD,
	COMPANION_CLI_OP_SET_PRV_KEY,
	COMPANION_CLI_OP_SET_PUB_KEY,
	COMPANION_CLI_OP_GET_REPEAT,
	COMPANION_CLI_OP_SET_REPEAT,
	COMPANION_CLI_OP_REBOOT,
};

enum companion_cli_stage {
	COMPANION_CLI_STAGE_DIRECT = 0,
	COMPANION_CLI_STAGE_CONFIG_GET,
	COMPANION_CLI_STAGE_CONFIG_SET,
};

struct companion_cli_pending {
	bool in_use;
	uint32_t tag;
	uint32_t sent_uptime_ms;
	uint32_t sender_timestamp;
	enum companion_cli_op op;
	enum companion_cli_stage stage;
	uint8_t request_id_len;
	char request_id[COMPANION_CLI_REQUEST_ID_MAX_LEN + 1U];
	uint8_t contact_prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES];
	uint8_t public_key_prefix[COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE];
	uint8_t secret[MBS_MANAGEMENT_SECRET_MAX_LEN];
	size_t secret_len;
	union {
		char text[sizeof(((meshbus_MeshcoreConfig *)0)->name)];
		struct {
			uint64_t frequency;
			uint32_t bandwidth;
			uint32_t spread_factor;
			uint32_t coding_rate;
		} radio;
		uint32_t value_u32;
		int32_t latitude;
		int32_t longitude;
		bool value_bool;
		struct {
			uint8_t bytes[MBS_MANAGEMENT_SECRET_MAX_LEN];
			size_t len;
		} secret;
		struct {
			uint8_t private_key[MBS_MESHCORE_PRIVATE_KEY_SIZE];
			uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE];
			bool has_private_key;
			bool has_public_key;
		} identity;
	} value;
};

K_MUTEX_DEFINE(adapter_lock);
K_MUTEX_DEFINE(companion_ack_lock);
K_MUTEX_DEFINE(companion_contact_sync_lock);
K_MUTEX_DEFINE(companion_trace_lock);
K_MUTEX_DEFINE(companion_status_lock);
K_MUTEX_DEFINE(companion_login_lock);
K_MUTEX_DEFINE(companion_cli_lock);
K_MUTEX_DEFINE(companion_contact_update_lock);
static bool adapter_initialized;
static bool adapter_connected;
static struct meshcore_companion_transport adapter_transport;
static struct companion_pending_ack companion_pending_acks[COMPANION_PENDING_ACK_SLOTS];
static struct companion_contact_sync_state companion_contact_sync;
static struct companion_trace_pending companion_pending_traces[COMPANION_PENDING_TRACE_SLOTS];
static struct companion_status_pending companion_pending_status[COMPANION_PENDING_STATUS_SLOTS];
static struct companion_login_pending companion_pending_login;
static struct companion_login_start companion_login_start;
static struct companion_login_session companion_login_sessions[COMPANION_LOGIN_SESSION_SLOTS];
static struct companion_cli_pending companion_pending_cli[COMPANION_PENDING_CLI_SLOTS];
static struct companion_cli_start companion_cli_start;
static struct companion_contact_update_start companion_contact_update_start;

static void companion_contact_sync_work_handler(struct k_work *work);
static void companion_login_start_work_handler(struct k_work *work);
static void companion_cli_start_work_handler(struct k_work *work);
static void companion_contact_update_work_handler(struct k_work *work);
static uint32_t companion_current_timestamp(void);

K_WORK_DELAYABLE_DEFINE(companion_contact_sync_work, companion_contact_sync_work_handler);
K_WORK_DEFINE(companion_login_start_work, companion_login_start_work_handler);
K_WORK_DEFINE(companion_cli_start_work, companion_cli_start_work_handler);
K_WORK_DEFINE(companion_contact_update_work, companion_contact_update_work_handler);

static void companion_secure_wipe(void *ptr, size_t len)
{
	volatile uint8_t *p = ptr;

	while (len-- > 0U) {
		*p++ = 0U;
	}
}

static bool companion_management_password_is_valid(const uint8_t *password,
						    size_t password_len)
{
	if (password == NULL || password_len < MBS_MANAGEMENT_SECRET_MIN_LEN ||
	    password_len > MBS_MANAGEMENT_SECRET_MAX_LEN) {
		return false;
	}

	for (size_t i = 0U; i < password_len; i++) {
		if (password[i] < 0x21U || password[i] > 0x7eU) {
			return false;
		}
	}

	return true;
}

static int validate_frame_args(const uint8_t *frame, size_t len)
{
	if (frame == NULL || len == 0U) {
		return -EINVAL;
	}

	if (len > MESHCORE_COMPANION_MAX_FRAME_SIZE) {
		return -EMSGSIZE;
	}

	return 0;
}

static int companion_transport_snapshot(struct meshcore_companion_transport *transport)
{
	int rc = 0;

	k_mutex_lock(&adapter_lock, K_FOREVER);
	if (!adapter_initialized || !adapter_connected || adapter_transport.send == NULL) {
		rc = -ENOTCONN;
	} else {
		*transport = adapter_transport;
	}
	k_mutex_unlock(&adapter_lock);

	return rc;
}

static int companion_send_frame(const uint8_t *frame, size_t len)
{
	struct meshcore_companion_transport transport;
	int rc;

	rc = validate_frame_args(frame, len);
	if (rc != 0) {
		return rc;
	}

	rc = companion_transport_snapshot(&transport);
	if (rc != 0) {
		return rc;
	}

	return transport.send(frame, len, transport.user_data);
}

static bool companion_send_error_retryable(int rc)
{
	switch (rc) {
	case -ENOSPC:
	case -EAGAIN:
	case -EBUSY:
	case -ENOMEM:
	case -ENOBUFS:
		return true;
	default:
		return false;
	}
}

static void companion_pending_ack_clear_all(void)
{
	k_mutex_lock(&companion_ack_lock, K_FOREVER);
	memset(companion_pending_acks, 0, sizeof(companion_pending_acks));
	k_mutex_unlock(&companion_ack_lock);
}

static void companion_pending_ack_store(uint8_t attempt, uint32_t ack_id)
{
	size_t free_idx = ARRAY_SIZE(companion_pending_acks);
	size_t oldest_idx = 0U;
	uint32_t oldest_uptime = 0U;
	uint32_t now = k_uptime_get_32();
	bool have_oldest = false;

	k_mutex_lock(&companion_ack_lock, K_FOREVER);
	for (size_t i = 0U; i < ARRAY_SIZE(companion_pending_acks); i++) {
		if (!companion_pending_acks[i].in_use) {
			if (free_idx == ARRAY_SIZE(companion_pending_acks)) {
				free_idx = i;
			}
			continue;
		}

		if (companion_pending_acks[i].attempt == attempt) {
			companion_pending_acks[i].ack_id = ack_id;
			companion_pending_acks[i].sent_uptime_ms = now;
			k_mutex_unlock(&companion_ack_lock);
			return;
		}

		if (!have_oldest ||
		    (int32_t)(companion_pending_acks[i].sent_uptime_ms - oldest_uptime) < 0) {
			oldest_uptime = companion_pending_acks[i].sent_uptime_ms;
			oldest_idx = i;
			have_oldest = true;
		}
	}

	if (free_idx == ARRAY_SIZE(companion_pending_acks)) {
		free_idx = oldest_idx;
		LOG_WRN("Companion pending ACK table full, replacing attempt=%u",
			(unsigned int)companion_pending_acks[free_idx].attempt);
	}

	companion_pending_acks[free_idx] = (struct companion_pending_ack){
		.in_use = true,
		.attempt = attempt,
		.ack_id = ack_id,
		.sent_uptime_ms = now,
	};
	k_mutex_unlock(&companion_ack_lock);
}

static bool companion_pending_ack_take(uint8_t attempt, uint32_t *ack_id,
				       uint32_t *elapsed_ms)
{
	uint32_t now = k_uptime_get_32();

	k_mutex_lock(&companion_ack_lock, K_FOREVER);
	for (size_t i = 0U; i < ARRAY_SIZE(companion_pending_acks); i++) {
		if (!companion_pending_acks[i].in_use ||
		    companion_pending_acks[i].attempt != attempt) {
			continue;
		}

		if (ack_id != NULL) {
			*ack_id = companion_pending_acks[i].ack_id;
		}
		if (elapsed_ms != NULL) {
			*elapsed_ms = now - companion_pending_acks[i].sent_uptime_ms;
		}
		companion_pending_acks[i] = (struct companion_pending_ack){0};
		k_mutex_unlock(&companion_ack_lock);
		return true;
	}
	k_mutex_unlock(&companion_ack_lock);

	return false;
}

static void companion_pending_ack_forget(uint8_t attempt)
{
	k_mutex_lock(&companion_ack_lock, K_FOREVER);
	for (size_t i = 0U; i < ARRAY_SIZE(companion_pending_acks); i++) {
		if (companion_pending_acks[i].in_use &&
		    companion_pending_acks[i].attempt == attempt) {
			companion_pending_acks[i] = (struct companion_pending_ack){0};
			break;
		}
	}
	k_mutex_unlock(&companion_ack_lock);
}

static void companion_pending_trace_clear_all(void)
{
	k_mutex_lock(&companion_trace_lock, K_FOREVER);
	memset(companion_pending_traces, 0, sizeof(companion_pending_traces));
	k_mutex_unlock(&companion_trace_lock);
}

static int companion_pending_trace_reserve(uint32_t app_tag, uint32_t auth_code,
					   uint8_t flags, const uint8_t *path,
					   uint8_t path_len, size_t *out_slot)
{
	size_t free_idx = ARRAY_SIZE(companion_pending_traces);
	size_t oldest_idx = 0U;
	uint32_t oldest_uptime = 0U;
	uint32_t now = k_uptime_get_32();
	bool have_oldest = false;

	k_mutex_lock(&companion_trace_lock, K_FOREVER);
	for (size_t i = 0U; i < ARRAY_SIZE(companion_pending_traces); i++) {
		if (!companion_pending_traces[i].in_use) {
			if (free_idx == ARRAY_SIZE(companion_pending_traces)) {
				free_idx = i;
			}
			continue;
		}

		if (!have_oldest ||
		    (int32_t)(companion_pending_traces[i].sent_uptime_ms - oldest_uptime) < 0) {
			oldest_uptime = companion_pending_traces[i].sent_uptime_ms;
			oldest_idx = i;
			have_oldest = true;
		}
	}

	if (free_idx == ARRAY_SIZE(companion_pending_traces)) {
		free_idx = oldest_idx;
		LOG_WRN("Companion pending trace table full, replacing tag=%u",
			(unsigned int)companion_pending_traces[free_idx].app_tag);
	}

	companion_pending_traces[free_idx] = (struct companion_trace_pending){
		.in_use = true,
		.app_tag = app_tag,
		.auth_code = auth_code,
		.sent_uptime_ms = now,
		.flags = flags,
		.path_len = path_len,
	};
	memcpy(companion_pending_traces[free_idx].path, path, path_len);
	*out_slot = free_idx;
	k_mutex_unlock(&companion_trace_lock);

	return 0;
}

static void companion_pending_trace_commit(size_t slot, uint32_t local_tag)
{
	k_mutex_lock(&companion_trace_lock, K_FOREVER);
	if (slot < ARRAY_SIZE(companion_pending_traces) &&
	    companion_pending_traces[slot].in_use) {
		companion_pending_traces[slot].local_tag = local_tag;
	}
	k_mutex_unlock(&companion_trace_lock);
}

static void companion_pending_trace_clear(size_t slot)
{
	k_mutex_lock(&companion_trace_lock, K_FOREVER);
	if (slot < ARRAY_SIZE(companion_pending_traces)) {
		companion_pending_traces[slot] = (struct companion_trace_pending){0};
	}
	k_mutex_unlock(&companion_trace_lock);
}

static bool companion_pending_trace_take(uint32_t local_tag,
					 struct companion_trace_pending *out)
{
	k_mutex_lock(&companion_trace_lock, K_FOREVER);
	for (size_t i = 0U; i < ARRAY_SIZE(companion_pending_traces); i++) {
		if (!companion_pending_traces[i].in_use ||
		    companion_pending_traces[i].local_tag != local_tag) {
			continue;
		}

		*out = companion_pending_traces[i];
		companion_pending_traces[i] = (struct companion_trace_pending){0};
		k_mutex_unlock(&companion_trace_lock);
		return true;
	}
	k_mutex_unlock(&companion_trace_lock);

	return false;
}

static void companion_pending_status_clear_all(void)
{
	k_mutex_lock(&companion_status_lock, K_FOREVER);
	memset(companion_pending_status, 0, sizeof(companion_pending_status));
	k_mutex_unlock(&companion_status_lock);
}

static void companion_pending_status_store(
	uint32_t tag,
	const uint8_t public_key_prefix[COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE])
{
	size_t slot = COMPANION_PENDING_STATUS_SLOTS;
	size_t oldest_idx = 0U;
	uint32_t oldest_uptime = 0U;
	uint32_t now = k_uptime_get_32();
	bool have_oldest = false;

	k_mutex_lock(&companion_status_lock, K_FOREVER);
	for (size_t i = 0U; i < ARRAY_SIZE(companion_pending_status); i++) {
		if (!companion_pending_status[i].in_use) {
			slot = i;
			break;
		}

		if (!have_oldest ||
		    (int32_t)(companion_pending_status[i].sent_uptime_ms - oldest_uptime) < 0) {
			oldest_uptime = companion_pending_status[i].sent_uptime_ms;
			oldest_idx = i;
			have_oldest = true;
		}
	}
	if (slot == COMPANION_PENDING_STATUS_SLOTS) {
		slot = oldest_idx;
		LOG_WRN("Companion pending status replaced: tag=%u",
			(unsigned int)companion_pending_status[slot].tag);
	}
	companion_pending_status[slot].in_use = true;
	companion_pending_status[slot].tag = tag;
	companion_pending_status[slot].sent_uptime_ms = now;
	memcpy(companion_pending_status[slot].public_key_prefix, public_key_prefix,
	       sizeof(companion_pending_status[slot].public_key_prefix));
	k_mutex_unlock(&companion_status_lock);
}

static bool companion_pending_status_take(uint32_t tag,
					  struct companion_status_pending *out)
{
	if (out == NULL) {
		return false;
	}

	k_mutex_lock(&companion_status_lock, K_FOREVER);
	for (size_t i = 0U; i < ARRAY_SIZE(companion_pending_status); i++) {
		if (companion_pending_status[i].in_use &&
		    companion_pending_status[i].tag == tag) {
			*out = companion_pending_status[i];
			companion_pending_status[i] = (struct companion_status_pending){0};
			k_mutex_unlock(&companion_status_lock);
			return true;
		}
	}
	k_mutex_unlock(&companion_status_lock);

	return false;
}

static void companion_pending_login_clear_all(void)
{
	k_mutex_lock(&companion_login_lock, K_FOREVER);
	companion_secure_wipe(&companion_pending_login,
			      sizeof(companion_pending_login));
	companion_secure_wipe(&companion_login_start,
			      sizeof(companion_login_start));
	k_mutex_unlock(&companion_login_lock);
}

static int companion_pending_login_store(
	uint32_t tag,
	const uint8_t contact_prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES],
	const uint8_t public_key_prefix[COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE],
	const uint8_t *secret, size_t secret_len)
{
	if (tag == 0U || contact_prefix == NULL || public_key_prefix == NULL ||
	    !companion_management_password_is_valid(secret, secret_len)) {
		return -EINVAL;
	}

	k_mutex_lock(&companion_login_lock, K_FOREVER);
	if (companion_pending_login.in_use) {
		k_mutex_unlock(&companion_login_lock);
		return -EBUSY;
	}
	companion_pending_login.in_use = true;
	companion_pending_login.tag = tag;
	companion_pending_login.sent_uptime_ms = k_uptime_get_32();
	memcpy(companion_pending_login.contact_prefix, contact_prefix,
	       sizeof(companion_pending_login.contact_prefix));
	memcpy(companion_pending_login.public_key_prefix, public_key_prefix,
	       sizeof(companion_pending_login.public_key_prefix));
	memcpy(companion_pending_login.secret, secret, secret_len);
	companion_pending_login.secret_len = secret_len;
	k_mutex_unlock(&companion_login_lock);

	return 0;
}

static bool companion_pending_login_take(uint32_t tag,
					 struct companion_login_pending *out)
{
	if (tag == 0U || out == NULL) {
		return false;
	}

	k_mutex_lock(&companion_login_lock, K_FOREVER);
	if (companion_pending_login.in_use && companion_pending_login.tag == tag) {
		*out = companion_pending_login;
		companion_secure_wipe(&companion_pending_login,
				      sizeof(companion_pending_login));
		k_mutex_unlock(&companion_login_lock);
		return true;
	}
	k_mutex_unlock(&companion_login_lock);

	return false;
}

static int companion_login_start_store(
	const uint8_t contact_prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES],
	const uint8_t public_key_prefix[COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE],
	const uint8_t *secret, size_t secret_len)
{
	if (contact_prefix == NULL || public_key_prefix == NULL ||
	    !companion_management_password_is_valid(secret, secret_len) ||
	    secret_len > sizeof(companion_login_start.secret)) {
		return -EINVAL;
	}

	k_mutex_lock(&companion_login_lock, K_FOREVER);
	if (companion_login_start.in_use || companion_pending_login.in_use) {
		k_mutex_unlock(&companion_login_lock);
		return -EBUSY;
	}

	companion_login_start.in_use = true;
	memcpy(companion_login_start.contact_prefix, contact_prefix,
	       sizeof(companion_login_start.contact_prefix));
	memcpy(companion_login_start.public_key_prefix, public_key_prefix,
	       sizeof(companion_login_start.public_key_prefix));
	memcpy(companion_login_start.secret, secret, secret_len);
	companion_login_start.secret_len = secret_len;
	k_mutex_unlock(&companion_login_lock);

	return 0;
}

static bool companion_login_start_take(struct companion_login_start *out)
{
	if (out == NULL) {
		return false;
	}

	k_mutex_lock(&companion_login_lock, K_FOREVER);
	if (!companion_login_start.in_use) {
		k_mutex_unlock(&companion_login_lock);
		return false;
	}

	*out = companion_login_start;
	companion_secure_wipe(&companion_login_start,
			      sizeof(companion_login_start));
	k_mutex_unlock(&companion_login_lock);
	return true;
}

static void companion_login_sessions_clear_all(void)
{
	k_mutex_lock(&companion_login_lock, K_FOREVER);
	companion_secure_wipe(companion_login_sessions,
			      sizeof(companion_login_sessions));
	k_mutex_unlock(&companion_login_lock);
}

static int companion_login_session_store(
	const uint8_t contact_prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES],
	const uint8_t public_key_prefix[COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE],
	const uint8_t *secret, size_t secret_len)
{
	size_t free_idx = ARRAY_SIZE(companion_login_sessions);
	size_t oldest_idx = 0U;
	uint32_t oldest_uptime = 0U;
	bool have_oldest = false;
	uint32_t now = k_uptime_get_32();

	if (contact_prefix == NULL || public_key_prefix == NULL ||
	    !companion_management_password_is_valid(secret, secret_len)) {
		return -EINVAL;
	}

	k_mutex_lock(&companion_login_lock, K_FOREVER);
	for (size_t i = 0U; i < ARRAY_SIZE(companion_login_sessions); i++) {
		if (!companion_login_sessions[i].in_use) {
			if (free_idx == ARRAY_SIZE(companion_login_sessions)) {
				free_idx = i;
			}
			continue;
		}

		if (memcmp(companion_login_sessions[i].contact_prefix, contact_prefix,
			   sizeof(companion_login_sessions[i].contact_prefix)) == 0) {
			companion_secure_wipe(&companion_login_sessions[i],
					      sizeof(companion_login_sessions[i]));
			free_idx = i;
			break;
		}

		if (!have_oldest ||
		    (int32_t)(companion_login_sessions[i].established_uptime_ms -
			      oldest_uptime) < 0) {
			oldest_uptime = companion_login_sessions[i].established_uptime_ms;
			oldest_idx = i;
			have_oldest = true;
		}
	}

	if (free_idx == ARRAY_SIZE(companion_login_sessions)) {
		free_idx = oldest_idx;
		LOG_WRN("Companion login session table full, replacing prefix=%02x%02x%02x%02x",
			companion_login_sessions[free_idx].contact_prefix[0],
			companion_login_sessions[free_idx].contact_prefix[1],
			companion_login_sessions[free_idx].contact_prefix[2],
			companion_login_sessions[free_idx].contact_prefix[3]);
		companion_secure_wipe(&companion_login_sessions[free_idx],
				      sizeof(companion_login_sessions[free_idx]));
	}

	companion_login_sessions[free_idx].in_use = true;
	companion_login_sessions[free_idx].established_uptime_ms = now;
	memcpy(companion_login_sessions[free_idx].contact_prefix, contact_prefix,
	       sizeof(companion_login_sessions[free_idx].contact_prefix));
	memcpy(companion_login_sessions[free_idx].public_key_prefix,
	       public_key_prefix,
	       sizeof(companion_login_sessions[free_idx].public_key_prefix));
	memcpy(companion_login_sessions[free_idx].secret, secret, secret_len);
	companion_login_sessions[free_idx].secret_len = secret_len;
	k_mutex_unlock(&companion_login_lock);

	return 0;
}

static bool companion_login_session_get(
	const uint8_t contact_prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES],
	struct companion_login_session *out)
{
	if (contact_prefix == NULL || out == NULL) {
		return false;
	}

	k_mutex_lock(&companion_login_lock, K_FOREVER);
	for (size_t i = 0U; i < ARRAY_SIZE(companion_login_sessions); i++) {
		if (companion_login_sessions[i].in_use &&
		    memcmp(companion_login_sessions[i].contact_prefix, contact_prefix,
			   sizeof(companion_login_sessions[i].contact_prefix)) == 0) {
			*out = companion_login_sessions[i];
			k_mutex_unlock(&companion_login_lock);
			return true;
		}
	}
	k_mutex_unlock(&companion_login_lock);

	return false;
}

static int companion_login_session_secret_replace(
	const uint8_t contact_prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES],
	const uint8_t *secret, size_t secret_len)
{
	if (contact_prefix == NULL ||
	    !companion_management_password_is_valid(secret, secret_len)) {
		return -EINVAL;
	}

	k_mutex_lock(&companion_login_lock, K_FOREVER);
	for (size_t i = 0U; i < ARRAY_SIZE(companion_login_sessions); i++) {
		if (!companion_login_sessions[i].in_use ||
		    memcmp(companion_login_sessions[i].contact_prefix, contact_prefix,
			   sizeof(companion_login_sessions[i].contact_prefix)) != 0) {
			continue;
		}

		companion_secure_wipe(companion_login_sessions[i].secret,
				      sizeof(companion_login_sessions[i].secret));
		memcpy(companion_login_sessions[i].secret, secret, secret_len);
		companion_login_sessions[i].secret_len = secret_len;
		companion_login_sessions[i].established_uptime_ms = k_uptime_get_32();
		k_mutex_unlock(&companion_login_lock);
		return 0;
	}
	k_mutex_unlock(&companion_login_lock);

	return -ENOENT;
}

static void companion_pending_cli_clear_all(void)
{
	k_mutex_lock(&companion_cli_lock, K_FOREVER);
	companion_secure_wipe(&companion_cli_start, sizeof(companion_cli_start));
	companion_secure_wipe(companion_pending_cli, sizeof(companion_pending_cli));
	k_mutex_unlock(&companion_cli_lock);
}

static void companion_contact_update_clear_all(void)
{
	k_mutex_lock(&companion_contact_update_lock, K_FOREVER);
	companion_secure_wipe(&companion_contact_update_start,
			      sizeof(companion_contact_update_start));
	k_mutex_unlock(&companion_contact_update_lock);
}

static int companion_cli_start_store(const uint8_t *frame, size_t len)
{
	bool submit = false;
	int rc;

	rc = validate_frame_args(frame, len);
	if (rc != 0) {
		return rc;
	}

	k_mutex_lock(&companion_cli_lock, K_FOREVER);
	if (companion_cli_start.in_use) {
		k_mutex_unlock(&companion_cli_lock);
		return -EBUSY;
	}

	companion_cli_start.in_use = true;
	companion_cli_start.len = len;
	memcpy(companion_cli_start.frame, frame, len);
	if (!companion_cli_start.processing) {
		companion_cli_start.processing = true;
		submit = true;
	}
	k_mutex_unlock(&companion_cli_lock);

	if (!submit) {
		return 0;
	}

	rc = k_work_submit(&companion_cli_start_work);
	if (rc < 0) {
		k_mutex_lock(&companion_cli_lock, K_FOREVER);
		companion_secure_wipe(&companion_cli_start,
				      sizeof(companion_cli_start));
		k_mutex_unlock(&companion_cli_lock);
		return rc;
	}

	return 0;
}

static int companion_contact_update_start_store(const uint8_t *frame, size_t len)
{
	bool submit = false;
	int rc;

	rc = validate_frame_args(frame, len);
	if (rc != 0) {
		return rc;
	}

	k_mutex_lock(&companion_contact_update_lock, K_FOREVER);
	if (companion_contact_update_start.in_use) {
		k_mutex_unlock(&companion_contact_update_lock);
		return -EBUSY;
	}

	companion_contact_update_start.in_use = true;
	companion_contact_update_start.len = len;
	memcpy(companion_contact_update_start.frame, frame, len);
	if (!companion_contact_update_start.processing) {
		companion_contact_update_start.processing = true;
		submit = true;
	}
	k_mutex_unlock(&companion_contact_update_lock);

	if (!submit) {
		return 0;
	}

	rc = k_work_submit(&companion_contact_update_work);
	if (rc < 0) {
		k_mutex_lock(&companion_contact_update_lock, K_FOREVER);
		companion_secure_wipe(&companion_contact_update_start,
				      sizeof(companion_contact_update_start));
		k_mutex_unlock(&companion_contact_update_lock);
		return rc;
	}

	return 0;
}

static bool companion_cli_start_take(struct companion_cli_start *out)
{
	if (out == NULL) {
		return false;
	}

	k_mutex_lock(&companion_cli_lock, K_FOREVER);
	if (!companion_cli_start.in_use) {
		companion_cli_start.processing = false;
		k_mutex_unlock(&companion_cli_lock);
		return false;
	}

	*out = companion_cli_start;
	companion_cli_start.in_use = false;
	companion_cli_start.len = 0U;
	companion_secure_wipe(companion_cli_start.frame,
			      sizeof(companion_cli_start.frame));
	k_mutex_unlock(&companion_cli_lock);
	return true;
}

static bool companion_contact_update_start_take(struct companion_contact_update_start *out)
{
	if (out == NULL) {
		return false;
	}

	k_mutex_lock(&companion_contact_update_lock, K_FOREVER);
	if (!companion_contact_update_start.in_use) {
		companion_contact_update_start.processing = false;
		k_mutex_unlock(&companion_contact_update_lock);
		return false;
	}

	*out = companion_contact_update_start;
	companion_contact_update_start.in_use = false;
	companion_contact_update_start.len = 0U;
	companion_secure_wipe(companion_contact_update_start.frame,
			      sizeof(companion_contact_update_start.frame));
	k_mutex_unlock(&companion_contact_update_lock);
	return true;
}

static int companion_pending_cli_store(const struct companion_cli_pending *pending)
{
	size_t free_idx = ARRAY_SIZE(companion_pending_cli);
	size_t oldest_idx = 0U;
	uint32_t oldest_uptime = 0U;
	bool have_oldest = false;

	if (pending == NULL || pending->tag == 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&companion_cli_lock, K_FOREVER);
	for (size_t i = 0U; i < ARRAY_SIZE(companion_pending_cli); i++) {
		if (!companion_pending_cli[i].in_use) {
			if (free_idx == ARRAY_SIZE(companion_pending_cli)) {
				free_idx = i;
			}
			continue;
		}

		if (!have_oldest ||
		    (int32_t)(companion_pending_cli[i].sent_uptime_ms -
			      oldest_uptime) < 0) {
			oldest_uptime = companion_pending_cli[i].sent_uptime_ms;
			oldest_idx = i;
			have_oldest = true;
		}
	}

	if (free_idx == ARRAY_SIZE(companion_pending_cli)) {
		free_idx = oldest_idx;
		LOG_WRN("Companion CLI pending table full, replacing tag=%u",
			(unsigned int)companion_pending_cli[free_idx].tag);
		companion_secure_wipe(&companion_pending_cli[free_idx],
				      sizeof(companion_pending_cli[free_idx]));
	}

	companion_pending_cli[free_idx] = *pending;
	companion_pending_cli[free_idx].in_use = true;
	k_mutex_unlock(&companion_cli_lock);

	return 0;
}

static bool companion_pending_cli_take(uint32_t tag,
				       struct companion_cli_pending *out)
{
	if (tag == 0U || out == NULL) {
		return false;
	}

	k_mutex_lock(&companion_cli_lock, K_FOREVER);
	for (size_t i = 0U; i < ARRAY_SIZE(companion_pending_cli); i++) {
		if (companion_pending_cli[i].in_use &&
		    companion_pending_cli[i].tag == tag) {
			*out = companion_pending_cli[i];
			companion_secure_wipe(&companion_pending_cli[i],
					      sizeof(companion_pending_cli[i]));
			k_mutex_unlock(&companion_cli_lock);
			return true;
		}
	}
	k_mutex_unlock(&companion_cli_lock);

	return false;
}

static void append_u16_le(uint8_t *frame, size_t *idx, uint16_t value)
{
	sys_put_le16(value, &frame[*idx]);
	*idx += sizeof(value);
}

static void append_u32_le(uint8_t *frame, size_t *idx, uint32_t value)
{
	sys_put_le32(value, &frame[*idx]);
	*idx += sizeof(value);
}

static bool companion_buffer_is_all_zero(const uint8_t *buf, size_t len)
{
	for (size_t i = 0U; i < len; i++) {
		if (buf[i] != 0U) {
			return false;
		}
	}

	return true;
}

static uint32_t read_u32_le(const uint8_t *frame)
{
	return sys_get_le32(frame);
}

static int32_t read_i32_le(const uint8_t *frame)
{
	return (int32_t)read_u32_le(frame);
}

static void append_fixed_string(uint8_t *frame, size_t *idx, size_t field_len, const char *value)
{
	size_t copy_len = 0U;

	if (value != NULL) {
		copy_len = strnlen(value, field_len);
		if (copy_len == field_len) {
			copy_len = field_len - 1U;
		}
		memcpy(&frame[*idx], value, copy_len);
	}

	memset(&frame[*idx + copy_len], 0, field_len - copy_len);
	*idx += field_len;
}

static const char *companion_command_name(uint8_t command)
{
	switch (command) {
	case COMPANION_CMD_APP_START:
		return "APP_START";
	case COMPANION_CMD_SEND_TXT_MSG:
		return "SEND_TXT_MSG";
	case COMPANION_CMD_SEND_CHANNEL_TXT_MSG:
		return "SEND_CHANNEL_TXT_MSG";
	case COMPANION_CMD_GET_CONTACTS:
		return "GET_CONTACTS";
	case COMPANION_CMD_GET_DEVICE_TIME:
		return "GET_DEVICE_TIME";
	case COMPANION_CMD_SET_DEVICE_TIME:
		return "SET_DEVICE_TIME";
	case COMPANION_CMD_SEND_SELF_ADVERT:
		return "SEND_SELF_ADVERT";
	case COMPANION_CMD_SET_NAME:
		return "SET_NAME";
	case COMPANION_CMD_ADD_UPDATE_CONTACT:
		return "ADD_UPDATE_CONTACT";
	case COMPANION_CMD_SYNC_NEXT_MESSAGE:
		return "SYNC_NEXT_MESSAGE";
	case COMPANION_CMD_SET_RADIO:
		return "SET_RADIO";
	case COMPANION_CMD_SET_TX_POWER:
		return "SET_TX_POWER";
	case COMPANION_CMD_RESET_PATH:
		return "RESET_PATH";
	case COMPANION_CMD_SET_COORDINATES:
		return "SET_COORDINATES";
	case COMPANION_CMD_REMOVE_CONTACT:
		return "REMOVE_CONTACT";
	case COMPANION_CMD_GET_BATT_AND_STORAGE:
		return "GET_BATT_AND_STORAGE";
	case COMPANION_CMD_SET_TUNING:
		return "SET_TUNING";
	case COMPANION_CMD_DEVICE_QEURY:
		return "DEVICE_QEURY";
	case COMPANION_CMD_SEND_RAW_DATA:
		return "SEND_RAW_DATA";
	case COMPANION_CMD_SEND_LOGIN:
		return "SEND_LOGIN";
	case COMPANION_CMD_SEND_STATUS_REQ:
		return "SEND_STATUS_REQ";
	case COMPANION_CMD_GET_CONTACT_BY_KEY:
		return "GET_CONTACT_BY_KEY";
	case COMPANION_CMD_GET_CHANNEL:
		return "GET_CHANNEL";
	case COMPANION_CMD_SET_CHANNEL:
		return "SET_CHANNEL";
	case COMPANION_CMD_SEND_TRACE_PATH:
		return "SEND_TRACE_PATH";
	case COMPANION_CMD_SET_DEVICE_PIN:
		return "SET_DEVICE_PIN";
	case COMPANION_CMD_SET_OTHER_PARAMS:
		return "SET_OTHER_PARAMS";
	case COMPANION_CMD_SEND_TELEMETRY_REQ:
		return "SEND_TELEMETRY_REQ";
	case COMPANION_CMD_SEND_BINARY_REQ:
		return "SEND_BINARY_REQ";
	case COMPANION_CMD_SEND_PATH_DISCOVERY_REQ:
		return "SEND_PATH_DISCOVERY_REQ";
	case COMPANION_CMD_SEND_CONTROL_DATA:
		return "SEND_CONTROL_DATA";
	case COMPANION_CMD_SET_AUTOADD_CONFIG:
		return "SET_AUTOADD_CONFIG";
	case COMPANION_CMD_GET_AUTOADD_CONFIG:
		return "GET_AUTOADD_CONFIG";
	case COMPANION_CMD_GET_ALLOWED_REPEAT_FREQ:
		return "GET_ALLOWED_REPEAT_FREQ";
	case COMPANION_CMD_SET_PATH_HASH_MODE:
		return "SET_PATH_HASH_MODE";
	case COMPANION_CMD_SEND_CHANNEL_DATA:
		return "SEND_CHANNEL_DATA";
	default:
		return "UNKNOWN";
	}
}

static void companion_log_text_payload(const char *kind, const uint8_t *payload,
				       size_t payload_len)
{
	char preview[COMPANION_TEXT_LOG_PREVIEW_SIZE + 1U];
	uint8_t dump[COMPANION_TEXT_LOG_PREVIEW_SIZE];
	size_t copy_len;

	if (kind == NULL || payload == NULL) {
		return;
	}

	copy_len = MIN(payload_len, (size_t)COMPANION_TEXT_LOG_PREVIEW_SIZE);
	memcpy(dump, payload, copy_len);
	for (size_t i = 0U; i < copy_len; i++) {
		uint8_t c = payload[i];

		preview[i] = (c >= 0x20U && c != 0x7fU) ? (char)c : '.';
	}
	preview[copy_len] = '\0';

	LOG_DBG("Companion app text payload: kind=%s len=%u preview=\"%s\"%s",
		kind, (unsigned int)payload_len, preview,
		payload_len > copy_len ? "..." : "");
	LOG_HEXDUMP_DBG(dump, copy_len, "Companion app text payload bytes");
}

static void companion_log_node_text_frame(const uint8_t *frame, size_t len)
{
	const size_t payload_offset = 13U;
	size_t payload_len;

	if (frame == NULL || len <= payload_offset) {
		return;
	}

	payload_len = len - payload_offset;
	LOG_DBG("Companion app text frame: kind=node type=%u attempt=%u target=%02x%02x%02x%02x%02x%02x payload_len=%u",
		(unsigned int)frame[1], (unsigned int)frame[2],
		frame[7], frame[8], frame[9], frame[10], frame[11], frame[12],
		(unsigned int)payload_len);
	companion_log_text_payload("node", &frame[payload_offset], payload_len);
}

static void companion_log_channel_text_frame(const uint8_t *frame, size_t len)
{
	const size_t payload_offset = 7U;
	size_t payload_len;

	if (frame == NULL || len <= payload_offset) {
		return;
	}

	payload_len = len - payload_offset;
	LOG_DBG("Companion app text frame: kind=channel type=%u channel=%u payload_len=%u",
		(unsigned int)frame[1], (unsigned int)frame[2],
		(unsigned int)payload_len);
	companion_log_text_payload("channel", &frame[payload_offset], payload_len);
}

static void companion_log_binary_payload(const char *kind, const uint8_t *payload,
					 size_t payload_len)
{
	uint8_t dump[COMPANION_TEXT_LOG_PREVIEW_SIZE];
	size_t copy_len;

	if (kind == NULL || payload == NULL) {
		return;
	}

	copy_len = MIN(payload_len, (size_t)COMPANION_TEXT_LOG_PREVIEW_SIZE);
	LOG_DBG("Companion app binary payload: kind=%s len=%u dump_len=%u%s",
		kind, (unsigned int)payload_len, (unsigned int)copy_len,
		payload_len > copy_len ? "..." : "");
	if (copy_len == 0U) {
		return;
	}

	memcpy(dump, payload, copy_len);
	LOG_HEXDUMP_DBG(dump, copy_len, "Companion app binary payload bytes");
}

static bool companion_command_has_binary_body(uint8_t command)
{
	switch (command) {
	case COMPANION_CMD_SEND_RAW_DATA:
	case COMPANION_CMD_SEND_BINARY_REQ:
	case COMPANION_CMD_SEND_CONTROL_DATA:
	case COMPANION_CMD_SEND_CHANNEL_DATA:
		return true;
	default:
		return false;
	}
}

static void companion_log_binary_command_body(uint8_t command, const uint8_t *frame,
					      size_t len)
{
	if (!companion_command_has_binary_body(command) || frame == NULL || len <= 1U) {
		return;
	}

	LOG_DBG("Companion app binary command body: cmd=%u name=%s body_len=%u",
		(unsigned int)command, companion_command_name(command),
		(unsigned int)(len - 1U));
	companion_log_binary_payload("command-body", &frame[1], len - 1U);
}

static int companion_send_error(uint8_t error_code)
{
	uint8_t frame[] = {
		COMPANION_RESP_CODE_ERR,
		error_code,
	};

	LOG_WRN("Companion app error response: error=%u",
		(unsigned int)error_code);

	return companion_send_frame(frame, sizeof(frame));
}

static int companion_send_ok(void)
{
	uint8_t frame[] = { COMPANION_RESP_CODE_OK };

	return companion_send_frame(frame, sizeof(frame));
}

static uint8_t companion_errno_to_error_code(int rc)
{
	switch (rc) {
	case -ENOENT:
	case -ENODEV:
		return COMPANION_ERR_CODE_NOT_FOUND;
	case -ENOSPC:
	case -ENOMEM:
	case -EBUSY:
		return COMPANION_ERR_CODE_TABLE_FULL;
	case -EINVAL:
	case -EMSGSIZE:
		return COMPANION_ERR_CODE_ILLEGAL_ARG;
	default:
		return COMPANION_ERR_CODE_UNSUPPORTED_CMD;
	}
}

static int companion_send_result(int rc)
{
	return (rc == 0) ? companion_send_ok() : companion_send_error(companion_errno_to_error_code(rc));
}

static int companion_send_sent_with_route(uint8_t route_flag, uint32_t tag,
					  uint32_t timeout_ms)
{
	uint8_t out[10] = {0};
	size_t idx = 0U;

	out[idx++] = COMPANION_RESP_CODE_SENT;
	out[idx++] = route_flag;
	append_u32_le(out, &idx, tag);
	append_u32_le(out, &idx, timeout_ms);

	LOG_DBG("Companion app SENT response: route=%u tag=%u timeout=%u",
		(unsigned int)route_flag, (unsigned int)tag,
		(unsigned int)timeout_ms);

	return companion_send_frame(out, idx);
}

static int companion_send_sent(uint32_t tag, uint32_t timeout_ms)
{
	return companion_send_sent_with_route(0U, tag, timeout_ms);
}

static int companion_cli_response_push(
	const uint8_t public_key_prefix[COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE],
	const char *request_id, size_t request_id_len, const char *response)
{
	uint8_t frame[MESHCORE_COMPANION_MAX_FRAME_SIZE] = {0};
	size_t response_len = response != NULL ? strlen(response) : 0U;
	size_t payload_len = request_id_len + 1U + response_len;
	size_t idx = 0U;

	if (public_key_prefix == NULL || request_id == NULL ||
	    request_id_len == 0U ||
	    request_id_len > COMPANION_CLI_REQUEST_ID_MAX_LEN ||
	    payload_len > MESHCORE_COMPANION_MAX_FRAME_SIZE - 16U) {
		return -EINVAL;
	}

	frame[idx++] = COMPANION_RESP_CODE_CONTACT_MSG_RECV_V3;
	frame[idx++] = 0U;
	frame[idx++] = 0U;
	frame[idx++] = 0U;
	memcpy(&frame[idx], public_key_prefix, COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE);
	idx += COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE;
	frame[idx++] = COMPANION_OUT_PATH_UNKNOWN;
	frame[idx++] = COMPANION_TXT_TYPE_CLI_DATA;
	append_u32_le(frame, &idx, companion_current_timestamp());
	memcpy(&frame[idx], request_id, request_id_len);
	idx += request_id_len;
	frame[idx++] = '|';
	if (response_len > 0U) {
		memcpy(&frame[idx], response, response_len);
		idx += response_len;
	}

	return meshcore_companion_adapter_queue_frame(frame, idx);
}

static int companion_cli_pending_push(const struct companion_cli_pending *pending,
				      const char *response)
{
	if (pending == NULL) {
		return -EINVAL;
	}

	return companion_cli_response_push(pending->public_key_prefix,
					   pending->request_id,
					   pending->request_id_len,
					   response);
}

static int companion_mgmt_cbor_data_encode(const void *msg,
					   const pb_msgdesc_t *fields,
					   size_t max_proto_size,
					   uint8_t *out, size_t out_size,
					   size_t *out_len)
{
	uint8_t *proto;
	pb_ostream_t stream;
	ZCBOR_STATE_E(zse, COMPANION_MGMT_CBOR_STATES, out, out_size, 0);

	if (msg == NULL || fields == NULL || out == NULL || out_len == NULL ||
	    max_proto_size > COMPANION_MGMT_PROTO_MAX_SIZE) {
		return -EINVAL;
	}
	if (out_size < (max_proto_size * 2U) + 16U) {
		return -ENOMEM;
	}

	proto = &out[out_size - max_proto_size];
	stream = pb_ostream_from_buffer(proto, max_proto_size);
	if (!pb_encode(&stream, fields, msg)) {
		companion_secure_wipe(proto, max_proto_size);
		return -EINVAL;
	}

	if (!zcbor_map_start_encode(zse, 1) ||
	    !zcbor_tstr_put_lit(zse, "data") ||
	    !zcbor_bstr_encode_ptr(zse, (const char *)proto,
				   stream.bytes_written) ||
	    !zcbor_map_end_encode(zse, 1)) {
		companion_secure_wipe(proto, max_proto_size);
		return -ENOMEM;
	}

	*out_len = (size_t)(zse->payload - out);
	companion_secure_wipe(proto, max_proto_size);
	return 0;
}

static int companion_mgmt_cbor_data_decode(const uint8_t *payload,
					   size_t payload_len,
					   const pb_msgdesc_t *fields,
					   void *msg, size_t msg_size)
{
	ZCBOR_STATE_D(zsd, COMPANION_MGMT_CBOR_STATES, payload, payload_len, 1, 0);
	struct zcbor_string key;
	struct zcbor_string data = {0};
	bool have_data = false;
	pb_istream_t stream;

	if (payload == NULL || fields == NULL || msg == NULL) {
		return -EINVAL;
	}

	memset(msg, 0, msg_size);
	if (!zcbor_map_start_decode(zsd)) {
		return -EINVAL;
	}

	while (!zcbor_array_at_end(zsd)) {
		if (!zcbor_tstr_decode(zsd, &key)) {
			return -EINVAL;
		}
		if (key.len == 4U && memcmp(key.value, "data", 4U) == 0) {
			if (!zcbor_bstr_decode(zsd, &data)) {
				return -EINVAL;
			}
			have_data = true;
		} else if (!zcbor_any_skip(zsd, NULL)) {
			return -EINVAL;
		}
	}

	if (!zcbor_map_end_decode(zsd) ||
	    (size_t)(zsd->payload - payload) != payload_len || !have_data) {
		return -EINVAL;
	}

	stream = pb_istream_from_buffer(data.value, data.len);
	if (!pb_decode(&stream, fields, msg)) {
		return -EINVAL;
	}

	return stream.bytes_left == 0U ? 0 : -EINVAL;
}

static int companion_mgmt_smp_packet_build(uint16_t group, uint8_t command,
					   uint8_t op, const void *msg,
					   const pb_msgdesc_t *fields,
					   size_t max_proto_size,
					   uint8_t *out, size_t out_size,
					   uint16_t *out_len)
{
	size_t payload_len = 0U;
	int rc;

	if (out == NULL || out_len == NULL || out_size < COMPANION_MGMT_HDR_SIZE) {
		return -EINVAL;
	}

	if (fields != NULL) {
		rc = companion_mgmt_cbor_data_encode(
			msg, fields, max_proto_size, &out[COMPANION_MGMT_HDR_SIZE],
			out_size - COMPANION_MGMT_HDR_SIZE, &payload_len);
		if (rc != 0) {
			return rc;
		}
	}
	if (payload_len > UINT16_MAX ||
	    payload_len + COMPANION_MGMT_HDR_SIZE > out_size) {
		return -EINVAL;
	}

	out[0] = op;
	out[1] = 0U;
	sys_put_be16((uint16_t)payload_len, &out[2]);
	sys_put_be16(group, &out[4]);
	out[6] = 0U;
	out[7] = command;
	*out_len = (uint16_t)(COMPANION_MGMT_HDR_SIZE + payload_len);
	return 0;
}

static int companion_mgmt_response_decode(
	const mbs_management_smp_response_event *event,
	uint16_t group, uint8_t command, uint8_t op,
	const pb_msgdesc_t *fields, void *msg, size_t msg_size)
{
	uint16_t payload_len;

	if (event == NULL || event->response_len < COMPANION_MGMT_HDR_SIZE) {
		return -ETIMEDOUT;
	}

	payload_len = sys_get_be16(&event->response[2]);
	if ((size_t)payload_len + COMPANION_MGMT_HDR_SIZE != event->response_len ||
	    event->response[0] != op ||
	    sys_get_be16(&event->response[4]) != group ||
	    event->response[7] != command) {
		return -EINVAL;
	}

	if (fields == NULL) {
		return 0;
	}

	return companion_mgmt_cbor_data_decode(&event->response[COMPANION_MGMT_HDR_SIZE],
					       payload_len, fields, msg, msg_size);
}

static uint8_t companion_role_to_advert_type(mbs_contact_role role)
{
	switch (role) {
	case MBS_CONTACT_ROLE_CHAT:
	case MBS_CONTACT_ROLE_REPEATER:
	case MBS_CONTACT_ROLE_ROOM:
	case MBS_CONTACT_ROLE_SENSOR:
		return (uint8_t)role;
	default:
		return 0U;
	}
}

static uint8_t companion_telemetry_mode(meshbus_MeshcoreConfig_TelemetryMode mode)
{
	return (uint8_t)CLAMP((int)mode, 0, 3);
}

static uint8_t companion_telemetry_flags(const mbs_meshcore_config *node)
{
	return (companion_telemetry_mode(node->telemetry_mode_environment) << 4) |
	       (companion_telemetry_mode(node->telemetry_mode_locat) << 2) |
	       companion_telemetry_mode(node->telemetry_mode_base);
}

static uint8_t companion_autoadd_config_bits(uint8_t add_contact_config)
{
	return add_contact_config & (MBS_MESHCORE_CONTACT_ADD_FILTER_OVERWRITE_OLDEST |
				  MBS_MESHCORE_CONTACT_ADD_FILTER_CHAT |
				  MBS_MESHCORE_CONTACT_ADD_FILTER_REPEATER |
				  MBS_MESHCORE_CONTACT_ADD_FILTER_ROOM |
				  MBS_MESHCORE_CONTACT_ADD_FILTER_SENSOR);
}

static int companion_meshcore_config_get(mbs_meshcore_config *node)
{
	int rc;

	rc = mbs_meshcore_config_get(node);
	if (rc != 0) {
		LOG_DBG("Companion MeshCore config unavailable: rc=%d", rc);
		memset(node, 0, sizeof(*node));
	}

	return rc;
}

static void companion_public_key_copy(uint8_t *dst, const mbs_meshcore_config *node)
{
	size_t copy_len = MIN(node->public_key.size, (size_t)COMPANION_PUBLIC_KEY_SIZE);

	memset(dst, 0, COMPANION_PUBLIC_KEY_SIZE);
	memcpy(dst, node->public_key.bytes, copy_len);
}

static bool companion_fixed_name_is_empty(const char *name, size_t len)
{
	return strnlen(name, len) == 0U;
}

static const char *companion_contact_display_name(const mbs_contact *contact)
{
	if (!companion_fixed_name_is_empty(contact->alias, sizeof(contact->alias))) {
		return contact->alias;
	}

	return contact->name;
}

static uint8_t companion_encode_contact_path_len(const mbs_contact *contact)
{
	uint32_t hash_size = contact->path_hash_size;
	size_t hop_count;

	if (contact->out_path.size == 0U) {
		if (contact->is_neighbor) {
			return 0U;
		}
		return COMPANION_OUT_PATH_UNKNOWN;
	}

	if (hash_size < 1U || hash_size > 3U || (contact->out_path.size % hash_size) != 0U) {
		return COMPANION_OUT_PATH_UNKNOWN;
	}

	hop_count = contact->out_path.size / hash_size;
	if (hop_count > 63U) {
		return COMPANION_OUT_PATH_UNKNOWN;
	}

	return (uint8_t)(((hash_size - 1U) << 6) | hop_count);
}

static size_t companion_decode_contact_path_len(uint8_t encoded, uint8_t *path_hash_size,
						bool *is_neighbor)
{
	uint8_t mode;
	uint8_t hop_count;

	*path_hash_size = 0U;
	*is_neighbor = false;

	if (encoded == COMPANION_OUT_PATH_UNKNOWN) {
		*path_hash_size = 1U;
		return 0U;
	}

	mode = encoded >> 6;
	if (mode >= 3U) {
		return SIZE_MAX;
	}

	*path_hash_size = mode + 1U;
	hop_count = encoded & 0x3fU;
	*is_neighbor = (hop_count == 0U);

	return (size_t)(*path_hash_size) * hop_count;
}

static bool companion_path_len_to_bytes(uint8_t encoded, size_t *out_len)
{
	uint8_t mode = encoded >> 6;
	uint8_t hop_count = encoded & 0x3fU;
	size_t byte_len;

	if (out_len == NULL || mode >= 3U) {
		return false;
	}

	byte_len = (size_t)(mode + 1U) * hop_count;
	if (byte_len > MBS_MESHCORE_PATH_MAX_LEN) {
		return false;
	}

	*out_len = byte_len;
	return true;
}

static void companion_copy_contact_app_prefix(uint8_t *dst, const uint8_t *key_prefix)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	int rc;

	memset(dst, 0, COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE);

	if (key_prefix == NULL) {
		return;
	}

	rc = mbs_contact_find_by_prefix(key_prefix, &contact);
	if (rc == 0 && contact.public_key.size >= COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE) {
		memcpy(dst, contact.public_key.bytes, COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE);
		return;
	}

	memcpy(dst, key_prefix, MIN((size_t)CONFIG_MBS_CONTACT_PREFIX_BYTES,
				   (size_t)COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE));
	LOG_WRN("Companion contact prefix fallback: rc=%d key_prefix=%02x%02x%02x",
		rc, key_prefix[0], key_prefix[1], key_prefix[2]);
}

static bool companion_copy_contact_public_key(uint8_t *dst, const uint8_t *key_prefix)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	int rc;

	memset(dst, 0, COMPANION_PUBLIC_KEY_SIZE);

	if (key_prefix == NULL) {
		return false;
	}

	rc = mbs_contact_find_by_prefix(key_prefix, &contact);
	if (rc == 0 && contact.public_key.size >= COMPANION_PUBLIC_KEY_SIZE) {
		memcpy(dst, contact.public_key.bytes, COMPANION_PUBLIC_KEY_SIZE);
		return true;
	}

	memcpy(dst, key_prefix,
	       MIN((size_t)CONFIG_MBS_CONTACT_PREFIX_BYTES,
		   (size_t)COMPANION_PUBLIC_KEY_SIZE));
	LOG_WRN("Companion contact key fallback: rc=%d key_prefix=%02x%02x%02x",
		rc, key_prefix[0], key_prefix[1], key_prefix[2]);
	return false;
}

static uint8_t companion_encode_path_len_field(uint8_t path_len, uint8_t path_hash_size)
{
	uint8_t normalized_hash_size = path_hash_size;
	uint8_t hops;

	if (normalized_hash_size == 0U || normalized_hash_size > MBS_MESHCORE_PATH_HASH_SIZE_MAX) {
		normalized_hash_size = 1U;
	}
	if (path_len == 0U) {
		return (uint8_t)((normalized_hash_size - 1U) << 6);
	}
	if ((path_len % normalized_hash_size) != 0U) {
		return 0U;
	}

	hops = path_len / normalized_hash_size;
	if (hops > 0x3fU) {
		return 0U;
	}

	return (uint8_t)(((normalized_hash_size - 1U) << 6) | hops);
}

static uint8_t companion_trace_path_hash_size(uint8_t flags)
{
	uint8_t path_sz = flags & 0x03U;
	uint8_t path_hash_size = (uint8_t)(1U << path_sz);

	if (path_hash_size > MBS_MESHCORE_PATH_HASH_SIZE_MAX) {
		return 0U;
	}

	return path_hash_size;
}

static uint32_t companion_trace_timeout_ms(uint8_t hop_count)
{
	return COMPANION_TRACE_TIMEOUT_BASE_MS +
	       (COMPANION_TRACE_TIMEOUT_PER_HOP_MS * ((uint32_t)hop_count + 1U));
}

static uint8_t companion_contact_path_out_len_field(
	const struct mbs_contact_response_path_event *event)
{
	if (event->out_path_len_field != 0U) {
		return event->out_path_len_field;
	}

	return companion_encode_path_len_field(event->out_path_len, event->path_hash_size);
}

static uint8_t companion_contact_path_in_len_field(
	const struct mbs_contact_response_path_event *event)
{
	if (event->in_path_len_field != 0U || event->in_path_len == 0U) {
		return event->in_path_len_field;
	}

	return companion_encode_path_len_field(event->in_path_len, event->path_hash_size);
}

static int companion_build_contact_frame(uint8_t code, const mbs_contact *contact, uint8_t *out,
					 size_t *out_len)
{
	size_t idx = 0U;
	uint8_t path_len;
	size_t path_copy_len;

	if (contact == NULL || out == NULL || out_len == NULL ||
	    contact->public_key.size < COMPANION_PUBLIC_KEY_SIZE) {
		return -EINVAL;
	}

	out[idx++] = code;
	memcpy(&out[idx], contact->public_key.bytes, COMPANION_PUBLIC_KEY_SIZE);
	idx += COMPANION_PUBLIC_KEY_SIZE;
	out[idx++] = companion_role_to_advert_type(contact->role);
	out[idx++] = (uint8_t)(contact->flags & 0xffU);
	path_len = companion_encode_contact_path_len(contact);
	out[idx++] = path_len;
	path_copy_len = (path_len == COMPANION_OUT_PATH_UNKNOWN) ? 0U :
			MIN(contact->out_path.size, (size_t)COMPANION_CONTACT_PATH_SIZE);
	memcpy(&out[idx], contact->out_path.bytes, path_copy_len);
	memset(&out[idx + path_copy_len], 0, COMPANION_CONTACT_PATH_SIZE - path_copy_len);
	idx += COMPANION_CONTACT_PATH_SIZE;
	append_fixed_string(out, &idx, COMPANION_CONTACT_NAME_SIZE, companion_contact_display_name(contact));
	append_u32_le(out, &idx, contact->last_seen_timestamp);
	append_u32_le(out, &idx, (uint32_t)contact->latitude);
	append_u32_le(out, &idx, (uint32_t)contact->longitude);
	append_u32_le(out, &idx, contact->last_seen_timestamp);

	*out_len = idx;
	return 0;
}

static uint32_t companion_current_timestamp(void)
{
	struct timespec now_ts;

	if (sys_clock_gettime(SYS_CLOCK_REALTIME, &now_ts) == 0 && now_ts.tv_sec > 0) {
		return (uint32_t)MIN((uint64_t)now_ts.tv_sec, (uint64_t)UINT32_MAX);
	}

	return 1U;
}

static int companion_format_clock_set_response(uint64_t unix_time_ms,
					       char *out, size_t out_size)
{
	time_t seconds = (time_t)(unix_time_ms / MSEC_PER_SEC);
	struct tm utc;

	if (out == NULL || out_size == 0U ||
	    (uint64_t)seconds != unix_time_ms / MSEC_PER_SEC ||
	    gmtime_r(&seconds, &utc) == NULL) {
		return -EINVAL;
	}

	(void)snprintk(out, out_size, "OK - clock set: %02d:%02d - %d/%d/%d UTC",
		       utc.tm_hour, utc.tm_min, utc.tm_mday, utc.tm_mon + 1,
		       utc.tm_year + 1900);
	return 0;
}

static void companion_format_scaled_u64(char *out, size_t out_size,
					uint64_t value, uint32_t scale,
					uint8_t decimals)
{
	uint64_t whole;
	uint64_t frac;
	uint64_t frac_div = 1U;
	char frac_buf[10];
	size_t len;

	if (out == NULL || out_size == 0U || scale == 0U ||
	    decimals >= sizeof(frac_buf)) {
		return;
	}

	whole = value / scale;
	for (uint8_t i = 0U; i < decimals; i++) {
		frac_div *= 10U;
	}
	frac = (value % scale) / MAX((uint64_t)1U, (uint64_t)scale / frac_div);
	for (uint8_t i = decimals; i > 0U; i--) {
		frac_buf[i - 1U] = (char)('0' + (frac % 10U));
		frac /= 10U;
	}
	frac_buf[decimals] = '\0';
	len = decimals;
	while (len > 0U && frac_buf[len - 1U] == '0') {
		frac_buf[--len] = '\0';
	}

	if (len == 0U) {
		(void)snprintk(out, out_size, "%llu", (unsigned long long)whole);
	} else {
		(void)snprintk(out, out_size, "%llu.%s",
			       (unsigned long long)whole, frac_buf);
	}
}

static void companion_format_scaled_i32(char *out, size_t out_size,
					int32_t value, uint32_t scale,
					uint8_t decimals)
{
	uint32_t abs_value;
	char tmp[24];

	if (value < 0) {
		abs_value = (uint32_t)(-value);
		companion_format_scaled_u64(tmp, sizeof(tmp), abs_value, scale, decimals);
		(void)snprintk(out, out_size, "-%s", tmp);
	} else {
		companion_format_scaled_u64(out, out_size, (uint32_t)value, scale, decimals);
	}
}

static int companion_parse_u32_arg(const char *text, uint32_t *out)
{
	char *end = NULL;
	unsigned long value;

	if (text == NULL || out == NULL || text[0] == '\0') {
		return -EINVAL;
	}

	errno = 0;
	value = strtoul(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' || value > UINT32_MAX) {
		return -EINVAL;
	}

	*out = (uint32_t)value;
	return 0;
}

static int companion_parse_u64_arg(const char *text, uint64_t *out)
{
	char *end = NULL;
	unsigned long long value;

	if (text == NULL || out == NULL || text[0] == '\0') {
		return -EINVAL;
	}

	errno = 0;
	value = strtoull(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0') {
		return -EINVAL;
	}

	*out = (uint64_t)value;
	return 0;
}

static int companion_hex_nibble(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}

	return -EINVAL;
}

static int companion_parse_fixed_hex(const char *text, uint8_t *out,
				     size_t out_len)
{
	size_t text_len;

	if (text == NULL || out == NULL || out_len == 0U) {
		return -EINVAL;
	}

	text_len = strlen(text);
	if (text_len != out_len * 2U) {
		return -EINVAL;
	}

	for (size_t i = 0U; i < out_len; i++) {
		int hi = companion_hex_nibble(text[i * 2U]);
		int lo = companion_hex_nibble(text[i * 2U + 1U]);

		if (hi < 0 || lo < 0) {
			return -EINVAL;
		}
		out[i] = (uint8_t)((hi << 4) | lo);
	}

	return 0;
}

static int companion_password_to_secret(const char *password,
					uint8_t *secret, size_t secret_size,
					size_t *secret_len)
{
	size_t password_len;

	if (password == NULL || secret == NULL || secret_len == NULL ||
	    secret_size < MBS_MANAGEMENT_SECRET_MIN_LEN) {
		return -EINVAL;
	}

	password_len = strlen(password);
	if (password_len > secret_size ||
	    !companion_management_password_is_valid((const uint8_t *)password,
						    password_len)) {
		return -EINVAL;
	}

	memset(secret, 0, secret_size);
	memcpy(secret, password, password_len);
	*secret_len = password_len;
	return 0;
}

static int companion_parse_scaled_decimal(const char *text, uint32_t scale,
					  uint8_t decimals, uint64_t *out)
{
	uint64_t whole = 0U;
	uint64_t frac = 0U;
	uint8_t frac_digits = 0U;
	uint64_t frac_unit;
	const char *p = text;

	if (text == NULL || out == NULL || text[0] == '\0') {
		return -EINVAL;
	}

	while (*p >= '0' && *p <= '9') {
		whole = whole * 10U + (uint64_t)(*p - '0');
		p++;
	}
	if (p == text) {
		return -EINVAL;
	}
	if (*p == '.') {
		p++;
		frac_unit = scale;
		while (*p >= '0' && *p <= '9') {
			if (frac_digits >= decimals) {
				return -EINVAL;
			}
			frac_unit /= 10U;
			frac += (uint64_t)(*p - '0') * frac_unit;
			frac_digits++;
			p++;
		}
	}
	if (*p != '\0') {
		return -EINVAL;
	}

	*out = whole * scale + frac;
	return 0;
}

static int companion_parse_scaled_i32(const char *text, uint32_t scale,
				      uint8_t decimals, int32_t *out)
{
	bool neg = false;
	uint64_t abs_value;
	int rc;

	if (text == NULL || out == NULL) {
		return -EINVAL;
	}
	if (text[0] == '-') {
		neg = true;
		text++;
	} else if (text[0] == '+') {
		text++;
	}

	rc = companion_parse_scaled_decimal(text, scale, decimals, &abs_value);
	if (rc != 0 || abs_value > (uint64_t)INT32_MAX + (neg ? 1U : 0U)) {
		return -EINVAL;
	}

	*out = neg ? -(int32_t)abs_value : (int32_t)abs_value;
	return 0;
}

static int companion_parse_radio_tuple(const char *text,
				       struct companion_cli_pending *pending)
{
	char buf[64];
	char *parts[4];
	char *cursor = buf;
	uint64_t value;

	if (text == NULL || pending == NULL ||
	    strlen(text) >= sizeof(buf)) {
		return -EINVAL;
	}
	memcpy(buf, text, strlen(text) + 1U);

	for (size_t i = 0U; i < ARRAY_SIZE(parts); i++) {
		parts[i] = cursor;
		cursor = strchr(cursor, ',');
		if (i < ARRAY_SIZE(parts) - 1U) {
			if (cursor == NULL) {
				return -EINVAL;
			}
			*cursor++ = '\0';
		} else if (cursor != NULL) {
			return -EINVAL;
		}
	}

	if (companion_parse_scaled_decimal(parts[0], 1000000U, 3U, &value) != 0) {
		return -EINVAL;
	}
	pending->value.radio.frequency = value;
	if (companion_parse_scaled_decimal(parts[1], 1000U, 3U, &value) != 0 ||
	    value > UINT32_MAX) {
		return -EINVAL;
	}
	pending->value.radio.bandwidth = (uint32_t)value;
	if (companion_parse_u32_arg(parts[2], &pending->value.radio.spread_factor) != 0 ||
	    companion_parse_u32_arg(parts[3], &pending->value.radio.coding_rate) != 0) {
		return -EINVAL;
	}

	return 0;
}

static int companion_cli_request_parse(const uint8_t *payload, size_t payload_len,
				       struct companion_cli_pending *pending,
				       bool *immediate, const char **immediate_rsp)
{
	const uint8_t *sep;
	char command[COMPANION_CLI_COMMAND_MAX_LEN + 1U];
	size_t command_len;

	if (payload == NULL || pending == NULL || immediate == NULL ||
	    immediate_rsp == NULL) {
		return -EINVAL;
	}

	*immediate = false;
	*immediate_rsp = NULL;
	sep = memchr(payload, '|', payload_len);
	if (sep == NULL || sep == payload ||
	    (size_t)(sep - payload) > COMPANION_CLI_REQUEST_ID_MAX_LEN) {
		return -EINVAL;
	}
	command_len = payload_len - (size_t)(sep - payload) - 1U;
	if (command_len == 0U || command_len > COMPANION_CLI_COMMAND_MAX_LEN) {
		return -EINVAL;
	}

	pending->request_id_len = (uint8_t)(sep - payload);
	memcpy(pending->request_id, payload, pending->request_id_len);
	pending->request_id[pending->request_id_len] = '\0';
	memcpy(command, sep + 1U, command_len);
	command[command_len] = '\0';

	if (strcmp(command, "get name") == 0) {
		pending->op = COMPANION_CLI_OP_GET_NAME;
		pending->stage = COMPANION_CLI_STAGE_CONFIG_GET;
		return 0;
	}
	if (strncmp(command, "set name ", 9) == 0) {
		size_t name_len = strlen(&command[9]);

		if (name_len == 0U || name_len >= sizeof(pending->value.text)) {
			return -EINVAL;
		}
		pending->op = COMPANION_CLI_OP_SET_NAME;
		pending->stage = COMPANION_CLI_STAGE_CONFIG_GET;
		memcpy(pending->value.text, &command[9], name_len + 1U);
		return 0;
	}
	if (strcmp(command, "get radio") == 0) {
		pending->op = COMPANION_CLI_OP_GET_RADIO;
		pending->stage = COMPANION_CLI_STAGE_CONFIG_GET;
		return 0;
	}
	if (strncmp(command, "set radio ", 10) == 0) {
		pending->op = COMPANION_CLI_OP_SET_RADIO;
		pending->stage = COMPANION_CLI_STAGE_CONFIG_GET;
		return companion_parse_radio_tuple(&command[10], pending);
	}
	if (strcmp(command, "get owner.info") == 0) {
		*immediate = true;
		*immediate_rsp = "";
		return 0;
	}
	if (strncmp(command, "set owner.info ", 15) == 0 ||
	    strncmp(command, "set guest.password ", 19) == 0 ||
	    strncmp(command, "region ", 7) == 0) {
		*immediate = true;
		*immediate_rsp = "ERR";
		return 0;
	}
	if (strcmp(command, "advert") == 0) {
		pending->op = COMPANION_CLI_OP_ADVERT;
		pending->stage = COMPANION_CLI_STAGE_DIRECT;
		return 0;
	}
	if (strcmp(command, "get advert.interval") == 0) {
		pending->op = COMPANION_CLI_OP_GET_ADVERT_INTERVAL;
		pending->stage = COMPANION_CLI_STAGE_CONFIG_GET;
		return 0;
	}
	if (strcmp(command, "get flood.advert.interval") == 0) {
		pending->op = COMPANION_CLI_OP_GET_FLOOD_ADVERT_INTERVAL;
		pending->stage = COMPANION_CLI_STAGE_CONFIG_GET;
		return 0;
	}
	if (strncmp(command, "set advert.interval ", 20) == 0) {
		uint32_t minutes;

		pending->op = COMPANION_CLI_OP_SET_ADVERT_INTERVAL;
		pending->stage = COMPANION_CLI_STAGE_CONFIG_GET;
		if (companion_parse_u32_arg(&command[20], &minutes) != 0 ||
		    minutes > UINT32_MAX / 60U) {
			return -EINVAL;
		}
		pending->value.value_u32 = minutes * 60U;
		return 0;
	}
	if (strncmp(command, "set flood.advert.interval ", 26) == 0) {
		uint32_t hours;

		pending->op = COMPANION_CLI_OP_SET_FLOOD_ADVERT_INTERVAL;
		pending->stage = COMPANION_CLI_STAGE_CONFIG_GET;
		if (companion_parse_u32_arg(&command[26], &hours) != 0 ||
		    hours > UINT32_MAX / 3600U) {
			return -EINVAL;
		}
		pending->value.value_u32 = hours * 3600U;
		return 0;
	}
	if (strcmp(command, "get lat") == 0) {
		pending->op = COMPANION_CLI_OP_GET_LAT;
		pending->stage = COMPANION_CLI_STAGE_CONFIG_GET;
		return 0;
	}
	if (strncmp(command, "set lat ", 8) == 0) {
		pending->op = COMPANION_CLI_OP_SET_LAT;
		pending->stage = COMPANION_CLI_STAGE_CONFIG_GET;
		return companion_parse_scaled_i32(&command[8], 1000000U, 6U,
						  &pending->value.latitude);
	}
	if (strcmp(command, "get lon") == 0) {
		pending->op = COMPANION_CLI_OP_GET_LON;
		pending->stage = COMPANION_CLI_STAGE_CONFIG_GET;
		return 0;
	}
	if (strncmp(command, "set lon ", 8) == 0) {
		pending->op = COMPANION_CLI_OP_SET_LON;
		pending->stage = COMPANION_CLI_STAGE_CONFIG_GET;
		return companion_parse_scaled_i32(&command[8], 1000000U, 6U,
						  &pending->value.longitude);
	}
	if (strncmp(command, "time ", 5) == 0) {
		uint64_t seconds;
		int rc;

		rc = companion_parse_u64_arg(&command[5], &seconds);
		if (rc != 0 || seconds > UINT64_MAX / MSEC_PER_SEC) {
			return -EINVAL;
		}
		pending->op = COMPANION_CLI_OP_TIME;
		pending->stage = COMPANION_CLI_STAGE_DIRECT;
		pending->value.value_u32 = (uint32_t)MIN(seconds, (uint64_t)UINT32_MAX);
		return 0;
	}
	if (strcmp(command, "clock sync") == 0 ||
	    strcmp(command, "sync_time") == 0 ||
	    strcmp(command, "st") == 0) {
		if (pending->sender_timestamp == 0U ||
		    pending->sender_timestamp == UINT32_MAX) {
			return -EINVAL;
		}
		pending->op = COMPANION_CLI_OP_TIME;
		pending->stage = COMPANION_CLI_STAGE_DIRECT;
		pending->value.value_u32 = pending->sender_timestamp + 1U;
		return 0;
	}
	if (strncmp(command, "password ", 9) == 0) {
		size_t secret_len = strlen(&command[9]);

		if (secret_len == 0U ||
		    companion_password_to_secret(&command[9],
						 pending->value.secret.bytes,
						 sizeof(pending->value.secret.bytes),
						 &pending->value.secret.len) != 0) {
			return -EINVAL;
		}
		pending->op = COMPANION_CLI_OP_PASSWORD;
		pending->stage = COMPANION_CLI_STAGE_DIRECT;
		return 0;
	}
	if (strncmp(command, "set prv.key ", 12) == 0) {
		struct meshcore_local_identity identity;

		if (companion_parse_fixed_hex(
			    &command[12], pending->value.identity.private_key,
			    sizeof(pending->value.identity.private_key)) != 0 ||
		    !meshcore_local_identity_validate_private_key(
			    pending->value.identity.private_key)) {
			return -EINVAL;
		}

		meshcore_local_identity_read_from(
			&identity, pending->value.identity.private_key,
			sizeof(pending->value.identity.private_key));
		memcpy(pending->value.identity.public_key,
		       identity.identity.pub_key,
		       sizeof(pending->value.identity.public_key));
		companion_secure_wipe(&identity, sizeof(identity));
		pending->op = COMPANION_CLI_OP_SET_PRV_KEY;
		pending->stage = COMPANION_CLI_STAGE_CONFIG_GET;
		pending->value.identity.has_private_key = true;
		pending->value.identity.has_public_key = true;
		return 0;
	}
	if (strncmp(command, "set pub.key ", 12) == 0) {
		if (companion_parse_fixed_hex(
			    &command[12], pending->value.identity.public_key,
			    sizeof(pending->value.identity.public_key)) != 0) {
			return -EINVAL;
		}

		pending->op = COMPANION_CLI_OP_SET_PUB_KEY;
		pending->stage = COMPANION_CLI_STAGE_CONFIG_GET;
		pending->value.identity.has_public_key = true;
		return 0;
	}
	if (strcmp(command, "get repeat") == 0) {
		pending->op = COMPANION_CLI_OP_GET_REPEAT;
		pending->stage = COMPANION_CLI_STAGE_CONFIG_GET;
		return 0;
	}
	if (strcmp(command, "set repeat on") == 0 ||
	    strcmp(command, "set repeat off") == 0) {
		pending->op = COMPANION_CLI_OP_SET_REPEAT;
		pending->stage = COMPANION_CLI_STAGE_CONFIG_GET;
		pending->value.value_bool = strcmp(command, "set repeat on") == 0;
		return 0;
	}
	if (strcmp(command, "reboot") == 0) {
		pending->op = COMPANION_CLI_OP_REBOOT;
		pending->stage = COMPANION_CLI_STAGE_DIRECT;
		return 0;
	}
	if (strcmp(command, "ver") == 0) {
		*immediate = true;
		*immediate_rsp = COMPANION_APP_BUILD_VERSION;
		return 0;
	}

	*immediate = true;
	*immediate_rsp = "ERR";
	return 0;
}

static int companion_build_new_advert_frame(const mbs_contact_response_advert_event *event,
					    uint8_t *out, size_t *out_len)
{
	size_t idx = 0U;

	if (event == NULL || out == NULL || out_len == NULL) {
		return -EINVAL;
	}

	out[idx++] = COMPANION_PUSH_CODE_NEW_ADVERT;
	memcpy(&out[idx], event->public_key, COMPANION_PUBLIC_KEY_SIZE);
	idx += COMPANION_PUBLIC_KEY_SIZE;
	out[idx++] = companion_role_to_advert_type(event->role);
	out[idx++] = 0U;
	out[idx++] = COMPANION_OUT_PATH_UNKNOWN;
	memset(&out[idx], 0, COMPANION_CONTACT_PATH_SIZE);
	idx += COMPANION_CONTACT_PATH_SIZE;
	append_fixed_string(out, &idx, COMPANION_CONTACT_NAME_SIZE, event->name);
	append_u32_le(out, &idx, event->advert_timestamp);
	append_u32_le(out, &idx, event->has_position ? (uint32_t)event->latitude : 0U);
	append_u32_le(out, &idx, event->has_position ? (uint32_t)event->longitude : 0U);
	append_u32_le(out, &idx, companion_current_timestamp());

	*out_len = idx;
	return 0;
}

static int companion_contact_from_frame(mbs_contact *contact, const uint8_t *frame, size_t len)
{
	size_t idx = 1U;
	uint8_t path_len;
	uint8_t path_hash_size;
	uint32_t app_flags;
	bool is_neighbor;
	bool has_existing = false;
	size_t decoded_path_bytes;
	uint32_t timestamp;
	uint8_t public_key[COMPANION_PUBLIC_KEY_SIZE];
	int rc;

	if (contact == NULL || frame == NULL || len < COMPANION_CONTACT_UPDATE_MIN_SIZE) {
		return -EINVAL;
	}

	*contact = (mbs_contact)meshbus_Contact_init_zero;
	memcpy(public_key, &frame[idx], sizeof(public_key));
	rc = mbs_contact_find_by_key(public_key, contact);
	if (rc == 0) {
		has_existing = true;
	} else if (rc != -ENOENT) {
		LOG_WRN("Companion contact frame existing lookup failed: "
			"prefix=%02x%02x%02x%02x rc=%d",
			public_key[0], public_key[1], public_key[2], public_key[3], rc);
		return rc;
	}

	contact->public_key.size = COMPANION_PUBLIC_KEY_SIZE;
	memcpy(contact->public_key.bytes, public_key, sizeof(public_key));
	idx += COMPANION_PUBLIC_KEY_SIZE;
	contact->role = (mbs_contact_role)frame[idx++];
	if (contact->role < MBS_CONTACT_ROLE_CHAT || contact->role > MBS_CONTACT_ROLE_SENSOR) {
		contact->role = MBS_CONTACT_ROLE_CHAT;
	}
	app_flags = frame[idx++];
	contact->flags = (contact->flags & ~MBS_CONTACT_FLAG_FAVORITE) |
			 (app_flags & MBS_CONTACT_FLAG_FAVORITE);
	path_len = frame[idx++];
	decoded_path_bytes = companion_decode_contact_path_len(path_len, &path_hash_size, &is_neighbor);
	if (decoded_path_bytes == SIZE_MAX || decoded_path_bytes > COMPANION_CONTACT_PATH_SIZE) {
		return -EINVAL;
	}
	if (path_len != COMPANION_OUT_PATH_UNKNOWN) {
		memset(&contact->out_path, 0, sizeof(contact->out_path));
		contact->out_path.size = decoded_path_bytes;
		memcpy(contact->out_path.bytes, &frame[idx], decoded_path_bytes);
		contact->path_hash_size = path_hash_size;
		contact->is_neighbor = is_neighbor;
	}
	idx += COMPANION_CONTACT_PATH_SIZE;
	if (!companion_fixed_name_is_empty((const char *)&frame[idx], COMPANION_CONTACT_NAME_SIZE)) {
		memset(contact->name, 0, sizeof(contact->name));
		memcpy(contact->name, &frame[idx],
		       MIN(sizeof(contact->name) - 1U, (size_t)COMPANION_CONTACT_NAME_SIZE));
		contact->name[sizeof(contact->name) - 1U] = '\0';
	}
	idx += COMPANION_CONTACT_NAME_SIZE;
	timestamp = read_u32_le(&frame[idx]);
	if (timestamp != 0U || !has_existing) {
		contact->last_seen_timestamp = timestamp;
	}
	idx += 4U;
	contact->latitude = (int32_t)read_u32_le(&frame[idx]);
	idx += 4U;
	contact->longitude = (int32_t)read_u32_le(&frame[idx]);
	idx += 4U;
	if (len >= idx + 4U) {
		timestamp = read_u32_le(&frame[idx]);
		if (timestamp != 0U || !has_existing) {
			contact->last_seen_timestamp = timestamp;
		}
	}
	if (contact->first_seen_timestamp == 0U) {
		contact->first_seen_timestamp = contact->last_seen_timestamp;
	}

	LOG_DBG("Companion contact frame parsed: prefix=%02x%02x%02x%02x "
		"existing=%d flags=0x%02x lat=%d lon=%d",
		public_key[0], public_key[1], public_key[2], public_key[3],
		has_existing, (unsigned int)(contact->flags & 0xffU),
		(int)contact->latitude, (int)contact->longitude);

	return 0;
}

static uint8_t companion_radio_tx_power(void)
{
#if defined(CONFIG_MBS_RADIO)
	mbs_radio_config radio;

	if (mbs_radio_config_get(&radio) == 0) {
		return (uint8_t)radio.tx_power;
	}
#endif

	return 0U;
}

static void companion_radio_params(uint32_t *freq_khz, uint32_t *bandwidth_hz,
				   uint8_t *spread_factor, uint8_t *coding_rate)
{
	*freq_khz = 0U;
	*bandwidth_hz = 0U;
	*spread_factor = 0U;
	*coding_rate = 0U;

#if defined(CONFIG_MBS_RADIO)
	mbs_radio_config radio;

	if (mbs_radio_config_get(&radio) == 0) {
		*freq_khz = (uint32_t)MIN(radio.frequency / 1000ULL, (uint64_t)UINT32_MAX);
		*bandwidth_hz = radio.bandwidth;
		*spread_factor = radio.spread_factor;
		*coding_rate = radio.coding_rate;
	}
#endif
}

static int companion_handle_device_query(const uint8_t *frame, size_t len)
{
	uint8_t out[MESHCORE_COMPANION_MAX_FRAME_SIZE] = {0};
	mbs_meshcore_config node = meshbus_MeshcoreConfig_init_zero;
	uint32_t ble_pin = COMPANION_DEFAULT_BLE_PIN;
	size_t idx = 0U;
	int rc;

	ARG_UNUSED(frame);

	if (len < 2U) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	rc = companion_meshcore_config_get(&node);
	if (rc != 0) {
		return companion_send_error(companion_errno_to_error_code(rc));
	}

	out[idx++] = COMPANION_RESP_CODE_DEVICE_INFO;
	out[idx++] = COMPANION_FIRMWARE_VER_CODE;
	out[idx++] = mbs_contact_store_size() / 2U;
	out[idx++] = mbs_channel_store_size();
	append_u32_le(out, &idx, ble_pin);
	append_fixed_string(out, &idx, COMPANION_DEVICE_INFO_BUILD_DATE_SIZE, "FoBE");
	append_fixed_string(out, &idx, COMPANION_DEVICE_INFO_MANUFACTURER_SIZE, "FoBE");
	append_fixed_string(out, &idx, COMPANION_DEVICE_INFO_VERSION_SIZE, "FoBE Zephyr");
	out[idx++] = node.client_repeat ? 1U : 0U;
	out[idx++] = 0U;

	return companion_send_frame(out, idx);
}

static int companion_handle_app_start(const uint8_t *frame, size_t len)
{
	uint8_t out[MESHCORE_COMPANION_MAX_FRAME_SIZE] = {0};
	mbs_meshcore_config node = {0};
	uint32_t freq_khz;
	uint32_t bandwidth_hz;
	uint8_t spread_factor;
	uint8_t coding_rate;
	size_t idx = 0U;
	size_t name_len;

	ARG_UNUSED(frame);

	if (len < 8U) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	(void)companion_meshcore_config_get(&node);
	companion_radio_params(&freq_khz, &bandwidth_hz, &spread_factor, &coding_rate);

	out[idx++] = COMPANION_RESP_CODE_SELF_INFO;
	out[idx++] = companion_role_to_advert_type(mbs_meshcore_active_role_get());
	out[idx++] = companion_radio_tx_power();
	out[idx++] = COMPANION_MAX_LORA_TX_POWER_DBM;
	companion_public_key_copy(&out[idx], &node);
	idx += COMPANION_PUBLIC_KEY_SIZE;
	append_u32_le(out, &idx, (uint32_t)node.latitude);
	append_u32_le(out, &idx, (uint32_t)node.longitude);
	out[idx++] = node.multi_acks;
	out[idx++] = node.advert_position ? COMPANION_ADV_LOC_SHARE : COMPANION_ADV_LOC_NONE;
	out[idx++] = companion_telemetry_flags(&node);
	out[idx++] = (node.add_contact_config & MBS_MESHCORE_CONTACT_ADD_FILTER_MANUAL_MODE) != 0U;
	append_u32_le(out, &idx, freq_khz);
	append_u32_le(out, &idx, bandwidth_hz);
	out[idx++] = spread_factor;
	out[idx++] = coding_rate;

	name_len = strnlen(node.name, sizeof(node.name));
	name_len = MIN(name_len, sizeof(out) - idx);
	memcpy(&out[idx], node.name, name_len);
	idx += name_len;

	return companion_send_frame(out, idx);
}

static int companion_handle_get_device_time(void)
{
	uint8_t out[5] = {0};
	struct timespec now_ts;
	size_t idx = 0U;
	uint32_t now = 0U;

	if (sys_clock_gettime(SYS_CLOCK_REALTIME, &now_ts) == 0 && now_ts.tv_sec > 0) {
		now = (uint32_t)MIN((uint64_t)now_ts.tv_sec, (uint64_t)UINT32_MAX);
	}

	out[idx++] = COMPANION_RESP_CODE_CURR_TIME;
	append_u32_le(out, &idx, now);

	return companion_send_frame(out, idx);
}

static int companion_handle_set_device_time(const uint8_t *frame, size_t len)
{
	if (len < 5U) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

#if defined(CONFIG_MBS_MESHCORE_COMPANION_PROTOCOL_CLOCK_SET)
	uint32_t secs = read_u32_le(&frame[1]);
	uint64_t applied_unix_ms;
	struct timespec curr_ts;

	if (secs == 0U) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	if (sys_clock_gettime(SYS_CLOCK_REALTIME, &curr_ts) == 0 && curr_ts.tv_sec > 0 &&
	    secs < (uint32_t)MIN((uint64_t)curr_ts.tv_sec, (uint64_t)UINT32_MAX)) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	return companion_send_result(
		mbs_clock_time_set_unix_ms((uint64_t)secs * MSEC_PER_SEC, &applied_unix_ms));
#else
	ARG_UNUSED(frame);
	return companion_send_error(COMPANION_ERR_CODE_UNSUPPORTED_CMD);
#endif
}

static int companion_handle_set_name(const uint8_t *frame, size_t len)
{
	mbs_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	size_t name_len;
	int rc;

	if (len < 2U) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	rc = mbs_meshcore_config_get(&cfg);
	if (rc != 0) {
		return companion_send_error(companion_errno_to_error_code(rc));
	}

	memset(cfg.name, 0, sizeof(cfg.name));
	name_len = MIN(len - 1U, sizeof(cfg.name) - 1U);
	memcpy(cfg.name, &frame[1], name_len);

	return companion_send_result(mbs_meshcore_config_set(&cfg));
}

static int companion_handle_set_coordinates(const uint8_t *frame, size_t len)
{
	mbs_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	int32_t latitude;
	int32_t longitude;
	int rc;

	if (len < 9U) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	latitude = read_i32_le(&frame[1]);
	longitude = read_i32_le(&frame[5]);
	if (latitude < -90000000 || latitude > 90000000 ||
	    longitude < -180000000 || longitude > 180000000) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	rc = mbs_meshcore_config_get(&cfg);
	if (rc != 0) {
		return companion_send_error(companion_errno_to_error_code(rc));
	}
	cfg.latitude = latitude;
	cfg.longitude = longitude;

	return companion_send_result(mbs_meshcore_config_set(&cfg));
}

static int companion_handle_set_radio(const uint8_t *frame, size_t len)
{
	if (len < 11U) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

#if defined(CONFIG_MBS_RADIO)
	mbs_radio_config radio = meshbus_RadioConfig_init_zero;
	uint32_t freq_wire;
	int rc;

	bool repeat_freq_allowed = false;

	rc = mbs_radio_config_get(&radio);
	if (rc != 0) {
		return companion_send_error(companion_errno_to_error_code(rc));
	}

	freq_wire = read_u32_le(&frame[1]);
	radio.frequency = (freq_wire >= 400000000U) ? (uint64_t)freq_wire :
						   (uint64_t)freq_wire * 1000ULL;
	radio.bandwidth = read_u32_le(&frame[5]);
	radio.spread_factor = frame[9];
	radio.coding_rate = frame[10];

	LOG_DBG("Companion set radio: freq_wire=%u frequency_hz=%llu bandwidth_hz=%u sf=%u cr=%u repeat=%u",
		freq_wire, (unsigned long long)radio.frequency, radio.bandwidth,
		radio.spread_factor, radio.coding_rate, (len >= 12U) ? frame[11] : 0U);

	for (size_t i = 0U; i < ARRAY_SIZE(companion_repeat_freq_ranges); i++) {
		const struct companion_repeat_freq_range *range = &companion_repeat_freq_ranges[i];
		uint32_t freq_khz;

		if ((radio.frequency % 1000ULL) != 0ULL ||
		    radio.frequency > (uint64_t)UINT32_MAX * 1000ULL) {
			break;
		}
		freq_khz = (uint32_t)(radio.frequency / 1000ULL);
		if (freq_khz >= range->lower_khz && freq_khz <= range->upper_khz) {
			repeat_freq_allowed = true;
			break;
		}
	}

	if (len >= 12U && frame[11] != 0U && !repeat_freq_allowed) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	rc = mbs_radio_config_set(&radio);
	if (rc != 0) {
		return companion_send_error(companion_errno_to_error_code(rc));
	}

	if (len >= 12U) {
		mbs_meshcore_config node = meshbus_MeshcoreConfig_init_zero;

		rc = mbs_meshcore_config_get(&node);
		if (rc != 0) {
			return companion_send_error(companion_errno_to_error_code(rc));
		}
		node.client_repeat = frame[11] != 0U;
		rc = mbs_meshcore_config_set(&node);
		if (rc != 0) {
			return companion_send_error(companion_errno_to_error_code(rc));
		}
	}

	return companion_send_ok();
#else
	ARG_UNUSED(frame);
	return companion_send_error(COMPANION_ERR_CODE_NOT_FOUND);
#endif
}

static int companion_handle_set_tx_power(const uint8_t *frame, size_t len)
{
	if (len < 2U) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

#if defined(CONFIG_MBS_RADIO)
	mbs_radio_config radio = meshbus_RadioConfig_init_zero;
	int rc;

	rc = mbs_radio_config_get(&radio);
	if (rc != 0) {
		return companion_send_error(companion_errno_to_error_code(rc));
	}
	radio.tx_power = (int8_t)frame[1];

	return companion_send_result(mbs_radio_config_set(&radio));
#else
	ARG_UNUSED(frame);
	return companion_send_error(COMPANION_ERR_CODE_NOT_FOUND);
#endif
}

static int companion_handle_set_tuning(const uint8_t *frame, size_t len)
{
	if (len < 9U) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	ARG_UNUSED(frame);
	return companion_send_ok();
}

static int companion_handle_set_device_pin(const uint8_t *frame, size_t len)
{
	if (len < 5U) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

#if defined(CONFIG_MBS_BLUETOOTH)
	mbs_bluetooth_config cfg = meshbus_BluetoothConfig_init_zero;
	uint32_t pin = read_u32_le(&frame[1]);
	int rc;

	if (pin > 999999U) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	rc = mbs_bluetooth_config_get(&cfg);
	if (rc != 0) {
		return companion_send_error(companion_errno_to_error_code(rc));
	}
	cfg.passkey_mode = MBS_BLUETOOTH_PASSKEY_MODE_FIXED;
	cfg.fixed_passkey = pin;

	return companion_send_result(mbs_bluetooth_config_set(&cfg));
#else
	ARG_UNUSED(frame);
	return companion_send_ok();
#endif
}

static int companion_handle_set_other_params(const uint8_t *frame, size_t len)
{
	mbs_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	uint8_t telemetry_mode;
	int rc;

	if (len < 4U) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	rc = mbs_meshcore_config_get(&cfg);
	if (rc != 0) {
		return companion_send_error(companion_errno_to_error_code(rc));
	}

	if (frame[1] != 0U) {
		cfg.add_contact_config |= MBS_MESHCORE_CONTACT_ADD_FILTER_MANUAL_MODE;
	} else {
		cfg.add_contact_config &= (uint8_t)~MBS_MESHCORE_CONTACT_ADD_FILTER_MANUAL_MODE;
	}

	telemetry_mode = frame[2];
	cfg.telemetry_mode_base = telemetry_mode & 0x03U;
	cfg.telemetry_mode_locat = (telemetry_mode >> 2) & 0x03U;
	cfg.telemetry_mode_environment = (telemetry_mode >> 4) & 0x03U;
	cfg.advert_position = frame[3] != 0U;
	if (len >= 5U) {
		cfg.multi_acks = frame[4];
	}

	return companion_send_result(mbs_meshcore_config_set(&cfg));
}

static int companion_handle_set_autoadd_config(const uint8_t *frame, size_t len)
{
	mbs_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	uint8_t autoadd_bits;
	int rc;

	if (len < 2U) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	rc = mbs_meshcore_config_get(&cfg);
	if (rc != 0) {
		return companion_send_error(companion_errno_to_error_code(rc));
	}

	autoadd_bits = companion_autoadd_config_bits(frame[1]);
	cfg.add_contact_config = (cfg.add_contact_config &
			       (uint8_t)~companion_autoadd_config_bits(UINT8_MAX)) |
			      autoadd_bits;
	if (len >= 3U) {
		cfg.add_contact_hops_limit = MIN(frame[2], (uint8_t)COMPANION_AUTOADD_MAX_HOPS);
	}

	return companion_send_result(mbs_meshcore_config_set(&cfg));
}

static int companion_handle_get_autoadd_config(void)
{
	uint8_t out[3] = {0};
	mbs_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	size_t idx = 0U;
	int rc;

	rc = mbs_meshcore_config_get(&cfg);
	if (rc != 0) {
		return companion_send_error(companion_errno_to_error_code(rc));
	}

	out[idx++] = COMPANION_RESP_CODE_AUTOADD_CONFIG;
	out[idx++] = companion_autoadd_config_bits(cfg.add_contact_config);
	out[idx++] = cfg.add_contact_hops_limit;

	return companion_send_frame(out, idx);
}

static int companion_handle_get_allowed_repeat_freq(void)
{
	uint8_t out[1U + ARRAY_SIZE(companion_repeat_freq_ranges) * 8U] = {0};
	size_t idx = 0U;

	out[idx++] = COMPANION_RESP_CODE_ALLOWED_REPEAT_FREQ;
	for (size_t i = 0U; i < ARRAY_SIZE(companion_repeat_freq_ranges); i++) {
		append_u32_le(out, &idx, companion_repeat_freq_ranges[i].lower_khz);
		append_u32_le(out, &idx, companion_repeat_freq_ranges[i].upper_khz);
	}

	return companion_send_frame(out, idx);
}

static int companion_handle_set_path_hash_mode(const uint8_t *frame, size_t len)
{
	mbs_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	uint8_t mode;
	int rc;

	if (len < 3U || frame[1] != 0U || frame[2] > 2U) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	mode = frame[2];
	rc = mbs_meshcore_config_get(&cfg);
	if (rc != 0) {
		return companion_send_error(companion_errno_to_error_code(rc));
	}
	cfg.path_hash_size = mode + 1U;

	return companion_send_result(mbs_meshcore_config_set(&cfg));
}

static int companion_handle_get_batt_and_storage(void)
{
	uint8_t out[11] = {0};
	size_t idx = 0U;
	uint16_t voltage_mv = 0U;
	uint32_t storage_used_kb = 0U;
	uint32_t storage_total_kb = 0U;

#if defined(CONFIG_MBS_POWER)
	uint8_t soc_percent;
	uint16_t temperature_dk;

	(void)mbs_power_fuel_gauge_get(&voltage_mv, &soc_percent, &temperature_dk);
#endif

	/*
	 * FoBE does not currently expose an app-facing storage usage API, so keep
	 * the storage fields as documented placeholders until such a service exists.
	 */
	out[idx++] = COMPANION_RESP_CODE_BATT_AND_STORAGE;
	append_u16_le(out, &idx, voltage_mv);
	append_u32_le(out, &idx, storage_used_kb);
	append_u32_le(out, &idx, storage_total_kb);

	return companion_send_frame(out, idx);
}

static void companion_contact_sync_schedule(k_timeout_t delay)
{
	(void)k_work_reschedule(&companion_contact_sync_work, delay);
}

static void companion_contact_sync_clear(void)
{
	k_mutex_lock(&companion_contact_sync_lock, K_FOREVER);
	companion_contact_sync = (struct companion_contact_sync_state){0};
	k_mutex_unlock(&companion_contact_sync_lock);
}

static bool companion_contact_sync_snapshot(struct companion_contact_sync_state *snapshot)
{
	bool active;

	k_mutex_lock(&companion_contact_sync_lock, K_FOREVER);
	active = companion_contact_sync.active;
	if (active) {
		*snapshot = companion_contact_sync;
	}
	k_mutex_unlock(&companion_contact_sync_lock);

	return active;
}

static void companion_contact_sync_finish_success(const struct companion_contact_sync_state *snapshot)
{
	bool finished = false;

	k_mutex_lock(&companion_contact_sync_lock, K_FOREVER);
	if (companion_contact_sync.active &&
	    companion_contact_sync.cursor == snapshot->cursor &&
	    companion_contact_sync.since == snapshot->since &&
	    companion_contact_sync.last_modified == snapshot->last_modified &&
	    companion_contact_sync.emitted == snapshot->emitted) {
		companion_contact_sync = (struct companion_contact_sync_state){0};
		finished = true;
	}
	k_mutex_unlock(&companion_contact_sync_lock);

	if (finished) {
		LOG_DBG("Companion contacts sync complete: emitted=%u since=%u last_modified=%u",
			(unsigned int)snapshot->emitted, (unsigned int)snapshot->since,
			(unsigned int)snapshot->last_modified);
	}
}

static void companion_contact_sync_advance_contact(
	const struct companion_contact_sync_state *snapshot,
	uint32_t last_modified)
{
	bool schedule = false;

	k_mutex_lock(&companion_contact_sync_lock, K_FOREVER);
	if (companion_contact_sync.active &&
	    companion_contact_sync.cursor == snapshot->cursor &&
	    companion_contact_sync.since == snapshot->since &&
	    companion_contact_sync.last_modified == snapshot->last_modified &&
	    companion_contact_sync.emitted == snapshot->emitted) {
		companion_contact_sync.cursor = snapshot->cursor + 1U;
		companion_contact_sync.last_modified =
			MAX(snapshot->last_modified, last_modified);
		companion_contact_sync.emitted = snapshot->emitted + 1U;
		schedule = true;
	}
	k_mutex_unlock(&companion_contact_sync_lock);

	if (schedule) {
		companion_contact_sync_schedule(K_MSEC(COMPANION_CONTACT_SYNC_FRAME_DELAY_MS));
	}
}

static void companion_contact_sync_skip_cursor(
	const struct companion_contact_sync_state *snapshot)
{
	bool schedule = false;

	k_mutex_lock(&companion_contact_sync_lock, K_FOREVER);
	if (companion_contact_sync.active &&
	    companion_contact_sync.cursor == snapshot->cursor &&
	    companion_contact_sync.since == snapshot->since &&
	    companion_contact_sync.last_modified == snapshot->last_modified &&
	    companion_contact_sync.emitted == snapshot->emitted) {
		companion_contact_sync.cursor = snapshot->cursor + 1U;
		schedule = true;
	}
	k_mutex_unlock(&companion_contact_sync_lock);

	if (schedule) {
		companion_contact_sync_schedule(K_NO_WAIT);
	}
}

static void companion_contact_sync_work_handler(struct k_work *work)
{
	struct companion_contact_sync_state snapshot;
	uint8_t out[MESHCORE_COMPANION_MAX_FRAME_SIZE] = {0};
	mbs_contact contact = meshbus_Contact_init_zero;
	size_t out_len;
	size_t idx = 0U;
	size_t store_size;
	int rc;

	ARG_UNUSED(work);

	if (!companion_contact_sync_snapshot(&snapshot)) {
		return;
	}

	store_size = mbs_contact_store_size();
	while (snapshot.cursor < store_size) {
		rc = mbs_contact_get(snapshot.cursor, &contact);
		if (rc != 0 || contact.last_seen_timestamp <= snapshot.since) {
			companion_contact_sync_skip_cursor(&snapshot);
			return;
		}

		rc = companion_build_contact_frame(COMPANION_RESP_CODE_CONTACT, &contact,
						   out, &out_len);
		if (rc != 0) {
			companion_contact_sync_skip_cursor(&snapshot);
			return;
		}

		LOG_DBG("Companion contact emit: key=%02x%02x%02x%02x%02x%02x type=%u "
			"path=0x%02x neighbor=%u hash_size=%u out_path_len=%u name=%s",
			contact.public_key.bytes[0], contact.public_key.bytes[1],
			contact.public_key.bytes[2], contact.public_key.bytes[3],
			contact.public_key.bytes[4], contact.public_key.bytes[5],
			(unsigned int)out[33], (unsigned int)out[35],
			contact.is_neighbor ? 1U : 0U, (unsigned int)contact.path_hash_size,
			(unsigned int)contact.out_path.size, companion_contact_display_name(&contact));

		rc = companion_send_frame(out, out_len);
		if (rc == 0) {
			companion_contact_sync_advance_contact(&snapshot,
							       contact.last_seen_timestamp);
		} else if (companion_send_error_retryable(rc)) {
			companion_contact_sync_schedule(K_MSEC(COMPANION_CONTACT_SYNC_FRAME_DELAY_MS));
		} else {
			companion_contact_sync_clear();
		}
		return;
	}

	out[idx++] = COMPANION_RESP_CODE_END_OF_CONTACTS;
	append_u32_le(out, &idx, snapshot.last_modified);
	rc = companion_send_frame(out, idx);
	if (rc == 0) {
		companion_contact_sync_finish_success(&snapshot);
	} else if (companion_send_error_retryable(rc)) {
		companion_contact_sync_schedule(K_MSEC(COMPANION_CONTACT_SYNC_FRAME_DELAY_MS));
	} else {
		companion_contact_sync_clear();
	}
}

static int companion_handle_get_contacts(const uint8_t *frame, size_t len)
{
	uint32_t since = 0U;
	uint8_t out[5] = {0};
	int rc;
	size_t idx = 0U;

	if (len >= 5U) {
		since = read_u32_le(&frame[1]);
	}

	k_mutex_lock(&companion_contact_sync_lock, K_FOREVER);
	if (companion_contact_sync.active) {
		k_mutex_unlock(&companion_contact_sync_lock);
		return companion_send_error(COMPANION_ERR_CODE_BAD_STATE);
	}
	companion_contact_sync = (struct companion_contact_sync_state){
		.active = true,
		.since = since,
	};
	k_mutex_unlock(&companion_contact_sync_lock);

	out[idx++] = COMPANION_RESP_CODE_CONTACTS_START;
	append_u32_le(out, &idx, mbs_contact_store_count());

	rc = companion_send_frame(out, idx);
	if (rc != 0) {
		companion_contact_sync_clear();
		return rc;
	}

	companion_contact_sync_schedule(K_MSEC(COMPANION_CONTACT_SYNC_FRAME_DELAY_MS));

	return 0;
}

static int companion_handle_get_contact_by_key(const uint8_t *frame, size_t len)
{
	uint8_t out[MESHCORE_COMPANION_MAX_FRAME_SIZE] = {0};
	mbs_contact contact = meshbus_Contact_init_zero;
	size_t out_len;
	int rc;

	if (len < 1U + COMPANION_PUBLIC_KEY_SIZE) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	rc = mbs_contact_find_by_key(&frame[1], &contact);
	if (rc != 0) {
		return companion_send_error(companion_errno_to_error_code(rc));
	}

	rc = companion_build_contact_frame(COMPANION_RESP_CODE_CONTACT, &contact, out, &out_len);
	if (rc != 0) {
		return companion_send_error(companion_errno_to_error_code(rc));
	}

	return companion_send_frame(out, out_len);
}

static int companion_process_add_update_contact(const uint8_t *frame, size_t len)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	int set_rc;
	int send_rc;
	int rc;

	rc = companion_contact_from_frame(&contact, frame, len);
	if (rc != 0) {
		return companion_send_error(rc == -EINVAL ? COMPANION_ERR_CODE_ILLEGAL_ARG :
					    companion_errno_to_error_code(rc));
	}

	set_rc = mbs_contact_set(contact.public_key.bytes, &contact);
	send_rc = companion_send_result(set_rc);
	if (set_rc != 0 || send_rc != 0) {
		LOG_WRN("Companion add/update contact failed: set_rc=%d send_rc=%d",
			set_rc, send_rc);
	}

	return send_rc;
}

static void companion_contact_update_work_handler(struct k_work *work)
{
	struct companion_contact_update_start start = {0};

	ARG_UNUSED(work);

	while (companion_contact_update_start_take(&start)) {
		LOG_DBG("Companion add/update contact work start: len=%u",
			(unsigned int)start.len);
		(void)companion_process_add_update_contact(start.frame, start.len);
		companion_secure_wipe(&start, sizeof(start));
	}
}

static int companion_handle_add_update_contact(const uint8_t *frame, size_t len)
{
	int rc;

	/* Contact persistence can block; keep Bluetooth RX workqueue lightweight. */
	rc = companion_contact_update_start_store(frame, len);
	if (rc != 0) {
		LOG_WRN("Companion add/update contact schedule failed: rc=%d", rc);
		return companion_send_error(companion_errno_to_error_code(rc));
	}

	return 0;
}

static int companion_handle_remove_contact(const uint8_t *frame, size_t len)
{
	if (len < 1U + COMPANION_PUBLIC_KEY_SIZE) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	return companion_send_result(mbs_contact_reset(&frame[1]));
}

static int companion_handle_reset_path(const uint8_t *frame, size_t len)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	mbs_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	int rc;

	if (len < 1U + COMPANION_PUBLIC_KEY_SIZE) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	rc = mbs_contact_find_by_key(&frame[1], &contact);
	if (rc != 0) {
		return companion_send_error(companion_errno_to_error_code(rc));
	}

	contact.out_path.size = 0U;
	memset(contact.out_path.bytes, 0, sizeof(contact.out_path.bytes));
	contact.is_neighbor = false;
	if (contact.path_hash_size == 0U || contact.path_hash_size > MBS_MESHCORE_PATH_HASH_SIZE_MAX) {
		if (mbs_meshcore_config_get(&cfg) == 0 && cfg.path_hash_size > 0U &&
		    cfg.path_hash_size <= MBS_MESHCORE_PATH_HASH_SIZE_MAX) {
			contact.path_hash_size = cfg.path_hash_size;
		} else {
			contact.path_hash_size = 1U;
		}
	}

	return companion_send_result(mbs_contact_set(contact.public_key.bytes, &contact));
}

static int companion_handle_get_channel(const uint8_t *frame, size_t len)
{
	uint8_t out[1U + 1U + COMPANION_CHANNEL_NAME_SIZE + COMPANION_CHANNEL_SECRET_SIZE] = {0};
	mbs_channel channel = meshbus_Channel_init_zero;
	size_t idx = 0U;
	uint8_t channel_idx;
	size_t secret_len;
	int rc;

	if (len < 2U) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	channel_idx = frame[1];
	rc = mbs_channel_get(channel_idx, &channel);
	if (rc != 0) {
		uint8_t store_size = mbs_channel_store_size();

		LOG_DBG("Companion get channel: index=%u rc=%d count=%u size=%u",
			(unsigned int)channel_idx, rc,
			(unsigned int)mbs_channel_store_count(),
			(unsigned int)store_size);
		if (rc != -ENOENT || channel_idx >= store_size) {
			return companion_send_error(companion_errno_to_error_code(rc));
		}
	}

	out[idx++] = COMPANION_RESP_CODE_CHANNEL_INFO;
	out[idx++] = channel_idx;
	if (rc == 0) {
		append_fixed_string(out, &idx, COMPANION_CHANNEL_NAME_SIZE, channel.name);
		secret_len = MIN(channel.secret.size, (size_t)COMPANION_CHANNEL_SECRET_SIZE);
		memcpy(&out[idx], channel.secret.bytes, secret_len);
	} else {
		idx += COMPANION_CHANNEL_NAME_SIZE;
	}
	idx += COMPANION_CHANNEL_SECRET_SIZE;

	return companion_send_frame(out, idx);
}

static int companion_handle_set_channel(const uint8_t *frame, size_t len)
{
	char name[COMPANION_CHANNEL_NAME_SIZE] = {0};
	uint8_t index;
	const uint8_t *secret;
	int rc;

	if (len < 2U + COMPANION_CHANNEL_NAME_SIZE + COMPANION_CHANNEL_SECRET_SIZE) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	index = frame[1];
	secret = &frame[2U + COMPANION_CHANNEL_NAME_SIZE];
	memcpy(name, &frame[2], sizeof(name) - 1U);
	LOG_DBG("Companion set channel: index=%u name=%s count=%u size=%u",
		(unsigned int)index, name, (unsigned int)mbs_channel_store_count(),
		(unsigned int)mbs_channel_store_size());

	if (name[0] == '\0' &&
	    companion_buffer_is_all_zero(secret, COMPANION_CHANNEL_SECRET_SIZE)) {
		rc = mbs_channel_reset(index);
		LOG_DBG("Companion clear channel result: index=%u rc=%d",
			(unsigned int)index, rc);
		return companion_send_result(rc);
	}

	rc = mbs_channel_set(index, secret, COMPANION_CHANNEL_SECRET_SIZE, name);
	LOG_DBG("Companion set channel result: requested_index=%u rc=%d count=%u size=%u",
		(unsigned int)index, rc, (unsigned int)mbs_channel_store_count(),
		(unsigned int)mbs_channel_store_size());
	return companion_send_result(rc);
}

static int companion_cli_management_submit(struct companion_cli_pending *pending,
					   uint16_t group, uint8_t command,
					   uint8_t op, const void *msg,
					   const pb_msgdesc_t *fields,
					   size_t max_proto_size)
{
#if defined(CONFIG_MBS_MANAGEMENT)
	mbs_management_smp_request_event *request;
	uint32_t tag = 0U;
	int rc;

	if (pending == NULL) {
		return -EINVAL;
	}

	request = k_malloc(sizeof(*request));
	if (request == NULL) {
		return -ENOMEM;
	}
	memset(request, 0, sizeof(*request));
	memcpy(request->contact_prefix, pending->contact_prefix,
	       sizeof(request->contact_prefix));
	rc = companion_mgmt_smp_packet_build(group, command, op, msg, fields,
					     max_proto_size, request->packet,
					     sizeof(request->packet),
					     &request->packet_len);
	if (rc != 0) {
		goto out;
	}

	rc = mbs_management_smp_request_with_secret(
		request, pending->secret, pending->secret_len, &tag);
	if (rc != 0) {
		goto out;
	}

	pending->tag = tag;
	pending->sent_uptime_ms = k_uptime_get_32();
	rc = companion_pending_cli_store(pending);

out:
	companion_secure_wipe(request, sizeof(*request));
	k_free(request);
	return rc;
#else
	ARG_UNUSED(pending);
	ARG_UNUSED(group);
	ARG_UNUSED(command);
	ARG_UNUSED(op);
	ARG_UNUSED(msg);
	ARG_UNUSED(fields);
	ARG_UNUSED(max_proto_size);

	return -ENOTSUP;
#endif
}

static int companion_cli_meshcore_config_get_submit(
	struct companion_cli_pending *pending)
{
	return companion_cli_management_submit(
		pending,
		meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
		meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_READ, NULL, NULL, 0U);
}

static int companion_cli_meshcore_config_set_submit(
	struct companion_cli_pending *pending,
	const meshbus_MeshcoreConfig *config)
{
	meshbus_MeshcoreConfigSetRequest req =
		meshbus_MeshcoreConfigSetRequest_init_zero;

	if (pending == NULL || config == NULL) {
		return -EINVAL;
	}

	req.has_config = true;
	req.config = *config;
	pending->stage = COMPANION_CLI_STAGE_CONFIG_SET;
	return companion_cli_management_submit(
		pending,
		meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
		meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_WRITE, &req, meshbus_MeshcoreConfigSetRequest_fields,
		meshbus_MeshcoreConfigSetRequest_size);
}

static int companion_cli_radio_config_get_submit(
	struct companion_cli_pending *pending)
{
	return companion_cli_management_submit(
		pending,
		meshbus_RadioMgmtGroupId_RADIO_MGMT_GROUP_ID_MESHBUS_RADIO,
		meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_READ, NULL, NULL, 0U);
}

static int companion_cli_radio_config_set_submit(
	struct companion_cli_pending *pending,
	const meshbus_RadioConfig *config)
{
	meshbus_RadioConfigSetRequest req =
		meshbus_RadioConfigSetRequest_init_zero;

	if (pending == NULL || config == NULL) {
		return -EINVAL;
	}

	req.has_config = true;
	req.config = *config;
	pending->stage = COMPANION_CLI_STAGE_CONFIG_SET;
	return companion_cli_management_submit(
		pending,
		meshbus_RadioMgmtGroupId_RADIO_MGMT_GROUP_ID_MESHBUS_RADIO,
		meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_WRITE, &req, meshbus_RadioConfigSetRequest_fields,
		meshbus_RadioConfigSetRequest_size);
}

static int companion_cli_direct_submit(struct companion_cli_pending *pending)
{
	if (pending == NULL) {
		return -EINVAL;
	}

	switch (pending->op) {
	case COMPANION_CLI_OP_ADVERT: {
		meshbus_MeshcoreAdvertRequest req =
			meshbus_MeshcoreAdvertRequest_init_zero;

		return companion_cli_management_submit(
			pending,
			meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
			meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_ADVERT,
			MGMT_OP_WRITE, &req, meshbus_MeshcoreAdvertRequest_fields,
			meshbus_MeshcoreAdvertRequest_size);
	}
	case COMPANION_CLI_OP_TIME: {
		meshbus_ClockTimeSetRequest req =
			meshbus_ClockTimeSetRequest_init_zero;

		req.unix_time_ms = (uint64_t)pending->value.value_u32 * MSEC_PER_SEC;
		return companion_cli_management_submit(
			pending,
			meshbus_ClockMgmtGroupId_CLOCK_MGMT_GROUP_ID_MESHBUS_CLOCK,
			meshbus_ClockMgmtCommandId_CLOCK_MGMT_COMMAND_ID_TIME_SET,
			MGMT_OP_WRITE, &req, meshbus_ClockTimeSetRequest_fields,
			meshbus_ClockTimeSetRequest_size);
	}
	case COMPANION_CLI_OP_PASSWORD: {
		meshbus_ManagementSecretSetRequest req =
			meshbus_ManagementSecretSetRequest_init_zero;

		req.secret.size = pending->value.secret.len;
		memcpy(req.secret.bytes, pending->value.secret.bytes,
		       pending->value.secret.len);
		return companion_cli_management_submit(
			pending,
			meshbus_ManagementGroupId_MANAGEMENT_GROUP_ID_MESHBUS_MANAGEMENT,
			meshbus_ManagementCommandId_MANAGEMENT_COMMAND_ID_SECRET,
			MGMT_OP_WRITE, &req,
			meshbus_ManagementSecretSetRequest_fields,
			meshbus_ManagementSecretSetRequest_size);
	}
	case COMPANION_CLI_OP_REBOOT: {
		meshbus_PowerRebootRequest req =
			meshbus_PowerRebootRequest_init_zero;

		return companion_cli_management_submit(
			pending,
			meshbus_PowerMgmtGroupId_POWER_MGMT_GROUP_ID_MESHBUS_POWER,
			meshbus_PowerMgmtCommandId_POWER_MGMT_COMMAND_ID_REBOOT,
			MGMT_OP_WRITE, &req, meshbus_PowerRebootRequest_fields,
			meshbus_PowerRebootRequest_size);
	}
	default:
		return -ENOTSUP;
	}
}

static int companion_cli_submit(struct companion_cli_pending *pending)
{
	if (pending == NULL) {
		return -EINVAL;
	}

	switch (pending->op) {
	case COMPANION_CLI_OP_GET_NAME:
	case COMPANION_CLI_OP_SET_NAME:
	case COMPANION_CLI_OP_GET_ADVERT_INTERVAL:
	case COMPANION_CLI_OP_GET_FLOOD_ADVERT_INTERVAL:
	case COMPANION_CLI_OP_SET_ADVERT_INTERVAL:
	case COMPANION_CLI_OP_SET_FLOOD_ADVERT_INTERVAL:
	case COMPANION_CLI_OP_GET_LAT:
	case COMPANION_CLI_OP_SET_LAT:
	case COMPANION_CLI_OP_GET_LON:
	case COMPANION_CLI_OP_SET_LON:
	case COMPANION_CLI_OP_SET_PRV_KEY:
	case COMPANION_CLI_OP_SET_PUB_KEY:
	case COMPANION_CLI_OP_GET_REPEAT:
	case COMPANION_CLI_OP_SET_REPEAT:
		return companion_cli_meshcore_config_get_submit(pending);
	case COMPANION_CLI_OP_GET_RADIO:
	case COMPANION_CLI_OP_SET_RADIO:
		return companion_cli_radio_config_get_submit(pending);
	case COMPANION_CLI_OP_ADVERT:
	case COMPANION_CLI_OP_TIME:
	case COMPANION_CLI_OP_PASSWORD:
	case COMPANION_CLI_OP_REBOOT:
		return companion_cli_direct_submit(pending);
	default:
		return -ENOTSUP;
	}
}

static int companion_handle_send_cli_msg(const uint8_t *frame, size_t len)
{
	const size_t payload_offset = 13U;
	mbs_contact contact = meshbus_Contact_init_zero;
	struct companion_login_session session = {0};
	struct companion_cli_pending pending = {0};
	bool immediate = false;
	const char *immediate_rsp = NULL;
	uint32_t sent_tag;
	int rc;

	if (len <= payload_offset) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	rc = mbs_contact_find_by_prefix(&frame[7], &contact);
	if (rc != 0 ||
	    contact.public_key.size < CONFIG_MBS_CONTACT_PREFIX_BYTES) {
		return companion_send_error(COMPANION_ERR_CODE_NOT_FOUND);
	}

	memcpy(pending.contact_prefix, contact.public_key.bytes,
	       sizeof(pending.contact_prefix));
	if (!companion_login_session_get(pending.contact_prefix, &session)) {
		return companion_send_error(COMPANION_ERR_CODE_BAD_STATE);
	}

	memcpy(pending.public_key_prefix, session.public_key_prefix,
	       sizeof(pending.public_key_prefix));
	memcpy(pending.secret, session.secret, session.secret_len);
	pending.secret_len = session.secret_len;
	pending.sender_timestamp = read_u32_le(&frame[3]);

	rc = companion_cli_request_parse(&frame[payload_offset], len - payload_offset,
					 &pending, &immediate, &immediate_rsp);
	if (rc != 0) {
		goto out_error;
	}

	LOG_DBG("Companion remote CLI request: id=%s op=%u stage=%u immediate=%u",
		pending.request_id, (unsigned int)pending.op,
		(unsigned int)pending.stage, immediate ? 1U : 0U);

	if (immediate) {
		rc = companion_send_sent(0U, COMPANION_CLI_RESPONSE_TIMEOUT_MS);
		if (rc == 0) {
			rc = companion_cli_pending_push(&pending, immediate_rsp);
		}
		goto out;
	}

	rc = companion_cli_submit(&pending);
	if (rc != 0) {
		goto out_error;
	}

	sent_tag = pending.tag;
	LOG_DBG("Companion remote CLI submitted: id=%s tag=%u op=%u stage=%u",
		pending.request_id, (unsigned int)sent_tag,
		(unsigned int)pending.op, (unsigned int)pending.stage);
	rc = companion_send_sent(0U, COMPANION_CLI_RESPONSE_TIMEOUT_MS);
	if (rc != 0) {
		struct companion_cli_pending discard;

		(void)companion_pending_cli_take(sent_tag, &discard);
		companion_secure_wipe(&discard, sizeof(discard));
	}
	goto out;

out_error:
	rc = companion_send_error(companion_errno_to_error_code(rc));
out:
	companion_secure_wipe(&session, sizeof(session));
	companion_secure_wipe(&pending, sizeof(pending));
	return rc;
}

static void companion_cli_start_work_handler(struct k_work *work)
{
	struct companion_cli_start start = {0};

	ARG_UNUSED(work);

	while (companion_cli_start_take(&start)) {
		LOG_DBG("Companion CLI work start: len=%u",
			(unsigned int)start.len);
		(void)companion_handle_send_cli_msg(start.frame, start.len);
		companion_secure_wipe(&start, sizeof(start));
	}
}

static int companion_handle_send_txt_msg(const uint8_t *frame, size_t len)
{
	uint8_t attempt;
	uint64_t ack_token = 0U;
	uint32_t ack_id;
	uint8_t prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES] = {0};
	size_t payload_offset = 13U;
	size_t payload_len;
	int rc;
	uint8_t out[10] = {0};
	size_t idx = 0U;

	if (len <= payload_offset) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	companion_log_node_text_frame(frame, len);
	attempt = frame[2];
	memcpy(prefix, &frame[7], MIN(sizeof(prefix), (size_t)COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE));
	payload_len = len - payload_offset;
	if (frame[1] == COMPANION_TXT_TYPE_CLI_DATA) {
		rc = companion_cli_start_store(frame, len);
		if (rc != 0) {
			return companion_send_error(companion_errno_to_error_code(rc));
		}
		return 0;
	}
	if (frame[1] != COMPANION_TXT_TYPE_PLAIN) {
		return companion_send_error(COMPANION_ERR_CODE_UNSUPPORTED_CMD);
	}

	rc = mbs_message_send_to_node(prefix, &frame[payload_offset], payload_len, false,
					  attempt, &ack_token);
	if (rc != 0) {
		return companion_send_error(companion_errno_to_error_code(rc));
	}

	ack_id = (uint32_t)ack_token;
	companion_pending_ack_store(attempt, ack_id);

	out[idx++] = COMPANION_RESP_CODE_SENT;
	out[idx++] = 0U;
	append_u32_le(out, &idx, ack_id);
	append_u32_le(out, &idx, COMPANION_EST_SEND_TIMEOUT_MS);

	rc = companion_send_frame(out, idx);
	if (rc != 0) {
		companion_pending_ack_forget(attempt);
	}

	return rc;
}

static int companion_handle_send_channel_txt_msg(const uint8_t *frame, size_t len)
{
	size_t payload_offset = 7U;

	if (len <= payload_offset) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	companion_log_channel_text_frame(frame, len);
	if (frame[1] != COMPANION_TXT_TYPE_PLAIN) {
		return companion_send_error(COMPANION_ERR_CODE_UNSUPPORTED_CMD);
	}

	return companion_send_result(
		mbs_message_send_to_channel(frame[2], &frame[payload_offset], len - payload_offset));
}

static uint32_t companion_timestamp_to_seconds(uint64_t timestamp)
{
	if (timestamp > UINT32_MAX) {
		timestamp /= MSEC_PER_SEC;
	}

	return (uint32_t)MIN(timestamp, (uint64_t)UINT32_MAX);
}

static int companion_channel_index_from_target(const uint8_t *target, size_t target_len,
					       uint8_t *out_index)
{
	mbs_channel channel = meshbus_Channel_init_zero;
	uint8_t store_size;

	if (target == NULL || target_len == 0U || out_index == NULL) {
		return -EINVAL;
	}

	store_size = mbs_channel_store_size();
	for (uint8_t idx = 0U; idx < store_size; idx++) {
		if (mbs_channel_get(idx, &channel) != 0) {
			continue;
		}
		if (channel.secret.size >= target_len &&
		    memcmp(channel.secret.bytes, target, target_len) == 0) {
			*out_index = idx;
			return 0;
		}
		if (channel.hash.size > 0U && channel.hash.bytes[0] == target[0]) {
			*out_index = idx;
			return 0;
		}
	}

	return -ENOENT;
}

static int companion_handle_sync_next_message(void)
{
	mbs_message_content message = meshbus_MessageContent_init_zero;
	uint8_t out[MESHCORE_COMPANION_MAX_FRAME_SIZE] = {0};
	size_t idx = 0U;
	uint8_t target[COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE] = {0};
	uint8_t channel_index;
	size_t payload_len;
	size_t sender_len;
	size_t room;
	int rc;

	rc = mbs_message_next(&message);
	if (rc == -ENOENT) {
		LOG_DBG("Companion sync next message: empty");
		out[0] = COMPANION_RESP_CODE_NO_MORE_MESSAGES;
		return companion_send_frame(out, 1U);
	}
	if (rc != 0) {
		LOG_WRN("Companion sync next message failed: rc=%d", rc);
		return companion_send_error(companion_errno_to_error_code(rc));
	}

	payload_len = MIN(message.payload.size, sizeof(out) - 16U);
	memcpy(target, message.target.bytes, MIN(message.target.size, sizeof(target)));
	if (message.type == meshbus_MessageContent_MessageType_RECEIVE_NODE) {
		LOG_DBG("Companion sync next node message: payload_len=%u",
			(unsigned int)message.payload.size);
		out[idx++] = COMPANION_RESP_CODE_CONTACT_MSG_RECV_V3;
		out[idx++] = message.has_rx_snr ? (uint8_t)((int8_t)(message.rx_snr * 4.0f)) : 0U;
		out[idx++] = 0U;
		out[idx++] = 0U;
		memcpy(&out[idx], target, sizeof(target));
		idx += sizeof(target);
		out[idx++] = COMPANION_OUT_PATH_UNKNOWN;
		out[idx++] = COMPANION_TXT_TYPE_PLAIN;
		append_u32_le(out, &idx, companion_timestamp_to_seconds(message.sender_timestamp));
	} else if (message.type == meshbus_MessageContent_MessageType_RECEIVE_CHANNEL) {
		rc = companion_channel_index_from_target(target, message.target.size, &channel_index);
		if (rc != 0) {
			LOG_WRN("Companion sync next channel index unresolved: target=%02x rc=%d",
				(unsigned int)target[0], rc);
			channel_index = target[0];
		}
		LOG_DBG("Companion sync next channel message: channel=%u payload_len=%u",
			(unsigned int)channel_index, (unsigned int)message.payload.size);
		out[idx++] = COMPANION_RESP_CODE_CHANNEL_MSG_RECV_V3;
		out[idx++] = message.has_rx_snr ? (uint8_t)((int8_t)(message.rx_snr * 4.0f)) : 0U;
		out[idx++] = 0U;
		out[idx++] = 0U;
		out[idx++] = channel_index;
		out[idx++] = COMPANION_OUT_PATH_UNKNOWN;
		out[idx++] = COMPANION_TXT_TYPE_PLAIN;
		append_u32_le(out, &idx, companion_timestamp_to_seconds(message.sender_timestamp));
		room = sizeof(out) - idx;
		sender_len = strnlen(message.sender_name, sizeof(message.sender_name));
		if (sender_len == 0U) {
			memcpy(&out[idx], "unknown", MIN(room, 7U));
			sender_len = MIN(room, 7U);
		} else {
			sender_len = MIN(sender_len, room);
			memcpy(&out[idx], message.sender_name, sender_len);
		}
		idx += sender_len;
		room = sizeof(out) - idx;
		if (room > 2U) {
			out[idx++] = ':';
			out[idx++] = ' ';
			payload_len = MIN(message.payload.size, sizeof(out) - idx);
		} else {
			payload_len = 0U;
		}
	} else {
		out[0] = COMPANION_RESP_CODE_NO_MORE_MESSAGES;
		return companion_send_frame(out, 1U);
	}

	memcpy(&out[idx], message.payload.bytes, payload_len);
	idx += payload_len;

	return companion_send_frame(out, idx);
}

static int companion_handle_send_self_advert(const uint8_t *frame, size_t len)
{
	bool flood = len >= 2U && frame[1] == 1U;

	return companion_send_result(mbs_meshcore_advert_request(flood));
}

static int companion_handle_path_discovery(const uint8_t *frame, size_t len)
{
	uint8_t prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES] = {0};
	uint8_t out[10] = {0};
	uint32_t tag = 0U;
	size_t idx = 0U;
	int rc;

	if (len < 2U + COMPANION_PUBLIC_KEY_SIZE) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	memcpy(prefix, &frame[2], MIN(sizeof(prefix), (size_t)COMPANION_PUBLIC_KEY_SIZE));
	rc = mbs_contact_discover_path_request(prefix, &tag);
	if (rc != 0) {
		return companion_send_error(companion_errno_to_error_code(rc));
	}

	out[idx++] = COMPANION_RESP_CODE_SENT;
	out[idx++] = 1U;
	append_u32_le(out, &idx, tag);
	append_u32_le(out, &idx, COMPANION_EST_SEND_TIMEOUT_MS);

	return companion_send_frame(out, idx);
}

static int companion_handle_legacy_contact_trace_path(const uint8_t *frame, size_t len)
{
	uint8_t prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES] = {0};
	uint8_t out[10] = {0};
	uint32_t tag = 0U;
	size_t idx = 0U;
	int rc;

	if (len < 1U + CONFIG_MBS_CONTACT_PREFIX_BYTES) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	memcpy(prefix, &frame[1], sizeof(prefix));
	rc = mbs_contact_trace_path_request(prefix, &tag);
	if (rc != 0) {
		return companion_send_error(companion_errno_to_error_code(rc));
	}

	out[idx++] = COMPANION_RESP_CODE_SENT;
	out[idx++] = 0U;
	append_u32_le(out, &idx, tag);
	append_u32_le(out, &idx, COMPANION_EST_SEND_TIMEOUT_MS);

	return companion_send_frame(out, idx);
}

static int companion_handle_trace_path(const uint8_t *frame, size_t len)
{
	uint8_t out[10] = {0};
	uint32_t app_tag;
	uint32_t auth_code;
	uint32_t local_tag = 0U;
	uint8_t flags;
	uint8_t path_hash_size;
	uint8_t path_len;
	uint8_t hop_count;
	size_t pending_slot = 0U;
	size_t idx = 0U;
	int rc;

	if (len <= COMPANION_TRACE_REQUEST_HEADER_LEN) {
		return companion_handle_legacy_contact_trace_path(frame, len);
	}

	path_len = (uint8_t)(len - COMPANION_TRACE_REQUEST_HEADER_LEN);
	flags = frame[9];
	path_hash_size = companion_trace_path_hash_size(flags);
	if (path_hash_size == 0U ||
	    path_len > MBS_MESHCORE_PATH_MAX_LEN ||
	    (path_len % path_hash_size) != 0U) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	hop_count = path_len / path_hash_size;
	if ((hop_count % 2U) == 0U) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	app_tag = read_u32_le(&frame[1]);
	auth_code = read_u32_le(&frame[5]);
	rc = companion_pending_trace_reserve(app_tag, auth_code, flags,
					     &frame[COMPANION_TRACE_REQUEST_HEADER_LEN],
					     path_len, &pending_slot);
	if (rc != 0) {
		return companion_send_error(companion_errno_to_error_code(rc));
	}

	rc = mbs_meshcore_trace_request(&frame[COMPANION_TRACE_REQUEST_HEADER_LEN],
					    path_len, path_hash_size, &local_tag);
	if (rc != 0) {
		companion_pending_trace_clear(pending_slot);
		return companion_send_error(companion_errno_to_error_code(rc));
	}
	companion_pending_trace_commit(pending_slot, local_tag);

	out[idx++] = COMPANION_RESP_CODE_SENT;
	out[idx++] = 0U;
	append_u32_le(out, &idx, app_tag);
	append_u32_le(out, &idx, companion_trace_timeout_ms(hop_count));

	return companion_send_frame(out, idx);
}

static int companion_handle_telemetry_self_req(void)
{
	meshcore_platform_telemetry_payload_t payload = {0};
	mbs_meshcore_config node = meshbus_MeshcoreConfig_init_zero;
	uint8_t out[MESHCORE_COMPANION_MAX_FRAME_SIZE] = {0};
	size_t idx = 0U;
	size_t copy_len;
	int rc;

	LOG_DBG("Companion telemetry self request");
	rc = companion_meshcore_config_get(&node);
	if (rc != 0) {
		LOG_WRN("Companion telemetry config get failed: rc=%d", rc);
		return companion_send_error(companion_errno_to_error_code(rc));
	}

	LOG_DBG("Companion telemetry self sample start");
	rc = meshcore_platform_telemetry_node_get(NULL,
						  COMPANION_DEFAULT_TELEMETRY_PERMISSION_MASK,
						  &payload);
	if (rc != 0) {
		LOG_WRN("Companion telemetry sample failed: rc=%d", rc);
		return companion_send_error(companion_errno_to_error_code(rc));
	}

	out[idx++] = COMPANION_PUSH_CODE_TELEMETRY_RESPONSE;
	out[idx++] = 0U;
	copy_len = MIN((size_t)node.public_key.size,
		       (size_t)COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE);
	memcpy(&out[idx], node.public_key.bytes, copy_len);
	idx += COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE;
	copy_len = MIN((size_t)payload.payload_len, sizeof(out) - idx);
	memcpy(&out[idx], payload.payload, copy_len);
	idx += copy_len;

	LOG_DBG("Companion telemetry self response: payload_len=%u frame_len=%u",
		(unsigned int)payload.payload_len, (unsigned int)idx);
	return companion_send_frame(out, idx);
}

static int companion_handle_telemetry_remote_req(const uint8_t *frame)
{
	uint8_t prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES] = {0};
	uint8_t out[10] = {0};
	uint32_t tag = 0U;
	size_t idx = 0U;
	int rc;

	memcpy(prefix, &frame[4], sizeof(prefix));
	LOG_DBG("Companion telemetry remote request: target=%02x%02x%02x",
		prefix[0], prefix[1], prefix[2]);

	rc = mbs_contact_telemetry_request(prefix, &tag);
	if (rc != 0) {
		LOG_WRN("Companion telemetry request failed: rc=%d", rc);
		return companion_send_error(companion_errno_to_error_code(rc));
	}
	LOG_DBG("Companion telemetry remote request accepted: tag=%u", tag);

	out[idx++] = COMPANION_RESP_CODE_SENT;
	out[idx++] = 0U;
	append_u32_le(out, &idx, tag);
	append_u32_le(out, &idx, COMPANION_EST_SEND_TIMEOUT_MS);

	return companion_send_frame(out, idx);
}

static int companion_handle_telemetry_req(const uint8_t *frame, size_t len)
{
	if (len == 4U) {
		return companion_handle_telemetry_self_req();
	}

	if (len >= 4U + COMPANION_PUBLIC_KEY_SIZE) {
		return companion_handle_telemetry_remote_req(frame);
	}

	return companion_send_error(COMPANION_ERR_CODE_UNSUPPORTED_CMD);
}

static int companion_handle_status_req(const uint8_t *frame, size_t len)
{
	uint8_t prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES] = {0};
	uint8_t payload[COMPANION_STATUS_REQUEST_PAYLOAD_LEN] = {0};
	uint8_t out[10] = {0};
	uint32_t tag = 0U;
	uint32_t nonce = sys_rand32_get();
	size_t idx = 0U;
	int rc;

	if (len < 1U + COMPANION_PUBLIC_KEY_SIZE) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	memcpy(prefix, &frame[1], sizeof(prefix));
	payload[0] = COMPANION_STATUS_REQUEST_TYPE;
	sys_put_le32(nonce, &payload[5]);

	LOG_DBG("Companion app status request: target=%02x%02x%02x%02x%02x%02x payload_len=%u",
		frame[1], frame[2], frame[3], frame[4], frame[5], frame[6],
		(unsigned int)sizeof(payload));

	rc = mbs_contact_binary_request(prefix, payload, sizeof(payload), &tag);
	if (rc != 0) {
		LOG_WRN("Companion app status request failed: target=%02x%02x%02x%02x%02x%02x "
			"rc=%d error=%u",
			frame[1], frame[2], frame[3], frame[4], frame[5], frame[6],
			rc, companion_errno_to_error_code(rc));
		return companion_send_error(companion_errno_to_error_code(rc));
	}

	companion_pending_status_store(tag, &frame[1]);
	LOG_DBG("Companion app status request accepted: tag=%u timeout=%u",
		(unsigned int)tag, (unsigned int)COMPANION_STATUS_RESPONSE_TIMEOUT_MS);

	out[idx++] = COMPANION_RESP_CODE_SENT;
	out[idx++] = 0U;
	append_u32_le(out, &idx, tag);
	append_u32_le(out, &idx, COMPANION_STATUS_RESPONSE_TIMEOUT_MS);

	return companion_send_frame(out, idx);
}

static int companion_handle_send_binary_req(const uint8_t *frame, size_t len)
{
	uint8_t prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES] = {0};
	const uint8_t *payload;
	size_t payload_len;
	uint32_t tag = 0U;
	uint8_t out[10] = {0};
	size_t idx = 0U;
	int rc;

	if (len < 1U + COMPANION_PUBLIC_KEY_SIZE + COMPANION_BINARY_REQUEST_MIN_PAYLOAD_LEN) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	payload = &frame[1U + COMPANION_PUBLIC_KEY_SIZE];
	payload_len = len - (1U + COMPANION_PUBLIC_KEY_SIZE);
	if (payload_len > MBS_CONTACT_BINARY_REQUEST_PAYLOAD_MAX_LEN) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	LOG_DBG("Companion app binary frame: kind=binary-req target=%02x%02x%02x%02x%02x%02x payload_len=%u",
		frame[1], frame[2], frame[3], frame[4], frame[5], frame[6],
		(unsigned int)payload_len);
	companion_log_binary_payload("binary-req", payload, payload_len);

	memcpy(prefix, &frame[1], sizeof(prefix));
	rc = mbs_contact_binary_request(prefix, payload, payload_len, &tag);
	if (rc != 0) {
		return companion_send_error(companion_errno_to_error_code(rc));
	}

	out[idx++] = COMPANION_RESP_CODE_SENT;
	out[idx++] = 0U;
	append_u32_le(out, &idx, tag);
	append_u32_le(out, &idx, COMPANION_EST_SEND_TIMEOUT_MS);

	return companion_send_frame(out, idx);
}

static int companion_build_power_status_smp(uint8_t *out, size_t cap,
					    uint16_t *out_len)
{
	if (out == NULL || out_len == NULL || cap < COMPANION_MGMT_HDR_SIZE) {
		return -EINVAL;
	}

	out[0] = MGMT_OP_READ;
	out[1] = 0U;
	out[2] = 0U;
	out[3] = 0U;
	out[4] = (uint8_t)(
		meshbus_PowerMgmtGroupId_POWER_MGMT_GROUP_ID_MESHBUS_POWER >> 8);
	out[5] = (uint8_t)
		meshbus_PowerMgmtGroupId_POWER_MGMT_GROUP_ID_MESHBUS_POWER;
	out[6] = 7U;
	out[7] = meshbus_PowerMgmtCommandId_POWER_MGMT_COMMAND_ID_STATUS;
	*out_len = COMPANION_MGMT_HDR_SIZE;

	return 0;
}

static int companion_management_login_start(
	const uint8_t contact_prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES],
	const uint8_t *secret, size_t secret_len, uint32_t *out_tag)
{
#if defined(CONFIG_MBS_MANAGEMENT)
	static mbs_management_smp_request_event request;
	int rc;

	if (contact_prefix == NULL || secret == NULL || out_tag == NULL) {
		return -EINVAL;
	}

	memset(&request, 0, sizeof(request));
	memcpy(request.contact_prefix, contact_prefix, sizeof(request.contact_prefix));
	rc = companion_build_power_status_smp(request.packet,
					      sizeof(request.packet),
					      &request.packet_len);
	if (rc != 0) {
		return rc;
	}

	return mbs_management_smp_request_with_secret(
		&request, secret, secret_len, out_tag);
#else
	ARG_UNUSED(contact_prefix);
	ARG_UNUSED(secret);
	ARG_UNUSED(secret_len);
	ARG_UNUSED(out_tag);

	return -ENOTSUP;
#endif
}

static void companion_login_fail_push(
	const uint8_t public_key_prefix[COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE])
{
	uint8_t frame[1U + 1U + COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE] = {0};
	size_t idx = 0U;

	if (public_key_prefix == NULL) {
		return;
	}

	frame[idx++] = COMPANION_PUSH_CODE_LOGIN_FAIL;
	frame[idx++] = 0U;
	memcpy(&frame[idx], public_key_prefix, COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE);
	idx += COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE;

	(void)meshcore_companion_adapter_queue_frame(frame, idx);
}

static void companion_login_start_work_handler(struct k_work *work)
{
	struct companion_login_start start = {0};
	struct meshcore_companion_transport transport;
	uint32_t tag = 0U;
	int rc;

	ARG_UNUSED(work);

	if (!companion_login_start_take(&start)) {
		return;
	}

	rc = companion_transport_snapshot(&transport);
	if (rc != 0) {
		goto out;
	}

	rc = companion_management_login_start(start.contact_prefix, start.secret,
					     start.secret_len, &tag);
	if (rc != 0) {
		LOG_DBG("Companion login management start failed: rc=%d", rc);
		companion_login_fail_push(start.public_key_prefix);
		goto out;
	}

	rc = companion_pending_login_store(tag, start.contact_prefix,
					   start.public_key_prefix,
					   start.secret, start.secret_len);
	if (rc != 0) {
		LOG_DBG("Companion login pending store failed: tag=%u rc=%d",
			(unsigned int)tag, rc);
		companion_login_fail_push(start.public_key_prefix);
	}

out:
	companion_secure_wipe(&start, sizeof(start));
}

static int companion_handle_send_login(const uint8_t *frame, size_t len)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	uint8_t contact_prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES];
	uint8_t app_prefix[COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE];
	uint8_t secret[MBS_MANAGEMENT_SECRET_MAX_LEN];
	char password[MBS_MANAGEMENT_SECRET_MAX_LEN + 1U];
	uint8_t out[10] = {0};
	size_t secret_len;
	size_t idx = 0U;
	uint32_t app_tag = 0U;
	int rc;

	if (len < 1U + COMPANION_PUBLIC_KEY_SIZE) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	secret_len = len - (1U + COMPANION_PUBLIC_KEY_SIZE);
	if (secret_len > 0U && frame[len - 1U] == 0U) {
		secret_len--;
	}
	if (secret_len == 0U || secret_len >= sizeof(password)) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	rc = mbs_contact_find_by_key(&frame[1], &contact);
	if (rc != 0) {
		return companion_send_error(COMPANION_ERR_CODE_NOT_FOUND);
	}
	if (contact.public_key.size < COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE) {
		rc = companion_send_error(COMPANION_ERR_CODE_NOT_FOUND);
		goto out;
	}

	memcpy(contact_prefix, contact.public_key.bytes, sizeof(contact_prefix));
	memcpy(app_prefix, contact.public_key.bytes, sizeof(app_prefix));
	memcpy(password, &frame[1U + COMPANION_PUBLIC_KEY_SIZE], secret_len);
	password[secret_len] = '\0';
	rc = companion_password_to_secret(password, secret, sizeof(secret), &secret_len);
	if (rc != 0) {
		rc = companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
		goto out;
	}
	rc = companion_login_start_store(contact_prefix, app_prefix, secret,
					 secret_len);
	if (rc != 0) {
		rc = companion_send_error(companion_errno_to_error_code(rc));
		goto out;
	}

	app_tag = read_u32_le(app_prefix);
	out[idx++] = COMPANION_RESP_CODE_SENT;
	out[idx++] = 0U;
	append_u32_le(out, &idx, app_tag);
	append_u32_le(out, &idx, COMPANION_LOGIN_RESPONSE_TIMEOUT_MS);
	rc = companion_send_frame(out, idx);
	if (rc != 0) {
		struct companion_login_start discard;

		(void)companion_login_start_take(&discard);
		companion_secure_wipe(&discard, sizeof(discard));
		goto out;
	}

	rc = k_work_submit(&companion_login_start_work);
	if (rc < 0) {
		struct companion_login_start discard;

		(void)companion_login_start_take(&discard);
		companion_secure_wipe(&discard, sizeof(discard));
		rc = companion_send_error(companion_errno_to_error_code(rc));
	} else {
		rc = 0;
	}

out:
	companion_secure_wipe(secret, sizeof(secret));
	companion_secure_wipe(password, sizeof(password));
	companion_secure_wipe(contact_prefix, sizeof(contact_prefix));
	companion_secure_wipe(app_prefix, sizeof(app_prefix));
	companion_secure_wipe(&contact, sizeof(contact));
	return rc;
}

static int companion_handle_send_channel_data(const uint8_t *frame, size_t len)
{
	uint8_t channel_idx;
	uint8_t path_len;
	uint8_t path[MBS_MESHCORE_PATH_MAX_LEN] = {0};
	size_t path_bytes = 0U;
	size_t idx = 1U;
	uint16_t data_type;
	const uint8_t *payload;
	size_t payload_len;

	if (len < 4U) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	channel_idx = frame[idx++];
	path_len = frame[idx++];
	if (path_len != MBS_MESHCORE_OUT_PATH_UNKNOWN) {
		if (!companion_path_len_to_bytes(path_len, &path_bytes) ||
		    idx + path_bytes + 2U > len) {
			return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
		}
		if (path_bytes > 0U) {
			memcpy(path, &frame[idx], path_bytes);
		}
		idx += path_bytes;
	} else if (idx + 2U > len) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	data_type = (uint16_t)frame[idx] | ((uint16_t)frame[idx + 1U] << 8);
	idx += 2U;
	payload = &frame[idx];
	payload_len = len - idx;
	if (data_type == MBS_MESHCORE_CHANNEL_DATA_TYPE_RESERVED ||
	    payload_len > MBS_MESHCORE_CHANNEL_DATA_PAYLOAD_MAX_LEN ||
	    payload_len > MESHCORE_COMPANION_MAX_FRAME_SIZE - COMPANION_CHANNEL_DATA_OVERHEAD) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	LOG_DBG("Companion app binary frame: kind=channel-data channel=%u path_len=0x%02x path_bytes=%u data_type=0x%04x payload_len=%u",
		(unsigned int)channel_idx, (unsigned int)path_len,
		(unsigned int)path_bytes, (unsigned int)data_type,
		(unsigned int)payload_len);
	if (path_bytes > 0U) {
		companion_log_binary_payload("channel-data-path", path, path_bytes);
	}
	companion_log_binary_payload("channel-data", payload, payload_len);

	return companion_send_result(mbs_meshcore_channel_data_send(
		channel_idx, path_len == MBS_MESHCORE_OUT_PATH_UNKNOWN ? NULL : path,
		path_len, data_type, payload, payload_len));
}

static int companion_handle_send_raw_data(const uint8_t *frame, size_t len)
{
	uint8_t path_len;
	const uint8_t *path;
	const uint8_t *payload;
	size_t path_bytes;
	size_t payload_len;

	if (len < 2U + COMPANION_RAW_DATA_MIN_PAYLOAD_LEN) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	path_len = frame[1];
	if (path_len == MBS_MESHCORE_OUT_PATH_UNKNOWN) {
		return companion_send_error(COMPANION_ERR_CODE_UNSUPPORTED_CMD);
	}
	if (!companion_path_len_to_bytes(path_len, &path_bytes) ||
	    2U + path_bytes + COMPANION_RAW_DATA_MIN_PAYLOAD_LEN > len) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	path = &frame[2];
	payload = &frame[2U + path_bytes];
	payload_len = len - (2U + path_bytes);
	if (payload_len > MESHCORE_COMPANION_MAX_FRAME_SIZE - COMPANION_RAW_CONTROL_OVERHEAD) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	LOG_DBG("Companion app binary frame: kind=raw-data path_len=0x%02x path_bytes=%u payload_len=%u",
		(unsigned int)path_len, (unsigned int)path_bytes,
		(unsigned int)payload_len);
	if (path_bytes > 0U) {
		companion_log_binary_payload("raw-data-path", path, path_bytes);
	}
	companion_log_binary_payload("raw-data", payload, payload_len);

	return companion_send_result(
		mbs_meshcore_raw_data_send(path, path_len, payload, payload_len));
}

static int companion_handle_send_control_data(const uint8_t *frame, size_t len)
{
	const uint8_t *payload;
	size_t payload_len;

	if (len < 2U || (frame[1] & 0x80U) == 0U) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	payload = &frame[1];
	payload_len = len - 1U;
	if (payload_len > MESHCORE_COMPANION_MAX_FRAME_SIZE - COMPANION_RAW_CONTROL_OVERHEAD) {
		return companion_send_error(COMPANION_ERR_CODE_ILLEGAL_ARG);
	}

	LOG_DBG("Companion app binary frame: kind=control-data first=0x%02x payload_len=%u",
		frame[1], (unsigned int)payload_len);
	companion_log_binary_payload("control-data", payload, payload_len);

	return companion_send_result(mbs_meshcore_control_data_send(payload, payload_len));
}

static void companion_message_waiting_listener_cb(const struct zbus_channel *chan)
{
	uint8_t frame[] = { COMPANION_PUSH_CODE_MSG_WAITING };
	int rc;

	ARG_UNUSED(chan);
	rc = meshcore_companion_adapter_queue_frame(frame, sizeof(frame));
	if (rc == 0) {
		LOG_DBG("Companion message waiting push queued");
	} else {
		LOG_WRN("Companion message waiting push dropped: rc=%d", rc);
	}
}

static void companion_message_ack_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_message_ack_response_event *event = zbus_chan_const_msg(chan);
	uint8_t frame[9] = {0};
	uint32_t ack_id;
	uint32_t elapsed_ms;
	size_t idx = 0U;
	int rc;

	if (event == NULL) {
		return;
	}
	if (!companion_pending_ack_take(event->attempt, &ack_id, &elapsed_ms)) {
		LOG_DBG("Companion ACK ignored without pending attempt=%u",
			(unsigned int)event->attempt);
		return;
	}

	frame[idx++] = COMPANION_PUSH_CODE_SEND_CONFIRMED;
	append_u32_le(frame, &idx, ack_id);
	append_u32_le(frame, &idx, elapsed_ms);

	rc = meshcore_companion_adapter_queue_frame(frame, idx);
	if (rc != 0) {
		LOG_WRN("Companion ACK push dropped: attempt=%u rc=%d",
			(unsigned int)event->attempt, rc);
	}
}

static void companion_contact_advert_response_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_contact_response_advert_event *event = zbus_chan_const_msg(chan);
	uint8_t frame[MESHCORE_COMPANION_MAX_FRAME_SIZE] = {0};
	size_t frame_len = 0U;
	int rc;

	if (event == NULL) {
		return;
	}

	if (event->is_new) {
		rc = companion_build_new_advert_frame(event, frame, &frame_len);
	} else {
		frame[0] = COMPANION_PUSH_CODE_ADVERT;
		memcpy(&frame[1], event->public_key, COMPANION_PUBLIC_KEY_SIZE);
		frame_len = 1U + COMPANION_PUBLIC_KEY_SIZE;
		rc = 0;
	}

	if (rc == 0) {
		(void)meshcore_companion_adapter_queue_frame(frame, frame_len);
	}

	ARG_UNUSED(chan);
}

static void companion_contact_path_response_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_contact_response_path_event *event = zbus_chan_const_msg(chan);
	uint8_t frame[MESHCORE_COMPANION_MAX_FRAME_SIZE] = {0};
	size_t idx = 0U;
	size_t copy_len;
	uint8_t path_len_field;

	ARG_UNUSED(chan);

	if (event == NULL) {
		return;
	}

	if (!event->is_discover) {
		frame[idx++] = COMPANION_PUSH_CODE_PATH_UPDATED;
		if (!companion_copy_contact_public_key(&frame[idx], event->key_prefix)) {
			return;
		}
		idx += COMPANION_PUBLIC_KEY_SIZE;

		(void)meshcore_companion_adapter_queue_frame(frame, idx);
		return;
	}

	frame[idx++] = COMPANION_PUSH_CODE_PATH_DISCOVERY_RESPONSE;
	frame[idx++] = 0U;
	companion_copy_contact_app_prefix(&frame[idx], event->key_prefix);
	idx += COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE;
	path_len_field = event->has_out_path ? companion_contact_path_out_len_field(event) : 0U;
	frame[idx++] = path_len_field;
	copy_len = MIN(event->has_out_path ? event->out_path_len : 0U, sizeof(frame) - idx - 1U);
	memcpy(&frame[idx], event->out_path, copy_len);
	idx += copy_len;
	path_len_field = companion_contact_path_in_len_field(event);
	frame[idx++] = path_len_field;
	copy_len = MIN(event->in_path_len, sizeof(frame) - idx);
	memcpy(&frame[idx], event->in_path, copy_len);
	idx += copy_len;

	(void)meshcore_companion_adapter_queue_frame(frame, idx);
}

static void companion_contact_trace_response_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_contact_response_trace_path_event *event = zbus_chan_const_msg(chan);
	uint8_t frame[MESHCORE_COMPANION_MAX_FRAME_SIZE] = {0};
	size_t idx = 0U;
	size_t out_count;
	size_t return_count;

	ARG_UNUSED(chan);

	if (event == NULL) {
		return;
	}

	frame[idx++] = COMPANION_PUSH_CODE_TRACE_DATA;
	frame[idx++] = event->state;
	frame[idx++] = 0U;
	frame[idx++] = 0U;
	append_u32_le(frame, &idx, event->timestamp);
	append_u32_le(frame, &idx, event->tag);
	out_count = MIN(event->out_path_snr_count, sizeof(frame) - idx - 1U);
	memcpy(&frame[idx], event->out_path_snr, out_count);
	idx += out_count;
	return_count = MIN(event->return_path_snr_count, sizeof(frame) - idx - 1U);
	memcpy(&frame[idx], event->return_path_snr, return_count);
	idx += return_count;
	frame[idx++] = event->has_response_snr ? (uint8_t)event->response_snr : 0U;

	(void)meshcore_companion_adapter_queue_frame(frame, idx);
}

static void companion_meshcore_trace_response_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_meshcore_trace_response_event *event = zbus_chan_const_msg(chan);
	struct companion_trace_pending pending = {0};
	uint8_t frame[MESHCORE_COMPANION_MAX_FRAME_SIZE] = {0};
	size_t idx = 0U;
	uint8_t path_hash_size;
	size_t path_snr_count;
	size_t forward_snr_count;
	size_t return_snr_count;
	size_t snr_idx;

	ARG_UNUSED(chan);

	if (event == NULL || !companion_pending_trace_take(event->tag, &pending)) {
		return;
	}

	path_hash_size = companion_trace_path_hash_size(pending.flags);
	if (path_hash_size == 0U) {
		return;
	}

	path_snr_count = pending.path_len / path_hash_size;
	if (12U + pending.path_len + path_snr_count + 1U > sizeof(frame)) {
		return;
	}

	frame[idx++] = COMPANION_PUSH_CODE_TRACE_DATA;
	frame[idx++] = 0U;
	frame[idx++] = pending.path_len;
	frame[idx++] = pending.flags;
	append_u32_le(frame, &idx, pending.app_tag);
	append_u32_le(frame, &idx, pending.auth_code);
	memcpy(&frame[idx], pending.path, pending.path_len);
	idx += pending.path_len;

	snr_idx = idx;
	memset(&frame[snr_idx], 0, path_snr_count);
	forward_snr_count = (path_snr_count + 1U) / 2U;
	for (size_t i = 0U; i < MIN((size_t)event->out_path_snr_count,
				    forward_snr_count); i++) {
		frame[snr_idx + i] = (uint8_t)event->out_path_snr[i];
	}

	return_snr_count = path_snr_count - forward_snr_count;
	for (size_t i = 0U; i < MIN((size_t)event->return_path_snr_count,
				    return_snr_count); i++) {
		frame[snr_idx + forward_snr_count + i] =
			(uint8_t)event->return_path_snr[i];
	}
	idx += path_snr_count;
	frame[idx++] = event->has_response_snr ? (uint8_t)event->response_snr : 0U;

	(void)meshcore_companion_adapter_queue_frame(frame, idx);
}

static void companion_contact_telemetry_response_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_contact_response_telemetry_event *event = zbus_chan_const_msg(chan);
	uint8_t frame[MESHCORE_COMPANION_MAX_FRAME_SIZE] = {0};
	size_t idx = 0U;
	size_t copy_len;
	int rc;

	ARG_UNUSED(chan);

	if (event == NULL) {
		return;
	}

	frame[idx++] = COMPANION_PUSH_CODE_TELEMETRY_RESPONSE;
	frame[idx++] = 0U;
	companion_copy_contact_app_prefix(&frame[idx], event->key_prefix);
	idx += COMPANION_APP_PUBLIC_KEY_PREFIX_SIZE;
	copy_len = MIN(event->payload_len, sizeof(frame) - idx);
	memcpy(&frame[idx], event->payload, copy_len);
	idx += copy_len;

	rc = meshcore_companion_adapter_queue_frame(frame, idx);
	if (rc != 0) {
		LOG_WRN("Companion telemetry response queue failed: rc=%d", rc);
		return;
	}

	LOG_DBG("Companion telemetry response queued: len=%u", (unsigned int)idx);
}

static void companion_contact_binary_response_listener_cb(const struct zbus_channel *chan)
{
	const mbs_contact_response_binary_event *event = zbus_chan_const_msg(chan);
	struct companion_status_pending status = {0};
	uint8_t frame[MESHCORE_COMPANION_MAX_FRAME_SIZE] = {0};
	size_t idx = 0U;

	ARG_UNUSED(chan);

	if (event == NULL) {
		return;
	}

	if (companion_pending_status_take(event->tag, &status)) {
		if ((size_t)event->payload_len + COMPANION_STATUS_RESPONSE_HEADER_LEN >
		    sizeof(frame)) {
			return;
		}
		frame[idx++] = COMPANION_PUSH_CODE_STATUS_RESPONSE;
		frame[idx++] = 0U;
		memcpy(&frame[idx], status.public_key_prefix,
		       sizeof(status.public_key_prefix));
		idx += sizeof(status.public_key_prefix);
		LOG_DBG("Companion status response queued: tag=%u len=%u",
			(unsigned int)event->tag, (unsigned int)event->payload_len);
	} else {
		if ((size_t)event->payload_len + COMPANION_BINARY_RESPONSE_HEADER_LEN >
		    sizeof(frame)) {
			return;
		}
		frame[idx++] = COMPANION_PUSH_CODE_BINARY_RESPONSE;
		frame[idx++] = 0U;
		append_u32_le(frame, &idx, event->tag);
	}
	if (event->payload_len > 0U) {
		memcpy(&frame[idx], event->payload, event->payload_len);
		idx += event->payload_len;
	}

	(void)meshcore_companion_adapter_queue_frame(frame, idx);
}

static void companion_meshcore_channel_data_response_listener_cb(
	const struct zbus_channel *chan)
{
	const struct mbs_meshcore_channel_data_response_event *event = zbus_chan_const_msg(chan);
	uint8_t frame[MESHCORE_COMPANION_MAX_FRAME_SIZE] = {0};
	size_t idx = 0U;

	ARG_UNUSED(chan);

	if (event == NULL ||
	    event->payload_len > MESHCORE_COMPANION_MAX_FRAME_SIZE -
					 COMPANION_CHANNEL_DATA_OVERHEAD) {
		return;
	}

	frame[idx++] = COMPANION_RESP_CODE_CHANNEL_DATA_RECV;
	frame[idx++] = event->has_rx_snr ? (uint8_t)event->rx_snr_q4 : 0U;
	frame[idx++] = 0U;
	frame[idx++] = 0U;
	frame[idx++] = event->channel_index;
	frame[idx++] = event->path_len;
	append_u16_le(frame, &idx, event->data_type);
	frame[idx++] = event->payload_len;
	if (event->payload_len > 0U) {
		memcpy(&frame[idx], event->payload, event->payload_len);
		idx += event->payload_len;
	}

	(void)meshcore_companion_adapter_queue_frame(frame, idx);
}

static void companion_meshcore_raw_data_response_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_meshcore_raw_data_response_event *event = zbus_chan_const_msg(chan);
	uint8_t frame[MESHCORE_COMPANION_MAX_FRAME_SIZE] = {0};
	size_t idx = 0U;

	ARG_UNUSED(chan);

	if (event == NULL ||
	    event->payload_len == 0U ||
	    (size_t)event->payload_len + COMPANION_RAW_CONTROL_OVERHEAD > sizeof(frame)) {
		return;
	}

	frame[idx++] = COMPANION_PUSH_CODE_RAW_DATA;
	frame[idx++] = event->has_rx_snr ? (uint8_t)event->rx_snr_q4 : 0U;
	frame[idx++] = 0U;
	frame[idx++] = MBS_MESHCORE_OUT_PATH_UNKNOWN;
	memcpy(&frame[idx], event->payload, event->payload_len);
	idx += event->payload_len;

	(void)meshcore_companion_adapter_queue_frame(frame, idx);
}

static void companion_meshcore_control_data_response_listener_cb(
	const struct zbus_channel *chan)
{
	const struct mbs_meshcore_control_data_response_event *event = zbus_chan_const_msg(chan);
	uint8_t frame[MESHCORE_COMPANION_MAX_FRAME_SIZE] = {0};
	size_t idx = 0U;

	ARG_UNUSED(chan);

	if (event == NULL ||
	    event->payload_len == 0U ||
	    (size_t)event->payload_len + COMPANION_RAW_CONTROL_OVERHEAD > sizeof(frame)) {
		return;
	}

	frame[idx++] = COMPANION_PUSH_CODE_CONTROL_DATA;
	frame[idx++] = event->has_rx_snr ? (uint8_t)event->rx_snr_q4 : 0U;
	frame[idx++] = 0U;
	frame[idx++] = event->path_len;
	memcpy(&frame[idx], event->payload, event->payload_len);
	idx += event->payload_len;

	(void)meshcore_companion_adapter_queue_frame(frame, idx);
}

static int companion_cli_meshcore_get_response_handle(
	struct companion_cli_pending *pending,
	const mbs_management_smp_response_event *event)
{
	meshbus_MeshcoreConfigGetResponse rsp =
		meshbus_MeshcoreConfigGetResponse_init_zero;
	meshbus_MeshcoreConfig cfg = meshbus_MeshcoreConfig_init_zero;
	char text[64];
	int rc;

	rc = companion_mgmt_response_decode(
		event,
		meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
		meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_READ_RSP, meshbus_MeshcoreConfigGetResponse_fields,
		&rsp, sizeof(rsp));
	if (rc != 0 || !rsp.has_config) {
		return -EINVAL;
	}
	cfg = rsp.config;

	switch (pending->op) {
	case COMPANION_CLI_OP_GET_NAME:
		return companion_cli_pending_push(pending, cfg.name);
	case COMPANION_CLI_OP_GET_ADVERT_INTERVAL:
		(void)snprintk(text, sizeof(text), "%u",
			       (unsigned int)(cfg.advert_interval / 60U));
		return companion_cli_pending_push(pending, text);
	case COMPANION_CLI_OP_GET_FLOOD_ADVERT_INTERVAL:
		(void)snprintk(text, sizeof(text), "%u",
			       (unsigned int)(cfg.flood_advert_interval / 3600U));
		return companion_cli_pending_push(pending, text);
	case COMPANION_CLI_OP_GET_LAT:
		companion_format_scaled_i32(text, sizeof(text), cfg.latitude,
					    1000000U, 6U);
		return companion_cli_pending_push(pending, text);
	case COMPANION_CLI_OP_GET_LON:
		companion_format_scaled_i32(text, sizeof(text), cfg.longitude,
					    1000000U, 6U);
		return companion_cli_pending_push(pending, text);
	case COMPANION_CLI_OP_GET_REPEAT:
		return companion_cli_pending_push(pending,
						  cfg.disable_fwd ? "off" : "on");
	case COMPANION_CLI_OP_SET_NAME:
		memset(cfg.name, 0, sizeof(cfg.name));
		(void)snprintk(cfg.name, sizeof(cfg.name), "%s",
			       pending->value.text);
		return companion_cli_meshcore_config_set_submit(pending, &cfg);
	case COMPANION_CLI_OP_SET_ADVERT_INTERVAL:
		cfg.advert_interval = pending->value.value_u32;
		return companion_cli_meshcore_config_set_submit(pending, &cfg);
	case COMPANION_CLI_OP_SET_FLOOD_ADVERT_INTERVAL:
		cfg.flood_advert_interval = pending->value.value_u32;
		return companion_cli_meshcore_config_set_submit(pending, &cfg);
	case COMPANION_CLI_OP_SET_LAT:
		cfg.latitude = pending->value.latitude;
		return companion_cli_meshcore_config_set_submit(pending, &cfg);
	case COMPANION_CLI_OP_SET_LON:
		cfg.longitude = pending->value.longitude;
		return companion_cli_meshcore_config_set_submit(pending, &cfg);
	case COMPANION_CLI_OP_SET_PRV_KEY:
		if (!pending->value.identity.has_private_key ||
		    !pending->value.identity.has_public_key) {
			return -EINVAL;
		}
		cfg.private_key.size = sizeof(pending->value.identity.private_key);
		memcpy(cfg.private_key.bytes, pending->value.identity.private_key,
		       cfg.private_key.size);
		cfg.public_key.size = sizeof(pending->value.identity.public_key);
		memcpy(cfg.public_key.bytes, pending->value.identity.public_key,
		       cfg.public_key.size);
		return companion_cli_meshcore_config_set_submit(pending, &cfg);
	case COMPANION_CLI_OP_SET_PUB_KEY:
		if (!pending->value.identity.has_public_key) {
			return -EINVAL;
		}
		cfg.public_key.size = sizeof(pending->value.identity.public_key);
		memcpy(cfg.public_key.bytes, pending->value.identity.public_key,
		       cfg.public_key.size);
		return companion_cli_meshcore_config_set_submit(pending, &cfg);
	case COMPANION_CLI_OP_SET_REPEAT:
		cfg.disable_fwd = !pending->value.value_bool;
		return companion_cli_meshcore_config_set_submit(pending, &cfg);
	default:
		return -ENOTSUP;
	}
}

static int companion_cli_meshcore_set_response_handle(
	struct companion_cli_pending *pending,
	const mbs_management_smp_response_event *event)
{
	meshbus_MeshcoreConfigSetResponse rsp =
		meshbus_MeshcoreConfigSetResponse_init_zero;
	int rc;

	rc = companion_mgmt_response_decode(
		event,
		meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
		meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_WRITE_RSP, meshbus_MeshcoreConfigSetResponse_fields,
		&rsp, sizeof(rsp));
	if (rc != 0 || !rsp.has_config) {
		return -EINVAL;
	}

	return companion_cli_pending_push(pending, "OK");
}

static int companion_cli_radio_get_response_handle(
	struct companion_cli_pending *pending,
	const mbs_management_smp_response_event *event)
{
	meshbus_RadioConfigGetResponse rsp =
		meshbus_RadioConfigGetResponse_init_zero;
	meshbus_RadioConfig cfg = meshbus_RadioConfig_init_zero;
	char freq[24];
	char bw[24];
	char text[80];
	int rc;

	rc = companion_mgmt_response_decode(
		event,
		meshbus_RadioMgmtGroupId_RADIO_MGMT_GROUP_ID_MESHBUS_RADIO,
		meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_READ_RSP, meshbus_RadioConfigGetResponse_fields,
		&rsp, sizeof(rsp));
	if (rc != 0 || !rsp.has_config) {
		return -EINVAL;
	}
	cfg = rsp.config;

	if (pending->op == COMPANION_CLI_OP_GET_RADIO) {
		companion_format_scaled_u64(freq, sizeof(freq),
					    cfg.frequency, 1000000U, 3U);
		companion_format_scaled_u64(bw, sizeof(bw),
					    cfg.bandwidth, 1000U, 3U);
		(void)snprintk(text, sizeof(text), "%s,%s,%u,%u", freq, bw,
			       (unsigned int)cfg.spread_factor,
			       (unsigned int)cfg.coding_rate);
		return companion_cli_pending_push(pending, text);
	}
	if (pending->op != COMPANION_CLI_OP_SET_RADIO) {
		return -ENOTSUP;
	}

	cfg.frequency = pending->value.radio.frequency;
	cfg.bandwidth = pending->value.radio.bandwidth;
	cfg.spread_factor = pending->value.radio.spread_factor;
	cfg.coding_rate = pending->value.radio.coding_rate;
	return companion_cli_radio_config_set_submit(pending, &cfg);
}

static int companion_cli_radio_set_response_handle(
	struct companion_cli_pending *pending,
	const mbs_management_smp_response_event *event)
{
	meshbus_RadioConfigSetResponse rsp =
		meshbus_RadioConfigSetResponse_init_zero;
	int rc;

	rc = companion_mgmt_response_decode(
		event,
		meshbus_RadioMgmtGroupId_RADIO_MGMT_GROUP_ID_MESHBUS_RADIO,
		meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_WRITE_RSP, meshbus_RadioConfigSetResponse_fields,
		&rsp, sizeof(rsp));
	if (rc != 0 || !rsp.has_config) {
		return -EINVAL;
	}

	return companion_cli_pending_push(pending, "OK");
}

static int companion_cli_direct_response_handle(
	struct companion_cli_pending *pending,
	const mbs_management_smp_response_event *event)
{
	int rc;

	switch (pending->op) {
	case COMPANION_CLI_OP_ADVERT: {
		meshbus_MeshcoreAdvertResponse rsp =
			meshbus_MeshcoreAdvertResponse_init_zero;

		rc = companion_mgmt_response_decode(
			event,
			meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
			meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_ADVERT,
			MGMT_OP_WRITE_RSP, meshbus_MeshcoreAdvertResponse_fields,
			&rsp, sizeof(rsp));
		if (rc != 0 || !rsp.accepted) {
			return -EINVAL;
		}
		return companion_cli_pending_push(pending, "OK");
	}
	case COMPANION_CLI_OP_TIME: {
		meshbus_ClockTimeSetResponse rsp =
			meshbus_ClockTimeSetResponse_init_zero;
		char text[96];

		rc = companion_mgmt_response_decode(
			event,
			meshbus_ClockMgmtGroupId_CLOCK_MGMT_GROUP_ID_MESHBUS_CLOCK,
			meshbus_ClockMgmtCommandId_CLOCK_MGMT_COMMAND_ID_TIME_SET,
			MGMT_OP_WRITE_RSP, meshbus_ClockTimeSetResponse_fields,
			&rsp, sizeof(rsp));
		if (rc != 0 || !rsp.accepted) {
			return -EINVAL;
		}
		rc = companion_format_clock_set_response(rsp.unix_time_ms,
							 text, sizeof(text));
		if (rc != 0) {
			return rc;
		}
		return companion_cli_pending_push(pending, text);
	}
	case COMPANION_CLI_OP_PASSWORD: {
		meshbus_ManagementSecretSetResponse rsp =
			meshbus_ManagementSecretSetResponse_init_zero;

		rc = companion_mgmt_response_decode(
			event,
			meshbus_ManagementGroupId_MANAGEMENT_GROUP_ID_MESHBUS_MANAGEMENT,
			meshbus_ManagementCommandId_MANAGEMENT_COMMAND_ID_SECRET,
			MGMT_OP_WRITE_RSP,
			meshbus_ManagementSecretSetResponse_fields,
			&rsp, sizeof(rsp));
		if (rc != 0 || !rsp.accepted) {
			return -EINVAL;
		}
		(void)companion_login_session_secret_replace(
			pending->contact_prefix, pending->value.secret.bytes,
			pending->value.secret.len);
		return companion_cli_pending_push(pending, "OK");
	}
	case COMPANION_CLI_OP_REBOOT: {
		meshbus_PowerRebootResponse rsp =
			meshbus_PowerRebootResponse_init_zero;

		rc = companion_mgmt_response_decode(
			event,
			meshbus_PowerMgmtGroupId_POWER_MGMT_GROUP_ID_MESHBUS_POWER,
			meshbus_PowerMgmtCommandId_POWER_MGMT_COMMAND_ID_REBOOT,
			MGMT_OP_WRITE_RSP, meshbus_PowerRebootResponse_fields,
			&rsp, sizeof(rsp));
		if (rc != 0 || !rsp.triggered) {
			return -EINVAL;
		}
		return companion_cli_pending_push(pending, "OK");
	}
	default:
		return -ENOTSUP;
	}
}

static int companion_cli_response_handle(
	struct companion_cli_pending *pending,
	const mbs_management_smp_response_event *event)
{
	if (pending == NULL || event == NULL) {
		return -EINVAL;
	}

	if (pending->stage == COMPANION_CLI_STAGE_CONFIG_GET) {
		switch (pending->op) {
		case COMPANION_CLI_OP_GET_NAME:
		case COMPANION_CLI_OP_SET_NAME:
		case COMPANION_CLI_OP_GET_ADVERT_INTERVAL:
		case COMPANION_CLI_OP_GET_FLOOD_ADVERT_INTERVAL:
		case COMPANION_CLI_OP_SET_ADVERT_INTERVAL:
		case COMPANION_CLI_OP_SET_FLOOD_ADVERT_INTERVAL:
		case COMPANION_CLI_OP_GET_LAT:
		case COMPANION_CLI_OP_SET_LAT:
		case COMPANION_CLI_OP_GET_LON:
		case COMPANION_CLI_OP_SET_LON:
		case COMPANION_CLI_OP_SET_PRV_KEY:
		case COMPANION_CLI_OP_SET_PUB_KEY:
		case COMPANION_CLI_OP_GET_REPEAT:
		case COMPANION_CLI_OP_SET_REPEAT:
			return companion_cli_meshcore_get_response_handle(pending, event);
		case COMPANION_CLI_OP_GET_RADIO:
		case COMPANION_CLI_OP_SET_RADIO:
			return companion_cli_radio_get_response_handle(pending, event);
		default:
			return -ENOTSUP;
		}
	}

	if (pending->stage == COMPANION_CLI_STAGE_CONFIG_SET) {
		switch (pending->op) {
		case COMPANION_CLI_OP_SET_NAME:
		case COMPANION_CLI_OP_SET_ADVERT_INTERVAL:
		case COMPANION_CLI_OP_SET_FLOOD_ADVERT_INTERVAL:
		case COMPANION_CLI_OP_SET_LAT:
		case COMPANION_CLI_OP_SET_LON:
		case COMPANION_CLI_OP_SET_PRV_KEY:
		case COMPANION_CLI_OP_SET_PUB_KEY:
		case COMPANION_CLI_OP_SET_REPEAT:
			return companion_cli_meshcore_set_response_handle(pending, event);
		case COMPANION_CLI_OP_SET_RADIO:
			return companion_cli_radio_set_response_handle(pending, event);
		default:
			return -ENOTSUP;
		}
	}

	return companion_cli_direct_response_handle(pending, event);
}

#if defined(CONFIG_MBS_MANAGEMENT)
static void companion_management_smp_response_listener_cb(
	const struct zbus_channel *chan)
{
	const mbs_management_smp_response_event *event =
		zbus_chan_const_msg(chan);
	struct companion_login_pending pending = {0};
	struct companion_cli_pending cli_pending = {0};
	uint8_t frame[MESHCORE_COMPANION_MAX_FRAME_SIZE] = {0};
	size_t idx = 0U;
	bool success;
	int rc;

	ARG_UNUSED(chan);

	if (event == NULL) {
		return;
	}
	if (!companion_pending_login_take(event->tag, &pending)) {
		if (!companion_pending_cli_take(event->tag, &cli_pending)) {
			return;
		}

		rc = companion_cli_response_handle(&cli_pending, event);
		if (rc != 0) {
			(void)companion_cli_pending_push(&cli_pending, "ERR");
		}
		companion_secure_wipe(&cli_pending, sizeof(cli_pending));
		return;
	}

	success = event->response_len >= COMPANION_MGMT_HDR_SIZE &&
		  event->response[0] == MGMT_OP_READ_RSP &&
		  event->response[4] ==
			  (uint8_t)(meshbus_PowerMgmtGroupId_POWER_MGMT_GROUP_ID_MESHBUS_POWER >> 8) &&
		  event->response[5] ==
			  (uint8_t)meshbus_PowerMgmtGroupId_POWER_MGMT_GROUP_ID_MESHBUS_POWER &&
		  event->response[7] == meshbus_PowerMgmtCommandId_POWER_MGMT_COMMAND_ID_STATUS;

	if (success) {
		frame[idx++] = COMPANION_PUSH_CODE_LOGIN_SUCCESS;
		frame[idx++] = COMPANION_LOGIN_PERMISSION_ADMIN;
		memcpy(&frame[idx], pending.public_key_prefix,
		       sizeof(pending.public_key_prefix));
		idx += sizeof(pending.public_key_prefix);
		append_u32_le(frame, &idx, companion_current_timestamp());
		frame[idx++] = COMPANION_LOGIN_ACL_PERM_ADMIN;
		frame[idx++] = COMPANION_FIRMWARE_VER_CODE;
		(void)companion_login_session_store(pending.contact_prefix,
						    pending.public_key_prefix,
						    pending.secret,
						    pending.secret_len);
	} else {
		frame[idx++] = COMPANION_PUSH_CODE_LOGIN_FAIL;
		frame[idx++] = 0U;
		memcpy(&frame[idx], pending.public_key_prefix,
		       sizeof(pending.public_key_prefix));
		idx += sizeof(pending.public_key_prefix);
	}

	(void)meshcore_companion_adapter_queue_frame(frame, idx);
	companion_secure_wipe(&pending, sizeof(pending));
}
#endif

ZBUS_LISTENER_DEFINE(companion_message_waiting_listener, companion_message_waiting_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_message_response_chan, companion_message_waiting_listener, 3);

ZBUS_LISTENER_DEFINE(companion_message_ack_listener, companion_message_ack_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_message_ack_response_chan, companion_message_ack_listener, 3);

ZBUS_LISTENER_DEFINE(companion_contact_advert_response_listener,
		     companion_contact_advert_response_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_contact_advert_chan, companion_contact_advert_response_listener, 3);

ZBUS_LISTENER_DEFINE(companion_contact_path_response_listener,
		     companion_contact_path_response_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_contact_path_response_chan, companion_contact_path_response_listener, 3);

ZBUS_LISTENER_DEFINE(companion_contact_trace_response_listener,
		     companion_contact_trace_response_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_contact_trace_path_response_chan, companion_contact_trace_response_listener, 3);

ZBUS_LISTENER_DEFINE(companion_meshcore_trace_response_listener,
		     companion_meshcore_trace_response_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_meshcore_trace_response_chan,
		  companion_meshcore_trace_response_listener, 3);

ZBUS_LISTENER_DEFINE(companion_contact_telemetry_response_listener,
		     companion_contact_telemetry_response_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_contact_telemetry_response_chan, companion_contact_telemetry_response_listener, 3);

ZBUS_LISTENER_DEFINE(companion_contact_binary_response_listener,
		     companion_contact_binary_response_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_contact_binary_response_chan, companion_contact_binary_response_listener, 3);

ZBUS_LISTENER_DEFINE(companion_meshcore_channel_data_response_listener,
		     companion_meshcore_channel_data_response_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_meshcore_channel_data_response_chan,
		  companion_meshcore_channel_data_response_listener, 3);

ZBUS_LISTENER_DEFINE(companion_meshcore_raw_data_response_listener,
		     companion_meshcore_raw_data_response_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_meshcore_raw_data_response_chan,
		  companion_meshcore_raw_data_response_listener, 3);

ZBUS_LISTENER_DEFINE(companion_meshcore_control_data_response_listener,
		     companion_meshcore_control_data_response_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_meshcore_control_data_response_chan,
		  companion_meshcore_control_data_response_listener, 3);

#if defined(CONFIG_MBS_MANAGEMENT)
ZBUS_LISTENER_DEFINE(companion_management_smp_response_listener,
		     companion_management_smp_response_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_management_smp_response_chan,
		  companion_management_smp_response_listener, 3);
#endif

int meshcore_companion_adapter_init(const struct meshcore_companion_transport *transport)
{
	if (transport == NULL || transport->send == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&adapter_lock, K_FOREVER);
	adapter_transport = *transport;
	adapter_initialized = true;
	adapter_connected = false;
	k_mutex_unlock(&adapter_lock);
	(void)k_work_cancel(&companion_cli_start_work);
	(void)k_work_cancel(&companion_contact_update_work);
	(void)k_work_cancel_delayable(&companion_contact_sync_work);

	companion_pending_ack_clear_all();
	companion_contact_sync_clear();
	companion_pending_trace_clear_all();
	companion_pending_status_clear_all();
	companion_pending_login_clear_all();
	companion_login_sessions_clear_all();
	companion_pending_cli_clear_all();
	companion_contact_update_clear_all();

	return 0;
}

void meshcore_companion_adapter_connected(void)
{
	k_mutex_lock(&adapter_lock, K_FOREVER);
	if (adapter_initialized) {
		adapter_connected = true;
	}
	k_mutex_unlock(&adapter_lock);
}

void meshcore_companion_adapter_disconnected(void)
{
	k_mutex_lock(&adapter_lock, K_FOREVER);
	adapter_connected = false;
	k_mutex_unlock(&adapter_lock);
	(void)k_work_cancel(&companion_cli_start_work);
	(void)k_work_cancel(&companion_contact_update_work);
	(void)k_work_cancel_delayable(&companion_contact_sync_work);

	companion_pending_ack_clear_all();
	companion_contact_sync_clear();
	companion_pending_trace_clear_all();
	companion_pending_status_clear_all();
	companion_pending_login_clear_all();
	companion_login_sessions_clear_all();
	companion_pending_cli_clear_all();
	companion_contact_update_clear_all();
}

int meshcore_companion_adapter_rx_frame(const uint8_t *frame, size_t len)
{
	uint8_t command;
	int rc;

	rc = validate_frame_args(frame, len);
	if (rc != 0) {
		LOG_WRN("Companion app RX rejected: len=%u rc=%d",
			(unsigned int)len, rc);
		return rc;
	}

	command = frame[0];
	LOG_DBG("Companion app RX command: cmd=%u name=%s len=%u",
		(unsigned int)command, companion_command_name(command),
		(unsigned int)len);
	companion_log_binary_command_body(command, frame, len);

	switch (command) {
	case COMPANION_CMD_DEVICE_QEURY:
		rc = companion_handle_device_query(frame, len);
		break;
	case COMPANION_CMD_APP_START:
		rc = companion_handle_app_start(frame, len);
		break;
	case COMPANION_CMD_SEND_TXT_MSG:
		rc = companion_handle_send_txt_msg(frame, len);
		break;
	case COMPANION_CMD_SEND_CHANNEL_TXT_MSG:
		rc = companion_handle_send_channel_txt_msg(frame, len);
		break;
	case COMPANION_CMD_GET_CONTACTS:
		rc = companion_handle_get_contacts(frame, len);
		break;
	case COMPANION_CMD_GET_DEVICE_TIME:
		rc = companion_handle_get_device_time();
		break;
	case COMPANION_CMD_SET_DEVICE_TIME:
		rc = companion_handle_set_device_time(frame, len);
		break;
	case COMPANION_CMD_SEND_SELF_ADVERT:
		rc = companion_handle_send_self_advert(frame, len);
		break;
	case COMPANION_CMD_SET_NAME:
		rc = companion_handle_set_name(frame, len);
		break;
	case COMPANION_CMD_ADD_UPDATE_CONTACT:
		rc = companion_handle_add_update_contact(frame, len);
		break;
	case COMPANION_CMD_SYNC_NEXT_MESSAGE:
		rc = companion_handle_sync_next_message();
		break;
	case COMPANION_CMD_SET_RADIO:
		rc = companion_handle_set_radio(frame, len);
		break;
	case COMPANION_CMD_SET_TX_POWER:
		rc = companion_handle_set_tx_power(frame, len);
		break;
	case COMPANION_CMD_RESET_PATH:
		rc = companion_handle_reset_path(frame, len);
		break;
	case COMPANION_CMD_SET_COORDINATES:
		rc = companion_handle_set_coordinates(frame, len);
		break;
	case COMPANION_CMD_REMOVE_CONTACT:
		rc = companion_handle_remove_contact(frame, len);
		break;
	case COMPANION_CMD_GET_BATT_AND_STORAGE:
		rc = companion_handle_get_batt_and_storage();
		break;
	case COMPANION_CMD_SET_TUNING:
		rc = companion_handle_set_tuning(frame, len);
		break;
	case COMPANION_CMD_SEND_RAW_DATA:
		rc = companion_handle_send_raw_data(frame, len);
		break;
	case COMPANION_CMD_SEND_LOGIN:
		rc = companion_handle_send_login(frame, len);
		break;
	case COMPANION_CMD_SEND_STATUS_REQ:
		rc = companion_handle_status_req(frame, len);
		break;
	case COMPANION_CMD_GET_CONTACT_BY_KEY:
		rc = companion_handle_get_contact_by_key(frame, len);
		break;
	case COMPANION_CMD_GET_CHANNEL:
		rc = companion_handle_get_channel(frame, len);
		break;
	case COMPANION_CMD_SET_CHANNEL:
		rc = companion_handle_set_channel(frame, len);
		break;
	case COMPANION_CMD_SEND_TRACE_PATH:
		rc = companion_handle_trace_path(frame, len);
		break;
	case COMPANION_CMD_SET_DEVICE_PIN:
		rc = companion_handle_set_device_pin(frame, len);
		break;
	case COMPANION_CMD_SET_OTHER_PARAMS:
		rc = companion_handle_set_other_params(frame, len);
		break;
	case COMPANION_CMD_SEND_TELEMETRY_REQ:
		rc = companion_handle_telemetry_req(frame, len);
		break;
	case COMPANION_CMD_SEND_BINARY_REQ:
		rc = companion_handle_send_binary_req(frame, len);
		break;
	case COMPANION_CMD_SEND_PATH_DISCOVERY_REQ:
		rc = companion_handle_path_discovery(frame, len);
		break;
	case COMPANION_CMD_SEND_CONTROL_DATA:
		rc = companion_handle_send_control_data(frame, len);
		break;
	case COMPANION_CMD_SET_AUTOADD_CONFIG:
		rc = companion_handle_set_autoadd_config(frame, len);
		break;
	case COMPANION_CMD_GET_AUTOADD_CONFIG:
		rc = companion_handle_get_autoadd_config();
		break;
	case COMPANION_CMD_GET_ALLOWED_REPEAT_FREQ:
		rc = companion_handle_get_allowed_repeat_freq();
		break;
	case COMPANION_CMD_SET_PATH_HASH_MODE:
		rc = companion_handle_set_path_hash_mode(frame, len);
		break;
	case COMPANION_CMD_SEND_CHANNEL_DATA:
		rc = companion_handle_send_channel_data(frame, len);
		break;
	default:
		LOG_WRN("Companion app unsupported command: cmd=%u len=%u",
			(unsigned int)command, (unsigned int)len);
		rc = companion_send_error(COMPANION_ERR_CODE_UNSUPPORTED_CMD);
		break;
	}

	if (rc != 0) {
		LOG_WRN("Companion app command handler failed: cmd=%u name=%s rc=%d",
			(unsigned int)command, companion_command_name(command), rc);
	}

	return rc;
}

int meshcore_companion_adapter_queue_frame(const uint8_t *frame, size_t len)
{
	return companion_send_frame(frame, len);
}

void meshcore_companion_adapter_flush(void)
{
	struct k_work_sync cli_start_sync;
	struct k_work_sync contact_update_sync;
	struct k_work_sync contact_sync;

	(void)k_work_flush(&companion_cli_start_work, &cli_start_sync);
	(void)k_work_flush(&companion_contact_update_work, &contact_update_sync);
	(void)k_work_flush_delayable(&companion_contact_sync_work, &contact_sync);
}
