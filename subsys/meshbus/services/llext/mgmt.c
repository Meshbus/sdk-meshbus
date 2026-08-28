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
#define MESHBUS_LLEXT_MGMT_LIST_DEFAULT_LIMIT 4U

#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
static int meshbus_llext_mgmt_translate_error_code(uint16_t err)
{
	return (int)err;
}
#endif

static meshbus_LlextServiceState llext_state_to_proto(
	enum meshbus_llext_state state)
{
	switch (state) {
	case MESHBUS_LLEXT_STATE_DISCOVERED:
		return meshbus_LlextServiceState_LLEXT_SERVICE_STATE_DISCOVERED;
	case MESHBUS_LLEXT_STATE_LOADED:
		return meshbus_LlextServiceState_LLEXT_SERVICE_STATE_LOADED;
	case MESHBUS_LLEXT_STATE_BROUGHT_UP:
		return meshbus_LlextServiceState_LLEXT_SERVICE_STATE_BROUGHT_UP;
	case MESHBUS_LLEXT_STATE_RUNNING:
		return meshbus_LlextServiceState_LLEXT_SERVICE_STATE_RUNNING;
	case MESHBUS_LLEXT_STATE_EXITED:
		return meshbus_LlextServiceState_LLEXT_SERVICE_STATE_EXITED;
	case MESHBUS_LLEXT_STATE_FAULTED:
		return meshbus_LlextServiceState_LLEXT_SERVICE_STATE_FAULTED;
	default:
		return meshbus_LlextServiceState_LLEXT_SERVICE_STATE_UNSPECIFIED;
	}
}

static void llext_service_to_proto(const struct meshbus_llext_service_info *info,
				   meshbus_LlextServiceInfo *proto)
{
	if (info == NULL || proto == NULL) {
		return;
	}

	memset(proto, 0, sizeof(*proto));
	(void)snprintk(proto->id, sizeof(proto->id), "%s", info->id);
	(void)snprintk(proto->name, sizeof(proto->name), "%s", info->name);
	proto->state = llext_state_to_proto(info->state);
	(void)snprintk(proto->version, sizeof(proto->version), "%s", info->version);
	(void)snprintk(proto->path, sizeof(proto->path), "%s", info->path);
	(void)snprintk(proto->description, sizeof(proto->description), "%s",
		       info->description);
	(void)snprintk(proto->edk_version, sizeof(proto->edk_version), "%s",
		       info->edk_version);
	(void)snprintk(proto->target, sizeof(proto->target), "%s", info->target);
	proto->stack_size_bytes = info->stack_size;
	proto->heap_size_bytes = info->heap_size;
	proto->last_error = info->last_error;
}

static void llext_service_summary_to_proto(
	const struct meshbus_llext_service_info *info,
	meshbus_LlextServiceSummary *proto)
{
	if (info == NULL || proto == NULL) {
		return;
	}

	memset(proto, 0, sizeof(*proto));
	(void)snprintk(proto->id, sizeof(proto->id), "%s", info->id);
	(void)snprintk(proto->name, sizeof(proto->name), "%s", info->name);
	proto->state = llext_state_to_proto(info->state);
}

MB_MGMT_CONFIG_GET_HANDLER_DEFINE(
	meshbus_llext_mgmt_config_get, meshbus_LlextConfigGetRequest,
	meshbus_LlextConfigGetResponse, meshbus_llext_config,
	meshbus_llext_config_get, meshbus_LlextConfigGetRequest_fields,
	meshbus_LlextConfigGetResponse_fields,
	MESHBUS_LLEXT_MGMT_PROTO_RSP_MAX_SIZE);

MB_MGMT_CONFIG_SET_HANDLER_DEFINE_NO_VALIDATE(
	meshbus_llext_mgmt_config_set, meshbus_LlextConfigSetRequest,
	meshbus_LlextConfigSetResponse, meshbus_llext_config,
	meshbus_llext_config_set, meshbus_LlextConfigSetRequest_fields,
	meshbus_LlextConfigSetResponse_fields,
	MESHBUS_LLEXT_MGMT_PROTO_RSP_MAX_SIZE);

MB_MGMT_CONFIG_RESET_HANDLER_DEFINE(
	meshbus_llext_mgmt_config_reset, meshbus_LlextConfigResetRequest,
	meshbus_LlextConfigResetResponse, meshbus_llext_config,
	meshbus_llext_config_reset, meshbus_llext_config_get,
	meshbus_LlextConfigResetRequest_fields,
	meshbus_LlextConfigResetResponse_fields,
	MESHBUS_LLEXT_MGMT_PROTO_RSP_MAX_SIZE);

static int meshbus_llext_mgmt_service_list(struct smp_streamer *ctxt)
{
	meshbus_LlextServiceListRequest req =
		meshbus_LlextServiceListRequest_init_zero;
	meshbus_LlextServiceListResponse rsp =
		meshbus_LlextServiceListResponse_init_zero;
	size_t count;
	size_t offset;
	size_t limit;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_LlextServiceListRequest_fields, true);

	if (rc != 0) {
		return rc;
	}
	rc = meshbus_llext_service_count(&count);
	if (rc != 0) {
		return rc;
	}

	offset = req.has_offset ? req.offset : 0U;
	limit = req.has_limit ? req.limit : MESHBUS_LLEXT_MGMT_LIST_DEFAULT_LIMIT;
	limit = MIN(limit, ARRAY_SIZE(rsp.services));
	if (offset > count) {
		offset = count;
	}

	for (size_t index = offset;
	     index < count && rsp.services_count < limit; index++) {
		struct meshbus_llext_service_info info;

		rc = meshbus_llext_service_get(index, &info);
		if (rc != 0) {
			return rc;
		}
		llext_service_summary_to_proto(
			&info, &rsp.services[rsp.services_count++]);
	}

	rsp.total_count = (uint32_t)count;
	rsp.next_offset = (uint32_t)(offset + rsp.services_count);
	return mb_mgmt_encode_proto(ctxt, &rsp,
				    meshbus_LlextServiceListResponse_fields,
				    MESHBUS_LLEXT_MGMT_PROTO_RSP_MAX_SIZE);
}

static int meshbus_llext_mgmt_service_status(struct smp_streamer *ctxt)
{
	meshbus_LlextServiceStatusRequest req =
		meshbus_LlextServiceStatusRequest_init_zero;
	meshbus_LlextServiceStatusResponse rsp =
		meshbus_LlextServiceStatusResponse_init_zero;
	struct meshbus_llext_service_info info;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_LlextServiceStatusRequest_fields, false);

	if (rc != 0) {
		return rc;
	}
	rc = meshbus_llext_service_status(req.service_id, &info);
	if (rc != 0) {
		return rc;
	}

	rsp.has_service = true;
	llext_service_to_proto(&info, &rsp.service);
	return mb_mgmt_encode_proto(ctxt, &rsp,
				    meshbus_LlextServiceStatusResponse_fields,
				    MESHBUS_LLEXT_MGMT_PROTO_RSP_MAX_SIZE);
}

static const struct mgmt_handler meshbus_llext_mgmt_group_handlers[] = {
	[meshbus_LlextMgmtCommandId_LLEXT_MGMT_COMMAND_ID_CONFIG] =
		{meshbus_llext_mgmt_config_get, meshbus_llext_mgmt_config_set},
	[meshbus_LlextMgmtCommandId_LLEXT_MGMT_COMMAND_ID_SERVICE_LIST] =
		{meshbus_llext_mgmt_service_list, NULL},
	[meshbus_LlextMgmtCommandId_LLEXT_MGMT_COMMAND_ID_SERVICE_STATUS] =
		{meshbus_llext_mgmt_service_status, NULL},
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
