/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/sensor.h>
#include <drivers/sensor/mini_pid.h>
#include <zephyr/ztest.h>

#define ADC_NODE DT_NODELABEL(test_adc)
#define MINI_PID_NODE DT_NODELABEL(mini_pid)
#define MINI_PID_32_NODE DT_NODELABEL(mini_pid_32)
#define MINI_PID_EMPTY_NODE DT_NODELABEL(mini_pid_empty)

static const struct device *const adc_dev = DEVICE_DT_GET(ADC_NODE);
static const struct device *const mini_pid_dev = DEVICE_DT_GET(MINI_PID_NODE);
static const struct device *const mini_pid_32_dev = DEVICE_DT_GET(MINI_PID_32_NODE);
static const struct device *const mini_pid_empty_dev = DEVICE_DT_GET(MINI_PID_EMPTY_NODE);

static void *mini_pid_setup(void)
{
	zassert_true(device_is_ready(adc_dev));
	zassert_true(device_is_ready(mini_pid_dev));
	zassert_true(device_is_ready(mini_pid_32_dev));
	zassert_true(device_is_ready(mini_pid_empty_dev));

	return NULL;
}

static void mini_pid_before(void *fixture)
{
	struct sensor_value baseline = {.val1 = 100000, .val2 = 0};
	struct sensor_value sensitivity = {.val1 = 1000, .val2 = 0};

	ARG_UNUSED(fixture);

	zassert_ok(sensor_attr_set(mini_pid_dev, SENSOR_CHAN_VOC,
				   SENSOR_ATTR_MINI_PID_BASELINE, &baseline));
	zassert_ok(sensor_attr_set(mini_pid_dev, SENSOR_CHAN_VOC,
				   SENSOR_ATTR_MINI_PID_SENSITIVITY, &sensitivity));
	zassert_ok(sensor_attr_set(mini_pid_32_dev, SENSOR_CHAN_VOC,
				   SENSOR_ATTR_MINI_PID_BASELINE, &baseline));
	zassert_ok(sensor_attr_set(mini_pid_32_dev, SENSOR_CHAN_VOC,
				   SENSOR_ATTR_MINI_PID_SENSITIVITY, &sensitivity));
}

ZTEST(mini_pid, test_channel_get_before_fetch_returns_eagain)
{
	struct sensor_value val;

	zassert_equal(sensor_channel_get(mini_pid_empty_dev, SENSOR_CHAN_VOC, &val), -EAGAIN);
}

ZTEST(mini_pid, test_voc_reports_ppm_sensor_value)
{
	struct sensor_value val;
	int64_t voc_micro;
	int64_t voltage_micro;

	zassert_ok(adc_emul_const_value_set(adc_dev, 0, 200));
	zassert_ok(sensor_sample_fetch(mini_pid_dev));
	zassert_ok(sensor_channel_get(mini_pid_dev, SENSOR_CHAN_VOC, &val));
	voc_micro = sensor_value_to_micro(&val);
	zassert_within(voc_micro, 100000000, 1000000);

	zassert_ok(sensor_channel_get(mini_pid_dev, SENSOR_CHAN_VOLTAGE, &val));
	voltage_micro = sensor_value_to_micro(&val);
	zassert_within(voltage_micro, 200000, 1000);
}

ZTEST(mini_pid, test_attr_validation)
{
	struct sensor_value invalid_fraction = {.val1 = 1000, .val2 = 1};
	struct sensor_value invalid_sensitivity = {.val1 = 0, .val2 = 0};
	struct sensor_value baseline = {.val1 = 50000, .val2 = 0};

	zassert_equal(sensor_attr_set(mini_pid_dev, SENSOR_CHAN_VOC,
				      SENSOR_ATTR_MINI_PID_BASELINE, NULL),
		      -EINVAL);
	zassert_equal(sensor_attr_set(mini_pid_dev, SENSOR_CHAN_VOC,
				      SENSOR_ATTR_MINI_PID_BASELINE, &invalid_fraction),
		      -EINVAL);
	zassert_equal(sensor_attr_set(mini_pid_dev, SENSOR_CHAN_VOC,
				      SENSOR_ATTR_MINI_PID_SENSITIVITY,
				      &invalid_sensitivity),
		      -EINVAL);
	zassert_ok(sensor_attr_set(mini_pid_dev, SENSOR_CHAN_VOC,
				   SENSOR_ATTR_MINI_PID_BASELINE, &baseline));
}

ZTEST(mini_pid, test_adc_16_bit_resolution_path)
{
	struct sensor_value val;
	int64_t voltage_micro;

	zassert_ok(adc_emul_const_value_set(adc_dev, 1, 250));
	zassert_ok(sensor_sample_fetch(mini_pid_32_dev));
	zassert_ok(sensor_channel_get(mini_pid_32_dev, SENSOR_CHAN_VOLTAGE, &val));
	voltage_micro = sensor_value_to_micro(&val);
	zassert_within(voltage_micro, 250000, 1000);
}

ZTEST_SUITE(mini_pid, NULL, mini_pid_setup, mini_pid_before, NULL, NULL);
