/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/sys/util.h>

#include "mbs_mgmt_internal.h"
#include "meshbus/telemetry.pb.h"

#include <telemetry/telemetry.h>

LOG_MODULE_REGISTER(mbs_telemetry_mgmt, CONFIG_MBS_TELEMETRY_LOG_LEVEL);

#define MBS_TELEMETRY_MGMT_PROTO_RSP_MAX_SIZE                                                  \
	MAX(MAX(MAX(meshbus_TelemetryStatusResponse_size,                                          \
		    meshbus_TelemetryConfigGetResponse_size),                                      \
		MAX(meshbus_TelemetryConfigSetResponse_size,                                       \
		    meshbus_TelemetryConfigResetResponse_size)),                                   \
	    MAX(MAX(meshbus_TelemetryReadResponse_size, meshbus_TelemetryTriggerResponse_size),    \
		meshbus_TelemetryEnableResponse_size))

#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
static int mbs_telemetry_mgmt_translate_error_code(uint16_t err)
{
	return (int)err;
}
#endif

MBS_MGMT_CONFIG_GET_HANDLER_DEFINE(
	mbs_telemetry_mgmt_config_get, meshbus_TelemetryConfigGetRequest,
	meshbus_TelemetryConfigGetResponse, mbs_telemetry_config_get,
	meshbus_TelemetryConfigGetRequest_fields, meshbus_TelemetryConfigGetResponse_fields,
	MBS_TELEMETRY_MGMT_PROTO_RSP_MAX_SIZE);

MBS_MGMT_CONFIG_SET_HANDLER_DEFINE(
	mbs_telemetry_mgmt_config_set, meshbus_TelemetryConfigSetRequest,
	meshbus_TelemetryConfigSetResponse, mbs_telemetry_config_set,
	meshbus_TelemetryConfigSetRequest_fields, meshbus_TelemetryConfigSetResponse_fields,
	MBS_TELEMETRY_MGMT_PROTO_RSP_MAX_SIZE);

MBS_MGMT_CONFIG_RESET_HANDLER_DEFINE(
	mbs_telemetry_mgmt_config_reset, meshbus_TelemetryConfigResetRequest,
	meshbus_TelemetryConfigResetResponse, mbs_telemetry_config_reset,
	mbs_telemetry_config_get, meshbus_TelemetryConfigResetRequest_fields,
	meshbus_TelemetryConfigResetResponse_fields, MBS_TELEMETRY_MGMT_PROTO_RSP_MAX_SIZE);

static int mbs_telemetry_mgmt_status(struct smp_streamer *ctxt)
{
	meshbus_TelemetryStatusRequest req = meshbus_TelemetryStatusRequest_init_zero;
	meshbus_TelemetryStatusResponse rsp = meshbus_TelemetryStatusResponse_init_zero;
	mbs_telemetry_config cfg;
	size_t binding_count;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_TelemetryStatusRequest_fields, true);

	if (rc != 0) {
		return rc;
	}

	rc = mbs_telemetry_config_get(&cfg);
	if (rc != 0) {
		return rc;
	}

	binding_count = mbs_telemetry_bindings_count();
	if (binding_count > ARRAY_SIZE(rsp.channels)) {
		LOG_ERR("Telemetry channels exceed protobuf max qty (%u > %u)",
			(unsigned int)binding_count, (unsigned int)ARRAY_SIZE(rsp.channels));
		return -EOVERFLOW;
	}

	rsp.has_config = true;
	rsp.config = cfg;
	rsp.channels_count = binding_count;

	for (size_t i = 0; i < binding_count; i++) {
		struct mbs_telemetry_binding binding;

		rc = mbs_telemetry_binding_get(i, &binding);
		if (rc != 0) {
			return rc;
		}

		rsp.channels[i] = (meshbus_TelemetryChannelId)binding.chan;
	}

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_TelemetryStatusResponse_fields,
				    MBS_TELEMETRY_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_telemetry_mgmt_read(struct smp_streamer *ctxt)
{
	meshbus_TelemetryReadRequest req = meshbus_TelemetryReadRequest_init_zero;
	meshbus_TelemetryReadResponse rsp = meshbus_TelemetryReadResponse_init_zero;
	struct sensor_value vals[MBS_TELEMETRY_MAX_VALUES] = {0};
	size_t value_count;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_TelemetryReadRequest_fields,
				      false);

	if (rc != 0) {
		return rc;
	}
	if ((uint32_t)req.channel_id >= (uint32_t)SENSOR_CHAN_ALL) {
		return -EINVAL;
	}

	value_count = mbs_telemetry_channel_value_count((enum sensor_channel)req.channel_id);
	value_count = MIN(value_count, MBS_TELEMETRY_MAX_VALUES);
	if (value_count > ARRAY_SIZE(rsp.values)) {
		LOG_ERR("Telemetry values exceed protobuf max qty (%u > %u)",
			(unsigned int)value_count, (unsigned int)ARRAY_SIZE(rsp.values));
		return -EOVERFLOW;
	}

	rc = mbs_telemetry_channel_get((enum sensor_channel)req.channel_id, vals);
	if (rc != 0) {
		return rc;
	}

	rsp.channel_id = req.channel_id;
	rsp.value_count = value_count;
	rsp.values_count = value_count;
	for (size_t i = 0; i < value_count; i++) {
		rsp.values[i].integer = vals[i].val1;
		rsp.values[i].fractional_micro = vals[i].val2;
	}

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_TelemetryReadResponse_fields,
				    MBS_TELEMETRY_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_telemetry_mgmt_trigger(struct smp_streamer *ctxt)
{
	meshbus_TelemetryTriggerRequest req = meshbus_TelemetryTriggerRequest_init_zero;
	meshbus_TelemetryTriggerResponse rsp = meshbus_TelemetryTriggerResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_TelemetryTriggerRequest_fields, true);

	if (rc != 0) {
		return rc;
	}

	rc = mbs_telemetry_sample_trigger();
	if (rc != 0) {
		return rc;
	}

	rsp.triggered = true;

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_TelemetryTriggerResponse_fields,
				    MBS_TELEMETRY_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_telemetry_mgmt_enable(struct smp_streamer *ctxt)
{
	meshbus_TelemetryEnableRequest req = meshbus_TelemetryEnableRequest_init_zero;
	meshbus_TelemetryEnableResponse rsp = meshbus_TelemetryEnableResponse_init_zero;
	mbs_telemetry_config cfg;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_TelemetryEnableRequest_fields, true);

	if (rc != 0) {
		return rc;
	}

	rc = mbs_telemetry_config_get(&cfg);
	if (rc != 0) {
		return rc;
	}

	cfg.enabled = req.enabled;

	rc = mbs_telemetry_config_set(&cfg);
	if (rc != 0) {
		return rc;
	}

	rsp.has_config = true;
	rsp.config = cfg;

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_TelemetryEnableResponse_fields,
				    MBS_TELEMETRY_MGMT_PROTO_RSP_MAX_SIZE);
}

static const struct mgmt_handler mbs_telemetry_mgmt_group_handlers[] = {
	[meshbus_TelemetryMgmtCommandId_TELEMETRY_MGMT_COMMAND_ID_STATUS] =
		{mbs_telemetry_mgmt_status, NULL},
	[meshbus_TelemetryMgmtCommandId_TELEMETRY_MGMT_COMMAND_ID_READ] =
		{mbs_telemetry_mgmt_read, NULL},
	[meshbus_TelemetryMgmtCommandId_TELEMETRY_MGMT_COMMAND_ID_TRIGGER] =
		{NULL, mbs_telemetry_mgmt_trigger},
	[meshbus_TelemetryMgmtCommandId_TELEMETRY_MGMT_COMMAND_ID_CONFIG] =
		{mbs_telemetry_mgmt_config_get, mbs_telemetry_mgmt_config_set},
	[meshbus_TelemetryMgmtCommandId_TELEMETRY_MGMT_COMMAND_ID_CONFIG_RESET] =
		{NULL, mbs_telemetry_mgmt_config_reset},
	[meshbus_TelemetryMgmtCommandId_TELEMETRY_MGMT_COMMAND_ID_ENABLE] =
		{NULL, mbs_telemetry_mgmt_enable},
};

#define MBS_TELEMETRY_MGMT_GROUP_SZ ARRAY_SIZE(mbs_telemetry_mgmt_group_handlers)

static struct mgmt_group mbs_telemetry_mgmt_group = {
	.mg_handlers = mbs_telemetry_mgmt_group_handlers,
	.mg_handlers_count = MBS_TELEMETRY_MGMT_GROUP_SZ,
	.mg_group_id = meshbus_TelemetryMgmtGroupId_TELEMETRY_MGMT_GROUP_ID_MESHBUS_TELEMETRY,
#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
	.mg_translate_error = mbs_telemetry_mgmt_translate_error_code,
#endif
#ifdef CONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME
	.mg_group_name = "meshbus telemetry mgmt",
#endif
};

static void mbs_telemetry_mgmt_register_group(void)
{
	mgmt_register_group(&mbs_telemetry_mgmt_group);
}

MCUMGR_HANDLER_DEFINE(mbs_telemetry_mgmt, mbs_telemetry_mgmt_register_group);
