/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <pb_decode.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt_defines.h>
#include <zephyr/meshbus/contact.h>
#include <zephyr/meshbus/management.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <zcbor_decode.h>

#include "common/shell.h"
#include "management_priv.h"
#include "meshbus/power.pb.h"
#include "meshbus/radio.pb.h"

#define MANAGEMENT_SHELL_CBOR_STATES 4U

static struct k_spinlock management_shell_smp_lock;

struct management_shell_pending {
	const struct shell *sh;
	uint32_t tag;
	bool password_rotation;
	uint8_t contact_prefix[MESHBUS_MANAGEMENT_CONTACT_PREFIX_BYTES];
	uint8_t password_len;
	uint8_t password[MESHBUS_MANAGEMENT_SECRET_MAX_LEN];
};

struct management_shell_completion {
	bool active;
	struct management_shell_pending pending;
	meshbus_management_smp_response_event event;
	meshbus_contact contact;
};

static struct management_shell_pending management_shell_smp_pending;
static struct management_shell_completion management_shell_smp_completion;

static void management_shell_smp_completion_work_handler(struct k_work *work);
K_WORK_DEFINE(management_shell_smp_completion_work,
	      management_shell_smp_completion_work_handler);

struct management_shell_cbor_envelope {
	bool has_data;
	struct zcbor_string data;
	bool has_rc;
	int64_t rc;
	bool has_err_group;
	uint32_t err_group;
	bool has_rsn;
	struct zcbor_string rsn;
};

struct management_shell_decoder {
	uint16_t group;
	uint8_t command;
	uint8_t op;
	const pb_msgdesc_t *fields;
	size_t msg_size;
	void (*print)(const struct shell *sh, const void *msg);
};

union management_shell_decoder_msg {
	meshbus_ManagementSecretSetResponse management_secret_set;
	meshbus_PowerConfigGetResponse power_config_get;
	meshbus_PowerStatusResponse power_status;
	meshbus_RadioConfigGetResponse radio_config_get;
	meshbus_RadioStatusResponse radio_status;
};

static int management_shell_parse_hex(const char *arg, uint8_t *out, size_t out_size,
				      size_t *out_len)
{
	size_t arg_len;
	size_t len;
	size_t parsed;

	if (arg == NULL || out == NULL || out_len == NULL) {
		return -EINVAL;
	}

	arg_len = strlen(arg);
	if (arg_len == 0U || (arg_len % 2U) != 0U) {
		return -EINVAL;
	}

	len = arg_len / 2U;
	if (len > out_size) {
		return -EINVAL;
	}

	parsed = hex2bin(arg, arg_len, out, len);
	if (parsed != len) {
		return -EINVAL;
	}

	*out_len = len;
	return 0;
}

static int management_shell_cbor_err_decode(zcbor_state_t *zsd,
					    struct management_shell_cbor_envelope *out);

static int management_shell_cbor_envelope_decode(
	const uint8_t *payload, size_t payload_len,
	struct management_shell_cbor_envelope *out)
{
	ZCBOR_STATE_D(zsd, MANAGEMENT_SHELL_CBOR_STATES, payload, payload_len, 1, 0);
	struct zcbor_string key;

	if (payload == NULL || out == NULL) {
		return -EINVAL;
	}

	memset(out, 0, sizeof(*out));
	if (!zcbor_map_start_decode(zsd)) {
		return -EINVAL;
	}

	while (!zcbor_array_at_end(zsd)) {
		if (!zcbor_tstr_decode(zsd, &key)) {
			return -EINVAL;
		}

		if (key.len == 4U && memcmp(key.value, "data", 4U) == 0) {
			if (!zcbor_bstr_decode(zsd, &out->data)) {
				return -EINVAL;
			}
			out->has_data = true;
		} else if (key.len == 2U && memcmp(key.value, "rc", 2U) == 0) {
			if (!zcbor_int64_decode(zsd, &out->rc)) {
				return -EINVAL;
			}
			out->has_rc = true;
		} else if (key.len == 3U && memcmp(key.value, "rsn", 3U) == 0) {
			if (!zcbor_tstr_decode(zsd, &out->rsn)) {
				return -EINVAL;
			}
			out->has_rsn = true;
		} else if (key.len == 3U && memcmp(key.value, "err", 3U) == 0) {
			if (management_shell_cbor_err_decode(zsd, out) != 0) {
				return -EINVAL;
			}
		} else if (!zcbor_any_skip(zsd, NULL)) {
			return -EINVAL;
		}
	}

	return zcbor_map_end_decode(zsd) &&
		       (size_t)(zsd->payload - payload) == payload_len ?
		       0 : -EINVAL;
}

static int management_shell_cbor_err_decode(zcbor_state_t *zsd,
					    struct management_shell_cbor_envelope *out)
{
	struct zcbor_string key;
	uint32_t rc;

	if (out == NULL || !zcbor_map_start_decode(zsd)) {
		return -EINVAL;
	}

	while (!zcbor_array_at_end(zsd)) {
		if (!zcbor_tstr_decode(zsd, &key)) {
			return -EINVAL;
		}

		if (key.len == 5U && memcmp(key.value, "group", 5U) == 0) {
			if (!zcbor_uint32_decode(zsd, &out->err_group)) {
				return -EINVAL;
			}
			out->has_err_group = true;
		} else if (key.len == 2U && memcmp(key.value, "rc", 2U) == 0) {
			if (!zcbor_uint32_decode(zsd, &rc)) {
				return -EINVAL;
			}
			out->rc = rc;
			out->has_rc = true;
		} else if (!zcbor_any_skip(zsd, NULL)) {
			return -EINVAL;
		}
	}

	return zcbor_map_end_decode(zsd) ? 0 : -EINVAL;
}

static int management_shell_proto_decode(const struct zcbor_string *data,
					 const pb_msgdesc_t *fields, void *msg,
					 size_t msg_size)
{
	pb_istream_t stream;

	if (data == NULL || fields == NULL || msg == NULL) {
		return -EINVAL;
	}

	memset(msg, 0, msg_size);
	stream = pb_istream_from_buffer(data->value, data->len);
	if (!pb_decode(&stream, fields, msg)) {
		return -EINVAL;
	}

	return stream.bytes_left == 0U ? 0 : -EINVAL;
}

static const char *management_shell_radio_state_str(meshbus_RadioState state)
{
	switch (state) {
	case meshbus_RadioState_RADIO_STATE_IDLE:
		return "idle";
	case meshbus_RadioState_RADIO_STATE_RECEIVE:
		return "receive";
	case meshbus_RadioState_RADIO_STATE_TRANSMIT:
		return "transmit";
	default:
		return "unknown";
	}
}

static void management_shell_print_radio_config(
	const struct shell *sh, const meshbus_RadioConfig *cfg)
{
	if (cfg == NULL) {
		return;
	}

	shell_print(sh, "Settings:");
	shell_print(sh, "  enabled:       %s", cfg->enabled ? "yes" : "no");
	shell_print(sh, "  frequency:     %llu Hz",
		    (unsigned long long)cfg->frequency);
	shell_print(sh, "  bandwidth:     %u Hz", (unsigned int)cfg->bandwidth);
	shell_print(sh, "  spread_factor: %u",
		    (unsigned int)cfg->spread_factor);
	shell_print(sh, "  coding_rate:   4/%u",
		    (unsigned int)cfg->coding_rate);
	shell_print(sh, "  preamble:      %u sym",
		    (unsigned int)cfg->preamble_length);
	shell_print(sh, "  tx_power:      %d dBm", (int)cfg->tx_power);
	shell_print(sh, "  receive_only:  %s",
		    cfg->receive_only ? "yes" : "no");
	shell_print(sh, "  rx_boosted:    %s",
		    cfg->rx_boosted ? "yes" : "no");
	shell_print(sh, "  crc:           %s", cfg->crc ? "yes" : "no");
	shell_print(sh, "  duty_cycle:    %s",
		    cfg->duty_cycle ? "yes" : "no");
	shell_print(sh, "  duty_rx_time:  %u ms",
		    (unsigned int)cfg->duty_cycle_rx_time);
	shell_print(sh, "  duty_slp_time: %u ms",
		    (unsigned int)cfg->duty_cycle_sleep_time);
}

static void management_shell_print_radio_config_get(
	const struct shell *sh, const void *msg)
{
	const meshbus_RadioConfigGetResponse *rsp = msg;

	if (rsp == NULL || !rsp->has_config) {
		shell_print(sh, "radio_config: missing");
		return;
	}

	management_shell_print_radio_config(sh, &rsp->config);
}

static void management_shell_print_radio_status(
	const struct shell *sh, const void *msg)
{
	const meshbus_RadioStatusResponse *rsp = msg;
	int8_t snr;

	if (rsp == NULL) {
		return;
	}

	snr = rsp->last_snr_q4;
	shell_print(sh, "Status:");
	shell_print(sh, "  Modem state:   %s",
		    management_shell_radio_state_str(rsp->state));
	shell_print(sh, "  Receiving:     %s", rsp->receiving ? "yes" : "no");
	if (rsp->has_config) {
		management_shell_print_radio_config(sh, &rsp->config);
	} else {
		shell_print(sh, "Settings: missing");
	}
	shell_print(sh, "Signal:");
	shell_print(sh, "  RSSI:          %d dBm", (int)rsp->last_rssi_dbm);
	shell_print(sh, "  SNR:           %d.%02u dB", snr / 4,
		    (unsigned int)((snr >= 0 ? snr : -snr) % 4) * 25U);
	shell_print(sh, "  Noise Floor:   %d dBm",
		    (int)rsp->noise_floor_dbm);
	if (rsp->has_rssi_inst_dbm) {
		shell_print(sh, "  RSSI Inst:     %d dBm",
			    (int)rsp->rssi_inst_dbm);
	}
}

static void management_shell_print_management_secret_set(
	const struct shell *sh, const void *msg)
{
	const meshbus_ManagementSecretSetResponse *rsp = msg;

	if (rsp == NULL) {
		shell_print(sh, "management_password: missing");
		return;
	}

	shell_print(sh, "accepted: %s", rsp->accepted ? "true" : "false");
}

static void management_shell_print_power_config(
	const struct shell *sh, const meshbus_PowerConfig *cfg)
{
	if (cfg == NULL) {
		return;
	}

	shell_print(sh, "Settings:");
	shell_print(sh, "  low_voltage_shutdown_timeout:   %u s",
		    (unsigned int)cfg->low_voltage_shutdown_timeout);
	shell_print(sh, "  losing_power_shutdown_timeout:  %u s",
		    (unsigned int)cfg->losing_power_shutdown_timeout);
	shell_print(sh, "  no_connection_shutdown_timeout: %u s",
		    (unsigned int)cfg->no_connection_shutdown_timeout);
}

static void management_shell_print_power_config_get(
	const struct shell *sh, const void *msg)
{
	const meshbus_PowerConfigGetResponse *rsp = msg;

	if (rsp == NULL || !rsp->has_config) {
		shell_print(sh, "power_config: missing");
		return;
	}

	management_shell_print_power_config(sh, &rsp->config);
}

static void management_shell_print_power_status(
	const struct shell *sh, const void *msg)
{
	const meshbus_PowerStatusResponse *rsp = msg;

	if (rsp == NULL) {
		return;
	}

	if (rsp->has_config) {
		management_shell_print_power_config(sh, &rsp->config);
	} else {
		shell_print(sh, "Settings: missing");
	}

	shell_print(sh, "Battery:");
	if (rsp->has_voltage_mv) {
		shell_print(sh, "  voltage: %u mV",
			    (unsigned int)rsp->voltage_mv);
	} else {
		shell_print(sh, "  voltage: NC");
	}
	if (rsp->has_soc_percent) {
		shell_print(sh, "  soc:     %u%%",
			    (unsigned int)rsp->soc_percent);
	} else {
		shell_print(sh, "  soc:     NC");
	}
	if (rsp->has_temperature_dk) {
		int32_t temp_c_x10 = (int32_t)rsp->temperature_dk - 2731;
		uint32_t temp_c_abs_x10 =
			(uint32_t)(temp_c_x10 < 0 ? -temp_c_x10 : temp_c_x10);

		shell_print(sh, "  temp:    %u dK (%s%u.%u C)",
			    (unsigned int)rsp->temperature_dk,
			    temp_c_x10 < 0 ? "-" : "",
			    temp_c_abs_x10 / 10U, temp_c_abs_x10 % 10U);
	} else {
		shell_print(sh, "  temp:    NC");
	}
	shell_print(sh, "  charging:%s", rsp->charging ? " yes" : " no");
	shell_print(sh, "  online:  %s", rsp->online ? " yes" : " no");
}

static const struct management_shell_decoder management_shell_decoders[] = {
	{
		.group = meshbus_RadioMgmtGroupId_RADIO_MGMT_GROUP_ID_MESHBUS_RADIO,
		.command = meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_STATUS,
		.op = MGMT_OP_READ_RSP,
		.fields = meshbus_RadioStatusResponse_fields,
		.msg_size = sizeof(meshbus_RadioStatusResponse),
		.print = management_shell_print_radio_status,
	},
	{
		.group = meshbus_RadioMgmtGroupId_RADIO_MGMT_GROUP_ID_MESHBUS_RADIO,
		.command = meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_CONFIG,
		.op = MGMT_OP_READ_RSP,
		.fields = meshbus_RadioConfigGetResponse_fields,
		.msg_size = sizeof(meshbus_RadioConfigGetResponse),
		.print = management_shell_print_radio_config_get,
	},
	{
		.group = meshbus_ManagementGroupId_MANAGEMENT_GROUP_ID_MESHBUS_MANAGEMENT,
		.command = meshbus_ManagementCommandId_MANAGEMENT_COMMAND_ID_SECRET,
		.op = MGMT_OP_WRITE_RSP,
		.fields = meshbus_ManagementSecretSetResponse_fields,
		.msg_size = sizeof(meshbus_ManagementSecretSetResponse),
		.print = management_shell_print_management_secret_set,
	},
	{
		.group = meshbus_PowerMgmtGroupId_POWER_MGMT_GROUP_ID_MESHBUS_POWER,
		.command = meshbus_PowerMgmtCommandId_POWER_MGMT_COMMAND_ID_STATUS,
		.op = MGMT_OP_READ_RSP,
		.fields = meshbus_PowerStatusResponse_fields,
		.msg_size = sizeof(meshbus_PowerStatusResponse),
		.print = management_shell_print_power_status,
	},
	{
		.group = meshbus_PowerMgmtGroupId_POWER_MGMT_GROUP_ID_MESHBUS_POWER,
		.command = meshbus_PowerMgmtCommandId_POWER_MGMT_COMMAND_ID_CONFIG,
		.op = MGMT_OP_READ_RSP,
		.fields = meshbus_PowerConfigGetResponse_fields,
		.msg_size = sizeof(meshbus_PowerConfigGetResponse),
		.print = management_shell_print_power_config_get,
	},
};

static const struct management_shell_decoder *management_shell_decoder_find(
	uint16_t group, uint8_t command, uint8_t op)
{
	for (size_t i = 0U; i < ARRAY_SIZE(management_shell_decoders); i++) {
		if (management_shell_decoders[i].group == group &&
		    management_shell_decoders[i].command == command &&
		    management_shell_decoders[i].op == op) {
			return &management_shell_decoders[i];
		}
	}

	return NULL;
}

static void management_shell_payload_print(const struct shell *sh,
					   const uint8_t *payload,
					   size_t payload_len)
{
	if (payload == NULL || payload_len == 0U) {
		shell_print(sh, "payload: empty");
		return;
	}

	shell_print(sh, "payload_hex:");
	shell_hexdump(sh, payload, payload_len);
}

static bool management_shell_known_payload_print(
	const struct shell *sh, const struct management_shell_decoder *decoder,
	const struct management_shell_cbor_envelope *envelope)
{
	union management_shell_decoder_msg msg;

	if (decoder == NULL || envelope == NULL || !envelope->has_data) {
		return false;
	}

	if (management_shell_proto_decode(&envelope->data, decoder->fields, &msg,
					  decoder->msg_size) != 0) {
		shell_print(sh, "decoded: invalid_proto");
		shell_print(sh, "data_len: %u", (unsigned int)envelope->data.len);
		shell_hexdump(sh, envelope->data.value, envelope->data.len);
		management_secure_wipe(&msg, sizeof(msg));
		return true;
	}

	shell_print(sh, "decoded: true");
	decoder->print(sh, &msg);
	management_secure_wipe(&msg, sizeof(msg));
	return true;
}

static void management_shell_smp_payload_print(const struct shell *sh,
					       uint16_t group, uint8_t command,
					       uint8_t op, const uint8_t *payload,
					       size_t payload_len)
{
	const struct management_shell_decoder *decoder;
	struct management_shell_cbor_envelope envelope;

	if (payload_len == 0U) {
		shell_print(sh, "payload: empty");
		return;
	}
	if (management_shell_cbor_envelope_decode(payload, payload_len,
						  &envelope) != 0) {
		shell_print(sh, "payload: invalid_cbor");
		management_shell_payload_print(sh, payload, payload_len);
		return;
	}

	if (envelope.has_rc || envelope.has_rsn) {
		shell_print(sh, "error: true");
		if (envelope.has_err_group) {
			shell_print(sh, "err_group: %u",
				    (unsigned int)envelope.err_group);
		}
		if (envelope.has_rc) {
			shell_print(sh, "rc: %lld", (long long)envelope.rc);
		}
		if (envelope.has_rsn) {
			shell_print(sh, "rsn: %.*s", (int)envelope.rsn.len,
				    (const char *)envelope.rsn.value);
		}
		if (!envelope.has_data) {
			return;
		}
	}

	decoder = management_shell_decoder_find(group, command, op);
	if (management_shell_known_payload_print(sh, decoder, &envelope)) {
		return;
	}

	if (envelope.has_data) {
		shell_print(sh, "data_len: %u", (unsigned int)envelope.data.len);
		shell_print(sh, "data_hex:");
		shell_hexdump(sh, envelope.data.value, envelope.data.len);
		return;
	}

	shell_print(sh, "payload: cbor_without_data");
	management_shell_payload_print(sh, payload, payload_len);
}

static void management_shell_smp_response_print(
	const struct shell *sh,
	const meshbus_management_smp_response_event *event)
{
	const uint8_t *response;
	uint16_t payload_len;
	uint16_t group;

	if (sh == NULL || event == NULL) {
		return;
	}

	shell_print(sh, "");
	shell_print(sh, "management_smp:");
	shell_print(sh, "tag: %u", (unsigned int)event->tag);
	shell_print(sh, "status: %d", event->status);
	if (event->response_len == 0U) {
		shell_print(sh, "response: %s",
			    event->status == -ETIMEDOUT ? "timeout" : "failed");
		return;
	}
	if (event->response_len < MGMT_HDR_SIZE) {
		shell_print(sh, "response: invalid");
		shell_print(sh, "response_len: %u",
			    (unsigned int)event->response_len);
		return;
	}

	response = event->response;
	payload_len = sys_get_be16(&response[2]);
	group = sys_get_be16(&response[4]);

	shell_print(sh, "response: true");
	shell_print(sh, "response_len: %u", (unsigned int)event->response_len);
	shell_print(sh, "op: %u", (unsigned int)response[0]);
	shell_print(sh, "flags: %u", (unsigned int)response[1]);
	shell_print(sh, "payload_len: %u", (unsigned int)payload_len);
	shell_print(sh, "group: %u", (unsigned int)group);
	shell_print(sh, "seq: %u", (unsigned int)response[6]);
	shell_print(sh, "command: %u", (unsigned int)response[7]);
	if ((size_t)payload_len + MGMT_HDR_SIZE != event->response_len) {
		shell_print(sh, "payload: invalid_len");
		management_shell_payload_print(sh, &response[MGMT_HDR_SIZE],
					       event->response_len - MGMT_HDR_SIZE);
		return;
	}

	management_shell_smp_payload_print(sh, group, response[7], response[0],
					   &response[MGMT_HDR_SIZE], payload_len);
}

static bool management_shell_password_set_accepted(
	const meshbus_management_smp_response_event *event)
{
	meshbus_ManagementSecretSetResponse rsp =
		meshbus_ManagementSecretSetResponse_init_zero;
	struct management_shell_cbor_envelope envelope;
	const uint8_t *response;
	uint16_t payload_len;
	bool accepted = false;

	if (event == NULL || event->status != 0 ||
	    event->response_len < MGMT_HDR_SIZE) {
		return false;
	}

	response = event->response;
	payload_len = sys_get_be16(&response[2]);
	if ((size_t)payload_len + MGMT_HDR_SIZE != event->response_len ||
	    response[0] != MGMT_OP_WRITE_RSP ||
	    sys_get_be16(&response[4]) !=
		meshbus_ManagementGroupId_MANAGEMENT_GROUP_ID_MESHBUS_MANAGEMENT ||
	    response[7] !=
		meshbus_ManagementCommandId_MANAGEMENT_COMMAND_ID_SECRET ||
	    management_shell_cbor_envelope_decode(&response[MGMT_HDR_SIZE],
						  payload_len, &envelope) != 0 ||
	    (envelope.has_rc && envelope.rc != 0) || !envelope.has_data ||
	    management_shell_proto_decode(&envelope.data,
					  meshbus_ManagementSecretSetResponse_fields,
					  &rsp, sizeof(rsp)) != 0) {
		goto out;
	}

	accepted = rsp.accepted;

out:
	management_secure_wipe(&rsp, sizeof(rsp));
	return accepted;
}

static void management_shell_smp_completion_work_handler(struct k_work *work)
{
	struct management_shell_completion *completion =
		&management_shell_smp_completion;
	bool remote_updated;
	int rc;
	k_spinlock_key_t key;

	ARG_UNUSED(work);
	management_shell_smp_response_print(completion->pending.sh,
					    &completion->event);

	remote_updated = completion->pending.password_rotation &&
		management_shell_password_set_accepted(&completion->event);
	if (remote_updated) {
		rc = meshbus_contact_find_by_prefix(completion->pending.contact_prefix,
						   &completion->contact);
		if (rc == 0) {
			memset(&completion->contact.management_secret, 0,
			       sizeof(completion->contact.management_secret));
			completion->contact.management_secret.size =
				(pb_size_t)completion->pending.password_len;
			memcpy(completion->contact.management_secret.bytes,
			       completion->pending.password,
			       completion->pending.password_len);
			rc = meshbus_contact_set(completion->pending.contact_prefix,
						 &completion->contact);
		}

		if (rc == 0) {
			shell_print(completion->pending.sh, "contact_updated: true");
		} else {
			shell_error(completion->pending.sh,
				    "remote_password_updated: true");
			shell_error(completion->pending.sh,
				    "contact_updated: false (%d)", rc);
		}
	}

	key = k_spin_lock(&management_shell_smp_lock);
	management_secure_wipe(&management_shell_smp_completion,
			       sizeof(management_shell_smp_completion));
	k_spin_unlock(&management_shell_smp_lock, key);
}

static bool management_shell_smp_busy(void)
{
	bool busy;
	k_spinlock_key_t key;

	key = k_spin_lock(&management_shell_smp_lock);
	busy = management_shell_smp_pending.sh != NULL ||
		management_shell_smp_completion.active;
	k_spin_unlock(&management_shell_smp_lock, key);
	return busy;
}

static void management_shell_smp_pending_set(
	const struct shell *sh, uint32_t tag, const uint8_t *contact_prefix,
	const uint8_t *password, size_t password_len)
{
	k_spinlock_key_t key;

	key = k_spin_lock(&management_shell_smp_lock);
	management_shell_smp_pending.sh = sh;
	management_shell_smp_pending.tag = tag;
	if (contact_prefix != NULL && password != NULL && password_len > 0U &&
	    password_len <= sizeof(management_shell_smp_pending.password)) {
		management_shell_smp_pending.password_rotation = true;
		memcpy(management_shell_smp_pending.contact_prefix, contact_prefix,
		       sizeof(management_shell_smp_pending.contact_prefix));
		management_shell_smp_pending.password_len = (uint8_t)password_len;
		memcpy(management_shell_smp_pending.password, password, password_len);
	}
	k_spin_unlock(&management_shell_smp_lock, key);
}

static void management_shell_smp_response_listener_cb(
	const struct zbus_channel *chan)
{
	const meshbus_management_smp_response_event *event;
	bool submit = false;
	int rc;
	k_spinlock_key_t key;

	event = (const meshbus_management_smp_response_event *)zbus_chan_const_msg(chan);
	if (event == NULL) {
		return;
	}

	key = k_spin_lock(&management_shell_smp_lock);
	if (!management_shell_smp_completion.active &&
	    management_shell_smp_pending.sh != NULL &&
	    management_shell_smp_pending.tag == event->tag) {
		management_shell_smp_completion.active = true;
		management_shell_smp_completion.pending =
			management_shell_smp_pending;
		management_shell_smp_completion.event = *event;
		management_secure_wipe(&management_shell_smp_pending,
				       sizeof(management_shell_smp_pending));
		submit = true;
	}
	k_spin_unlock(&management_shell_smp_lock, key);

	if (!submit) {
		return;
	}

	rc = k_work_submit(&management_shell_smp_completion_work);
	if (rc < 0) {
		key = k_spin_lock(&management_shell_smp_lock);
		management_secure_wipe(&management_shell_smp_completion,
				       sizeof(management_shell_smp_completion));
		k_spin_unlock(&management_shell_smp_lock, key);
	}
}

ZBUS_LISTENER_DEFINE(management_shell_smp_response_listener,
		     management_shell_smp_response_listener_cb);
ZBUS_CHAN_ADD_OBS(meshbus_management_smp_response_chan,
		  management_shell_smp_response_listener, 0);

static int management_shell_parse_prefix(
	const char *arg, uint8_t prefix[MESHBUS_MANAGEMENT_CONTACT_PREFIX_BYTES])
{
	size_t len = 0U;

	return management_shell_parse_hex(arg, prefix,
					  MESHBUS_MANAGEMENT_CONTACT_PREFIX_BYTES,
					  &len) == 0 &&
		       len == MESHBUS_MANAGEMENT_CONTACT_PREFIX_BYTES ?
		       0 : -EINVAL;
}

static int management_shell_smp_submit(
	const struct shell *sh, const meshbus_management_smp_request_event *request)
{
	uint32_t tag = 0U;
	int rc;

	if (management_shell_smp_busy()) {
		mb_shell_error(sh, -EBUSY);
		return -EBUSY;
	}

	rc = meshbus_management_smp_request(request, &tag);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	management_shell_smp_pending_set(sh, tag, NULL, NULL, 0U);
	shell_print(sh, "accepted: true");
	shell_print(sh, "tag: %u", (unsigned int)tag);
	return 0;
}

static int management_shell_smp_submit_with_password(
	const struct shell *sh, const meshbus_management_smp_request_event *request,
	const uint8_t *password, size_t password_len)
{
	uint32_t tag = 0U;
	int rc;

	if (management_shell_smp_busy()) {
		mb_shell_error(sh, -EBUSY);
		return -EBUSY;
	}

	rc = meshbus_management_smp_request_with_secret(request, password,
							 password_len, &tag);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	management_shell_smp_pending_set(sh, tag, NULL, NULL, 0U);
	shell_print(sh, "accepted: true");
	shell_print(sh, "tag: %u", (unsigned int)tag);
	return 0;
}

static int management_shell_parse_smp_op(const char *arg, uint8_t *op)
{
	uint8_t value;
	int rc;

	if (arg == NULL || op == NULL) {
		return -EINVAL;
	}
	if (strcmp(arg, "read") == 0 || strcmp(arg, "r") == 0) {
		*op = MGMT_OP_READ;
		return 0;
	}
	if (strcmp(arg, "write") == 0 || strcmp(arg, "w") == 0) {
		*op = MGMT_OP_WRITE;
		return 0;
	}

	rc = mb_shell_parse_u8_arg(arg, &value);
	if (rc != 0) {
		return rc;
	}
	switch (value) {
	case 0U:
		*op = MGMT_OP_READ;
		return 0;
	case 1U:
	case MGMT_OP_WRITE:
		*op = MGMT_OP_WRITE;
		return 0;
	default:
		return -EINVAL;
	}
}

static int management_shell_smp_packet_build(uint16_t group, uint8_t command,
					     uint8_t op, uint8_t *out,
					     size_t out_size, size_t payload_len,
					     size_t *out_len)
{
	if (out == NULL || out_len == NULL || payload_len > UINT16_MAX ||
	    MGMT_HDR_SIZE + payload_len > out_size) {
		return -EINVAL;
	}

	out[0] = op;
	out[1] = 0U;
	sys_put_be16((uint16_t)payload_len, &out[2]);
	sys_put_be16(group, &out[4]);
	out[6] = 0U;
	out[7] = command;
	*out_len = MGMT_HDR_SIZE + payload_len;
	return 0;
}

static int management_shell_smp_empty_submit(const struct shell *sh, const char *prefix,
					     uint16_t group, uint8_t command)
{
	meshbus_management_smp_request_event request = {0};
	size_t packet_len = 0U;
	int rc;

	if (management_shell_parse_prefix(prefix, request.contact_prefix) != 0) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	rc = management_shell_smp_packet_build(group, command, MGMT_OP_READ,
					       request.packet, sizeof(request.packet),
					       0U, &packet_len);
	if (rc != 0 || packet_len == 0U || packet_len > UINT16_MAX) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}
	request.packet_len = (uint16_t)packet_len;
	return management_shell_smp_submit(sh, &request);
}

static int cmd_management_smp(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_management_smp_request_event request = {0};
	uint16_t group;
	uint8_t command;
	uint8_t op;
	size_t payload_len = 0U;
	size_t packet_len = 0U;
	int rc;

	if (management_shell_parse_prefix(argv[1], request.contact_prefix) != 0 ||
	    mb_shell_parse_u16_arg(argv[2], &group) != 0 ||
	    mb_shell_parse_u8_arg(argv[3], &command) != 0 ||
	    management_shell_parse_smp_op(argv[4], &op) != 0) {
		mb_shell_invalid(sh);
		management_secure_wipe(&request, sizeof(request));
		return -EINVAL;
	}

	if (argc > 5U &&
	    management_shell_parse_hex(argv[5], &request.packet[MGMT_HDR_SIZE],
				       sizeof(request.packet) - MGMT_HDR_SIZE,
				       &payload_len) != 0) {
		mb_shell_invalid(sh);
		management_secure_wipe(&request, sizeof(request));
		return -EINVAL;
	}

	rc = management_shell_smp_packet_build(group, command, op, request.packet,
					       sizeof(request.packet), payload_len,
					       &packet_len);
	if (rc != 0 || packet_len == 0U || packet_len > UINT16_MAX) {
		mb_shell_invalid(sh);
		management_secure_wipe(&request, sizeof(request));
		return -EINVAL;
	}
	request.packet_len = (uint16_t)packet_len;
	rc = management_shell_smp_submit(sh, &request);
	management_secure_wipe(&request, sizeof(request));
	return rc;
}

static int cmd_management_smp_with_password(const struct shell *sh, size_t argc,
					    char **argv)
{
	meshbus_management_smp_request_event request = {0};
	const uint8_t *password = (const uint8_t *)argv[2];
	size_t password_len = strlen(argv[2]);
	uint16_t group;
	uint8_t command;
	uint8_t op;
	size_t payload_len = 0U;
	size_t packet_len = 0U;
	int rc;

	if (management_shell_parse_prefix(argv[1], request.contact_prefix) != 0 ||
	    !management_password_is_valid(password, password_len) ||
	    mb_shell_parse_u16_arg(argv[3], &group) != 0 ||
	    mb_shell_parse_u8_arg(argv[4], &command) != 0 ||
	    management_shell_parse_smp_op(argv[5], &op) != 0) {
		mb_shell_invalid(sh);
		management_secure_wipe(&request, sizeof(request));
		return -EINVAL;
	}

	if (argc > 6U &&
	    management_shell_parse_hex(argv[6], &request.packet[MGMT_HDR_SIZE],
				       sizeof(request.packet) - MGMT_HDR_SIZE,
				       &payload_len) != 0) {
		mb_shell_invalid(sh);
		management_secure_wipe(&request, sizeof(request));
		return -EINVAL;
	}

	rc = management_shell_smp_packet_build(group, command, op, request.packet,
					       sizeof(request.packet), payload_len,
					       &packet_len);
	if (rc != 0 || packet_len == 0U || packet_len > UINT16_MAX) {
		mb_shell_invalid(sh);
		management_secure_wipe(&request, sizeof(request));
		return -EINVAL;
	}
	request.packet_len = (uint16_t)packet_len;
	rc = management_shell_smp_submit_with_password(sh, &request, password,
						       password_len);
	management_secure_wipe(&request, sizeof(request));
	return rc;
}

static int cmd_management_radio_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	return management_shell_smp_empty_submit(
		sh, argv[1],
		meshbus_RadioMgmtGroupId_RADIO_MGMT_GROUP_ID_MESHBUS_RADIO,
		meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_STATUS);
}

static int cmd_management_radio_config_get(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	return management_shell_smp_empty_submit(
		sh, argv[1],
		meshbus_RadioMgmtGroupId_RADIO_MGMT_GROUP_ID_MESHBUS_RADIO,
		meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_CONFIG);
}

static int cmd_management_config_get(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_management_config cfg = meshbus_ManagementConfig_init_zero;
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = meshbus_management_config_get(&cfg);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	shell_print(sh, "password: %s", cfg.secret.size > 0U ? "set" : "not set");
	shell_print(sh, "effective_max_len: %u",
		    (unsigned int)meshbus_management_smp_effective_max_len_get());
	management_secure_wipe(&cfg, sizeof(cfg));
	return 0;
}

static int cmd_management_config_reset(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = meshbus_management_config_reset();
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	shell_print(sh, "ok");
	return 0;
}

static int cmd_management_config_set(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_management_config cfg = meshbus_ManagementConfig_init_zero;
	size_t password_len;
	int rc;

	if (argc != 3U || strcmp(argv[1], "--password") != 0) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	password_len = strlen(argv[2]);
	if (!management_password_is_valid((const uint8_t *)argv[2],
					  password_len)) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	rc = meshbus_management_config_get(&cfg);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}
	cfg.secret.size = (pb_size_t)password_len;
	memcpy(cfg.secret.bytes, argv[2], password_len);
	rc = meshbus_management_config_set(&cfg);
	management_secure_wipe(&cfg, sizeof(cfg));
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	shell_print(sh, "ok");
	return 0;
}

static int cmd_management_config(const struct shell *sh, size_t argc, char **argv)
{
	if (argc == 1U) {
		return cmd_management_config_get(sh, argc, argv);
	}
	if (argc == 2U && strcmp(argv[1], "--reset") == 0) {
		return cmd_management_config_reset(sh, argc, argv);
	}
	if (argc == 3U && strcmp(argv[1], "--password") == 0) {
		return cmd_management_config_set(sh, argc, argv);
	}

	mb_shell_invalid(sh);
	return -EINVAL;
}

static int cmd_management_power_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	return management_shell_smp_empty_submit(
		sh, argv[1],
		meshbus_PowerMgmtGroupId_POWER_MGMT_GROUP_ID_MESHBUS_POWER,
		meshbus_PowerMgmtCommandId_POWER_MGMT_COMMAND_ID_STATUS);
}

static int cmd_management_power_config_get(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	return management_shell_smp_empty_submit(
		sh, argv[1],
		meshbus_PowerMgmtGroupId_POWER_MGMT_GROUP_ID_MESHBUS_POWER,
		meshbus_PowerMgmtCommandId_POWER_MGMT_COMMAND_ID_CONFIG);
}

static int cmd_management_password_set(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_management_secret_set_request_event request = {0};
	size_t password_len;
	uint32_t tag = 0U;
	int rc;

	ARG_UNUSED(argc);
	password_len = strlen(argv[2]);
	if (management_shell_parse_prefix(argv[1], request.contact_prefix) != 0 ||
	    !management_password_is_valid((const uint8_t *)argv[2], password_len)) {
		mb_shell_invalid(sh);
		management_secure_wipe(&request, sizeof(request));
		return -EINVAL;
	}
	request.secret_len = (uint8_t)password_len;
	memcpy(request.secret, argv[2], password_len);

	if (management_shell_smp_busy()) {
		mb_shell_error(sh, -EBUSY);
		management_secure_wipe(&request, sizeof(request));
		return -EBUSY;
	}

	rc = meshbus_management_secret_set_request(&request, &tag);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		management_secure_wipe(&request, sizeof(request));
		return rc;
	}
	management_shell_smp_pending_set(sh, tag, request.contact_prefix,
					 request.secret, request.secret_len);
	management_secure_wipe(&request, sizeof(request));
	shell_print(sh, "accepted: true");
	shell_print(sh, "tag: %u", (unsigned int)tag);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(meshbus_management_remote_password_cmds,
	SHELL_CMD_ARG(set, NULL, "<contact_prefix_hex> <new_password>",
		      cmd_management_password_set, 3, 0),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(meshbus_management_remote_radio_config_cmds,
	SHELL_CMD_ARG(get, NULL, "<contact_prefix_hex>",
		      cmd_management_radio_config_get, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(meshbus_management_remote_radio_cmds,
	SHELL_CMD_ARG(status, NULL, "<contact_prefix_hex>",
		      cmd_management_radio_status, 2, 0),
	SHELL_CMD(config, &meshbus_management_remote_radio_config_cmds,
		  "Remote radio config", NULL),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(meshbus_management_remote_power_config_cmds,
	SHELL_CMD_ARG(get, NULL, "<contact_prefix_hex>",
		      cmd_management_power_config_get, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(meshbus_management_remote_power_cmds,
	SHELL_CMD_ARG(status, NULL, "<contact_prefix_hex>",
		      cmd_management_power_status, 2, 0),
	SHELL_CMD(config, &meshbus_management_remote_power_config_cmds,
		  "Remote power config", NULL),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(meshbus_management_remote_cmds,
	SHELL_CMD_ARG(smp, NULL,
		      "<contact_prefix_hex> <group> <command> <read|write|0|1|2> [payload_hex]",
		      cmd_management_smp, 5, 1),
	SHELL_CMD_ARG(smp_with_password, NULL,
		      "<contact_prefix_hex> <password> <group> <command> <read|write|0|1|2> [payload_hex]",
		      cmd_management_smp_with_password, 6, 1),
	SHELL_CMD(password, &meshbus_management_remote_password_cmds,
		  "Remote management password commands", NULL),
	SHELL_CMD(radio, &meshbus_management_remote_radio_cmds,
		  "Remote radio commands", NULL),
	SHELL_CMD(power, &meshbus_management_remote_power_cmds,
		  "Remote power commands", NULL),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(meshbus_management_cmds,
	SHELL_CMD_ARG(config, NULL,
		      "[--password <password>] | [--reset]",
		      cmd_management_config, 1, 2),
	SHELL_CMD(remote, &meshbus_management_remote_cmds,
		  "Remote management commands", NULL),
	SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((meshbus), management, &meshbus_management_cmds,
		 "Management diagnostics", NULL, 0, 0);
