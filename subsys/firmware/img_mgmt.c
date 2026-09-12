/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt.h>
#include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt_callbacks.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt_defines.h>

#include "firmware_priv.h"

LOG_MODULE_DECLARE(mbs_firmware, CONFIG_MBS_FIRMWARE_LOG_LEVEL);

static enum mgmt_cb_return upload_reject(int error, int32_t *rc,
					 bool *abort_more)
{
	*abort_more = true;
	switch (error) {
	case -EBUSY:
	case -ESTALE:
		*rc = MGMT_ERR_EBUSY;
		break;
	case -EAGAIN:
		*rc = MGMT_ERR_EACCESSDENIED;
		break;
	case -EINVAL:
		*rc = MGMT_ERR_EINVAL;
		break;
	default:
		*rc = MGMT_ERR_EUNKNOWN;
		break;
	}
	return MGMT_CB_ERROR_RC;
}

static enum mgmt_cb_return firmware_img_mgmt_callback(
	uint32_t event, enum mgmt_cb_return prev_status, int32_t *rc,
	uint16_t *group, bool *abort_more, void *data, size_t data_size)
{
	ARG_UNUSED(group);

	if (prev_status != MGMT_CB_OK) {
		return MGMT_CB_OK;
	}

	switch (event) {
	case MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK: {
		const struct img_mgmt_upload_check *check = data;
		const uint8_t *hash;
		int admit_rc;

		if (check == NULL || data_size != sizeof(*check) ||
		    check->req == NULL) {
			return upload_reject(-EINVAL, rc, abort_more);
		}
		if (check->req->off == 0U) {
			if (check->req->data_sha.len !=
			    MBS_FIRMWARE_HASH_SIZE) {
				return upload_reject(-EINVAL, rc, abort_more);
			}
			hash = check->req->data_sha.value;
		} else if (g_img_mgmt_state.data_sha_len ==
			   MBS_FIRMWARE_HASH_SIZE) {
			hash = g_img_mgmt_state.data_sha;
		} else {
			return upload_reject(-ESTALE, rc, abort_more);
		}
		admit_rc = mbs_firmware_full_image_upload_admit(
			hash, check->req->off);
		if (admit_rc != 0) {
			LOG_WRN("MCUmgr full-image upload rejected: %d", admit_rc);
			return upload_reject(admit_rc, rc, abort_more);
		}
		break;
	}
	case MGMT_EVT_OP_IMG_MGMT_DFU_PENDING:
		mbs_firmware_full_image_upload_pending();
		break;
	case MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED:
		mbs_firmware_full_image_upload_stopped(-EIO);
		break;
	case MGMT_EVT_OP_IMG_MGMT_DFU_CONFIRMED:
		mbs_firmware_full_image_confirmed();
		break;
	default:
		break;
	}

	return MGMT_CB_OK;
}

static enum mgmt_cb_return firmware_smp_callback(
	uint32_t event, enum mgmt_cb_return prev_status, int32_t *rc,
	uint16_t *group, bool *abort_more, void *data, size_t data_size)
{
	const struct mgmt_evt_op_cmd_arg *command = data;
	int admit_rc;

	ARG_UNUSED(group);
	if (prev_status != MGMT_CB_OK || event != MGMT_EVT_OP_CMD_RECV ||
	    command == NULL || data_size != sizeof(*command) ||
	    command->group != MGMT_GROUP_ID_IMAGE ||
	    command->id != IMG_MGMT_ID_STATE || command->op != MGMT_OP_WRITE) {
		return MGMT_CB_OK;
	}

	admit_rc = mbs_firmware_full_image_state_write_admit();
	if (admit_rc != 0) {
		LOG_WRN("MCUmgr image-state write rejected: %d", admit_rc);
		return upload_reject(admit_rc, rc, abort_more);
	}
	return MGMT_CB_OK;
}

static struct mgmt_callback firmware_img_mgmt_events = {
	.callback = firmware_img_mgmt_callback,
	.event_id = MGMT_EVT_OP_IMG_MGMT_ALL,
};

static struct mgmt_callback firmware_smp_events = {
	.callback = firmware_smp_callback,
	.event_id = MGMT_EVT_OP_CMD_RECV,
};

static int firmware_img_mgmt_init(void)
{
	mgmt_callback_register(&firmware_img_mgmt_events);
	mgmt_callback_register(&firmware_smp_events);
	return 0;
}

SYS_INIT(firmware_img_mgmt_init, APPLICATION,
	 CONFIG_MBS_FIRMWARE_INIT_PRIORITY);
