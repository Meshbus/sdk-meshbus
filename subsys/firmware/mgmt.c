/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <firmware/firmware.h>

#include "mbs_mgmt_internal.h"
#include "meshbus/firmware.pb.h"

#define FIRMWARE_RSP_MAX MESHBUS_MESHBUS_FIRMWARE_PB_H_MAX_SIZE

LOG_MODULE_DECLARE(mbs_firmware, CONFIG_MBS_FIRMWARE_LOG_LEVEL);

BUILD_ASSERT((int)meshbus_FirmwareState_FIRMWARE_STATE_ABORTED ==
	     (int)MBS_FIRMWARE_STATE_ABORTED);
BUILD_ASSERT((int)meshbus_FirmwareResult_FIRMWARE_RESULT_INTERNAL ==
	     (int)MBS_FIRMWARE_RESULT_INTERNAL);
BUILD_ASSERT((int)meshbus_FirmwareUpdateKind_FIRMWARE_UPDATE_KIND_FULL_IMAGE ==
	     (int)MBS_FIRMWARE_UPDATE_KIND_FULL_IMAGE);

#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
static int translate_error(uint16_t err)
{
	return (int)err;
}
#endif

static void response_fill(meshbus_FirmwareResponse *rsp,
			  const struct mbs_firmware_status *status)
{
	rsp->result = (meshbus_FirmwareResult)status->result;
	rsp->state = (meshbus_FirmwareState)status->state;
	rsp->update_kind = (meshbus_FirmwareUpdateKind)status->update_kind;
	rsp->transfer_id.size = MBS_FIRMWARE_TRANSFER_ID_SIZE;
	memcpy(rsp->transfer_id.bytes, status->transfer_id,
	       MBS_FIRMWARE_TRANSFER_ID_SIZE);
	rsp->durable_received = status->durable_received;
	rsp->next_offset = status->next_offset;
	rsp->patch_size = status->patch_size;
	rsp->chunk_size = status->chunk_size;
	rsp->retryable = status->retryable;
	rsp->safe_to_receive = status->safe_to_receive;
	rsp->safe_to_apply = status->safe_to_apply;
	rsp->shutdown_pending = status->shutdown_pending;
	rsp->detail = status->detail;
}

static int response_encode(struct smp_streamer *ctxt,
			   const struct mbs_firmware_status *status)
{
	meshbus_FirmwareResponse rsp = meshbus_FirmwareResponse_init_zero;

	response_fill(&rsp, status);
	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_FirmwareResponse_fields,
				    FIRMWARE_RSP_MAX);
}

static void lifecycle_result_ensure(struct mbs_firmware_status *status, int rc)
{
	if (rc == 0) {
		return;
	}
	status->detail = rc;
	if (status->result != MBS_FIRMWARE_RESULT_OK) {
		return;
	}
	switch (rc) {
	case -ENOENT:
	case -EILSEQ:
		status->result = MBS_FIRMWARE_RESULT_CONFLICT;
		break;
	case -EBUSY:
	case -EALREADY:
		status->result = MBS_FIRMWARE_RESULT_BUSY;
		break;
	case -EINVAL:
	case -ENODATA:
	case -EACCES:
		status->result = MBS_FIRMWARE_RESULT_INVALID;
		break;
	default:
		status->result = MBS_FIRMWARE_RESULT_INTERNAL;
		break;
	}
}

static void lifecycle_status_prepare(struct mbs_firmware_status *status)
{
	status->result = MBS_FIRMWARE_RESULT_OK;
	status->detail = 0;
}

static int status_handler(struct smp_streamer *ctxt)
{
	meshbus_FirmwareStatusRequest req = meshbus_FirmwareStatusRequest_init_zero;
	struct mbs_firmware_status status;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_FirmwareStatusRequest_fields, true);

	if (rc != 0) {
		return rc;
	}
	rc = mbs_firmware_status_get(&status);
	if (rc == 0 && req.transfer_id.size != 0U &&
	    (req.transfer_id.size != MBS_FIRMWARE_TRANSFER_ID_SIZE ||
	     memcmp(req.transfer_id.bytes, status.transfer_id,
		    MBS_FIRMWARE_TRANSFER_ID_SIZE) != 0)) {
		status.result = MBS_FIRMWARE_RESULT_CONFLICT;
		status.detail = -ENOENT;
	}
	return rc == 0 ? response_encode(ctxt, &status) : rc;
}

static int begin_handler(struct smp_streamer *ctxt)
{
	meshbus_FirmwareDeltaBeginRequest req = meshbus_FirmwareDeltaBeginRequest_init_zero;
	struct mbs_firmware_status status;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_FirmwareDeltaBeginRequest_fields, true);

	if (rc != 0) {
		LOG_DBG("FIRMWARE begin request decode failed: rc=%d", rc);
		return rc;
	}
	LOG_DBG("FIRMWARE begin request decoded: manifest_len=%u signature_len=%u",
		(unsigned int)req.manifest.size,
		(unsigned int)req.signature.size);
	if (req.signature.size != MBS_FIRMWARE_SIGNATURE_SIZE) {
		LOG_DBG("FIRMWARE begin signature length invalid: got=%u expected=%u",
			(unsigned int)req.signature.size,
			(unsigned int)MBS_FIRMWARE_SIGNATURE_SIZE);
		return -EINVAL;
	}
	rc = mbs_firmware_status_get(&status);
	if (rc != 0) {
		return rc;
	}
	lifecycle_status_prepare(&status);
	rc = mbs_firmware_delta_begin(req.manifest.bytes, req.manifest.size,
				 req.signature.bytes, &status);
	lifecycle_result_ensure(&status, rc);
	return response_encode(ctxt, &status);
}

static int write_handler(struct smp_streamer *ctxt)
{
	meshbus_FirmwareDeltaWriteRequest req = meshbus_FirmwareDeltaWriteRequest_init_zero;
	struct mbs_firmware_status status;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_FirmwareDeltaWriteRequest_fields, true);

	if (rc != 0) {
		return rc;
	}
	if (req.transfer_id.size != MBS_FIRMWARE_TRANSFER_ID_SIZE) {
		return -EINVAL;
	}
	rc = mbs_firmware_status_get(&status);
	if (rc != 0) {
		return rc;
	}
	lifecycle_status_prepare(&status);
	rc = mbs_firmware_delta_write(req.transfer_id.bytes, req.offset,
				 req.data.bytes, req.data.size, &status);
	lifecycle_result_ensure(&status, rc);
	return response_encode(ctxt, &status);
}

typedef int (*transfer_fn_t)(const uint8_t transfer_id[32],
			     struct mbs_firmware_status *status);

static int transfer_handler(struct smp_streamer *ctxt, transfer_fn_t fn)
{
	meshbus_FirmwareTransferRequest req = meshbus_FirmwareTransferRequest_init_zero;
	struct mbs_firmware_status status;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_FirmwareTransferRequest_fields, true);

	if (rc != 0) {
		return rc;
	}
	if (req.transfer_id.size != MBS_FIRMWARE_TRANSFER_ID_SIZE) {
		return -EINVAL;
	}
	rc = mbs_firmware_status_get(&status);
	if (rc != 0) {
		return rc;
	}
	lifecycle_status_prepare(&status);
	rc = fn(req.transfer_id.bytes, &status);
	lifecycle_result_ensure(&status, rc);
	return response_encode(ctxt, &status);
}

static int finish_handler(struct smp_streamer *ctxt)
{
	return transfer_handler(ctxt, mbs_firmware_delta_finish);
}

static int apply_handler(struct smp_streamer *ctxt)
{
	return transfer_handler(ctxt, mbs_firmware_delta_apply);
}

static int activate_handler(struct smp_streamer *ctxt)
{
	return transfer_handler(ctxt, mbs_firmware_delta_activate);
}

static int abort_handler(struct smp_streamer *ctxt)
{
	return transfer_handler(ctxt, mbs_firmware_delta_abort);
}

static const struct mgmt_handler handlers[] = {
	[meshbus_FirmwareMgmtCommandId_FIRMWARE_MGMT_COMMAND_ID_STATUS] =
		{status_handler, NULL},
	[meshbus_FirmwareMgmtCommandId_FIRMWARE_MGMT_COMMAND_ID_DELTA_BEGIN] =
		{NULL, begin_handler},
	[meshbus_FirmwareMgmtCommandId_FIRMWARE_MGMT_COMMAND_ID_DELTA_WRITE] =
		{NULL, write_handler},
	[meshbus_FirmwareMgmtCommandId_FIRMWARE_MGMT_COMMAND_ID_DELTA_FINISH] =
		{NULL, finish_handler},
	[meshbus_FirmwareMgmtCommandId_FIRMWARE_MGMT_COMMAND_ID_DELTA_APPLY] =
		{NULL, apply_handler},
	[meshbus_FirmwareMgmtCommandId_FIRMWARE_MGMT_COMMAND_ID_DELTA_ACTIVATE] =
		{NULL, activate_handler},
	[meshbus_FirmwareMgmtCommandId_FIRMWARE_MGMT_COMMAND_ID_DELTA_ABORT] =
		{NULL, abort_handler},
};

static struct mgmt_group group = {
	.mg_handlers = handlers,
	.mg_handlers_count = ARRAY_SIZE(handlers),
	.mg_group_id = meshbus_FirmwareMgmtGroupId_FIRMWARE_MGMT_GROUP_ID_MESHBUS_FIRMWARE,
#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
	.mg_translate_error = translate_error,
#endif
#ifdef CONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME
	.mg_group_name = "meshbus firmware",
#endif
};

static void register_group(void)
{
	mgmt_register_group(&group);
}

MCUMGR_HANDLER_DEFINE(mbs_firmware_mgmt, register_group);
