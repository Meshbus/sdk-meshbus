// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <time.h>

#include <zephyr/meshbus/clock.h>
#include <zephyr/meshbus/time.h>
#include <zephyr/sys/clock.h>
#include <zephyr/ztest.h>

static meshbus_clock_config valid_clock_config(void)
{
	meshbus_clock_config cfg = meshbus_ClockConfig_init_zero;

	cfg.time_format = meshbus_ClockConfig_ClockTimeFormat_TIME_FORMAT_24H;
	cfg.utc_offset_minutes = 480;
	return cfg;
}

static int test_realtime_unix_ms_get(uint64_t *unix_time_ms)
{
	struct timespec ts;
	int rc;

	if (unix_time_ms == NULL) {
		return -EINVAL;
	}

	rc = sys_clock_gettime(SYS_CLOCK_REALTIME, &ts);
	if (rc != 0) {
		return rc;
	}

	if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000L) {
		return -EINVAL;
	}

	*unix_time_ms = ((uint64_t)ts.tv_sec * 1000ULL) + (uint64_t)(ts.tv_nsec / 1000000L);
	return 0;
}

static void test_realtime_reset(void)
{
	const struct timespec invalid_realtime = {
		.tv_sec = 1,
		.tv_nsec = 0,
	};

	zassert_ok(sys_clock_settime(SYS_CLOCK_REALTIME, &invalid_realtime));
}

static void *clock_suite_setup(void)
{
	test_realtime_reset();
	zassert_ok(meshbus_clock_config_reset());
	return NULL;
}

static void clock_before(void *fixture)
{
	ARG_UNUSED(fixture);
	test_realtime_reset();
	zassert_ok(meshbus_clock_config_reset());
}

ZTEST(meshbus_clock_contract, test_config_get_rejects_null_and_returns_defaults)
{
	meshbus_clock_config cfg;

	zassert_equal(meshbus_clock_config_get(NULL), -EINVAL);
	zassert_ok(meshbus_clock_config_get(&cfg));
	zassert_equal(cfg.time_format, CONFIG_MESHBUS_CLOCK_DEFAULT_TIME_FORMAT);
	zassert_equal(cfg.utc_offset_minutes, CONFIG_MESHBUS_CLOCK_DEFAULT_UTC_OFFSET_MINUTES);
}

ZTEST(meshbus_clock_contract, test_config_set_validates_time_format_and_utc_offset)
{
	meshbus_clock_config cfg = valid_clock_config();

	zassert_equal(meshbus_clock_config_set(NULL), -EINVAL);

	cfg.time_format = (meshbus_ClockConfig_ClockTimeFormat)99;
	zassert_equal(meshbus_clock_config_set(&cfg), -EINVAL);

	cfg = valid_clock_config();
	cfg.utc_offset_minutes = MESHBUS_CLOCK_MAX_UTC_OFFSET_MINUTES + 15;
	zassert_equal(meshbus_clock_config_set(&cfg), -EINVAL);

	cfg = valid_clock_config();
	cfg.utc_offset_minutes = MESHBUS_CLOCK_MIN_UTC_OFFSET_MINUTES - 15;
	zassert_equal(meshbus_clock_config_set(&cfg), -EINVAL);

	cfg = valid_clock_config();
	cfg.utc_offset_minutes = 7;
	zassert_equal(meshbus_clock_config_set(&cfg), -EINVAL);
}

ZTEST(meshbus_clock_contract, test_config_set_get_and_reset)
{
	meshbus_clock_config cfg = valid_clock_config();
	meshbus_clock_config got;

	zassert_ok(meshbus_clock_config_set(&cfg));
	zassert_ok(meshbus_clock_config_get(&got));
	zassert_equal(got.time_format, cfg.time_format);
	zassert_equal(got.utc_offset_minutes, cfg.utc_offset_minutes);

	zassert_ok(meshbus_clock_config_reset());
	zassert_ok(meshbus_clock_config_get(&got));
	zassert_equal(got.time_format, CONFIG_MESHBUS_CLOCK_DEFAULT_TIME_FORMAT);
	zassert_equal(got.utc_offset_minutes, CONFIG_MESHBUS_CLOCK_DEFAULT_UTC_OFFSET_MINUTES);
}

ZTEST(meshbus_clock_contract, test_localtime_applies_configured_offset_and_date_rollover)
{
	const time_t utc_time = (time_t)1704069000;
	meshbus_clock_config cfg = valid_clock_config();
	struct tm local_time;

	zassert_equal(meshbus_clock_localtime(utc_time, NULL), -EINVAL);

	zassert_ok(meshbus_clock_config_set(&cfg));
	zassert_ok(meshbus_clock_localtime(utc_time, &local_time));
	zassert_equal(local_time.tm_year, 124);
	zassert_equal(local_time.tm_mon, 0);
	zassert_equal(local_time.tm_mday, 1);
	zassert_equal(local_time.tm_hour, 8);
	zassert_equal(local_time.tm_min, 30);

	cfg.utc_offset_minutes = -60;
	zassert_ok(meshbus_clock_config_set(&cfg));
	zassert_ok(meshbus_clock_localtime(utc_time, &local_time));
	zassert_equal(local_time.tm_year, 123);
	zassert_equal(local_time.tm_mon, 11);
	zassert_equal(local_time.tm_mday, 31);
	zassert_equal(local_time.tm_hour, 23);
	zassert_equal(local_time.tm_min, 30);
}

ZTEST(meshbus_clock_contract, test_time_set_rejects_zero_and_updates_realtime_clock)
{
	const uint64_t target_unix_ms = 1775000123456ULL;
	uint64_t applied_unix_ms = 123U;
	uint64_t current_unix_ms = 0U;

	zassert_equal(meshbus_clock_time_set_unix_ms(0U, &applied_unix_ms), -EINVAL);
	zassert_equal(applied_unix_ms, 123U);

	zassert_ok(meshbus_clock_time_set_unix_ms(target_unix_ms, &applied_unix_ms));
	zassert_true(applied_unix_ms >= target_unix_ms,
		     "applied time went backwards: %llu < %llu",
		     (unsigned long long)applied_unix_ms, (unsigned long long)target_unix_ms);
	zassert_true(applied_unix_ms < target_unix_ms + 10000U,
		     "applied time drift too large: %llu >= %llu",
		     (unsigned long long)applied_unix_ms,
		     (unsigned long long)(target_unix_ms + 10000U));

	zassert_ok(test_realtime_unix_ms_get(&current_unix_ms));
	zassert_true(current_unix_ms >= target_unix_ms,
		     "current time went backwards: %llu < %llu",
		     (unsigned long long)current_unix_ms, (unsigned long long)target_unix_ms);
	zassert_true(current_unix_ms < target_unix_ms + 10000U,
		     "current time drift too large: %llu >= %llu",
		     (unsigned long long)current_unix_ms,
		     (unsigned long long)(target_unix_ms + 10000U));

	zassert_ok(meshbus_clock_time_set_unix_ms(target_unix_ms + 1000U, NULL));
}

ZTEST(meshbus_clock_contract, test_timestamp_helpers_use_valid_realtime_or_uptime_fallback)
{
	const uint64_t target_unix_ms = 1775000456789ULL;
	uint32_t timestamp_s = 0U;
	uint64_t timestamp_ms = 0U;

	zassert_false(meshbus_time_realtime_is_valid(),
		      "low realtime should not be considered synchronized");
	zassert_equal(meshbus_time_timestamp_s_get(NULL), -EINVAL);
	zassert_equal(meshbus_time_timestamp_ms_get(NULL), -EINVAL);
	zassert_ok(meshbus_time_timestamp_s_get(&timestamp_s));
	zassert_ok(meshbus_time_timestamp_ms_get(&timestamp_ms));
	zassert_not_equal(timestamp_s, 0U, "fallback seconds timestamp should be nonzero");
	zassert_not_equal(timestamp_ms, 0U, "fallback milliseconds timestamp should be nonzero");

	zassert_ok(meshbus_clock_time_set_unix_ms(target_unix_ms, NULL));
	zassert_true(meshbus_time_realtime_is_valid(),
		     "valid realtime should be considered synchronized");
	zassert_ok(meshbus_time_timestamp_s_get(&timestamp_s));
	zassert_true(timestamp_s >= (uint32_t)(target_unix_ms / 1000U),
		     "seconds timestamp went backwards");
	zassert_true(timestamp_s < (uint32_t)(target_unix_ms / 1000U) + 10U,
		     "seconds timestamp drift too large");
	zassert_ok(meshbus_time_timestamp_ms_get(&timestamp_ms));
	zassert_true(timestamp_ms >= target_unix_ms, "milliseconds timestamp went backwards");
	zassert_true(timestamp_ms < target_unix_ms + 10000U,
		     "milliseconds timestamp drift too large");
}

ZTEST_SUITE(meshbus_clock_contract, NULL, clock_suite_setup, clock_before, NULL, NULL);
