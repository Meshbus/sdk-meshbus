/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "mbs_mgmt_internal.h"
#include "meshbus/power.pb.h"

#include <power/power.h>

LOG_MODULE_REGISTER(mbs_power_mgmt, CONFIG_MBS_POWER_LOG_LEVEL);

#define MBS_POWER_MGMT_PROTO_RSP_MAX_SIZE                                                      \
	MESHBUS_MESHBUS_POWER_PB_H_MAX_SIZE
#define MBS_POWER_MGMT_ACTION_DELAY K_MSEC(750)

enum mbs_power_mgmt_action {
	MBS_POWER_MGMT_ACTION_NONE = 0,
	MBS_POWER_MGMT_ACTION_SHUTDOWN,
	MBS_POWER_MGMT_ACTION_REBOOT,
	MBS_POWER_MGMT_ACTION_BOOTLOADER,
};

static atomic_t mbs_power_mgmt_pending_action =
	ATOMIC_INIT(MBS_POWER_MGMT_ACTION_NONE);

static void mbs_power_mgmt_action_work_handler(struct k_work *work)
{
	enum mbs_power_mgmt_action action =
		(enum mbs_power_mgmt_action)atomic_get(
			&mbs_power_mgmt_pending_action);
	int rc;

	ARG_UNUSED(work);

	switch (action) {
	case MBS_POWER_MGMT_ACTION_SHUTDOWN:
		rc = mbs_power_shutdown();
		break;
	case MBS_POWER_MGMT_ACTION_REBOOT:
		rc = mbs_power_reboot();
		break;
	case MBS_POWER_MGMT_ACTION_BOOTLOADER:
		rc = mbs_power_reboot_to_bootloader();
		break;
	default:
		rc = -EINVAL;
		break;
	}

	LOG_ERR("Deferred power action %d returned: %d", action, rc);
	atomic_clear(&mbs_power_mgmt_pending_action);
}

static K_WORK_DELAYABLE_DEFINE(mbs_power_mgmt_action_work,
			       mbs_power_mgmt_action_work_handler);

static int mbs_power_mgmt_schedule_action(
	enum mbs_power_mgmt_action action)
{
	int rc;

	if (!atomic_cas(&mbs_power_mgmt_pending_action,
			MBS_POWER_MGMT_ACTION_NONE, action)) {
		return -EBUSY;
	}

	rc = k_work_schedule(&mbs_power_mgmt_action_work,
			     MBS_POWER_MGMT_ACTION_DELAY);
	if (rc < 0) {
		atomic_clear(&mbs_power_mgmt_pending_action);
		return rc;
	}

	return 0;
}

#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
static int mbs_power_mgmt_translate_error_code(uint16_t err)
{
	return (int)err;
}
#endif

static int mbs_power_mgmt_shutdown(struct smp_streamer *ctxt)
{
	meshbus_PowerShutdownRequest req = meshbus_PowerShutdownRequest_init_zero;
	meshbus_PowerShutdownResponse rsp = meshbus_PowerShutdownResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_PowerShutdownRequest_fields,
				      true);

	if (rc != 0) {
		return rc;
	}

	rc = mbs_power_mgmt_schedule_action(
		MBS_POWER_MGMT_ACTION_SHUTDOWN);
	if (rc != 0) {
		return rc;
	}

	rsp.triggered = true;

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_PowerShutdownResponse_fields,
				    MBS_POWER_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_power_mgmt_status(struct smp_streamer *ctxt)
{
	meshbus_PowerStatusRequest req = meshbus_PowerStatusRequest_init_zero;
	meshbus_PowerStatusResponse rsp = meshbus_PowerStatusResponse_init_zero;
	mbs_power_config cfg;
	uint16_t voltage_mv = 0U;
	uint16_t temperature_dk = 0U;
	uint8_t soc_percent = 0U;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_PowerStatusRequest_fields,
				      true);

	if (rc != 0) {
		return rc;
	}

	rc = mbs_power_config_get(&cfg);
	if (rc != 0) {
		return rc;
	}

	rsp.has_config = true;
	rsp.config = cfg;

	rc = mbs_power_fuel_gauge_get(&voltage_mv, &soc_percent, &temperature_dk);
	if (rc == 0) {
		rsp.has_voltage_mv = true;
		rsp.voltage_mv = voltage_mv;
		rsp.has_soc_percent = true;
		rsp.soc_percent = soc_percent;
		rsp.has_temperature_dk = true;
		rsp.temperature_dk = temperature_dk;
	}
	rsp.charging = mbs_power_is_charging();
	rsp.online = mbs_power_is_online();

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_PowerStatusResponse_fields,
				    MBS_POWER_MGMT_PROTO_RSP_MAX_SIZE);
}

MBS_MGMT_CONFIG_GET_HANDLER_DEFINE(
	mbs_power_mgmt_config_get, meshbus_PowerConfigGetRequest,
	meshbus_PowerConfigGetResponse, mbs_power_config_get,
	meshbus_PowerConfigGetRequest_fields, meshbus_PowerConfigGetResponse_fields,
	MBS_POWER_MGMT_PROTO_RSP_MAX_SIZE);

MBS_MGMT_CONFIG_SET_HANDLER_DEFINE(
	mbs_power_mgmt_config_set, meshbus_PowerConfigSetRequest,
	meshbus_PowerConfigSetResponse, mbs_power_config_set,
	meshbus_PowerConfigSetRequest_fields, meshbus_PowerConfigSetResponse_fields,
	MBS_POWER_MGMT_PROTO_RSP_MAX_SIZE);

MBS_MGMT_CONFIG_RESET_HANDLER_DEFINE(
	mbs_power_mgmt_config_reset, meshbus_PowerConfigResetRequest,
	meshbus_PowerConfigResetResponse, mbs_power_config_reset, mbs_power_config_get,
	meshbus_PowerConfigResetRequest_fields, meshbus_PowerConfigResetResponse_fields,
	MBS_POWER_MGMT_PROTO_RSP_MAX_SIZE);

static int mbs_power_mgmt_reboot(struct smp_streamer *ctxt)
{
	meshbus_PowerRebootRequest req = meshbus_PowerRebootRequest_init_zero;
	meshbus_PowerRebootResponse rsp = meshbus_PowerRebootResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_PowerRebootRequest_fields,
				      true);

	if (rc != 0) {
		return rc;
	}

	rc = mbs_power_mgmt_schedule_action(
		MBS_POWER_MGMT_ACTION_REBOOT);
	if (rc != 0) {
		return rc;
	}

	rsp.triggered = true;

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_PowerRebootResponse_fields,
				    MBS_POWER_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_power_mgmt_bootloader(struct smp_streamer *ctxt)
{
	meshbus_PowerBootloaderRequest req =
		meshbus_PowerBootloaderRequest_init_zero;
	meshbus_PowerBootloaderResponse rsp =
		meshbus_PowerBootloaderResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(
		ctxt, &req, sizeof(req),
		meshbus_PowerBootloaderRequest_fields, true);

	if (rc != 0) {
		return rc;
	}

	if (!IS_ENABLED(CONFIG_RETENTION_BOOT_MODE) &&
	    !IS_ENABLED(CONFIG_MBS_POWER_BOOTLOADER_GPREGRET)) {
		return -ENOTSUP;
	}

	rc = mbs_power_mgmt_schedule_action(
		MBS_POWER_MGMT_ACTION_BOOTLOADER);
	if (rc != 0) {
		return rc;
	}

	rsp.triggered = true;
	return mbs_mgmt_encode_proto(
		ctxt, &rsp, meshbus_PowerBootloaderResponse_fields,
		MBS_POWER_MGMT_PROTO_RSP_MAX_SIZE);
}

static const struct mgmt_handler mbs_power_mgmt_group_handlers[] = {
	[meshbus_PowerMgmtCommandId_POWER_MGMT_COMMAND_ID_SHUTDOWN] = {NULL,
								       mbs_power_mgmt_shutdown},
	[meshbus_PowerMgmtCommandId_POWER_MGMT_COMMAND_ID_STATUS] = {mbs_power_mgmt_status,
								     NULL},
	[meshbus_PowerMgmtCommandId_POWER_MGMT_COMMAND_ID_CONFIG] = {mbs_power_mgmt_config_get,
								     mbs_power_mgmt_config_set},
	[meshbus_PowerMgmtCommandId_POWER_MGMT_COMMAND_ID_CONFIG_RESET] =
		{NULL, mbs_power_mgmt_config_reset},
	[meshbus_PowerMgmtCommandId_POWER_MGMT_COMMAND_ID_REBOOT] = {NULL,
								     mbs_power_mgmt_reboot},
	[meshbus_PowerMgmtCommandId_POWER_MGMT_COMMAND_ID_BOOTLOADER] = {
		NULL, mbs_power_mgmt_bootloader
	},
};

#define MBS_POWER_MGMT_GROUP_SZ ARRAY_SIZE(mbs_power_mgmt_group_handlers)

static struct mgmt_group mbs_power_mgmt_group = {
	.mg_handlers = mbs_power_mgmt_group_handlers,
	.mg_handlers_count = MBS_POWER_MGMT_GROUP_SZ,
	.mg_group_id = meshbus_PowerMgmtGroupId_POWER_MGMT_GROUP_ID_MESHBUS_POWER,
#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
	.mg_translate_error = mbs_power_mgmt_translate_error_code,
#endif
#ifdef CONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME
	.mg_group_name = "meshbus power mgmt",
#endif
};

static void mbs_power_mgmt_register_group(void)
{
	mgmt_register_group(&mbs_power_mgmt_group);
}

MCUMGR_HANDLER_DEFINE(mbs_power_mgmt, mbs_power_mgmt_register_group);
