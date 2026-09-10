/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/meshbus/display.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/sys/util.h>

#include "common/mgmt.h"
#include "meshbus/display.pb.h"

LOG_MODULE_REGISTER(meshbus_display_mgmt, CONFIG_MESHBUS_DISPLAY_LOG_LEVEL);

#define MESHBUS_DISPLAY_MGMT_PROTO_RSP_MAX_SIZE                                                    \
	MAX(MAX(meshbus_DisplayStatusResponse_size, meshbus_DisplayConfigGetResponse_size),        \
	    MAX(MAX(meshbus_DisplayConfigSetResponse_size,                                         \
		    meshbus_DisplayConfigResetResponse_size),                                      \
		meshbus_DisplayActiveResponse_size))

#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
static int meshbus_display_mgmt_translate_error_code(uint16_t err)
{
	return (int)err;
}
#endif

static bool mgmt_display_device_ready(void)
{
#if DT_HAS_CHOSEN(zephyr_display)
	const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	return device_is_ready(dev);
#else
	return false;
#endif
}

MB_MGMT_CONFIG_GET_HANDLER_DEFINE(
	meshbus_display_mgmt_config_get, meshbus_DisplayConfigGetRequest,
	meshbus_DisplayConfigGetResponse, meshbus_display_config_get,
	meshbus_DisplayConfigGetRequest_fields, meshbus_DisplayConfigGetResponse_fields,
	MESHBUS_DISPLAY_MGMT_PROTO_RSP_MAX_SIZE);

MB_MGMT_CONFIG_SET_HANDLER_DEFINE(
	meshbus_display_mgmt_config_set, meshbus_DisplayConfigSetRequest,
	meshbus_DisplayConfigSetResponse, meshbus_display_config_set,
	meshbus_DisplayConfigSetRequest_fields, meshbus_DisplayConfigSetResponse_fields,
	MESHBUS_DISPLAY_MGMT_PROTO_RSP_MAX_SIZE);

static int meshbus_display_mgmt_status(struct smp_streamer *ctxt)
{
	meshbus_DisplayStatusRequest req = meshbus_DisplayStatusRequest_init_zero;
	meshbus_DisplayStatusResponse rsp = meshbus_DisplayStatusResponse_init_zero;
	meshbus_display_config cfg;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_DisplayStatusRequest_fields,
				      true);

	if (rc != 0) {
		return rc;
	}

	rc = meshbus_display_config_get(&cfg);
	if (rc != 0) {
		return rc;
	}

	rsp.has_config = true;
	rsp.config = cfg;
	rsp.active = meshbus_display_is_active();
	rsp.device_ready = mgmt_display_device_ready();

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_DisplayStatusResponse_fields,
				    MESHBUS_DISPLAY_MGMT_PROTO_RSP_MAX_SIZE);
}

static int meshbus_display_mgmt_active_set(struct smp_streamer *ctxt)
{
	meshbus_DisplayActiveRequest req = meshbus_DisplayActiveRequest_init_zero;
	meshbus_DisplayActiveResponse rsp = meshbus_DisplayActiveResponse_init_zero;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_DisplayActiveRequest_fields,
				      true);

	if (rc != 0) {
		return rc;
	}

	meshbus_display_active(req.active);
	rsp.active = meshbus_display_is_active();

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_DisplayActiveResponse_fields,
				    MESHBUS_DISPLAY_MGMT_PROTO_RSP_MAX_SIZE);
}

MB_MGMT_CONFIG_RESET_HANDLER_DEFINE(
	meshbus_display_mgmt_config_reset, meshbus_DisplayConfigResetRequest,
	meshbus_DisplayConfigResetResponse, meshbus_display_config_reset,
	meshbus_display_config_get, meshbus_DisplayConfigResetRequest_fields,
	meshbus_DisplayConfigResetResponse_fields, MESHBUS_DISPLAY_MGMT_PROTO_RSP_MAX_SIZE);

static int meshbus_display_mgmt_dump(struct smp_streamer *ctxt)
{
	meshbus_DisplayDumpRequest req = meshbus_DisplayDumpRequest_init_zero;
	meshbus_DisplayDumpResponse rsp = meshbus_DisplayDumpResponse_init_zero;
	struct meshbus_display_dump_chunk chunk;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_DisplayDumpRequest_fields, true);

	if (rc == 0) {
		rc = meshbus_display_dump_read(req.snapshot_id, req.offset, req.length, &chunk);
	}
	switch (rc) {
	case 0:
		break;
	case -EINVAL:
		return MGMT_ERR_EINVAL;
	case -ENOENT:
		return MGMT_ERR_ENOENT;
	case -ENODEV:
		return MGMT_ERR_EBADSTATE;
	case -ENOTSUP:
		return MGMT_ERR_ENOTSUP;
	case -ENOSPC:
		return MGMT_ERR_EMSGSIZE;
	default:
		return MGMT_ERR_EUNKNOWN;
	}
	rsp.snapshot_id = chunk.snapshot_id;
	rsp.offset = chunk.offset;
	rsp.total_size = chunk.total_size;
	rsp.width = chunk.width;
	rsp.height = chunk.height;
	rsp.format = chunk.format;
	rsp.orientation = chunk.orientation;
	rsp.inverted = chunk.inverted;
	rsp.data.size = chunk.data_len;
	BUILD_ASSERT(sizeof(rsp.data.bytes) == sizeof(chunk.data));
	memcpy(rsp.data.bytes, chunk.data, chunk.data_len);
	rc = mb_mgmt_encode_proto(ctxt, &rsp, meshbus_DisplayDumpResponse_fields,
				 meshbus_DisplayDumpResponse_size);
	return rc == 0 ? MGMT_ERR_EOK : (rc == -ENOMEM ? MGMT_ERR_ENOMEM : MGMT_ERR_EUNKNOWN);
}

static const struct mgmt_handler meshbus_display_mgmt_group_handlers[] = {
	[meshbus_DisplayMgmtCommandId_DISPLAY_MGMT_COMMAND_ID_CONFIG] =
		{meshbus_display_mgmt_config_get, meshbus_display_mgmt_config_set},
	[meshbus_DisplayMgmtCommandId_DISPLAY_MGMT_COMMAND_ID_STATUS] =
		{meshbus_display_mgmt_status, NULL},
	[meshbus_DisplayMgmtCommandId_DISPLAY_MGMT_COMMAND_ID_ACTIVE] =
		{NULL, meshbus_display_mgmt_active_set},
	[meshbus_DisplayMgmtCommandId_DISPLAY_MGMT_COMMAND_ID_CONFIG_RESET] =
		{NULL, meshbus_display_mgmt_config_reset},
	[meshbus_DisplayMgmtCommandId_DISPLAY_MGMT_COMMAND_ID_DUMP] =
		{meshbus_display_mgmt_dump, NULL},
};

#define MESHBUS_DISPLAY_MGMT_GROUP_SZ ARRAY_SIZE(meshbus_display_mgmt_group_handlers)

static struct mgmt_group meshbus_display_mgmt_group = {
	.mg_handlers = meshbus_display_mgmt_group_handlers,
	.mg_handlers_count = MESHBUS_DISPLAY_MGMT_GROUP_SZ,
	.mg_group_id = meshbus_DisplayMgmtGroupId_DISPLAY_MGMT_GROUP_ID_MESHBUS_DISPLAY,
#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
	.mg_translate_error = meshbus_display_mgmt_translate_error_code,
#endif
#ifdef CONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME
	.mg_group_name = "meshbus display mgmt",
#endif
};

static void meshbus_display_mgmt_register_group(void)
{
	mgmt_register_group(&meshbus_display_mgmt_group);
}

MCUMGR_HANDLER_DEFINE(meshbus_display_mgmt, meshbus_display_mgmt_register_group);
