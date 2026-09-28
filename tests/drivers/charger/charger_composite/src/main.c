/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/charger.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#define ADC_NODE DT_NODELABEL(test_adc)
#define ADC_CHANNEL_ID 0

#define CHARGER_INFER_NODE DT_NODELABEL(charger_infer)
#define CHARGER_GPIO_NODE          DT_NODELABEL(charger_gpio)
#define CHARGER_ONLINE_NODE        DT_NODELABEL(charger_online)
#define CHARGER_DIRECT_NODE        DT_NODELABEL(charger_direct)
#define CHARGER_CHARGING_ONLY_NODE DT_NODELABEL(charger_charging_only)
#define CHARGER_POLL_NODE          DT_NODELABEL(charger_poll)
#define CHARGER_SENSOR_NODE DT_NODELABEL(charger_sensor)
#define CHARGER_FALLBACK_NODE DT_NODELABEL(charger_fallback)
#define CHARGER_FAIL_NODE DT_NODELABEL(charger_fail)
#define CHARGER_UNSUPPORTED_NODE DT_NODELABEL(charger_unsupported)
#define FAILING_SENSOR_NODE DT_NODELABEL(failing_sensor)
#define UNSUPPORTED_SOURCE_NODE DT_NODELABEL(unsupported_source)

#define GPIO_NODE DT_NODELABEL(test_gpio)
#define CHARGING_PIN DT_GPIO_PIN(CHARGER_GPIO_NODE, charging_gpios)
#define ONLINE_PIN DT_GPIO_PIN(CHARGER_GPIO_NODE, online_gpios)
#define POLL_GPIO_NODE    DT_NODELABEL(test_poll_gpio)
#define POLL_CHARGING_PIN DT_GPIO_PIN(CHARGER_POLL_NODE, charging_gpios)
#define POLL_ONLINE_PIN   DT_GPIO_PIN(CHARGER_POLL_NODE, online_gpios)

#define INFER_SAMPLE_INTERVAL_MS DT_PROP(CHARGER_INFER_NODE, charge_detect_sample_interval_ms)
#define INFER_HOLD_MS            DT_PROP(CHARGER_INFER_NODE, charge_detect_hold_ms)
#define ONLINE_HOLD_MS           DT_PROP(CHARGER_INFER_NODE, online_infer_hold_ms)
#define GPIO_DEBOUNCE_MS         DT_PROP(CHARGER_DIRECT_NODE, gpio_debounce_ms)
#define GPIO_POLL_INTERVAL_MS \
	DT_PROP(CHARGER_POLL_NODE, gpio_poll_interval_ms)

static const struct device *const adc_dev = DEVICE_DT_GET(ADC_NODE);
static const struct device *const charger_infer_dev = DEVICE_DT_GET(CHARGER_INFER_NODE);
static const struct device *const charger_gpio_dev = DEVICE_DT_GET(CHARGER_GPIO_NODE);
static const struct device *const charger_online_dev =
	DEVICE_DT_GET(CHARGER_ONLINE_NODE);
static const struct device *const charger_direct_dev =
	DEVICE_DT_GET(CHARGER_DIRECT_NODE);
static const struct device *const charger_charging_only_dev =
	DEVICE_DT_GET(CHARGER_CHARGING_ONLY_NODE);
static const struct device *const charger_poll_dev =
	DEVICE_DT_GET(CHARGER_POLL_NODE);
static const struct device *const charger_sensor_dev = DEVICE_DT_GET(CHARGER_SENSOR_NODE);
static const struct device *const charger_fallback_dev = DEVICE_DT_GET(CHARGER_FALLBACK_NODE);
static const struct device *const charger_fail_dev = DEVICE_DT_GET(CHARGER_FAIL_NODE);
static const struct device *const charger_unsupported_dev = DEVICE_DT_GET(CHARGER_UNSUPPORTED_NODE);
static const struct device *const failing_sensor_dev = DEVICE_DT_GET(FAILING_SENSOR_NODE);
static const struct device *const unsupported_source_dev = DEVICE_DT_GET(UNSUPPORTED_SOURCE_NODE);
static const struct device *const gpio_dev = DEVICE_DT_GET(GPIO_NODE);
static const struct device *const poll_gpio_dev = DEVICE_DT_GET(POLL_GPIO_NODE);

static K_SEM_DEFINE(status_notification_sem, 0, 1);
static K_SEM_DEFINE(online_notification_sem, 0, 1);
static enum charger_status notified_status;
static enum charger_online notified_online;

static void test_status_notifier(enum charger_status status)
{
	notified_status = status;
	k_sem_give(&status_notification_sem);
}

static void test_online_notifier(enum charger_online online)
{
	notified_online = online;
	k_sem_give(&online_notification_sem);
}

static int test_set_status_notifier(const struct device *charger_dev,
				    charger_status_notifier_t notifier)
{
	union charger_propval val = {.status_notification = notifier};

	return charger_set_prop(charger_dev, CHARGER_PROP_STATUS_NOTIFICATION,
				&val);
}

static int test_set_online_notifier(const struct device *charger_dev,
				    charger_online_notifier_t notifier)
{
	union charger_propval val = {.online_notification = notifier};

	return charger_set_prop(charger_dev, CHARGER_PROP_ONLINE_NOTIFICATION,
				&val);
}

static int test_source_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

static int failing_sensor_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(chan);

	return -EIO;
}

static int failing_sensor_channel_get(const struct device *dev, enum sensor_channel chan,
				      struct sensor_value *val)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(chan);

	val->val1 = 0;
	val->val2 = 0;
	return 0;
}

static DEVICE_API(sensor, failing_sensor_api) = {
	.sample_fetch = failing_sensor_sample_fetch,
	.channel_get = failing_sensor_channel_get,
};

DEVICE_DT_DEFINE(FAILING_SENSOR_NODE, test_source_init, NULL, NULL, NULL, POST_KERNEL, 80,
		 &failing_sensor_api);
DEVICE_DT_DEFINE(UNSUPPORTED_SOURCE_NODE, test_source_init, NULL, NULL, NULL, POST_KERNEL, 80,
		 NULL);

static int test_get_status_rc(const struct device *charger_dev, enum charger_status *status)
{
	union charger_propval val;
	int rc = charger_get_prop(charger_dev, CHARGER_PROP_STATUS, &val);

	if (rc == 0) {
		*status = val.status;
	}

	return rc;
}

static enum charger_status test_get_status(const struct device *charger_dev)
{
	enum charger_status status;
	int rc = test_get_status_rc(charger_dev, &status);

	zassert_ok(rc, "CHARGER_PROP_STATUS failed: %d", rc);
	return status;
}

static enum charger_online test_get_online(const struct device *charger_dev)
{
	union charger_propval val;
	int rc = charger_get_prop(charger_dev, CHARGER_PROP_ONLINE, &val);

	zassert_ok(rc, "CHARGER_PROP_ONLINE failed: %d", rc);
	return val.online;
}

static void test_set_battery_mv(uint32_t voltage_mv)
{
	int rc = adc_emul_const_value_set(adc_dev, ADC_CHANNEL_ID, voltage_mv);

	zassert_ok(rc, "adc_emul_const_value_set failed: %d", rc);
	k_sleep(K_MSEC(CONFIG_CHARGER_COMPOSITE_DATA_VALIDITY_MS + 5));
}

static void test_wait_infer_window(void)
{
	k_sleep(K_MSEC(INFER_SAMPLE_INTERVAL_MS + 5));
}

static void test_drive_inferred_charging(const struct device *charger_dev)
{
	test_set_battery_mv(3900);
	test_wait_infer_window();
	(void)test_get_status(charger_dev);

	test_set_battery_mv(3925);
	test_wait_infer_window();
	(void)test_get_status(charger_dev);

	test_set_battery_mv(3950);
	test_wait_infer_window();
	zassert_equal(test_get_status(charger_dev), CHARGER_STATUS_CHARGING);
}

static void *charger_composite_setup(void)
{
	zassert_true(device_is_ready(adc_dev), "adc device not ready");
	zassert_true(device_is_ready(charger_infer_dev), "inferred charger not ready");
	zassert_true(device_is_ready(charger_gpio_dev), "gpio charger not ready");
	zassert_true(device_is_ready(charger_online_dev),
		     "online charger not ready");
	zassert_true(device_is_ready(charger_direct_dev),
		     "direct charger not ready");
	zassert_true(device_is_ready(charger_charging_only_dev),
		     "charging-only charger not ready");
	zassert_true(device_is_ready(charger_poll_dev),
		     "poll charger not ready");
	zassert_true(device_is_ready(charger_sensor_dev), "sensor charger not ready");
	zassert_true(device_is_ready(charger_fallback_dev), "fallback charger not ready");
	zassert_true(device_is_ready(charger_fail_dev), "failure charger not ready");
	zassert_true(device_is_ready(charger_unsupported_dev), "unsupported charger not ready");
	zassert_true(device_is_ready(failing_sensor_dev), "failing sensor not ready");
	zassert_true(device_is_ready(unsupported_source_dev), "unsupported source not ready");
	zassert_true(device_is_ready(gpio_dev), "gpio emul device not ready");
	zassert_true(device_is_ready(poll_gpio_dev),
		     "poll gpio emul device not ready");

	return NULL;
}

static void charger_composite_before(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_ok(gpio_emul_input_set(gpio_dev, CHARGING_PIN, 0));
	zassert_ok(gpio_emul_input_set(gpio_dev, ONLINE_PIN, 0));
	zassert_ok(gpio_emul_input_set(poll_gpio_dev, POLL_CHARGING_PIN, 0));
	zassert_ok(gpio_emul_input_set(poll_gpio_dev, POLL_ONLINE_PIN, 0));
	k_sem_reset(&status_notification_sem);
	k_sem_reset(&online_notification_sem);
	notified_status = CHARGER_STATUS_UNKNOWN;
	notified_online = CHARGER_ONLINE_OFFLINE;
	test_set_battery_mv(3900);
	test_wait_infer_window();
	(void)test_get_status(charger_infer_dev);
	k_sleep(K_MSEC(MAX(INFER_HOLD_MS, ONLINE_HOLD_MS) + 20));
	(void)test_get_status(charger_infer_dev);
}

ZTEST(charger_composite, test_infer_status_from_voltage_trend)
{
	test_set_battery_mv(3900);
	test_wait_infer_window();
	zassert_equal(test_get_status(charger_infer_dev), CHARGER_STATUS_NOT_CHARGING);

	test_set_battery_mv(3925);
	test_wait_infer_window();
	zassert_equal(test_get_status(charger_infer_dev), CHARGER_STATUS_NOT_CHARGING);

	test_set_battery_mv(3950);
	test_wait_infer_window();
	zassert_equal(test_get_status(charger_infer_dev), CHARGER_STATUS_CHARGING);

	test_set_battery_mv(3920);
	test_wait_infer_window();
	zassert_equal(test_get_status(charger_infer_dev), CHARGER_STATUS_CHARGING);

	test_set_battery_mv(3890);
	test_wait_infer_window();
	zassert_equal(test_get_status(charger_infer_dev), CHARGER_STATUS_NOT_CHARGING);
}

ZTEST(charger_composite, test_infer_status_hold_timeout)
{
	test_drive_inferred_charging(charger_infer_dev);
	test_set_battery_mv(3950);
	test_wait_infer_window();
	zassert_equal(test_get_status(charger_infer_dev), CHARGER_STATUS_CHARGING);

	k_sleep(K_MSEC(INFER_HOLD_MS + INFER_SAMPLE_INTERVAL_MS + 20));
	zassert_equal(test_get_status(charger_infer_dev), CHARGER_STATUS_NOT_CHARGING);
}

ZTEST(charger_composite, test_full_hysteresis_without_charging_gpio)
{
	test_set_battery_mv(4210);
	test_wait_infer_window();
	zassert_equal(test_get_status(charger_infer_dev), CHARGER_STATUS_FULL);

	test_set_battery_mv(4160);
	test_wait_infer_window();
	zassert_equal(test_get_status(charger_infer_dev), CHARGER_STATUS_FULL);

	test_set_battery_mv(4130);
	test_wait_infer_window();
	zassert_not_equal(test_get_status(charger_infer_dev), CHARGER_STATUS_FULL);
}

ZTEST(charger_composite, test_infer_online_without_online_gpio)
{
	test_set_battery_mv(3900);
	test_wait_infer_window();
	zassert_equal(test_get_online(charger_infer_dev), CHARGER_ONLINE_OFFLINE);

	test_drive_inferred_charging(charger_infer_dev);
	zassert_equal(test_get_online(charger_infer_dev), CHARGER_ONLINE_FIXED);

	k_sleep(K_MSEC(INFER_HOLD_MS + INFER_SAMPLE_INTERVAL_MS + 20));
	zassert_equal(test_get_online(charger_infer_dev), CHARGER_ONLINE_FIXED);

	k_sleep(K_MSEC(ONLINE_HOLD_MS + 20));
	zassert_equal(test_get_online(charger_infer_dev), CHARGER_ONLINE_OFFLINE);
}

ZTEST(charger_composite, test_gpio_mode_compatibility)
{
	test_set_battery_mv(3900);

	zassert_ok(gpio_emul_input_set(gpio_dev, ONLINE_PIN, 1));
	zassert_ok(gpio_emul_input_set(gpio_dev, CHARGING_PIN, 1));
	zassert_equal(test_get_status(charger_gpio_dev), CHARGER_STATUS_CHARGING);

	zassert_equal(test_get_online(charger_gpio_dev), CHARGER_ONLINE_FIXED);

	zassert_ok(gpio_emul_input_set(gpio_dev, CHARGING_PIN, 0));
	zassert_equal(test_get_status(charger_gpio_dev), CHARGER_STATUS_NOT_CHARGING);

	zassert_ok(gpio_emul_input_set(gpio_dev, ONLINE_PIN, 0));
	zassert_equal(test_get_online(charger_gpio_dev), CHARGER_ONLINE_OFFLINE);

	zassert_ok(gpio_emul_input_set(gpio_dev, ONLINE_PIN, 1));
	test_set_battery_mv(4210);
	test_wait_infer_window();
	zassert_equal(test_get_status(charger_gpio_dev), CHARGER_STATUS_FULL);
}

ZTEST(charger_composite, test_online_gpio_off_clears_inferred_charging)
{
	zassert_ok(gpio_emul_input_set(gpio_dev, ONLINE_PIN, 1));
	test_drive_inferred_charging(charger_online_dev);

	zassert_ok(gpio_emul_input_set(gpio_dev, ONLINE_PIN, 0));
	zassert_equal(test_get_status(charger_online_dev),
		      CHARGER_STATUS_NOT_CHARGING);
}

ZTEST(charger_composite, test_gpio_edges_drive_standard_notifications)
{
	zassert_ok(test_set_status_notifier(charger_direct_dev,
					    test_status_notifier));
	zassert_ok(test_set_online_notifier(charger_direct_dev,
					    test_online_notifier));
	k_sleep(K_MSEC(GPIO_DEBOUNCE_MS + 10));

	zassert_ok(gpio_emul_input_set(gpio_dev, ONLINE_PIN, 1));
	zassert_ok(gpio_emul_input_set(gpio_dev, CHARGING_PIN, 1));
	zassert_ok(k_sem_take(&online_notification_sem,
			      K_MSEC(GPIO_DEBOUNCE_MS + 100)));
	zassert_ok(k_sem_take(&status_notification_sem,
			      K_MSEC(GPIO_DEBOUNCE_MS + 100)));
	zassert_equal(notified_online, CHARGER_ONLINE_FIXED);
	zassert_equal(notified_status, CHARGER_STATUS_CHARGING);

	k_sem_reset(&online_notification_sem);
	k_sem_reset(&status_notification_sem);
	zassert_ok(gpio_emul_input_set(gpio_dev, CHARGING_PIN, 0));
	zassert_ok(k_sem_take(&status_notification_sem,
			      K_MSEC(GPIO_DEBOUNCE_MS + 100)));
	zassert_equal(notified_status, CHARGER_STATUS_NOT_CHARGING);
	zassert_equal(k_sem_take(&online_notification_sem,
				 K_MSEC(GPIO_DEBOUNCE_MS + 20)),
		      -EAGAIN);

	k_sem_reset(&online_notification_sem);
	k_sem_reset(&status_notification_sem);
	zassert_ok(gpio_emul_input_set(gpio_dev, ONLINE_PIN, 0));
	zassert_ok(k_sem_take(&online_notification_sem,
			      K_MSEC(GPIO_DEBOUNCE_MS + 100)));
	zassert_equal(notified_online, CHARGER_ONLINE_OFFLINE);
	zassert_equal(k_sem_take(&status_notification_sem,
				 K_MSEC(GPIO_DEBOUNCE_MS + 20)),
		      -EAGAIN);

	zassert_ok(test_set_status_notifier(charger_direct_dev, NULL));
	zassert_ok(test_set_online_notifier(charger_direct_dev, NULL));
}

ZTEST(charger_composite, test_notification_support_matches_state_sources)
{
	zassert_equal(test_set_status_notifier(charger_infer_dev,
					       test_status_notifier),
		      -ENOTSUP);
	zassert_equal(test_set_online_notifier(charger_infer_dev,
					       test_online_notifier),
		      -ENOTSUP);

	zassert_equal(test_set_status_notifier(charger_gpio_dev,
					       test_status_notifier),
		      -ENOTSUP);
	zassert_ok(test_set_online_notifier(charger_gpio_dev,
					    test_online_notifier));
	zassert_ok(test_set_online_notifier(charger_gpio_dev, NULL));

	zassert_ok(test_set_status_notifier(charger_charging_only_dev,
					    test_status_notifier));
	zassert_equal(test_set_online_notifier(charger_charging_only_dev,
					       test_online_notifier),
		      -ENOTSUP);
	zassert_ok(test_set_status_notifier(charger_charging_only_dev, NULL));
}

ZTEST(charger_composite, test_gpio_poll_fallback_uses_standard_notifications)
{
	zassert_ok(test_set_status_notifier(charger_poll_dev,
					    test_status_notifier));
	zassert_ok(test_set_online_notifier(charger_poll_dev,
					    test_online_notifier));
	k_sleep(K_MSEC(GPIO_POLL_INTERVAL_MS + 10));

	zassert_ok(gpio_emul_input_set(poll_gpio_dev, POLL_ONLINE_PIN, 1));
	zassert_ok(gpio_emul_input_set(poll_gpio_dev, POLL_CHARGING_PIN, 1));
	zassert_ok(k_sem_take(&online_notification_sem,
			      K_MSEC(GPIO_POLL_INTERVAL_MS + 100)));
	zassert_ok(k_sem_take(&status_notification_sem,
			      K_MSEC(GPIO_POLL_INTERVAL_MS + 100)));
	zassert_equal(notified_online, CHARGER_ONLINE_FIXED);
	zassert_equal(notified_status, CHARGER_STATUS_CHARGING);

	zassert_ok(test_set_status_notifier(charger_poll_dev, NULL));
	zassert_ok(test_set_online_notifier(charger_poll_dev, NULL));

	k_sem_reset(&online_notification_sem);
	k_sem_reset(&status_notification_sem);
	zassert_ok(gpio_emul_input_set(poll_gpio_dev, POLL_ONLINE_PIN, 0));
	zassert_ok(gpio_emul_input_set(poll_gpio_dev, POLL_CHARGING_PIN, 0));
	k_sleep(K_MSEC(GPIO_POLL_INTERVAL_MS * 2U));
	zassert_equal(k_sem_take(&online_notification_sem, K_NO_WAIT), -EBUSY);
	zassert_equal(k_sem_take(&status_notification_sem, K_NO_WAIT), -EBUSY);
}

ZTEST(charger_composite, test_sensor_source_voltage)
{
	test_set_battery_mv(4210);
	test_wait_infer_window();
	zassert_equal(test_get_status(charger_sensor_dev), CHARGER_STATUS_FULL);
}

ZTEST(charger_composite, test_primary_failure_falls_back_to_secondary)
{
	test_set_battery_mv(4210);
	test_wait_infer_window();
	zassert_equal(test_get_status(charger_fallback_dev), CHARGER_STATUS_FULL);
}

ZTEST(charger_composite, test_source_failures_are_returned)
{
	enum charger_status status;

	zassert_equal(test_get_status_rc(charger_fail_dev, &status), -EIO);
	zassert_equal(test_get_status_rc(charger_unsupported_dev, &status), -ENOTSUP);
}

ZTEST_SUITE(charger_composite, NULL, charger_composite_setup, charger_composite_before, NULL, NULL);
