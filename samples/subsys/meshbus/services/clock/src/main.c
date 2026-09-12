/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <clock/clock.h>

LOG_MODULE_REGISTER(mbs_clock_sample, LOG_LEVEL_INF);

int main(void)
{
	mbs_clock_config cfg;
	int rc;

	LOG_INF("Meshbus clock sample started");
	LOG_INF("Build timestamp: " __DATE__ " " __TIME__);

	rc = mbs_clock_config_get(&cfg);
	if (rc != 0) {
		LOG_ERR("Failed to read clock config: %d", rc);
		return 0;
	}

	LOG_INF("clock/config: time_format=%d utc_offset_minutes=%d", cfg.time_format,
		cfg.utc_offset_minutes);

	return 0;
}
