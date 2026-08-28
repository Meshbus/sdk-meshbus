/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/meshbus/time.h>
#include <zephyr/sys/clock.h>

static bool realtime_timespec_is_valid(const struct timespec *ts)
{
	if (ts == NULL || ts->tv_sec < 0 || ts->tv_nsec < 0 ||
	    ts->tv_nsec >= NSEC_PER_SEC) {
		return false;
	}

	return (uint64_t)ts->tv_sec >= MESHBUS_TIME_VALID_UNIX_TIMESTAMP_S &&
	       (uint64_t)ts->tv_sec <= UINT32_MAX;
}

bool meshbus_time_realtime_is_valid(void)
{
	struct timespec ts;

	return sys_clock_gettime(SYS_CLOCK_REALTIME, &ts) == 0 &&
	       realtime_timespec_is_valid(&ts);
}

int meshbus_time_timestamp_s_get(uint32_t *out_timestamp_s)
{
	struct timespec ts;

	if (out_timestamp_s == NULL) {
		return -EINVAL;
	}

	if (sys_clock_gettime(SYS_CLOCK_REALTIME, &ts) == 0 &&
	    realtime_timespec_is_valid(&ts)) {
		*out_timestamp_s = (uint32_t)ts.tv_sec;
		return 0;
	}

	*out_timestamp_s = (uint32_t)k_uptime_seconds();
	if (*out_timestamp_s == 0U) {
		*out_timestamp_s = 1U;
	}
	return 0;
}

int meshbus_time_timestamp_ms_get(uint64_t *out_timestamp_ms)
{
	struct timespec ts;

	if (out_timestamp_ms == NULL) {
		return -EINVAL;
	}

	if (sys_clock_gettime(SYS_CLOCK_REALTIME, &ts) == 0 &&
	    realtime_timespec_is_valid(&ts)) {
		*out_timestamp_ms = ((uint64_t)ts.tv_sec * MSEC_PER_SEC) +
				    (uint64_t)(ts.tv_nsec / NSEC_PER_MSEC);
		return 0;
	}

	*out_timestamp_ms = (uint64_t)k_uptime_get();
	if (*out_timestamp_ms == 0U) {
		*out_timestamp_ms = 1U;
	}
	return 0;
}
