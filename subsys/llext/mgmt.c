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
#include <llext/llext.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/sys/util.h>

#include "mbs_mgmt_internal.h"
#include "meshbus/llext.pb.h"

LOG_MODULE_REGISTER(mbs_llext_mgmt, CONFIG_MBS_LLEXT_LOG_LEVEL);

#define MBS_LLEXT_MGMT_PROTO_RSP_MAX_SIZE MESHBUS_MESHBUS_LLEXT_PB_H_MAX_SIZE

#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
static int mbs_llext_mgmt_translate_error_code(uint16_t err)
{
	return (int)err;
}
#endif

MBS_MGMT_CONFIG_GET_HANDLER_DEFINE(
	mbs_llext_mgmt_config_get, meshbus_LlextConfigGetRequest,
	meshbus_LlextConfigGetResponse, mbs_llext_config_get,
	meshbus_LlextConfigGetRequest_fields, meshbus_LlextConfigGetResponse_fields,
	MBS_LLEXT_MGMT_PROTO_RSP_MAX_SIZE);

MBS_MGMT_CONFIG_SET_HANDLER_DEFINE(
	mbs_llext_mgmt_config_set, meshbus_LlextConfigSetRequest,
	meshbus_LlextConfigSetResponse, mbs_llext_config_set,
	meshbus_LlextConfigSetRequest_fields, meshbus_LlextConfigSetResponse_fields,
	MBS_LLEXT_MGMT_PROTO_RSP_MAX_SIZE);

MBS_MGMT_CONFIG_RESET_HANDLER_DEFINE(
	mbs_llext_mgmt_config_reset, meshbus_LlextConfigResetRequest,
	meshbus_LlextConfigResetResponse, mbs_llext_config_reset, mbs_llext_config_get,
	meshbus_LlextConfigResetRequest_fields, meshbus_LlextConfigResetResponse_fields,
	MBS_LLEXT_MGMT_PROTO_RSP_MAX_SIZE);

static int mbs_llext_mgmt_host_info(struct smp_streamer *ctxt)
{
	meshbus_LlextHostInfoRequest req = meshbus_LlextHostInfoRequest_init_zero;
	meshbus_LlextHostInfoResponse rsp = meshbus_LlextHostInfoResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
		meshbus_LlextHostInfoRequest_fields, true);

	if (rc == 0) {
		rc = mbs_llext_host_info_get(&rsp);
	}
	return rc != 0 ? rc : mbs_mgmt_encode_proto(ctxt, &rsp,
		meshbus_LlextHostInfoResponse_fields, meshbus_LlextHostInfoResponse_size);
}

static const struct mgmt_handler mbs_llext_mgmt_group_handlers[] = {
	[meshbus_LlextMgmtCommandId_LLEXT_MGMT_COMMAND_ID_HOST_INFO] =
		{mbs_llext_mgmt_host_info, NULL},
	[meshbus_LlextMgmtCommandId_LLEXT_MGMT_COMMAND_ID_CONFIG] =
		{mbs_llext_mgmt_config_get, mbs_llext_mgmt_config_set},
	[meshbus_LlextMgmtCommandId_LLEXT_MGMT_COMMAND_ID_CONFIG_RESET] =
		{NULL, mbs_llext_mgmt_config_reset},
};

static struct mgmt_group mbs_llext_mgmt_group = {
	.mg_handlers = mbs_llext_mgmt_group_handlers,
	.mg_handlers_count = ARRAY_SIZE(mbs_llext_mgmt_group_handlers),
	.mg_group_id =
		meshbus_LlextMgmtGroupId_LLEXT_MGMT_GROUP_ID_MESHBUS_LLEXT,
#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
	.mg_translate_error = mbs_llext_mgmt_translate_error_code,
#endif
#ifdef CONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME
	.mg_group_name = "meshbus llext mgmt",
#endif
};

static void mbs_llext_mgmt_register_group(void)
{
	mgmt_register_group(&mbs_llext_mgmt_group);
}

MCUMGR_HANDLER_DEFINE(mbs_llext_mgmt, mbs_llext_mgmt_register_group);
