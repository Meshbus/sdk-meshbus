/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(meshbus_llext_sample, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("Meshbus LLEXT boot service sample started");
	LOG_INF("Install service artifacts as /extra/svcs/*.mbs");
	LOG_INF("New services run after the next device reboot");
	LOG_INF("Use: meshbus llext service list|status <id>");

	return 0;
}
