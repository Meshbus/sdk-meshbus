/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/meshbus/clock.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/util.h>

#include "common/settings.h"

LOG_MODULE_REGISTER(meshbus_clock, CONFIG_MESHBUS_CLOCK_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* Defaults And State                                                          */
/* -------------------------------------------------------------------------- */

#define MESHBUS_CLOCK_CONFIG_DEFAULTS                                                              \
	{                                                                                          \
		.time_format = (meshbus_ClockConfig_ClockTimeFormat)                        \
			CONFIG_MESHBUS_CLOCK_DEFAULT_TIME_FORMAT,                                  \
		.utc_offset_minutes = CONFIG_MESHBUS_CLOCK_DEFAULT_UTC_OFFSET_MINUTES,             \
	}

static meshbus_clock_config clock_cfg = MESHBUS_CLOCK_CONFIG_DEFAULTS;
static K_MUTEX_DEFINE(settings_mutex);
static bool settings_initial_apply;
static struct k_work_delayable settings_persistence_work;

/* -------------------------------------------------------------------------- */
/* Settings Schema                                                             */
/* -------------------------------------------------------------------------- */

#define MESHBUS_CLOCK_SETTINGS_SUBTREE    "meshbus/clock"
#define MESHBUS_CLOCK_SETTINGS_KEY_CONFIG "config"

static meshbus_clock_config settings_load_cfg = MESHBUS_CLOCK_CONFIG_DEFAULTS;
static struct mb_settings_blob_load_state settings_load_state;

MB_SETTINGS_BLOB_SCHEMA_DEFINE(clock_settings_schema, MESHBUS_CLOCK_SETTINGS_SUBTREE,
			       MESHBUS_CLOCK_SETTINGS_KEY_CONFIG, meshbus_ClockConfig,
			       meshbus_clock_config);

/* -------------------------------------------------------------------------- */
/* Config Apply                                                                */
/* -------------------------------------------------------------------------- */

static int clock_config_validate(const meshbus_clock_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	if ((cfg->time_format != meshbus_ClockConfig_ClockTimeFormat_TIME_FORMAT_12H) &&
	    (cfg->time_format != meshbus_ClockConfig_ClockTimeFormat_TIME_FORMAT_24H)) {
		LOG_ERR("Invalid time_format: %u", (unsigned int)cfg->time_format);
		return -EINVAL;
	}

	if (cfg->utc_offset_minutes < MESHBUS_CLOCK_MIN_UTC_OFFSET_MINUTES ||
	    cfg->utc_offset_minutes > MESHBUS_CLOCK_MAX_UTC_OFFSET_MINUTES) {
		LOG_ERR("Invalid utc_offset_minutes: %d (valid: %d..%d)", cfg->utc_offset_minutes,
			MESHBUS_CLOCK_MIN_UTC_OFFSET_MINUTES, MESHBUS_CLOCK_MAX_UTC_OFFSET_MINUTES);
		return -EINVAL;
	}

	if ((cfg->utc_offset_minutes % 15) != 0) {
		LOG_ERR("Invalid utc_offset_minutes step: %d (must be multiple of 15)",
			cfg->utc_offset_minutes);
		return -EINVAL;
	}

	return 0;
}

static int clock_unix_ms_to_timespec(uint64_t unix_time_ms, struct timespec *out)
{
	if (unix_time_ms == 0U || out == NULL) {
		return -EINVAL;
	}

	uint64_t seconds = unix_time_ms / 1000U;
	time_t tv_sec = (time_t)seconds;

	if ((uint64_t)tv_sec != seconds) {
		return -EINVAL;
	}

	out->tv_sec = tv_sec;
	out->tv_nsec = (long)((unix_time_ms % 1000U) * 1000000ULL);
	return 0;
}

static int clock_timespec_to_unix_ms(const struct timespec *ts, uint64_t *unix_time_ms)
{
	if (ts == NULL || unix_time_ms == NULL || ts->tv_sec < 0 ||
	    ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000L) {
		return -EINVAL;
	}

	*unix_time_ms = ((uint64_t)ts->tv_sec * 1000ULL) + (uint64_t)(ts->tv_nsec / 1000000L);
	return 0;
}

static int settings_handler_apply(const meshbus_clock_config *cfg, bool persistence, bool force)
{
	int rc = clock_config_validate(cfg);

	if (rc != 0) {
		return rc;
	}

	k_mutex_lock(&settings_mutex, K_FOREVER);

	if (!force && (clock_cfg.time_format == cfg->time_format) &&
	    (clock_cfg.utc_offset_minutes == cfg->utc_offset_minutes)) {
		settings_initial_apply = true;
		k_mutex_unlock(&settings_mutex);
		LOG_DBG("Settings unchanged, nothing to apply");
		return 0;
	}

	clock_cfg = *cfg;
	settings_initial_apply = true;
	k_mutex_unlock(&settings_mutex);

	LOG_INF("Settings apply: time_format=%u utc_offset_minutes=%d",
		(unsigned int)cfg->time_format, cfg->utc_offset_minutes);

	if (persistence) {
		(void)k_work_reschedule(&settings_persistence_work,
					K_MSEC(CONFIG_MESHBUS_SETTINGS_PERSISTENCE_DELAY));
	}

	return 0;
}

/* -------------------------------------------------------------------------- */
/* Settings Handlers                                                           */
/* -------------------------------------------------------------------------- */

MB_SETTINGS_BLOB_CONFIG_DEFINE(clock_settings_schema, settings_mutex, settings_load_state,
			       settings_load_cfg, clock_cfg, settings_initial_apply,
			       meshbus_clock_config, meshbus_ClockConfig_size,
			       settings_handler_apply, "clock")

SETTINGS_STATIC_HANDLER_DEFINE(meshbus_clock, MESHBUS_CLOCK_SETTINGS_SUBTREE, NULL,
			       settings_handle_set, settings_handle_commit, settings_handle_export);

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */

int meshbus_clock_config_get(meshbus_clock_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&settings_mutex, K_FOREVER);
	*cfg = clock_cfg;
	k_mutex_unlock(&settings_mutex);

	return 0;
}

int meshbus_clock_config_set(const meshbus_clock_config *cfg)
{
	return settings_handler_apply(cfg, true, false);
}

int meshbus_clock_config_reset(void)
{
	struct k_work_sync sync;
	meshbus_clock_config cfg = MESHBUS_CLOCK_CONFIG_DEFAULTS;

	(void)k_work_cancel_delayable_sync(&settings_persistence_work, &sync);

	int rc = settings_handler_apply(&cfg, false, true);
	if (rc != 0) {
		return rc;
	}

	rc = mb_settings_blob_delete(&clock_settings_schema);
	if (rc != 0) {
		LOG_ERR("Failed to delete persisted settings: %d", rc);
		return rc;
	}

	return 0;
}

int meshbus_clock_localtime(time_t utc_time, struct tm *local_time)
{
	meshbus_clock_config cfg;
	int64_t utc_seconds = (int64_t)utc_time;
	int64_t offset_seconds;
	int64_t local_seconds;
	time_t local_timestamp;

	if (local_time == NULL) {
		return -EINVAL;
	}

	if ((time_t)utc_seconds != utc_time) {
		return -ERANGE;
	}

	k_mutex_lock(&settings_mutex, K_FOREVER);
	cfg = clock_cfg;
	k_mutex_unlock(&settings_mutex);

	offset_seconds = (int64_t)cfg.utc_offset_minutes * 60;
	if ((offset_seconds > 0 && utc_seconds > INT64_MAX - offset_seconds) ||
	    (offset_seconds < 0 && utc_seconds < INT64_MIN - offset_seconds)) {
		return -ERANGE;
	}

	local_seconds = utc_seconds + offset_seconds;
	local_timestamp = (time_t)local_seconds;
	if ((int64_t)local_timestamp != local_seconds ||
	    gmtime_r(&local_timestamp, local_time) == NULL) {
		return -ERANGE;
	}

	return 0;
}

int meshbus_clock_time_set_unix_ms(uint64_t unix_time_ms, uint64_t *applied_unix_time_ms)
{
	struct timespec target;
	int rc = clock_unix_ms_to_timespec(unix_time_ms, &target);

	if (rc != 0) {
		return rc;
	}

	rc = sys_clock_settime(SYS_CLOCK_REALTIME, &target);
	if (rc != 0) {
		LOG_WRN("Failed to set real-time clock: %d", rc);
		return rc;
	}

	if (applied_unix_time_ms != NULL) {
		struct timespec applied;

		rc = sys_clock_gettime(SYS_CLOCK_REALTIME, &applied);
		if (rc != 0) {
			LOG_WRN("Failed to read applied real-time clock: %d", rc);
			return rc;
		}

		rc = clock_timespec_to_unix_ms(&applied, applied_unix_time_ms);
		if (rc != 0) {
			return rc;
		}
	}

	LOG_INF("Time set: unix_ms=%llu", (unsigned long long)unix_time_ms);
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                              */
/* -------------------------------------------------------------------------- */

static int meshbus_clock_init(void)
{
	int rc;

	k_work_init_delayable(&settings_persistence_work, settings_persistence_work_handler);

	rc = settings_load_subtree(MESHBUS_CLOCK_SETTINGS_SUBTREE);
	if (rc != 0) {
		return rc;
	}

	if (!settings_initial_apply) {
		rc = settings_handler_apply(&clock_cfg, false, true);
		if (rc != 0) {
			return rc;
		}
	}

	LOG_INF("Meshbus clock service ready");
	return 0;
}

SYS_INIT(meshbus_clock_init, APPLICATION, CONFIG_MESHBUS_CLOCK_INIT_PRIORITY);
