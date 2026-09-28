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
#if defined(CONFIG_MBS_INDICATOR)
#include <indicator/indicator.h>
#include <zephyr/kernel.h>
#if defined(CONFIG_MBS_MESSAGE)
#include <message/message.h>
#endif
#if defined(CONFIG_MBS_RADIO)
#include <radio/radio.h>
#endif
static void startup_check(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(startup_check_work, startup_check);
static void startup_check(struct k_work *work)
{
	ARG_UNUSED(work);
	bool ready = true;
#if defined(CONFIG_MBS_MESHCORE)
	ready = ready && mbs_meshcore_runtime_is_ready();
#endif
#if defined(CONFIG_MBS_MESSAGE)
	ready = ready && mbs_message_is_ready();
#endif
#if defined(CONFIG_MBS_RADIO)
	struct mbs_radio_health_event health;
	ready = ready && mbs_radio_health_get(&health) == 0 && health.ready;
#endif
	mbs_indicator_startup_complete(ready);
	if (!ready) {
		(void)k_work_schedule(&startup_check_work, K_SECONDS(1));
	}
}
#endif

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
#if defined(CONFIG_MBS_INDICATOR)
	(void)k_work_schedule(&startup_check_work, K_NO_WAIT);
#endif
	return 0;
}
