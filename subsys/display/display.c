/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#if defined(CONFIG_U8G2)
#include <display/u8g2.h>
#endif
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <display/display.h>
#include <power/power.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>
#if defined(CONFIG_PM_DEVICE)
#include <zephyr/pm/device.h>
#endif
#if defined(CONFIG_PM_DEVICE_RUNTIME)
#include <zephyr/pm/device_runtime.h>
#endif
#if defined(CONFIG_MBS_INPUT)
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <input/input.h>
#endif

#include "mbs_settings_internal.h"

LOG_MODULE_REGISTER(mbs_display, CONFIG_MBS_DISPLAY_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* ZBus Channels                                                              */
/* -------------------------------------------------------------------------- */
ZBUS_CHAN_DEFINE(mbs_display_state_chan, struct mbs_display_state_event,
		 NULL, /* validator */
		 NULL, /* user_data */
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

/* -------------------------------------------------------------------------- */
/* Devices                                                                    */
/* -------------------------------------------------------------------------- */
#if DT_HAS_CHOSEN(zephyr_display)
#define MBS_DISPLAY_HAS_DEVICE 1
#define MBS_DISPLAY_NODE       DT_CHOSEN(zephyr_display)
static const struct device *const display_dev = DEVICE_DT_GET(MBS_DISPLAY_NODE);
#if DT_NODE_HAS_PROP(MBS_DISPLAY_NODE, power_domains)
static const struct device *const display_pd_dev =
	DEVICE_DT_GET_OR_NULL(DT_PHANDLE_BY_IDX(MBS_DISPLAY_NODE, power_domains, 0));
#else
static const struct device *const display_pd_dev = NULL;
#endif
#else
#define MBS_DISPLAY_HAS_DEVICE 0
static const struct device *const display_dev;
static const struct device *const display_pd_dev;
#endif

/* -------------------------------------------------------------------------- */
/* Defaults And State                                                         */
/* -------------------------------------------------------------------------- */
#define MBS_DISPLAY_CONFIG_DEFAULTS                                                       \
	{                                                                                       \
		.brightness = CONFIG_MBS_DISPLAY_DEFAULT_BRIGHTNESS,                          \
		.sleep_timeout = CONFIG_MBS_DISPLAY_DEFAULT_SLEEP_TIMEOUT,                    \
		.invert = IS_ENABLED(CONFIG_MBS_DISPLAY_DEFAULT_INVERT),                      \
	}

/* Run right after display driver init to blank panel before slower APP init hooks. */
#define MBS_DISPLAY_BOOT_BLANK_INIT_PRIORITY UTIL_INC(CONFIG_DISPLAY_INIT_PRIORITY)

/* Persisted configuration guarded by settings_mutex. */
static mbs_display_config display_cfg = MBS_DISPLAY_CONFIG_DEFAULTS;
static K_MUTEX_DEFINE(settings_mutex);
static K_MUTEX_DEFINE(apply_mutex);
static bool display_boot_buffer_cleared;
static bool settings_initial_apply;
static struct k_work_delayable settings_persistence_work;
static mbs_display_config settings_load_cfg = MBS_DISPLAY_CONFIG_DEFAULTS;
static struct mbs_settings_blob_load_state settings_load_state;

#define MBS_DISPLAY_SETTINGS_SUBTREE    "meshbus/display"
#define MBS_DISPLAY_SETTINGS_KEY_CONFIG "config"

MBS_SETTINGS_BLOB_SCHEMA_DEFINE(display_settings_schema, MBS_DISPLAY_SETTINGS_SUBTREE,
			       MBS_DISPLAY_SETTINGS_KEY_CONFIG, meshbus_DisplayConfig,
			       mbs_display_config);

/* Runtime state guarded by state_mutex. */
static K_MUTEX_DEFINE(state_mutex);
static bool display_active_state;

/* Serialize active transitions to avoid duplicate hw operations. */
static K_MUTEX_DEFINE(active_mutex);

/* Runtime PM claim state guarded by pm_mutex. */
static K_MUTEX_DEFINE(pm_mutex);
static bool display_dev_claimed;
static bool display_pd_claimed;

static struct k_work_delayable display_sleep_work;
static struct k_work display_activity_work;

static atomic_t display_service_ready = ATOMIC_INIT(0);
static atomic_t display_shutdown_started = ATOMIC_INIT(0);
static atomic_t display_warn_missing_hw = ATOMIC_INIT(0);
static atomic_t display_warn_not_ready = ATOMIC_INIT(0);

/* -------------------------------------------------------------------------- */
/* Declarations                                                               */
/* -------------------------------------------------------------------------- */
static int settings_handler_apply(const mbs_display_config *cfg, bool persistence, bool force);

/* -------------------------------------------------------------------------- */
/* Validation And Helpers                                                     */
/* -------------------------------------------------------------------------- */
static mbs_display_config display_config_snapshot(void)
{
	mbs_display_config cfg;

	k_mutex_lock(&settings_mutex, K_FOREVER);
	cfg = display_cfg;
	k_mutex_unlock(&settings_mutex);

	return cfg;
}

static bool display_device_ready(void)
{
	if (display_dev == NULL) {
		if (atomic_cas(&display_warn_missing_hw, 0, 1)) {
			LOG_WRN("Display device not configured (missing zephyr,display chosen)");
		}
		return false;
	}

	if (!device_is_ready(display_dev)) {
		if (atomic_cas(&display_warn_not_ready, 0, 1)) {
			LOG_WRN("Display device not ready");
		}
		return false;
	}

	return true;
}

#if defined(CONFIG_PM_DEVICE_RUNTIME)
static int display_pm_claim(const struct device *dev)
{
	if (dev == NULL) {
		return 0;
	}

	if (!device_is_ready(dev)) {
		return -ENODEV;
	}

	if (!pm_device_runtime_is_enabled(dev)) {
		int en_rc = pm_device_runtime_enable(dev);

		if (en_rc != 0 && en_rc != -ENOTSUP && en_rc != -EBUSY) {
			return en_rc;
		}
	}

	int rc = pm_device_runtime_get(dev);
	if (rc == -ENOTSUP) {
		return 0;
	}

	return rc;
}

static int display_pm_release(const struct device *dev)
{
	if (dev == NULL) {
		return 0;
	}

	if (!device_is_ready(dev)) {
		return 0;
	}

	int rc = pm_device_runtime_put(dev);
	if (rc == -ENOTSUP || rc == -EALREADY) {
		return 0;
	}

	return rc;
}

static int display_runtime_claim(void)
{
	int rc = 0;
	bool claimed_pd = false;

	k_mutex_lock(&pm_mutex, K_FOREVER);

	if (!display_pd_claimed) {
		rc = display_pm_claim(display_pd_dev);
		if (rc != 0) {
			goto out;
		}
		display_pd_claimed = true;
		claimed_pd = true;
	}

	if (!display_dev_claimed) {
		rc = display_pm_claim(display_dev);
		if (rc != 0) {
			if (claimed_pd) {
				(void)display_pm_release(display_pd_dev);
				display_pd_claimed = false;
			}
			goto out;
		}
		display_dev_claimed = true;
	}

out:
	k_mutex_unlock(&pm_mutex);

	return rc;
}

static void display_runtime_release(void)
{
	k_mutex_lock(&pm_mutex, K_FOREVER);

	if (display_dev_claimed) {
		(void)display_pm_release(display_dev);
		display_dev_claimed = false;
	}

	if (display_pd_claimed) {
		(void)display_pm_release(display_pd_dev);
		display_pd_claimed = false;
	}

	k_mutex_unlock(&pm_mutex);
}
#else
static int display_runtime_claim(void)
{
	return 0;
}

static void display_runtime_release(void)
{
}
#endif /* CONFIG_PM_DEVICE_RUNTIME */

static int display_hw_clear_boot_buffer(void)
{
	struct display_capabilities caps;
	struct display_buffer_descriptor desc;
	uint8_t *buf;
	size_t buf_size;
	uint8_t black;
	int rc;

	if (!display_device_ready()) {
		return -ENODEV;
	}

	display_get_capabilities(display_dev, &caps);
	if ((caps.current_pixel_format != PIXEL_FORMAT_MONO01 &&
	     caps.current_pixel_format != PIXEL_FORMAT_MONO10) ||
	    (caps.y_resolution & 0x7U) != 0U) {
		return -ENOTSUP;
	}

	buf_size = ((size_t)caps.x_resolution * caps.y_resolution) / 8U;
	buf = k_malloc(buf_size);
	if (buf == NULL) {
		return -ENOMEM;
	}

	black = caps.current_pixel_format == PIXEL_FORMAT_MONO10 ? 0xFFU : 0x00U;
	memset(buf, black, buf_size);

	desc = (struct display_buffer_descriptor) {
		.buf_size = buf_size,
		.width = caps.x_resolution,
		.height = caps.y_resolution,
		.pitch = caps.x_resolution,
	};

	rc = display_write(display_dev, 0U, 0U, &desc, buf);
	k_free(buf);

	return rc;
}

static int display_hw_set_active(bool active)
{
	if (!display_device_ready()) {
		return -ENODEV;
	}

	int rc;

	if (active) {
		rc = display_runtime_claim();
		if (rc != 0) {
			LOG_WRN("Display runtime claim failed: %d", rc);
			return rc;
		}

		if (!display_boot_buffer_cleared) {
			display_boot_buffer_cleared = true;
			rc = display_hw_clear_boot_buffer();
			if (rc != 0 && rc != -ENOTSUP) {
				LOG_DBG("Display boot buffer clear skipped: %d", rc);
			}
		}

		rc = display_blanking_off(display_dev);
#if defined(CONFIG_PM_DEVICE)
		if (rc == -ENOSYS) {
			rc = pm_device_action_run(display_dev, PM_DEVICE_ACTION_RESUME);
			if (rc == -EALREADY || rc == -ENOTSUP) {
				rc = 0;
			}
		}
#endif
		if (rc != 0) {
			LOG_WRN("Display wake failed: %d", rc);
			display_runtime_release();
		}

		return rc;
	}

	rc = display_blanking_on(display_dev);
#if defined(CONFIG_PM_DEVICE)
	if (rc == -ENOSYS) {
		rc = pm_device_action_run(display_dev, PM_DEVICE_ACTION_SUSPEND);
		if (rc == -EALREADY || rc == -ENOTSUP) {
			rc = 0;
		}
	}
#endif
	if (rc != 0) {
		LOG_WRN("Display sleep failed: %d", rc);
	}

	display_runtime_release();

	return rc;
}

static int display_brightness_apply(uint32_t brightness)
{
	if (!display_device_ready()) {
		return -ENODEV;
	}

	uint32_t scaled = ((brightness * 255U) + 50U) / 100U;
	int rc = display_set_contrast(display_dev, (uint8_t)scaled);

	if (rc == -ENOSYS || rc == -ENOTSUP) {
		rc = display_set_brightness(display_dev, (uint8_t)scaled);
	}

	if (rc == -ENOSYS || rc == -ENOTSUP) {
		return 0;
	}

	return rc;
}

static int display_invert_apply(bool invert)
{
	if (!display_device_ready()) {
		return -ENODEV;
	}

#if defined(CONFIG_U8G2)
	return u8g2_set_output_invert(invert);
#else

	struct display_capabilities caps;
	display_get_capabilities(display_dev, &caps);

	const bool has_mono01 =
		(caps.supported_pixel_formats & (uint32_t)PIXEL_FORMAT_MONO01) != 0U;
	const bool has_mono10 =
		(caps.supported_pixel_formats & (uint32_t)PIXEL_FORMAT_MONO10) != 0U;

	enum display_pixel_format target = caps.current_pixel_format;

	if (invert) {
		if (!has_mono10) {
			return -ENOTSUP;
		}
		target = PIXEL_FORMAT_MONO10;
	} else if (has_mono01) {
		target = PIXEL_FORMAT_MONO01;
	} else {
		return 0;
	}

	if (caps.current_pixel_format == target) {
		return 0;
	}

	int rc = display_set_pixel_format(display_dev, target);

	if (rc == -ENOSYS || rc == -ENOTSUP) {
		return invert ? -ENOTSUP : 0;
	}

	return rc;
#endif
}

static int display_config_validate(const mbs_display_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	if (cfg->brightness > 100U) {
		LOG_ERR("Invalid brightness: %u", cfg->brightness);
		return -EINVAL;
	}

	if (cfg->sleep_timeout > 300U) {
		LOG_ERR("Invalid sleep_timeout: %u", cfg->sleep_timeout);
		return -EINVAL;
	}

	return 0;
}

static void display_state_publish(bool active)
{
	struct mbs_display_state_event ev = {
		.active = active,
	};

	int rc = zbus_chan_pub(&mbs_display_state_chan, &ev, K_NO_WAIT);
	if (rc != 0) {
		LOG_DBG("Display state publish failed: %d", rc);
	}
}

static void display_sleep_reschedule_from_cfg(const mbs_display_config *cfg, bool active)
{
	if (!active || cfg->sleep_timeout == 0U) {
		(void)k_work_cancel_delayable(&display_sleep_work);
		return;
	}

	(void)k_work_reschedule(&display_sleep_work, K_SECONDS(cfg->sleep_timeout));
}

/* -------------------------------------------------------------------------- */
/* Callbacks And Work                                                         */
/* -------------------------------------------------------------------------- */
static void display_sleep_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	mbs_display_active(false);
}

static void display_activity_refresh_from_input(void)
{
	mbs_display_config cfg = display_config_snapshot();
	display_sleep_reschedule_from_cfg(&cfg, true);
}

static void display_activity_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (atomic_get(&display_service_ready) == 0) {
		return;
	}

	if (!mbs_display_is_active()) {
		mbs_display_active(true);
		return;
	}

	display_activity_refresh_from_input();
}

/* -------------------------------------------------------------------------- */
/* Settings Schema And Apply                                                  */
/* -------------------------------------------------------------------------- */
static int settings_handler_apply(const mbs_display_config *cfg, bool persistence, bool force)
{
	int rc = display_config_validate(cfg);
	if (rc != 0) {
		return rc;
	}

	k_mutex_lock(&apply_mutex, K_FOREVER);

	mbs_display_config prev;
	k_mutex_lock(&settings_mutex, K_FOREVER);
	prev = display_cfg;
	k_mutex_unlock(&settings_mutex);

	if (!force && prev.brightness == cfg->brightness &&
	    prev.sleep_timeout == cfg->sleep_timeout && prev.invert == cfg->invert) {
		k_mutex_lock(&settings_mutex, K_FOREVER);
		settings_initial_apply = true;
		k_mutex_unlock(&settings_mutex);
		k_mutex_unlock(&apply_mutex);
		LOG_DBG("Settings unchanged, nothing to apply");
		return 0;
	}

	rc = display_invert_apply(cfg->invert);
	if (rc != 0 && rc != -ENODEV) {
		k_mutex_unlock(&apply_mutex);
		return rc;
	}

	rc = display_brightness_apply(cfg->brightness);
	if (rc != 0 && rc != -ENODEV) {
		int rollback_rc = display_invert_apply(prev.invert);

		if (rollback_rc != 0 && rollback_rc != -ENODEV) {
			LOG_ERR("Brightness apply failed (%d) and invert rollback failed (%d); "
				"display state is unknown",
				rc, rollback_rc);
			k_mutex_unlock(&apply_mutex);
			return -EIO;
		}

		LOG_WRN("Brightness apply failed (%d); restored invert=%d", rc,
			(int)prev.invert);
		k_mutex_unlock(&apply_mutex);
		return rc;
	}

	k_mutex_lock(&settings_mutex, K_FOREVER);
	display_cfg = *cfg;
	settings_initial_apply = true;
	k_mutex_unlock(&settings_mutex);

	LOG_INF("Settings apply: brightness=%u sleep_timeout=%u invert=%d", cfg->brightness,
		cfg->sleep_timeout, (int)cfg->invert);

	mbs_display_active(true);

	if (persistence) {
		(void)k_work_reschedule(&settings_persistence_work,
					K_MSEC(CONFIG_MBS_SETTINGS_PERSISTENCE_DELAY));
	}

	k_mutex_unlock(&apply_mutex);
	return 0;
}

MBS_SETTINGS_BLOB_CONFIG_DEFINE(display_settings_schema, settings_mutex, settings_load_state,
			       settings_load_cfg, display_cfg, settings_initial_apply,
			       mbs_display_config, meshbus_DisplayConfig_size,
			       settings_handler_apply, "display")

SETTINGS_STATIC_HANDLER_DEFINE(mbs_display, MBS_DISPLAY_SETTINGS_SUBTREE, NULL,
			       settings_handle_set, settings_handle_commit,
			       settings_handle_export);

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */
int mbs_display_config_get(mbs_display_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&settings_mutex, K_FOREVER);
	*cfg = display_cfg;
	k_mutex_unlock(&settings_mutex);

	return 0;
}

int mbs_display_config_set(const mbs_display_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	return settings_handler_apply(cfg, true, false);
}

int mbs_display_config_reset(void)
{
	struct k_work_sync sync;
	mbs_display_config cfg = MBS_DISPLAY_CONFIG_DEFAULTS;

	(void)k_work_cancel_delayable_sync(&settings_persistence_work, &sync);

	int rc = settings_handler_apply(&cfg, false, true);
	if (rc != 0) {
		return rc;
	}

	rc = mbs_settings_blob_delete(&display_settings_schema);
	if (rc != 0) {
		LOG_ERR("Failed to delete persisted settings: %d", rc);
		return rc;
	}

	return 0;
}

bool mbs_display_is_active(void)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
	bool active = display_active_state;
	k_mutex_unlock(&state_mutex);

	return active;
}

void mbs_display_active(bool active)
{
	if (active && atomic_get(&display_shutdown_started) != 0) {
		return;
	}

	mbs_display_config cfg = display_config_snapshot();

	bool publish = false;

	k_mutex_lock(&active_mutex, K_FOREVER);

	k_mutex_lock(&state_mutex, K_FOREVER);
	bool prev = display_active_state;
	k_mutex_unlock(&state_mutex);

	if (prev == active) {
		display_sleep_reschedule_from_cfg(&cfg, active);
		k_mutex_unlock(&active_mutex);
		return;
	}

	int rc = display_hw_set_active(active);
	if (rc != 0 && rc != -ENODEV) {
		LOG_WRN("Display active transition failed: %d", rc);
		k_mutex_unlock(&active_mutex);
		return;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);
	display_active_state = active;
	k_mutex_unlock(&state_mutex);

	publish = true;

	display_sleep_reschedule_from_cfg(&cfg, active);

	k_mutex_unlock(&active_mutex);

	if (publish) {
		display_state_publish(active);
	}
}

/* -------------------------------------------------------------------------- */
/* Power Callback                                                             */
/* -------------------------------------------------------------------------- */
static void display_force_inactive(void)
{
	k_mutex_lock(&active_mutex, K_FOREVER);

	int rc = display_hw_set_active(false);
	if (rc != 0 && rc != -ENODEV) {
		LOG_WRN("Display shutdown stop failed: %d", rc);
	}

	k_mutex_lock(&state_mutex, K_FOREVER);
	display_active_state = false;
	k_mutex_unlock(&state_mutex);

	(void)k_work_cancel_delayable(&display_sleep_work);

	k_mutex_unlock(&active_mutex);
}

static void mbs_power_display_cb(enum mbs_power_action action, void *user_data)
{
	ARG_UNUSED(user_data);

	if (action != MBS_POWER_ACTION_SHUTDOWN && action != MBS_POWER_ACTION_REBOOT) {
		return;
	}

	atomic_set(&display_shutdown_started, 1);
	atomic_set(&display_service_ready, 0);

	(void)k_work_cancel(&display_activity_work);
	(void)k_work_cancel_delayable(&display_sleep_work);
	(void)k_work_cancel_delayable(&settings_persistence_work);

	display_force_inactive();

	LOG_INF("Display stopped");
}
MBS_POWER_ACTION_CALLBACK_DEFINE(mbs_power_display_cb, NULL);

#if defined(CONFIG_MBS_INPUT)
static bool display_input_event_is_wake_activity(const struct zbus_channel *chan)
{
	if (chan == &mbs_input_key_chan) {
		const struct mbs_input_event *evt = zbus_chan_const_msg(chan);

		if (evt == NULL) {
			return false;
		}

		/* Ignore key RELEASE while sleeping to avoid immediate wake-after-sleep. */
		if (evt->type == INPUT_EV_KEY && evt->value == 0) {
			return false;
		}

		return true;
	}

	if (chan == &mbs_input_action_chan) {
		return zbus_chan_const_msg(chan) != NULL;
	}

	return false;
}

static void display_input_listener_cb(const struct zbus_channel *chan)
{
	if (atomic_get(&display_service_ready) == 0) {
		return;
	}

	if (!display_input_event_is_wake_activity(chan)) {
		return;
	}

	/*
	 * Avoid queueing a wake work item while already active.
	 * Otherwise the same key used to trigger "sleep" can race and wake
	 * the display immediately after we blank it.
	 */
	if (mbs_display_is_active()) {
		display_activity_refresh_from_input();
		return;
	}

	(void)k_work_submit(&display_activity_work);
}

ZBUS_LISTENER_DEFINE(mbs_display_input_listener, display_input_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_input_key_chan, mbs_display_input_listener, 2);
ZBUS_CHAN_ADD_OBS(mbs_input_action_chan, mbs_display_input_listener, 2);
#endif /* CONFIG_MBS_INPUT */

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */
static int mbs_display_init(void)
{
	int rc;

	k_work_init_delayable(&settings_persistence_work, settings_persistence_work_handler);
	k_work_init_delayable(&display_sleep_work, display_sleep_work_handler);
	k_work_init(&display_activity_work, display_activity_work_handler);

	rc = settings_load_subtree(MBS_DISPLAY_SETTINGS_SUBTREE);
	if (rc != 0) {
		return rc;
	}

	if (!settings_initial_apply) {
		rc = settings_handler_apply(&display_cfg, false, true);
		if (rc != 0) {
			return rc;
		}
	}

	atomic_set(&display_service_ready, 1);
	LOG_INF("Meshbus display service ready");

	return 0;
}

static int mbs_display_boot_blank_init(void)
{
	/*
	 * Best-effort early blanking:
	 * keep OLED dark while APPLICATION init may be blocked by other drivers.
	 */
	if (display_dev == NULL || !device_is_ready(display_dev)) {
		return 0;
	}

	int rc = display_runtime_claim();
	if (rc != 0) {
		LOG_DBG("Early display runtime claim skipped: %d", rc);
		return 0;
	}

	rc = display_blanking_on(display_dev);
#if defined(CONFIG_PM_DEVICE)
	if (rc == -ENOSYS) {
		rc = pm_device_action_run(display_dev, PM_DEVICE_ACTION_SUSPEND);
		if (rc == -EALREADY || rc == -ENOTSUP) {
			rc = 0;
		}
	}
#endif
	if (rc != 0) {
		LOG_DBG("Early display blanking skipped: %d", rc);
	}

	display_runtime_release();

	return 0;
}

SYS_INIT(mbs_display_boot_blank_init, POST_KERNEL, MBS_DISPLAY_BOOT_BLANK_INIT_PRIORITY);
SYS_INIT(mbs_display_init, APPLICATION, CONFIG_MBS_DISPLAY_INIT_PRIORITY);
