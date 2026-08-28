/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/sys/util.h>

#include "common/mgmt.h"
#include "meshbus/radio.pb.h"

#include <zephyr/meshbus/radio.h>

LOG_MODULE_REGISTER(meshbus_radio_mgmt, CONFIG_MESHBUS_RADIO_LOG_LEVEL);

#define MESHBUS_RADIO_MGMT_PROTO_RSP_MAX_SIZE                                                      \
	MAX(MAX(MAX(meshbus_RadioStatusResponse_size, meshbus_RadioConfigGetResponse_size),        \
		MAX(meshbus_RadioConfigSetResponse_size, meshbus_RadioConfigResetResponse_size)),  \
	    MAX(MAX(meshbus_RadioCalibrateResponse_size, meshbus_RadioAirtimeResponse_size),       \
		MAX(MAX(meshbus_RadioScoreResponse_size, meshbus_RadioAgcResetResponse_size),      \
		    MAX(meshbus_RadioEnableResponse_size,                                          \
			MAX(meshbus_RadioSendResponse_size,                                        \
			    meshbus_RadioContinuousWaveResponse_size)))))
#define MESHBUS_RADIO_CALIBRATE_DEFAULT_THRESHOLD_DB 14

#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
static int meshbus_radio_mgmt_translate_error_code(uint16_t err)
{
	return (int)err;
}
#endif

static int meshbus_radio_mgmt_status(struct smp_streamer *ctxt)
{
	meshbus_radio_config cfg;
	struct meshbus_radio_status status;
	meshbus_RadioStatusResponse rsp = meshbus_RadioStatusResponse_init_zero;
	int rc = meshbus_radio_config_get(&cfg);

	if (rc != 0) {
		return rc;
	}
	rc = meshbus_radio_status_get(&status);
	if (rc != 0) {
		return rc;
	}

	rsp.has_config = true;
	rsp.config = cfg;
	rsp.state = (meshbus_RadioState)status.state;
	rsp.receiving = status.receiving;
	rsp.last_rssi_dbm = status.last_rssi_dbm;
	rsp.last_snr_q4 = status.last_snr_q4;
	rsp.noise_floor_dbm = status.noise_floor_dbm;
	if (status.has_rssi_inst_dbm) {
		rsp.has_rssi_inst_dbm = true;
		rsp.rssi_inst_dbm = status.rssi_inst_dbm;
	}

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_RadioStatusResponse_fields,
				    MESHBUS_RADIO_MGMT_PROTO_RSP_MAX_SIZE);
}

static int meshbus_radio_mgmt_config_get(struct smp_streamer *ctxt)
{
	meshbus_radio_config cfg;
	meshbus_RadioConfigGetResponse rsp = meshbus_RadioConfigGetResponse_init_zero;
	int rc = meshbus_radio_config_get(&cfg);

	if (rc != 0) {
		return rc;
	}

	rsp.has_config = true;
	rsp.config = cfg;

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_RadioConfigGetResponse_fields,
				    MESHBUS_RADIO_MGMT_PROTO_RSP_MAX_SIZE);
}

MB_MGMT_CONFIG_SET_HANDLER_DEFINE_NO_VALIDATE(
	meshbus_radio_mgmt_config_set, meshbus_RadioConfigSetRequest,
	meshbus_RadioConfigSetResponse, meshbus_radio_config, meshbus_radio_config_set,
	meshbus_RadioConfigSetRequest_fields, meshbus_RadioConfigSetResponse_fields,
	MESHBUS_RADIO_MGMT_PROTO_RSP_MAX_SIZE);

static int meshbus_radio_mgmt_config_reset(struct smp_streamer *ctxt)
{
	meshbus_RadioConfigResetResponse rsp = meshbus_RadioConfigResetResponse_init_zero;
	meshbus_radio_config cfg;
	int rc;

	rc = meshbus_radio_config_reset();
	if (rc != 0) {
		return rc;
	}

	rc = meshbus_radio_config_get(&cfg);
	if (rc != 0) {
		return rc;
	}

	rsp.has_config = true;
	rsp.config = cfg;

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_RadioConfigResetResponse_fields,
				    MESHBUS_RADIO_MGMT_PROTO_RSP_MAX_SIZE);
}

static int meshbus_radio_mgmt_calibrate(struct smp_streamer *ctxt)
{
	meshbus_RadioCalibrateRequest req = meshbus_RadioCalibrateRequest_init_zero;
	meshbus_RadioCalibrateResponse rsp = meshbus_RadioCalibrateResponse_init_zero;
	int32_t threshold = MESHBUS_RADIO_CALIBRATE_DEFAULT_THRESHOLD_DB;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_RadioCalibrateRequest_fields,
				      true);

	if (rc != 0) {
		return rc;
	}

	if (req.has_threshold_db) {
		threshold = req.threshold_db;
	}
	meshbus_radio_noise_calibrate((int16_t)threshold);
	rsp.threshold_db = threshold;

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_RadioCalibrateResponse_fields,
				    meshbus_RadioCalibrateResponse_size);
}

static int meshbus_radio_mgmt_airtime(struct smp_streamer *ctxt)
{
	meshbus_RadioAirtimeRequest req = meshbus_RadioAirtimeRequest_init_zero;
	meshbus_RadioAirtimeResponse rsp = meshbus_RadioAirtimeResponse_init_zero;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_RadioAirtimeRequest_fields,
				      false);

	if (rc != 0) {
		return rc;
	}
	if (req.payload_len > MESHBUS_RADIO_MAX_PAYLOAD) {
		return -EINVAL;
	}

	rsp.airtime_ms = meshbus_radio_airtime((uint16_t)req.payload_len);

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_RadioAirtimeResponse_fields,
				    meshbus_RadioAirtimeResponse_size);
}

static int meshbus_radio_mgmt_score(struct smp_streamer *ctxt)
{
	meshbus_RadioScoreRequest req = meshbus_RadioScoreRequest_init_zero;
	meshbus_RadioScoreResponse rsp = meshbus_RadioScoreResponse_init_zero;
	float score;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_RadioScoreRequest_fields,
				      false);

	if (rc != 0) {
		return rc;
	}
	if (req.payload_len > MESHBUS_RADIO_MAX_PAYLOAD) {
		return -EINVAL;
	}

	score = meshbus_radio_packet_score(req.snr_db, (uint16_t)req.payload_len);
	rsp.score_percent = (uint32_t)(score * 100.0f);

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_RadioScoreResponse_fields,
				    meshbus_RadioScoreResponse_size);
}

static int meshbus_radio_mgmt_agc(struct smp_streamer *ctxt)
{
	meshbus_RadioAgcResetResponse rsp = meshbus_RadioAgcResetResponse_init_zero;

	meshbus_radio_agc_reset();
	rsp.reset = true;

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_RadioAgcResetResponse_fields,
				    meshbus_RadioAgcResetResponse_size);
}

static int meshbus_radio_mgmt_enable(struct smp_streamer *ctxt)
{
	meshbus_RadioEnableRequest req = meshbus_RadioEnableRequest_init_zero;
	meshbus_RadioEnableResponse rsp = meshbus_RadioEnableResponse_init_zero;
	meshbus_radio_config cfg;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_RadioEnableRequest_fields,
				      true);

	if (rc != 0) {
		return rc;
	}

	rc = meshbus_radio_config_get(&cfg);
	if (rc != 0) {
		return rc;
	}

	cfg.enabled = req.enabled;

	rc = meshbus_radio_config_set(&cfg);
	if (rc != 0) {
		return rc;
	}

	rsp.has_config = true;
	rsp.config = cfg;

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_RadioEnableResponse_fields,
				    MESHBUS_RADIO_MGMT_PROTO_RSP_MAX_SIZE);
}

static int meshbus_radio_mgmt_send(struct smp_streamer *ctxt)
{
	meshbus_RadioSendRequest req = meshbus_RadioSendRequest_init_zero;
	meshbus_RadioSendResponse rsp = meshbus_RadioSendResponse_init_zero;
	struct meshbus_radio_publish_event event = {0};
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_RadioSendRequest_fields,
				      false);

	if (rc != 0) {
		return rc;
	}
	if (req.payload.size == 0U || req.payload.size > MESHBUS_RADIO_MAX_PAYLOAD) {
		return -EINVAL;
	}

	memcpy(event.data, req.payload.bytes, req.payload.size);
	event.len = (uint16_t)req.payload.size;

	rc = zbus_chan_pub(&meshbus_radio_publish_chan, &event, K_NO_WAIT);
	if (rc != 0) {
		return rc;
	}

	rsp.accepted_len = event.len;

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_RadioSendResponse_fields,
				    meshbus_RadioSendResponse_size);
}

static int meshbus_radio_mgmt_continuous_wave(struct smp_streamer *ctxt)
{
	meshbus_RadioContinuousWaveRequest req = meshbus_RadioContinuousWaveRequest_init_zero;
	meshbus_RadioContinuousWaveResponse rsp = meshbus_RadioContinuousWaveResponse_init_zero;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_RadioContinuousWaveRequest_fields, false);

	if (rc != 0) {
		return rc;
	}
	if (req.tx_power_dbm < INT8_MIN || req.tx_power_dbm > INT8_MAX ||
	    req.duration_s > UINT16_MAX) {
		return -EINVAL;
	}

	struct meshbus_radio_cw_request_event event = {
		.frequency_hz = req.frequency_hz,
		.duration_s = (uint16_t)req.duration_s,
		.tx_power_dbm = (int8_t)req.tx_power_dbm,
	};
	rc = zbus_chan_pub(&meshbus_radio_cw_chan, &event, K_NO_WAIT);
	if (rc != 0) {
		return rc;
	}

	rsp.started = true;
	rsp.accepted = true;

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_RadioContinuousWaveResponse_fields,
				    meshbus_RadioContinuousWaveResponse_size);
}

static const struct mgmt_handler meshbus_radio_mgmt_group_handlers[] = {
	[meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_STATUS] = {meshbus_radio_mgmt_status,
								     NULL},
	[meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_CONFIG] = {meshbus_radio_mgmt_config_get,
								     meshbus_radio_mgmt_config_set},
	[meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_CONFIG_RESET] =
		{NULL, meshbus_radio_mgmt_config_reset},
	[meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_CALIBRATE] =
		{NULL, meshbus_radio_mgmt_calibrate},
	[meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_AIRTIME] = {meshbus_radio_mgmt_airtime,
								      NULL},
	[meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_SCORE] = {meshbus_radio_mgmt_score, NULL},
	[meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_AGC_RESET] = {NULL,
									meshbus_radio_mgmt_agc},
	[meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_ENABLE] = {NULL,
								     meshbus_radio_mgmt_enable},
	[meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_SEND] = {NULL, meshbus_radio_mgmt_send},
	[meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_CONTINUOUS_WAVE] =
		{NULL, meshbus_radio_mgmt_continuous_wave},
};

#define MESHBUS_RADIO_MGMT_GROUP_SZ ARRAY_SIZE(meshbus_radio_mgmt_group_handlers)

static struct mgmt_group meshbus_radio_mgmt_group = {
	.mg_handlers = meshbus_radio_mgmt_group_handlers,
	.mg_handlers_count = MESHBUS_RADIO_MGMT_GROUP_SZ,
	.mg_group_id = meshbus_RadioMgmtGroupId_RADIO_MGMT_GROUP_ID_MESHBUS_RADIO,
#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
	.mg_translate_error = meshbus_radio_mgmt_translate_error_code,
#endif
#ifdef CONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME
	.mg_group_name = "meshbus radio mgmt",
#endif
};

static void meshbus_radio_mgmt_register_group(void)
{
	mgmt_register_group(&meshbus_radio_mgmt_group);
}

MCUMGR_HANDLER_DEFINE(meshbus_radio_mgmt, meshbus_radio_mgmt_register_group);
