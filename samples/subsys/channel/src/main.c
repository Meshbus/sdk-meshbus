/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <channel/channel.h>

LOG_MODULE_REGISTER(mbs_channel_sample, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("Meshbus channel sample started");
	LOG_INF("Build timestamp: " __DATE__ " " __TIME__);
	LOG_INF("Channel store: count=%u capacity=%u",
		(unsigned int)mbs_channel_store_count(),
		(unsigned int)mbs_channel_store_size());

	return 0;
}
