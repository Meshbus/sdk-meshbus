/*
 * Copyright (c) 2025 FoBE Projects
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/app_version.h>
#include <zephyr/logging/log.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/pm/device_runtime.h>
#include <meshcore/meshcore.h>

LOG_MODULE_REGISTER(meshbus_app, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("Meshbus firmware version: %s", APP_VERSION_STRING);

#ifdef CONFIG_BOOTLOADER_MCUBOOT
	LOG_INF("MCUboot bootloader support enabled");
#endif

#if defined(CONFIG_MESHBUS_UART_MCUMGR_LOGGING)
	const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_uart_mcumgr));

	if (!device_is_ready(uart) || pm_device_runtime_get(uart) < 0) {
		LOG_ERR("Unable to keep UART MCUmgr transport active");
	}
#endif
#if defined(CONFIG_MBS_MESHCORE)
	LOG_INF("MESHBUS_READY active_role=%u", (unsigned int)mbs_meshcore_active_role_get());
#else
	LOG_INF("MESHBUS_READY");
#endif
	return 0;
}
