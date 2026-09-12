/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/sys/util.h>

#include "mbs_mgmt_internal.h"
#include "meshbus/indicator.pb.h"

#include <indicator/indicator.h>

LOG_MODULE_REGISTER(mbs_indicator_mgmt, CONFIG_MBS_INDICATOR_LOG_LEVEL);

#define MBS_INDICATOR_MGMT_PROTO_RSP_MAX_SIZE                                                  \
	MAX(MAX(MAX(meshbus_IndicatorStatusResponse_size,                                          \
		    meshbus_IndicatorConfigGetResponse_size),                                      \
		MAX(meshbus_IndicatorConfigSetResponse_size,                                       \
		    meshbus_IndicatorConfigResetResponse_size)),                                   \
	    MAX(MAX(meshbus_IndicatorStopResponse_size, meshbus_IndicatorLightPlayResponse_size),  \
		MAX(MAX(meshbus_IndicatorLightColorResponse_size,                                  \
			meshbus_IndicatorLightStopResponse_size),                                  \
		    MAX(MAX(meshbus_IndicatorBuzzerPlayResponse_size,                              \
			    meshbus_IndicatorBuzzerRtttlResponse_size),                            \
			meshbus_IndicatorBuzzerStopResponse_size))))

#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
static int mbs_indicator_mgmt_translate_error_code(uint16_t err)
{
	return (int)err;
}
#endif

static int mbs_indicator_mgmt_status(struct smp_streamer *ctxt)
{
	meshbus_IndicatorStatusRequest req = meshbus_IndicatorStatusRequest_init_zero;
	meshbus_IndicatorStatusResponse rsp = meshbus_IndicatorStatusResponse_init_zero;
	mbs_indicator_config cfg;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_IndicatorStatusRequest_fields, true);

	if (rc != 0) {
		return rc;
	}

	rc = mbs_indicator_config_get(&cfg);
	if (rc != 0) {
		return rc;
	}

	rsp.has_config = true;
	rsp.config = cfg;
	rsp.light_ready = mbs_indicator_light_is_ready();
	rsp.buzzer_ready = mbs_indicator_buzzer_is_ready();

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_IndicatorStatusResponse_fields,
				    MBS_INDICATOR_MGMT_PROTO_RSP_MAX_SIZE);
}

MBS_MGMT_CONFIG_GET_HANDLER_DEFINE(
	mbs_indicator_mgmt_config_get, meshbus_IndicatorConfigGetRequest,
	meshbus_IndicatorConfigGetResponse, mbs_indicator_config_get,
	meshbus_IndicatorConfigGetRequest_fields, meshbus_IndicatorConfigGetResponse_fields,
	MBS_INDICATOR_MGMT_PROTO_RSP_MAX_SIZE);

MBS_MGMT_CONFIG_SET_HANDLER_DEFINE(
	mbs_indicator_mgmt_config_set, meshbus_IndicatorConfigSetRequest,
	meshbus_IndicatorConfigSetResponse, mbs_indicator_config_set,
	meshbus_IndicatorConfigSetRequest_fields, meshbus_IndicatorConfigSetResponse_fields,
	MBS_INDICATOR_MGMT_PROTO_RSP_MAX_SIZE);

MBS_MGMT_CONFIG_RESET_HANDLER_DEFINE(
	mbs_indicator_mgmt_config_reset, meshbus_IndicatorConfigResetRequest,
	meshbus_IndicatorConfigResetResponse, mbs_indicator_config_reset,
	mbs_indicator_config_get, meshbus_IndicatorConfigResetRequest_fields,
	meshbus_IndicatorConfigResetResponse_fields, MBS_INDICATOR_MGMT_PROTO_RSP_MAX_SIZE);

static int mbs_indicator_mgmt_stop(struct smp_streamer *ctxt)
{
	meshbus_IndicatorStopRequest req = meshbus_IndicatorStopRequest_init_zero;
	meshbus_IndicatorStopResponse rsp = meshbus_IndicatorStopResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_IndicatorStopRequest_fields,
				      true);

	if (rc != 0) {
		return rc;
	}

	(void)mbs_indicator_light_stop();
	mbs_indicator_buzzer_stop();
	rsp.stopped = true;

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_IndicatorStopResponse_fields,
				    MBS_INDICATOR_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_indicator_mgmt_light_play(struct smp_streamer *ctxt)
{
	meshbus_IndicatorLightPlayRequest req = meshbus_IndicatorLightPlayRequest_init_zero;
	meshbus_IndicatorLightPlayResponse rsp = meshbus_IndicatorLightPlayResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_IndicatorLightPlayRequest_fields, false);

	if (rc != 0) {
		return rc;
	}
	rc = mbs_indicator_light_play(req.on_ms, req.off_ms, (uint8_t)req.count);
	if (rc != 0) {
		return rc;
	}

	rsp.started = true;

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_IndicatorLightPlayResponse_fields,
				    MBS_INDICATOR_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_indicator_mgmt_light_color(struct smp_streamer *ctxt)
{
	meshbus_IndicatorLightColorRequest req = meshbus_IndicatorLightColorRequest_init_zero;
	meshbus_IndicatorLightColorResponse rsp = meshbus_IndicatorLightColorResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_IndicatorLightColorRequest_fields, false);

	if (rc != 0) {
		return rc;
	}
	rc = mbs_indicator_light_color((uint8_t)req.r, (uint8_t)req.g, (uint8_t)req.b);
	if (rc != 0) {
		return rc;
	}

	rsp.applied = true;

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_IndicatorLightColorResponse_fields,
				    MBS_INDICATOR_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_indicator_mgmt_light_idle_color(struct smp_streamer *ctxt)
{
	meshbus_IndicatorLightIdleColorRequest req =
		meshbus_IndicatorLightIdleColorRequest_init_zero;
	meshbus_IndicatorLightIdleColorResponse rsp =
		meshbus_IndicatorLightIdleColorResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(
		ctxt, &req, sizeof(req),
		meshbus_IndicatorLightIdleColorRequest_fields, false);

	if (rc != 0) {
		return rc;
	}

	rc = mbs_indicator_light_idle_color((uint8_t)req.r, (uint8_t)req.g,
						 (uint8_t)req.b);
	if (rc != 0) {
		return rc;
	}

	rsp.applied = true;
	return mbs_mgmt_encode_proto(
		ctxt, &rsp, meshbus_IndicatorLightIdleColorResponse_fields,
		MBS_INDICATOR_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_indicator_mgmt_light_idle_pattern(struct smp_streamer *ctxt)
{
	meshbus_IndicatorLightIdlePatternRequest req =
		meshbus_IndicatorLightIdlePatternRequest_init_zero;
	meshbus_IndicatorLightIdlePatternResponse rsp =
		meshbus_IndicatorLightIdlePatternResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(
		ctxt, &req, sizeof(req),
		meshbus_IndicatorLightIdlePatternRequest_fields, false);

	if (rc != 0) {
		return rc;
	}

	rc = mbs_indicator_light_idle(req.on_ms, req.off_ms);
	if (rc != 0) {
		return rc;
	}

	rsp.applied = true;
	return mbs_mgmt_encode_proto(
		ctxt, &rsp, meshbus_IndicatorLightIdlePatternResponse_fields,
		MBS_INDICATOR_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_indicator_mgmt_light_stop(struct smp_streamer *ctxt)
{
	meshbus_IndicatorLightStopRequest req = meshbus_IndicatorLightStopRequest_init_zero;
	meshbus_IndicatorLightStopResponse rsp = meshbus_IndicatorLightStopResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_IndicatorLightStopRequest_fields, true);

	if (rc != 0) {
		return rc;
	}

	rc = mbs_indicator_light_stop();
	if (rc != 0) {
		return rc;
	}

	rsp.stopped = true;

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_IndicatorLightStopResponse_fields,
				    MBS_INDICATOR_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_indicator_mgmt_buzzer_play(struct smp_streamer *ctxt)
{
	meshbus_IndicatorBuzzerPlayRequest req = meshbus_IndicatorBuzzerPlayRequest_init_zero;
	meshbus_IndicatorBuzzerPlayResponse rsp = meshbus_IndicatorBuzzerPlayResponse_init_zero;
	static struct indicator_buzzer_note note;
	static struct indicator_buzzer_melody melody;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_IndicatorBuzzerPlayRequest_fields, false);

	if (rc != 0) {
		return rc;
	}
	if (req.freq_hz == 0U || req.duration_ms == 0U) {
		return -ERANGE;
	}

	note.freq_hz = (uint16_t)req.freq_hz;
	note.duration_ms = (uint16_t)req.duration_ms;
	melody.notes = &note;
	melody.length = 1U;

	rc = mbs_indicator_buzzer_play(INDICATOR_SOURCE_SYSTEM, &melody);
	if (rc != 0) {
		return rc;
	}

	rsp.started = true;

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_IndicatorBuzzerPlayResponse_fields,
				    MBS_INDICATOR_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_indicator_mgmt_buzzer_rtttl(struct smp_streamer *ctxt)
{
	meshbus_IndicatorBuzzerRtttlRequest req = meshbus_IndicatorBuzzerRtttlRequest_init_zero;
	meshbus_IndicatorBuzzerRtttlResponse rsp = meshbus_IndicatorBuzzerRtttlResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_IndicatorBuzzerRtttlRequest_fields, false);

	if (rc != 0) {
		return rc;
	}
	if (req.rtttl[0] == '\0') {
		return -EINVAL;
	}

	rc = mbs_indicator_buzzer_rtttl(req.rtttl);
	if (rc != 0) {
		return rc;
	}

	rsp.started = true;

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_IndicatorBuzzerRtttlResponse_fields,
				    MBS_INDICATOR_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_indicator_mgmt_buzzer_stop(struct smp_streamer *ctxt)
{
	meshbus_IndicatorBuzzerStopRequest req = meshbus_IndicatorBuzzerStopRequest_init_zero;
	meshbus_IndicatorBuzzerStopResponse rsp = meshbus_IndicatorBuzzerStopResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_IndicatorBuzzerStopRequest_fields, true);

	if (rc != 0) {
		return rc;
	}

	mbs_indicator_buzzer_stop();
	rsp.stopped = true;

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_IndicatorBuzzerStopResponse_fields,
				    MBS_INDICATOR_MGMT_PROTO_RSP_MAX_SIZE);
}

static const struct mgmt_handler mbs_indicator_mgmt_group_handlers[] = {
	[meshbus_IndicatorMgmtCommandId_INDICATOR_MGMT_COMMAND_ID_STATUS] =
		{mbs_indicator_mgmt_status, NULL},
	[meshbus_IndicatorMgmtCommandId_INDICATOR_MGMT_COMMAND_ID_STOP] =
		{NULL, mbs_indicator_mgmt_stop},
	[meshbus_IndicatorMgmtCommandId_INDICATOR_MGMT_COMMAND_ID_CONFIG] =
		{mbs_indicator_mgmt_config_get, mbs_indicator_mgmt_config_set},
	[meshbus_IndicatorMgmtCommandId_INDICATOR_MGMT_COMMAND_ID_CONFIG_RESET] =
		{NULL, mbs_indicator_mgmt_config_reset},
	[meshbus_IndicatorMgmtCommandId_INDICATOR_MGMT_COMMAND_ID_LIGHT_PLAY] =
		{NULL, mbs_indicator_mgmt_light_play},
	[meshbus_IndicatorMgmtCommandId_INDICATOR_MGMT_COMMAND_ID_LIGHT_COLOR] =
		{NULL, mbs_indicator_mgmt_light_color},
	[meshbus_IndicatorMgmtCommandId_INDICATOR_MGMT_COMMAND_ID_LIGHT_IDLE_COLOR] = {
		NULL, mbs_indicator_mgmt_light_idle_color
	},
	[meshbus_IndicatorMgmtCommandId_INDICATOR_MGMT_COMMAND_ID_LIGHT_IDLE_PATTERN] = {
		NULL, mbs_indicator_mgmt_light_idle_pattern
	},
	[meshbus_IndicatorMgmtCommandId_INDICATOR_MGMT_COMMAND_ID_LIGHT_STOP] =
		{NULL, mbs_indicator_mgmt_light_stop},
	[meshbus_IndicatorMgmtCommandId_INDICATOR_MGMT_COMMAND_ID_BUZZER_PLAY] =
		{NULL, mbs_indicator_mgmt_buzzer_play},
	[meshbus_IndicatorMgmtCommandId_INDICATOR_MGMT_COMMAND_ID_BUZZER_RTTTL] =
		{NULL, mbs_indicator_mgmt_buzzer_rtttl},
	[meshbus_IndicatorMgmtCommandId_INDICATOR_MGMT_COMMAND_ID_BUZZER_STOP] =
		{NULL, mbs_indicator_mgmt_buzzer_stop},
};

#define MBS_INDICATOR_MGMT_GROUP_SZ ARRAY_SIZE(mbs_indicator_mgmt_group_handlers)

static struct mgmt_group mbs_indicator_mgmt_group = {
	.mg_handlers = mbs_indicator_mgmt_group_handlers,
	.mg_handlers_count = MBS_INDICATOR_MGMT_GROUP_SZ,
	.mg_group_id = meshbus_IndicatorMgmtGroupId_INDICATOR_MGMT_GROUP_ID_MESHBUS_INDICATOR,
#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
	.mg_translate_error = mbs_indicator_mgmt_translate_error_code,
#endif
#ifdef CONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME
	.mg_group_name = "meshbus indicator mgmt",
#endif
};

static void mbs_indicator_mgmt_register_group(void)
{
	mgmt_register_group(&mbs_indicator_mgmt_group);
}

MCUMGR_HANDLER_DEFINE(mbs_indicator_mgmt, mbs_indicator_mgmt_register_group);
