/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/meshbus/clock.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/sys/clock.h>

#include "common/mgmt.h"
#include "meshbus/clock.pb.h"

LOG_MODULE_REGISTER(meshbus_clock_mgmt, CONFIG_MESHBUS_CLOCK_LOG_LEVEL);

#define MESHBUS_CLOCK_MGMT_PROTO_RSP_MAX_SIZE                                                   \
	MAX(MAX(MAX(meshbus_ClockStatusResponse_size, meshbus_ClockConfigGetResponse_size),    \
		MAX(meshbus_ClockConfigSetResponse_size, meshbus_ClockConfigResetResponse_size)), \
	    meshbus_ClockTimeSetResponse_size)

#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
static int meshbus_clock_mgmt_translate_error_code(uint16_t err)
{
	return (int)err;
}
#endif

static int meshbus_clock_mgmt_status(struct smp_streamer *ctxt)
{
	meshbus_ClockStatusRequest req = meshbus_ClockStatusRequest_init_zero;
	meshbus_ClockStatusResponse rsp = meshbus_ClockStatusResponse_init_zero;
	meshbus_clock_config cfg;
	struct timespec ts;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_ClockStatusRequest_fields,
				      true);

	if (rc != 0) {
		return rc;
	}

	rc = meshbus_clock_config_get(&cfg);
	if (rc != 0) {
		return rc;
	}

	rsp.has_config = true;
	rsp.config = cfg;
	rsp.uptime_ms = (uint64_t)k_uptime_get();
	if (sys_clock_gettime(SYS_CLOCK_REALTIME, &ts) == 0 && ts.tv_sec >= 0 &&
	    ts.tv_nsec >= 0 && ts.tv_nsec < 1000000000L) {
		rsp.has_unix_time_ms = true;
		rsp.unix_time_ms = ((uint64_t)ts.tv_sec * 1000ULL) +
				   (uint64_t)(ts.tv_nsec / 1000000L);
		rsp.has_unix_time_s = true;
		rsp.unix_time_s = (uint64_t)ts.tv_sec;
		rsp.has_nanosecond = true;
		rsp.nanosecond = (uint32_t)ts.tv_nsec;
	}

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_ClockStatusResponse_fields,
				    MESHBUS_CLOCK_MGMT_PROTO_RSP_MAX_SIZE);
}

MB_MGMT_CONFIG_GET_HANDLER_DEFINE(
	meshbus_clock_mgmt_config_get, meshbus_ClockConfigGetRequest,
	meshbus_ClockConfigGetResponse, meshbus_clock_config_get,
	meshbus_ClockConfigGetRequest_fields, meshbus_ClockConfigGetResponse_fields,
	MESHBUS_CLOCK_MGMT_PROTO_RSP_MAX_SIZE);

MB_MGMT_CONFIG_SET_HANDLER_DEFINE(
	meshbus_clock_mgmt_config_set, meshbus_ClockConfigSetRequest,
	meshbus_ClockConfigSetResponse, meshbus_clock_config_set,
	meshbus_ClockConfigSetRequest_fields, meshbus_ClockConfigSetResponse_fields,
	MESHBUS_CLOCK_MGMT_PROTO_RSP_MAX_SIZE);

MB_MGMT_CONFIG_RESET_HANDLER_DEFINE(
	meshbus_clock_mgmt_config_reset, meshbus_ClockConfigResetRequest,
	meshbus_ClockConfigResetResponse, meshbus_clock_config_reset, meshbus_clock_config_get,
	meshbus_ClockConfigResetRequest_fields, meshbus_ClockConfigResetResponse_fields,
	MESHBUS_CLOCK_MGMT_PROTO_RSP_MAX_SIZE);

static int meshbus_clock_mgmt_time_set(struct smp_streamer *ctxt)
{
	meshbus_ClockTimeSetRequest req = meshbus_ClockTimeSetRequest_init_zero;
	meshbus_ClockTimeSetResponse rsp = meshbus_ClockTimeSetResponse_init_zero;
	uint64_t applied_unix_time_ms = 0U;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_ClockTimeSetRequest_fields,
				      false);

	if (rc != 0) {
		return rc;
	}

	rc = meshbus_clock_time_set_unix_ms(req.unix_time_ms, &applied_unix_time_ms);
	if (rc != 0) {
		return rc;
	}

	rsp.accepted = true;
	rsp.unix_time_ms = applied_unix_time_ms;

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_ClockTimeSetResponse_fields,
				    MESHBUS_CLOCK_MGMT_PROTO_RSP_MAX_SIZE);
}

static const struct mgmt_handler meshbus_clock_mgmt_group_handlers[] = {
	[meshbus_ClockMgmtCommandId_CLOCK_MGMT_COMMAND_ID_CONFIG] =
		{meshbus_clock_mgmt_config_get, meshbus_clock_mgmt_config_set},
	[meshbus_ClockMgmtCommandId_CLOCK_MGMT_COMMAND_ID_STATUS] =
		{meshbus_clock_mgmt_status, NULL},
	[meshbus_ClockMgmtCommandId_CLOCK_MGMT_COMMAND_ID_CONFIG_RESET] =
		{NULL, meshbus_clock_mgmt_config_reset},
	[meshbus_ClockMgmtCommandId_CLOCK_MGMT_COMMAND_ID_TIME_SET] =
		{NULL, meshbus_clock_mgmt_time_set},
};

#define MESHBUS_CLOCK_MGMT_GROUP_SZ ARRAY_SIZE(meshbus_clock_mgmt_group_handlers)

static struct mgmt_group meshbus_clock_mgmt_group = {
	.mg_handlers = meshbus_clock_mgmt_group_handlers,
	.mg_handlers_count = MESHBUS_CLOCK_MGMT_GROUP_SZ,
	.mg_group_id = meshbus_ClockMgmtGroupId_CLOCK_MGMT_GROUP_ID_MESHBUS_CLOCK,
#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
	.mg_translate_error = meshbus_clock_mgmt_translate_error_code,
#endif
#ifdef CONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME
	.mg_group_name = "meshbus clock mgmt",
#endif
};

static void meshbus_clock_mgmt_register_group(void)
{
	mgmt_register_group(&meshbus_clock_mgmt_group);
}

MCUMGR_HANDLER_DEFINE(meshbus_clock_mgmt, meshbus_clock_mgmt_register_group);
