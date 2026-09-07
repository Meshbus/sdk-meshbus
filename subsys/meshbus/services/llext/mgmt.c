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
#include <zephyr/meshbus/llext.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/sys/util.h>

#include "common/mgmt.h"
#include "meshbus/llext.pb.h"

LOG_MODULE_REGISTER(meshbus_llext_mgmt, CONFIG_MESHBUS_LLEXT_LOG_LEVEL);

#define MESHBUS_LLEXT_MGMT_PROTO_RSP_MAX_SIZE MESHBUS_MESHBUS_LLEXT_PB_H_MAX_SIZE

#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
static int meshbus_llext_mgmt_translate_error_code(uint16_t err)
{
	return (int)err;
}
#endif

MB_MGMT_CONFIG_GET_HANDLER_DEFINE(
	meshbus_llext_mgmt_config_get, meshbus_LlextConfigGetRequest,
	meshbus_LlextConfigGetResponse, meshbus_llext_config_get,
	meshbus_LlextConfigGetRequest_fields, meshbus_LlextConfigGetResponse_fields,
	MESHBUS_LLEXT_MGMT_PROTO_RSP_MAX_SIZE);

MB_MGMT_CONFIG_SET_HANDLER_DEFINE(
	meshbus_llext_mgmt_config_set, meshbus_LlextConfigSetRequest,
	meshbus_LlextConfigSetResponse, meshbus_llext_config_set,
	meshbus_LlextConfigSetRequest_fields, meshbus_LlextConfigSetResponse_fields,
	MESHBUS_LLEXT_MGMT_PROTO_RSP_MAX_SIZE);

MB_MGMT_CONFIG_RESET_HANDLER_DEFINE(
	meshbus_llext_mgmt_config_reset, meshbus_LlextConfigResetRequest,
	meshbus_LlextConfigResetResponse, meshbus_llext_config_reset, meshbus_llext_config_get,
	meshbus_LlextConfigResetRequest_fields, meshbus_LlextConfigResetResponse_fields,
	MESHBUS_LLEXT_MGMT_PROTO_RSP_MAX_SIZE);

static const struct mgmt_handler meshbus_llext_mgmt_group_handlers[] = {
	[meshbus_LlextMgmtCommandId_LLEXT_MGMT_COMMAND_ID_CONFIG] =
		{meshbus_llext_mgmt_config_get, meshbus_llext_mgmt_config_set},
	[meshbus_LlextMgmtCommandId_LLEXT_MGMT_COMMAND_ID_CONFIG_RESET] =
		{NULL, meshbus_llext_mgmt_config_reset},
};

static struct mgmt_group meshbus_llext_mgmt_group = {
	.mg_handlers = meshbus_llext_mgmt_group_handlers,
	.mg_handlers_count = ARRAY_SIZE(meshbus_llext_mgmt_group_handlers),
	.mg_group_id =
		meshbus_LlextMgmtGroupId_LLEXT_MGMT_GROUP_ID_MESHBUS_LLEXT,
#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
	.mg_translate_error = meshbus_llext_mgmt_translate_error_code,
#endif
#ifdef CONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME
	.mg_group_name = "meshbus llext mgmt",
#endif
};

static void meshbus_llext_mgmt_register_group(void)
{
	mgmt_register_group(&meshbus_llext_mgmt_group);
}

MCUMGR_HANDLER_DEFINE(meshbus_llext_mgmt, meshbus_llext_mgmt_register_group);
