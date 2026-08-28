/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Meshbus Bluetooth service sample
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(meshbus_services_bluetooth_sample, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("Meshbus Bluetooth service sample started");
	LOG_INF("Build timestamp: " __DATE__ " " __TIME__);
	return 0;
}

