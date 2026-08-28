/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/sys/util.h>

#include "common/mgmt.h"
#include "input.h"
#include "meshbus/input.pb.h"

LOG_MODULE_REGISTER(meshbus_input_mgmt, CONFIG_MESHBUS_INPUT_LOG_LEVEL);

#define MESHBUS_INPUT_MGMT_PROTO_RSP_MAX_SIZE                                                     \
	MAX(MAX(meshbus_InputStatusResponse_size, meshbus_InputInjectActResponse_size),          \
	    meshbus_InputInjectRawResponse_size)

#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
static int meshbus_input_mgmt_translate_error_code(uint16_t err)
{
	return (int)err;
}
#endif

static int meshbus_input_mgmt_validate_type(uint32_t type)
{
	if (type > UINT8_MAX) {
		return -EINVAL;
	}

	return 0;
}

static int meshbus_input_mgmt_validate_code(uint32_t code)
{
	if (code > UINT16_MAX) {
		return -EINVAL;
	}

	return 0;
}

static int meshbus_input_mgmt_validate_action(uint32_t action)
{
	if (action > INPUT_ACT_SCROLL_CCW) {
		return -EINVAL;
	}

	return 0;
}

static int meshbus_input_mgmt_status(struct smp_streamer *ctxt)
{
	meshbus_InputStatusRequest req = meshbus_InputStatusRequest_init_zero;
	meshbus_InputStatusResponse rsp = meshbus_InputStatusResponse_init_zero;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_InputStatusRequest_fields,
				      true);

	if (rc != 0) {
		return rc;
	}

	rsp.button_enabled = IS_ENABLED(CONFIG_MESHBUS_INPUT_BUTTON);
	rsp.encoder_enabled = IS_ENABLED(CONFIG_MESHBUS_INPUT_ENCODER);
	rsp.keypad_enabled = IS_ENABLED(CONFIG_MESHBUS_INPUT_KEYPAD);
	rsp.long_press_ms = CONFIG_MESHBUS_INPUT_LONG_PRESS_MS;
	rsp.repeat_delay_ms = CONFIG_MESHBUS_INPUT_REPEAT_DELAY_MS;
	rsp.repeat_interval_ms = CONFIG_MESHBUS_INPUT_REPEAT_INTERVAL_MS;
	rsp.debounce_ms = CONFIG_MESHBUS_INPUT_DEBOUNCE_MS;
	rsp.encoder_step_threshold = CONFIG_MESHBUS_INPUT_ENCODER_STEP_THRESHOLD;

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_InputStatusResponse_fields,
				    MESHBUS_INPUT_MGMT_PROTO_RSP_MAX_SIZE);
}

static int meshbus_input_mgmt_inject_act(struct smp_streamer *ctxt)
{
	meshbus_InputInjectActRequest req = meshbus_InputInjectActRequest_init_zero;
	meshbus_InputInjectActResponse rsp = meshbus_InputInjectActResponse_init_zero;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_InputInjectActRequest_fields, false);

	if (rc != 0) {
		return rc;
	}

	rc = meshbus_input_mgmt_validate_type(req.type);
	if (rc != 0) {
		return rc;
	}

	rc = meshbus_input_mgmt_validate_code(req.code);
	if (rc != 0) {
		return rc;
	}

	rc = meshbus_input_mgmt_validate_action(req.action);
	if (rc != 0) {
		return rc;
	}

	rc = meshbus_input_action_event_publish((uint8_t)req.type, (uint16_t)req.code,
					     (uint8_t)req.action);
	if (rc != 0) {
		return rc;
	}

	rsp.accepted = true;

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_InputInjectActResponse_fields,
				    MESHBUS_INPUT_MGMT_PROTO_RSP_MAX_SIZE);
}

static int meshbus_input_mgmt_inject_raw(struct smp_streamer *ctxt)
{
	meshbus_InputInjectRawRequest req = meshbus_InputInjectRawRequest_init_zero;
	meshbus_InputInjectRawResponse rsp = meshbus_InputInjectRawResponse_init_zero;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_InputInjectRawRequest_fields, false);

	if (rc != 0) {
		return rc;
	}

	rc = meshbus_input_mgmt_validate_type(req.type);
	if (rc != 0) {
		return rc;
	}

	rc = meshbus_input_mgmt_validate_code(req.code);
	if (rc != 0) {
		return rc;
	}

	rc = meshbus_input_key_event_publish((uint8_t)req.type, (uint16_t)req.code, req.value);
	if (rc != 0) {
		return rc;
	}

	rsp.accepted = true;

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_InputInjectRawResponse_fields,
				    MESHBUS_INPUT_MGMT_PROTO_RSP_MAX_SIZE);
}

static const struct mgmt_handler meshbus_input_mgmt_group_handlers[] = {
	[meshbus_InputMgmtCommandId_INPUT_MGMT_COMMAND_ID_STATUS] =
		{meshbus_input_mgmt_status, NULL},
	[meshbus_InputMgmtCommandId_INPUT_MGMT_COMMAND_ID_INJECT_ACT] =
		{NULL, meshbus_input_mgmt_inject_act},
	[meshbus_InputMgmtCommandId_INPUT_MGMT_COMMAND_ID_INJECT_RAW] =
		{NULL, meshbus_input_mgmt_inject_raw},
};

#define MESHBUS_INPUT_MGMT_GROUP_SZ ARRAY_SIZE(meshbus_input_mgmt_group_handlers)

static struct mgmt_group meshbus_input_mgmt_group = {
	.mg_handlers = meshbus_input_mgmt_group_handlers,
	.mg_handlers_count = MESHBUS_INPUT_MGMT_GROUP_SZ,
	.mg_group_id = meshbus_InputMgmtGroupId_INPUT_MGMT_GROUP_ID_MESHBUS_INPUT,
#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
	.mg_translate_error = meshbus_input_mgmt_translate_error_code,
#endif
#ifdef CONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME
	.mg_group_name = "meshbus input mgmt",
#endif
};

static void meshbus_input_mgmt_register_group(void)
{
	mgmt_register_group(&meshbus_input_mgmt_group);
}

MCUMGR_HANDLER_DEFINE(meshbus_input_mgmt, meshbus_input_mgmt_register_group);
