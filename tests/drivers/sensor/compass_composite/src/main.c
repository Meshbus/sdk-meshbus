/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/compass_composite.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#define ACCEL_NODE           DT_NODELABEL(fake_accel)
#define GYRO_NODE            DT_NODELABEL(fake_gyro)
#define IMU_NODE             DT_NODELABEL(fake_imu)
#define MAGN_NODE            DT_NODELABEL(fake_magn)
#define COMPASS_NODE         DT_NODELABEL(compass)
#define COMPASS_INHERIT_NODE DT_NODELABEL(compass_inherit)
#define COMPASS_FAST_NODE    DT_NODELABEL(compass_fast_fusion)
#define COMPASS_SLOW_NODE    DT_NODELABEL(compass_slow_fusion)
#define COMPASS_SHARED_NODE  DT_NODELABEL(compass_shared_imu)

struct vector_sensor_data {
	double xyz[3];
	double delayed_xyz[3];
	int generic_fetch_rc;
	int channel_fetch_rc;
	uint32_t fetch_count;
	uint32_t fetch_chan_count;
	uint32_t last_fetch_order;
	uint8_t valid_after_fetch_count;
	int accel_attr_set_rc;
	int gyro_attr_set_rc;
	struct sensor_value accel_rate;
	struct sensor_value gyro_rate;
};

static struct vector_sensor_data accel_data;
static struct vector_sensor_data gyro_data;
static struct vector_sensor_data imu_data;
static struct vector_sensor_data magn_data;
static uint32_t fetch_order;

static const struct device *const compass_dev = DEVICE_DT_GET(COMPASS_NODE);
static const struct device *const compass_inherit_dev = DEVICE_DT_GET(COMPASS_INHERIT_NODE);
static const struct device *const compass_fast_dev = DEVICE_DT_GET(COMPASS_FAST_NODE);
static const struct device *const compass_slow_dev = DEVICE_DT_GET(COMPASS_SLOW_NODE);
static const struct device *const compass_shared_dev = DEVICE_DT_GET(COMPASS_SHARED_NODE);

static const double level_xy_samples[][2] = {
	{0.30, 0.00},  {0.21, 0.21},   {0.00, 0.30},  {-0.21, 0.21},
	{-0.30, 0.00}, {-0.21, -0.21}, {0.00, -0.30}, {0.21, -0.21},
};

static int vector_sensor_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

static int vector_sensor_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct vector_sensor_data *data = dev->data;
	uint32_t total_fetch_count;

	data->last_fetch_order = ++fetch_order;
	if (chan == SENSOR_CHAN_ALL) {
		data->fetch_count++;
	} else {
		data->fetch_chan_count++;
	}

	total_fetch_count = data->fetch_count + data->fetch_chan_count;
	if ((data->valid_after_fetch_count > 0U) &&
	    (total_fetch_count >= data->valid_after_fetch_count)) {
		for (size_t i = 0; i < ARRAY_SIZE(data->xyz); i++) {
			data->xyz[i] = data->delayed_xyz[i];
		}
		data->valid_after_fetch_count = 0U;
	}

	return chan == SENSOR_CHAN_ALL ? data->generic_fetch_rc : data->channel_fetch_rc;
}

static int vector_sensor_channel_get(const struct device *dev, enum sensor_channel chan,
				     struct sensor_value *val)
{
	struct vector_sensor_data *data = dev->data;

	ARG_UNUSED(chan);

	for (size_t i = 0; i < 3; i++) {
		zassert_ok(sensor_value_from_double(&val[i], data->xyz[i]));
	}

	return 0;
}

static int vector_sensor_attr_set(const struct device *dev, enum sensor_channel chan,
				  enum sensor_attribute attr, const struct sensor_value *val)
{
	struct vector_sensor_data *data = dev->data;

	if (val == NULL || attr != SENSOR_ATTR_SAMPLING_FREQUENCY) {
		return -ENOTSUP;
	}
	if (chan == SENSOR_CHAN_ACCEL_XYZ) {
		if (data->accel_attr_set_rc != 0) {
			return data->accel_attr_set_rc;
		}
		data->accel_rate = *val;
		return 0;
	}
	if (chan == SENSOR_CHAN_GYRO_XYZ) {
		if (data->gyro_attr_set_rc != 0) {
			return data->gyro_attr_set_rc;
		}
		data->gyro_rate = *val;
		return 0;
	}
	return -ENOTSUP;
}

static int vector_sensor_attr_get(const struct device *dev, enum sensor_channel chan,
				  enum sensor_attribute attr, struct sensor_value *val)
{
	struct vector_sensor_data *data = dev->data;

	if (val == NULL || attr != SENSOR_ATTR_SAMPLING_FREQUENCY) {
		return -ENOTSUP;
	}
	if (chan == SENSOR_CHAN_ACCEL_XYZ) {
		*val = data->accel_rate;
		return 0;
	}
	if (chan == SENSOR_CHAN_GYRO_XYZ) {
		*val = data->gyro_rate;
		return 0;
	}
	return -ENOTSUP;
}

static DEVICE_API(sensor, vector_sensor_api) = {
	.sample_fetch = vector_sensor_sample_fetch,
	.channel_get = vector_sensor_channel_get,
	.attr_set = vector_sensor_attr_set,
	.attr_get = vector_sensor_attr_get,
};

DEVICE_DT_DEFINE(ACCEL_NODE, vector_sensor_init, NULL, &accel_data, NULL, POST_KERNEL, 80,
		 &vector_sensor_api);
DEVICE_DT_DEFINE(GYRO_NODE, vector_sensor_init, NULL, &gyro_data, NULL, POST_KERNEL, 80,
		 &vector_sensor_api);
DEVICE_DT_DEFINE(IMU_NODE, vector_sensor_init, NULL, &imu_data, NULL, POST_KERNEL, 80,
		 &vector_sensor_api);
DEVICE_DT_DEFINE(MAGN_NODE, vector_sensor_init, NULL, &magn_data, NULL, POST_KERNEL, 80,
		 &vector_sensor_api);

static void reset_vectors(void)
{
	struct sensor_value normal_rate = {.val1 = 12, .val2 = 500000};

	accel_data = (struct vector_sensor_data){.xyz = {0.0, 0.0, 1.0},
						.accel_rate = normal_rate,
						.gyro_rate = normal_rate};
	gyro_data = (struct vector_sensor_data){.xyz = {0.0, 0.0, 0.0},
					       .accel_rate = normal_rate,
					       .gyro_rate = normal_rate};
	imu_data = (struct vector_sensor_data){.xyz = {0.0, 0.0, 1.0},
					      .accel_rate = normal_rate,
					      .gyro_rate = normal_rate};
	magn_data = (struct vector_sensor_data){.xyz = {1.0, 0.0, 0.0},
					       .accel_rate = normal_rate,
					       .gyro_rate = normal_rate};
	fetch_order = 0U;
}

static void clear_compass_calibration(const struct device *dev)
{
	static const enum sensor_channel axes[] = {
		SENSOR_CHAN_MAGN_X,
		SENSOR_CHAN_MAGN_Y,
		SENSOR_CHAN_MAGN_Z,
	};
	struct sensor_value zero = {0};
	struct sensor_value trigger = {.val1 = 1};

	for (size_t i = 0; i < ARRAY_SIZE(axes); i++) {
		zassert_ok(sensor_attr_set(dev, axes[i], SENSOR_ATTR_COMPASS_MAG_SCALE, &zero));
	}

	zassert_ok(sensor_attr_set(dev, SENSOR_CHAN_COMPASS_HEADING, SENSOR_ATTR_COMPASS_CAL_RESET,
				   &trigger));
}

static void feed_level_xy_calibration_with_bias(const struct device *dev, double bias_x,
						double bias_y, double magn_z)
{
	for (size_t i = 0; i < ARRAY_SIZE(level_xy_samples); i++) {
		magn_data.xyz[0] = bias_x + level_xy_samples[i][0];
		magn_data.xyz[1] = bias_y + level_xy_samples[i][1];
		magn_data.xyz[2] = magn_z;
		zassert_ok(sensor_sample_fetch(dev));
	}
}

static void feed_level_xy_calibration(const struct device *dev)
{
	feed_level_xy_calibration_with_bias(dev, 0.0, 0.0, 0.02);
}

static double get_heading(const struct device *dev)
{
	struct sensor_value val;

	zassert_ok(sensor_channel_get(dev, SENSOR_CHAN_COMPASS_HEADING, &val));
	return sensor_value_to_double(&val);
}

static void *compass_setup(void)
{
	zassert_true(device_is_ready(compass_dev));
	zassert_true(device_is_ready(compass_inherit_dev));
	zassert_true(device_is_ready(compass_fast_dev));
	zassert_true(device_is_ready(compass_slow_dev));
	zassert_true(device_is_ready(compass_shared_dev));

	return NULL;
}

static void compass_before(void *fixture)
{
	ARG_UNUSED(fixture);

	reset_vectors();
	clear_compass_calibration(compass_dev);
	clear_compass_calibration(compass_inherit_dev);
	clear_compass_calibration(compass_fast_dev);
	clear_compass_calibration(compass_slow_dev);
	clear_compass_calibration(compass_shared_dev);
}

ZTEST(compass_composite, test_fetches_only_configured_channels)
{
	zassert_ok(sensor_sample_fetch(compass_dev));
	zassert_equal(accel_data.fetch_count, 0);
	zassert_equal(gyro_data.fetch_count, 0);
	zassert_equal(magn_data.fetch_count, 0);
	zassert_equal(accel_data.fetch_chan_count, 1);
	zassert_equal(gyro_data.fetch_chan_count, 1);
	zassert_equal(magn_data.fetch_chan_count, 1);
}

ZTEST(compass_composite, test_shared_imu_uses_one_burst_after_magnetometer)
{
	zassert_ok(sensor_sample_fetch(compass_shared_dev));
	zassert_equal(magn_data.fetch_chan_count, 1U);
	zassert_equal(imu_data.fetch_count, 1U);
	zassert_equal(imu_data.fetch_chan_count, 0U);
	zassert_true(magn_data.last_fetch_order < imu_data.last_fetch_order,
		     "magnetometer order=%u imu order=%u", magn_data.last_fetch_order,
		     imu_data.last_fetch_order);
}

ZTEST(compass_composite, test_imu_rate_override_restores_exact_rates)
{
	struct sensor_value rate;

	zassert_ok(compass_composite_imu_rate_override(compass_dev, 104U));
	zassert_ok(sensor_attr_get(DEVICE_DT_GET(ACCEL_NODE), SENSOR_CHAN_ACCEL_XYZ,
				   SENSOR_ATTR_SAMPLING_FREQUENCY, &rate));
	zassert_equal(rate.val1, 104);
	zassert_ok(sensor_attr_get(DEVICE_DT_GET(GYRO_NODE), SENSOR_CHAN_GYRO_XYZ,
				   SENSOR_ATTR_SAMPLING_FREQUENCY, &rate));
	zassert_equal(rate.val1, 104);
	zassert_ok(compass_composite_imu_rate_restore(compass_dev));
	zassert_ok(sensor_attr_get(DEVICE_DT_GET(ACCEL_NODE), SENSOR_CHAN_ACCEL_XYZ,
				   SENSOR_ATTR_SAMPLING_FREQUENCY, &rate));
	zassert_equal(rate.val1, 12);
	zassert_equal(rate.val2, 500000);
	zassert_ok(sensor_attr_get(DEVICE_DT_GET(GYRO_NODE), SENSOR_CHAN_GYRO_XYZ,
				   SENSOR_ATTR_SAMPLING_FREQUENCY, &rate));
	zassert_equal(rate.val1, 12);
	zassert_equal(rate.val2, 500000);
}

ZTEST(compass_composite, test_imu_rate_override_capability_follows_dts)
{
	struct sensor_value capabilities;
	struct sensor_value rate = {.val1 = 104};

	zassert_ok(sensor_attr_get(compass_dev, SENSOR_CHAN_COMPASS_HEADING,
				   SENSOR_ATTR_COMPASS_CAPABILITIES, &capabilities));
	zassert_true((capabilities.val1 & COMPASS_CAP_RATE_OVERRIDE) != 0);
	zassert_ok(sensor_attr_get(compass_inherit_dev, SENSOR_CHAN_COMPASS_HEADING,
				   SENSOR_ATTR_COMPASS_CAPABILITIES, &capabilities));
	zassert_false((capabilities.val1 & COMPASS_CAP_RATE_OVERRIDE) != 0);
	zassert_equal(sensor_attr_set(compass_inherit_dev, SENSOR_CHAN_COMPASS_HEADING,
				      SENSOR_ATTR_COMPASS_RATE_OVERRIDE, &rate),
		      -ENOTSUP);
	zassert_equal(compass_composite_imu_rate_override(compass_inherit_dev, 104U), -ENOTSUP);
	zassert_ok(compass_composite_imu_rate_restore(compass_inherit_dev));
}

ZTEST(compass_composite, test_imu_rate_override_rolls_back_partial_failure)
{
	struct sensor_value rate;

	gyro_data.gyro_attr_set_rc = -EIO;
	zassert_equal(compass_composite_imu_rate_override(compass_dev, 104U), -EIO);
	zassert_ok(sensor_attr_get(DEVICE_DT_GET(ACCEL_NODE), SENSOR_CHAN_ACCEL_XYZ,
				   SENSOR_ATTR_SAMPLING_FREQUENCY, &rate));
	zassert_equal(rate.val1, 12);
	zassert_equal(rate.val2, 500000);
	zassert_ok(compass_composite_imu_rate_restore(compass_dev));
}

ZTEST(compass_composite, test_channel_fetch_falls_back_to_generic_fetch)
{
	accel_data.channel_fetch_rc = -ENOTSUP;
	gyro_data.channel_fetch_rc = -ENOTSUP;
	magn_data.channel_fetch_rc = -ENOTSUP;

	zassert_ok(sensor_sample_fetch(compass_dev));
	zassert_equal(accel_data.fetch_count, 1);
	zassert_equal(gyro_data.fetch_count, 1);
	zassert_equal(magn_data.fetch_count, 1);
	zassert_equal(accel_data.fetch_chan_count, 1);
	zassert_equal(gyro_data.fetch_chan_count, 1);
	zassert_equal(magn_data.fetch_chan_count, 1);
}

ZTEST(compass_composite, test_retries_invalid_first_source_sample)
{
	accel_data.xyz[0] = 0.0;
	accel_data.xyz[1] = 0.0;
	accel_data.xyz[2] = 0.0;
	accel_data.delayed_xyz[0] = 0.0;
	accel_data.delayed_xyz[1] = 0.0;
	accel_data.delayed_xyz[2] = 1.0;
	accel_data.valid_after_fetch_count = 2U;

	zassert_ok(sensor_sample_fetch(compass_dev));
	zassert_equal(accel_data.fetch_chan_count, 2U);
	zassert_true(get_heading(compass_dev) >= 0.0);
}

ZTEST(compass_composite, test_independent_gyro_axis_mapping_affects_fusion)
{
	double before;
	double after;

	accel_data.xyz[0] = 1.0;
	accel_data.xyz[1] = 0.0;
	accel_data.xyz[2] = 0.0;
	magn_data.xyz[0] = 0.0;
	magn_data.xyz[1] = 1.0;
	magn_data.xyz[2] = 0.0;

	zassert_ok(sensor_sample_fetch(compass_dev));
	before = get_heading(compass_dev);

	k_sleep(K_MSEC(120));
	gyro_data.xyz[0] = 0.0;
	gyro_data.xyz[1] = 1.0;
	gyro_data.xyz[2] = 0.0;
	zassert_ok(sensor_sample_fetch(compass_dev));
	after = get_heading(compass_dev);

	zassert_true(fabs(after - before) > 1.0, "heading delta was %f", fabs(after - before));
}

ZTEST(compass_composite, test_gyro_mapping_inherits_accel_mapping_when_omitted)
{
	double before;
	double after;

	accel_data.xyz[0] = 0.0;
	accel_data.xyz[1] = 1.0;
	accel_data.xyz[2] = 0.0;
	magn_data.xyz[0] = 0.0;
	magn_data.xyz[1] = 1.0;
	magn_data.xyz[2] = 0.0;

	zassert_ok(sensor_sample_fetch(compass_inherit_dev));
	before = get_heading(compass_inherit_dev);

	k_sleep(K_MSEC(120));
	gyro_data.xyz[0] = 0.0;
	gyro_data.xyz[1] = 1.0;
	gyro_data.xyz[2] = 0.0;
	zassert_ok(sensor_sample_fetch(compass_inherit_dev));
	after = get_heading(compass_inherit_dev);

	zassert_true(fabs(after - before) > 1.0, "heading delta was %f", fabs(after - before));
}

ZTEST(compass_composite, test_fusion_response_is_sample_rate_invariant)
{
	double fast_heading;
	double slow_heading;

	magn_data.xyz[0] = 1.0;
	magn_data.xyz[1] = 0.0;
	zassert_ok(sensor_sample_fetch(compass_fast_dev));
	zassert_ok(sensor_sample_fetch(compass_slow_dev));

	magn_data.xyz[0] = 0.0;
	magn_data.xyz[1] = 1.0;
	for (size_t i = 0; i < 5U; i++) {
		k_sleep(K_MSEC(20));
		zassert_ok(sensor_sample_fetch(compass_fast_dev));
	}
	zassert_ok(sensor_sample_fetch(compass_slow_dev));

	fast_heading = get_heading(compass_fast_dev);
	slow_heading = get_heading(compass_slow_dev);
	zassert_within(fast_heading, slow_heading, 0.5,
		       "fast=%f slow=%f", fast_heading, slow_heading);
}

ZTEST(compass_composite, test_cal_reset_preserves_manual_override)
{
	struct sensor_value bias = {.val1 = 2, .val2 = 0};
	struct sensor_value scale = {.val1 = 3, .val2 = 0};
	struct sensor_value trigger = {.val1 = 1, .val2 = 0};
	struct sensor_value out;

	zassert_ok(sensor_attr_set(compass_dev, SENSOR_CHAN_MAGN_X, SENSOR_ATTR_COMPASS_MAG_BIAS,
				   &bias));
	zassert_ok(sensor_attr_set(compass_dev, SENSOR_CHAN_MAGN_X, SENSOR_ATTR_COMPASS_MAG_SCALE,
				   &scale));
	zassert_ok(sensor_attr_set(compass_dev, SENSOR_CHAN_COMPASS_HEADING,
				   SENSOR_ATTR_COMPASS_CAL_RESET, &trigger));
	zassert_ok(sensor_attr_get(compass_dev, SENSOR_CHAN_MAGN_X, SENSOR_ATTR_COMPASS_MAG_BIAS,
				   &out));
	zassert_equal(out.val1, 2);
	zassert_equal(out.val2, 0);
	zassert_ok(sensor_attr_get(compass_dev, SENSOR_CHAN_MAGN_X, SENSOR_ATTR_COMPASS_MAG_SCALE,
				   &out));
	zassert_equal(out.val1, 3);
	zassert_equal(out.val2, 0);
}

ZTEST(compass_composite, test_rejects_angle_attribute_overflow)
{
	struct sensor_value value = {.val1 = INT32_MAX};

	zassert_equal(sensor_attr_set(compass_dev, SENSOR_CHAN_COMPASS_HEADING,
				      SENSOR_ATTR_COMPASS_DECLINATION, &value),
		      -ERANGE);
	zassert_equal(sensor_attr_set(compass_dev, SENSOR_CHAN_COMPASS_HEADING,
				      SENSOR_ATTR_COMPASS_MOUNT_OFFSET, &value),
		      -ERANGE);
}

ZTEST(compass_composite, test_level_xy_coverage_satisfies_calibration_hint)
{
	struct sensor_value hint;
	struct sensor_value accuracy;
	struct sensor_value bias;
	struct sensor_value scale;

	feed_level_xy_calibration(compass_dev);

	zassert_ok(sensor_attr_get(compass_dev, SENSOR_CHAN_COMPASS_HEADING,
				   SENSOR_ATTR_COMPASS_CAL_HINT, &hint));
	zassert_not_equal(hint.val1, COMPASS_CAL_HINT_FIGURE_EIGHT);

	zassert_ok(sensor_attr_get(compass_dev, SENSOR_CHAN_COMPASS_HEADING,
				   SENSOR_ATTR_COMPASS_ACCURACY, &accuracy));
	zassert_true(accuracy.val1 >= COMPASS_ACCURACY_MEDIUM, "accuracy was %d", accuracy.val1);

	zassert_ok(sensor_attr_get(compass_dev, SENSOR_CHAN_MAGN_X,
				   SENSOR_ATTR_COMPASS_MAG_BIAS_EST, &bias));
	zassert_equal(sensor_attr_get(compass_dev, SENSOR_CHAN_MAGN_Z,
				      SENSOR_ATTR_COMPASS_MAG_BIAS_EST, &bias),
		      -EAGAIN);
	zassert_ok(sensor_attr_get(compass_dev, SENSOR_CHAN_MAGN_Z, SENSOR_ATTR_COMPASS_MAG_SCALE,
				   &scale));
	zassert_equal(scale.val1, 1);
	zassert_equal(scale.val2, 0);
}

ZTEST(compass_composite, test_calibration_requires_enough_samples)
{
	struct sensor_value bias;

	magn_data.xyz[0] = 0.30;
	magn_data.xyz[1] = 0.00;
	zassert_ok(sensor_sample_fetch(compass_dev));
	magn_data.xyz[0] = -0.30;
	zassert_ok(sensor_sample_fetch(compass_dev));

	zassert_equal(sensor_attr_get(compass_dev, SENSOR_CHAN_MAGN_X,
				      SENSOR_ATTR_COMPASS_MAG_BIAS_EST, &bias),
		      -EAGAIN);
}

ZTEST(compass_composite, test_calibration_window_uses_elapsed_time)
{
	struct sensor_value bias;

	feed_level_xy_calibration(compass_inherit_dev);
	zassert_equal(sensor_attr_get(compass_inherit_dev, SENSOR_CHAN_MAGN_X,
				      SENSOR_ATTR_COMPASS_MAG_BIAS_EST, &bias),
		      -EAGAIN);

	clear_compass_calibration(compass_inherit_dev);
	for (size_t i = 0; i < ARRAY_SIZE(level_xy_samples); i++) {
		if (i > 0U) {
			k_sleep(K_MSEC(110));
		}
		magn_data.xyz[0] = level_xy_samples[i][0];
		magn_data.xyz[1] = level_xy_samples[i][1];
		magn_data.xyz[2] = 0.02;
		zassert_ok(sensor_sample_fetch(compass_inherit_dev));
	}

	zassert_ok(sensor_attr_get(compass_inherit_dev, SENSOR_CHAN_MAGN_X,
				   SENSOR_ATTR_COMPASS_MAG_BIAS_EST, &bias));
}

ZTEST(compass_composite, test_new_calibration_rebaselines_field_stability)
{
	struct sensor_value accuracy;

	feed_level_xy_calibration_with_bias(compass_dev, 1.0, 0.5, 0.2);

	zassert_ok(sensor_attr_get(compass_dev, SENSOR_CHAN_COMPASS_HEADING,
				   SENSOR_ATTR_COMPASS_ACCURACY, &accuracy));
	zassert_true(accuracy.val1 >= COMPASS_ACCURACY_MEDIUM, "accuracy was %d", accuracy.val1);
}

ZTEST(compass_composite, test_xy_calibration_requires_level_sample_history)
{
	struct sensor_value bias;

	accel_data.xyz[0] = 0.60;
	accel_data.xyz[2] = 0.80;
	feed_level_xy_calibration(compass_dev);

	zassert_equal(sensor_attr_get(compass_dev, SENSOR_CHAN_MAGN_X,
				      SENSOR_ATTR_COMPASS_MAG_BIAS_EST, &bias),
		      -EAGAIN);
}

ZTEST(compass_composite, test_full_3d_coverage_estimates_all_axes)
{
	struct sensor_value bias;

	for (size_t sample = 0; sample < 8; sample++) {
		magn_data.xyz[0] = (sample & BIT(0)) != 0U ? 0.30 : -0.30;
		magn_data.xyz[1] = (sample & BIT(1)) != 0U ? 0.30 : -0.30;
		magn_data.xyz[2] = (sample & BIT(2)) != 0U ? 0.30 : -0.30;
		zassert_ok(sensor_sample_fetch(compass_dev));
	}

	zassert_ok(sensor_attr_get(compass_dev, SENSOR_CHAN_MAGN_Z,
				   SENSOR_ATTR_COMPASS_MAG_BIAS_EST, &bias));
}

ZTEST(compass_composite, test_estimated_calibration_survives_stationary_window)
{
	struct sensor_value hint;
	struct sensor_value accuracy;
	struct sensor_value bias;

	feed_level_xy_calibration(compass_dev);

	magn_data.xyz[0] = 0.30;
	magn_data.xyz[1] = 0.00;
	magn_data.xyz[2] = 0.02;

	for (size_t i = 0; i < 80; i++) {
		zassert_ok(sensor_sample_fetch(compass_dev));
	}

	zassert_ok(sensor_attr_get(compass_dev, SENSOR_CHAN_COMPASS_HEADING,
				   SENSOR_ATTR_COMPASS_CAL_HINT, &hint));
	zassert_not_equal(hint.val1, COMPASS_CAL_HINT_FIGURE_EIGHT);

	zassert_ok(sensor_attr_get(compass_dev, SENSOR_CHAN_COMPASS_HEADING,
				   SENSOR_ATTR_COMPASS_ACCURACY, &accuracy));
	zassert_true(accuracy.val1 >= COMPASS_ACCURACY_MEDIUM, "accuracy was %d", accuracy.val1);

	zassert_ok(sensor_attr_get(compass_dev, SENSOR_CHAN_MAGN_X,
				   SENSOR_ATTR_COMPASS_MAG_BIAS_EST, &bias));
}

ZTEST_SUITE(compass_composite, NULL, compass_setup, compass_before, NULL, NULL);
