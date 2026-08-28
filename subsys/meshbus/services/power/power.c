/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/meshbus/power.h>

#include <zephyr/drivers/charger.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/timer/system_timer.h>
#if IS_ENABLED(CONFIG_MESHBUS_POWER_BACK_HOLD_SHUTDOWN)
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/meshbus/input.h>
#if IS_ENABLED(CONFIG_MESHBUS_INDICATOR)
#include "services/indicator/indicator_buzzer_tone.h"
#endif
#endif
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>
#if IS_ENABLED(CONFIG_RETENTION_BOOT_MODE)
#include <zephyr/retention/bootmode.h>
#endif
#if IS_ENABLED(CONFIG_MESHBUS_POWER_BOOTLOADER_GPREGRET)
#include <hal/nrf_power.h>
#endif
#include <stddef.h>

#include "common/settings.h"

LOG_MODULE_REGISTER(meshbus_power, CONFIG_MESHBUS_POWER_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* ZBus Channels                                                              */
/* -------------------------------------------------------------------------- */
#if IS_ENABLED(CONFIG_MESHBUS_POWER_BACK_HOLD_SHUTDOWN)
static void power_back_hold_input_listener_cb(const struct zbus_channel *chan, const void *msg);
#endif

ZBUS_CHAN_DEFINE(meshbus_power_fuel_gauge_data_chan, struct meshbus_power_fuel_gauge_data_event,
		 NULL, /* validator */
		 NULL, /* user_data */
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

#if IS_ENABLED(CONFIG_MESHBUS_POWER_BACK_HOLD_SHUTDOWN)
ZBUS_ASYNC_LISTENER_DEFINE(meshbus_power_back_hold_input_listener,
			   power_back_hold_input_listener_cb);
ZBUS_CHAN_ADD_OBS(meshbus_input_raw_event_chan, meshbus_power_back_hold_input_listener, 2);
#endif

/* -------------------------------------------------------------------------- */
/* Devices                                                                    */
/* -------------------------------------------------------------------------- */
#if DT_HAS_CHOSEN(meshbus_fuel_gauge)
static const struct device *const fuel_gauge_dev = DEVICE_DT_GET(DT_CHOSEN(meshbus_fuel_gauge));
#endif

#if DT_HAS_CHOSEN(meshbus_charger)
static const struct device *const charger_dev = DEVICE_DT_GET(DT_CHOSEN(meshbus_charger));
#endif

#if DT_HAS_CHOSEN(meshbus_power_button)
static const struct gpio_dt_spec power_button_gpio =
	GPIO_DT_SPEC_GET(DT_CHOSEN(meshbus_power_button), gpios);
#else
static const struct gpio_dt_spec power_button_gpio = {.port = NULL};
#endif

/* -------------------------------------------------------------------------- */
/* Defaults And State                                                         */
/* -------------------------------------------------------------------------- */
#define MESHBUS_POWER_TIMEOUT_MAX_SECONDS 300U
#define MESHBUS_POWER_BOOTLOADER_GPREGRET_REG 0U
#define MESHBUS_POWER_BOOTLOADER_GPREGRET_UF2 0x57U
#define MESHBUS_POWER_LOW_VOLTAGE_SAMPLE_THRESHOLD 6U
#define MESHBUS_POWER_EXTERNAL_SAMPLE_INTERVAL_MS  5000U
#define MESHBUS_POWER_CONFIG_DEFAULTS                                                          \
	{                                                                                       \
		.low_voltage_shutdown_timeout = 0U,                                             \
		.losing_power_shutdown_timeout = 0U,                                            \
		.no_connection_shutdown_timeout = 0U,                                           \
	}

static meshbus_power_config power_cfg = MESHBUS_POWER_CONFIG_DEFAULTS;
#if DT_HAS_CHOSEN(meshbus_fuel_gauge)
static uint16_t power_battery_voltage_mv;
static uint8_t power_battery_soc_percent;
static uint16_t power_battery_temperature_dk;
static bool power_battery_sample_valid;
#endif
static bool power_charging;
static bool power_online;
static K_SEM_DEFINE(power_action_sem, 1, 1);
#if DT_HAS_CHOSEN(meshbus_fuel_gauge)
static struct k_work_delayable fuel_gauge_sample_work;
static struct k_work_delayable fuel_gauge_low_voltage_shutdown_work;
#if DT_HAS_CHOSEN(meshbus_charger)
static struct k_work_delayable power_losing_power_shutdown_work;
static struct k_work charger_notification_work;
static bool power_charger_status_notification_enabled;
static bool power_charger_online_notification_enabled;
#endif
static K_MUTEX_DEFINE(fuel_gauge_sample_mutex);
static uint8_t low_voltage_sample_count;
static bool power_work_initialized;
#endif

#if DT_HAS_CHOSEN(meshbus_fuel_gauge)
static int power_fuel_gauge_sample_once(bool publish);
#endif

#if IS_ENABLED(CONFIG_MESHBUS_POWER_BACK_HOLD_SHUTDOWN)
#define POWER_BACK_HOLD_TICK_START_S     3U
#define POWER_BACK_HOLD_RELEASE_CUE_S    6U
#define POWER_BACK_HOLD_CONFIRM_WINDOW_S 1U
#define POWER_BACK_HOLD_TICK_STEP_HZ     250U
#define POWER_BACK_HOLD_TICK_FREQ_HZ(s)                                                             \
	(1500U - (((s) - POWER_BACK_HOLD_TICK_START_S) * POWER_BACK_HOLD_TICK_STEP_HZ))

enum power_back_hold_state {
	POWER_BACK_HOLD_IDLE,
	POWER_BACK_HOLD_HOLDING,
	POWER_BACK_HOLD_CONFIRM,
	POWER_BACK_HOLD_EXPIRED,
};

static K_MUTEX_DEFINE(power_back_hold_mutex);
static struct k_work_delayable power_back_hold_work;
static struct k_work power_back_hold_shutdown_work;
static enum power_back_hold_state power_back_hold_state = POWER_BACK_HOLD_IDLE;
static uint8_t power_back_hold_next_cue_s;
static bool power_back_hold_initialized;

#endif

/* -------------------------------------------------------------------------- */
/* Settings Schema And Apply                                                  */
/* -------------------------------------------------------------------------- */
#define MESHBUS_POWER_SETTINGS_SUBTREE    "meshbus/power"
#define MESHBUS_POWER_SETTINGS_KEY_CONFIG "config"

static K_MUTEX_DEFINE(settings_mutex);
static bool settings_initial_apply = false;
static struct k_work_delayable settings_persistence_work;
static meshbus_power_config settings_load_cfg = MESHBUS_POWER_CONFIG_DEFAULTS;
static struct mb_settings_blob_load_state settings_load_state;

MB_SETTINGS_BLOB_SCHEMA_DEFINE(power_settings_schema, MESHBUS_POWER_SETTINGS_SUBTREE,
			       MESHBUS_POWER_SETTINGS_KEY_CONFIG, meshbus_PowerConfig,
			       meshbus_power_config);

static int power_config_validate(const meshbus_power_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	if (cfg->low_voltage_shutdown_timeout > MESHBUS_POWER_TIMEOUT_MAX_SECONDS) {
		LOG_ERR("Invalid low_voltage_shutdown_timeout: %u",
			cfg->low_voltage_shutdown_timeout);
		return -EINVAL;
	}

	if (cfg->losing_power_shutdown_timeout > MESHBUS_POWER_TIMEOUT_MAX_SECONDS) {
		LOG_ERR("Invalid losing_power_shutdown_timeout: %u",
			cfg->losing_power_shutdown_timeout);
		return -EINVAL;
	}

	if (cfg->no_connection_shutdown_timeout > MESHBUS_POWER_TIMEOUT_MAX_SECONDS) {
		LOG_ERR("Invalid no_connection_shutdown_timeout: %u",
			cfg->no_connection_shutdown_timeout);
		return -EINVAL;
	}

	return 0;
}

static int settings_handler_apply(const meshbus_power_config *cfg, bool persistence, bool force)
{
	int rc = power_config_validate(cfg);
	meshbus_power_config prev_cfg = {0};
#if DT_HAS_CHOSEN(meshbus_fuel_gauge)
	bool reset_low_voltage_state;
#endif
	if (rc != 0) {
		return rc;
	}

	k_mutex_lock(&settings_mutex, K_FOREVER);

	if (!settings_initial_apply) {
		settings_initial_apply = true;
	}
	prev_cfg = power_cfg;
#if DT_HAS_CHOSEN(meshbus_fuel_gauge)
	reset_low_voltage_state =
		force || prev_cfg.low_voltage_shutdown_timeout != cfg->low_voltage_shutdown_timeout;
#endif

	/* Check if there are any changes (skip if force is set) */
	if (!force && prev_cfg.low_voltage_shutdown_timeout == cfg->low_voltage_shutdown_timeout &&
	    prev_cfg.losing_power_shutdown_timeout == cfg->losing_power_shutdown_timeout &&
	    prev_cfg.no_connection_shutdown_timeout == cfg->no_connection_shutdown_timeout) {
		k_mutex_unlock(&settings_mutex);
		LOG_DBG("Settings unchanged, nothing to apply");
		return 0;
	}

	LOG_INF("Settings apply: low_voltage_shutdown_timeout=%u losing_power_shutdown_timeout=%u "
		"no_connection_shutdown_timeout=%u",
		cfg->low_voltage_shutdown_timeout, cfg->losing_power_shutdown_timeout,
		cfg->no_connection_shutdown_timeout);
	if (cfg->no_connection_shutdown_timeout > 0U) {
		LOG_DBG("no_connection_shutdown_timeout is persisted but not enforced");
	}

	/* Copy configuration */
	memcpy(&power_cfg, cfg, sizeof(meshbus_power_config));
#if DT_HAS_CHOSEN(meshbus_fuel_gauge)
	if (reset_low_voltage_state) {
		low_voltage_sample_count = 0U;
	}
#endif

	k_mutex_unlock(&settings_mutex);

#if DT_HAS_CHOSEN(meshbus_fuel_gauge)
	if (power_work_initialized && reset_low_voltage_state) {
		(void)k_work_cancel_delayable(&fuel_gauge_low_voltage_shutdown_work);
	}
#if DT_HAS_CHOSEN(meshbus_charger)
	if (power_work_initialized &&
	    prev_cfg.losing_power_shutdown_timeout != cfg->losing_power_shutdown_timeout) {
		(void)k_work_cancel_delayable(&power_losing_power_shutdown_work);
	}
#endif
#endif

	if (persistence) {
		k_work_reschedule(&settings_persistence_work,
				  K_MSEC(CONFIG_MESHBUS_SETTINGS_PERSISTENCE_DELAY));
	}

	return 0;
}

MB_SETTINGS_BLOB_CONFIG_DEFINE(power_settings_schema, settings_mutex, settings_load_state,
			       settings_load_cfg, power_cfg, settings_initial_apply,
			       meshbus_power_config, meshbus_PowerConfig_size,
			       settings_handler_apply, "power")

SETTINGS_STATIC_HANDLER_DEFINE(meshbus_power, MESHBUS_POWER_SETTINGS_SUBTREE, NULL,
			       settings_handle_set, settings_handle_commit,
			       settings_handle_export);

/* -------------------------------------------------------------------------- */
/* Callbacks And Work                                                         */
/* -------------------------------------------------------------------------- */
#if DT_HAS_CHOSEN(meshbus_fuel_gauge)
static int power_fuel_gauge_sample_once(bool publish)
{
	int rc;
	int sample_rc = -ENODATA;
	struct meshbus_power_fuel_gauge_data_event event = {0};
	union fuel_gauge_prop_val val;
	meshbus_power_config cfg;
	uint32_t raw_voltage_mv = 0;
	bool has_voltage = false;
	bool has_soc = false;
	bool has_sample = false;
	bool charging_valid = true;
	bool low_voltage_sample = false;
	bool should_schedule_low_voltage_shutdown = false;
	bool should_cancel_low_voltage_shutdown = false;
#if DT_HAS_CHOSEN(meshbus_charger)
	bool charger_online = false;
	bool charger_online_valid = false;
	bool should_schedule_losing_power_shutdown = false;
	bool should_cancel_losing_power_shutdown = false;
#endif

	k_mutex_lock(&fuel_gauge_sample_mutex, K_FOREVER);

	/* Get voltage */
	rc = fuel_gauge_get_prop(fuel_gauge_dev, FUEL_GAUGE_VOLTAGE_UV, &val);
	if (rc == 0) {
		raw_voltage_mv = (uint32_t)(val.voltage_uv / 1000U); /* Convert uV to mV */
		has_voltage = true;
	} else {
		LOG_DBG("Failed to get battery voltage: %d", rc);
	}

	/* Get state of charge */
	rc = fuel_gauge_get_prop(fuel_gauge_dev, FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE_PCT, &val);
	if (rc == 0) {
		event.soc_percent = val.relative_state_of_charge_pct;
		has_soc = true;
	} else {
		LOG_DBG("Failed to get battery SoC: %d", rc);
	}

	/* Get temperature */
	rc = fuel_gauge_get_prop(fuel_gauge_dev, FUEL_GAUGE_TEMPERATURE_DK, &val);
	if (rc == 0) {
		event.temperature_dk = val.temperature_dk;
	} else {
		event.temperature_dk = 0U;
		LOG_DBG("Failed to get battery temperature: %d", rc);
	}

	/* Sample charger status */
	event.charging = false;
#if DT_HAS_CHOSEN(meshbus_charger)
	union charger_propval charger_val;
	charging_valid = false;
	rc = charger_get_prop(charger_dev, CHARGER_PROP_STATUS, &charger_val);
	if (rc == 0) {
		event.charging = (charger_val.status == CHARGER_STATUS_CHARGING);
		charging_valid = true;
	} else {
		LOG_DBG("Failed to get charger status: %d", rc);
	}

	rc = charger_get_prop(charger_dev, CHARGER_PROP_ONLINE, &charger_val);
	if (rc == 0) {
		charger_online = (charger_val.online != CHARGER_ONLINE_OFFLINE);
		charger_online_valid = true;
	} else if (rc != -ENOTSUP) {
		LOG_DBG("Failed to get charger online status: %d", rc);
	}
#endif

	k_mutex_lock(&settings_mutex, K_FOREVER);
	cfg = power_cfg;

	if (has_voltage) {
		event.voltage_mv =
			(raw_voltage_mv > UINT16_MAX) ? UINT16_MAX : (uint16_t)raw_voltage_mv;
	} else {
		event.voltage_mv = 0U;
	}
	has_sample = has_voltage && has_soc;

	low_voltage_sample =
		cfg.low_voltage_shutdown_timeout > 0U && has_sample && charging_valid &&
		event.soc_percent == 0U && !event.charging;
	if (low_voltage_sample) {
		if (low_voltage_sample_count < MESHBUS_POWER_LOW_VOLTAGE_SAMPLE_THRESHOLD) {
			low_voltage_sample_count++;
		}
		should_schedule_low_voltage_shutdown =
			low_voltage_sample_count >= MESHBUS_POWER_LOW_VOLTAGE_SAMPLE_THRESHOLD;
	} else {
		low_voltage_sample_count = 0U;
		should_cancel_low_voltage_shutdown = true;
	}

#if DT_HAS_CHOSEN(meshbus_charger)
	if (cfg.losing_power_shutdown_timeout > 0U && charger_online_valid && !charger_online) {
		should_schedule_losing_power_shutdown = true;
	} else {
		should_cancel_losing_power_shutdown = true;
	}
#endif

	/* Update cached battery info only after a complete voltage/SoC sample. */
	if (has_sample) {
		power_battery_voltage_mv = event.voltage_mv;
		power_battery_soc_percent = event.soc_percent;
		power_battery_temperature_dk = event.temperature_dk;
		power_battery_sample_valid = true;
	}
	power_charging = event.charging;
#if DT_HAS_CHOSEN(meshbus_charger)
	power_online = charger_online_valid ? charger_online : false;
	event.online = power_online;
#endif

	k_mutex_unlock(&settings_mutex);

	if (should_schedule_low_voltage_shutdown &&
	    !k_work_delayable_is_pending(&fuel_gauge_low_voltage_shutdown_work)) {
		LOG_WRN("Battery at 0%% for %u samples - scheduling shutdown in %u seconds",
			MESHBUS_POWER_LOW_VOLTAGE_SAMPLE_THRESHOLD,
			cfg.low_voltage_shutdown_timeout);
		(void)k_work_schedule(&fuel_gauge_low_voltage_shutdown_work,
				      K_SECONDS(cfg.low_voltage_shutdown_timeout));
	} else if (should_cancel_low_voltage_shutdown) {
		(void)k_work_cancel_delayable(&fuel_gauge_low_voltage_shutdown_work);
	}

#if DT_HAS_CHOSEN(meshbus_charger)
	if (should_schedule_losing_power_shutdown &&
	    !k_work_delayable_is_pending(&power_losing_power_shutdown_work)) {
		LOG_WRN("External power lost - scheduling shutdown in %u seconds",
			cfg.losing_power_shutdown_timeout);
		(void)k_work_schedule(&power_losing_power_shutdown_work,
				      K_SECONDS(cfg.losing_power_shutdown_timeout));
	} else if (should_cancel_losing_power_shutdown) {
		(void)k_work_cancel_delayable(&power_losing_power_shutdown_work);
	}
#endif

	/* Publish fuel gauge sample event */
	if (has_sample && publish) {
		rc = zbus_chan_pub(&meshbus_power_fuel_gauge_data_chan, &event, K_NO_WAIT);
		if (rc != 0) {
			LOG_DBG("Failed to publish fuel gauge data event: %d", rc);
		}
	}

	LOG_DBG("Fuel gauge sample: valid=%s voltage=%u mV, soc=%u%%, temp=%u dK, charging=%s, "
		"online=%s",
		has_sample ? "yes" : "no", event.voltage_mv, event.soc_percent,
		event.temperature_dk, event.charging ? "yes" : "no",
		event.online ? "yes" : "no");

	if (has_sample) {
		sample_rc = 0;
	}

	k_mutex_unlock(&fuel_gauge_sample_mutex);

	return sample_rc;
}

#if DT_HAS_CHOSEN(meshbus_charger)
static void power_charger_notification_work_handler(struct k_work *work)
{
	int rc;

	ARG_UNUSED(work);

	rc = power_fuel_gauge_sample_once(true);
	if (rc != 0) {
		LOG_DBG("Failed to sample power state after charger notification: %d", rc);
	}
}

static void power_charger_status_notifier(enum charger_status status)
{
	ARG_UNUSED(status);

	(void)k_work_submit(&charger_notification_work);
}

static void power_charger_online_notifier(enum charger_online online)
{
	ARG_UNUSED(online);

	(void)k_work_submit(&charger_notification_work);
}

static void power_charger_notifications_register(void)
{
	union charger_propval val;
	int rc;

	power_charger_status_notification_enabled = false;
	power_charger_online_notification_enabled = false;

	val.status_notification = power_charger_status_notifier;
	rc = charger_set_prop(charger_dev, CHARGER_PROP_STATUS_NOTIFICATION, &val);
	if (rc == 0) {
		power_charger_status_notification_enabled = true;
	} else if (rc != -ENOTSUP) {
		LOG_WRN("Failed to register charger status notification: %d", rc);
	}

	val.online_notification = power_charger_online_notifier;
	rc = charger_set_prop(charger_dev, CHARGER_PROP_ONLINE_NOTIFICATION, &val);
	if (rc == 0) {
		power_charger_online_notification_enabled = true;
	} else if (rc != -ENOTSUP) {
		LOG_WRN("Failed to register charger online notification: %d", rc);
	}

	LOG_INF("Charger notifications: status=%s online=%s",
		power_charger_status_notification_enabled ? "yes" : "no",
		power_charger_online_notification_enabled ? "yes" : "no");
}
#endif

static void power_fuel_gauge_sample_work_handler(struct k_work *work)
{
	uint32_t sample_interval_ms = CONFIG_MESHBUS_POWER_SAMPLE_INTERVAL;

	ARG_UNUSED(work);

	(void)power_fuel_gauge_sample_once(true);

#if DT_HAS_CHOSEN(meshbus_charger)
	if (!power_charger_online_notification_enabled &&
	    (meshbus_power_is_charging() || meshbus_power_is_online())) {
		sample_interval_ms =
			MIN(sample_interval_ms, MESHBUS_POWER_EXTERNAL_SAMPLE_INTERVAL_MS);
	}
#endif

	k_work_schedule(&fuel_gauge_sample_work, K_MSEC(sample_interval_ms));
}

static void power_low_voltage_shutdown_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	LOG_ERR("Low battery shutdown: battery at 0%%");
	meshbus_power_shutdown();
}

#if DT_HAS_CHOSEN(meshbus_charger)
static void power_losing_power_shutdown_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	LOG_ERR("Losing power shutdown: external power offline");
	meshbus_power_shutdown();
}
#endif
#endif

#if IS_ENABLED(CONFIG_MESHBUS_POWER_BACK_HOLD_SHUTDOWN)
static bool power_back_hold_is_back_code(uint16_t code)
{
	if (code == INPUT_BTN_BACK || code == INPUT_KEY_ESC || code == INPUT_KEY_BACKSPACE) {
		return true;
	}

#if defined(INPUT_KEY_BACK)
	if (code == INPUT_KEY_BACK) {
		return true;
	}
#endif

	return false;
}

static void power_back_hold_play_tick(uint8_t cue_s)
{
	ARG_UNUSED(cue_s);

#if IS_ENABLED(CONFIG_MESHBUS_INDICATOR)
	static struct indicator_buzzer_note tick_note;
	static struct indicator_buzzer_melody tick_melody = {
		.notes = &tick_note,
		.length = 1U,
	};
	int rc;

	tick_note.freq_hz = POWER_BACK_HOLD_TICK_FREQ_HZ(cue_s);
	tick_note.duration_ms = 45U;
	rc = meshbus_indicator_buzzer_play(INDICATOR_SOURCE_SYSTEM, &tick_melody);

	if (rc != 0 && rc != -EACCES) {
		LOG_DBG("BACK hold tick skipped: %d", rc);
	}
#endif
}

static void power_back_hold_play_release_cue(void)
{
#if IS_ENABLED(CONFIG_MESHBUS_INDICATOR)
	int rc = meshbus_indicator_buzzer_play(INDICATOR_SOURCE_SYSTEM,
					       &indicator_buzzer_power_back_hold_release_tone);

	if (rc != 0 && rc != -EACCES) {
		LOG_DBG("BACK hold release cue skipped: %d", rc);
	}
#endif
}

static void power_back_hold_cancel_locked(void)
{
	power_back_hold_state = POWER_BACK_HOLD_IDLE;
	power_back_hold_next_cue_s = 0U;
	(void)k_work_cancel_delayable(&power_back_hold_work);
}

static void power_back_hold_work_handler(struct k_work *work)
{
	uint8_t tick_cue_s = 0U;
	bool play_release_cue = false;
	bool play_tick = false;

	ARG_UNUSED(work);

	k_mutex_lock(&power_back_hold_mutex, K_FOREVER);

	switch (power_back_hold_state) {
	case POWER_BACK_HOLD_HOLDING:
		if (power_back_hold_next_cue_s < POWER_BACK_HOLD_RELEASE_CUE_S) {
			play_tick = true;
			tick_cue_s = power_back_hold_next_cue_s;
			power_back_hold_next_cue_s++;
			(void)k_work_schedule(&power_back_hold_work, K_SECONDS(1));
		} else {
			play_release_cue = true;
			power_back_hold_state = POWER_BACK_HOLD_CONFIRM;
			(void)k_work_schedule(&power_back_hold_work,
					      K_SECONDS(POWER_BACK_HOLD_CONFIRM_WINDOW_S));
		}
		break;
	case POWER_BACK_HOLD_CONFIRM:
		power_back_hold_state = POWER_BACK_HOLD_EXPIRED;
		break;
	default:
		break;
	}

	k_mutex_unlock(&power_back_hold_mutex);

	if (play_tick) {
		power_back_hold_play_tick(tick_cue_s);
	}
	if (play_release_cue) {
		power_back_hold_play_release_cue();
	}
}

static void power_back_hold_shutdown_work_handler(struct k_work *work)
{
	int rc;

	ARG_UNUSED(work);

	LOG_INF("BACK hold shutdown confirmed");
	rc = meshbus_power_shutdown();
	if (rc != 0) {
		LOG_WRN("BACK hold shutdown failed: %d", rc);
	}
}

static void power_back_hold_press(void)
{
	k_mutex_lock(&power_back_hold_mutex, K_FOREVER);

	if (power_back_hold_state == POWER_BACK_HOLD_IDLE) {
		power_back_hold_state = POWER_BACK_HOLD_HOLDING;
		power_back_hold_next_cue_s = POWER_BACK_HOLD_TICK_START_S;
		(void)k_work_reschedule(&power_back_hold_work,
					 K_SECONDS(POWER_BACK_HOLD_TICK_START_S));
	}

	k_mutex_unlock(&power_back_hold_mutex);
}

static void power_back_hold_release(void)
{
	bool shutdown_confirmed;

	k_mutex_lock(&power_back_hold_mutex, K_FOREVER);

	shutdown_confirmed = power_back_hold_state == POWER_BACK_HOLD_CONFIRM;
	power_back_hold_cancel_locked();

	k_mutex_unlock(&power_back_hold_mutex);

	if (shutdown_confirmed) {
		(void)k_work_submit(&power_back_hold_shutdown_work);
	}
}

static void power_back_hold_input_listener_cb(const struct zbus_channel *chan, const void *msg)
{
	const struct meshbus_input_event *event = msg;

	if (!power_back_hold_initialized || chan != &meshbus_input_raw_event_chan ||
	    event == NULL || event->type != INPUT_EV_KEY ||
	    !power_back_hold_is_back_code(event->code)) {
		return;
	}

	if (event->value == 0) {
		power_back_hold_release();
	} else {
		power_back_hold_press();
	}
}
#endif

/* -------------------------------------------------------------------------- */
/* Validation And Helpers                                                     */
/* -------------------------------------------------------------------------- */
static int power_wakeup_sources_apply(void)
{
	if (power_button_gpio.port == NULL) {
		LOG_WRN("No power button configured");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&power_button_gpio)) {
		LOG_WRN("Power button GPIO not ready");
		return -ENODEV;
	}

	/* Configure as input */
	int ret = gpio_pin_configure_dt(&power_button_gpio, GPIO_INPUT);
	if (ret != 0) {
		LOG_ERR("Failed to configure wakeup GPIO: %d", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&power_button_gpio, GPIO_INT_LEVEL_ACTIVE);
	if (ret != 0) {
		LOG_ERR("Failed to configure wakeup GPIO interrupt: %d", ret);
		return ret;
	}

	return 0;
}

static void power_action_publish(enum meshbus_power_action event)
{
	(void)k_sem_take(&power_action_sem, K_FOREVER);

	STRUCT_SECTION_FOREACH(meshbus_power_action_callback, callback) {
		if (callback->callback != NULL) {
			callback->callback(event, callback->user_data);
		}
	}

	k_sem_give(&power_action_sem);
}

/* -------------------------------------------------------------------------- */
/* Power Callback                                                             */
/* -------------------------------------------------------------------------- */
static void meshbus_power_power_cb(enum meshbus_power_action action, void *user_data)
{
	ARG_UNUSED(user_data);

	if (action != MESHBUS_POWER_ACTION_SHUTDOWN && action != MESHBUS_POWER_ACTION_REBOOT) {
		return;
	}

	/* Best-effort: stop any pending flash writes before shutdown/reboot. */
	(void)k_work_cancel_delayable(&settings_persistence_work);

#if IS_ENABLED(CONFIG_MESHBUS_POWER_BACK_HOLD_SHUTDOWN)
	k_mutex_lock(&power_back_hold_mutex, K_FOREVER);
	power_back_hold_cancel_locked();
	k_mutex_unlock(&power_back_hold_mutex);
#endif
}
MESHBUS_POWER_ACTION_CALLBACK_DEFINE(meshbus_power_power_cb, NULL);

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */
int meshbus_power_config_get(meshbus_power_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&settings_mutex, K_FOREVER);
	memcpy(cfg, &power_cfg, sizeof(meshbus_power_config));
	k_mutex_unlock(&settings_mutex);

	return 0;
}

int meshbus_power_config_set(const meshbus_power_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}
	return settings_handler_apply(cfg, true, false);
}

int meshbus_power_config_reset(void)
{
	meshbus_power_config cfg = MESHBUS_POWER_CONFIG_DEFAULTS;
	struct k_work_sync sync;
	int rc;

	(void)k_work_cancel_delayable_sync(&settings_persistence_work, &sync);

	rc = settings_handler_apply(&cfg, false, true);
	if (rc != 0) {
		return rc;
	}

	rc = mb_settings_blob_delete(&power_settings_schema);
	if (rc != 0) {
		LOG_ERR("Failed to delete persisted settings: %d", rc);
		return rc;
	}

	return 0;
}

bool meshbus_power_is_charging(void)
{
	k_mutex_lock(&settings_mutex, K_FOREVER);
	bool charging = power_charging;
	k_mutex_unlock(&settings_mutex);

	return charging;
}

bool meshbus_power_is_online(void)
{
	k_mutex_lock(&settings_mutex, K_FOREVER);
	bool online = power_online;
	k_mutex_unlock(&settings_mutex);

	return online;
}

int meshbus_power_fuel_gauge_get(uint16_t *voltage_mv, uint8_t *soc_percent,
				 uint16_t *temperature_dk)
{
#if !DT_HAS_CHOSEN(meshbus_fuel_gauge)
	ARG_UNUSED(voltage_mv);
	ARG_UNUSED(soc_percent);
	ARG_UNUSED(temperature_dk);
	return -ENODEV;
#else
	bool sample_valid;

	k_mutex_lock(&settings_mutex, K_FOREVER);
	sample_valid = power_battery_sample_valid;
	k_mutex_unlock(&settings_mutex);

	if (!sample_valid) {
		int rc;

		if (!power_work_initialized) {
			return -EAGAIN;
		}

		rc = power_fuel_gauge_sample_once(false);
		if (rc != 0) {
			return rc;
		}
	}

	k_mutex_lock(&settings_mutex, K_FOREVER);

	if (voltage_mv != NULL) {
		*voltage_mv = power_battery_voltage_mv;
	}

	if (soc_percent != NULL) {
		*soc_percent = power_battery_soc_percent;
	}

	if (temperature_dk != NULL) {
		*temperature_dk = power_battery_temperature_dk;
	}

	k_mutex_unlock(&settings_mutex);

	return 0;
#endif
}

int meshbus_power_shutdown(void)
{
	int rc;

	LOG_INF("Shutdown system...");

#if DT_HAS_CHOSEN(meshbus_fuel_gauge)
	k_work_cancel_delayable(&fuel_gauge_sample_work);
	k_work_cancel_delayable(&fuel_gauge_low_voltage_shutdown_work);
#if DT_HAS_CHOSEN(meshbus_charger)
	k_work_cancel_delayable(&power_losing_power_shutdown_work);
	(void)k_work_cancel(&charger_notification_work);
#endif
#endif

	/* Execute all registered shutdown hooks */
	power_action_publish(MESHBUS_POWER_ACTION_SHUTDOWN);

	/* Configure wakeup sources first (before suspending GPIO controller) */
	rc = power_wakeup_sources_apply();
	if (rc != 0) {
		LOG_WRN("Failed to configure wakeup sources: %d", rc);
		/* Continue anyway - device may still wake from reset */
	}

	const struct device *cons = DEVICE_DT_GET_OR_NULL(DT_CHOSEN(zephyr_console));
	if (cons != NULL && device_is_ready(cons)) {
		/* Flush any pending output */
		k_sleep(K_MSEC(50));

		int ret = pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);
		if (ret < 0 && ret != -EALREADY && ret != -ENOTSUP) {
			LOG_WRN("Failed to suspend console: %d", ret);
		}
	}

	/*
	 * Disable the system clock before entering System OFF.
	 * This shuts down GRTC and LFCLK, further reducing power consumption.
	 * Note: This is a one-way operation - the system will not return.
	 */
#if defined(CONFIG_SYSTEM_TIMER_HAS_DISABLE_SUPPORT)
	sys_clock_disable();
#endif

	sys_poweroff();

	/* System should enter sleep and shutdown - should not reach here */
	k_sleep(K_MSEC(100));

	/* If we reach here, sleep failed - try reboot as fallback */
	sys_reboot(SYS_REBOOT_COLD);

	return -EIO;
}

int meshbus_power_reboot(void)
{
	LOG_INF("Rebooting system...");

#if DT_HAS_CHOSEN(meshbus_fuel_gauge)
	k_work_cancel_delayable(&fuel_gauge_sample_work);
	k_work_cancel_delayable(&fuel_gauge_low_voltage_shutdown_work);
#if DT_HAS_CHOSEN(meshbus_charger)
	k_work_cancel_delayable(&power_losing_power_shutdown_work);
	(void)k_work_cancel(&charger_notification_work);
#endif
#endif

	/* Execute all registered reboot hooks */
	power_action_publish(MESHBUS_POWER_ACTION_REBOOT);

	/* Small delay to ensure logs are flushed */
	k_sleep(K_MSEC(100));

	sys_reboot(SYS_REBOOT_COLD);

	/* Should not reach here */
	return -EIO;
}

int meshbus_power_reboot_to_bootloader(void)
{
#if IS_ENABLED(CONFIG_RETENTION_BOOT_MODE)
	int rc;

	LOG_INF("Rebooting into bootloader recovery...");

	rc = bootmode_set(BOOT_MODE_TYPE_BOOTLOADER);
	if (rc != 0) {
		LOG_WRN("Failed to request bootloader recovery: %d", rc);
		return rc;
	}

	return meshbus_power_reboot();
#elif IS_ENABLED(CONFIG_MESHBUS_POWER_BOOTLOADER_GPREGRET)
	LOG_INF("Rebooting into bootloader recovery via GPREGRET...");

	nrf_power_gpregret_set(NRF_POWER, MESHBUS_POWER_BOOTLOADER_GPREGRET_REG,
			       MESHBUS_POWER_BOOTLOADER_GPREGRET_UF2);

	return meshbus_power_reboot();
#else
	return -ENOTSUP;
#endif
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */
static int meshbus_power_init(void)
{
	int rc;

	/* Initialize work items */
	k_work_init_delayable(&settings_persistence_work, settings_persistence_work_handler);
#if IS_ENABLED(CONFIG_MESHBUS_POWER_BACK_HOLD_SHUTDOWN)
	k_work_init_delayable(&power_back_hold_work, power_back_hold_work_handler);
	k_work_init(&power_back_hold_shutdown_work, power_back_hold_shutdown_work_handler);
	power_back_hold_initialized = true;
#endif

	/* Load settings */
	rc = settings_load_subtree(MESHBUS_POWER_SETTINGS_SUBTREE);
	if (rc != 0) {
		return rc;
	}
	if (!settings_initial_apply) {
		rc = settings_handler_apply(&power_cfg, false, true);
		if (rc != 0) {
			return rc;
		}
	}

#if DT_HAS_CHOSEN(meshbus_fuel_gauge)
	/* Initialize fuel gauge */
	k_work_init_delayable(&fuel_gauge_sample_work, power_fuel_gauge_sample_work_handler);
	k_work_init_delayable(&fuel_gauge_low_voltage_shutdown_work,
			      power_low_voltage_shutdown_work_handler);
#if DT_HAS_CHOSEN(meshbus_charger)
	k_work_init_delayable(&power_losing_power_shutdown_work,
			      power_losing_power_shutdown_work_handler);
	k_work_init(&charger_notification_work, power_charger_notification_work_handler);
#endif
	power_work_initialized = true;

	if (!device_is_ready(fuel_gauge_dev)) {
		LOG_ERR("Fuel gauge device not ready");
		return -ENODEV;
	}
	LOG_INF("Fuel gauge device ready");

#if DT_HAS_CHOSEN(meshbus_charger)
	if (!device_is_ready(charger_dev)) {
		LOG_WRN("Charger device not ready");
		return -ENODEV;
	}
	LOG_INF("Charger device ready");
	power_charger_notifications_register();
#endif

	k_work_schedule(&fuel_gauge_sample_work, K_NO_WAIT);

#endif

	return 0;
}

SYS_INIT(meshbus_power_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
