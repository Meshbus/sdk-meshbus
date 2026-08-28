/*
 * Copyright (c) 2025 FoBE Projects
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Meshbus subsystem test application
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(meshbus_test, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("Meshbus test application started");
	LOG_INF("Build timestamp: " __DATE__ " " __TIME__);

#ifdef CONFIG_BOOTLOADER_MCUBOOT
	LOG_INF("MCUboot bootloader support enabled");
#endif

	return 0;
}
