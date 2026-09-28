/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include <string.h>
#include <time.h>

#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/clock.h>
#include <zephyr/ztest.h>

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
#include "meshcore_prvi.h"
#include "companion/protocol.h"

/* Companion wire and adapter contract coverage. */

#define TEST_RESP_CODE_ERR          1U
#define TEST_ERR_UNSUPPORTED_CMD    1U
#define TEST_ERR_BAD_STATE          4U
#define TEST_ERR_ILLEGAL_ARG        6U
#define TEST_CAPTURED_FRAME_COUNT   16U
#define TEST_MGMT_HDR_SIZE          8U
#define TEST_MGMT_CBOR_STATES       4U

enum {
	TEST_MGMT_PROTO_MAX_SIZE =
		MAX(MAX(meshbus_MeshcoreConfigGetResponse_size,
			meshbus_MeshcoreConfigSetRequest_size),
			MAX(MAX(meshbus_RadioConfigGetResponse_size,
				meshbus_RadioConfigSetRequest_size),
			    MAX(MAX(meshbus_ManagementSecretSetRequest_size,
				    meshbus_ClockTimeSetRequest_size),
				meshbus_ClockTimeSetResponse_size))),
};

#define TEST_CMD_APP_START             1U
#define TEST_CMD_SEND_TXT_MSG          2U
#define TEST_CMD_SEND_CHANNEL_TXT_MSG  3U
#define TEST_CMD_GET_CONTACTS          4U
#define TEST_CMD_GET_DEVICE_TIME       5U
#define TEST_CMD_SET_DEVICE_TIME       6U
#define TEST_CMD_SEND_SELF_ADVERT      7U
#define TEST_CMD_SET_NAME              8U
#define TEST_CMD_ADD_UPDATE_CONTACT    9U
#define TEST_CMD_SYNC_NEXT_MESSAGE     10U
#define TEST_CMD_RESET_PATH            13U
#define TEST_CMD_SET_COORDINATES       14U
#define TEST_CMD_REMOVE_CONTACT        15U
#define TEST_CMD_SET_TUNING            21U
#define TEST_CMD_GET_BATT_AND_STORAGE  20U
#define TEST_CMD_DEVICE_QEURY          22U
#define TEST_CMD_SEND_RAW_DATA         25U
#define TEST_CMD_SEND_LOGIN            26U
#define TEST_CMD_SEND_STATUS_REQ       27U
#define TEST_CMD_GET_CONTACT_BY_KEY    30U
#define TEST_CMD_GET_CHANNEL           31U
#define TEST_CMD_SET_CHANNEL           32U
#define TEST_CMD_SET_DEVICE_PIN        37U
#define TEST_CMD_SET_OTHER_PARAMS      38U
#define TEST_CMD_SEND_TRACE_PATH       36U
#define TEST_CMD_SEND_TELEMETRY_REQ    39U
#define TEST_CMD_SEND_BINARY_REQ       50U
#define TEST_CMD_SEND_PATH_DISCOVERY   52U
#define TEST_CMD_SEND_CONTROL_DATA     55U
#define TEST_CMD_SET_AUTOADD_CONFIG    58U
#define TEST_CMD_GET_AUTOADD_CONFIG    59U
#define TEST_CMD_GET_REPEAT_FREQ       60U
#define TEST_CMD_SET_PATH_HASH_MODE    61U
#define TEST_CMD_SEND_CHANNEL_DATA     62U

#define TEST_RESP_CODE_OK                0U
#define TEST_RESP_CODE_CONTACTS_START    2U
#define TEST_RESP_CODE_CONTACT           3U
#define TEST_RESP_CODE_END_OF_CONTACTS   4U
#define TEST_RESP_CODE_SELF_INFO         5U
#define TEST_RESP_CODE_SENT              6U
#define TEST_RESP_CODE_CURR_TIME         9U
#define TEST_RESP_CODE_NO_MORE_MESSAGES  10U
#define TEST_RESP_CODE_BATT_AND_STORAGE  12U
#define TEST_RESP_CODE_DEVICE_INFO       13U
#define TEST_RESP_CODE_CONTACT_MSG_V3    16U
#define TEST_RESP_CODE_CHANNEL_MSG_V3    17U
#define TEST_RESP_CODE_CHANNEL_INFO      18U
#define TEST_RESP_CODE_AUTOADD_CONFIG    25U
#define TEST_RESP_CODE_REPEAT_FREQ       26U
#define TEST_RESP_CODE_CHANNEL_DATA_RECV 27U

#define TEST_TXT_TYPE_PLAIN    0U
#define TEST_TXT_TYPE_CLI_DATA 1U

#define TEST_PUSH_CODE_SEND_CONFIRMED     0x82U
#define TEST_PUSH_CODE_MSG_WAITING        0x83U
#define TEST_PUSH_CODE_RAW_DATA           0x84U
#define TEST_PUSH_CODE_LOGIN_SUCCESS      0x85U
#define TEST_PUSH_CODE_LOGIN_FAIL         0x86U
#define TEST_PUSH_CODE_STATUS_RESPONSE    0x87U
#define TEST_PUSH_CODE_ADVERT             0x80U
#define TEST_PUSH_CODE_PATH_UPDATED       0x81U
#define TEST_PUSH_CODE_NEW_ADVERT         0x8aU
#define TEST_PUSH_CODE_TRACE_DATA         0x89U
#define TEST_PUSH_CODE_TELEMETRY_RESPONSE 0x8bU
#define TEST_PUSH_CODE_BINARY_RESPONSE    0x8cU
#define TEST_PUSH_CODE_PATH_DISCOVERY     0x8dU
#define TEST_PUSH_CODE_CONTROL_DATA       0x8eU

#define TEST_APP_PREFIX_SIZE 6U
#define TEST_CONTACT_FRAME_SIZE 148U
#define TEST_CONTACT_PATH_UNKNOWN 0xffU
#define TEST_CHANNEL_INFO_SIZE 50U

struct captured_tx {
	size_t count;
	size_t len[TEST_CAPTURED_FRAME_COUNT];
	uint8_t frame[TEST_CAPTURED_FRAME_COUNT][MESHCORE_COMPANION_MAX_FRAME_SIZE];
};

static struct captured_tx captured;
static K_SEM_DEFINE(captured_tx_changed, 0, TEST_CAPTURED_FRAME_COUNT);
static uint8_t test_public_key[32];
static uint64_t captured_set_time_ms;
static uint8_t captured_node_prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES];
static uint8_t captured_payload[CONFIG_MBS_MESSAGE_TX_MAX_LEN];
static size_t captured_payload_len;
static uint8_t captured_attempt;
static uint8_t captured_channel_index;
static size_t captured_channel_set_index;
static uint8_t captured_channel_set_secret[MBS_CHANNEL_SECRET_DEFAULT_LEN];
static char captured_channel_set_name[MBS_CHANNEL_NAME_MAX_LEN];
static size_t captured_channel_reset_index;
static uint8_t captured_binary_prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES];
static uint32_t captured_discover_tag;
static uint32_t captured_trace_tag;
static uint32_t captured_telemetry_tag;
static uint32_t captured_binary_tag;
static uint32_t captured_binary_next_tag;
static uint8_t captured_binary_payload[MBS_CONTACT_BINARY_REQUEST_PAYLOAD_MAX_LEN];
static size_t captured_binary_payload_len;
static uint8_t captured_meshcore_path[MBS_MESHCORE_PATH_MAX_LEN];
static uint8_t captured_meshcore_path_len;
static uint8_t captured_meshcore_trace_path[MBS_MESHCORE_PATH_MAX_LEN];
static uint8_t captured_meshcore_trace_path_len;
static uint8_t captured_meshcore_trace_path_hash_size;
static uint32_t captured_meshcore_trace_tag;
static uint16_t captured_meshcore_data_type;
static uint8_t captured_meshcore_payload[MBS_MESHCORE_RAW_DATA_PAYLOAD_MAX_LEN];
static size_t captured_meshcore_payload_len;
static bool captured_flood;
static mbs_management_smp_request_event captured_management_request;
static uint8_t captured_management_secret[MBS_MANAGEMENT_SECRET_MAX_LEN];
static size_t captured_management_secret_len;
static uint32_t captured_management_tag;
static int captured_management_rc;
static uint32_t next_management_tag = 0x77889900U;
static mbs_contact captured_contact_update;
static mbs_meshcore_config captured_meshcore_config_set;
static size_t captured_meshcore_config_set_count;
static bool mock_message_available;
static mbs_message_content mock_message;
static bool mock_client_repeat;
static bool mock_unknown_zero_path_contact;
static uint8_t capture_fail_code;
static int capture_fail_rc;
static uint8_t capture_fail_remaining;

static size_t test_path_len_bytes(uint8_t path_len);

static int capture_send(const uint8_t *frame, size_t len, void *user_data)
{
	struct captured_tx *tx = user_data;

	/* Capture final transport admission; no driver I/O takes place here. */
	zassert_not_null(frame, "frame must be set");
	zassert_not_null(tx, "capture must be set");
	zassert_true(len <= MESHCORE_COMPANION_MAX_FRAME_SIZE, "frame too large");
	zassert_true(tx->count < TEST_CAPTURED_FRAME_COUNT, "capture overflow");

	if (capture_fail_remaining > 0U && frame[0] == capture_fail_code) {
		capture_fail_remaining--;
		return capture_fail_rc;
	}

	tx->len[tx->count] = len;
	memcpy(tx->frame[tx->count], frame, len);
	tx->count++;
	k_sem_give(&captured_tx_changed);

	return 0;
}

int __wrap_mbs_meshcore_config_get(mbs_meshcore_config *cfg)
{
	zassert_not_null(cfg, "cfg must be set");

	memset(cfg, 0, sizeof(*cfg));
	cfg->public_key.size = sizeof(test_public_key);
	memcpy(cfg->public_key.bytes, test_public_key, sizeof(test_public_key));
	strncpy(cfg->name, "phase2-meshcore", sizeof(cfg->name) - 1U);
	cfg->latitude = -1234567;
	cfg->longitude = 7654321;
	cfg->multi_acks = 3U;
	cfg->advert_position = true;
	cfg->telemetry_mode_base = meshbus_MeshcoreConfig_TelemetryMode_TELEMETRY_ALLOW_FLAGS;
	cfg->telemetry_mode_locat = meshbus_MeshcoreConfig_TelemetryMode_TELEMETRY_ALLOW_ALL;
	cfg->telemetry_mode_environment = meshbus_MeshcoreConfig_TelemetryMode_TELEMETRY_DENY;
	cfg->add_contact_config = 0x23U;
	cfg->client_repeat = mock_client_repeat;

	return 0;
}

int __wrap_mbs_meshcore_config_set(mbs_meshcore_config *cfg)
{
	zassert_not_null(cfg, "cfg must be set");
	captured_meshcore_config_set = *cfg;
	captured_meshcore_config_set_count++;
	return 0;
}

uint8_t __wrap_mbs_contact_store_size(void)
{
	return 64U;
}

uint8_t __wrap_mbs_contact_store_count(void)
{
	return 2U;
}

static void fill_contact(mbs_contact *contact, uint8_t seed)
{
	memset(contact, 0, sizeof(*contact));
	contact->public_key.size = 32U;
	for (size_t i = 0; i < contact->public_key.size; i++) {
		contact->public_key.bytes[i] = seed + i;
	}
	strncpy(contact->name, seed == 0x10U ? "alpha" : "bravo", sizeof(contact->name) - 1U);
	contact->role = MBS_CONTACT_ROLE_CHAT;
	contact->flags = 0x0fU;
	if (mock_unknown_zero_path_contact && seed == 0x10U) {
		contact->is_neighbor = false;
		contact->path_hash_size = 0U;
	} else {
		contact->is_neighbor = true;
		contact->path_hash_size = 1U;
	}
	contact->latitude = 1234567;
	contact->longitude = -7654321;
	contact->last_seen_timestamp = seed == 0x10U ? 1775000001U : 1775000002U;
}

int __wrap_mbs_contact_get(size_t index, mbs_contact *contact)
{
	if (contact == NULL) {
		return -EINVAL;
	}
	if (index == 0U) {
		fill_contact(contact, 0x10U);
		return 0;
	}
	if (index == 1U) {
		fill_contact(contact, 0x20U);
		return 0;
	}

	return -ENOENT;
}

int __wrap_mbs_contact_find_by_key(const uint8_t *public_key, mbs_contact *contact)
{
	if (public_key == NULL || contact == NULL) {
		return -EINVAL;
	}
	if (public_key[0] == 0x10U) {
		fill_contact(contact, 0x10U);
		return 0;
	}

	return -ENOENT;
}

int __wrap_mbs_contact_find_by_prefix(const uint8_t *prefix, mbs_contact *contact)
{
	if (prefix == NULL || contact == NULL) {
		return -EINVAL;
	}
	if (prefix[0] == 0x10U) {
		fill_contact(contact, 0x10U);
		return 0;
	}
	if (prefix[0] == 0x41U) {
		*contact = (mbs_contact)meshbus_Contact_init_zero;
		contact->public_key.size = MBS_CONTACT_PUBLIC_KEY_SIZE;
		memcpy(contact->public_key.bytes, prefix, CONFIG_MBS_CONTACT_PREFIX_BYTES);
		for (size_t i = CONFIG_MBS_CONTACT_PREFIX_BYTES; i < TEST_APP_PREFIX_SIZE;
		     i++) {
			contact->public_key.bytes[i] = (uint8_t)(0x51U + i -
								 CONFIG_MBS_CONTACT_PREFIX_BYTES);
		}
		return 0;
	}

	return -ENOENT;
}

int __wrap_mbs_contact_set(const uint8_t *public_key_prefix, const mbs_contact *contact)
{
	zassert_not_null(public_key_prefix, "contact prefix must be set");
	zassert_not_null(contact, "contact must be set");
	captured_contact_update = *contact;
	return 0;
}

int __wrap_mbs_contact_reset(const uint8_t *public_key_prefix)
{
	return public_key_prefix == NULL ? -EINVAL : 0;
}

uint8_t __wrap_mbs_channel_store_size(void)
{
	return 8U;
}

int __wrap_mbs_channel_get(size_t index, mbs_channel *channel)
{
	if (channel == NULL) {
		return -EINVAL;
	}
	if (index != 2U) {
		return -ENOENT;
	}

	memset(channel, 0, sizeof(*channel));
	strncpy(channel->name, "ops", sizeof(channel->name) - 1U);
	channel->secret.size = 16U;
	for (size_t i = 0; i < channel->secret.size; i++) {
		channel->secret.bytes[i] = 0xb0U + i;
	}

	return 0;
}

int __wrap_mbs_channel_set(size_t index, const uint8_t *secret, size_t secret_len,
			       const char *name)
{
	zassert_not_null(secret, "secret must be set");
	zassert_equal(secret_len, MBS_CHANNEL_SECRET_DEFAULT_LEN, "secret length mismatch");
	zassert_not_null(name, "name must be set");

	captured_channel_set_index = index;
	memcpy(captured_channel_set_secret, secret, sizeof(captured_channel_set_secret));
	strncpy(captured_channel_set_name, name, sizeof(captured_channel_set_name) - 1U);
	return 0;
}

int __wrap_mbs_channel_reset(size_t index)
{
	captured_channel_reset_index = index;
	return 0;
}

int __wrap_mbs_message_send_to_node(const uint8_t *public_key_prefix,
					const uint8_t *payload, size_t payload_len,
					bool flood, uint8_t attempt, uint64_t *out_ack_token)
{
	zassert_not_null(public_key_prefix, "prefix must be set");
	zassert_not_null(payload, "payload must be set");
	zassert_true(payload_len <= sizeof(captured_payload), "payload too large");

	memcpy(captured_node_prefix, public_key_prefix, sizeof(captured_node_prefix));
	memcpy(captured_payload, payload, payload_len);
	captured_payload_len = payload_len;
	captured_attempt = attempt;
	captured_flood = flood;
	if (out_ack_token != NULL) {
		*out_ack_token = 0x11223344U;
	}

	return 0;
}

int __wrap_mbs_message_send_to_channel(size_t channel_index, const uint8_t *payload,
					   size_t payload_len)
{
	zassert_not_null(payload, "payload must be set");
	zassert_true(payload_len <= sizeof(captured_payload), "payload too large");

	captured_channel_index = channel_index;
	memcpy(captured_payload, payload, payload_len);
	captured_payload_len = payload_len;

	return 0;
}

int __wrap_mbs_message_next(mbs_message_content *message)
{
	if (message == NULL) {
		return -EINVAL;
	}
	if (!mock_message_available) {
		*message = (mbs_message_content)meshbus_MessageContent_init_zero;
		return -ENOENT;
	}

	*message = mock_message;
	mock_message_available = false;
	return 0;
}

int __wrap_mbs_meshcore_advert_request(bool flood)
{
	captured_flood = flood;
	return 0;
}

int __wrap_mbs_meshcore_trace_request(const uint8_t *path, uint8_t path_len,
					  uint8_t path_hash_size, uint32_t *out_tag)
{
	zassert_not_null(path, "path must be set");
	zassert_true(path_len <= sizeof(captured_meshcore_trace_path),
		     "trace path too long");

	memcpy(captured_meshcore_trace_path, path, path_len);
	captured_meshcore_trace_path_len = path_len;
	captured_meshcore_trace_path_hash_size = path_hash_size;
	captured_meshcore_trace_tag = 0x55667788U;
	if (out_tag != NULL) {
		*out_tag = captured_meshcore_trace_tag;
	}

	return 0;
}

int __wrap_mbs_contact_discover_path_request(const uint8_t *prefix, uint32_t *out_tag)
{
	zassert_not_null(prefix, "prefix must be set");
	memcpy(captured_node_prefix, prefix, sizeof(captured_node_prefix));
	captured_discover_tag = 0x22334455U;
	if (out_tag != NULL) {
		*out_tag = captured_discover_tag;
	}
	return 0;
}

int __wrap_mbs_contact_trace_path_request(const uint8_t *prefix, uint32_t *out_tag)
{
	zassert_not_null(prefix, "prefix must be set");
	memcpy(captured_node_prefix, prefix, sizeof(captured_node_prefix));
	captured_trace_tag = 0x33445566U;
	if (out_tag != NULL) {
		*out_tag = captured_trace_tag;
	}
	return 0;
}

int __wrap_mbs_contact_telemetry_request(const uint8_t *prefix, uint32_t *out_tag)
{
	zassert_not_null(prefix, "prefix must be set");
	memcpy(captured_node_prefix, prefix, sizeof(captured_node_prefix));
	captured_telemetry_tag = 0x11223344U;
	if (out_tag != NULL) {
		*out_tag = captured_telemetry_tag;
	}

	return 0;
}

int __wrap_meshcore_platform_telemetry_node_get(
	const meshcore_platform_request_source_t *requester,
	uint8_t permission_mask, meshcore_platform_telemetry_payload_t *out)
{
	ARG_UNUSED(requester);

	zassert_not_null(out, "telemetry payload must be set");
	zassert_equal(permission_mask,
		      MESHCORE_TELEM_PERM_BASE | MESHCORE_TELEM_PERM_LOCATION |
			      MESHCORE_TELEM_PERM_ENVIRONMENT,
		      "telemetry permission mismatch");

	out->payload_len = 2U;
	out->payload[0] = 0xccU;
	out->payload[1] = 0xddU;
	return 0;
}

int __wrap_mbs_contact_binary_request(const uint8_t *prefix, const uint8_t *payload,
					       size_t payload_len, uint32_t *out_tag)
{
	zassert_not_null(prefix, "prefix must be set");
	zassert_not_null(payload, "payload must be set");
	zassert_true(payload_len <= sizeof(captured_binary_payload), "binary payload too large");

	memcpy(captured_binary_prefix, prefix, sizeof(captured_binary_prefix));
	memcpy(captured_binary_payload, payload, payload_len);
	captured_binary_payload_len = payload_len;
	captured_binary_tag = captured_binary_next_tag++;
	if (out_tag != NULL) {
		*out_tag = captured_binary_tag;
	}

	return 0;
}

int __wrap_mbs_meshcore_channel_data_send(size_t channel_index,
					      const uint8_t *path, uint8_t path_len,
					      uint16_t data_type,
					      const uint8_t *payload,
					      size_t payload_len)
{
	zassert_true(channel_index <= UINT8_MAX, "channel index too large");
	zassert_true(payload != NULL || payload_len == 0U, "payload pointer mismatch");
	zassert_true(payload_len <= sizeof(captured_meshcore_payload),
		     "channel payload too large");

	captured_channel_index = (uint8_t)channel_index;
	captured_meshcore_path_len = path_len;
	memset(captured_meshcore_path, 0, sizeof(captured_meshcore_path));
	if (path != NULL) {
		memcpy(captured_meshcore_path, path,
		       MIN(test_path_len_bytes(path_len), sizeof(captured_meshcore_path)));
	}
	captured_meshcore_data_type = data_type;
	captured_meshcore_payload_len = payload_len;
	if (payload_len > 0U) {
		memcpy(captured_meshcore_payload, payload, payload_len);
	}

	return 0;
}

int __wrap_mbs_meshcore_raw_data_send(const uint8_t *path, uint8_t path_len,
					  const uint8_t *payload, size_t payload_len)
{
	zassert_not_null(path, "path must be set");
	zassert_not_null(payload, "payload must be set");
	zassert_true(payload_len <= sizeof(captured_meshcore_payload), "raw payload too large");

	captured_meshcore_path_len = path_len;
	memset(captured_meshcore_path, 0, sizeof(captured_meshcore_path));
	memcpy(captured_meshcore_path, path,
	       MIN(test_path_len_bytes(path_len), sizeof(captured_meshcore_path)));
	captured_meshcore_payload_len = payload_len;
	memcpy(captured_meshcore_payload, payload, payload_len);

	return 0;
}

int __wrap_mbs_meshcore_control_data_send(const uint8_t *payload,
					      size_t payload_len)
{
	zassert_not_null(payload, "payload must be set");
	zassert_true(payload_len <= sizeof(captured_meshcore_payload),
		     "control payload too large");

	captured_meshcore_payload_len = payload_len;
	memcpy(captured_meshcore_payload, payload, payload_len);

	return 0;
}

int __wrap_mbs_management_smp_request_with_secret(
	const mbs_management_smp_request_event *request,
	const uint8_t *secret, size_t secret_len, uint32_t *out_tag)
{
	zassert_not_null(request, "management request must be set");
	zassert_not_null(secret, "management secret must be set");
	zassert_true(secret_len <= sizeof(captured_management_secret),
		     "management secret too long");

	captured_management_request = *request;
	memcpy(captured_management_secret, secret, secret_len);
	captured_management_secret_len = secret_len;
	captured_management_tag = next_management_tag++;
	if (out_tag != NULL) {
		*out_tag = captured_management_tag;
	}

	return captured_management_rc;
}

int __wrap_mbs_clock_time_set_unix_ms(uint64_t unix_time_ms,
					  uint64_t *applied_unix_time_ms)
{
	captured_set_time_ms = unix_time_ms;
	if (applied_unix_time_ms != NULL) {
		*applied_unix_time_ms = unix_time_ms;
	}
	return 0;
}

static uint32_t get_u32_le(const uint8_t *buf)
{
	return ((uint32_t)buf[0]) | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) |
	       ((uint32_t)buf[3] << 24);
}

static void put_u32_le(uint8_t *buf, uint32_t value)
{
	buf[0] = (uint8_t)value;
	buf[1] = (uint8_t)(value >> 8);
	buf[2] = (uint8_t)(value >> 16);
	buf[3] = (uint8_t)(value >> 24);
}

static int test_mgmt_cbor_data_encode(const void *msg, const pb_msgdesc_t *fields,
				      size_t max_proto_size, uint8_t *out,
				      size_t out_size, size_t *out_len)
{
	uint8_t proto[TEST_MGMT_PROTO_MAX_SIZE];
	pb_ostream_t stream;
	ZCBOR_STATE_E(zse, TEST_MGMT_CBOR_STATES, out, out_size, 0);

	zassert_true(max_proto_size <= sizeof(proto), "proto buffer too small");
	stream = pb_ostream_from_buffer(proto, max_proto_size);
	zassert_true(pb_encode(&stream, fields, msg), "proto encode failed");
	zassert_true(zcbor_map_start_encode(zse, 1), "cbor map start");
	zassert_true(zcbor_tstr_put_lit(zse, "data"), "cbor key");
	zassert_true(zcbor_bstr_encode_ptr(zse, (const char *)proto,
					   stream.bytes_written), "cbor data");
	zassert_true(zcbor_map_end_encode(zse, 1), "cbor map end");
	*out_len = (size_t)(zse->payload - out);
	return 0;
}

static void test_mgmt_response_build(mbs_management_smp_response_event *event,
				     uint32_t tag, uint16_t group, uint8_t command,
				     uint8_t op, const void *msg,
				     const pb_msgdesc_t *fields,
				     size_t max_proto_size)
{
	size_t payload_len = 0U;

	zassert_not_null(event, "event must be set");
	memset(event, 0, sizeof(*event));
	event->tag = tag;
	if (fields != NULL) {
		zassert_ok(test_mgmt_cbor_data_encode(
				   msg, fields, max_proto_size,
				   &event->response[TEST_MGMT_HDR_SIZE],
				   sizeof(event->response) - TEST_MGMT_HDR_SIZE,
				   &payload_len),
			   "response payload encode");
	}
	event->response[0] = op;
	event->response[1] = 0U;
	sys_put_be16((uint16_t)payload_len, &event->response[2]);
	sys_put_be16(group, &event->response[4]);
	event->response[6] = 0U;
	event->response[7] = command;
	event->response_len = TEST_MGMT_HDR_SIZE + payload_len;
}

static int test_mgmt_cbor_data_decode(const uint8_t *payload, size_t payload_len,
				      const pb_msgdesc_t *fields, void *msg,
				      size_t msg_size)
{
	ZCBOR_STATE_D(zsd, TEST_MGMT_CBOR_STATES, payload, payload_len, 1, 0);
	struct zcbor_string key;
	struct zcbor_string data = {0};
	pb_istream_t stream;

	memset(msg, 0, msg_size);
	zassert_true(zcbor_map_start_decode(zsd), "decode map start");
	zassert_true(zcbor_tstr_decode(zsd, &key), "decode key");
	zassert_equal(key.len, 4U, "key len");
	zassert_mem_equal(key.value, "data", 4U, "key value");
	zassert_true(zcbor_bstr_decode(zsd, &data), "decode data");
	zassert_true(zcbor_map_end_decode(zsd), "decode map end");
	stream = pb_istream_from_buffer(data.value, data.len);
	return pb_decode(&stream, fields, msg) && stream.bytes_left == 0U ? 0 : -EINVAL;
}

static size_t test_path_len_bytes(uint8_t path_len)
{
	uint8_t hash_size = (path_len >> 6) + 1U;
	uint8_t hash_count = path_len & 0x3fU;

	if (hash_size > 3U) {
		return 0U;
	}

	return (size_t)hash_size * hash_count;
}

static void reset_adapter(bool connected)
{
	const struct meshcore_companion_transport transport = {
		.send = capture_send,
		.user_data = &captured,
	};

	meshcore_companion_adapter_disconnected();
	meshcore_companion_adapter_flush();
	memset(&captured, 0, sizeof(captured));
	k_sem_reset(&captured_tx_changed);
	memset(captured_node_prefix, 0, sizeof(captured_node_prefix));
	memset(captured_payload, 0, sizeof(captured_payload));
	captured_payload_len = 0U;
	captured_attempt = 0U;
	captured_channel_index = 0U;
	captured_channel_set_index = (size_t)-1;
	memset(captured_channel_set_secret, 0, sizeof(captured_channel_set_secret));
	memset(captured_channel_set_name, 0, sizeof(captured_channel_set_name));
	captured_channel_reset_index = (size_t)-1;
	memset(captured_binary_prefix, 0, sizeof(captured_binary_prefix));
	captured_discover_tag = 0U;
	captured_trace_tag = 0U;
	captured_telemetry_tag = 0U;
	captured_binary_tag = 0U;
	captured_binary_next_tag = 0x55667788U;
	memset(captured_binary_payload, 0, sizeof(captured_binary_payload));
	captured_binary_payload_len = 0U;
	memset(captured_meshcore_path, 0, sizeof(captured_meshcore_path));
	captured_meshcore_path_len = 0U;
	memset(captured_meshcore_trace_path, 0, sizeof(captured_meshcore_trace_path));
	captured_meshcore_trace_path_len = 0U;
	captured_meshcore_trace_path_hash_size = 0U;
	captured_meshcore_trace_tag = 0U;
	captured_meshcore_data_type = 0U;
	memset(captured_meshcore_payload, 0, sizeof(captured_meshcore_payload));
	captured_meshcore_payload_len = 0U;
	captured_flood = false;
	captured_management_request =
		(mbs_management_smp_request_event){0};
	memset(captured_management_secret, 0, sizeof(captured_management_secret));
	captured_management_secret_len = 0U;
	captured_management_tag = 0U;
	captured_management_rc = 0;
	next_management_tag = 0x77889900U;
	captured_contact_update = (mbs_contact)meshbus_Contact_init_zero;
	captured_meshcore_config_set = (mbs_meshcore_config)meshbus_MeshcoreConfig_init_zero;
	captured_meshcore_config_set_count = 0U;
	captured_set_time_ms = 0U;
	mock_message_available = false;
	mock_message = (mbs_message_content)meshbus_MessageContent_init_zero;
	mock_client_repeat = false;
	mock_unknown_zero_path_contact = false;
	capture_fail_code = 0U;
	capture_fail_rc = 0;
	capture_fail_remaining = 0U;
	for (size_t i = 0U; i < sizeof(test_public_key); i++) {
		test_public_key[i] = (uint8_t)(0xa0U + i);
	}

	zassert_ok(meshcore_companion_adapter_init(&transport), "adapter init failed");
	if (connected) {
		meshcore_companion_adapter_connected();
	}
}

static void drain_companion_work_until(size_t expected_count)
{
	k_timepoint_t deadline = sys_timepoint_calc(K_SECONDS(1));

	while (captured.count < expected_count) {
		zassert_ok(k_sem_take(&captured_tx_changed, sys_timepoint_timeout(deadline)),
			   "timed out waiting for frame %u", (unsigned int)expected_count);
	}
}

static void wait_login_management_start(void)
{
	for (size_t i = 0U; i < 16U && captured_management_tag == 0U; i++) {
		k_sleep(K_MSEC(10));
	}
}

static void companion_test_login_success(uint8_t public_key[MBS_CONTACT_PUBLIC_KEY_SIZE])
{
	const uint8_t password[] = "phase10-password";
	uint8_t login[1U + MBS_CONTACT_PUBLIC_KEY_SIZE + sizeof(password)] = {0};
	mbs_management_smp_response_event response = {0};

	login[0] = TEST_CMD_SEND_LOGIN;
	for (size_t i = 0U; i < MBS_CONTACT_PUBLIC_KEY_SIZE; i++) {
		login[1U + i] = 0x10U + i;
	}
	memcpy(&login[1U + MBS_CONTACT_PUBLIC_KEY_SIZE], password,
	       sizeof(password));

	zassert_ok(meshcore_companion_adapter_rx_frame(login, sizeof(login)),
		   "login frame failed");
	wait_login_management_start();
	zassert_not_equal(captured_management_tag, 0U,
			  "login should start management exchange");
	test_mgmt_response_build(
		&response, captured_management_tag,
		meshbus_PowerMgmtGroupId_POWER_MGMT_GROUP_ID_MESHBUS_POWER,
		meshbus_PowerMgmtCommandId_POWER_MGMT_COMMAND_ID_STATUS,
		MGMT_OP_READ_RSP, NULL, NULL, 0U);
	zassert_ok(zbus_chan_pub(&mbs_management_smp_response_chan,
				 &response, K_NO_WAIT));
	drain_companion_work_until(2U);
	zassert_equal(captured.count, 2U, "login should emit sent + success");
	zassert_equal(captured.frame[1][0], TEST_PUSH_CODE_LOGIN_SUCCESS,
		      "login should succeed");
	memcpy(public_key, &login[1], MBS_CONTACT_PUBLIC_KEY_SIZE);
}

static size_t companion_test_cli_frame(uint8_t *frame, size_t frame_size,
				       const uint8_t public_key[MBS_CONTACT_PUBLIC_KEY_SIZE],
				       const char *payload)
{
	size_t payload_len = strlen(payload);
	size_t len = 13U + payload_len;

	zassert_true(frame_size >= len, "CLI frame buffer too small");
	memset(frame, 0, frame_size);
	frame[0] = TEST_CMD_SEND_TXT_MSG;
	frame[1] = TEST_TXT_TYPE_CLI_DATA;
	frame[2] = 1U;
	put_u32_le(&frame[3], 1782636681U);
	memcpy(&frame[7], public_key, TEST_APP_PREFIX_SIZE);
	memcpy(&frame[13], payload, payload_len);
	return len;
}

ZTEST(mbs_meshcore_companion_contract, test_init_rejects_missing_transport)
{
	const struct meshcore_companion_transport missing_send = {
		.send = NULL,
		.user_data = &captured,
	};

	zassert_equal(meshcore_companion_adapter_init(NULL), -EINVAL, "NULL transport accepted");
	zassert_equal(meshcore_companion_adapter_init(&missing_send), -EINVAL,
		      "missing send callback accepted");
}

ZTEST(mbs_meshcore_companion_contract, test_unknown_command_emits_companion_error)
{
	const uint8_t command[] = {0xff};

	reset_adapter(true);

	zassert_ok(meshcore_companion_adapter_rx_frame(command, sizeof(command)),
		   "unknown command should be encoded as a Companion error frame");
	zassert_equal(captured.count, 1U, "unexpected send count");
	zassert_equal(captured.len[0], 2U, "unexpected error frame length");
	zassert_equal(captured.frame[0][0], TEST_RESP_CODE_ERR, "unexpected response code");
	zassert_equal(captured.frame[0][1], TEST_ERR_UNSUPPORTED_CMD, "unexpected error code");
}

ZTEST(mbs_meshcore_companion_contract, test_device_query_response_layout)
{
	const uint8_t command[] = {TEST_CMD_DEVICE_QEURY, 11U};

	reset_adapter(true);

	zassert_ok(meshcore_companion_adapter_rx_frame(command, sizeof(command)),
		   "device query failed");
	zassert_equal(captured.count, 1U, "unexpected send count");
	zassert_equal(captured.len[0], 82U, "unexpected device info length");
	zassert_equal(captured.frame[0][0], TEST_RESP_CODE_DEVICE_INFO, "response code mismatch");
	zassert_equal(captured.frame[0][1], 11U, "firmware protocol version mismatch");
	zassert_equal(captured.frame[0][2], 32U, "contact capacity mismatch");
	zassert_equal(captured.frame[0][3], 8U, "channel capacity mismatch");
	zassert_equal(get_u32_le(&captured.frame[0][4]), 0U, "BLE PIN placeholder mismatch");
	zassert_mem_equal(&captured.frame[0][20], "FoBE", 4U, "manufacturer mismatch");
	zassert_mem_equal(&captured.frame[0][60], "FoBE Zephyr", 11U, "version mismatch");
	zassert_equal(captured.frame[0][80], 0U, "client repeat placeholder mismatch");
	zassert_equal(captured.frame[0][81], 0U, "path hash placeholder mismatch");
}

ZTEST(mbs_meshcore_companion_contract, test_app_start_response_uses_meshcore_config)
{
	const uint8_t command[] = {
		TEST_CMD_APP_START, 0, 0, 0, 0, 0, 0, 0,
		'M', 'e', 's', 'h', 'C', 'o', 'r', 'e',
	};
	const uint8_t expected_telemetry = (0U << 4) | (2U << 2) | 1U;
	size_t idx = 0U;

	reset_adapter(true);

	zassert_ok(meshcore_companion_adapter_rx_frame(command, sizeof(command)),
		   "app start failed");
	zassert_equal(captured.count, 1U, "unexpected send count");
	zassert_equal(captured.frame[0][idx++], TEST_RESP_CODE_SELF_INFO, "response code mismatch");
	zassert_equal(captured.frame[0][idx++], MBS_CONTACT_ROLE_CHAT, "role mismatch");
	zassert_equal(captured.frame[0][idx++], 0U, "tx power placeholder mismatch");
	zassert_equal(captured.frame[0][idx++], 22U, "max tx power mismatch");
	zassert_mem_equal(&captured.frame[0][idx], test_public_key, sizeof(test_public_key),
			  "public key mismatch");
	idx += sizeof(test_public_key);
	zassert_equal((int32_t)get_u32_le(&captured.frame[0][idx]), -1234567,
		      "latitude mismatch");
	idx += 4U;
	zassert_equal(get_u32_le(&captured.frame[0][idx]), 7654321U, "longitude mismatch");
	idx += 4U;
	zassert_equal(captured.frame[0][idx++], 3U, "multi-acks mismatch");
	zassert_equal(captured.frame[0][idx++], 1U, "advert loc policy mismatch");
	zassert_equal(captured.frame[0][idx++], expected_telemetry, "telemetry flags mismatch");
	zassert_equal(captured.frame[0][idx++], 1U, "manual add flag mismatch");
	zassert_equal(get_u32_le(&captured.frame[0][idx]), 0U, "freq placeholder mismatch");
	idx += 4U;
	zassert_equal(get_u32_le(&captured.frame[0][idx]), 0U, "bandwidth placeholder mismatch");
	idx += 4U;
	zassert_equal(captured.frame[0][idx++], 0U, "spread factor placeholder mismatch");
	zassert_equal(captured.frame[0][idx++], 0U, "coding rate placeholder mismatch");
	zassert_mem_equal(&captured.frame[0][idx], "phase2-meshcore", 15U, "meshcore name mismatch");
	zassert_equal(captured.len[0], idx + 15U, "unexpected app start length");
}

ZTEST(mbs_meshcore_companion_contract, test_time_and_battery_response_shapes)
{
	const uint8_t get_time[] = {TEST_CMD_GET_DEVICE_TIME};
	const uint8_t get_batt[] = {TEST_CMD_GET_BATT_AND_STORAGE};
	const struct timespec target = {
		.tv_sec = 1775000123,
		.tv_nsec = 0,
	};

	reset_adapter(true);
	zassert_ok(sys_clock_settime(SYS_CLOCK_REALTIME, &target), "clock set failed");

	zassert_ok(meshcore_companion_adapter_rx_frame(get_time, sizeof(get_time)),
		   "get time failed");
	zassert_ok(meshcore_companion_adapter_rx_frame(get_batt, sizeof(get_batt)),
		   "get battery failed");
	zassert_equal(captured.count, 2U, "unexpected send count");
	zassert_equal(captured.len[0], 5U, "time response length mismatch");
	zassert_equal(captured.frame[0][0], TEST_RESP_CODE_CURR_TIME, "time response code mismatch");
	zassert_true(get_u32_le(&captured.frame[0][1]) >= 1775000123U,
		     "time response should use realtime seconds");
	zassert_equal(captured.len[1], 11U, "battery response length mismatch");
	zassert_equal(captured.frame[1][0], TEST_RESP_CODE_BATT_AND_STORAGE,
		      "battery response code mismatch");
	zassert_equal(captured.frame[1][1], 0U, "battery placeholder low byte mismatch");
	zassert_equal(captured.frame[1][2], 0U, "battery placeholder high byte mismatch");
	zassert_equal(get_u32_le(&captured.frame[1][3]), 0U, "storage used placeholder mismatch");
	zassert_equal(get_u32_le(&captured.frame[1][7]), 0U, "storage total placeholder mismatch");
}

ZTEST(mbs_meshcore_companion_contract, test_set_time_uses_app_uint32_seconds)
{
	const uint8_t command[] = {TEST_CMD_SET_DEVICE_TIME, 0x11U, 0x11U, 0x11U, 0x71U};

	reset_adapter(true);

	zassert_ok(meshcore_companion_adapter_rx_frame(command, sizeof(command)),
		   "set time should emit OK");
	zassert_equal(captured.count, 1U, "unexpected send count");
	zassert_equal(captured.len[0], 1U, "unexpected OK length");
	zassert_equal(captured.frame[0][0], TEST_RESP_CODE_OK, "unexpected response code");
	zassert_equal(captured_set_time_ms, 0x71111111ULL * 1000ULL, "set time ms mismatch");
}

ZTEST(mbs_meshcore_companion_contract, test_short_handshake_frames_return_illegal_arg)
{
	const uint8_t device_query[] = {TEST_CMD_DEVICE_QEURY};
	const uint8_t app_start[] = {TEST_CMD_APP_START, 0, 0, 0, 0, 0, 0};
	const uint8_t set_time[] = {TEST_CMD_SET_DEVICE_TIME, 1U, 2U, 3U};

	reset_adapter(true);

	zassert_ok(meshcore_companion_adapter_rx_frame(device_query, sizeof(device_query)),
		   "short device query should emit error frame");
	zassert_ok(meshcore_companion_adapter_rx_frame(app_start, sizeof(app_start)),
		   "short app start should emit error frame");
	zassert_ok(meshcore_companion_adapter_rx_frame(set_time, sizeof(set_time)),
		   "short set time should emit error frame");
	zassert_equal(captured.count, 3U, "unexpected send count");
	for (size_t i = 0U; i < captured.count; i++) {
		zassert_equal(captured.len[i], 2U, "unexpected error length");
		zassert_equal(captured.frame[i][0], TEST_RESP_CODE_ERR, "unexpected response code");
		zassert_equal(captured.frame[i][1], TEST_ERR_ILLEGAL_ARG, "unexpected error code");
	}
}

ZTEST(mbs_meshcore_companion_contract, test_app_source_minimum_golden_frames)
{
	const uint8_t device_query[] = {TEST_CMD_DEVICE_QEURY, 0x03U};
	const uint8_t app_start[] = {
		TEST_CMD_APP_START, 0x03U, ' ', ' ', ' ', ' ', ' ', ' ',
		'M', 'C', 'o', 'r', 'e',
	};
	const uint8_t get_time[] = {TEST_CMD_GET_DEVICE_TIME};
	const uint8_t set_time[] = {TEST_CMD_SET_DEVICE_TIME, 0x11U, 0x11U, 0x11U, 0x71U};
	const uint8_t get_battery[] = {TEST_CMD_GET_BATT_AND_STORAGE};

	reset_adapter(true);

	zassert_ok(meshcore_companion_adapter_rx_frame(device_query, sizeof(device_query)));
	zassert_ok(meshcore_companion_adapter_rx_frame(app_start, sizeof(app_start)));
	zassert_ok(meshcore_companion_adapter_rx_frame(get_time, sizeof(get_time)));
	zassert_ok(meshcore_companion_adapter_rx_frame(set_time, sizeof(set_time)));
	zassert_ok(meshcore_companion_adapter_rx_frame(get_battery, sizeof(get_battery)));

	zassert_equal(captured.count, 5U, "unexpected response count");
	zassert_equal(captured.frame[0][0], TEST_RESP_CODE_DEVICE_INFO);
	zassert_equal(captured.len[0], 82U, "DeviceInfo must satisfy App v11 parser");
	zassert_equal(captured.frame[1][0], TEST_RESP_CODE_SELF_INFO);
	zassert_true(captured.len[1] >= 58U, "SelfInfo must satisfy App parser minimum");
	zassert_equal(captured.frame[2][0], TEST_RESP_CODE_CURR_TIME);
	zassert_equal(captured.len[2], 5U, "CurrentTime frame length mismatch");
	zassert_equal(captured.frame[3][0], TEST_RESP_CODE_OK);
	zassert_equal(captured.frame[4][0], TEST_RESP_CODE_BATT_AND_STORAGE);
	zassert_equal(captured.len[4], 11U, "Battery frame should include storage extension");
}

ZTEST(mbs_meshcore_companion_contract, test_device_query_reports_client_repeat_and_repeat_freqs)
{
	const uint8_t device_query[] = {TEST_CMD_DEVICE_QEURY, 0x03U};
	const uint8_t get_repeat_freq[] = {TEST_CMD_GET_REPEAT_FREQ};

	reset_adapter(true);
	mock_client_repeat = true;

	zassert_ok(meshcore_companion_adapter_rx_frame(device_query, sizeof(device_query)));
	zassert_ok(meshcore_companion_adapter_rx_frame(get_repeat_freq, sizeof(get_repeat_freq)));

	zassert_equal(captured.count, 2U, "unexpected response count");
	zassert_equal(captured.frame[0][0], TEST_RESP_CODE_DEVICE_INFO);
	zassert_equal(captured.len[0], 82U, "DeviceInfo v11 layout mismatch");
	zassert_equal(captured.frame[0][80], 1U, "client_repeat byte mismatch");
	zassert_equal(captured.frame[0][81], 0U, "path_hash_mode byte mismatch");

	zassert_equal(captured.frame[1][0], TEST_RESP_CODE_REPEAT_FREQ);
	zassert_equal(captured.len[1], 25U, "repeat frequency frame length mismatch");
	zassert_equal(get_u32_le(&captured.frame[1][1]), 433000U);
	zassert_equal(get_u32_le(&captured.frame[1][5]), 433000U);
	zassert_equal(get_u32_le(&captured.frame[1][9]), 869000U);
	zassert_equal(get_u32_le(&captured.frame[1][13]), 869000U);
	zassert_equal(get_u32_le(&captured.frame[1][17]), 918000U);
	zassert_equal(get_u32_le(&captured.frame[1][21]), 918000U);
}

ZTEST(mbs_meshcore_companion_contract, test_contacts_and_contact_lookup_use_app_parser_layout)
{
	const uint8_t get_contacts[] = {TEST_CMD_GET_CONTACTS};
	const uint8_t get_contact[] = {
		TEST_CMD_GET_CONTACT_BY_KEY,
		0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U,
		0x18U, 0x19U, 0x1aU, 0x1bU, 0x1cU, 0x1dU, 0x1eU, 0x1fU,
		0x20U, 0x21U, 0x22U, 0x23U, 0x24U, 0x25U, 0x26U, 0x27U,
		0x28U, 0x29U, 0x2aU, 0x2bU, 0x2cU, 0x2dU, 0x2eU, 0x2fU,
	};
	uint8_t app_update[147] = {TEST_CMD_ADD_UPDATE_CONTACT};
	uint8_t reset_path[1U + 32U] = {TEST_CMD_RESET_PATH};
	size_t idx;

	reset_adapter(true);

	zassert_ok(meshcore_companion_adapter_rx_frame(get_contacts, sizeof(get_contacts)));
	zassert_equal(captured.count, 1U, "contacts should send start synchronously");
	drain_companion_work_until(4U);
	zassert_equal(captured.count, 4U, "contacts should send start, two contacts, end");
	zassert_equal(captured.frame[0][0], TEST_RESP_CODE_CONTACTS_START);
	zassert_equal(captured.len[0], 5U, "contacts start length mismatch");
	zassert_equal(get_u32_le(&captured.frame[0][1]), 2U, "contacts count mismatch");
	zassert_equal(captured.frame[1][0], TEST_RESP_CODE_CONTACT);
	zassert_equal(captured.len[1], TEST_CONTACT_FRAME_SIZE, "contact frame length mismatch");
	zassert_equal(captured.frame[1][35], 0U, "neighbor contact path length mismatch");
	zassert_equal(captured.frame[3][0], TEST_RESP_CODE_END_OF_CONTACTS);
	zassert_equal(captured.len[3], 5U, "contacts end length mismatch");
	zassert_equal(get_u32_le(&captured.frame[3][1]), 1775000002U,
		      "contacts end last-modified mismatch");

	reset_adapter(true);
	mock_unknown_zero_path_contact = true;
	zassert_ok(meshcore_companion_adapter_rx_frame(get_contacts, sizeof(get_contacts)));
	drain_companion_work_until(4U);
	zassert_equal(captured.frame[1][35], 0xffU,
		      "unknown zero path contact should export as flood route");

	reset_adapter(true);
	zassert_ok(meshcore_companion_adapter_rx_frame(get_contact, sizeof(get_contact)));
	zassert_equal(captured.count, 1U, "contact lookup send count mismatch");
	zassert_equal(captured.frame[0][0], TEST_RESP_CODE_CONTACT);
	zassert_equal(captured.len[0], TEST_CONTACT_FRAME_SIZE, "contact lookup length mismatch");
	zassert_mem_equal(&captured.frame[0][1], &get_contact[1], 32U, "contact key mismatch");

	idx = 1U;
	for (size_t i = 0U; i < 32U; i++) {
		app_update[idx++] = (uint8_t)(0x10U + i);
	}
	app_update[idx++] = MBS_CONTACT_ROLE_SENSOR;
	app_update[idx++] = 0x5aU;
	app_update[idx++] = 0xffU;
	idx += 64U;
	memcpy(&app_update[idx], "app contact", 11U);
	idx += 32U;
	put_u32_le(&app_update[idx], 1775000300U);
	idx += 4U;
	put_u32_le(&app_update[idx], (uint32_t)(int32_t)-1000);
	idx += 4U;
	put_u32_le(&app_update[idx], 2000U);

	reset_adapter(true);
	zassert_ok(meshcore_companion_adapter_rx_frame(app_update, sizeof(app_update)));
	meshcore_companion_adapter_flush();
	zassert_equal(captured.count, 1U, "contact update response count mismatch");
	zassert_equal(captured.frame[0][0], TEST_RESP_CODE_OK);
	zassert_equal(captured_contact_update.role, MBS_CONTACT_ROLE_SENSOR, "updated role mismatch");
	zassert_equal(captured_contact_update.flags, 0x0eU, "updated flags mismatch");
	zassert_mem_equal(captured_contact_update.public_key.bytes, &app_update[1], 32U,
			  "updated key mismatch");
	zassert_equal(captured_contact_update.latitude, -1000, "updated latitude mismatch");
	zassert_equal(captured_contact_update.longitude, 2000, "updated longitude mismatch");
	zassert_equal(captured_contact_update.last_seen_timestamp, 1775000300U,
		      "updated timestamp mismatch");
	zassert_equal(captured_contact_update.path_hash_size, 1U,
		      "unknown path should preserve existing path hash size");
	zassert_true(captured_contact_update.is_neighbor,
		     "unknown path should preserve existing route metadata");

	memcpy(&reset_path[1], &app_update[1], 32U);
	zassert_ok(meshcore_companion_adapter_rx_frame(reset_path, sizeof(reset_path)));
	zassert_equal(captured.frame[1][0], TEST_RESP_CODE_OK, "reset path should OK");
	zassert_equal(captured_contact_update.out_path.size, 0U, "reset path should clear path");
	zassert_false(captured_contact_update.is_neighbor, "reset path should force flood route");
	zassert_equal(captured_contact_update.path_hash_size, 1U, "reset path hash size mismatch");
}

ZTEST(mbs_meshcore_companion_contract, test_contacts_retry_backpressure_and_busy_request)
{
	const uint8_t get_contacts[] = {TEST_CMD_GET_CONTACTS};

	reset_adapter(true);
	capture_fail_code = TEST_RESP_CODE_CONTACT;
	capture_fail_rc = -ENOSPC;
	capture_fail_remaining = 1U;

	zassert_ok(meshcore_companion_adapter_rx_frame(get_contacts, sizeof(get_contacts)));
	zassert_equal(captured.count, 1U, "contacts start should be sent");
	zassert_equal(captured.frame[0][0], TEST_RESP_CODE_CONTACTS_START);

	zassert_ok(meshcore_companion_adapter_rx_frame(get_contacts, sizeof(get_contacts)));
	zassert_equal(captured.count, 2U, "busy request should emit error frame");
	zassert_equal(captured.frame[1][0], TEST_RESP_CODE_ERR, "busy response code");
	zassert_equal(captured.frame[1][1], TEST_ERR_BAD_STATE, "busy error code");

	drain_companion_work_until(5U);
	zassert_equal(captured.count, 5U, "retry sync should finish after busy error");
	zassert_equal(captured.frame[2][0], TEST_RESP_CODE_CONTACT,
		      "first contact should retry after backpressure");
	zassert_equal(captured.frame[3][0], TEST_RESP_CODE_CONTACT,
		      "second contact should follow retried contact");
	zassert_equal(captured.frame[4][0], TEST_RESP_CODE_END_OF_CONTACTS,
		      "contacts end should be sent after retry");
	zassert_equal(get_u32_le(&captured.frame[4][1]), 1775000002U,
		      "contacts end last-modified mismatch");
}

ZTEST(mbs_meshcore_companion_contract, test_app_config_commands_update_meshcore_config)
{
	const uint8_t set_name[] = {
		TEST_CMD_SET_NAME, 'f', 'o', 'b', 'e',
	};
	const uint8_t set_coords[] = {
		TEST_CMD_SET_COORDINATES,
		0x40U, 0x42U, 0x0fU, 0x00U,
		0x80U, 0x84U, 0x1eU, 0x00U,
		0U, 0U, 0U, 0U,
	};
	const uint8_t set_other[] = {
		TEST_CMD_SET_OTHER_PARAMS,
		1U,
		(uint8_t)((3U << 4) | (2U << 2) | 1U),
		1U,
		5U,
	};
	const uint8_t set_path_hash[] = {
		TEST_CMD_SET_PATH_HASH_MODE, 0U, 2U,
	};
	const uint8_t set_autoadd[] = {
		TEST_CMD_SET_AUTOADD_CONFIG,
		MBS_MESHCORE_CONTACT_ADD_FILTER_OVERWRITE_OLDEST |
			MBS_MESHCORE_CONTACT_ADD_FILTER_CHAT |
			MBS_MESHCORE_CONTACT_ADD_FILTER_REPEATER,
		3U,
	};
	const uint8_t get_autoadd[] = {TEST_CMD_GET_AUTOADD_CONFIG};
	const uint8_t set_tuning[] = {
		TEST_CMD_SET_TUNING,
		1U, 0U, 0U, 0U,
		2U, 0U, 0U, 0U,
		0U, 0U,
	};
	const uint8_t set_pin[] = {
		TEST_CMD_SET_DEVICE_PIN,
		0x40U, 0x42U, 0x0fU, 0x00U,
	};

	reset_adapter(true);

	zassert_ok(meshcore_companion_adapter_rx_frame(set_name, sizeof(set_name)));
	zassert_equal(captured.frame[0][0], TEST_RESP_CODE_OK);
	zassert_equal(captured_meshcore_config_set_count, 1U);
	zassert_mem_equal(captured_meshcore_config_set.name, "fobe", 4U, "name mismatch");

	zassert_ok(meshcore_companion_adapter_rx_frame(set_coords, sizeof(set_coords)));
	zassert_equal(captured.frame[1][0], TEST_RESP_CODE_OK);
	zassert_equal(captured_meshcore_config_set.latitude, 1000000, "latitude mismatch");
	zassert_equal(captured_meshcore_config_set.longitude, 2000000, "longitude mismatch");

	zassert_ok(meshcore_companion_adapter_rx_frame(set_other, sizeof(set_other)));
	zassert_equal(captured.frame[2][0], TEST_RESP_CODE_OK);
	zassert_true((captured_meshcore_config_set.add_contact_config &
		      MBS_MESHCORE_CONTACT_ADD_FILTER_MANUAL_MODE) != 0U);
	zassert_equal(captured_meshcore_config_set.telemetry_mode_base, 1U);
	zassert_equal(captured_meshcore_config_set.telemetry_mode_locat, 2U);
	zassert_equal(captured_meshcore_config_set.telemetry_mode_environment, 3U);
	zassert_true(captured_meshcore_config_set.advert_position);
	zassert_equal(captured_meshcore_config_set.multi_acks, 5U);

	zassert_ok(meshcore_companion_adapter_rx_frame(set_path_hash, sizeof(set_path_hash)));
	zassert_equal(captured.frame[3][0], TEST_RESP_CODE_OK);
	zassert_equal(captured_meshcore_config_set.path_hash_size, 3U);

	zassert_ok(meshcore_companion_adapter_rx_frame(set_autoadd, sizeof(set_autoadd)));
	zassert_equal(captured.frame[4][0], TEST_RESP_CODE_OK);
	zassert_equal(captured_meshcore_config_set.add_contact_hops_limit, 3U);
	zassert_equal(captured_meshcore_config_set.add_contact_config &
			      (MBS_MESHCORE_CONTACT_ADD_FILTER_OVERWRITE_OLDEST |
			       MBS_MESHCORE_CONTACT_ADD_FILTER_CHAT |
			       MBS_MESHCORE_CONTACT_ADD_FILTER_REPEATER),
		      set_autoadd[1]);

	zassert_ok(meshcore_companion_adapter_rx_frame(get_autoadd, sizeof(get_autoadd)));
	zassert_equal(captured.frame[5][0], TEST_RESP_CODE_AUTOADD_CONFIG);
	zassert_equal(captured.frame[5][1], 0x03U, "auto-add bitmask mismatch");

	zassert_ok(meshcore_companion_adapter_rx_frame(set_tuning, sizeof(set_tuning)));
	zassert_equal(captured.frame[6][0], TEST_RESP_CODE_OK);

	zassert_ok(meshcore_companion_adapter_rx_frame(set_pin, sizeof(set_pin)));
	zassert_equal(captured.frame[7][0], TEST_RESP_CODE_OK);
}

ZTEST(mbs_meshcore_companion_contract, test_channel_get_set_and_message_send_commands)
{
	const uint8_t get_empty_channel[] = {TEST_CMD_GET_CHANNEL, 1U};
	const uint8_t get_channel[] = {TEST_CMD_GET_CHANNEL, 2U};
	const uint8_t send_node[] = {
		TEST_CMD_SEND_TXT_MSG, 0U, 0U, 0, 0, 0, 0,
		0xa0U, 0xa1U, 0xa2U, 0xa3U, 0xa4U, 0xa5U,
		'h', 'e', 'l', 'l', 'o',
	};
	const uint8_t send_channel[] = {
		TEST_CMD_SEND_CHANNEL_TXT_MSG, 0U, 2U, 0, 0, 0, 0,
		'r', 'o', 'o', 'm',
	};
	uint8_t set_channel[1U + 1U + 32U + 16U] = {TEST_CMD_SET_CHANNEL, 2U};
	uint8_t clear_channel[1U + 1U + 32U + 16U] = {TEST_CMD_SET_CHANNEL, 2U};

	memcpy(&set_channel[2], "ops-new", 7U);
	for (size_t i = 0; i < 16U; i++) {
		set_channel[34U + i] = 0xc0U + i;
	}

	reset_adapter(true);

	zassert_ok(meshcore_companion_adapter_rx_frame(get_empty_channel, sizeof(get_empty_channel)));
	zassert_equal(captured.frame[0][0], TEST_RESP_CODE_CHANNEL_INFO);
	zassert_equal(captured.len[0], TEST_CHANNEL_INFO_SIZE);
	zassert_equal(captured.frame[0][1], 1U, "empty channel index mismatch");
	zassert_mem_equal(&captured.frame[0][2], (uint8_t[32]){0}, 32U,
			  "empty channel name should be zeroed");
	zassert_mem_equal(&captured.frame[0][34], (uint8_t[16]){0}, 16U,
			  "empty channel secret should be zeroed");

	zassert_ok(meshcore_companion_adapter_rx_frame(get_channel, sizeof(get_channel)));
	zassert_equal(captured.frame[1][0], TEST_RESP_CODE_CHANNEL_INFO);
	zassert_equal(captured.len[1], TEST_CHANNEL_INFO_SIZE);
	zassert_equal(captured.frame[1][1], 2U, "channel index mismatch");
	zassert_mem_equal(&captured.frame[1][2], "ops", 3U, "channel name mismatch");
	zassert_equal(captured.frame[1][34], 0xb0U, "channel secret mismatch");

	zassert_ok(meshcore_companion_adapter_rx_frame(set_channel, sizeof(set_channel)));
	zassert_equal(captured.frame[2][0], TEST_RESP_CODE_OK, "set channel should OK");
	zassert_equal(captured_channel_set_index, 2U, "set channel index mismatch");
	zassert_mem_equal(captured_channel_set_secret, &set_channel[34],
			  sizeof(captured_channel_set_secret), "set channel secret mismatch");
	zassert_true(strcmp(captured_channel_set_name, "ops-new") == 0,
		     "set channel name mismatch: %s", captured_channel_set_name);

	zassert_ok(meshcore_companion_adapter_rx_frame(send_node, sizeof(send_node)));
	zassert_mem_equal(captured_node_prefix, &send_node[7], sizeof(captured_node_prefix),
			  "node prefix mismatch");
	zassert_equal(captured_attempt, 0U, "attempt mismatch");
	zassert_mem_equal(captured_payload, "hello", 5U, "node payload mismatch");
	zassert_equal(captured.frame[3][0], TEST_RESP_CODE_SENT, "send node response mismatch");
	zassert_equal(get_u32_le(&captured.frame[3][2]), 0x11223344U, "ack token mismatch");

	zassert_ok(meshcore_companion_adapter_rx_frame(send_channel, sizeof(send_channel)));
	zassert_equal(captured_channel_index, 2U, "channel send index mismatch");
	zassert_mem_equal(captured_payload, "room", 4U, "channel payload mismatch");
	zassert_equal(captured.frame[4][0], TEST_RESP_CODE_OK, "send channel should OK");

	zassert_ok(meshcore_companion_adapter_rx_frame(clear_channel, sizeof(clear_channel)));
	zassert_equal(captured.frame[5][0], TEST_RESP_CODE_OK, "clear channel should OK");
	zassert_equal(captured_channel_reset_index, 2U, "clear channel index mismatch");
}

ZTEST(mbs_meshcore_companion_contract, test_sync_next_message_formats_v3_frames)
{
	const uint8_t sync[] = {TEST_CMD_SYNC_NEXT_MESSAGE};

	reset_adapter(true);

	mock_message_available = true;
	mock_message = (mbs_message_content)meshbus_MessageContent_init_zero;
	mock_message.type = meshbus_MessageContent_MessageType_RECEIVE_NODE;
	mock_message.route = meshbus_MessageContent_MessageRoute_ROUTE_DIRECT;
	mock_message.target.size = 4U;
	mock_message.target.bytes[0] = 0x31U;
	mock_message.target.bytes[1] = 0x32U;
	mock_message.target.bytes[2] = 0x33U;
	mock_message.target.bytes[3] = 0x34U;
	mock_message.payload.size = 4U;
	memcpy(mock_message.payload.bytes, "ping", 4U);
	mock_message.sender_timestamp = 1775000200U;
	mock_message.has_rx_snr = true;
	mock_message.rx_snr = 2.0f;

	zassert_ok(meshcore_companion_adapter_rx_frame(sync, sizeof(sync)));
	zassert_equal(captured.frame[0][0], TEST_RESP_CODE_CONTACT_MSG_V3);
	zassert_equal(captured.frame[0][1], 8U, "SNR Q4 mismatch");
	zassert_mem_equal(&captured.frame[0][4], mock_message.target.bytes, 4U,
			  "message target prefix mismatch");
	zassert_equal(get_u32_le(&captured.frame[0][12]), 1775000200U,
		      "message timestamp mismatch");
	zassert_mem_equal(&captured.frame[0][16], "ping", 4U, "message payload mismatch");

	zassert_ok(meshcore_companion_adapter_rx_frame(sync, sizeof(sync)));
	zassert_equal(captured.frame[1][0], TEST_RESP_CODE_NO_MORE_MESSAGES);
	zassert_equal(captured.len[1], 1U, "no-more frame length mismatch");

	mock_message_available = true;
	mock_message = (mbs_message_content)meshbus_MessageContent_init_zero;
	mock_message.type = meshbus_MessageContent_MessageType_RECEIVE_NODE;
	mock_message.route = meshbus_MessageContent_MessageRoute_ROUTE_DIRECT;
	mock_message.target.size = 4U;
	mock_message.target.bytes[0] = 0x31U;
	mock_message.target.bytes[1] = 0x32U;
	mock_message.target.bytes[2] = 0x33U;
	mock_message.target.bytes[3] = 0x34U;
	mock_message.payload.size = sizeof("01|OK") - 1U;
	memcpy(mock_message.payload.bytes, "01|OK", mock_message.payload.size);
	mock_message.sender_timestamp = 1775000201U;

	zassert_ok(meshcore_companion_adapter_rx_frame(sync, sizeof(sync)));
	zassert_equal(captured.frame[2][0], TEST_RESP_CODE_CONTACT_MSG_V3);
	zassert_equal(captured.frame[2][11], TEST_TXT_TYPE_PLAIN,
		      "message queue payload should remain plain text");
	zassert_equal(get_u32_le(&captured.frame[2][12]), 1775000201U,
		      "plain message timestamp mismatch");
	zassert_mem_equal(&captured.frame[2][16], "01|OK", sizeof("01|OK") - 1U,
			  "plain message payload mismatch");

	mock_message_available = true;
	mock_message = (mbs_message_content)meshbus_MessageContent_init_zero;
	mock_message.type = meshbus_MessageContent_MessageType_RECEIVE_CHANNEL;
	mock_message.route = meshbus_MessageContent_MessageRoute_ROUTE_FLOOD;
	mock_message.target.size = 4U;
	mock_message.target.bytes[0] = 0xb0U;
	mock_message.target.bytes[1] = 0xb1U;
	mock_message.target.bytes[2] = 0xb2U;
	mock_message.target.bytes[3] = 0xb3U;
	mock_message.payload.size = 4U;
	memcpy(mock_message.payload.bytes, "room", 4U);
	(void)snprintk(mock_message.sender_name, sizeof(mock_message.sender_name), "sender");
	mock_message.sender_timestamp = 1775000300U;
	mock_message.has_rx_snr = true;
	mock_message.rx_snr = 1.0f;

	zassert_ok(meshcore_companion_adapter_rx_frame(sync, sizeof(sync)));
	zassert_equal(captured.frame[3][0], TEST_RESP_CODE_CHANNEL_MSG_V3);
	zassert_equal(captured.frame[3][1], 4U, "channel SNR Q4 mismatch");
	zassert_equal(captured.frame[3][4], 2U, "channel index mismatch");
	zassert_equal(get_u32_le(&captured.frame[3][7]), 1775000300U,
		      "channel timestamp mismatch");
	zassert_mem_equal(&captured.frame[3][11], "sender: room", 12U,
			  "channel payload mismatch");
}

ZTEST(mbs_meshcore_companion_contract, test_mesh_request_commands_and_push_frames)
{
	const uint8_t self_advert[] = {TEST_CMD_SEND_SELF_ADVERT, 1U};
	const uint8_t path_discovery[] = {
		TEST_CMD_SEND_PATH_DISCOVERY, 1U,
		0x41U, 0x42U, 0x43U, 0x44U, 0x45U, 0x46U, 0x47U, 0x48U,
		0x49U, 0x4aU, 0x4bU, 0x4cU, 0x4dU, 0x4eU, 0x4fU, 0x50U,
		0x51U, 0x52U, 0x53U, 0x54U, 0x55U, 0x56U, 0x57U, 0x58U,
		0x59U, 0x5aU, 0x5bU, 0x5cU, 0x5dU, 0x5eU, 0x5fU, 0x60U,
	};
	const uint8_t trace[] = {
		TEST_CMD_SEND_TRACE_PATH,
		0x41U, 0x42U, 0x43U, 0x44U, 0x45U, 0x46U,
	};
	const uint8_t telemetry[] = {
		TEST_CMD_SEND_TELEMETRY_REQ, 0U, 0U, 0U,
		0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U,
		0x16U, 0x17U, 0x18U, 0x19U, 0x1aU, 0x1bU, 0x1cU, 0x1dU,
		0x1eU, 0x1fU, 0x20U, 0x21U, 0x22U, 0x23U, 0x24U, 0x25U,
		0x26U, 0x27U, 0x28U, 0x29U, 0x2aU, 0x2bU, 0x2cU, 0x2dU,
		0x2eU, 0x2fU,
	};
	const uint8_t self_telemetry[] = {
		TEST_CMD_SEND_TELEMETRY_REQ, 0U, 0U, 0U,
	};
	struct mbs_message_response_event received = {0};
	struct mbs_contact_response_path_event path = {0};
	struct mbs_contact_response_trace_path_event trace_resp = {0};
	struct mbs_contact_response_telemetry_event tel = {0};

	reset_adapter(true);

	zassert_ok(meshcore_companion_adapter_rx_frame(self_advert, sizeof(self_advert)));
	zassert_true(captured_flood, "self advert flood flag mismatch");
	zassert_equal(captured.frame[0][0], TEST_RESP_CODE_OK);

	zassert_ok(meshcore_companion_adapter_rx_frame(path_discovery, sizeof(path_discovery)));
	zassert_mem_equal(captured_node_prefix, &path_discovery[2], sizeof(captured_node_prefix),
			  "path discovery prefix mismatch");
	zassert_equal(captured.frame[1][0], TEST_RESP_CODE_SENT);
	zassert_equal(captured.frame[1][1], 1U, "path discovery route flag mismatch");
	zassert_equal(get_u32_le(&captured.frame[1][2]), captured_discover_tag,
		      "path discovery tag mismatch");
	zassert_equal(get_u32_le(&captured.frame[1][6]), 0U,
		      "path discovery timeout mismatch");

	zassert_ok(meshcore_companion_adapter_rx_frame(trace, sizeof(trace)));
	zassert_mem_equal(captured_node_prefix, &trace[1], sizeof(captured_node_prefix),
			  "trace prefix mismatch");
	zassert_equal(captured.frame[2][0], TEST_RESP_CODE_SENT);
	zassert_equal(captured.frame[2][1], 0U, "trace route flag mismatch");
	zassert_equal(get_u32_le(&captured.frame[2][2]), captured_trace_tag,
		      "trace tag mismatch");
	zassert_equal(get_u32_le(&captured.frame[2][6]), 0U,
		      "trace timeout mismatch");

	zassert_ok(meshcore_companion_adapter_rx_frame(telemetry, sizeof(telemetry)));
	meshcore_companion_adapter_flush();
	zassert_mem_equal(captured_node_prefix, &telemetry[4], sizeof(captured_node_prefix),
			  "telemetry prefix mismatch");
	zassert_equal(captured.frame[3][0], TEST_RESP_CODE_SENT);
	zassert_equal(captured.frame[3][1], 0U, "telemetry route flag mismatch");
	zassert_equal(get_u32_le(&captured.frame[3][2]), captured_telemetry_tag,
		      "telemetry tag mismatch");
	zassert_equal(get_u32_le(&captured.frame[3][6]), 0U,
		      "telemetry timeout mismatch");

	zassert_ok(meshcore_companion_adapter_rx_frame(self_telemetry, sizeof(self_telemetry)));
	meshcore_companion_adapter_flush();
	zassert_equal(captured.frame[4][0], TEST_PUSH_CODE_TELEMETRY_RESPONSE,
		      "self telemetry push code mismatch");
	zassert_mem_equal(&captured.frame[4][2], test_public_key, TEST_APP_PREFIX_SIZE,
			  "self telemetry key prefix mismatch");
	zassert_equal(captured.frame[4][2 + TEST_APP_PREFIX_SIZE], 0xccU,
		      "self telemetry payload byte 0 mismatch");
	zassert_equal(captured.frame[4][3 + TEST_APP_PREFIX_SIZE], 0xddU,
		      "self telemetry payload byte 1 mismatch");

	path.is_discover = true;
	memcpy(path.key_prefix, path_discovery + 2, sizeof(path.key_prefix));
	path.tag = captured_discover_tag;
	path.has_out_path = true;
	path.out_path_len_field = 1U;
	path.out_path_len = 1U;
	path.out_path[0] = 0x7aU;
	path.in_path_len_field = 1U;
	path.in_path_len = 1U;
	path.in_path[0] = 0x8bU;
	zassert_ok(zbus_chan_pub(&mbs_contact_path_response_chan, &path, K_NO_WAIT));

	trace_resp.state = 1U;
	memcpy(trace_resp.key_prefix, trace + 1, sizeof(trace_resp.key_prefix));
	trace_resp.tag = captured_trace_tag;
	trace_resp.timestamp = 1775000400U;
	trace_resp.out_path_snr_count = 2U;
	trace_resp.out_path_snr[0] = 3;
	trace_resp.out_path_snr[1] = 4;
	zassert_ok(zbus_chan_pub(&mbs_contact_trace_path_response_chan, &trace_resp, K_NO_WAIT));

	meshcore_companion_adapter_flush();

	zassert_equal(captured.frame[5][0], TEST_PUSH_CODE_PATH_DISCOVERY,
		      "path push code mismatch: got 0x%02x", captured.frame[5][0]);
	zassert_equal(captured.frame[5][1], 0U, "path push reserved byte mismatch");
	zassert_mem_equal(&captured.frame[5][2], path_discovery + 2,
			  CONFIG_MBS_CONTACT_PREFIX_BYTES,
			  "path push prefix mismatch");
	for (size_t i = CONFIG_MBS_CONTACT_PREFIX_BYTES; i < TEST_APP_PREFIX_SIZE; i++) {
		zassert_equal(captured.frame[5][2 + i],
			      (uint8_t)(0x51U + i - CONFIG_MBS_CONTACT_PREFIX_BYTES),
			      "path push app prefix byte mismatch");
	}
	zassert_equal(captured.frame[5][2 + TEST_APP_PREFIX_SIZE], 1U,
		      "path push out path len mismatch");
	zassert_equal(captured.frame[5][3 + TEST_APP_PREFIX_SIZE], 0x7aU,
		      "path push out path byte mismatch");
	zassert_equal(captured.frame[5][4 + TEST_APP_PREFIX_SIZE], 1U,
		      "path push in path len mismatch");
	zassert_equal(captured.frame[5][5 + TEST_APP_PREFIX_SIZE], 0x8bU,
		      "path push in path byte mismatch");

	zassert_equal(captured.frame[6][0], TEST_PUSH_CODE_TRACE_DATA,
		      "trace push code mismatch: got 0x%02x", captured.frame[6][0]);
	zassert_equal(captured.frame[6][1], 1U, "trace push state mismatch");
	zassert_equal(get_u32_le(&captured.frame[6][4]), 1775000400U,
		      "trace push timestamp mismatch");
	zassert_equal(get_u32_le(&captured.frame[6][8]), captured_trace_tag,
		      "trace push tag mismatch");
	zassert_equal(captured.frame[6][12], 3U, "trace push out snr 0 mismatch");
	zassert_equal(captured.frame[6][13], 4U, "trace push out snr 1 mismatch");

	memset(&path, 0, sizeof(path));
	path.is_discover = true;
	memcpy(path.key_prefix, path_discovery + 2, sizeof(path.key_prefix));
	path.tag = captured_discover_tag;
	path.has_out_path = true;
	path.path_hash_size = 2U;
	zassert_ok(zbus_chan_pub(&mbs_contact_path_response_chan, &path, K_NO_WAIT));

	meshcore_companion_adapter_flush();

	zassert_equal(captured.frame[7][0], TEST_PUSH_CODE_PATH_DISCOVERY,
		      "zero-hop path push code mismatch: got 0x%02x",
		      captured.frame[7][0]);
	zassert_mem_equal(&captured.frame[7][2], path_discovery + 2,
			  CONFIG_MBS_CONTACT_PREFIX_BYTES,
			  "zero-hop path push prefix mismatch");
	zassert_equal(captured.frame[7][2 + TEST_APP_PREFIX_SIZE], 0x40U,
		      "zero-hop path push out path len field mismatch");
	zassert_equal(captured.frame[7][3 + TEST_APP_PREFIX_SIZE], 0U,
		      "zero-hop path push in path len field mismatch");

	received.type = meshbus_MessageContent_MessageType_RECEIVE_CHANNEL;
	received.route = meshbus_MessageContent_MessageRoute_ROUTE_FLOOD;
	received.target[0] = 0x11U;
	received.target[1] = 0x22U;
	received.target[2] = 0x33U;
	received.target[3] = 0x44U;
	received.payload_len = 4U;
	memcpy(received.payload, "room", received.payload_len);
	(void)snprintk(received.sender_name, sizeof(received.sender_name), "sender");
	received.sender_timestamp = 2U;
	zassert_ok(zbus_chan_pub(&mbs_message_response_chan, &received, K_NO_WAIT));

	memcpy(tel.key_prefix, path_discovery + 2, sizeof(tel.key_prefix));
	tel.tag = captured_telemetry_tag;
	tel.payload_len = 2U;
	tel.payload[0] = 0xaaU;
	tel.payload[1] = 0xbbU;
	zassert_ok(zbus_chan_pub(&mbs_contact_telemetry_response_chan, &tel, K_NO_WAIT));

	meshcore_companion_adapter_flush();

	zassert_equal(captured.frame[8][0], TEST_PUSH_CODE_MSG_WAITING,
		      "message waiting push code mismatch");
	zassert_equal(captured.frame[9][0], TEST_PUSH_CODE_TELEMETRY_RESPONSE,
		      "telemetry push code mismatch");
	zassert_equal(captured.frame[9][1], 0U, "telemetry push reserved byte mismatch");
	zassert_mem_equal(&captured.frame[9][2], path_discovery + 2,
			  CONFIG_MBS_CONTACT_PREFIX_BYTES,
			  "telemetry push prefix mismatch");
	for (size_t i = CONFIG_MBS_CONTACT_PREFIX_BYTES; i < TEST_APP_PREFIX_SIZE; i++) {
		zassert_equal(captured.frame[9][2 + i],
			      (uint8_t)(0x51U + i - CONFIG_MBS_CONTACT_PREFIX_BYTES),
			      "telemetry push app prefix byte mismatch");
	}
	zassert_equal(captured.frame[9][2 + TEST_APP_PREFIX_SIZE], 0xaaU,
		      "telemetry push payload byte 0 mismatch");
	zassert_equal(captured.frame[9][3 + TEST_APP_PREFIX_SIZE], 0xbbU,
		      "telemetry push payload byte 1 mismatch");
}

ZTEST(mbs_meshcore_companion_contract, test_trace_command_matches_arduino_wire_shape)
{
	const uint8_t trace[] = {
		TEST_CMD_SEND_TRACE_PATH,
		0x44U, 0x33U, 0x22U, 0x11U,
		0xddU, 0xccU, 0xbbU, 0xaaU,
		0x01U,
		0x10U, 0x11U, 0x20U, 0x21U, 0x30U, 0x31U,
	};
	struct mbs_meshcore_trace_response_event trace_resp = {0};

	reset_adapter(true);

	zassert_ok(meshcore_companion_adapter_rx_frame(trace, sizeof(trace)));
	zassert_equal(captured_meshcore_trace_path_len, 6U, "trace path len mismatch");
	zassert_equal(captured_meshcore_trace_path_hash_size, 2U,
		      "trace path hash size mismatch");
	zassert_mem_equal(captured_meshcore_trace_path, &trace[10], 6U,
			  "trace path mismatch");
	zassert_equal(captured.frame[0][0], TEST_RESP_CODE_SENT);
	zassert_equal(captured.frame[0][1], 0U, "trace route flag mismatch");
	zassert_equal(get_u32_le(&captured.frame[0][2]), 0x11223344U,
		      "trace app tag mismatch");
	zassert_equal(get_u32_le(&captured.frame[0][6]), 6000U,
		      "trace timeout mismatch");

	trace_resp.tag = captured_meshcore_trace_tag;
	trace_resp.state = 1U;
	trace_resp.out_path_snr_count = 2U;
	trace_resp.out_path_snr[0] = 3;
	trace_resp.out_path_snr[1] = 4;
	trace_resp.return_path_snr_count = 1U;
	trace_resp.return_path_snr[0] = 5;
	trace_resp.has_response_snr = true;
	trace_resp.response_snr = 6;
	zassert_ok(zbus_chan_pub(&mbs_meshcore_trace_response_chan, &trace_resp, K_NO_WAIT));

	meshcore_companion_adapter_flush();

	zassert_equal(captured.frame[1][0], TEST_PUSH_CODE_TRACE_DATA,
		      "trace push code mismatch");
	zassert_equal(captured.frame[1][1], 0U, "trace push reserved mismatch");
	zassert_equal(captured.frame[1][2], 6U, "trace push path len mismatch");
	zassert_equal(captured.frame[1][3], 1U, "trace push flags mismatch");
	zassert_equal(get_u32_le(&captured.frame[1][4]), 0x11223344U,
		      "trace push app tag mismatch");
	zassert_equal(get_u32_le(&captured.frame[1][8]), 0xaabbccddU,
		      "trace push auth mismatch");
	zassert_mem_equal(&captured.frame[1][12], &trace[10], 6U,
			  "trace push path mismatch");
	zassert_equal(captured.frame[1][18], 3U, "trace push path snr 0 mismatch");
	zassert_equal(captured.frame[1][19], 4U, "trace push path snr 1 mismatch");
	zassert_equal(captured.frame[1][20], 5U, "trace push path snr 2 mismatch");
	zassert_equal(captured.frame[1][21], 6U, "trace push response snr mismatch");
}

ZTEST(mbs_meshcore_companion_contract, test_status_request_maps_to_upstream_push)
{
	uint8_t status_req[1U + 32U] = {TEST_CMD_SEND_STATUS_REQ};
	mbs_contact_response_binary_event binary = {0};
	const uint8_t status_payload[] = {
		0x34U, 0x12U, 0x02U, 0x00U, 0xf0U, 0xffU,
	};

	for (size_t i = 0U; i < 32U; i++) {
		status_req[1U + i] = (uint8_t)(0x10U + i);
	}

	reset_adapter(true);

	zassert_ok(meshcore_companion_adapter_rx_frame(status_req, sizeof(status_req)));
	zassert_mem_equal(captured_binary_prefix, &status_req[1],
			  sizeof(captured_binary_prefix), "status prefix mismatch");
	zassert_equal(captured_binary_payload_len, 9U, "status payload len mismatch");
	zassert_equal(captured_binary_payload[0], 0x01U, "status request type mismatch");
	zassert_mem_equal(&captured_binary_payload[1], (uint8_t[4]){0}, 4U,
			  "status reserved bytes mismatch");
	zassert_equal(captured.frame[0][0], TEST_RESP_CODE_SENT, "status SENT code");
	zassert_equal(captured.frame[0][1], 0U, "status route flag");
	zassert_equal(get_u32_le(&captured.frame[0][2]), captured_binary_tag,
		      "status tag mismatch");
	zassert_equal(get_u32_le(&captured.frame[0][6]),
		      CONFIG_MBS_CONTACT_REQUEST_TIMEOUT_MS, "status timeout");

	binary.tag = captured_binary_tag;
	binary.payload_len = sizeof(status_payload);
	memcpy(binary.payload, status_payload, sizeof(status_payload));
	zassert_ok(zbus_chan_pub(&mbs_contact_binary_response_chan, &binary, K_NO_WAIT));
	meshcore_companion_adapter_flush();

	zassert_equal(captured.count, 2U, "status push count");
	zassert_equal(captured.frame[1][0], TEST_PUSH_CODE_STATUS_RESPONSE,
		      "status push code");
	zassert_equal(captured.frame[1][1], 0U, "status push reserved byte");
	zassert_mem_equal(&captured.frame[1][2], &status_req[1], TEST_APP_PREFIX_SIZE,
			  "status push prefix mismatch");
	zassert_mem_equal(&captured.frame[1][2 + TEST_APP_PREFIX_SIZE],
			  status_payload, sizeof(status_payload), "status push payload");
}

ZTEST(mbs_meshcore_companion_contract, test_status_requests_keep_tag_prefix_correlation)
{
	uint8_t status_a[1U + 32U] = {TEST_CMD_SEND_STATUS_REQ};
	uint8_t status_b[1U + 32U] = {TEST_CMD_SEND_STATUS_REQ};
	mbs_contact_response_binary_event binary = {0};
	uint32_t tag_a;
	uint32_t tag_b;

	for (size_t i = 0U; i < 32U; i++) {
		status_a[1U + i] = (uint8_t)(0x20U + i);
		status_b[1U + i] = (uint8_t)(0x80U + i);
	}

	reset_adapter(true);

	zassert_ok(meshcore_companion_adapter_rx_frame(status_a, sizeof(status_a)));
	tag_a = captured_binary_tag;
	zassert_ok(meshcore_companion_adapter_rx_frame(status_b, sizeof(status_b)));
	tag_b = captured_binary_tag;
	zassert_equal(tag_b, tag_a + 1U, "binary tag did not advance");

	binary.tag = tag_b;
	binary.payload_len = 1U;
	binary.payload[0] = 0xb0U;
	zassert_ok(zbus_chan_pub(&mbs_contact_binary_response_chan, &binary, K_NO_WAIT));
	meshcore_companion_adapter_flush();

	binary.tag = tag_a;
	binary.payload[0] = 0xa0U;
	zassert_ok(zbus_chan_pub(&mbs_contact_binary_response_chan, &binary, K_NO_WAIT));
	meshcore_companion_adapter_flush();

	zassert_equal(captured.count, 4U, "status response count");
	zassert_equal(captured.frame[2][0], TEST_PUSH_CODE_STATUS_RESPONSE,
		      "status B push code");
	zassert_mem_equal(&captured.frame[2][2], &status_b[1], TEST_APP_PREFIX_SIZE,
			  "status B prefix mismatch");
	zassert_equal(captured.frame[2][2 + TEST_APP_PREFIX_SIZE], 0xb0U,
		      "status B payload mismatch");
	zassert_equal(captured.frame[3][0], TEST_PUSH_CODE_STATUS_RESPONSE,
		      "status A push code");
	zassert_mem_equal(&captured.frame[3][2], &status_a[1], TEST_APP_PREFIX_SIZE,
			  "status A prefix mismatch");
	zassert_equal(captured.frame[3][2 + TEST_APP_PREFIX_SIZE], 0xa0U,
		      "status A payload mismatch");
}

ZTEST(mbs_meshcore_companion_contract, test_low_level_meshcore_commands)
{
	uint8_t binary_req[1U + 32U + 3U] = {TEST_CMD_SEND_BINARY_REQ};
	const uint8_t channel_data[] = {
		TEST_CMD_SEND_CHANNEL_DATA, 2U, 1U, 0xaaU,
		0xffU, 0xffU, 'd', 'a', 't', 'a',
	};
	const uint8_t channel_data_reserved[] = {
		TEST_CMD_SEND_CHANNEL_DATA, 2U, 0xffU, 0x00U, 0x00U,
	};
	const uint8_t binary_short[] = {
		TEST_CMD_SEND_BINARY_REQ, 0x00U,
	};
	const uint8_t raw_data[] = {
		TEST_CMD_SEND_RAW_DATA, 1U, 0xbbU, 'r', 'a', 'w', '!',
	};
	const uint8_t raw_flood[] = {
		TEST_CMD_SEND_RAW_DATA, TEST_CONTACT_PATH_UNKNOWN, 'r', 'a', 'w', '!',
	};
	const uint8_t control_data[] = {
		TEST_CMD_SEND_CONTROL_DATA, 0x80U, 'c', 't', 'l',
	};
	const uint8_t control_data_low_bit[] = {
		TEST_CMD_SEND_CONTROL_DATA, 0x01U,
	};

	for (size_t i = 0U; i < 32U; i++) {
		binary_req[1U + i] = (uint8_t)(0x40U + i);
	}
	memcpy(&binary_req[33], "bin", 3U);

	reset_adapter(true);

	zassert_ok(meshcore_companion_adapter_rx_frame(binary_req, sizeof(binary_req)));
	zassert_mem_equal(captured_binary_prefix, &binary_req[1],
			  sizeof(captured_binary_prefix), "binary prefix mismatch");
	zassert_equal(captured_binary_payload_len, 3U, "binary payload len mismatch");
	zassert_mem_equal(captured_binary_payload, "bin", 3U, "binary payload mismatch");
	zassert_equal(captured.frame[0][0], TEST_RESP_CODE_SENT, "binary response code");
	zassert_equal(captured.frame[0][1], 0U, "binary flood flag placeholder");
	zassert_equal(get_u32_le(&captured.frame[0][2]), 0x55667788U,
		      "binary response tag");
	zassert_equal(get_u32_le(&captured.frame[0][6]), 0U,
		      "binary timeout placeholder");

	zassert_ok(meshcore_companion_adapter_rx_frame(channel_data, sizeof(channel_data)));
	zassert_equal(captured_channel_index, 2U, "channel data index");
	zassert_equal(captured_meshcore_path_len, 1U, "channel path len");
	zassert_equal(captured_meshcore_path[0], 0xaaU, "channel path byte");
	zassert_equal(captured_meshcore_data_type, 0xffffU, "channel data type");
	zassert_equal(captured_meshcore_payload_len, 4U, "channel payload len");
	zassert_mem_equal(captured_meshcore_payload, "data", 4U, "channel payload");
	zassert_equal(captured.frame[1][0], TEST_RESP_CODE_OK, "channel data OK");

	zassert_ok(meshcore_companion_adapter_rx_frame(raw_data, sizeof(raw_data)));
	zassert_equal(captured_meshcore_path_len, 1U, "raw path len");
	zassert_equal(captured_meshcore_path[0], 0xbbU, "raw path byte");
	zassert_equal(captured_meshcore_payload_len, 4U, "raw payload len");
	zassert_mem_equal(captured_meshcore_payload, "raw!", 4U, "raw payload");
	zassert_equal(captured.frame[2][0], TEST_RESP_CODE_OK, "raw data OK");

	zassert_ok(meshcore_companion_adapter_rx_frame(control_data, sizeof(control_data)));
	zassert_equal(captured_meshcore_payload_len, 4U, "control payload len");
	zassert_mem_equal(captured_meshcore_payload, &control_data[1], 4U, "control payload");
	zassert_equal(captured.frame[3][0], TEST_RESP_CODE_OK, "control data OK");

	zassert_ok(meshcore_companion_adapter_rx_frame(channel_data_reserved,
						      sizeof(channel_data_reserved)));
	zassert_equal(captured.frame[4][0], TEST_RESP_CODE_ERR, "reserved type error code");
	zassert_equal(captured.frame[4][1], TEST_ERR_ILLEGAL_ARG,
		      "reserved type should be illegal");

	zassert_ok(meshcore_companion_adapter_rx_frame(binary_short,
						      sizeof(binary_short)));
	zassert_equal(captured.frame[5][0], TEST_RESP_CODE_ERR, "short binary error code");
	zassert_equal(captured.frame[5][1], TEST_ERR_ILLEGAL_ARG,
		      "short binary should be illegal");

	zassert_ok(meshcore_companion_adapter_rx_frame(raw_flood, sizeof(raw_flood)));
	zassert_equal(captured.frame[6][0], TEST_RESP_CODE_ERR, "raw flood error code");
	zassert_equal(captured.frame[6][1], TEST_ERR_UNSUPPORTED_CMD,
		      "raw flood should be unsupported");

	zassert_ok(meshcore_companion_adapter_rx_frame(control_data_low_bit,
						      sizeof(control_data_low_bit)));
	zassert_equal(captured.frame[7][0], TEST_RESP_CODE_ERR,
		      "low-bit control error code");
	zassert_equal(captured.frame[7][1], TEST_ERR_ILLEGAL_ARG,
		      "low-bit control should be illegal");
}

ZTEST(mbs_meshcore_companion_contract, test_login_uses_transient_management_secret)
{
	const uint8_t password[] = "phase9-password!";
	uint8_t login[1U + MBS_CONTACT_PUBLIC_KEY_SIZE + sizeof(password)] = {0};
	mbs_management_smp_response_event response = {0};

	reset_adapter(true);
	login[0] = TEST_CMD_SEND_LOGIN;
	for (size_t i = 0U; i < MBS_CONTACT_PUBLIC_KEY_SIZE; i++) {
		login[1U + i] = 0x10U + i;
	}
	memcpy(&login[1U + MBS_CONTACT_PUBLIC_KEY_SIZE], password,
	       sizeof(password));

	zassert_ok(meshcore_companion_adapter_rx_frame(login, sizeof(login)));
	zassert_equal(captured.count, 1U, "login should send SENT response first");
	zassert_equal(captured.frame[0][0], TEST_RESP_CODE_SENT,
		      "login response code");
	zassert_equal(captured.frame[0][1], 0U, "login route placeholder");
	zassert_equal(get_u32_le(&captured.frame[0][2]),
		      get_u32_le(&login[1]), "login app tag");
	zassert_equal(get_u32_le(&captured.frame[0][6]), 60000U,
		      "login timeout");

	wait_login_management_start();
	zassert_not_equal(captured_management_tag, 0U,
			  "login should start management exchange");
	zassert_mem_equal(captured_management_request.contact_prefix,
			  &login[1], CONFIG_MBS_CONTACT_PREFIX_BYTES,
			  "management contact prefix");
	zassert_mem_equal(captured_management_secret, password,
			  sizeof(password) - 1U, "transient secret");
	zassert_equal(captured_management_secret_len, sizeof(password) - 1U,
		      "transient secret length");
	zassert_equal(captured_management_request.packet_len, TEST_MGMT_HDR_SIZE,
		      "login SMP length");
	zassert_equal(captured_management_request.packet[0], MGMT_OP_READ,
		      "login SMP op");
	zassert_equal(captured_management_request.packet[4],
		      (uint8_t)(meshbus_PowerMgmtGroupId_POWER_MGMT_GROUP_ID_MESHBUS_POWER >> 8),
		      "login SMP group high");
	zassert_equal(captured_management_request.packet[5],
		      (uint8_t)meshbus_PowerMgmtGroupId_POWER_MGMT_GROUP_ID_MESHBUS_POWER,
		      "login SMP group low");
	zassert_equal(captured_management_request.packet[7],
		      meshbus_PowerMgmtCommandId_POWER_MGMT_COMMAND_ID_STATUS,
		      "login SMP command");
	zassert_equal(captured_contact_update.public_key.size, 0U,
		      "login must not persist contact updates");

	response.tag = captured_management_tag;
	response.response_len = TEST_MGMT_HDR_SIZE;
	response.response[0] = MGMT_OP_READ_RSP;
	response.response[4] =
		(uint8_t)(meshbus_PowerMgmtGroupId_POWER_MGMT_GROUP_ID_MESHBUS_POWER >> 8);
	response.response[5] =
		(uint8_t)meshbus_PowerMgmtGroupId_POWER_MGMT_GROUP_ID_MESHBUS_POWER;
	response.response[7] =
		meshbus_PowerMgmtCommandId_POWER_MGMT_COMMAND_ID_STATUS;
	zassert_ok(zbus_chan_pub(&mbs_management_smp_response_chan,
				 &response, K_NO_WAIT));
	drain_companion_work_until(2U);
	zassert_equal(captured.count, 2U, "login success push count");
	zassert_equal(captured.frame[1][0], TEST_PUSH_CODE_LOGIN_SUCCESS,
		      "login success code");
	zassert_equal(captured.frame[1][1], 1U, "login admin permission");
	zassert_mem_equal(&captured.frame[1][2], &login[1], TEST_APP_PREFIX_SIZE,
			  "login success prefix");
	zassert_equal(captured.frame[1][12], 3U, "login ACL permissions");
	zassert_equal(captured.frame[1][13], 11U, "login firmware level");
}

ZTEST(mbs_meshcore_companion_contract, test_login_accepts_minimum_password)
{
	const uint8_t password[] = "12345678";
	uint8_t login[1U + MBS_CONTACT_PUBLIC_KEY_SIZE + sizeof(password)] = {0};

	reset_adapter(true);
	login[0] = TEST_CMD_SEND_LOGIN;
	for (size_t i = 0U; i < MBS_CONTACT_PUBLIC_KEY_SIZE; i++) {
		login[1U + i] = 0x10U + i;
	}
	memcpy(&login[1U + MBS_CONTACT_PUBLIC_KEY_SIZE], password,
	       sizeof(password));

	zassert_ok(meshcore_companion_adapter_rx_frame(login, sizeof(login)));
	wait_login_management_start();
	zassert_not_equal(captured_management_tag, 0U,
			  "login should start management exchange");
	zassert_equal(captured_management_secret_len, sizeof(password) - 1U,
		      "minimum password length mismatch");
	zassert_mem_equal(captured_management_secret, password,
			  sizeof(password) - 1U, "minimum password bytes");
}

ZTEST(mbs_meshcore_companion_contract, test_login_failure_pushes_fail_frame)
{
	const uint8_t password[] = "phase9-password!";
	uint8_t login[1U + MBS_CONTACT_PUBLIC_KEY_SIZE + sizeof(password)] = {0};
	mbs_management_smp_response_event response = {0};

	reset_adapter(true);
	login[0] = TEST_CMD_SEND_LOGIN;
	for (size_t i = 0U; i < MBS_CONTACT_PUBLIC_KEY_SIZE; i++) {
		login[1U + i] = 0x10U + i;
	}
	memcpy(&login[1U + MBS_CONTACT_PUBLIC_KEY_SIZE], password,
	       sizeof(password));

	zassert_ok(meshcore_companion_adapter_rx_frame(login, sizeof(login)));
	wait_login_management_start();
	zassert_not_equal(captured_management_tag, 0U,
			  "login should start management exchange");

	response.tag = captured_management_tag;
	response.status = -EACCES;
	response.response_len = 0U;
	zassert_ok(zbus_chan_pub(&mbs_management_smp_response_chan,
				 &response, K_NO_WAIT));
	drain_companion_work_until(2U);
	zassert_equal(captured.count, 2U, "login failure push count");
	zassert_equal(captured.frame[1][0], TEST_PUSH_CODE_LOGIN_FAIL,
		      "login failure code");
	zassert_equal(captured.frame[1][1], 0U, "login failure reserved");
	zassert_mem_equal(&captured.frame[1][2], &login[1], TEST_APP_PREFIX_SIZE,
			  "login failure prefix");
}

ZTEST(mbs_meshcore_companion_contract, test_cli_data_get_name_uses_login_session)
{
	uint8_t public_key[MBS_CONTACT_PUBLIC_KEY_SIZE];
	uint8_t cli[64];
	size_t cli_len;
	mbs_management_smp_response_event response = {0};
	meshbus_MeshcoreConfigGetResponse rsp =
		meshbus_MeshcoreConfigGetResponse_init_zero;

	reset_adapter(true);
	companion_test_login_success(public_key);

	cli_len = companion_test_cli_frame(cli, sizeof(cli), public_key,
					   "01|get name");
	zassert_ok(meshcore_companion_adapter_rx_frame(cli, cli_len),
		   "CLI get name should be accepted");
	meshcore_companion_adapter_flush();
	zassert_equal(captured.count, 3U, "CLI should send immediate SENT");
	zassert_equal(captured.frame[2][0], TEST_RESP_CODE_SENT,
		      "CLI sent response");
	zassert_equal(get_u32_le(&captured.frame[2][2]), 0U,
		      "CLI sent response should not expect message ACK");
	zassert_mem_equal(captured_management_request.contact_prefix, public_key,
			  CONFIG_MBS_CONTACT_PREFIX_BYTES,
			  "CLI management target prefix");
	zassert_mem_equal(captured_management_secret, "phase10-password",
			  sizeof("phase10-password") - 1U,
			  "CLI should use transient login secret");
	zassert_equal(captured_management_request.packet[0], MGMT_OP_READ,
		      "CLI get name op");
	zassert_equal(sys_get_be16(&captured_management_request.packet[4]),
		      meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
		      "CLI get name group");
	zassert_equal(captured_management_request.packet[7],
		      meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG,
		      "CLI get name command");

	rsp.has_config = true;
	(void)snprintk(rsp.config.name, sizeof(rsp.config.name), "%s",
		       "remote-name");
	test_mgmt_response_build(
		&response, captured_management_tag,
		meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
		meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_READ_RSP, &rsp, meshbus_MeshcoreConfigGetResponse_fields,
		meshbus_MeshcoreConfigGetResponse_size);
	zassert_ok(zbus_chan_pub(&mbs_management_smp_response_chan,
				 &response, K_NO_WAIT));
	drain_companion_work_until(4U);
	zassert_equal(captured.count, 4U, "CLI response push count");
	zassert_equal(captured.frame[3][0], TEST_RESP_CODE_CONTACT_MSG_V3,
		      "CLI response frame code");
	zassert_mem_equal(&captured.frame[3][4], public_key, TEST_APP_PREFIX_SIZE,
			  "CLI response sender prefix");
	zassert_equal(captured.frame[3][11], TEST_TXT_TYPE_CLI_DATA,
		      "CLI response text type");
	zassert_mem_equal(&captured.frame[3][16], "01|remote-name",
			  sizeof("01|remote-name") - 1U,
			  "CLI response payload");
}

ZTEST(mbs_meshcore_companion_contract, test_cli_data_get_radio_formats_upstream_units)
{
	uint8_t public_key[MBS_CONTACT_PUBLIC_KEY_SIZE];
	uint8_t cli[64];
	size_t cli_len;
	mbs_management_smp_response_event response = {0};
	meshbus_RadioConfigGetResponse rsp =
		meshbus_RadioConfigGetResponse_init_zero;

	reset_adapter(true);
	companion_test_login_success(public_key);

	cli_len = companion_test_cli_frame(cli, sizeof(cli), public_key,
					   "03|get radio");
	zassert_ok(meshcore_companion_adapter_rx_frame(cli, cli_len),
		   "CLI get radio should be accepted");
	meshcore_companion_adapter_flush();
	zassert_equal(captured_management_request.packet[0], MGMT_OP_READ,
		      "CLI get radio op");
	zassert_equal(sys_get_be16(&captured_management_request.packet[4]),
		      meshbus_RadioMgmtGroupId_RADIO_MGMT_GROUP_ID_MESHBUS_RADIO,
		      "CLI get radio group");
	zassert_equal(captured_management_request.packet[7],
		      meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_CONFIG,
		      "CLI get radio command");

	rsp.has_config = true;
	rsp.config.frequency = 916575000ULL;
	rsp.config.bandwidth = 62500U;
	rsp.config.spread_factor = 7U;
	rsp.config.coding_rate = 8U;
	test_mgmt_response_build(
		&response, captured_management_tag,
		meshbus_RadioMgmtGroupId_RADIO_MGMT_GROUP_ID_MESHBUS_RADIO,
		meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_READ_RSP, &rsp, meshbus_RadioConfigGetResponse_fields,
		meshbus_RadioConfigGetResponse_size);
	zassert_ok(zbus_chan_pub(&mbs_management_smp_response_chan,
				 &response, K_NO_WAIT));
	drain_companion_work_until(4U);
	zassert_equal(captured.count, 4U, "CLI radio response push count");
	zassert_equal(captured.frame[3][0], TEST_RESP_CODE_CONTACT_MSG_V3,
		      "CLI radio response frame code");
	zassert_equal(captured.frame[3][11], TEST_TXT_TYPE_CLI_DATA,
		      "CLI radio response text type");
	zassert_mem_equal(&captured.frame[3][16], "03|916.575,62.5,7,8",
			  sizeof("03|916.575,62.5,7,8") - 1U,
			  "CLI radio response payload");
}

ZTEST(mbs_meshcore_companion_contract, test_cli_data_set_repeat_maps_disable_fwd)
{
	uint8_t public_key[MBS_CONTACT_PUBLIC_KEY_SIZE];
	uint8_t cli[64];
	size_t cli_len;
	uint32_t get_tag;
	uint32_t set_tag;
	mbs_management_smp_response_event response = {0};
	meshbus_MeshcoreConfigGetResponse get_rsp =
		meshbus_MeshcoreConfigGetResponse_init_zero;
	meshbus_MeshcoreConfigSetResponse set_rsp =
		meshbus_MeshcoreConfigSetResponse_init_zero;
	meshbus_MeshcoreConfigSetRequest set_req =
		meshbus_MeshcoreConfigSetRequest_init_zero;
	uint16_t payload_len;

	reset_adapter(true);
	companion_test_login_success(public_key);

	cli_len = companion_test_cli_frame(cli, sizeof(cli), public_key,
					   "17|set repeat on");
	zassert_ok(meshcore_companion_adapter_rx_frame(cli, cli_len),
		   "CLI set repeat should be accepted");
	meshcore_companion_adapter_flush();
	get_tag = captured_management_tag;
	zassert_equal(captured_management_request.packet[0], MGMT_OP_READ,
		      "set repeat should start with config get");

	get_rsp.has_config = true;
	get_rsp.config.disable_fwd = true;
	get_rsp.config.advert_interval = 11U;
	get_rsp.config.flood_advert_interval = 22U;
	test_mgmt_response_build(
		&response, get_tag,
		meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
		meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_READ_RSP, &get_rsp,
		meshbus_MeshcoreConfigGetResponse_fields,
		meshbus_MeshcoreConfigGetResponse_size);
	zassert_ok(zbus_chan_pub(&mbs_management_smp_response_chan,
				 &response, K_NO_WAIT));
	set_tag = captured_management_tag;
	zassert_not_equal(set_tag, get_tag, "set repeat should queue config set");
	zassert_equal(captured_management_request.packet[0], MGMT_OP_WRITE,
		      "set repeat config set op");
	payload_len = sys_get_be16(&captured_management_request.packet[2]);
	zassert_ok(test_mgmt_cbor_data_decode(
			   &captured_management_request.packet[TEST_MGMT_HDR_SIZE],
			   payload_len, meshbus_MeshcoreConfigSetRequest_fields,
			   &set_req, sizeof(set_req)),
		   "decode set repeat request");
	zassert_true(set_req.has_config, "set request config");
	zassert_false(set_req.config.disable_fwd,
		      "repeat on should clear disable_fwd");
	zassert_equal(set_req.config.advert_interval, 11U,
		      "set should preserve advert interval");

	set_rsp.has_config = true;
	set_rsp.config = set_req.config;
	test_mgmt_response_build(
		&response, set_tag,
		meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
		meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_WRITE_RSP, &set_rsp,
		meshbus_MeshcoreConfigSetResponse_fields,
		meshbus_MeshcoreConfigSetResponse_size);
	zassert_ok(zbus_chan_pub(&mbs_management_smp_response_chan,
				 &response, K_NO_WAIT));
	drain_companion_work_until(4U);
	zassert_equal(captured.count, 4U, "set repeat response push count");
	zassert_equal(captured.frame[3][0], TEST_RESP_CODE_CONTACT_MSG_V3,
		      "set repeat response frame code");
	zassert_equal(captured.frame[3][11], TEST_TXT_TYPE_CLI_DATA,
		      "set repeat response type");
	zassert_mem_equal(&captured.frame[3][16], "17|OK",
			  sizeof("17|OK") - 1U,
			  "set repeat response payload");
}

ZTEST(mbs_meshcore_companion_contract, test_cli_data_meshcore_config_upstream_units)
{
	uint8_t public_key[MBS_CONTACT_PUBLIC_KEY_SIZE];
	uint8_t cli[96];
	size_t cli_len;
	uint32_t get_tag;
	uint32_t set_tag;
	mbs_management_smp_response_event response = {0};
	meshbus_MeshcoreConfigGetResponse get_rsp =
		meshbus_MeshcoreConfigGetResponse_init_zero;
	meshbus_MeshcoreConfigSetResponse set_rsp =
		meshbus_MeshcoreConfigSetResponse_init_zero;
	meshbus_MeshcoreConfigSetRequest set_req =
		meshbus_MeshcoreConfigSetRequest_init_zero;
	uint16_t payload_len;

	reset_adapter(true);
	companion_test_login_success(public_key);

	cli_len = companion_test_cli_frame(cli, sizeof(cli), public_key,
					   "09|get advert.interval");
	zassert_ok(meshcore_companion_adapter_rx_frame(cli, cli_len));
	meshcore_companion_adapter_flush();
	get_rsp.has_config = true;
	get_rsp.config.advert_interval = 120U;
	get_rsp.config.flood_advert_interval = 7200U;
	get_rsp.config.latitude = 59257018;
	get_rsp.config.longitude = -161468113;
	test_mgmt_response_build(
		&response, captured_management_tag,
		meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
		meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_READ_RSP, &get_rsp,
		meshbus_MeshcoreConfigGetResponse_fields,
		meshbus_MeshcoreConfigGetResponse_size);
	zassert_ok(zbus_chan_pub(&mbs_management_smp_response_chan,
				 &response, K_NO_WAIT));
	drain_companion_work_until(4U);
	zassert_mem_equal(&captured.frame[3][16], "09|2",
			  sizeof("09|2") - 1U,
			  "advert interval should be reported in minutes");

	cli_len = companion_test_cli_frame(cli, sizeof(cli), public_key,
					   "0a|get flood.advert.interval");
	zassert_ok(meshcore_companion_adapter_rx_frame(cli, cli_len));
	meshcore_companion_adapter_flush();
	test_mgmt_response_build(
		&response, captured_management_tag,
		meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
		meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_READ_RSP, &get_rsp,
		meshbus_MeshcoreConfigGetResponse_fields,
		meshbus_MeshcoreConfigGetResponse_size);
	zassert_ok(zbus_chan_pub(&mbs_management_smp_response_chan,
				 &response, K_NO_WAIT));
	drain_companion_work_until(6U);
	zassert_mem_equal(&captured.frame[5][16], "0a|2",
			  sizeof("0a|2") - 1U,
			  "flood advert interval should be reported in hours");

	cli_len = companion_test_cli_frame(cli, sizeof(cli), public_key,
					   "20|get lon");
	zassert_ok(meshcore_companion_adapter_rx_frame(cli, cli_len));
	meshcore_companion_adapter_flush();
	test_mgmt_response_build(
		&response, captured_management_tag,
		meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
		meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_READ_RSP, &get_rsp,
		meshbus_MeshcoreConfigGetResponse_fields,
		meshbus_MeshcoreConfigGetResponse_size);
	zassert_ok(zbus_chan_pub(&mbs_management_smp_response_chan,
				 &response, K_NO_WAIT));
	drain_companion_work_until(8U);
	zassert_mem_equal(&captured.frame[7][16], "20|-161.468113",
			  sizeof("20|-161.468113") - 1U,
			  "longitude should be formatted as decimal degrees");

	cli_len = companion_test_cli_frame(cli, sizeof(cli), public_key,
					   "0b|set advert.interval 1");
	zassert_ok(meshcore_companion_adapter_rx_frame(cli, cli_len));
	meshcore_companion_adapter_flush();
	get_tag = captured_management_tag;
	test_mgmt_response_build(
		&response, get_tag,
		meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
		meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_READ_RSP, &get_rsp,
		meshbus_MeshcoreConfigGetResponse_fields,
		meshbus_MeshcoreConfigGetResponse_size);
	zassert_ok(zbus_chan_pub(&mbs_management_smp_response_chan,
				 &response, K_NO_WAIT));
	set_tag = captured_management_tag;
	payload_len = sys_get_be16(&captured_management_request.packet[2]);
	zassert_ok(test_mgmt_cbor_data_decode(
			   &captured_management_request.packet[TEST_MGMT_HDR_SIZE],
			   payload_len, meshbus_MeshcoreConfigSetRequest_fields,
			   &set_req, sizeof(set_req)),
		   "decode set advert interval request");
	zassert_equal(set_req.config.advert_interval, 60U,
		      "set advert.interval should convert minutes to seconds");
	set_rsp.has_config = true;
	set_rsp.config = set_req.config;
	test_mgmt_response_build(
		&response, set_tag,
		meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
		meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_WRITE_RSP, &set_rsp,
		meshbus_MeshcoreConfigSetResponse_fields,
		meshbus_MeshcoreConfigSetResponse_size);
	zassert_ok(zbus_chan_pub(&mbs_management_smp_response_chan,
				 &response, K_NO_WAIT));
	drain_companion_work_until(10U);

	cli_len = companion_test_cli_frame(cli, sizeof(cli), public_key,
					   "0c|set flood.advert.interval 1");
	zassert_ok(meshcore_companion_adapter_rx_frame(cli, cli_len));
	meshcore_companion_adapter_flush();
	get_tag = captured_management_tag;
	test_mgmt_response_build(
		&response, get_tag,
		meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
		meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_READ_RSP, &get_rsp,
		meshbus_MeshcoreConfigGetResponse_fields,
		meshbus_MeshcoreConfigGetResponse_size);
	zassert_ok(zbus_chan_pub(&mbs_management_smp_response_chan,
				 &response, K_NO_WAIT));
	payload_len = sys_get_be16(&captured_management_request.packet[2]);
	zassert_ok(test_mgmt_cbor_data_decode(
			   &captured_management_request.packet[TEST_MGMT_HDR_SIZE],
			   payload_len, meshbus_MeshcoreConfigSetRequest_fields,
			   &set_req, sizeof(set_req)),
		   "decode set flood advert interval request");
	zassert_equal(set_req.config.flood_advert_interval, 3600U,
		      "set flood.advert.interval should convert hours to seconds");

	cli_len = companion_test_cli_frame(cli, sizeof(cli), public_key,
					   "21|set lon -161.468113");
	zassert_ok(meshcore_companion_adapter_rx_frame(cli, cli_len));
	meshcore_companion_adapter_flush();
	get_tag = captured_management_tag;
	test_mgmt_response_build(
		&response, get_tag,
		meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
		meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_READ_RSP, &get_rsp,
		meshbus_MeshcoreConfigGetResponse_fields,
		meshbus_MeshcoreConfigGetResponse_size);
	zassert_ok(zbus_chan_pub(&mbs_management_smp_response_chan,
				 &response, K_NO_WAIT));
	payload_len = sys_get_be16(&captured_management_request.packet[2]);
	zassert_ok(test_mgmt_cbor_data_decode(
			   &captured_management_request.packet[TEST_MGMT_HDR_SIZE],
			   payload_len, meshbus_MeshcoreConfigSetRequest_fields,
			   &set_req, sizeof(set_req)),
		   "decode set lon request");
	zassert_equal(set_req.config.longitude, -161468113,
		      "set lon should store signed microdegrees");
}

ZTEST(mbs_meshcore_companion_contract, test_cli_data_direct_clock_and_password)
{
	uint8_t public_key[MBS_CONTACT_PUBLIC_KEY_SIZE];
	uint8_t cli[80];
	size_t cli_len;
	uint32_t time_tag;
	uint32_t sync_tag;
	mbs_management_smp_response_event response = {0};
	meshbus_ClockTimeSetRequest time_req =
		meshbus_ClockTimeSetRequest_init_zero;
	meshbus_ClockTimeSetRequest sync_req =
		meshbus_ClockTimeSetRequest_init_zero;
	meshbus_ClockTimeSetResponse time_rsp =
		meshbus_ClockTimeSetResponse_init_zero;
	meshbus_ManagementSecretSetRequest secret_req =
		meshbus_ManagementSecretSetRequest_init_zero;
	uint16_t payload_len;

	reset_adapter(true);
	companion_test_login_success(public_key);

	cli_len = companion_test_cli_frame(cli, sizeof(cli), public_key,
					   "0f|time 1782636681");
	zassert_ok(meshcore_companion_adapter_rx_frame(cli, cli_len));
	meshcore_companion_adapter_flush();
	time_tag = captured_management_tag;
	zassert_equal(captured.frame[2][0], TEST_RESP_CODE_SENT,
		      "time should send SENT before management response");
	zassert_equal(get_u32_le(&captured.frame[2][2]), 0U,
		      "time sent response should not expect message ACK");
	zassert_equal(captured_management_request.packet[0], MGMT_OP_WRITE,
		      "time command op");
	zassert_equal(sys_get_be16(&captured_management_request.packet[4]),
		      meshbus_ClockMgmtGroupId_CLOCK_MGMT_GROUP_ID_MESHBUS_CLOCK,
		      "time command group");
	zassert_equal(captured_management_request.packet[7],
		      meshbus_ClockMgmtCommandId_CLOCK_MGMT_COMMAND_ID_TIME_SET,
		      "time command id");
	payload_len = sys_get_be16(&captured_management_request.packet[2]);
	zassert_ok(test_mgmt_cbor_data_decode(
			   &captured_management_request.packet[TEST_MGMT_HDR_SIZE],
			   payload_len, meshbus_ClockTimeSetRequest_fields,
			   &time_req, sizeof(time_req)),
		   "decode time set request");
	zassert_equal(time_req.unix_time_ms, 1782636681000ULL,
		      "time command should send unix milliseconds");

	time_rsp.accepted = true;
	time_rsp.unix_time_ms = time_req.unix_time_ms;
	test_mgmt_response_build(
		&response, time_tag,
		meshbus_ClockMgmtGroupId_CLOCK_MGMT_GROUP_ID_MESHBUS_CLOCK,
		meshbus_ClockMgmtCommandId_CLOCK_MGMT_COMMAND_ID_TIME_SET,
		MGMT_OP_WRITE_RSP, &time_rsp, meshbus_ClockTimeSetResponse_fields,
		meshbus_ClockTimeSetResponse_size);
	zassert_ok(zbus_chan_pub(&mbs_management_smp_response_chan,
				 &response, K_NO_WAIT));
	drain_companion_work_until(4U);
	zassert_equal(captured.frame[3][0], TEST_RESP_CODE_CONTACT_MSG_V3,
		      "time management response should push CLI response");
	zassert_equal(captured.frame[3][11], TEST_TXT_TYPE_CLI_DATA,
		      "time response text type");
	zassert_mem_equal(&captured.frame[3][16],
			  "0f|OK - clock set: 08:51 - 28/6/2026 UTC",
			  sizeof("0f|OK - clock set: 08:51 - 28/6/2026 UTC") - 1U,
			  "time response payload");

	cli_len = companion_test_cli_frame(cli, sizeof(cli), public_key,
					   "11|sync_time");
	zassert_ok(meshcore_companion_adapter_rx_frame(cli, cli_len));
	meshcore_companion_adapter_flush();
	sync_tag = captured_management_tag;
	zassert_equal(captured.frame[4][0], TEST_RESP_CODE_SENT,
		      "sync_time should send SENT before management response");
	zassert_equal(get_u32_le(&captured.frame[4][2]), 0U,
		      "sync_time sent response should not expect message ACK");
	zassert_equal(captured_management_request.packet[0], MGMT_OP_WRITE,
		      "sync_time command op");
	zassert_equal(sys_get_be16(&captured_management_request.packet[4]),
		      meshbus_ClockMgmtGroupId_CLOCK_MGMT_GROUP_ID_MESHBUS_CLOCK,
		      "sync_time command group");
	zassert_equal(captured_management_request.packet[7],
		      meshbus_ClockMgmtCommandId_CLOCK_MGMT_COMMAND_ID_TIME_SET,
		      "sync_time command id");
	payload_len = sys_get_be16(&captured_management_request.packet[2]);
	zassert_ok(test_mgmt_cbor_data_decode(
			   &captured_management_request.packet[TEST_MGMT_HDR_SIZE],
			   payload_len, meshbus_ClockTimeSetRequest_fields,
			   &sync_req, sizeof(sync_req)),
		   "decode sync_time set request");
	zassert_equal(sync_req.unix_time_ms, 1782636682000ULL,
		      "sync_time should use app timestamp plus one second");

	time_rsp.accepted = true;
	time_rsp.unix_time_ms = sync_req.unix_time_ms;
	test_mgmt_response_build(
		&response, sync_tag,
		meshbus_ClockMgmtGroupId_CLOCK_MGMT_GROUP_ID_MESHBUS_CLOCK,
		meshbus_ClockMgmtCommandId_CLOCK_MGMT_COMMAND_ID_TIME_SET,
		MGMT_OP_WRITE_RSP, &time_rsp, meshbus_ClockTimeSetResponse_fields,
		meshbus_ClockTimeSetResponse_size);
	zassert_ok(zbus_chan_pub(&mbs_management_smp_response_chan,
				 &response, K_NO_WAIT));
	drain_companion_work_until(6U);
	zassert_equal(captured.frame[5][0], TEST_RESP_CODE_CONTACT_MSG_V3,
		      "sync_time management response should push CLI response");
	zassert_equal(captured.frame[5][11], TEST_TXT_TYPE_CLI_DATA,
		      "sync_time response text type");
	zassert_mem_equal(&captured.frame[5][16],
			  "11|OK - clock set: 08:51 - 28/6/2026 UTC",
			  sizeof("11|OK - clock set: 08:51 - 28/6/2026 UTC") - 1U,
			  "sync_time response payload");

	cli_len = companion_test_cli_frame(cli, sizeof(cli), public_key,
					   "10|password 12345678");
	zassert_ok(meshcore_companion_adapter_rx_frame(cli, cli_len));
	meshcore_companion_adapter_flush();
	zassert_equal(sys_get_be16(&captured_management_request.packet[4]),
		      meshbus_ManagementGroupId_MANAGEMENT_GROUP_ID_MESHBUS_MANAGEMENT,
		      "password command group");
	zassert_equal(captured_management_request.packet[7],
		      meshbus_ManagementCommandId_MANAGEMENT_COMMAND_ID_SECRET,
		      "password command id");
	payload_len = sys_get_be16(&captured_management_request.packet[2]);
	zassert_ok(test_mgmt_cbor_data_decode(
			   &captured_management_request.packet[TEST_MGMT_HDR_SIZE],
			   payload_len, meshbus_ManagementSecretSetRequest_fields,
			   &secret_req, sizeof(secret_req)),
		   "decode password secret request");
	zassert_equal(secret_req.secret.size, sizeof("12345678") - 1U,
		      "app password length mismatch");
	zassert_mem_equal(secret_req.secret.bytes, "12345678",
			  sizeof("12345678") - 1U, "app password bytes");
}

ZTEST(mbs_meshcore_companion_contract, test_cli_data_identity_key_set)
{
	static const char prv_hex[] =
		"7065e18fd9fabb70c1ed90dca19907de"
		"698c88b709ea146eafd93d9b830c7b60"
		"c4681193c79bbc39945ba8064104bb61"
		"8f8fd7a84a0af6f57033d6e8ddcd6471";
	static const uint8_t expected_pub[MBS_MESHCORE_PUBLIC_KEY_SIZE] = {
		0x1e, 0xc7, 0x71, 0x75, 0xb0, 0x91, 0x8e, 0xd2,
		0x06, 0xf9, 0xae, 0x04, 0xec, 0x13, 0x6d, 0x6d,
		0x5d, 0x43, 0x15, 0xbb, 0x26, 0x30, 0x54, 0x27,
		0xf6, 0x45, 0xb4, 0x92, 0xe9, 0x35, 0x0c, 0x10
	};
	uint8_t public_key[MBS_CONTACT_PUBLIC_KEY_SIZE];
	uint8_t cli[180];
	char payload[160];
	size_t cli_len;
	mbs_management_smp_response_event response = {0};
	meshbus_MeshcoreConfigGetResponse get_rsp =
		meshbus_MeshcoreConfigGetResponse_init_zero;
	meshbus_MeshcoreConfigSetRequest set_req =
		meshbus_MeshcoreConfigSetRequest_init_zero;
	uint16_t payload_len;

	reset_adapter(true);
	companion_test_login_success(public_key);

	(void)snprintk(payload, sizeof(payload), "12|set prv.key %s", prv_hex);
	cli_len = companion_test_cli_frame(cli, sizeof(cli), public_key, payload);
	zassert_ok(meshcore_companion_adapter_rx_frame(cli, cli_len));
	meshcore_companion_adapter_flush();
	get_rsp.has_config = true;
	test_mgmt_response_build(
		&response, captured_management_tag,
		meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
		meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_READ_RSP, &get_rsp,
		meshbus_MeshcoreConfigGetResponse_fields,
		meshbus_MeshcoreConfigGetResponse_size);
	zassert_ok(zbus_chan_pub(&mbs_management_smp_response_chan,
				 &response, K_NO_WAIT));
	payload_len = sys_get_be16(&captured_management_request.packet[2]);
	zassert_ok(test_mgmt_cbor_data_decode(
			   &captured_management_request.packet[TEST_MGMT_HDR_SIZE],
			   payload_len, meshbus_MeshcoreConfigSetRequest_fields,
			   &set_req, sizeof(set_req)),
		   "decode set prv.key request");
	zassert_equal(set_req.config.private_key.size,
		      MBS_MESHCORE_PRIVATE_KEY_SIZE,
		      "private key size");
	zassert_equal(set_req.config.public_key.size,
		      MBS_MESHCORE_PUBLIC_KEY_SIZE,
		      "derived public key size");
	zassert_mem_equal(set_req.config.public_key.bytes, expected_pub,
			  sizeof(expected_pub), "derived public key");

	(void)snprintk(payload, sizeof(payload),
		       "13|set pub.key "
		       "1ec77175b0918ed206f9ae04ec136d6d"
		       "5d4315bb26305427f645b492e9350c10");
	cli_len = companion_test_cli_frame(cli, sizeof(cli), public_key, payload);
	zassert_ok(meshcore_companion_adapter_rx_frame(cli, cli_len));
	meshcore_companion_adapter_flush();
	test_mgmt_response_build(
		&response, captured_management_tag,
		meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
		meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_READ_RSP, &get_rsp,
		meshbus_MeshcoreConfigGetResponse_fields,
		meshbus_MeshcoreConfigGetResponse_size);
	zassert_ok(zbus_chan_pub(&mbs_management_smp_response_chan,
				 &response, K_NO_WAIT));
	payload_len = sys_get_be16(&captured_management_request.packet[2]);
	zassert_ok(test_mgmt_cbor_data_decode(
			   &captured_management_request.packet[TEST_MGMT_HDR_SIZE],
			   payload_len, meshbus_MeshcoreConfigSetRequest_fields,
			   &set_req, sizeof(set_req)),
		   "decode set pub.key request");
	zassert_equal(set_req.config.public_key.size,
		      MBS_MESHCORE_PUBLIC_KEY_SIZE,
		      "public key size");
	zassert_mem_equal(set_req.config.public_key.bytes, expected_pub,
			  sizeof(expected_pub), "public key bytes");
}

ZTEST(mbs_meshcore_companion_contract, test_low_level_meshcore_response_push_frames)
{
	mbs_contact_response_binary_event binary = {0};
	struct mbs_meshcore_raw_data_response_event raw = {0};
	struct mbs_meshcore_control_data_response_event control = {0};
	struct mbs_meshcore_channel_data_response_event channel = {0};

	reset_adapter(true);

	binary.tag = 0x01020304U;
	binary.payload_len = 2U;
	binary.payload[0] = 0xaaU;
	binary.payload[1] = 0xbbU;
	zassert_ok(zbus_chan_pub(&mbs_contact_binary_response_chan, &binary, K_NO_WAIT));
	meshcore_companion_adapter_flush();
	zassert_equal(captured.count, 1U, "binary push count");
	zassert_equal(captured.frame[0][0], TEST_PUSH_CODE_BINARY_RESPONSE,
		      "binary push code");
	zassert_equal(captured.frame[0][1], 0U, "binary reserved byte");
	zassert_equal(get_u32_le(&captured.frame[0][2]), 0x01020304U, "binary tag");
	zassert_mem_equal(&captured.frame[0][6], binary.payload, binary.payload_len,
			  "binary push payload");

	raw.path_len = 1U;
	raw.path[0] = 0x10U;
	raw.payload_len = 4U;
	memcpy(raw.payload, "raw!", 4U);
	raw.has_rx_snr = true;
	raw.rx_snr_q4 = 8;
	zassert_ok(zbus_chan_pub(&mbs_meshcore_raw_data_response_chan, &raw, K_NO_WAIT));
	meshcore_companion_adapter_flush();
	zassert_equal(captured.count, 2U, "raw push count");
	zassert_equal(captured.frame[1][0], TEST_PUSH_CODE_RAW_DATA, "raw push code");
	zassert_equal(captured.frame[1][1], 8U, "raw snr q4");
	zassert_equal(captured.frame[1][3], TEST_CONTACT_PATH_UNKNOWN, "raw reserved path");
	zassert_mem_equal(&captured.frame[1][4], "raw!", 4U, "raw push payload");

	control.path_len = 1U;
	control.path[0] = 0x20U;
	control.payload_len = 2U;
	control.payload[0] = 0x80U;
	control.payload[1] = 0x01U;
	control.has_rx_snr = true;
	control.rx_snr_q4 = -4;
	zassert_ok(zbus_chan_pub(&mbs_meshcore_control_data_response_chan,
				 &control, K_NO_WAIT));
	meshcore_companion_adapter_flush();
	zassert_equal(captured.count, 3U, "control push count");
	zassert_equal(captured.frame[2][0], TEST_PUSH_CODE_CONTROL_DATA,
		      "control push code");
	zassert_equal((int8_t)captured.frame[2][1], -4, "control snr q4");
	zassert_equal(captured.frame[2][3], 1U, "control path len");
	zassert_mem_equal(&captured.frame[2][4], control.payload, control.payload_len,
			  "control payload");

	channel.channel_index = 2U;
	channel.path_len = TEST_CONTACT_PATH_UNKNOWN;
	channel.data_type = 0xffffU;
	channel.payload_len = 3U;
	memcpy(channel.payload, "grp", 3U);
	channel.has_rx_snr = true;
	channel.rx_snr_q4 = 7;
	zassert_ok(zbus_chan_pub(&mbs_meshcore_channel_data_response_chan,
				 &channel, K_NO_WAIT));
	meshcore_companion_adapter_flush();
	zassert_equal(captured.count, 4U, "channel data push count");
	zassert_equal(captured.frame[3][0], TEST_RESP_CODE_CHANNEL_DATA_RECV,
		      "channel data frame code");
	zassert_equal(captured.frame[3][1], 7U, "channel data snr q4");
	zassert_equal(captured.frame[3][4], 2U, "channel data index");
	zassert_equal(captured.frame[3][5], TEST_CONTACT_PATH_UNKNOWN,
		      "channel data path len");
	zassert_equal(captured.frame[3][6], 0xffU, "channel data type low");
	zassert_equal(captured.frame[3][7], 0xffU, "channel data type high");
	zassert_equal(captured.frame[3][8], 3U, "channel data payload len");
	zassert_mem_equal(&captured.frame[3][9], "grp", 3U, "channel data payload");
}

ZTEST(mbs_meshcore_companion_contract, test_advert_push_matches_arduino_wire_shape)
{
	mbs_contact_response_advert_event event = {0};

	reset_adapter(true);

	for (size_t i = 0U; i < sizeof(event.public_key); i++) {
		event.public_key[i] = (uint8_t)(0x70U + i);
	}
	strncpy(event.name, "new advert", sizeof(event.name) - 1U);
	event.role = MBS_CONTACT_ROLE_CHAT;
	event.advert_timestamp = 1775000400U;
	event.has_position = true;
	event.latitude = -1234567;
	event.longitude = 7654321;

	zassert_ok(zbus_chan_pub(&mbs_contact_advert_chan, &event, K_NO_WAIT));
	meshcore_companion_adapter_flush();

	zassert_equal(captured.count, 1U, "existing advert push count mismatch");
	zassert_equal(captured.len[0], 1U + sizeof(event.public_key),
		      "existing advert push len mismatch");
	zassert_equal(captured.frame[0][0], TEST_PUSH_CODE_ADVERT,
		      "existing advert push code mismatch");
	zassert_mem_equal(&captured.frame[0][1], event.public_key, sizeof(event.public_key),
			  "existing advert public key mismatch");

	reset_adapter(true);
	event.is_new = true;
	zassert_ok(zbus_chan_pub(&mbs_contact_advert_chan, &event, K_NO_WAIT));
	meshcore_companion_adapter_flush();

	zassert_equal(captured.count, 1U, "new advert push count mismatch");
	zassert_equal(captured.len[0], TEST_CONTACT_FRAME_SIZE, "new advert frame len mismatch");
	zassert_equal(captured.frame[0][0], TEST_PUSH_CODE_NEW_ADVERT,
		      "new advert push code mismatch");
	zassert_mem_equal(&captured.frame[0][1], event.public_key, sizeof(event.public_key),
			  "new advert public key mismatch");
	zassert_equal(captured.frame[0][33], MBS_CONTACT_ROLE_CHAT,
		      "new advert type mismatch");
	zassert_equal(captured.frame[0][34], 0U, "new advert flags mismatch");
	zassert_equal(captured.frame[0][35], TEST_CONTACT_PATH_UNKNOWN,
		      "new advert should use unknown path");
	for (size_t i = 0U; i < 64U; i++) {
		zassert_equal(captured.frame[0][36U + i], 0U,
			      "new advert path byte %u mismatch", (unsigned int)i);
	}
	zassert_mem_equal(&captured.frame[0][100], "new advert", strlen("new advert"),
			  "new advert name mismatch");
	zassert_equal(get_u32_le(&captured.frame[0][132]), 1775000400U,
		      "new advert timestamp mismatch");
	zassert_equal((int32_t)get_u32_le(&captured.frame[0][136]), -1234567,
		      "new advert latitude mismatch");
	zassert_equal(get_u32_le(&captured.frame[0][140]), 7654321U,
		      "new advert longitude mismatch");
	zassert_not_equal(get_u32_le(&captured.frame[0][144]), 0U,
			  "new advert local lastmod should be nonzero");
}

ZTEST(mbs_meshcore_companion_contract, test_path_updated_push_matches_arduino_wire_shape)
{
	struct mbs_contact_response_path_event event = {0};

	reset_adapter(true);

	event.is_discover = false;
	event.key_prefix[0] = 0x41U;
	event.key_prefix[1] = 0x42U;
	event.key_prefix[2] = 0x43U;
	event.key_prefix[3] = 0x44U;

	zassert_ok(zbus_chan_pub(&mbs_contact_path_response_chan, &event, K_NO_WAIT));
	meshcore_companion_adapter_flush();

	zassert_equal(captured.count, 1U, "path updated push count mismatch");
	zassert_equal(captured.len[0], 1U + MBS_CONTACT_PUBLIC_KEY_SIZE,
		      "path updated push len mismatch");
	zassert_equal(captured.frame[0][0], TEST_PUSH_CODE_PATH_UPDATED,
		      "path updated push code mismatch");
	zassert_mem_equal(&captured.frame[0][1], event.key_prefix,
			  CONFIG_MBS_CONTACT_PREFIX_BYTES,
			  "path updated public key prefix mismatch");
	for (size_t i = CONFIG_MBS_CONTACT_PREFIX_BYTES; i < TEST_APP_PREFIX_SIZE; i++) {
		zassert_equal(captured.frame[0][1 + i],
			      (uint8_t)(0x51U + i - CONFIG_MBS_CONTACT_PREFIX_BYTES),
			      "path updated app prefix byte mismatch");
	}
}

ZTEST(mbs_meshcore_companion_contract, test_ack_push_matches_sent_ack_token)
{
	const uint8_t send_node[] = {
		TEST_CMD_SEND_TXT_MSG, 0U, 7U, 0, 0, 0, 0,
		0xa0U, 0xa1U, 0xa2U, 0xa3U, 0xa4U, 0xa5U,
		'h', 'e', 'l', 'l', 'o',
	};
	struct mbs_message_ack_response_event ack = {0};

	reset_adapter(true);

	zassert_ok(meshcore_companion_adapter_rx_frame(send_node, sizeof(send_node)));
	zassert_equal(captured.frame[0][0], TEST_RESP_CODE_SENT, "send node response mismatch");
	zassert_equal(get_u32_le(&captured.frame[0][2]), 0x11223344U, "ack token mismatch");

	ack.target[0] = 0xaaU;
	ack.target[1] = 0xbbU;
	ack.target[2] = 0xccU;
	ack.target[3] = 0xddU;
	ack.attempt = 7U;
	zassert_ok(zbus_chan_pub(&mbs_message_ack_response_chan, &ack, K_NO_WAIT));

	meshcore_companion_adapter_flush();

	zassert_equal(captured.len[1], 9U, "ACK push length mismatch");
	zassert_equal(captured.frame[1][0], TEST_PUSH_CODE_SEND_CONFIRMED,
		      "ACK push code mismatch");
	zassert_equal(get_u32_le(&captured.frame[1][1]), 0x11223344U,
		      "ACK push token mismatch");
}

ZTEST(mbs_meshcore_companion_contract,
      test_ack_handoff_retries_busy_channel_and_bounds_capacity)
{
	struct mbs_message_ack_response_event event = {0};
	struct mbs_message_ack_response_event observed = {0};
	unsigned int i;

	reset_adapter(true);
	event.target[0] = 0xdeU;
	event.target[1] = 0xadU;
	event.target[2] = 0xbeU;
	event.target[3] = 0xefU;

	zassert_equal(mbs_meshcore_ack_handoff_publish(NULL), -EINVAL,
		      "NULL ACK handoff should fail");
	zassert_ok(zbus_chan_claim(&mbs_message_ack_response_chan,
				   K_NO_WAIT),
		   "failed to claim ACK channel");

	for (i = 0U; i < MBS_MESHCORE_ACK_HANDOFF_QUEUE_DEPTH; i++) {
		event.attempt = (uint8_t)i;
		zassert_ok(mbs_meshcore_ack_handoff_publish(&event),
			   "ACK handoff enqueue %u failed", i);
	}
	event.attempt = 0xffU;
	zassert_equal(mbs_meshcore_ack_handoff_publish(&event), -ENOBUFS,
		      "full ACK handoff queue should reject overflow");
	zassert_ok(zbus_chan_finish(&mbs_message_ack_response_chan),
		   "failed to release ACK channel");

	for (i = 0U; i < 100U; i++) {
		zassert_ok(zbus_chan_read(&mbs_message_ack_response_chan,
					 &observed, K_NO_WAIT),
			   "failed to read ACK channel");
		if (observed.attempt ==
		    MBS_MESHCORE_ACK_HANDOFF_QUEUE_DEPTH - 1U) {
			break;
		}
		k_sleep(K_MSEC(1));
	}

	zassert_equal(observed.attempt,
		      MBS_MESHCORE_ACK_HANDOFF_QUEUE_DEPTH - 1U,
		      "queued ACK events did not drain in order");
	zassert_mem_equal(observed.target, event.target, sizeof(observed.target),
			  "drained ACK target mismatch");
}

ZTEST(mbs_meshcore_companion_contract, test_invalid_rx_frames_fail_without_tx)
{
	uint8_t oversized[MESHCORE_COMPANION_MAX_FRAME_SIZE + 1U] = {0};
	const uint8_t command[] = {0x01};

	reset_adapter(true);

	zassert_equal(meshcore_companion_adapter_rx_frame(NULL, sizeof(command)), -EINVAL,
		      "NULL frame should fail");
	zassert_equal(meshcore_companion_adapter_rx_frame(command, 0U), -EINVAL,
		      "empty frame should fail");
	zassert_equal(meshcore_companion_adapter_rx_frame(oversized, sizeof(oversized)), -EMSGSIZE,
		      "oversized frame should fail");
	zassert_equal(captured.count, 0U, "invalid RX should not emit TX");
}

ZTEST(mbs_meshcore_companion_contract, test_rx_requires_connected_transport)
{
	const uint8_t command[] = {0x01};

	reset_adapter(false);

	zassert_equal(meshcore_companion_adapter_rx_frame(command, sizeof(command)), -ENOTCONN,
		      "RX should fail when no transport is connected");
	zassert_equal(captured.count, 0U, "disconnected RX should not emit TX");
}

ZTEST(mbs_meshcore_companion_contract, test_async_queue_reports_transport_admission)
{
	const uint8_t push_frame[] = {0x80, 0x01, 0x02};

	reset_adapter(true);

	zassert_ok(meshcore_companion_adapter_queue_frame(push_frame, sizeof(push_frame)),
		   "queue frame failed");
	zassert_equal(captured.count, 1U, "transport did not accept the frame");
	zassert_equal(captured.len[0], sizeof(push_frame), "queued frame length mismatch");
	zassert_mem_equal(captured.frame[0], push_frame, sizeof(push_frame),
			  "queued frame payload mismatch");

	capture_fail_code = push_frame[0];
	capture_fail_rc = -ENOSPC;
	capture_fail_remaining = 1U;
	zassert_equal(meshcore_companion_adapter_queue_frame(push_frame, sizeof(push_frame)),
		      -ENOSPC, "transport backpressure did not reach the producer");
	zassert_equal(captured.count, 1U, "rejected frame was accepted");
}

ZTEST(mbs_meshcore_companion_contract, test_disconnect_rejects_async_frames)
{
	const uint8_t push_frame[] = {0x80, 0x03};

	reset_adapter(true);

	zassert_ok(meshcore_companion_adapter_queue_frame(push_frame, sizeof(push_frame)),
		   "queue frame failed");
	meshcore_companion_adapter_disconnected();
	meshcore_companion_adapter_flush();

	zassert_equal(meshcore_companion_adapter_queue_frame(push_frame, sizeof(push_frame)),
		      -ENOTCONN, "disconnected producer was accepted");
	zassert_equal(captured.count, 1U, "disconnected frame reached the transport");
}

ZTEST_SUITE(mbs_meshcore_companion_contract, NULL, NULL, NULL, NULL, NULL);
