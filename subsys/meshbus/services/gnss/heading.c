/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/meshbus/gnss.h>
#include <zephyr/meshbus/power.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "heading.h"

#if DT_HAS_CHOSEN(meshbus_compass)
#define MESHBUS_GNSS_HEADING_HAS_ELECTRONIC 1
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/compass.h>
#include <zephyr/pm/device_runtime.h>
#if defined(CONFIG_COMPASS_COMPOSITE)
#include <zephyr/drivers/sensor/compass_composite.h>
#endif
#endif

LOG_MODULE_REGISTER(meshbus_gnss_heading, CONFIG_MESHBUS_GNSS_LOG_LEVEL);

#define HEADING_ENUM_ASSERT(local_value, wire_value)                                      \
	BUILD_ASSERT((int)(local_value) == (int)(wire_value),                              \
		     #local_value " must match " #wire_value)

HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_SOURCE_UNSPECIFIED,
		    meshbus_GnssHeadingSource_GNSS_HEADING_SOURCE_UNSPECIFIED);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_SOURCE_ELECTRONIC,
		    meshbus_GnssHeadingSource_GNSS_HEADING_SOURCE_ELECTRONIC);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_SOURCE_COURSE,
		    meshbus_GnssHeadingSource_GNSS_HEADING_SOURCE_COURSE);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_STATE_UNAVAILABLE,
		    meshbus_GnssHeadingState_GNSS_HEADING_STATE_UNAVAILABLE);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_STATE_IDLE,
		    meshbus_GnssHeadingState_GNSS_HEADING_STATE_IDLE);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_STATE_STARTING,
		    meshbus_GnssHeadingState_GNSS_HEADING_STATE_STARTING);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_STATE_READY,
		    meshbus_GnssHeadingState_GNSS_HEADING_STATE_READY);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_STATE_WAITING_FOR_FIX,
		    meshbus_GnssHeadingState_GNSS_HEADING_STATE_WAITING_FOR_FIX);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_STATE_WAITING_FOR_MOTION,
		    meshbus_GnssHeadingState_GNSS_HEADING_STATE_WAITING_FOR_MOTION);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_STATE_STALE,
		    meshbus_GnssHeadingState_GNSS_HEADING_STATE_STALE);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_STATE_SOURCE_DISABLED,
		    meshbus_GnssHeadingState_GNSS_HEADING_STATE_SOURCE_DISABLED);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_STATE_ERROR,
		    meshbus_GnssHeadingState_GNSS_HEADING_STATE_ERROR);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_ACCURACY_UNRELIABLE,
		    meshbus_GnssHeadingAccuracy_GNSS_HEADING_ACCURACY_UNRELIABLE);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_ACCURACY_LOW,
		    meshbus_GnssHeadingAccuracy_GNSS_HEADING_ACCURACY_LOW);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_ACCURACY_MEDIUM,
		    meshbus_GnssHeadingAccuracy_GNSS_HEADING_ACCURACY_MEDIUM);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_ACCURACY_HIGH,
		    meshbus_GnssHeadingAccuracy_GNSS_HEADING_ACCURACY_HIGH);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_CALIBRATION_HINT_NONE,
		    meshbus_GnssHeadingCalibrationHint_GNSS_HEADING_CALIBRATION_HINT_NONE);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_CALIBRATION_HINT_FIGURE_EIGHT,
		    meshbus_GnssHeadingCalibrationHint_GNSS_HEADING_CALIBRATION_HINT_FIGURE_EIGHT);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_CALIBRATION_HINT_KEEP_LEVEL,
		    meshbus_GnssHeadingCalibrationHint_GNSS_HEADING_CALIBRATION_HINT_KEEP_LEVEL);

#if defined(MESHBUS_GNSS_HEADING_HAS_ELECTRONIC)
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_ACCURACY_UNRELIABLE, COMPASS_ACCURACY_UNRELIABLE);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_ACCURACY_LOW, COMPASS_ACCURACY_LOW);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_ACCURACY_MEDIUM, COMPASS_ACCURACY_MEDIUM);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_ACCURACY_HIGH, COMPASS_ACCURACY_HIGH);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_CALIBRATION_HINT_NONE, COMPASS_CAL_HINT_NONE);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_CALIBRATION_HINT_FIGURE_EIGHT,
		    COMPASS_CAL_HINT_FIGURE_EIGHT);
HEADING_ENUM_ASSERT(MESHBUS_GNSS_HEADING_CALIBRATION_HINT_KEEP_LEVEL,
		    COMPASS_CAL_HINT_KEEP_LEVEL);
#endif

#undef HEADING_ENUM_ASSERT

static struct k_work_delayable compass_sample_work;
/*
 * Control paths take transition_mutex before runtime_mutex, then drop
 * runtime_mutex before waiting for provider_io_mutex.  Sampling may recheck
 * runtime state while holding provider_io_mutex; it never takes transition_mutex.
 */
static K_MUTEX_DEFINE(runtime_mutex);
static K_MUTEX_DEFINE(transition_mutex);
static K_MUTEX_DEFINE(snapshot_mutex);
static struct meshbus_gnss_heading_snapshot compass_snapshot;
static uint16_t active_client_count;
static uint32_t provider_capabilities;
static uint32_t sample_deadline_ms;
static uint8_t active_source = MESHBUS_GNSS_HEADING_SOURCE_UNSPECIFIED;
static bool electronic_compass_requested = true;
static bool runtime_quiescing;
static bool power_quiescing;
static atomic_t compass_warn_flags;

#if defined(MESHBUS_GNSS_HEADING_HAS_ELECTRONIC)
static K_MUTEX_DEFINE(provider_io_mutex);
static const struct device *const compass_dev = DEVICE_DT_GET(DT_CHOSEN(meshbus_compass));
static bool provider_pm_held;
static bool provider_rate_overridden;
#endif

enum compass_warn_bit {
	COMPASS_WARN_UNAVAILABLE,
	COMPASS_WARN_SAMPLE,
	COMPASS_WARN_PROFILE,
};

static enum meshbus_gnss_heading_source compass_source_for_config(bool electronic_compass)
{
#if defined(MESHBUS_GNSS_HEADING_HAS_ELECTRONIC)
	if (electronic_compass) {
		return MESHBUS_GNSS_HEADING_SOURCE_ELECTRONIC;
	}
#else
	ARG_UNUSED(electronic_compass);
#endif
	return MESHBUS_GNSS_HEADING_SOURCE_COURSE;
}

static enum meshbus_gnss_heading_source compass_source_get(void)
{
	enum meshbus_gnss_heading_source source;

	k_mutex_lock(&runtime_mutex, K_FOREVER);
	if (active_client_count > 0U) {
		source = (enum meshbus_gnss_heading_source)active_source;
	} else {
		source = compass_source_for_config(electronic_compass_requested);
	}
	k_mutex_unlock(&runtime_mutex);
	return source;
}

static void compass_warn_once(enum compass_warn_bit bit, const char *operation, int error)
{
	if (!atomic_test_and_set_bit(&compass_warn_flags, bit)) {
		LOG_WRN("Compass %s failed: %d", operation, error);
	}
}

static void compass_snapshot_commit(struct meshbus_gnss_heading_snapshot *next)
{
	k_mutex_lock(&snapshot_mutex, K_FOREVER);
	next->sequence = compass_snapshot.sequence + 1U;
	compass_snapshot = *next;
	k_mutex_unlock(&snapshot_mutex);
}

static void compass_snapshot_set_state(enum meshbus_gnss_heading_state state, int error)
{
	struct meshbus_gnss_heading_snapshot next;

	k_mutex_lock(&snapshot_mutex, K_FOREVER);
	next = compass_snapshot;
	k_mutex_unlock(&snapshot_mutex);
	next.source = compass_source_get();
	next.state = state;
	next.valid = false;
	next.last_error = error;
	next.accuracy = MESHBUS_GNSS_HEADING_ACCURACY_UNRELIABLE;
	next.calibration_hint = MESHBUS_GNSS_HEADING_CALIBRATION_HINT_NONE;
	compass_snapshot_commit(&next);
}

#if defined(MESHBUS_GNSS_HEADING_HAS_ELECTRONIC)
static int compass_provider_pm_acquire(void)
{
	int rc;

	if (provider_pm_held) {
		return 0;
	}
	if (!device_is_ready(compass_dev)) {
		return -ENODEV;
	}

#if defined(CONFIG_PM_DEVICE) && defined(CONFIG_PM_DEVICE_RUNTIME)
	if (!pm_device_runtime_is_enabled(compass_dev)) {
		rc = pm_device_runtime_enable(compass_dev);
		if (rc == -ENOTSUP || rc == -ENOSYS) {
			return 0;
		}
		if (rc != 0) {
			return rc;
		}
	}
	if (!pm_device_runtime_is_enabled(compass_dev)) {
		return 0;
	}
	rc = pm_device_runtime_get(compass_dev);
	if (rc != 0) {
		return rc;
	}
	provider_pm_held = true;
#endif
	return 0;
}

static int compass_provider_pm_release(void)
{
	if (!provider_pm_held) {
		return 0;
	}
#if defined(CONFIG_PM_DEVICE) && defined(CONFIG_PM_DEVICE_RUNTIME)
	int rc = pm_device_runtime_put(compass_dev);

	if (rc != 0) {
		return rc;
	}
#endif
	provider_pm_held = false;
	return 0;
}

static int compass_provider_rate_override(void)
{
	struct sensor_value value = {
		.val1 = CONFIG_MESHBUS_GNSS_HEADING_PROVIDER_FAST_FREQUENCY,
	};
	int rc = sensor_attr_set(compass_dev, SENSOR_CHAN_COMPASS_HEADING,
				 SENSOR_ATTR_COMPASS_RATE_OVERRIDE, &value);

	if (rc == -ENOTSUP) {
		return 0;
	}
	if (rc == 0) {
		provider_rate_overridden = true;
	}
	return rc;
}

static int compass_provider_rate_restore(void)
{
	struct sensor_value value = {0};
	int rc;

	if (!provider_rate_overridden) {
		return 0;
	}
	rc = sensor_attr_set(compass_dev, SENSOR_CHAN_COMPASS_HEADING,
			     SENSOR_ATTR_COMPASS_RATE_OVERRIDE, &value);
	if (rc == 0 || rc == -ENOTSUP) {
		provider_rate_overridden = false;
		return 0;
	}
	return rc;
}

static uint32_t compass_provider_capabilities_read(void)
{
	struct sensor_value value;

	if (sensor_attr_get(compass_dev, SENSOR_CHAN_COMPASS_HEADING,
			    SENSOR_ATTR_COMPASS_CAPABILITIES, &value) == 0 &&
	    value.val1 >= 0) {
		return (uint32_t)value.val1;
	}
	return 0U;
}

static void compass_electronic_snapshot_read(struct meshbus_gnss_heading_snapshot *next)
{
	struct sensor_value value;
	int rc;

	next->source_timestamp_ms = k_uptime_get_32();
	rc = device_is_ready(compass_dev) ? sensor_sample_fetch(compass_dev) : -ENODEV;
	if (rc == 0) {
		rc = sensor_channel_get(compass_dev, SENSOR_CHAN_COMPASS_HEADING, &value);
	}
	if (rc != 0) {
		next->state = MESHBUS_GNSS_HEADING_STATE_ERROR;
		next->last_error = rc;
		return;
	}

	int64_t heading_mdeg = sensor_value_to_micro(&value) / 1000LL;

	heading_mdeg %= 360000LL;
	if (heading_mdeg < 0) {
		heading_mdeg += 360000LL;
	}
	next->heading_milli_deg = (int32_t)heading_mdeg;
	next->valid = true;
	next->state = MESHBUS_GNSS_HEADING_STATE_READY;
	next->last_error = 0;

	if (sensor_attr_get(compass_dev, SENSOR_CHAN_COMPASS_HEADING,
			    SENSOR_ATTR_COMPASS_ACCURACY, &value) == 0 &&
	    value.val1 >= 0 && value.val1 <= MESHBUS_GNSS_HEADING_ACCURACY_HIGH) {
		next->accuracy = (enum meshbus_gnss_heading_accuracy)value.val1;
	}
	if (sensor_attr_get(compass_dev, SENSOR_CHAN_COMPASS_HEADING,
			    SENSOR_ATTR_COMPASS_CAL_HINT, &value) == 0 &&
	    value.val1 >= 0 &&
	    value.val1 <= MESHBUS_GNSS_HEADING_CALIBRATION_HINT_KEEP_LEVEL) {
		next->calibration_hint =
			(enum meshbus_gnss_heading_calibration_hint)value.val1;
	}
}
#endif

static void compass_gnss_snapshot_read(struct meshbus_gnss_heading_snapshot *next)
{
	struct meshbus_gnss_data_event fix = {0};
	meshbus_gnss_config cfg;
	uint32_t source_timestamp_ms;
	uint32_t age_ms;
	int rc;

	rc = meshbus_gnss_config_get(&cfg);
	if (rc != 0 || !cfg.enabled) {
		next->state = MESHBUS_GNSS_HEADING_STATE_SOURCE_DISABLED;
		next->last_error = 0;
		return;
	}
	rc = meshbus_gnss_fix_snapshot_get(&fix, &source_timestamp_ms);
	if (rc == -ENODATA) {
		next->state = MESHBUS_GNSS_HEADING_STATE_WAITING_FOR_FIX;
		next->last_error = 0;
		return;
	}
	if (rc != 0) {
		next->state = MESHBUS_GNSS_HEADING_STATE_ERROR;
		next->last_error = rc;
		return;
	}

	next->source_timestamp_ms = source_timestamp_ms;
	age_ms = k_uptime_get_32() - source_timestamp_ms;
	if (age_ms > CONFIG_MESHBUS_GNSS_HEADING_MAX_COURSE_AGE) {
		next->state = MESHBUS_GNSS_HEADING_STATE_STALE;
		next->last_error = 0;
		return;
	}
	if (fix.nav_data.speed < CONFIG_MESHBUS_GNSS_HEADING_MIN_COURSE_SPEED) {
		next->state = MESHBUS_GNSS_HEADING_STATE_WAITING_FOR_MOTION;
		next->last_error = 0;
		return;
	}

	next->heading_milli_deg = (int32_t)(fix.nav_data.bearing % 360000U);
	next->accuracy = MESHBUS_GNSS_HEADING_ACCURACY_LOW;
	next->state = MESHBUS_GNSS_HEADING_STATE_READY;
	next->last_error = 0;
	next->valid = true;
}

static void compass_sample_work_handler(struct k_work *work)
{
	struct meshbus_gnss_heading_snapshot next = {
		.state = MESHBUS_GNSS_HEADING_STATE_STARTING,
		.accuracy = MESHBUS_GNSS_HEADING_ACCURACY_UNRELIABLE,
		.calibration_hint = MESHBUS_GNSS_HEADING_CALIBRATION_HINT_NONE,
	};
	uint32_t deadline_ms;
	enum meshbus_gnss_heading_source source;
	bool active;
	bool commit = true;

	ARG_UNUSED(work);
	k_mutex_lock(&runtime_mutex, K_FOREVER);
	active = active_client_count > 0U && !runtime_quiescing;
	source = (enum meshbus_gnss_heading_source)active_source;
	deadline_ms = sample_deadline_ms;
	k_mutex_unlock(&runtime_mutex);
	if (!active) {
		return;
	}
	next.source = source;

#if defined(MESHBUS_GNSS_HEADING_HAS_ELECTRONIC)
	if (source == MESHBUS_GNSS_HEADING_SOURCE_ELECTRONIC) {
		/*
		 * Provider I/O, rate overrides, and runtime-PM teardown share this
		 * mutex.  Recheck after acquiring it because a release or power action
		 * can begin while this work item is waiting for an in-flight control
		 * operation.
		 */
		k_mutex_lock(&provider_io_mutex, K_FOREVER);
		k_mutex_lock(&runtime_mutex, K_FOREVER);
		commit = active_client_count > 0U && !runtime_quiescing &&
			 active_source == MESHBUS_GNSS_HEADING_SOURCE_ELECTRONIC;
		k_mutex_unlock(&runtime_mutex);
		if (commit) {
			compass_electronic_snapshot_read(&next);
			compass_snapshot_commit(&next);
		}
		k_mutex_unlock(&provider_io_mutex);
	} else
#endif
	{
		compass_gnss_snapshot_read(&next);
	}
	if (!commit) {
		return;
	}
	if (next.state == MESHBUS_GNSS_HEADING_STATE_ERROR) {
		compass_warn_once(next.last_error == -ENODEV ? COMPASS_WARN_UNAVAILABLE
							 : COMPASS_WARN_SAMPLE,
				  next.last_error == -ENODEV ? "provider availability"
							 : "direction update",
				  next.last_error);
	}
#if defined(MESHBUS_GNSS_HEADING_HAS_ELECTRONIC)
	if (source != MESHBUS_GNSS_HEADING_SOURCE_ELECTRONIC)
#endif
	{
		compass_snapshot_commit(&next);
	}

	k_mutex_lock(&runtime_mutex, K_FOREVER);
	if (active_client_count > 0U && !runtime_quiescing && active_source == source) {
		uint32_t now_ms = k_uptime_get_32();
		uint32_t next_ms = deadline_ms + CONFIG_MESHBUS_GNSS_HEADING_SAMPLE_INTERVAL;
		int32_t remaining_ms = (int32_t)(next_ms - now_ms);

		if (deadline_ms == 0U) {
			next_ms = now_ms + CONFIG_MESHBUS_GNSS_HEADING_SAMPLE_INTERVAL;
		} else if (remaining_ms <= 0) {
			uint32_t missed =
				((uint32_t)(now_ms - next_ms) /
				 CONFIG_MESHBUS_GNSS_HEADING_SAMPLE_INTERVAL) +
				1U;

			next_ms += missed * CONFIG_MESHBUS_GNSS_HEADING_SAMPLE_INTERVAL;
		}
		sample_deadline_ms = next_ms;
		(void)k_work_reschedule(&compass_sample_work,
					K_MSEC((uint32_t)(next_ms - now_ms)));
	}
	k_mutex_unlock(&runtime_mutex);
}

int meshbus_gnss_heading_acquire(void)
{
	enum meshbus_gnss_heading_source source;
	uint32_t delay_ms = 0U;
	uint32_t capabilities = 0U;
	bool course_acquisition_requested = false;
	int rc = 0;

retry:
	k_mutex_lock(&transition_mutex, K_FOREVER);
	k_mutex_lock(&runtime_mutex, K_FOREVER);
	if (power_quiescing) {
		rc = -ESHUTDOWN;
		goto out_unlock_runtime;
	}
	if (active_client_count == UINT16_MAX) {
		rc = -EOVERFLOW;
		goto out_unlock_runtime;
	}
	if (active_client_count > 0U) {
		active_client_count++;
		goto out_unlock_runtime;
	}
	source = compass_source_for_config(electronic_compass_requested);
	k_mutex_unlock(&runtime_mutex);

	/*
	 * meshbus_gnss_acquisition() takes GNSS's settings-apply mutex, while a
	 * GNSS config apply calls back into this module with that mutex held.  Drop
	 * the heading transition mutex around the first course request to preserve
	 * the established GNSS -> heading lock order, then re-evaluate the source.
	 */
	if (source == MESHBUS_GNSS_HEADING_SOURCE_COURSE &&
	    !course_acquisition_requested) {
		k_mutex_unlock(&transition_mutex);
		rc = meshbus_gnss_acquisition();
		if (rc != 0 && rc != -EBUSY) {
			return rc;
		}
		course_acquisition_requested = true;
		goto retry;
	}

#if defined(MESHBUS_GNSS_HEADING_HAS_ELECTRONIC)
	if (source == MESHBUS_GNSS_HEADING_SOURCE_ELECTRONIC) {
		k_mutex_lock(&provider_io_mutex, K_FOREVER);
		rc = compass_provider_pm_acquire();
		if (rc != 0) {
			goto out_unlock_provider;
		}
		capabilities = compass_provider_capabilities_read();
		rc = compass_provider_rate_override();
		if (rc != 0) {
			(void)compass_provider_pm_release();
			goto out_unlock_provider;
		}
		k_mutex_unlock(&provider_io_mutex);
		delay_ms = CONFIG_MESHBUS_GNSS_HEADING_PROVIDER_SETTLE_MS;
	}
#endif

	k_mutex_lock(&runtime_mutex, K_FOREVER);
	active_client_count = 1U;
	active_source = source;
	provider_capabilities = capabilities;
	sample_deadline_ms = k_uptime_get_32() + delay_ms;
	runtime_quiescing = false;
	k_mutex_unlock(&runtime_mutex);
	compass_snapshot_set_state(MESHBUS_GNSS_HEADING_STATE_STARTING, 0);
	(void)k_work_reschedule(&compass_sample_work, K_MSEC(delay_ms));
	LOG_DBG("Compass heading runtime acquired");
	goto out_unlock_transition;

#if defined(MESHBUS_GNSS_HEADING_HAS_ELECTRONIC)
out_unlock_provider:
	k_mutex_unlock(&provider_io_mutex);
	goto out_unlock_transition;
#endif
out_unlock_runtime:
	k_mutex_unlock(&runtime_mutex);
out_unlock_transition:
	k_mutex_unlock(&transition_mutex);
	return rc;
}

int meshbus_gnss_heading_release(void)
{
	struct k_work_sync sync;
	enum meshbus_gnss_heading_source source;
	int profile_rc = 0;
	int power_rc = 0;
	int rc = 0;

	k_mutex_lock(&transition_mutex, K_FOREVER);
	k_mutex_lock(&runtime_mutex, K_FOREVER);
	if (active_client_count == 0U) {
		rc = -EALREADY;
		goto out_unlock_runtime;
	}
	if (active_client_count > 1U) {
		active_client_count--;
		goto out_unlock_runtime;
	}
	source = (enum meshbus_gnss_heading_source)active_source;
	sample_deadline_ms = 0U;
	runtime_quiescing = true;
	k_mutex_unlock(&runtime_mutex);

	(void)k_work_cancel_delayable_sync(&compass_sample_work, &sync);
#if defined(MESHBUS_GNSS_HEADING_HAS_ELECTRONIC)
	if (source == MESHBUS_GNSS_HEADING_SOURCE_ELECTRONIC) {
		k_mutex_lock(&provider_io_mutex, K_FOREVER);
		profile_rc = compass_provider_rate_restore();
		if (profile_rc == 0) {
			power_rc = compass_provider_pm_release();
		}
		k_mutex_unlock(&provider_io_mutex);
		if (profile_rc != 0) {
			compass_warn_once(COMPASS_WARN_PROFILE, "profile restore", profile_rc);
		}
	}
#else
	ARG_UNUSED(source);
#endif
	rc = profile_rc != 0 ? profile_rc : power_rc;
	if (rc != 0) {
		/*
		 * Keep the final lease and source intact.  The caller can retry release
		 * after a transient rate or runtime-PM failure, while sampling remains
		 * quiesced and the provider hold remains accounted for.
		 */
		compass_snapshot_set_state(MESHBUS_GNSS_HEADING_STATE_ERROR, rc);
		goto out_unlock_transition;
	}

	k_mutex_lock(&runtime_mutex, K_FOREVER);
	active_client_count = 0U;
	active_source = MESHBUS_GNSS_HEADING_SOURCE_UNSPECIFIED;
	provider_capabilities = 0U;
	runtime_quiescing = false;
	k_mutex_unlock(&runtime_mutex);
	compass_snapshot_set_state(MESHBUS_GNSS_HEADING_STATE_IDLE, 0);
	LOG_DBG("Compass heading runtime released");
	goto out_unlock_transition;

out_unlock_runtime:
	k_mutex_unlock(&runtime_mutex);
out_unlock_transition:
	k_mutex_unlock(&transition_mutex);
	return rc;
}

int meshbus_gnss_heading_snapshot_get(struct meshbus_gnss_heading_snapshot *snapshot)
{
	if (snapshot == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&snapshot_mutex, K_FOREVER);
	*snapshot = compass_snapshot;
	k_mutex_unlock(&snapshot_mutex);
	return 0;
}

int meshbus_gnss_heading_runtime_status_get(struct meshbus_gnss_heading_runtime_status *status)
{
	if (status == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&runtime_mutex, K_FOREVER);
	status->capabilities = provider_capabilities;
	status->sample_interval_ms = CONFIG_MESHBUS_GNSS_HEADING_SAMPLE_INTERVAL;
	status->active_client_count = active_client_count;
	status->active = active_client_count > 0U;
	k_mutex_unlock(&runtime_mutex);
#if defined(MESHBUS_GNSS_HEADING_HAS_ELECTRONIC)
	status->electronic_available = device_is_ready(compass_dev);
#else
	status->electronic_available = false;
#endif
	return 0;
}

int meshbus_gnss_heading_calibration_reset(void)
{
#if defined(MESHBUS_GNSS_HEADING_HAS_ELECTRONIC)
	struct sensor_value reset = {.val1 = 1};
	bool temporary_power;
	int release_rc;
	int rc;

	k_mutex_lock(&transition_mutex, K_FOREVER);
	k_mutex_lock(&runtime_mutex, K_FOREVER);
	if (power_quiescing) {
		rc = -ESHUTDOWN;
		k_mutex_unlock(&runtime_mutex);
		k_mutex_unlock(&transition_mutex);
		return rc;
	}
	temporary_power = active_client_count == 0U ||
			  active_source != MESHBUS_GNSS_HEADING_SOURCE_ELECTRONIC;
	k_mutex_unlock(&runtime_mutex);
	if (temporary_power) {
		k_mutex_lock(&provider_io_mutex, K_FOREVER);
		rc = compass_provider_pm_acquire();
		if (rc != 0) {
			goto out_unlock_provider;
		}
	} else {
		k_mutex_lock(&provider_io_mutex, K_FOREVER);
	}
	rc = sensor_attr_set(compass_dev, SENSOR_CHAN_COMPASS_HEADING,
			     SENSOR_ATTR_COMPASS_CAL_RESET, &reset);
	if (temporary_power) {
		release_rc = compass_provider_pm_release();
		if (rc == 0) {
			rc = release_rc;
		}
	}
out_unlock_provider:
	k_mutex_unlock(&provider_io_mutex);
	k_mutex_unlock(&transition_mutex);
	return rc;
#else
	return -ENOTSUP;
#endif
}

int meshbus_gnss_heading_config_apply(bool electronic_compass)
{
	enum meshbus_gnss_heading_source current_source;
	enum meshbus_gnss_heading_source next_source;
	bool active;

	k_mutex_lock(&transition_mutex, K_FOREVER);
	k_mutex_lock(&runtime_mutex, K_FOREVER);
	if (power_quiescing) {
		k_mutex_unlock(&runtime_mutex);
		k_mutex_unlock(&transition_mutex);
		return -ESHUTDOWN;
	}
	active = active_client_count > 0U;
	current_source = active ? (enum meshbus_gnss_heading_source)active_source
				: compass_source_for_config(electronic_compass_requested);
	next_source = compass_source_for_config(electronic_compass);
	if (active && current_source != next_source) {
		k_mutex_unlock(&runtime_mutex);
		k_mutex_unlock(&transition_mutex);
		return -EBUSY;
	}
	electronic_compass_requested = electronic_compass;
	k_mutex_unlock(&runtime_mutex);

	if (!active) {
		compass_snapshot_set_state(MESHBUS_GNSS_HEADING_STATE_IDLE, 0);
	}
	k_mutex_unlock(&transition_mutex);
	return 0;
}

static void meshbus_power_compass_cb(enum meshbus_power_action action, void *user_data)
{
#if defined(MESHBUS_GNSS_HEADING_HAS_ELECTRONIC)
	int profile_rc;
	int power_rc;
#endif

	ARG_UNUSED(user_data);
	if (action != MESHBUS_POWER_ACTION_SHUTDOWN && action != MESHBUS_POWER_ACTION_REBOOT) {
		return;
	}

	k_mutex_lock(&transition_mutex, K_FOREVER);
	k_mutex_lock(&runtime_mutex, K_FOREVER);
	active_client_count = 0U;
	active_source = MESHBUS_GNSS_HEADING_SOURCE_UNSPECIFIED;
	provider_capabilities = 0U;
	sample_deadline_ms = 0U;
	runtime_quiescing = true;
	power_quiescing = true;
	k_mutex_unlock(&runtime_mutex);
	(void)k_work_cancel_delayable(&compass_sample_work);
#if defined(MESHBUS_GNSS_HEADING_HAS_ELECTRONIC)
	/*
	 * Power callbacks may execute on the system workqueue, so cancellation
	 * above must stay asynchronous.  Always inspect provider-owned flags under
	 * the I/O mutex: a failed normal release or temporary calibration may leave
	 * a hold even when active_source no longer identifies the provider.
	 */
	k_mutex_lock(&provider_io_mutex, K_FOREVER);
	profile_rc = compass_provider_rate_restore();
	power_rc = compass_provider_pm_release();

	k_mutex_unlock(&provider_io_mutex);
	if (profile_rc != 0) {
		compass_warn_once(COMPASS_WARN_PROFILE, "profile restore", profile_rc);
	}
	if (power_rc != 0) {
		compass_warn_once(COMPASS_WARN_UNAVAILABLE, "power release", power_rc);
	}
#endif
	k_mutex_unlock(&transition_mutex);
}
MESHBUS_POWER_ACTION_CALLBACK_DEFINE(meshbus_power_compass_cb, NULL);

static int meshbus_gnss_heading_init(void)
{
	k_work_init_delayable(&compass_sample_work, compass_sample_work_handler);
	compass_snapshot = (struct meshbus_gnss_heading_snapshot){
		.source = compass_source_for_config(electronic_compass_requested),
		.state = MESHBUS_GNSS_HEADING_STATE_IDLE,
		.accuracy = MESHBUS_GNSS_HEADING_ACCURACY_UNRELIABLE,
		.calibration_hint = MESHBUS_GNSS_HEADING_CALIBRATION_HINT_NONE,
	};

#if defined(MESHBUS_GNSS_HEADING_HAS_ELECTRONIC)
	if (!device_is_ready(compass_dev)) {
		compass_snapshot.state = MESHBUS_GNSS_HEADING_STATE_UNAVAILABLE;
		compass_snapshot.last_error = -ENODEV;
		LOG_WRN("Electronic Compass provider is not ready");
		return 0;
	}
#endif

	LOG_INF("Compass heading runtime ready (%s)",
		compass_source_for_config(electronic_compass_requested) ==
				MESHBUS_GNSS_HEADING_SOURCE_ELECTRONIC
			? "electronic"
			: "GNSS course");
	return 0;
}

SYS_INIT(meshbus_gnss_heading_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
