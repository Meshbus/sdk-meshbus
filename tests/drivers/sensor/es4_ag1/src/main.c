/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/sensor.h>
#include <drivers/sensor/es4_ag1.h>
#include <zephyr/ztest.h>

#define ADC_NODE DT_NODELABEL(test_adc)
#define ES4_NODE DT_NODELABEL(es4_ag1)
#define ES4_32_NODE DT_NODELABEL(es4_ag1_32)
#define ES4_EMPTY_NODE DT_NODELABEL(es4_ag1_empty)

static const struct device *const adc_dev = DEVICE_DT_GET(ADC_NODE);
static const struct device *const es4_dev = DEVICE_DT_GET(ES4_NODE);
static const struct device *const es4_32_dev = DEVICE_DT_GET(ES4_32_NODE);
static const struct device *const es4_empty_dev = DEVICE_DT_GET(ES4_EMPTY_NODE);

static void *es4_setup(void)
{
	zassert_true(device_is_ready(adc_dev));
	zassert_true(device_is_ready(es4_dev));
	zassert_true(device_is_ready(es4_32_dev));
	zassert_true(device_is_ready(es4_empty_dev));

	return NULL;
}

static void es4_before(void *fixture)
{
	struct sensor_value zero_offset = {.val1 = 100000, .val2 = 0};
	struct sensor_value transimpedance = {.val1 = 1000, .val2 = 0};
	struct sensor_value sensitivity = {.val1 = 100, .val2 = 0};

	ARG_UNUSED(fixture);

	zassert_ok(sensor_attr_set(es4_dev, SENSOR_CHAN_VOC,
				   SENSOR_ATTR_ES4_AG1_ZERO_OFFSET, &zero_offset));
	zassert_ok(sensor_attr_set(es4_dev, SENSOR_CHAN_VOC,
				   SENSOR_ATTR_ES4_AG1_TRANSIMPEDANCE, &transimpedance));
	zassert_ok(sensor_attr_set(es4_dev, SENSOR_CHAN_VOC,
				   SENSOR_ATTR_ES4_AG1_SENSITIVITY, &sensitivity));
	zassert_ok(sensor_attr_set(es4_32_dev, SENSOR_CHAN_VOC,
				   SENSOR_ATTR_ES4_AG1_ZERO_OFFSET, &zero_offset));
	zassert_ok(sensor_attr_set(es4_32_dev, SENSOR_CHAN_VOC,
				   SENSOR_ATTR_ES4_AG1_TRANSIMPEDANCE, &transimpedance));
	zassert_ok(sensor_attr_set(es4_32_dev, SENSOR_CHAN_VOC,
				   SENSOR_ATTR_ES4_AG1_SENSITIVITY, &sensitivity));
}

ZTEST(es4_ag1, test_channel_get_before_fetch_returns_eagain)
{
	struct sensor_value val;

	zassert_equal(sensor_channel_get(es4_empty_dev, SENSOR_CHAN_VOC, &val), -EAGAIN);
}

ZTEST(es4_ag1, test_voc_conversion_and_voltage)
{
	struct sensor_value val;
	int64_t voc_micro;
	int64_t voltage_micro;

	zassert_ok(adc_emul_const_value_set(adc_dev, 0, 200));
	zassert_ok(sensor_sample_fetch(es4_dev));

	zassert_ok(sensor_channel_get(es4_dev, SENSOR_CHAN_VOLTAGE, &val));
	voltage_micro = sensor_value_to_micro(&val);
	zassert_within(voltage_micro, 200000, 1000);

	zassert_ok(sensor_channel_get(es4_dev, SENSOR_CHAN_VOC, &val));
	voc_micro = sensor_value_to_micro(&val);
	zassert_true(voc_micro > 900000000LL && voc_micro < 1100000000LL,
		     "voc_micro=%lld", (long long)voc_micro);
}

ZTEST(es4_ag1, test_attr_validation)
{
	struct sensor_value invalid_fraction = {.val1 = 1, .val2 = 1};
	struct sensor_value invalid_zero = {.val1 = 0, .val2 = 0};
	struct sensor_value offset = {.val1 = 50000, .val2 = 0};

	zassert_equal(sensor_attr_set(es4_dev, SENSOR_CHAN_VOC,
				      SENSOR_ATTR_ES4_AG1_ZERO_OFFSET, NULL),
		      -EINVAL);
	zassert_equal(sensor_attr_set(es4_dev, SENSOR_CHAN_VOC,
				      SENSOR_ATTR_ES4_AG1_ZERO_OFFSET,
				      &invalid_fraction),
		      -EINVAL);
	zassert_equal(sensor_attr_set(es4_dev, SENSOR_CHAN_VOC,
				      SENSOR_ATTR_ES4_AG1_TRANSIMPEDANCE,
				      &invalid_zero),
		      -EINVAL);
	zassert_ok(sensor_attr_set(es4_dev, SENSOR_CHAN_VOC,
				   SENSOR_ATTR_ES4_AG1_ZERO_OFFSET, &offset));
}

ZTEST(es4_ag1, test_adc_16_bit_resolution_path)
{
	struct sensor_value val;
	int64_t voltage_micro;

	zassert_ok(adc_emul_const_value_set(adc_dev, 1, 250));
	zassert_ok(sensor_sample_fetch(es4_32_dev));
	zassert_ok(sensor_channel_get(es4_32_dev, SENSOR_CHAN_VOLTAGE, &val));
	voltage_micro = sensor_value_to_micro(&val);
	zassert_within(voltage_micro, 250000, 1000);
}

ZTEST_SUITE(es4_ag1, NULL, es4_setup, es4_before, NULL, NULL);
