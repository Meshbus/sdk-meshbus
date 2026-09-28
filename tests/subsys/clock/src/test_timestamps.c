/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <time.h>
#include <zephyr/kernel.h>
#include <clock/timestamp.h>
#include <zephyr/sys/clock.h>
#include <zephyr/ztest.h>

ZTEST(mbs_clock_timestamps, test_units_validity_and_fallback)
{
	uint32_t seconds;
	uint64_t milliseconds;
	struct timespec realtime = { .tv_sec = 1 };

	zassert_equal(mbs_clock_timestamp_s_get(NULL), -EINVAL);
	zassert_equal(mbs_clock_timestamp_ms_get(NULL), -EINVAL);
	zassert_ok(sys_clock_settime(SYS_CLOCK_REALTIME, &realtime));
	zassert_false(mbs_clock_realtime_is_valid());
	zassert_ok(mbs_clock_timestamp_s_get(&seconds));
	zassert_ok(mbs_clock_timestamp_ms_get(&milliseconds));
	zassert_between_inclusive(seconds, 1U, MAX(1U, k_uptime_seconds()));
	zassert_between_inclusive(milliseconds, 1ULL, MAX(1LL, k_uptime_get()));

	realtime.tv_sec = MBS_CLOCK_VALID_UNIX_TIMESTAMP_S;
	realtime.tv_nsec = 123000000;
	zassert_ok(sys_clock_settime(SYS_CLOCK_REALTIME, &realtime));
	zassert_true(mbs_clock_realtime_is_valid());
	zassert_ok(mbs_clock_timestamp_s_get(&seconds));
	zassert_ok(mbs_clock_timestamp_ms_get(&milliseconds));
	zassert_between_inclusive(seconds, realtime.tv_sec, realtime.tv_sec + 1);
	zassert_between_inclusive(milliseconds, (uint64_t)realtime.tv_sec * 1000 + 123,
				  (uint64_t)realtime.tv_sec * 1000 + 1123);

	/* The 32-bit protocol boundary applies to both timestamp units. */
	realtime.tv_sec = (time_t)UINT32_MAX + 1LL;
	realtime.tv_nsec = 0;
	if (realtime.tv_sec > 0) {
		zassert_ok(sys_clock_settime(SYS_CLOCK_REALTIME, &realtime));
		zassert_false(mbs_clock_realtime_is_valid());
		zassert_ok(mbs_clock_timestamp_s_get(&seconds));
		zassert_true(seconds < MBS_CLOCK_VALID_UNIX_TIMESTAMP_S);
	}
}

ZTEST_SUITE(mbs_clock_timestamps, NULL, NULL, NULL, NULL, NULL);
