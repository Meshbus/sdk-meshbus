/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/gnss.h>
#include <zephyr/meshbus/gnss.h>
#include <zephyr/meshbus/power.h>

#include <stddef.h>
#include <time.h>

#include "common/settings.h"
#include "heading.h"
#include "gnss_internal.h"

LOG_MODULE_REGISTER(meshbus_gnss, CONFIG_MESHBUS_GNSS_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* ZBus Channels                                                              */
/* -------------------------------------------------------------------------- */

static bool gnss_data_publish_validator(const void *msg, size_t msg_size);

ZBUS_CHAN_DEFINE(meshbus_gnss_data_chan, struct meshbus_gnss_data_event,
		 gnss_data_publish_validator, /* validator */
		 NULL,                        /* user_data */
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

/* -------------------------------------------------------------------------- */
/* Statistics                                                                 */
/* -------------------------------------------------------------------------- */

#ifdef CONFIG_MESHBUS_GNSS_STATS
static int64_t acquisition_start_time;
STATS_SECT_DECL(meshbus_gnss_stats) meshbus_gnss_stats;
#endif

/* -------------------------------------------------------------------------- */
/* Devices                                                                    */
/* -------------------------------------------------------------------------- */

#if DT_HAS_CHOSEN(meshbus_gnss)
static const struct device *const gnss_dev = DEVICE_DT_GET(DT_CHOSEN(meshbus_gnss));
#else
static const struct device *const gnss_dev = NULL;
#endif

/* -------------------------------------------------------------------------- */
/* Defaults And State                                                         */
/* -------------------------------------------------------------------------- */

#define MESHBUS_GNSS_CONFIG_DEFAULTS                                                               \
	{                                                                                          \
		.enabled = IS_ENABLED(CONFIG_MESHBUS_GNSS_DEFAULT_ENABLED),                        \
		.update_interval = CONFIG_MESHBUS_GNSS_DEFAULT_UPDATE_INTERVAL,                    \
		.nav_mode = CONFIG_MESHBUS_GNSS_DEFAULT_NAV_MODE,                                  \
		.fix_rate = CONFIG_MESHBUS_GNSS_DEFAULT_FIX_RATE,                                  \
		.system_mask = CONFIG_MESHBUS_GNSS_DEFAULT_SYSTEM_MASK,                            \
		.min_active_time = CONFIG_MESHBUS_GNSS_DEFAULT_MIN_ACTIVE_TIME,                    \
		.time_sync = IS_ENABLED(CONFIG_MESHBUS_GNSS_DEFAULT_TIME_SYNC),                    \
		.has_electronic_compass = true,                                                   \
		.electronic_compass = true,                                                       \
	}

static meshbus_gnss_config gnss_cfg = MESHBUS_GNSS_CONFIG_DEFAULTS;

/*
 * meshbus_gnss_config_get/set were exported to LLEXT before the protobuf grew
 * the electronic-Compass fields.  On the supported ABI those fields occupy
 * the old structure's tail padding, so sizeof() alone cannot detect the
 * semantic break.  Keep the legacy layout explicit and fail the build if a
 * generated-header change makes config_get unsafe for an ABI-v1 caller.
 */
struct meshbus_gnss_config_abi_v1_layout {
	bool enabled;
	meshbus_GnssConfig_GnssNavMode nav_mode;
	uint8_t fix_rate;
	uint32_t system_mask;
	uint32_t update_interval;
	uint32_t min_active_time;
	bool time_sync;
};

#define GNSS_CONFIG_ABI_V1_FIELD_ASSERT(field)                                                \
	BUILD_ASSERT(offsetof(meshbus_gnss_config, field) ==                                   \
			     offsetof(struct meshbus_gnss_config_abi_v1_layout, field),        \
		     "meshbus_gnss_config ABI-v1 offset changed: " #field)

BUILD_ASSERT(sizeof(meshbus_gnss_config) == sizeof(struct meshbus_gnss_config_abi_v1_layout),
	     "meshbus_gnss_config no longer fits the exported ABI-v1 storage");
GNSS_CONFIG_ABI_V1_FIELD_ASSERT(enabled);
GNSS_CONFIG_ABI_V1_FIELD_ASSERT(nav_mode);
GNSS_CONFIG_ABI_V1_FIELD_ASSERT(fix_rate);
GNSS_CONFIG_ABI_V1_FIELD_ASSERT(system_mask);
GNSS_CONFIG_ABI_V1_FIELD_ASSERT(update_interval);
GNSS_CONFIG_ABI_V1_FIELD_ASSERT(min_active_time);
GNSS_CONFIG_ABI_V1_FIELD_ASSERT(time_sync);

#undef GNSS_CONFIG_ABI_V1_FIELD_ASSERT

/* Lock order: settings_transaction_mutex -> settings_apply_mutex ->
 * settings_mutex -> gnss_data_mutex.
 */
static K_MUTEX_DEFINE(settings_transaction_mutex);
static K_MUTEX_DEFINE(settings_mutex);
static K_MUTEX_DEFINE(settings_apply_mutex);
/* Lock-free config flags for hot paths (driver callbacks). */
static atomic_t gnss_enabled_flag = ATOMIC_INIT(IS_ENABLED(CONFIG_MESHBUS_GNSS_DEFAULT_ENABLED));
static atomic_t gnss_time_sync_flag =
	ATOMIC_INIT(IS_ENABLED(CONFIG_MESHBUS_GNSS_DEFAULT_TIME_SYNC));
static atomic_t gnss_state_flag = ATOMIC_INIT(MESHBUS_GNSS_STATE_SLEEP);
static bool has_valid_fix = false;
/* Latest valid fix observed from driver callbacks (retained across cycles). */
static struct gnss_data gnss_latest_valid_fix;
#if defined(CONFIG_GNSS_SATELLITES) && defined(CONFIG_MESHBUS_GNSS_SATELLITE_CACHE_SIZE)
static struct gnss_satellite gnss_sat_cache[CONFIG_MESHBUS_GNSS_SATELLITE_CACHE_SIZE];
static uint16_t gnss_sat_cache_count; /**< Number of cached satellites */
#endif
static K_MUTEX_DEFINE(gnss_data_mutex);
/** Delayed work for periodic update cycle */
static struct k_work_delayable gnss_cycle_work;
static struct k_work_delayable gnss_time_sync_work;
static uint32_t gnss_latest_valid_fix_rx_uptime_ms;
static uint32_t gnss_time_sync_last_rx_uptime_ms;
static bool gnss_time_sync_cal_mode;
enum gnss_cycle_phase {
	GNSS_CYCLE_PHASE_RESUME = 0,
	GNSS_CYCLE_PHASE_CHECK = 1,
};
static atomic_t gnss_cycle_phase = ATOMIC_INIT(GNSS_CYCLE_PHASE_RESUME);

/* -------------------------------------------------------------------------- */
/* Declarations                                                               */
/* -------------------------------------------------------------------------- */

static int gnss_shutdown(void);
static void gnss_time_sync_work_handler(struct k_work *work);
static void gnss_cycle_work_handler(struct k_work *work);

/* -------------------------------------------------------------------------- */
/* Device And Config Validation                                               */
/* -------------------------------------------------------------------------- */

static bool gnss_device_ready(void)
{
	return (gnss_dev != NULL) && device_is_ready(gnss_dev);
}

static bool gnss_fix_rate_is_valid(uint32_t fix_rate_hz)
{
	/* L76K supports only 1, 2, 4, 5, 10 Hz. */
	return (fix_rate_hz == 1U) || (fix_rate_hz == 2U) || (fix_rate_hz == 4U) ||
	       (fix_rate_hz == 5U) || (fix_rate_hz == 10U);
}

static int gnss_config_validate(const meshbus_gnss_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	if (cfg->update_interval < CONFIG_MESHBUS_GNSS_MIN_UPDATE_INTERVAL ||
	    cfg->update_interval > CONFIG_MESHBUS_GNSS_MAX_UPDATE_INTERVAL) {
		LOG_ERR("Invalid update_interval: %u (valid: %u-%u ms)",
			(unsigned int)cfg->update_interval, CONFIG_MESHBUS_GNSS_MIN_UPDATE_INTERVAL,
			CONFIG_MESHBUS_GNSS_MAX_UPDATE_INTERVAL);
		return -EINVAL;
	}

	/* nav_mode is a 0..3 protobuf enum, map directly to Zephyr gnss_navigation_mode. */
	if ((uint32_t)cfg->nav_mode > (uint32_t)GNSS_NAVIGATION_MODE_HIGH_DYNAMICS) {
		LOG_ERR("Invalid nav_mode: %u (valid: 0-3)", (unsigned int)cfg->nav_mode);
		return -EINVAL;
	}

	if (!gnss_fix_rate_is_valid(cfg->fix_rate)) {
		LOG_ERR("Invalid fix_rate: %u Hz (valid: 1, 2, 4, 5, 10)",
			(unsigned int)cfg->fix_rate);
		return -EINVAL;
	}

	if ((cfg->system_mask == 0U) || (cfg->system_mask > 0xFFU)) {
		LOG_ERR("Invalid system_mask: 0x%02x (valid: 0x01-0xFF)",
			(unsigned int)cfg->system_mask);
		return -EINVAL;
	}

	if (cfg->min_active_time < CONFIG_MESHBUS_GNSS_MIN_ACTIVE_TIME ||
	    cfg->min_active_time > CONFIG_MESHBUS_GNSS_MAX_ACTIVE_TIME) {
		LOG_ERR("Invalid min_active_time: %u (valid: %u-%u ms)",
			(unsigned int)cfg->min_active_time, CONFIG_MESHBUS_GNSS_MIN_ACTIVE_TIME,
			CONFIG_MESHBUS_GNSS_MAX_ACTIVE_TIME);
		return -EINVAL;
	}

	return 0;
}

static bool gnss_data_publish_validator(const void *msg, size_t msg_size)
{
	if (msg == NULL) {
		return false;
	}
	if (msg_size != sizeof(struct meshbus_gnss_data_event)) {
		return false;
	}

	const struct meshbus_gnss_data_event *event = msg;

	/* If the event claims to be valid, require a fix status other than NO_FIX. */
	if (event->valid && (event->info.fix_status == GNSS_FIX_STATUS_NO_FIX)) {
		return false;
	}

	return true;
}

/* -------------------------------------------------------------------------- */
/* Cached Data And Time Helpers                                               */
/* -------------------------------------------------------------------------- */

static const struct gnss_data *gnss_cached_fix_select_locked(void)
{
	/* Keep serving the previous valid fix while next acquisition is in progress. */
	if (gnss_latest_valid_fix.info.fix_status != GNSS_FIX_STATUS_NO_FIX) {
		return &gnss_latest_valid_fix;
	}

	return NULL;
}

static void gnss_cached_data_clear_locked(void)
{
	has_valid_fix = false;
	memset(&gnss_latest_valid_fix, 0, sizeof(gnss_latest_valid_fix));
	gnss_latest_valid_fix_rx_uptime_ms = 0U;
	gnss_time_sync_last_rx_uptime_ms = 0U;
}

static uint32_t gnss_active_window_ms(uint32_t update_interval_ms, uint32_t min_active_time_ms)
{
	/* If min_active_time is greater than update_interval, clamp it to update_interval.
	 * This ensures the acquisition window does not end early.
	 */
	if (update_interval_ms == 0U) {
		return 0U;
	}
	if (min_active_time_ms == 0U) {
		min_active_time_ms = CONFIG_MESHBUS_GNSS_DEFAULT_MIN_ACTIVE_TIME;
	}
	return MIN(update_interval_ms, min_active_time_ms);
}

static void gnss_cycle_schedule(enum gnss_cycle_phase phase, k_timeout_t delay)
{
	atomic_set(&gnss_cycle_phase, (int)phase);
	(void)k_work_reschedule(&gnss_cycle_work, delay);
}

static bool gnss_time_is_valid(const struct gnss_time *t)
{
	if (t == NULL) {
		return false;
	}

	/* GNSS provides year as 0..99; we treat it as 2000..2099. */
	uint32_t year = 2000U + t->century_year;
	if (year < 2000U || year > 2099U) {
		return false;
	}

	if (t->month < 1U || t->month > 12U) {
		return false;
	}
	if (t->hour > 23U || t->minute > 59U) {
		return false;
	}
	/* Spec: 0..60999 */
	if (t->millisecond > 60999U) {
		return false;
	}

	static const uint8_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	uint8_t max_day = days_in_month[t->month - 1U];
	/* Full leap-year check even though the range is 2000..2099. */
	bool leap = ((year % 4U) == 0U) && (((year % 100U) != 0U) || ((year % 400U) == 0U));
	if (leap && t->month == 2U) {
		max_day = 29U;
	}
	if (t->month_day < 1U || t->month_day > max_day) {
		return false;
	}

	return true;
}

static int gnss_time_to_timespec_utc(const struct gnss_time *t, struct timespec *out)
{
	if (!gnss_time_is_valid(t) || out == NULL) {
		return -EINVAL;
	}

	/* GNSS provides year as 0..99. Treat as 2000..2099. */
	struct tm tm = {0};
	tm.tm_year = (int)(2000 + t->century_year) - 1900;
	tm.tm_mon = (int)t->month - 1;
	tm.tm_mday = (int)t->month_day;
	tm.tm_hour = (int)t->hour;
	tm.tm_min = (int)t->minute;
	tm.tm_sec = (int)(t->millisecond / 1000U);

	int64_t sec = timeutil_timegm64(&tm);
	if (sec < 0) {
		return -EINVAL;
	}

	out->tv_sec = (time_t)sec;
	out->tv_nsec = (long)((t->millisecond % 1000U) * 1000000UL);
	return 0;
}

static void timespec_add_ms(struct timespec *ts, int64_t add_ms)
{
	if (ts == NULL || add_ms == 0) {
		return;
	}

	/* Split into sec + nsec and normalize. */
	ts->tv_sec += (time_t)(add_ms / 1000);
	add_ms %= 1000;
	ts->tv_nsec += (long)(add_ms * 1000000LL);

	while (ts->tv_nsec >= 1000000000L) {
		ts->tv_nsec -= 1000000000L;
		ts->tv_sec += 1;
	}
	while (ts->tv_nsec < 0) {
		ts->tv_nsec += 1000000000L;
		ts->tv_sec -= 1;
	}
}

/* -------------------------------------------------------------------------- */
/* Runtime PM And Hardware Apply                                              */
/* -------------------------------------------------------------------------- */

static K_MUTEX_DEFINE(gnss_pm_mutex);
static bool gnss_pm_claimed;

static int gnss_pm_claim_acquire(void)
{
#if defined(CONFIG_PM_DEVICE) && defined(CONFIG_PM_DEVICE_RUNTIME)
	if (!gnss_device_ready()) {
		return 0;
	}

	k_mutex_lock(&gnss_pm_mutex, K_FOREVER);
	if (gnss_pm_claimed) {
		k_mutex_unlock(&gnss_pm_mutex);
		return 0;
	}

	/* If runtime PM isn't enabled for this device, we can't claim its domain. */
	if (!pm_device_runtime_is_enabled(gnss_dev)) {
		int en_rc = pm_device_runtime_enable(gnss_dev);

		if (en_rc != 0 && en_rc != -ENOTSUP && en_rc != -EBUSY) {
			k_mutex_unlock(&gnss_pm_mutex);
			return en_rc;
		}
		if (!pm_device_runtime_is_enabled(gnss_dev)) {
			LOG_WRN("GNSS runtime PM not enabled; power-domain may autosuspend");
			k_mutex_unlock(&gnss_pm_mutex);
			return 0;
		}
	}

	int rc = pm_device_runtime_get(gnss_dev);
	if (rc != 0) {
		k_mutex_unlock(&gnss_pm_mutex);
		return rc;
	}

	gnss_pm_claimed = true;
	k_mutex_unlock(&gnss_pm_mutex);

	LOG_DBG("GNSS runtime-PM claimed");
#endif
	return 0;
}

static int gnss_pm_claim_release(void)
{
#if defined(CONFIG_PM_DEVICE) && defined(CONFIG_PM_DEVICE_RUNTIME)
	if (!gnss_device_ready()) {
		return 0;
	}

	k_mutex_lock(&gnss_pm_mutex, K_FOREVER);
	if (!gnss_pm_claimed) {
		k_mutex_unlock(&gnss_pm_mutex);
		return 0;
	}

	int rc = pm_device_runtime_put(gnss_dev);

	/* -EALREADY can happen if get/put got unbalanced elsewhere; treat as released. */
	if (rc != 0 && rc != -EALREADY) {
		k_mutex_unlock(&gnss_pm_mutex);
		return rc;
	}

	gnss_pm_claimed = false;
	k_mutex_unlock(&gnss_pm_mutex);

	LOG_DBG("GNSS PM claim released");
#endif
	return 0;
}

static int gnss_resume(void)
{
#ifdef CONFIG_PM_DEVICE
	if (gnss_device_ready()) {
		enum pm_device_state state;
		int rc = pm_device_state_get(gnss_dev, &state);
		if (rc != 0) {
			return 0;
		}
		if (state == PM_DEVICE_STATE_ACTIVE) {
			return 0;
		}
		rc = pm_device_action_run(gnss_dev, PM_DEVICE_ACTION_RESUME);
		if (rc != 0 && rc != -EALREADY) {
			return rc;
		}
	}

	LOG_DBG("GNSS resumed");
#endif
	return 0;
}

static int gnss_suspend(void)
{
#ifdef CONFIG_PM_DEVICE
	if (gnss_device_ready()) {
		enum pm_device_state state;
		int rc = pm_device_state_get(gnss_dev, &state);
		if (rc != 0) {
			return 0;
		}
		if (state != PM_DEVICE_STATE_SUSPENDED) {
			rc = pm_device_action_run(gnss_dev, PM_DEVICE_ACTION_SUSPEND);
			if (rc != 0 && rc != -EALREADY) {
				return rc;
			}
		}
	}
	LOG_DBG("GNSS suspended");
#endif
	return 0;
}

static int gnss_shutdown(void)
{
	bool runtime_claim_released = false;

#ifdef CONFIG_PM_DEVICE
	/* If we currently hold a runtime-PM claim, release it.
	 * Releasing the claim allows the power-domain to autosuspend and issue TURN_OFF.
	 *
	 * Note: pm_device_runtime_put() will suspend the device as usage drops to zero,
	 * so avoid an extra SUSPEND here to prevent double-suspend side effects.
	 */
#if defined(CONFIG_PM_DEVICE_RUNTIME)
	if (gnss_device_ready()) {
		bool claimed;

		k_mutex_lock(&gnss_pm_mutex, K_FOREVER);
		claimed = gnss_pm_claimed;
		k_mutex_unlock(&gnss_pm_mutex);

		if (claimed) {
			int rc = gnss_pm_claim_release();
			if (rc != 0) {
				return rc;
			}
			LOG_DBG("GNSS shutdown (released runtime-PM claim)");
			runtime_claim_released = true;
		}
	}
#endif /* CONFIG_PM_DEVICE_RUNTIME */

	if (!runtime_claim_released) {
		/* No runtime-PM claim held; best-effort put the module to sleep. */
		(void)gnss_suspend();
		LOG_DBG("GNSS shutdown (suspended)");
	}
#endif

	k_mutex_lock(&gnss_data_mutex, K_FOREVER);
	gnss_cached_data_clear_locked();
#if defined(CONFIG_GNSS_SATELLITES) && defined(CONFIG_MESHBUS_GNSS_SATELLITE_CACHE_SIZE)
	gnss_sat_cache_count = 0;
	memset(gnss_sat_cache, 0, sizeof(gnss_sat_cache));
#endif
	k_mutex_unlock(&gnss_data_mutex);
	atomic_set(&gnss_state_flag, MESHBUS_GNSS_STATE_SLEEP);

#if defined(CONFIG_GNSS_SATELLITES) && defined(CONFIG_MESHBUS_GNSS_SATELLITE_CACHE_SIZE)
	LOG_DBG("Satellite cache cleared");
#endif

	return 0;
}

static int gnss_effective_system_mask(const meshbus_gnss_config *cfg,
				      gnss_systems_t *effective_mask)
{
	gnss_systems_t supported_systems = 0;
	int rc;

	if (cfg == NULL || effective_mask == NULL) {
		return -EINVAL;
	}

	*effective_mask = (gnss_systems_t)cfg->system_mask;

	rc = gnss_get_supported_systems(gnss_dev, &supported_systems);
	if (rc == 0 && supported_systems != 0) {
		gnss_systems_t unsupported = *effective_mask & ~supported_systems;

		if (unsupported != 0) {
			LOG_DBG("Ignoring unsupported GNSS systems: 0x%02x (supported: 0x%02x)",
				(unsigned int)unsupported, (unsigned int)supported_systems);
			*effective_mask &= supported_systems;
		}
		if (*effective_mask == 0) {
			LOG_WRN("No valid systems in config, using all supported: 0x%02x",
				(unsigned int)supported_systems);
			*effective_mask = supported_systems;
		}
	} else if (rc == -ENOSYS) {
		LOG_DBG("GNSS driver does not report supported systems, using config as-is");
	} else {
		LOG_WRN("Failed to get supported systems: %d, using config as-is", rc);
	}

	return 0;
}

static int gnss_apply_hardware_config(const meshbus_gnss_config *cfg)
{
	gnss_systems_t effective_mask;
	int rc;

	if (cfg == NULL) {
		return -EINVAL;
	}

	rc = gnss_set_fix_rate(gnss_dev, 1000U / cfg->fix_rate);
	if (rc == -ENOSYS) {
		LOG_DBG("GNSS driver does not support set_fix_rate");
	} else if (rc != 0) {
		LOG_ERR("Failed to set fix rate: %d", rc);
		return rc;
	}

	rc = gnss_set_navigation_mode(gnss_dev, (enum gnss_navigation_mode)cfg->nav_mode);
	if (rc == -ENOSYS) {
		LOG_DBG("GNSS driver does not support set_navigation_mode");
	} else if (rc != 0) {
		LOG_ERR("Failed to set navigation mode: %d", rc);
		return rc;
	}

	rc = gnss_effective_system_mask(cfg, &effective_mask);
	if (rc != 0) {
		return rc;
	}

	rc = gnss_set_enabled_systems(gnss_dev, effective_mask);
	if (rc == -ENOSYS) {
		LOG_DBG("GNSS driver does not support set_enabled_systems");
		return 0;
	}
	if (rc != 0) {
		LOG_ERR("Failed to set enabled systems: %d", rc);
		return rc;
	}

	return 0;
}

static int gnss_restore_previous_hardware_config(const meshbus_gnss_config *previous_cfg)
{
	int rc;

	if (previous_cfg == NULL) {
		return -EINVAL;
	}

	if (!previous_cfg->enabled) {
		rc = gnss_shutdown();
		if (rc != 0) {
			LOG_ERR("Failed to restore disabled GNSS state: %d", rc);
		}
		return rc;
	}

	rc = gnss_apply_hardware_config(previous_cfg);
	if (rc != 0) {
		LOG_ERR("Failed to restore previous GNSS hardware config: %d", rc);
		atomic_set(&gnss_state_flag, MESHBUS_GNSS_STATE_ERROR);
		return rc;
	}

	return 0;
}

/* -------------------------------------------------------------------------- */
/* Settings Schema And Apply                                                  */
/* -------------------------------------------------------------------------- */

#define MESHBUS_GNSS_SETTINGS_SUBTREE    "meshbus/gnss"
#define MESHBUS_GNSS_SETTINGS_KEY_CONFIG "config"
static bool settings_initial_apply = false;
static bool settings_load_in_progress;
static struct k_work_delayable settings_persistence_work;
static meshbus_gnss_config settings_load_cfg = MESHBUS_GNSS_CONFIG_DEFAULTS;
static struct mb_settings_blob_load_state settings_load_state;

MB_SETTINGS_BLOB_SCHEMA_DEFINE(gnss_settings_schema, MESHBUS_GNSS_SETTINGS_SUBTREE,
			       MESHBUS_GNSS_SETTINGS_KEY_CONFIG, meshbus_GnssConfig,
			       meshbus_gnss_config);

static int settings_handler_apply(const meshbus_gnss_config *cfg, bool persistence, bool force)
{
	meshbus_gnss_config normalized_cfg;
	int rc = 0;
	bool should_time_sync_run = false;
	bool should_cycle_run = false;
	bool device_available;
	bool allow_unavailable_device;
	meshbus_gnss_config previous_cfg;
	struct k_work_sync cycle_sync;

	if (cfg == NULL) {
		return -EINVAL;
	}
	normalized_cfg = *cfg;
	if (!normalized_cfg.has_electronic_compass) {
		normalized_cfg.has_electronic_compass = true;
		normalized_cfg.electronic_compass = true;
	}
	cfg = &normalized_cfg;
	rc = gnss_config_validate(cfg);
	if (rc != 0) {
		return rc;
	}
	device_available = gnss_device_ready();
	allow_unavailable_device = settings_load_in_progress && !persistence;

	k_mutex_lock(&settings_apply_mutex, K_FOREVER);

	/* Quiesce cycle work before changing enabled state/hardware config. */
	(void)k_work_cancel_delayable_sync(&gnss_cycle_work, &cycle_sync);

	k_mutex_lock(&settings_mutex, K_FOREVER);

	/* Public runtime updates keep reporting an unavailable enabled device. */
	if (cfg->enabled && !device_available && !allow_unavailable_device) {
		k_mutex_unlock(&settings_mutex);
		LOG_ERR("GNSS device not ready");
		rc = -ENODEV;
		goto out_unlock_apply;
	}

	/* Check if there are any changes (skip if force is set). */
	if (!force && gnss_cfg.enabled == cfg->enabled &&
	    gnss_cfg.update_interval == cfg->update_interval &&
	    gnss_cfg.nav_mode == cfg->nav_mode && gnss_cfg.fix_rate == cfg->fix_rate &&
	    gnss_cfg.system_mask == cfg->system_mask &&
	    gnss_cfg.min_active_time == cfg->min_active_time &&
	    gnss_cfg.time_sync == cfg->time_sync &&
	    gnss_cfg.has_electronic_compass == cfg->has_electronic_compass &&
	    gnss_cfg.electronic_compass == cfg->electronic_compass) {
		settings_initial_apply = true;
		k_mutex_unlock(&settings_mutex);
		if (cfg->enabled && device_available) {
			gnss_cycle_schedule(GNSS_CYCLE_PHASE_RESUME, K_NO_WAIT);
		}
		LOG_DBG("Settings unchanged, nothing to apply");
		goto out_unlock_apply;
	}

	previous_cfg = gnss_cfg;
	k_mutex_unlock(&settings_mutex);

	rc = meshbus_gnss_heading_config_apply(cfg->electronic_compass);
	if (rc != 0) {
		if (previous_cfg.enabled && device_available) {
			gnss_cycle_schedule(GNSS_CYCLE_PHASE_RESUME, K_NO_WAIT);
		}
		goto out_unlock_apply;
	}

	LOG_INF("Settings apply: enabled=%d nav_mode=%u fix_rate=%u system_mask=0x%02x "
		"update_interval=%u min_active_time=%u time_sync=%d electronic_compass=%d",
		cfg->enabled, (unsigned int)cfg->nav_mode, (unsigned int)cfg->fix_rate,
		(unsigned int)cfg->system_mask, (unsigned int)cfg->update_interval,
		(unsigned int)cfg->min_active_time, cfg->time_sync, cfg->electronic_compass);

	/* Hardware setting */
	if (cfg->enabled && device_available) {
		/* Claim runtime PM once while GNSS is enabled so the power-domain won't
		 * autosuspend. */
		rc = gnss_pm_claim_acquire();
		if (rc != 0) {
			LOG_WRN("Failed to claim GNSS runtime PM: %d", rc);
			(void)meshbus_gnss_heading_config_apply(previous_cfg.electronic_compass);
			if (previous_cfg.enabled) {
				gnss_cycle_schedule(GNSS_CYCLE_PHASE_RESUME, K_NO_WAIT);
			}
			goto out_unlock_apply;
		}

		rc = gnss_resume();
		if (rc != 0) {
			LOG_WRN("Failed to resume GNSS for apply settings: %d", rc);
			int restore_rc = gnss_restore_previous_hardware_config(&previous_cfg);
			int heading_restore_rc =
				meshbus_gnss_heading_config_apply(previous_cfg.electronic_compass);

			if (restore_rc != 0 || heading_restore_rc != 0) {
				LOG_ERR("GNSS hardware/config may be inconsistent after resume "
					"failure: apply=%d restore=%d heading_restore=%d",
					rc, restore_rc, heading_restore_rc);
			} else if (previous_cfg.enabled) {
				gnss_cycle_schedule(GNSS_CYCLE_PHASE_RESUME, K_NO_WAIT);
			}
			goto out_unlock_apply;
		}

		rc = gnss_apply_hardware_config(cfg);
		if (rc != 0) {
			int restore_rc = gnss_restore_previous_hardware_config(&previous_cfg);
			int heading_restore_rc =
				meshbus_gnss_heading_config_apply(previous_cfg.electronic_compass);

			if (restore_rc != 0 || heading_restore_rc != 0) {
				LOG_ERR("GNSS hardware/config may be inconsistent after apply "
					"failure: apply=%d restore=%d heading_restore=%d",
					rc, restore_rc, heading_restore_rc);
			} else if (previous_cfg.enabled) {
				gnss_cycle_schedule(GNSS_CYCLE_PHASE_RESUME, K_NO_WAIT);
			}
			goto out_unlock_apply;
		}

		should_cycle_run = true;
	} else if (!cfg->enabled) {
		rc = gnss_shutdown();
		if (rc != 0) {
			LOG_WRN("Failed to shutdown GNSS for apply settings: %d", rc);
			(void)meshbus_gnss_heading_config_apply(previous_cfg.electronic_compass);
			goto out_unlock_apply;
		}
	} else {
		/*
		 * Persisted desired state remains visible on Compass-only hardware, but
		 * no GNSS work, time sync, PM claim, or driver operation may start.
		 */
		atomic_set(&gnss_state_flag, MESHBUS_GNSS_STATE_ERROR);
		LOG_WRN("GNSS enabled in settings but no device is available; hardware idle");
	}

	k_mutex_lock(&settings_mutex, K_FOREVER);
	memcpy(&gnss_cfg, cfg, sizeof(meshbus_gnss_config));
	atomic_set(&gnss_enabled_flag, cfg->enabled ? 1 : 0);
	atomic_set(&gnss_time_sync_flag, cfg->time_sync ? 1 : 0);
	should_time_sync_run = cfg->enabled && cfg->time_sync && device_available;
	settings_initial_apply = true;
	k_mutex_unlock(&settings_mutex);

	/* Start/stop time sync work.
	 *
	 * The work self-reschedules:
	 * - initial phase: poll every CONFIG_MESHBUS_GNSS_TIME_SYNC_INIT_INTERVAL until a valid fix
	 * is observed
	 * - steady phase: poll every CONFIG_MESHBUS_GNSS_TIME_SYNC_CAL_INTERVAL and sync only when
	 * a new valid-fix sample arrives (detected via gnss_latest_valid_fix_rx_uptime_ms changes).
	 */
	if (should_time_sync_run) {
		k_mutex_lock(&gnss_data_mutex, K_FOREVER);
		gnss_time_sync_cal_mode = false;
		gnss_time_sync_last_rx_uptime_ms = 0U;
		k_mutex_unlock(&gnss_data_mutex);

		(void)k_work_reschedule(&gnss_time_sync_work, K_NO_WAIT);
	} else {
		(void)k_work_cancel_delayable(&gnss_time_sync_work);
		k_mutex_lock(&gnss_data_mutex, K_FOREVER);
		gnss_time_sync_cal_mode = false;
		gnss_time_sync_last_rx_uptime_ms = 0U;
		k_mutex_unlock(&gnss_data_mutex);
	}

	if (persistence) {
		k_work_reschedule(&settings_persistence_work,
				  K_MSEC(CONFIG_MESHBUS_SETTINGS_PERSISTENCE_DELAY));
	}

	if (should_cycle_run) {
		gnss_cycle_schedule(GNSS_CYCLE_PHASE_RESUME, K_NO_WAIT);
	}

out_unlock_apply:
	k_mutex_unlock(&settings_apply_mutex);
	return rc;
}

MB_SETTINGS_BLOB_CONFIG_DEFINE(gnss_settings_schema, settings_mutex, settings_load_state,
			       settings_load_cfg, gnss_cfg, settings_initial_apply,
			       meshbus_gnss_config, meshbus_GnssConfig_size,
			       settings_handler_apply, "GNSS")

SETTINGS_STATIC_HANDLER_DEFINE(meshbus_gnss, MESHBUS_GNSS_SETTINGS_SUBTREE, NULL,
			       settings_handle_set, settings_handle_commit, settings_handle_export);

/* -------------------------------------------------------------------------- */
/* Callbacks And Work                                                         */
/* -------------------------------------------------------------------------- */

#if DT_HAS_CHOSEN(meshbus_gnss)
static void gnss_data_callback(const struct device *dev, const struct gnss_data *data)
{
	ARG_UNUSED(dev);

	k_mutex_lock(&gnss_data_mutex, K_FOREVER);

	/* Mark as valid if we have a fix */
	if (data->info.fix_status != GNSS_FIX_STATUS_NO_FIX) {
		uint32_t rx_uptime_ms = k_uptime_get_32();

		/* Keep a copy of the latest valid fix for end-of-sample publishing. */
		memcpy(&gnss_latest_valid_fix, data, sizeof(struct gnss_data));
		gnss_latest_valid_fix_rx_uptime_ms = rx_uptime_ms;
#ifdef CONFIG_MESHBUS_GNSS_STATS
		/* Calculate TTFF on first fix of this acquisition cycle */
		if (!has_valid_fix && acquisition_start_time > 0) {
			int64_t now = k_uptime_get();
			uint32_t ttff_ms = (uint32_t)(now - acquisition_start_time);

			STATS_SET(meshbus_gnss_stats, ttff_last, ttff_ms);

			/* Update min (initialize on first measurement) */
			if (meshbus_gnss_stats.ttff_min == 0 ||
			    ttff_ms < meshbus_gnss_stats.ttff_min) {
				STATS_SET(meshbus_gnss_stats, ttff_min, ttff_ms);
			}

			/* Update max */
			if (ttff_ms > meshbus_gnss_stats.ttff_max) {
				STATS_SET(meshbus_gnss_stats, ttff_max, ttff_ms);
			}

			LOG_DBG("TTFF: %u ms (min=%u, max=%u)", ttff_ms,
				meshbus_gnss_stats.ttff_min, meshbus_gnss_stats.ttff_max);

			/* Reset start time to prevent re-calculation on subsequent fixes */
			acquisition_start_time = 0;
		}
#endif
		has_valid_fix = true;
		atomic_set(&gnss_state_flag, MESHBUS_GNSS_STATE_TRACK);
		LOG_DBG("GNSS fix: lat=%lld, lon=%lld, sats=%u, hdop=%u",
			(long long)data->nav_data.latitude, (long long)data->nav_data.longitude,
			data->info.satellites_cnt, data->info.hdop);
	}

	k_mutex_unlock(&gnss_data_mutex);
}

#if defined(CONFIG_GNSS_SATELLITES) && defined(CONFIG_MESHBUS_GNSS_SATELLITE_CACHE_SIZE)
static int gnss_satellite_find_index_by_key(enum gnss_system system, uint8_t prn)
{
	for (uint16_t i = 0; i < gnss_sat_cache_count; i++) {
		/* (system, prn) is the stable unique key; PRNs can overlap across systems. */
		if ((gnss_sat_cache[i].system == system) && (gnss_sat_cache[i].prn == prn)) {
			return (int)i;
		}
	}
	return -1;
}

/**
 * @brief Find a low-value satellite entry that can be replaced
 *
 * Picks the first entry that is not tracked or has low SNR/elevation.
 *
 * @return Index of a replacement candidate, or -1 if none found
 */
static int gnss_satellite_find_invalid_index(void)
{
	for (uint16_t i = 0; i < gnss_sat_cache_count; i++) {
		if (gnss_sat_cache[i].snr < 10 || gnss_sat_cache[i].elevation < 10 ||
		    !gnss_sat_cache[i].is_tracked) {
			return (int)i;
		}
	}
	return -1;
}

static void gnss_satellites_callback(const struct device *dev,
				     const struct gnss_satellite *satellites, uint16_t size)
{
	ARG_UNUSED(dev);

	if (satellites == NULL || size == 0) {
		return;
	}

	k_mutex_lock(&gnss_data_mutex, K_FOREVER);

	for (uint16_t i = 0; i < size; i++) {
		const struct gnss_satellite *sat = &satellites[i];

		/* Find existing satellite by (system, PRN) to avoid cross-system collisions. */
		int idx = gnss_satellite_find_index_by_key(sat->system, sat->prn);

		if (idx >= 0) {
			/* Update existing satellite entry */
			memcpy(&gnss_sat_cache[idx], sat, sizeof(struct gnss_satellite));
		} else if (gnss_sat_cache_count < CONFIG_MESHBUS_GNSS_SATELLITE_CACHE_SIZE) {
			/* Add new satellite entry */
			memcpy(&gnss_sat_cache[gnss_sat_cache_count], sat,
			       sizeof(struct gnss_satellite));
			gnss_sat_cache_count++;
		} else {
			/* Cache full - try to replace an invalid entry */
			int invalid_idx = gnss_satellite_find_invalid_index();

			if (invalid_idx >= 0) {
				/* Replace invalid entry with new satellite */
				LOG_DBG("Replacing low-value satellite idx=%d (sys=%u PRN=%u) with "
					"(sys=%u PRN=%u)",
					invalid_idx, gnss_sat_cache[invalid_idx].system,
					gnss_sat_cache[invalid_idx].prn, sat->system, sat->prn);
				memcpy(&gnss_sat_cache[invalid_idx], sat,
				       sizeof(struct gnss_satellite));
			} else {
				LOG_WRN("Satellite cache full, ignoring sys=%u PRN=%u", sat->system,
					sat->prn);
			}
		}
	}

	k_mutex_unlock(&gnss_data_mutex);
}
#endif

GNSS_DATA_CALLBACK_DEFINE(DEVICE_DT_GET(DT_CHOSEN(meshbus_gnss)), gnss_data_callback);
#if defined(CONFIG_GNSS_SATELLITES) && defined(CONFIG_MESHBUS_GNSS_SATELLITE_CACHE_SIZE)
GNSS_SATELLITES_CALLBACK_DEFINE(DEVICE_DT_GET(DT_CHOSEN(meshbus_gnss)), gnss_satellites_callback);
#else
GNSS_SATELLITES_CALLBACK_DEFINE(DEVICE_DT_GET(DT_CHOSEN(meshbus_gnss)), NULL);
#endif
#endif

static void gnss_time_sync_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	bool enabled = atomic_get(&gnss_enabled_flag) != 0;
	bool time_sync = atomic_get(&gnss_time_sync_flag) != 0;

	if (!enabled || !time_sync) {
		return;
	}

	struct gnss_time utc;
	uint32_t rx_uptime_ms = 0U;
	uint32_t last_rx_uptime_ms = 0U;
	bool cal_mode = false;
	enum gnss_fix_status fix_status = GNSS_FIX_STATUS_NO_FIX;

	/* Snapshot the latest valid fix sample quickly; don't hold the mutex while converting. */
	k_mutex_lock(&gnss_data_mutex, K_FOREVER);
	last_rx_uptime_ms = gnss_time_sync_last_rx_uptime_ms;
	cal_mode = gnss_time_sync_cal_mode;
	fix_status = gnss_latest_valid_fix.info.fix_status;
	utc = gnss_latest_valid_fix.utc;
	rx_uptime_ms = gnss_latest_valid_fix_rx_uptime_ms;

	bool has_valid_fix_sample = (fix_status != GNSS_FIX_STATUS_NO_FIX) && (rx_uptime_ms != 0U);
	bool new_sample = has_valid_fix_sample && (rx_uptime_ms != last_rx_uptime_ms);

	/* Fast poll until we see the first valid fix, then slow poll waiting for a new sample. */
	if (!new_sample) {
		k_mutex_unlock(&gnss_data_mutex);
		(void)k_work_reschedule(
			&gnss_time_sync_work,
			K_MSEC(cal_mode ? CONFIG_MESHBUS_GNSS_TIME_SYNC_CAL_INTERVAL
					: CONFIG_MESHBUS_GNSS_TIME_SYNC_INIT_INTERVAL));
		return;
	}

	/* Consume this sample so we don't re-process it on subsequent polls. */
	gnss_time_sync_last_rx_uptime_ms = rx_uptime_ms;
	gnss_time_sync_cal_mode = true;
	k_mutex_unlock(&gnss_data_mutex);

	/* Compensate GNSS timestamp to "now" using uptime delta since the sample was received. */
	uint32_t elapsed_ms = k_uptime_get_32() - rx_uptime_ms;

	struct timespec target;
	if (gnss_time_to_timespec_utc(&utc, &target) != 0) {
		LOG_DBG("Time sync: invalid GNSS UTC, skip");
		(void)k_work_reschedule(&gnss_time_sync_work,
					K_MSEC(CONFIG_MESHBUS_GNSS_TIME_SYNC_CAL_INTERVAL));
		return;
	}
	timespec_add_ms(&target, (int64_t)elapsed_ms);

	struct timespec now;
	int rc = sys_clock_gettime(SYS_CLOCK_REALTIME, &now);
	if (rc != 0) {
		LOG_DBG("Time sync: gettime failed: %d", rc);
		(void)k_work_reschedule(&gnss_time_sync_work,
					K_MSEC(CONFIG_MESHBUS_GNSS_TIME_SYNC_CAL_INTERVAL));
		return;
	}

	int64_t diff_s = (int64_t)target.tv_sec - (int64_t)now.tv_sec;
	if (diff_s < 0) {
		diff_s = -diff_s;
	}

	if (diff_s <= (int64_t)CONFIG_MESHBUS_GNSS_TIME_SYNC_THRESHOLD_S) {
		LOG_DBG("Time sync: skip (diff=%llds <= %ds)", (long long)diff_s,
			CONFIG_MESHBUS_GNSS_TIME_SYNC_THRESHOLD_S);
		(void)k_work_reschedule(&gnss_time_sync_work,
					K_MSEC(CONFIG_MESHBUS_GNSS_TIME_SYNC_CAL_INTERVAL));
		return;
	}

	rc = sys_clock_settime(SYS_CLOCK_REALTIME, &target);
	if (rc != 0) {
		LOG_WRN("Time sync: settime failed: %d", rc);
		(void)k_work_reschedule(&gnss_time_sync_work,
					K_MSEC(CONFIG_MESHBUS_GNSS_TIME_SYNC_CAL_INTERVAL));
		return;
	}

	LOG_INF("Time sync: updated system time (diff=%llds)", (long long)diff_s);
	(void)k_work_reschedule(&gnss_time_sync_work,
				K_MSEC(CONFIG_MESHBUS_GNSS_TIME_SYNC_CAL_INTERVAL));
}

static void gnss_cycle_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	enum gnss_cycle_phase phase = (enum gnss_cycle_phase)atomic_get(&gnss_cycle_phase);
	struct meshbus_gnss_data_event event = {0};
	uint32_t interval_ms;
	uint32_t active_window_ms;
	bool enabled;

	k_mutex_lock(&settings_mutex, K_FOREVER);
	enabled = gnss_cfg.enabled;
	interval_ms = gnss_cfg.update_interval;
	active_window_ms = gnss_active_window_ms(interval_ms, gnss_cfg.min_active_time);
	k_mutex_unlock(&settings_mutex);

	if (!enabled) {
		return;
	}

	if (phase == GNSS_CYCLE_PHASE_RESUME) {
		LOG_INF("Starting GNSS fix acquisition");

#ifdef CONFIG_MESHBUS_GNSS_STATS
		/* Record acquisition start time for TTFF calculation */
		acquisition_start_time = k_uptime_get();
#endif

		/* Start a new acquisition cycle while keeping last valid fix readable via API. */
		k_mutex_lock(&gnss_data_mutex, K_FOREVER);
		has_valid_fix = false;
		k_mutex_unlock(&gnss_data_mutex);

		/* Resume GNSS hardware */
		int rc = gnss_resume();
		if (rc != 0) {
			LOG_ERR("Failed to resume GNSS, aborting acquisition: %d", rc);
			atomic_set(&gnss_state_flag, MESHBUS_GNSS_STATE_SLEEP);

			/* Still schedule next cycle if service remains enabled. */
			k_mutex_lock(&settings_mutex, K_FOREVER);
			enabled = gnss_cfg.enabled;
			interval_ms = gnss_cfg.update_interval;
			k_mutex_unlock(&settings_mutex);
			if (enabled) {
				gnss_cycle_schedule(GNSS_CYCLE_PHASE_RESUME, K_MSEC(interval_ms));
			}
			return;
		}

		atomic_set(&gnss_state_flag, MESHBUS_GNSS_STATE_ACQUIRING);
		gnss_cycle_schedule(GNSS_CYCLE_PHASE_CHECK, K_MSEC(active_window_ms));
		LOG_DBG("Sample end scheduled in %u ms", (unsigned int)active_window_ms);
		return;
	}

	/* CHECK phase: publish latest valid fix if available. */
	bool short_interval_mode = (active_window_ms >= interval_ms);
	bool has_fix;

	k_mutex_lock(&gnss_data_mutex, K_FOREVER);
	has_fix = has_valid_fix;
	k_mutex_unlock(&gnss_data_mutex);

	if (!has_fix) {
		atomic_set(&gnss_state_flag, MESHBUS_GNSS_STATE_ACQUIRING);
		LOG_DBG("No valid fix yet, continuing GNSS acquisition");
		gnss_cycle_schedule(GNSS_CYCLE_PHASE_CHECK, K_MSEC(active_window_ms));
		return;
	}

	if (!short_interval_mode) {
		/* Normal mode: suspend GNSS device (keep power on for next cycle) */
		int rc = gnss_suspend();
		if (rc == -ENOTSUP) {
			LOG_DBG("GNSS driver does not support suspend after sample window");
		} else if (rc != 0) {
			LOG_WRN("Failed to suspend GNSS after sample window: %d", rc);
		} else {
			atomic_set(&gnss_state_flag, MESHBUS_GNSS_STATE_SLEEP);
		}
	}
	/* In short interval mode, keep GNSS active for continuous positioning */

	k_mutex_lock(&gnss_data_mutex, K_FOREVER);
	/* Publish the latest valid fix captured during this sample window. */
	event.nav_data = gnss_latest_valid_fix.nav_data;
	event.info = gnss_latest_valid_fix.info;
	event.utc = gnss_latest_valid_fix.utc;
	event.valid = true;
	has_valid_fix = false;
	k_mutex_unlock(&gnss_data_mutex);

	LOG_INF("GNSS position update: sats=%u hdop=%u.%03u",
		event.info.satellites_cnt, event.info.hdop / 1000, event.info.hdop % 1000);

#if (CONFIG_MESHBUS_GNSS_LOG_LEVEL >= LOG_LEVEL_DBG)
	/* Log full position in human-readable format when GNSS debug logs are enabled. */
	int64_t lat_deg = event.nav_data.latitude / 1000000000LL;
	int64_t lat_frac = (event.nav_data.latitude % 1000000000LL) / 1000LL;
	int64_t lon_deg = event.nav_data.longitude / 1000000000LL;
	int64_t lon_frac = (event.nav_data.longitude % 1000000000LL) / 1000LL;

	if (lat_frac < 0) {
		lat_frac = -lat_frac;
	}
	if (lon_frac < 0) {
		lon_frac = -lon_frac;
	}

	LOG_DBG("GNSS position detail: lat=%lld.%06lld lon=%lld.%06lld alt=%d.%03um sats=%u "
		"hdop=%u.%03u)",
		(long long)lat_deg, (long long)lat_frac, (long long)lon_deg, (long long)lon_frac,
		event.nav_data.altitude / 1000, (unsigned int)abs(event.nav_data.altitude % 1000),
		event.info.satellites_cnt, event.info.hdop / 1000, event.info.hdop % 1000);
#endif

	/* Publish event to ZBus */
	int rc = zbus_chan_pub(&meshbus_gnss_data_chan, &event, K_NO_WAIT);
	if (rc != 0) {
		LOG_ERR("Failed to publish GNSS event: %d", rc);
	}

	/* Schedule next update cycle if still enabled */
	k_mutex_lock(&settings_mutex, K_FOREVER);
	enabled = gnss_cfg.enabled;
	interval_ms = gnss_cfg.update_interval;
	k_mutex_unlock(&settings_mutex);

	if (!enabled) {
		return;
	}

	if (short_interval_mode) {
		/* Keep GNSS active, reschedule publish check */
		gnss_cycle_schedule(GNSS_CYCLE_PHASE_CHECK, K_MSEC(interval_ms));
		LOG_DBG("Short interval: next check in %u ms", interval_ms);
	} else {
		/* Normal mode: wait for next cycle */
		gnss_cycle_schedule(GNSS_CYCLE_PHASE_RESUME, K_MSEC(interval_ms));
		LOG_DBG("Next update in %u ms", interval_ms);
	}
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

int meshbus_gnss_config_get(meshbus_gnss_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&settings_mutex, K_FOREVER);
	memcpy(cfg, &gnss_cfg, sizeof(meshbus_gnss_config));
	k_mutex_unlock(&settings_mutex);

	return 0;
}

int meshbus_gnss_config_set(const meshbus_gnss_config *cfg)
{
	meshbus_gnss_config compatible = meshbus_GnssConfig_init_zero;
	bool electronic_compass;
	int rc;

	if (cfg == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&settings_transaction_mutex, K_FOREVER);

	/*
	 * ABI-v1 callers own fields only through time_sync.  Copy those fields one
	 * by one so the generated tail booleans are never read from what used to be
	 * uninitialized padding in an older LLEXT binary.
	 */
	compatible.enabled = cfg->enabled;
	compatible.nav_mode = cfg->nav_mode;
	compatible.fix_rate = cfg->fix_rate;
	compatible.system_mask = cfg->system_mask;
	compatible.update_interval = cfg->update_interval;
	compatible.min_active_time = cfg->min_active_time;
	compatible.time_sync = cfg->time_sync;

	k_mutex_lock(&settings_mutex, K_FOREVER);
	electronic_compass = gnss_cfg.electronic_compass;
	k_mutex_unlock(&settings_mutex);
	compatible.has_electronic_compass = true;
	compatible.electronic_compass = electronic_compass;

	rc = settings_handler_apply(&compatible, true, false);
	k_mutex_unlock(&settings_transaction_mutex);
	return rc;
}

int meshbus_gnss_config_set_full(const meshbus_gnss_config *cfg)
{
	int rc;

	if (cfg == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&settings_transaction_mutex, K_FOREVER);
	rc = settings_handler_apply(cfg, true, false);
	k_mutex_unlock(&settings_transaction_mutex);
	return rc;
}

int meshbus_gnss_config_reset(void)
{
	meshbus_gnss_config cfg = MESHBUS_GNSS_CONFIG_DEFAULTS;
	struct k_work_sync sync;
	int rc;

	k_mutex_lock(&settings_transaction_mutex, K_FOREVER);
	(void)k_work_cancel_delayable_sync(&settings_persistence_work, &sync);

	rc = settings_handler_apply(&cfg, false, true);
	if (rc != 0) {
		goto out_unlock;
	}

	rc = mb_settings_blob_delete(&gnss_settings_schema);
	if (rc != 0) {
		LOG_ERR("Failed to delete persisted settings: %d", rc);
		goto out_unlock;
	}

out_unlock:
	k_mutex_unlock(&settings_transaction_mutex);
	return rc;
}

enum meshbus_gnss_state meshbus_gnss_state_get(void)
{
	return (enum meshbus_gnss_state)atomic_get(&gnss_state_flag);
}

int meshbus_gnss_acquisition(void)
{
	if (atomic_get(&gnss_enabled_flag) == 0) {
		return -EACCES;
	}
	if (!gnss_device_ready()) {
		return -ENODEV;
	}

	k_mutex_lock(&settings_apply_mutex, K_FOREVER);

	/* If the "sample end / no-fix check" work is pending, we're in an acquisition window.
	 * In that case, don't interrupt it.
	 *
	 * Note: a RESUME phase can be pending simply because we're waiting for the next
	 * periodic cycle; meshbus_gnss_acquisition() should override that and start immediately.
	 */
	if (k_work_delayable_is_pending(&gnss_cycle_work) &&
	    (atomic_get(&gnss_cycle_phase) == GNSS_CYCLE_PHASE_CHECK)) {
		k_mutex_unlock(&settings_apply_mutex);
		LOG_WRN("Fix acquisition already in progress");
		return -EBUSY;
	}

	/* Override any scheduled next-cycle work. */
	(void)k_work_cancel_delayable(&gnss_cycle_work);

	/* Trigger immediate update */
	gnss_cycle_schedule(GNSS_CYCLE_PHASE_RESUME, K_NO_WAIT);
	k_mutex_unlock(&settings_apply_mutex);

	LOG_INF("GNSS acquisition triggered");

	return 0;
}

int meshbus_gnss_position_get(struct navigation_data *nav)
{
	const struct gnss_data *fix;

	if (nav == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&gnss_data_mutex, K_FOREVER);

	fix = gnss_cached_fix_select_locked();
	if (fix == NULL) {
		k_mutex_unlock(&gnss_data_mutex);
		return -ENODATA;
	}

	memcpy(nav, &fix->nav_data, sizeof(struct navigation_data));

	k_mutex_unlock(&gnss_data_mutex);

	return 0;
}

int meshbus_gnss_fix_snapshot_get(struct meshbus_gnss_data_event *event,
				  uint32_t *source_timestamp_ms)
{
	const struct gnss_data *fix;

	if (event == NULL || source_timestamp_ms == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&gnss_data_mutex, K_FOREVER);
	fix = gnss_cached_fix_select_locked();
	if (fix == NULL) {
		k_mutex_unlock(&gnss_data_mutex);
		return -ENODATA;
	}
	event->nav_data = fix->nav_data;
	event->info = fix->info;
	event->utc = fix->utc;
	event->valid = true;
	*source_timestamp_ms = gnss_latest_valid_fix_rx_uptime_ms;
	k_mutex_unlock(&gnss_data_mutex);

	return 0;
}

int meshbus_gnss_info_get(struct gnss_info *info)
{
	const struct gnss_data *fix;

	if (info == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&gnss_data_mutex, K_FOREVER);
	fix = gnss_cached_fix_select_locked();
	if (fix == NULL) {
		k_mutex_unlock(&gnss_data_mutex);
		return -ENODATA;
	}
	memcpy(info, &fix->info, sizeof(struct gnss_info));
	k_mutex_unlock(&gnss_data_mutex);

	return 0;
}

int meshbus_gnss_time_get(struct gnss_time *time)
{
	const struct gnss_data *fix;

	if (time == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&gnss_data_mutex, K_FOREVER);
	fix = gnss_cached_fix_select_locked();
	if (fix == NULL) {
		k_mutex_unlock(&gnss_data_mutex);
		return -ENODATA;
	}
	memcpy(time, &fix->utc, sizeof(struct gnss_time));
	k_mutex_unlock(&gnss_data_mutex);

	return 0;
}

#if defined(CONFIG_GNSS_SATELLITES) && defined(CONFIG_MESHBUS_GNSS_SATELLITE_CACHE_SIZE)
int meshbus_gnss_satellites_get(struct gnss_satellite *satellites, uint16_t max_count,
				uint16_t *count)
{
	if (satellites == NULL || count == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&gnss_data_mutex, K_FOREVER);

	if (gnss_sat_cache_count == 0) {
		k_mutex_unlock(&gnss_data_mutex);
		*count = 0;
		return -ENODATA;
	}

	uint16_t copy_count = MIN(gnss_sat_cache_count, max_count);
	memcpy(satellites, gnss_sat_cache, copy_count * sizeof(struct gnss_satellite));
	*count = copy_count;

	k_mutex_unlock(&gnss_data_mutex);

	return 0;
}

int meshbus_gnss_satellite_get_by_index(uint16_t index, struct gnss_satellite *satellite)
{
	if (satellite == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&gnss_data_mutex, K_FOREVER);
	if (index >= gnss_sat_cache_count) {
		k_mutex_unlock(&gnss_data_mutex);
		return -ENODATA;
	}

	*satellite = gnss_sat_cache[index];
	k_mutex_unlock(&gnss_data_mutex);

	return 0;
}

uint16_t meshbus_gnss_satellites_count(void)
{
	uint16_t count;

	k_mutex_lock(&gnss_data_mutex, K_FOREVER);
	count = gnss_sat_cache_count;
	k_mutex_unlock(&gnss_data_mutex);

	return count;
}

void meshbus_gnss_satellites_cache_clear(void)
{
	k_mutex_lock(&gnss_data_mutex, K_FOREVER);
	gnss_sat_cache_count = 0;
	memset(gnss_sat_cache, 0, sizeof(gnss_sat_cache));
	k_mutex_unlock(&gnss_data_mutex);

	LOG_DBG("Satellite cache cleared");
}
#endif

/* -------------------------------------------------------------------------- */
/* Power Callback                                                             */
/* -------------------------------------------------------------------------- */

static void meshbus_power_gnss_cb(enum meshbus_power_action action, void *user_data)
{
	ARG_UNUSED(user_data);

	if (action != MESHBUS_POWER_ACTION_SHUTDOWN && action != MESHBUS_POWER_ACTION_REBOOT) {
		return;
	}

	k_mutex_lock(&settings_apply_mutex, K_FOREVER);

	/* Prevent any future activity while we unwind PM holds. */
	(void)k_work_cancel_delayable(&settings_persistence_work);
	(void)k_work_cancel_delayable(&gnss_cycle_work);
	(void)k_work_cancel_delayable(&gnss_time_sync_work);

	/* Release runtime-PM claim / suspend GNSS if applicable. */
	(void)gnss_shutdown();
	k_mutex_unlock(&settings_apply_mutex);

	LOG_INF("GNSS stopped");
}
MESHBUS_POWER_ACTION_CALLBACK_DEFINE(meshbus_power_gnss_cb, NULL);

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

static int meshbus_gnss_init(void)
{
	int rc;

#ifdef CONFIG_MESHBUS_GNSS_STATS
	rc = STATS_INIT_AND_REG(meshbus_gnss_stats, STATS_SIZE_32, "meshbus_gnss_stats");
	if (rc != 0) {
		LOG_WRN("Failed to register stats: %d", rc);
	}
#endif

	/* Initialize work items */
	k_work_init_delayable(&settings_persistence_work, settings_persistence_work_handler);
	k_work_init_delayable(&gnss_cycle_work, gnss_cycle_work_handler);
	k_work_init_delayable(&gnss_time_sync_work, gnss_time_sync_work_handler);

	/* Optional GNSS hardware does not own the settings/Compass configuration. */
	if (gnss_dev == NULL) {
		LOG_WRN("No GNSS device configured (missing chosen meshbus_gnss in DT)");
	} else if (!gnss_device_ready()) {
		LOG_WRN("GNSS device not ready");
	} else {
		LOG_INF("GNSS device ready (%s)", gnss_dev->name);
	}

	/* Load configuration even on Compass-only or temporarily unavailable hardware. */
	settings_load_in_progress = true;
	rc = settings_load_subtree(MESHBUS_GNSS_SETTINGS_SUBTREE);
	if (rc == 0 && !settings_initial_apply) {
		rc = settings_handler_apply(&gnss_cfg, false, true);
	}
	settings_load_in_progress = false;

	return rc;
}

SYS_INIT(meshbus_gnss_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
