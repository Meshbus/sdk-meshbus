/*
 * Copyright (c) 2025 FoBE Projects
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_charger_composite

#include <zephyr/device.h>
#include <zephyr/drivers/charger.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#if IS_ENABLED(CONFIG_SOC_FAMILY_NORDIC_NRF)
#include <hal/nrf_power.h>
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(charger_composite, CONFIG_CHARGER_LOG_LEVEL);

/* Default full charge voltage if not specified in DT (4.20V) */
#define DEFAULT_CHARGE_FULL_VOLTAGE_UV 4200000

/* Default hysteresis voltage (50mV) */
#define DEFAULT_CHARGE_FULL_HYSTERESIS_UV 50000

/* Default rise evidence threshold for voltage trend based charging detect (8mV) */
#define DEFAULT_CHARGE_DETECT_RISE_THRESHOLD_UV 8000

/* Default fall evidence threshold for charging exit (12mV) */
#define DEFAULT_CHARGE_DETECT_FALL_THRESHOLD_UV 12000

/* Minimum elapsed time between trend samples (5s) */
#define DEFAULT_CHARGE_DETECT_SAMPLE_INTERVAL_MS 5000

/* Rise evidence count needed to enter inferred charging state */
#define DEFAULT_CHARGE_DETECT_ENTER_COUNT 2

/* Fall evidence count needed to exit inferred charging state */
#define DEFAULT_CHARGE_DETECT_EXIT_COUNT 2

/* Keep inferred charging for this long after last rise evidence (45s) */
#define DEFAULT_CHARGE_DETECT_HOLD_MS 45000

/* Keep inferred online for this long after charging/full evidence (120s) */
#define DEFAULT_ONLINE_INFER_HOLD_MS 120000

/* Debounce charging/online GPIO edges before notifying consumers (30ms) */
#define DEFAULT_GPIO_DEBOUNCE_MS 30

/* Poll GPIOs without interrupt support once per second */
#define DEFAULT_GPIO_POLL_INTERVAL_MS 1000

struct charger_composite_notify_data;

struct charger_composite_config {
	const struct device *source_primary;
	const struct device *source_secondary;
	struct charger_composite_notify_data *notification;
	struct gpio_dt_spec charging_gpio;
	struct gpio_dt_spec online_gpio;
	bool nordic_vbus_detect;
	uint32_t charge_full_voltage_uv;
	uint32_t charge_full_hysteresis_uv;
	uint32_t charge_detect_rise_threshold_uv;
	uint32_t charge_detect_fall_threshold_uv;
	uint32_t charge_detect_sample_interval_ms;
	uint32_t charge_detect_enter_count;
	uint32_t charge_detect_exit_count;
	uint32_t charge_detect_hold_ms;
	uint32_t online_infer_hold_ms;
	uint32_t gpio_debounce_ms;
	uint32_t gpio_poll_interval_ms;
};

struct charger_composite_gpio_irq {
	struct gpio_callback callback;
	struct charger_composite_notify_data *notification;
	bool ready;
};

struct charger_composite_data {
	struct k_mutex lock;
	k_timepoint_t next_reading;
	int32_t cached_voltage_uv;
	bool voltage_valid;
	bool was_full;	/* Track previous FULL state for hysteresis */
	k_timepoint_t next_infer_sample;
	int32_t last_infer_voltage_uv;
	bool infer_valid;
	uint32_t rise_count;
	uint32_t fall_count;
	k_timepoint_t charge_hold_deadline;
	bool inferred_charging;
	k_timepoint_t online_hold_deadline;
};

struct charger_composite_notify_data {
	const struct device *dev;
	struct k_work_delayable notification_work;
	struct charger_composite_gpio_irq charging_irq;
	struct charger_composite_gpio_irq online_irq;
	charger_status_notifier_t status_notifier;
	charger_online_notifier_t online_notifier;
	enum charger_status notified_status;
	enum charger_online notified_online;
	bool notified_status_valid;
	bool notified_online_valid;
	int charging_gpio_state;
	int online_gpio_state;
	bool charging_gpio_state_valid;
	bool online_gpio_state_valid;
	atomic_t notification_event_pending;
};

static const char *composite_status_to_str(enum charger_status status)
{
	switch (status) {
	case CHARGER_STATUS_CHARGING:
		return "CHARGING";
	case CHARGER_STATUS_NOT_CHARGING:
		return "NOT_CHARGING";
	case CHARGER_STATUS_FULL:
		return "FULL";
	case CHARGER_STATUS_DISCHARGING:
		return "DISCHARGING";
	case CHARGER_STATUS_UNKNOWN:
	default:
		return "UNKNOWN";
	}
}

static bool composite_nordic_vbus_detect_enabled(const struct charger_composite_config *config)
{
	if (!config->nordic_vbus_detect) {
		return false;
	}

#if IS_ENABLED(CONFIG_SOC_FAMILY_NORDIC_NRF) && defined(NRF_POWER) && \
	defined(POWER_USBREGSTATUS_VBUSDETECT_Msk)
	return true;
#else
	LOG_WRN_ONCE("nordic,vbus-detect is not supported on this SoC");
	return false;
#endif
}

static bool composite_nordic_vbus_present(const struct charger_composite_config *config)
{
	if (!composite_nordic_vbus_detect_enabled(config)) {
		return false;
	}

#if IS_ENABLED(CONFIG_SOC_FAMILY_NORDIC_NRF) && defined(NRF_POWER) && \
	defined(POWER_USBREGSTATUS_VBUSDETECT_Msk)
	return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0U;
#else
	return false;
#endif
}

/**
 * @brief Try to get voltage from a device using fuel gauge API first, then sensor API
 *
 * @param source Device to read voltage from
 * @param voltage_uv Output voltage in microvolts
 * @return 0 on success, negative error code on failure
 */
static int composite_get_voltage_from_device(const struct device *source, int32_t *voltage_uv)
{
	struct sensor_value sensor_val;
	union fuel_gauge_prop_val fg_val;
	int rc;
	int last_rc = -ENOTSUP;

	if (DEVICE_API_IS(fuel_gauge, source)) {
		rc = fuel_gauge_get_prop(source, FUEL_GAUGE_VOLTAGE_UV, &fg_val);
		if (rc == 0) {
			*voltage_uv = fg_val.voltage_uv;
			return 0;
		}
		last_rc = rc;
	}

	if (DEVICE_API_IS(sensor, source)) {
		rc = sensor_sample_fetch(source);
		if (rc == 0) {
			rc = sensor_channel_get(source, SENSOR_CHAN_VOLTAGE, &sensor_val);
			if (rc == 0) {
				*voltage_uv = sensor_value_to_micro(&sensor_val);
				return 0;
			}
		}
		last_rc = rc;
	}

	return last_rc;
}

static int composite_fetch_voltage(const struct device *dev)
{
	const struct charger_composite_config *config = dev->config;
	struct charger_composite_data *data = dev->data;
	int32_t voltage_uv;
	int rc = -ENOTSUP;
	bool pm_acquired;

	/* Try primary source first if configured */
	if (config->source_primary != NULL) {
		pm_acquired = false;
		rc = pm_device_runtime_get(config->source_primary);
		if (rc < 0 && rc != -ENOTSUP) {
			return rc;
		}
		pm_acquired = (rc >= 0);

		rc = composite_get_voltage_from_device(config->source_primary, &voltage_uv);
		if (rc == 0) {
			data->cached_voltage_uv = voltage_uv;
			LOG_DBG("Voltage sample from primary %s: %d uV",
				config->source_primary->name, voltage_uv);
		}

		if (pm_acquired) {
			(void)pm_device_runtime_put(config->source_primary);
		}
	}

	/* If primary failed or not configured, try secondary */
	if (rc != 0 && config->source_secondary != NULL) {
		pm_acquired = false;
		rc = pm_device_runtime_get(config->source_secondary);
		if (rc < 0 && rc != -ENOTSUP) {
			return rc;
		}
		pm_acquired = (rc >= 0);

		rc = composite_get_voltage_from_device(config->source_secondary, &voltage_uv);
		if (rc == 0) {
			data->cached_voltage_uv = voltage_uv;
			LOG_DBG("Voltage sample from secondary %s: %d uV",
				config->source_secondary->name, voltage_uv);
		}

		if (pm_acquired) {
			(void)pm_device_runtime_put(config->source_secondary);
		}
	}

	return rc;
}

static int composite_get_voltage(const struct device *dev, int32_t *voltage_uv)
{
	struct charger_composite_data *data = dev->data;
	int rc = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (sys_timepoint_expired(data->next_reading)) {
		rc = composite_fetch_voltage(dev);
		if (rc != 0) {
			k_mutex_unlock(&data->lock);
			return rc;
		}
		data->voltage_valid = true;
		data->next_reading =
			sys_timepoint_calc(K_MSEC(CONFIG_CHARGER_COMPOSITE_DATA_VALIDITY_MS));
	}

	if (!data->voltage_valid) {
		k_mutex_unlock(&data->lock);
		return -EAGAIN;
	}

	*voltage_uv = data->cached_voltage_uv;
	k_mutex_unlock(&data->lock);
	return 0;
}

static bool composite_has_voltage_source(const struct charger_composite_config *config)
{
	return (config->source_primary != NULL) || (config->source_secondary != NULL);
}

static enum charger_status composite_get_full_status_locked(
	const struct charger_composite_config *config, struct charger_composite_data *data,
	int32_t voltage_uv)
{
	int32_t full_exit_threshold;
	if (data->was_full) {
		full_exit_threshold = (int32_t)config->charge_full_voltage_uv -
				      (int32_t)config->charge_full_hysteresis_uv;
		if (full_exit_threshold < 0) {
			full_exit_threshold = 0;
		}

		if (voltage_uv >= full_exit_threshold) {
			return CHARGER_STATUS_FULL;
		}

		data->was_full = false;
		LOG_DBG("FULL exited at %d uV (exit threshold %d uV)", voltage_uv,
			full_exit_threshold);
	}

	if (voltage_uv >= (int32_t)config->charge_full_voltage_uv) {
		data->was_full = true;
		LOG_DBG("FULL entered at %d uV (enter threshold %u uV)", voltage_uv,
			config->charge_full_voltage_uv);
		return CHARGER_STATUS_FULL;
	}

	return CHARGER_STATUS_NOT_CHARGING;
}

static enum charger_status composite_get_inferred_status_locked(
	const struct charger_composite_config *config, struct charger_composite_data *data,
	int32_t voltage_uv)
{
	bool prev_inferred = data->inferred_charging;
	int32_t delta_uv;

	if (!data->infer_valid) {
		data->infer_valid = true;
		data->last_infer_voltage_uv = voltage_uv;
		data->next_infer_sample =
			sys_timepoint_calc(K_MSEC(config->charge_detect_sample_interval_ms));
		LOG_DBG("Inference baseline initialized: %d uV", voltage_uv);
	} else {
		if ((config->charge_detect_sample_interval_ms == 0U) ||
		    sys_timepoint_expired(data->next_infer_sample)) {
			delta_uv = voltage_uv - data->last_infer_voltage_uv;

			if (delta_uv >= (int32_t)config->charge_detect_rise_threshold_uv) {
				data->rise_count++;
				data->fall_count = 0U;
				data->charge_hold_deadline =
					sys_timepoint_calc(K_MSEC(config->charge_detect_hold_ms));
			} else if (delta_uv <= -(int32_t)config->charge_detect_fall_threshold_uv) {
				data->fall_count++;
				data->rise_count = 0U;
			} else {
				if (data->rise_count > 0U) {
					data->rise_count--;
				}
				if (data->fall_count > 0U) {
					data->fall_count--;
				}
			}

			LOG_DBG("Inference sample: v=%d uV, delta=%d uV, rise=%u, fall=%u",
				voltage_uv, delta_uv, data->rise_count, data->fall_count);
			data->last_infer_voltage_uv = voltage_uv;
			data->next_infer_sample =
				sys_timepoint_calc(K_MSEC(config->charge_detect_sample_interval_ms));
		}
	}

	if (data->rise_count >= config->charge_detect_enter_count) {
		data->inferred_charging = true;
	}

	if (data->fall_count >= config->charge_detect_exit_count) {
		data->inferred_charging = false;
	}

	if (data->inferred_charging && (config->charge_detect_hold_ms > 0U)) {
		if (sys_timepoint_expired(data->charge_hold_deadline)) {
			data->inferred_charging = false;
			LOG_DBG("Inference hold timeout expired (%u ms)", config->charge_detect_hold_ms);
		}
	}

	if (prev_inferred != data->inferred_charging) {
		LOG_DBG("Inferred charging state changed: %s -> %s",
			prev_inferred ? "CHARGING" : "NOT_CHARGING",
			data->inferred_charging ? "CHARGING" : "NOT_CHARGING");
	}

	return data->inferred_charging ? CHARGER_STATUS_CHARGING : CHARGER_STATUS_NOT_CHARGING;
}

static int composite_get_status(const struct device *dev, enum charger_status *status)
{
	const struct charger_composite_config *config = dev->config;
	struct charger_composite_data *data = dev->data;
	int32_t voltage_uv;
	enum charger_status full_status;
	int gpio_val;
	int rc;

	if (gpio_is_ready_dt(&config->online_gpio)) {
		gpio_val = gpio_pin_get_dt(&config->online_gpio);
		if (gpio_val < 0) {
			LOG_ERR("Failed to read online GPIO for status: %d",
				gpio_val);
			return gpio_val;
		}

		if (gpio_val == 0) {
			k_mutex_lock(&data->lock, K_FOREVER);
			data->was_full = false;
			data->inferred_charging = false;
			data->rise_count = 0U;
			data->fall_count = 0U;
			data->infer_valid = false;
			data->charge_hold_deadline =
				sys_timepoint_calc(K_NO_WAIT);
			k_mutex_unlock(&data->lock);
			*status = CHARGER_STATUS_NOT_CHARGING;
			LOG_DBG("Online GPIO inactive: status NOT_CHARGING");
			return 0;
		}
	}

	if (gpio_is_ready_dt(&config->charging_gpio)) {
		gpio_val = gpio_pin_get_dt(&config->charging_gpio);
		if (gpio_val < 0) {
			LOG_ERR("Failed to read charging GPIO: %d", gpio_val);
			return gpio_val;
		}

		if (gpio_val != 0) {
			/* GPIO is active - charging in progress */
			k_mutex_lock(&data->lock, K_FOREVER);
			data->was_full = false;
			data->inferred_charging = false;
			data->rise_count = 0U;
			data->fall_count = 0U;
			data->infer_valid = false;
			k_mutex_unlock(&data->lock);
			LOG_DBG("Status determined by charging-gpio: CHARGING");
			*status = CHARGER_STATUS_CHARGING;
			return 0;
		}
	}

	if (composite_nordic_vbus_present(config)) {
		if (composite_has_voltage_source(config)) {
			rc = composite_get_voltage(dev, &voltage_uv);
			if (rc != 0) {
				LOG_WRN("Failed to get voltage while VBUS present: %d", rc);
				return rc;
			}

			k_mutex_lock(&data->lock, K_FOREVER);
			full_status = composite_get_full_status_locked(config, data, voltage_uv);
			if (full_status == CHARGER_STATUS_FULL) {
				data->inferred_charging = false;
				data->rise_count = 0U;
				data->fall_count = 0U;
				data->infer_valid = false;
				*status = CHARGER_STATUS_FULL;
				k_mutex_unlock(&data->lock);
				LOG_DBG("Status determined by Nordic VBUS and full-threshold: FULL");
				return 0;
			}

			data->was_full = false;
			data->inferred_charging = true;
			data->rise_count = 0U;
			data->fall_count = 0U;
			data->infer_valid = false;
			data->charge_hold_deadline =
				sys_timepoint_calc(K_MSEC(config->charge_detect_hold_ms));
			k_mutex_unlock(&data->lock);
		}

		*status = CHARGER_STATUS_CHARGING;
		LOG_DBG("Status determined by Nordic VBUS: CHARGING");
		return 0;
	}

	if (composite_nordic_vbus_detect_enabled(config)) {
		k_mutex_lock(&data->lock, K_FOREVER);
		data->was_full = false;
		data->inferred_charging = false;
		data->rise_count = 0U;
		data->fall_count = 0U;
		data->infer_valid = false;
		data->charge_hold_deadline = sys_timepoint_calc(K_NO_WAIT);
		k_mutex_unlock(&data->lock);
		*status = CHARGER_STATUS_NOT_CHARGING;
		LOG_DBG("Status determined by Nordic VBUS absence: NOT_CHARGING");
		return 0;
	}

	/* Without voltage source we can only report NOT_CHARGING. */
	if (!composite_has_voltage_source(config)) {
		k_mutex_lock(&data->lock, K_FOREVER);
		data->inferred_charging = false;
		data->rise_count = 0U;
		data->fall_count = 0U;
		data->infer_valid = false;
		k_mutex_unlock(&data->lock);
		LOG_DBG("No charging-gpio active and no voltage source, fallback to NOT_CHARGING");
		*status = CHARGER_STATUS_NOT_CHARGING;
		return 0;
	}

	rc = composite_get_voltage(dev, &voltage_uv);
	if (rc != 0) {
		LOG_WRN("Failed to get voltage: %d", rc);
		return rc;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	full_status = composite_get_full_status_locked(config, data, voltage_uv);
	if (full_status == CHARGER_STATUS_FULL) {
		data->inferred_charging = false;
		data->rise_count = 0U;
		data->fall_count = 0U;
		data->infer_valid = false;
		*status = CHARGER_STATUS_FULL;
		k_mutex_unlock(&data->lock);
		LOG_DBG("Status determined by full-threshold: FULL");
		return 0;
	}

	*status = composite_get_inferred_status_locked(config, data, voltage_uv);
	LOG_DBG("Status inferred from voltage trend: %s (%d uV)", composite_status_to_str(*status),
		voltage_uv);
	k_mutex_unlock(&data->lock);
	return 0;
}

static int composite_get_online(const struct device *dev, enum charger_online *online)
{
	const struct charger_composite_config *config = dev->config;
	struct charger_composite_data *data = dev->data;
	enum charger_status status;
	int gpio_val;
	int rc;

	if (gpio_is_ready_dt(&config->online_gpio)) {
		gpio_val = gpio_pin_get_dt(&config->online_gpio);
		if (gpio_val < 0) {
			LOG_ERR("Failed to read online GPIO: %d", gpio_val);
			return gpio_val;
		}

		*online = gpio_val ? CHARGER_ONLINE_FIXED : CHARGER_ONLINE_OFFLINE;
		LOG_DBG("Online determined by online-gpio: %s",
			(*online == CHARGER_ONLINE_OFFLINE) ? "OFFLINE" : "FIXED");
		return 0;
	}

	if (composite_nordic_vbus_present(config)) {
		*online = CHARGER_ONLINE_FIXED;
		LOG_DBG("Online determined by Nordic VBUS: FIXED");
		return 0;
	}

	if (composite_nordic_vbus_detect_enabled(config)) {
		k_mutex_lock(&data->lock, K_FOREVER);
		data->online_hold_deadline = sys_timepoint_calc(K_NO_WAIT);
		k_mutex_unlock(&data->lock);
		*online = CHARGER_ONLINE_OFFLINE;
		LOG_DBG("Online determined by Nordic VBUS absence: OFFLINE");
		return 0;
	}

	if (!composite_has_voltage_source(config)) {
		return -ENOTSUP;
	}

	rc = composite_get_status(dev, &status);
	if (rc != 0) {
		return rc;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	if ((status == CHARGER_STATUS_CHARGING) || (status == CHARGER_STATUS_FULL)) {
		data->online_hold_deadline =
			sys_timepoint_calc(K_MSEC(config->online_infer_hold_ms));
		*online = CHARGER_ONLINE_FIXED;
		LOG_DBG("Online inferred FIXED due to status=%s",
			composite_status_to_str(status));
	} else if (!sys_timepoint_expired(data->online_hold_deadline)) {
		*online = CHARGER_ONLINE_FIXED;
		LOG_DBG("Online inferred FIXED by hold timeout");
	} else {
		*online = CHARGER_ONLINE_OFFLINE;
		LOG_DBG("Online inferred OFFLINE");
	}
	k_mutex_unlock(&data->lock);

	return 0;
}

static bool
composite_status_notification_supported(
	const struct charger_composite_config *config)
{
	return config->notification != NULL &&
	       !composite_has_voltage_source(config) &&
	       !composite_nordic_vbus_detect_enabled(config);
}

static bool
composite_online_notification_supported(
	const struct charger_composite_config *config)
{
	return config->notification != NULL &&
	       config->online_gpio.port != NULL;
}

static const char *
composite_gpio_notification_backend(
	const struct gpio_dt_spec *gpio,
	const struct charger_composite_gpio_irq *irq)
{
	if (gpio->port == NULL) {
		return "none";
	}

	return irq->ready ? "irq" : "poll";
}

static bool composite_gpio_polling_required_locked(
	const struct charger_composite_config *config,
	const struct charger_composite_notify_data *notification)
{
	bool notifier_registered = notification->status_notifier != NULL ||
				   notification->online_notifier != NULL;
	bool charging_poll =
		config->charging_gpio.port != NULL &&
		!notification->charging_irq.ready;
	bool online_poll = config->online_gpio.port != NULL &&
			   !notification->online_irq.ready;

	return notifier_registered && (charging_poll || online_poll);
}

static bool
composite_gpio_poll_changed(
	const struct gpio_dt_spec *gpio,
	const struct charger_composite_gpio_irq *irq,
	int *last_state, bool *state_valid, const char *name)
{
	int state;
	bool changed;

	if (gpio->port == NULL || irq->ready) {
		return false;
	}

	state = gpio_pin_get_dt(gpio);
	if (state < 0) {
		LOG_DBG("Failed to poll %s GPIO: %d", name, state);
		return false;
	}

	changed = *state_valid && (*last_state != state);
	*last_state = state;
	*state_valid = true;
	return changed;
}

static void composite_notification_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct charger_composite_notify_data *notification =
		CONTAINER_OF(dwork, struct charger_composite_notify_data,
			     notification_work);
	const struct charger_composite_config *config =
		notification->dev->config;
	struct charger_composite_data *data = notification->dev->data;
	charger_status_notifier_t status_notifier = NULL;
	charger_online_notifier_t online_notifier = NULL;
	enum charger_status status = CHARGER_STATUS_UNKNOWN;
	enum charger_online online = CHARGER_ONLINE_OFFLINE;
	int status_rc = -ENOTSUP;
	int online_rc = -ENOTSUP;
	bool evaluate;
	bool status_valid;
	bool online_valid;
	bool notify_status = false;
	bool notify_online = false;
	bool polling_required;

	evaluate =
		atomic_set(&notification->notification_event_pending, 0) != 0;
	evaluate |= composite_gpio_poll_changed(
		&config->charging_gpio, &notification->charging_irq,
		&notification->charging_gpio_state,
		&notification->charging_gpio_state_valid, "charging");
	evaluate |= composite_gpio_poll_changed(
		&config->online_gpio, &notification->online_irq,
		&notification->online_gpio_state,
		&notification->online_gpio_state_valid, "online");

	k_mutex_lock(&data->lock, K_FOREVER);
	status_notifier = notification->status_notifier;
	online_notifier = notification->online_notifier;
	status_valid = notification->notified_status_valid;
	online_valid = notification->notified_online_valid;
	k_mutex_unlock(&data->lock);

	if (status_notifier == NULL && online_notifier == NULL) {
		return;
	}

	if (online_notifier != NULL && (evaluate || !online_valid)) {
		online_rc = composite_get_online(notification->dev, &online);
	}
	if (status_notifier != NULL && (evaluate || !status_valid)) {
		status_rc = composite_get_status(notification->dev, &status);
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	if (online_rc == 0 && notification->online_notifier != NULL) {
		notify_online = notification->notified_online_valid &&
				(notification->notified_online != online);
		notification->notified_online = online;
		notification->notified_online_valid = true;
		online_notifier = notification->online_notifier;
	}
	if (status_rc == 0 && notification->status_notifier != NULL) {
		notify_status = notification->notified_status_valid &&
				(notification->notified_status != status);
		notification->notified_status = status;
		notification->notified_status_valid = true;
		status_notifier = notification->status_notifier;
	}
	polling_required =
		composite_gpio_polling_required_locked(config, notification);
	k_mutex_unlock(&data->lock);

	if (online_rc != 0 && online_rc != -ENOTSUP) {
		LOG_WRN("Failed to read online notification state: %d",
			online_rc);
	}
	if (status_rc != 0 && status_rc != -ENOTSUP) {
		LOG_WRN("Failed to read status notification state: %d",
			status_rc);
	}

	if (notify_online) {
		online_notifier(online);
	}
	if (notify_status) {
		status_notifier(status);
	}

	if (polling_required) {
		(void)k_work_schedule(&notification->notification_work,
				      K_MSEC(config->gpio_poll_interval_ms));
	}
}

static void
composite_gpio_callback(const struct device *port,
			struct gpio_callback *cb, uint32_t pins)
{
	struct charger_composite_gpio_irq *irq =
		CONTAINER_OF(cb, struct charger_composite_gpio_irq, callback);
	const struct charger_composite_config *config =
		irq->notification->dev->config;

	ARG_UNUSED(port);
	ARG_UNUSED(pins);

	atomic_set(&irq->notification->notification_event_pending, 1);
	(void)k_work_reschedule(&irq->notification->notification_work,
				K_MSEC(config->gpio_debounce_ms));
}

static bool composite_gpio_irq_init(const struct gpio_dt_spec *gpio,
				    struct charger_composite_gpio_irq *irq,
				    struct charger_composite_notify_data
					    *notification,
				    const char *name)
{
	const struct charger_composite_config *config =
		notification->dev->config;
	int rc;

	if (gpio->port == NULL) {
		return false;
	}

	irq->notification = notification;
	gpio_init_callback(&irq->callback, composite_gpio_callback,
			   BIT(gpio->pin));

	rc = gpio_add_callback_dt(gpio, &irq->callback);
	if (rc != 0) {
		if (rc == -ENOTSUP) {
			LOG_INF("%s GPIO callback unavailable; poll %u ms",
				name,
				config->gpio_poll_interval_ms);
		} else {
			LOG_WRN("Failed to add %s GPIO callback; "
				"poll every %u ms: %d",
				name, config->gpio_poll_interval_ms, rc);
		}
		return false;
	}

	rc = gpio_pin_interrupt_configure_dt(gpio, GPIO_INT_EDGE_BOTH);
	if (rc != 0) {
		(void)gpio_remove_callback(gpio->port, &irq->callback);
		if (rc == -ENOTSUP) {
			LOG_INF("%s GPIO IRQ unavailable; poll every %u ms",
				name,
				config->gpio_poll_interval_ms);
		} else {
			LOG_WRN("Failed to configure %s GPIO interrupt; "
				"poll every %u ms: %d",
				name, config->gpio_poll_interval_ms, rc);
		}
		return false;
	}

	irq->ready = true;
	return true;
}

static int composite_get_prop(const struct device *dev, charger_prop_t prop,
			      union charger_propval *val)
{
	const struct charger_composite_config *config = dev->config;

	switch (prop) {
	case CHARGER_PROP_STATUS:
		return composite_get_status(dev, &val->status);
	case CHARGER_PROP_ONLINE:
		return composite_get_online(dev, &val->online);
	case CHARGER_PROP_CONSTANT_CHARGE_VOLTAGE_UV:
		val->const_charge_voltage_uv = config->charge_full_voltage_uv;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int composite_set_prop(const struct device *dev, charger_prop_t prop,
			      const union charger_propval *val)
{
	const struct charger_composite_config *config = dev->config;
	struct charger_composite_data *data = dev->data;
	struct charger_composite_notify_data *notification =
		config->notification;
	bool notifier_registered;

	if (val == NULL) {
		return -EINVAL;
	}

	if (notification == NULL) {
		return -ENOTSUP;
	}

	switch (prop) {
	case CHARGER_PROP_STATUS_NOTIFICATION:
		if (!composite_status_notification_supported(config)) {
			return -ENOTSUP;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		notification->status_notifier = val->status_notification;
		notification->notified_status_valid = false;
		notifier_registered = (notification->status_notifier != NULL) ||
				      (notification->online_notifier != NULL);
		k_mutex_unlock(&data->lock);
		break;
	case CHARGER_PROP_ONLINE_NOTIFICATION:
		if (!composite_online_notification_supported(config)) {
			return -ENOTSUP;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		notification->online_notifier = val->online_notification;
		notification->notified_online_valid = false;
		notifier_registered = (notification->status_notifier != NULL) ||
				      (notification->online_notifier != NULL);
		k_mutex_unlock(&data->lock);
		break;
	default:
		return -ENOTSUP;
	}

	if (notifier_registered) {
		(void)k_work_reschedule(&notification->notification_work,
					K_NO_WAIT);
	} else {
		(void)k_work_cancel_delayable(&notification->notification_work);
	}

	return 0;
}

static int composite_charge_enable(const struct device *dev, bool enable)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(enable);

	/* This composite driver does not support enabling/disabling charging */
	return -ENOTSUP;
}

static int charger_composite_init(const struct device *dev)
{
	const struct charger_composite_config *config = dev->config;
	struct charger_composite_data *data = dev->data;
	int rc;

	/* Initialize data structure */
	k_mutex_init(&data->lock);
	data->next_reading = sys_timepoint_calc(K_NO_WAIT);
	data->cached_voltage_uv = 0;
	data->voltage_valid = false;
	data->was_full = false;
	data->next_infer_sample = sys_timepoint_calc(K_NO_WAIT);
	data->last_infer_voltage_uv = 0;
	data->infer_valid = false;
	data->rise_count = 0U;
	data->fall_count = 0U;
	data->charge_hold_deadline = sys_timepoint_calc(K_NO_WAIT);
	data->inferred_charging = false;
	data->online_hold_deadline = sys_timepoint_calc(K_NO_WAIT);

	if (config->charge_detect_enter_count == 0U || config->charge_detect_exit_count == 0U) {
		LOG_ERR("charge-detect-enter-count and charge-detect-exit-count must be > 0");
		return -EINVAL;
	}

	if (config->notification != NULL &&
	    config->gpio_poll_interval_ms == 0U) {
		LOG_ERR("gpio-poll-interval-ms must be > 0");
		return -EINVAL;
	}

	if (config->charge_full_hysteresis_uv > config->charge_full_voltage_uv) {
		LOG_ERR("charge-full-hysteresis-microvolt must be <= full voltage");
		return -EINVAL;
	}

	if (config->charge_detect_rise_threshold_uv == 0U ||
	    config->charge_detect_fall_threshold_uv == 0U) {
		LOG_ERR("charge-detect rise/fall thresholds must be > 0");
		return -EINVAL;
	}

	/* Validate primary source if configured */
	if (config->source_primary != NULL && !device_is_ready(config->source_primary)) {
		LOG_ERR("Primary source device not ready");
		return -ENODEV;
	}

	/* Validate secondary source if configured */
	if (config->source_secondary != NULL && !device_is_ready(config->source_secondary)) {
		LOG_ERR("Secondary source device not ready");
		return -ENODEV;
	}

	/* Configure charging GPIO if specified */
	if (config->charging_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&config->charging_gpio)) {
			LOG_ERR("Charging GPIO not ready");
			return -ENODEV;
		}

		rc = gpio_pin_configure_dt(&config->charging_gpio, GPIO_INPUT);
		if (rc < 0) {
			LOG_ERR("Failed to configure charging GPIO: %d", rc);
			return rc;
		}
	}

	/* Configure online GPIO if specified */
	if (config->online_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&config->online_gpio)) {
			LOG_ERR("Online GPIO not ready");
			return -ENODEV;
		}

		rc = gpio_pin_configure_dt(&config->online_gpio, GPIO_INPUT);
		if (rc < 0) {
			LOG_ERR("Failed to configure online GPIO: %d", rc);
			return rc;
		}
	}

	if (config->notification != NULL) {
		struct charger_composite_notify_data *notification =
			config->notification;

		notification->dev = dev;
		k_work_init_delayable(&notification->notification_work,
				      composite_notification_work_handler);
		notification->charging_irq.ready = false;
		notification->online_irq.ready = false;
		notification->status_notifier = NULL;
		notification->online_notifier = NULL;
		notification->notified_status = CHARGER_STATUS_UNKNOWN;
		notification->notified_online = CHARGER_ONLINE_OFFLINE;
		notification->notified_status_valid = false;
		notification->notified_online_valid = false;
		notification->charging_gpio_state = 0;
		notification->online_gpio_state = 0;
		notification->charging_gpio_state_valid = false;
		notification->online_gpio_state_valid = false;
		atomic_set(&notification->notification_event_pending, 0);

		(void)composite_gpio_irq_init(
			&config->charging_gpio, &notification->charging_irq,
			notification, "charging");
		(void)composite_gpio_irq_init(
			&config->online_gpio, &notification->online_irq,
			notification, "online");

		LOG_INF("GPIO notification backends: charging=%s online=%s",
			composite_gpio_notification_backend(
				&config->charging_gpio,
				&notification->charging_irq),
			composite_gpio_notification_backend(
				&config->online_gpio,
				&notification->online_irq));
	}

	LOG_DBG("Charger composite initialized, full threshold=%u uV, rise=%u uV, fall=%u uV",
		config->charge_full_voltage_uv, config->charge_detect_rise_threshold_uv,
		config->charge_detect_fall_threshold_uv);
	LOG_DBG("charging-gpio=%s, online-gpio=%s, nordic-vbus=%s, inference=%s",
		config->charging_gpio.port ? "yes" : "no",
		config->online_gpio.port ? "yes" : "no",
		config->nordic_vbus_detect ? "yes" : "no",
		composite_has_voltage_source(config) ? "enabled" : "disabled");

	return 0;
}

static DEVICE_API(charger, charger_composite_api) = {
	.get_property = composite_get_prop,
	.set_property = composite_set_prop,
	.charge_enable = composite_charge_enable,
};

#define CHARGER_COMPOSITE_HAS_GPIO(inst)                                \
	UTIL_OR(DT_INST_NODE_HAS_PROP(inst, charging_gpios),              \
		DT_INST_NODE_HAS_PROP(inst, online_gpios))

#define CHARGER_COMPOSITE_NOTIFICATION_DEFINE(inst)                 \
	COND_CODE_1(                                                  \
		CHARGER_COMPOSITE_HAS_GPIO(inst),                     \
		(static struct charger_composite_notify_data          \
			 charger_composite_notify_data_##inst;), ())

#define CHARGER_COMPOSITE_NOTIFICATION_GET(inst)                    \
	COND_CODE_1(                                                  \
		CHARGER_COMPOSITE_HAS_GPIO(inst),                     \
		(&charger_composite_notify_data_##inst), (NULL))

#define CHARGER_COMPOSITE_SOURCE(inst, prop)                        \
	COND_CODE_1(                                                  \
		DT_INST_NODE_HAS_PROP(inst, prop),                    \
		(DEVICE_DT_GET(DT_INST_PROP(inst, prop))), (NULL))

#define CHARGER_COMPOSITE_INIT(inst)                                \
	CHARGER_COMPOSITE_NOTIFICATION_DEFINE(inst)                  \
	static const struct charger_composite_config                 \
		charger_composite_config_##inst = {                   \
		.source_primary =                                     \
			CHARGER_COMPOSITE_SOURCE(inst, source_primary), \
		.source_secondary =                                   \
			CHARGER_COMPOSITE_SOURCE(inst, source_secondary), \
		.notification =                                      \
			CHARGER_COMPOSITE_NOTIFICATION_GET(inst),      \
		.charging_gpio =                                     \
			GPIO_DT_SPEC_INST_GET_OR(                     \
				inst, charging_gpios, {0}),           \
		.online_gpio =                                       \
			GPIO_DT_SPEC_INST_GET_OR(                     \
				inst, online_gpios, {0}),             \
		.nordic_vbus_detect =                                \
			DT_INST_PROP(inst, nordic_vbus_detect),       \
		.charge_full_voltage_uv =                            \
			DT_INST_PROP_OR(                              \
				inst,                                  \
				constant_charge_voltage_max_microvolt, \
				DEFAULT_CHARGE_FULL_VOLTAGE_UV),       \
		.charge_full_hysteresis_uv =                         \
			DT_INST_PROP_OR(                              \
				inst, charge_full_hysteresis_microvolt, \
				DEFAULT_CHARGE_FULL_HYSTERESIS_UV),    \
		.charge_detect_rise_threshold_uv =                   \
			DT_INST_PROP_OR(                              \
				inst,                                  \
				charge_detect_rise_threshold_microvolt, \
				DEFAULT_CHARGE_DETECT_RISE_THRESHOLD_UV), \
		.charge_detect_fall_threshold_uv =                   \
			DT_INST_PROP_OR(                              \
				inst,                                  \
				charge_detect_fall_threshold_microvolt, \
				DEFAULT_CHARGE_DETECT_FALL_THRESHOLD_UV), \
		.charge_detect_sample_interval_ms =                  \
			DT_INST_PROP_OR(                              \
				inst, charge_detect_sample_interval_ms, \
				DEFAULT_CHARGE_DETECT_SAMPLE_INTERVAL_MS), \
		.charge_detect_enter_count =                         \
			DT_INST_PROP_OR(                              \
				inst, charge_detect_enter_count,       \
				DEFAULT_CHARGE_DETECT_ENTER_COUNT),    \
		.charge_detect_exit_count =                          \
			DT_INST_PROP_OR(                              \
				inst, charge_detect_exit_count,        \
				DEFAULT_CHARGE_DETECT_EXIT_COUNT),     \
		.charge_detect_hold_ms =                             \
			DT_INST_PROP_OR(                              \
				inst, charge_detect_hold_ms,           \
				DEFAULT_CHARGE_DETECT_HOLD_MS),        \
		.online_infer_hold_ms =                              \
			DT_INST_PROP_OR(                              \
				inst, online_infer_hold_ms,            \
				DEFAULT_ONLINE_INFER_HOLD_MS),         \
		.gpio_debounce_ms =                                  \
			DT_INST_PROP_OR(                              \
				inst, gpio_debounce_ms,                 \
				DEFAULT_GPIO_DEBOUNCE_MS),              \
		.gpio_poll_interval_ms =                             \
			DT_INST_PROP_OR(                              \
				inst, gpio_poll_interval_ms,            \
				DEFAULT_GPIO_POLL_INTERVAL_MS),         \
	};                                                          \
	static struct charger_composite_data                         \
		charger_composite_data_##inst;                        \
	DEVICE_DT_INST_DEFINE(                                       \
		inst, charger_composite_init, NULL,                   \
		&charger_composite_data_##inst,                       \
		&charger_composite_config_##inst, POST_KERNEL,        \
		CONFIG_CHARGER_INIT_PRIORITY, &charger_composite_api)

DT_INST_FOREACH_STATUS_OKAY(CHARGER_COMPOSITE_INIT)
