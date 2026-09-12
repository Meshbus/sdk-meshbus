/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/sys/util.h>
#include <zui/zui.h>

#include "mbs_mgmt_internal.h"
#include "meshbus/desktop.pb.h"

LOG_MODULE_REGISTER(mbs_desktop_mgmt, CONFIG_MBS_DESKTOP_LOG_LEVEL);

#define MBS_DESKTOP_MGMT_PROTO_RSP_MAX_SIZE meshbus_DesktopStatusResponse_size

#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
static int mbs_desktop_mgmt_translate_error_code(uint16_t err)
{
	return (int)err;
}
#endif

static int mbs_desktop_mgmt_status(struct smp_streamer *ctxt)
{
	meshbus_DesktopStatusRequest req = meshbus_DesktopStatusRequest_init_zero;
	meshbus_DesktopStatusResponse rsp = meshbus_DesktopStatusResponse_init_zero;
	struct zui_runtime_stats stats = {0};
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_DesktopStatusRequest_fields,
				      true);

	if (rc != 0) {
		return rc;
	}

	rc = zui_get_runtime_stats(&stats);
	if (rc != 0) {
		return rc;
	}

	if (!stats.heap_stats_available) {
		return -ENOTSUP;
	}

	rsp.free_bytes = stats.heap_free_bytes;
	rsp.allocated_bytes = stats.heap_allocated_bytes;
	rsp.max_allocated_bytes = stats.heap_max_allocated_bytes;

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_DesktopStatusResponse_fields,
				    MBS_DESKTOP_MGMT_PROTO_RSP_MAX_SIZE);
}

static const struct mgmt_handler mbs_desktop_mgmt_group_handlers[] = {
	[meshbus_DesktopMgmtCommandId_DESKTOP_MGMT_COMMAND_ID_STATUS] =
		{mbs_desktop_mgmt_status, NULL},
};

#define MBS_DESKTOP_MGMT_GROUP_SZ ARRAY_SIZE(mbs_desktop_mgmt_group_handlers)

static struct mgmt_group mbs_desktop_mgmt_group = {
	.mg_handlers = mbs_desktop_mgmt_group_handlers,
	.mg_handlers_count = MBS_DESKTOP_MGMT_GROUP_SZ,
	.mg_group_id = meshbus_DesktopMgmtGroupId_DESKTOP_MGMT_GROUP_ID_MESHBUS_DESKTOP,
#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
	.mg_translate_error = mbs_desktop_mgmt_translate_error_code,
#endif
#ifdef CONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME
	.mg_group_name = "meshbus desktop mgmt",
#endif
};

static void mbs_desktop_mgmt_register_group(void)
{
	mgmt_register_group(&mbs_desktop_mgmt_group);
}

MCUMGR_HANDLER_DEFINE(mbs_desktop_mgmt, mbs_desktop_mgmt_register_group);
