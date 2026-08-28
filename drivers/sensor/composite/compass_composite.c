/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_compass_composite

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/compass_composite.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(compass_composite, CONFIG_SENSOR_LOG_LEVEL);

#define COMPASS_DEG_PER_RAD 57.2957795130823208768f
#define COMPASS_EPSILON     1.0e-6f

#define COMPASS_COVERAGE_SPAN_MIN_GAUSS     0.08f
#define COMPASS_COVERAGE_SPAN_WEAK_GAUSS    0.04f
#define COMPASS_FIELD_INSTABILITY_RATIO_MAX 0.20f
#define COMPASS_LEVEL_RATIO_MIN             0.90f
#define COMPASS_FIELD_STABILITY_EMA_ALPHA   0.10f
#define COMPASS_CAL_SCALE_MIN               0.25f
#define COMPASS_CAL_SCALE_MAX               4.00f
#define COMPASS_MAGN_WINDOW_SIZE            64U
#define COMPASS_MAGN_WINDOW_BLOCK_COUNT     4U
#define COMPASS_MAGN_WINDOW_BLOCK_SIZE      \
	(COMPASS_MAGN_WINDOW_SIZE / COMPASS_MAGN_WINDOW_BLOCK_COUNT)
#define COMPASS_MAGN_WINDOW_SCALE           1000.0f
#define COMPASS_MAGN_WINDOW_SCALE_INV       (1.0f / COMPASS_MAGN_WINDOW_SCALE)
#define COMPASS_CAL_MIN_SAMPLES             8U
#define COMPASS_CAL_LEVEL_SAMPLE_RATIO_NUM  3U
#define COMPASS_CAL_LEVEL_SAMPLE_RATIO_DEN  4U
#define COMPASS_FUSION_REFERENCE_PERIOD_S   0.1f

#define COMPASS_MAGN_AXIS_X_MASK   BIT(0)
#define COMPASS_MAGN_AXIS_Y_MASK   BIT(1)
#define COMPASS_MAGN_AXIS_Z_MASK   BIT(2)
#define COMPASS_MAGN_AXIS_XY_MASK  (COMPASS_MAGN_AXIS_X_MASK | COMPASS_MAGN_AXIS_Y_MASK)
#define COMPASS_MAGN_AXIS_XYZ_MASK (COMPASS_MAGN_AXIS_XY_MASK | COMPASS_MAGN_AXIS_Z_MASK)

#define COMPASS_HEADING_CHANNEL ((enum sensor_channel)SENSOR_CHAN_COMPASS_HEADING)

BUILD_ASSERT(COMPASS_MAGN_WINDOW_SIZE % COMPASS_MAGN_WINDOW_BLOCK_COUNT == 0U,
	     "magnetometer window must divide evenly into summary blocks");
BUILD_ASSERT(COMPASS_MAGN_WINDOW_BLOCK_SIZE <= UINT8_MAX,
	     "magnetometer block sample count must fit in uint8_t");

struct compass_composite_config {
	const struct device *accel_source;
	const struct device *gyro_source;
	const struct device *magn_source;
	enum sensor_channel accel_channel;
	enum sensor_channel gyro_channel;
	enum sensor_channel magn_channel;
	uint32_t source_startup_delay_ms;
	uint32_t source_idle_timeout_ms;
	uint16_t calibration_sample_period_ms;
	int32_t accel_axis_remap[3];
	int32_t accel_axis_sign[3];
	int32_t gyro_axis_remap[3];
	int32_t gyro_axis_sign[3];
	int32_t magn_axis_remap[3];
	int32_t magn_axis_sign[3];
	int32_t declination_mdeg;
	int32_t mount_offset_mdeg;
	uint16_t gyro_fusion_alpha_milli;
	bool imu_rate_override_supported;
};

struct compass_magn_window_block {
	int16_t min[3];
	int16_t max[3];
	uint8_t sample_count;
	uint8_t level_count;
};

BUILD_ASSERT(sizeof(struct compass_magn_window_block) * COMPASS_MAGN_WINDOW_BLOCK_COUNT <= 64U,
	     "magnetometer window summaries exceed their RAM budget");

struct compass_composite_data {
	struct k_mutex lock;
	const struct device *source_lifetime_devices[3];
	bool source_lifetime_held[3];
	size_t source_lifetime_count;
	bool source_lifetime_active;
	struct sensor_value saved_accel_rate;
	struct sensor_value saved_gyro_rate;
	uint32_t imu_rate_override_hz;
	bool imu_rate_overridden;
	float heading_deg;
	float fused_magnetic_heading_deg;
	uint32_t last_sample_timestamp;
	int32_t declination_mdeg;
	int32_t mount_offset_mdeg;
	uint16_t fusion_alpha_milli;
	bool heading_valid;
	bool fused_heading_valid;
	float magn_min[3];
	float magn_max[3];
	/* Summaries retain a 64-sample horizon without storing every 3-axis sample. */
	struct compass_magn_window_block magn_window[COMPASS_MAGN_WINDOW_BLOCK_COUNT];
	uint8_t magn_window_count;
	uint8_t magn_window_block;
	uint8_t magn_window_level_count;
	uint16_t magn_window_elapsed_ms;
	float field_magnitude_ema;
	bool field_magnitude_ema_valid;
	float magn_bias_manual[3];
	float magn_scale_manual[3];
	bool magn_manual_override[3];
	float magn_bias_est[3];
	float magn_scale_est[3];
	float magn_est_quality;
	uint8_t magn_est_valid_mask;
	enum compass_sensor_cal_hint cal_hint;
	enum compass_sensor_accuracy accuracy;
};

/* Composite instances may share physical source sensors. */
static K_MUTEX_DEFINE(compass_source_mutex);

static bool compass_channel_supported(enum sensor_channel chan)
{
	return (chan == SENSOR_CHAN_ALL) || (chan == COMPASS_HEADING_CHANNEL);
}

static bool compass_heading_channel_supported_for_attr(enum sensor_channel chan)
{
	return (chan == SENSOR_CHAN_ALL) || (chan == COMPASS_HEADING_CHANNEL);
}

static int compass_magn_channel_to_axis(enum sensor_channel chan)
{
	switch (chan) {
	case SENSOR_CHAN_MAGN_X:
		return 0;
	case SENSOR_CHAN_MAGN_Y:
		return 1;
	case SENSOR_CHAN_MAGN_Z:
		return 2;
	default:
		return -ENOTSUP;
	}
}

static float compass_wrap_degrees(float deg)
{
	deg = fmodf(deg, 360.0f);
	if (deg < 0.0f) {
		deg += 360.0f;
	}

	return deg;
}

static int compass_sensor_value_to_mdeg(const struct sensor_value *val, int32_t *mdeg)
{
	const int64_t value_mdeg = sensor_value_to_micro(val) / 1000LL;

	if (value_mdeg < INT32_MIN || value_mdeg > INT32_MAX) {
		return -ERANGE;
	}

	*mdeg = (int32_t)value_mdeg;
	return 0;
}

static float compass_angle_diff_degrees(float target, float reference)
{
	float diff = target - reference;

	while (diff > 180.0f) {
		diff -= 360.0f;
	}
	while (diff < -180.0f) {
		diff += 360.0f;
	}

	return diff;
}

static float compass_norm3(const float v[3])
{
	return sqrtf((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]));
}

static bool compass_vector_is_finite(const float v[3])
{
	return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

static int16_t compass_magn_window_encode(float gauss)
{
	float scaled = CLAMP(gauss * COMPASS_MAGN_WINDOW_SCALE, (float)INT16_MIN, (float)INT16_MAX);

	if (scaled <= (float)INT16_MIN) {
		return INT16_MIN;
	}
	if (scaled >= (float)INT16_MAX) {
		return INT16_MAX;
	}

	return (int16_t)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

static float compass_magn_window_decode(int16_t milligauss)
{
	return (float)milligauss * COMPASS_MAGN_WINDOW_SCALE_INV;
}

static bool compass_axis_config_valid(const int32_t axis_map[3], const int32_t axis_sign[3])
{
	bool used[3] = {false, false, false};

	for (size_t i = 0; i < 3; i++) {
		if ((axis_map[i] < 0) || (axis_map[i] > 2)) {
			return false;
		}

		if ((axis_sign[i] != 1) && (axis_sign[i] != -1)) {
			return false;
		}

		if (used[(int)axis_map[i]]) {
			return false;
		}

		used[(int)axis_map[i]] = true;
	}

	return true;
}

static void compass_apply_axis_mapping(const float in[3], const int32_t axis_map[3],
				       const int32_t axis_sign[3], float out[3])
{
	for (size_t i = 0; i < 3; i++) {
		out[i] = (float)axis_sign[i] * in[(int)axis_map[i]];
	}
}

static void compass_sensor_values_to_float3(const struct sensor_value vals[3], float out[3])
{
	for (size_t i = 0; i < 3; i++) {
		out[i] = (float)sensor_value_to_double(&vals[i]);
	}
}

static float compass_accuracy_to_trust(enum compass_sensor_accuracy accuracy)
{
	switch (accuracy) {
	case COMPASS_ACCURACY_HIGH:
		return 1.0f;
	case COMPASS_ACCURACY_MEDIUM:
		return 0.6f;
	case COMPASS_ACCURACY_LOW:
		return 0.3f;
	case COMPASS_ACCURACY_UNRELIABLE:
	default:
		return 0.1f;
	}
}

static uint8_t compass_calibrated_axes_locked(const struct compass_composite_data *data)
{
	uint8_t axes = data->magn_est_valid_mask;

	for (size_t i = 0; i < 3; i++) {
		if (data->magn_manual_override[i]) {
			axes |= BIT(i);
		}
	}

	return axes;
}

static bool compass_has_effective_calibration_locked(const struct compass_composite_data *data,
						     bool level_good)
{
	uint8_t required_axes = level_good ? COMPASS_MAGN_AXIS_XY_MASK : COMPASS_MAGN_AXIS_XYZ_MASK;

	return (compass_calibrated_axes_locked(data) & required_axes) == required_axes;
}

static void compass_active_calibration_axis_locked(const struct compass_composite_data *data,
						   size_t axis, float *bias, float *scale)
{
	if (data->magn_manual_override[axis]) {
		*bias = data->magn_bias_manual[axis];
		*scale = data->magn_scale_manual[axis];
	} else if ((data->magn_est_valid_mask & BIT(axis)) != 0U) {
		*bias = data->magn_bias_est[axis];
		*scale = data->magn_scale_est[axis];
	} else {
		*bias = 0.0f;
		*scale = 1.0f;
	}
}

static void compass_reset_live_calibration_locked(struct compass_composite_data *data)
{
	data->magn_window_count = 0U;
	data->magn_window_block = 0U;
	data->magn_window_level_count = 0U;
	data->magn_window_elapsed_ms = 0U;
	data->field_magnitude_ema = 0.0f;
	data->field_magnitude_ema_valid = false;
	data->magn_est_quality = 0.0f;
	data->magn_est_valid_mask = 0U;
	data->cal_hint = COMPASS_CAL_HINT_FIGURE_EIGHT;
	data->accuracy = COMPASS_ACCURACY_UNRELIABLE;
	for (size_t block = 0; block < COMPASS_MAGN_WINDOW_BLOCK_COUNT; block++) {
		data->magn_window[block].sample_count = 0U;
		data->magn_window[block].level_count = 0U;
	}

	for (size_t i = 0; i < 3; i++) {
		data->magn_min[i] = 0.0f;
		data->magn_max[i] = 0.0f;
		data->magn_bias_est[i] = 0.0f;
		data->magn_scale_est[i] = 1.0f;
	}
}

static void compass_update_heading_locked(struct compass_composite_data *data)
{
	float declination_deg;
	float mount_offset_deg;

	if (!data->fused_heading_valid) {
		data->heading_valid = false;
		return;
	}

	declination_deg = (float)data->declination_mdeg / 1000.0f;
	mount_offset_deg = (float)data->mount_offset_mdeg / 1000.0f;
	data->heading_deg = compass_wrap_degrees(data->fused_magnetic_heading_deg +
						 declination_deg + mount_offset_deg);
	data->heading_valid = true;
}

static void compass_recompute_extrema_locked(struct compass_composite_data *data)
{
	bool initialized = false;

	for (size_t block = 0; block < COMPASS_MAGN_WINDOW_BLOCK_COUNT; block++) {
		const struct compass_magn_window_block *summary = &data->magn_window[block];

		if (summary->sample_count == 0U) {
			continue;
		}

		for (size_t axis = 0; axis < 3; axis++) {
			const float min = compass_magn_window_decode(summary->min[axis]);
			const float max = compass_magn_window_decode(summary->max[axis]);

			if (!initialized) {
				data->magn_min[axis] = min;
				data->magn_max[axis] = max;
			} else {
				data->magn_min[axis] = MIN(data->magn_min[axis], min);
				data->magn_max[axis] = MAX(data->magn_max[axis], max);
			}
		}
		initialized = true;
	}
}

static void compass_update_magn_window_locked(struct compass_composite_data *data,
						      const float magn[3], bool level_good)
{
	struct compass_magn_window_block *block = &data->magn_window[data->magn_window_block];

	if (block->sample_count == COMPASS_MAGN_WINDOW_BLOCK_SIZE) {
		data->magn_window_block =
			(data->magn_window_block + 1U) % COMPASS_MAGN_WINDOW_BLOCK_COUNT;
		block = &data->magn_window[data->magn_window_block];
		data->magn_window_count -= block->sample_count;
		data->magn_window_level_count -= block->level_count;
		block->sample_count = 0U;
		block->level_count = 0U;
	}

	for (size_t axis = 0; axis < 3; axis++) {
		const int16_t stored = compass_magn_window_encode(magn[axis]);

		if (block->sample_count == 0U) {
			block->min[axis] = stored;
			block->max[axis] = stored;
		} else {
			block->min[axis] = MIN(block->min[axis], stored);
			block->max[axis] = MAX(block->max[axis], stored);
		}
	}
	block->sample_count++;
	data->magn_window_count++;
	if (level_good) {
		block->level_count++;
		data->magn_window_level_count++;
	}

	compass_recompute_extrema_locked(data);
}

static bool compass_magn_window_sample_due_locked(struct compass_composite_data *data,
						  uint32_t elapsed_ms,
						  uint16_t sample_period_ms)
{
	uint64_t accumulated_ms;

	if (data->magn_window_count == 0U || sample_period_ms == 0U) {
		data->magn_window_elapsed_ms = 0U;
		return true;
	}

	accumulated_ms = (uint64_t)data->magn_window_elapsed_ms + elapsed_ms;
	if (accumulated_ms < sample_period_ms) {
		data->magn_window_elapsed_ms = (uint16_t)accumulated_ms;
		return false;
	}

	data->magn_window_elapsed_ms = (uint16_t)(accumulated_ms % sample_period_ms);
	return true;
}

static bool compass_window_level_coverage_good(const struct compass_composite_data *data)
{
	return data->magn_window_count >= COMPASS_CAL_MIN_SAMPLES &&
	       ((uint32_t)data->magn_window_level_count * COMPASS_CAL_LEVEL_SAMPLE_RATIO_DEN) >=
		       ((uint32_t)data->magn_window_count * COMPASS_CAL_LEVEL_SAMPLE_RATIO_NUM);
}

static uint8_t compass_compute_magn_calibration(const struct compass_composite_data *data,
						float bias[3], float scale[3], float *quality)
{
	float span[3];
	float radius[3];

	if (data->magn_window_count < COMPASS_CAL_MIN_SAMPLES) {
		return 0U;
	}

	for (size_t i = 0; i < 3; i++) {
		bias[i] = (data->magn_max[i] + data->magn_min[i]) * 0.5f;
		span[i] = data->magn_max[i] - data->magn_min[i];
		radius[i] = span[i] * 0.5f;
		scale[i] = 1.0f;
	}

	/*
	 * Basic hard/soft-iron compensation using a min/max "window" ellipsoid
	 * approximation.
	 *
	 * - Full 3D calibration requires all axes to have enough span.
	 * - As a pragmatic fallback, allow 2D (XY) calibration when the board is kept
	 * level.
	 */
	const bool have_3d_coverage = (span[0] >= COMPASS_COVERAGE_SPAN_WEAK_GAUSS) &&
				      (span[1] >= COMPASS_COVERAGE_SPAN_WEAK_GAUSS) &&
				      (span[2] >= COMPASS_COVERAGE_SPAN_WEAK_GAUSS);

	const bool have_2d_coverage = compass_window_level_coverage_good(data) &&
				      (span[0] >= COMPASS_COVERAGE_SPAN_WEAK_GAUSS) &&
				      (span[1] >= COMPASS_COVERAGE_SPAN_WEAK_GAUSS);

	if (have_3d_coverage) {
		float avg_radius = (radius[0] + radius[1] + radius[2]) / 3.0f;

		for (size_t i = 0; i < 3; i++) {
			if (radius[i] > COMPASS_EPSILON) {
				scale[i] = avg_radius / radius[i];
			}
		}
	} else if (have_2d_coverage) {
		float avg_radius = (radius[0] + radius[1]) / 2.0f;

		for (size_t i = 0; i < 2; i++) {
			if (radius[i] > COMPASS_EPSILON) {
				scale[i] = avg_radius / radius[i];
			}
		}
		scale[2] = 1.0f;
	} else {
		return 0U;
	}

	for (size_t i = 0; i < 3; i++) {
		scale[i] = CLAMP(scale[i], COMPASS_CAL_SCALE_MIN, COMPASS_CAL_SCALE_MAX);
	}

	*quality = have_3d_coverage ? MIN(span[0], MIN(span[1], span[2])) : MIN(span[0], span[1]);
	return have_3d_coverage ? COMPASS_MAGN_AXIS_XYZ_MASK : COMPASS_MAGN_AXIS_XY_MASK;
}

static int compass_source_runtime_get(const struct device *sensor, bool *held,
				      bool *was_suspended)
{
	*held = false;
	*was_suspended = false;

	if (!device_is_ready(sensor)) {
		return -ENODEV;
	}

#if defined(CONFIG_PM_DEVICE) && defined(CONFIG_PM_DEVICE_RUNTIME)
	enum pm_device_state state = PM_DEVICE_STATE_ACTIVE;
	int rc;

	(void)pm_device_state_get(sensor, &state);
	*was_suspended = (state == PM_DEVICE_STATE_SUSPENDED);

	if (!pm_device_runtime_is_enabled(sensor)) {
		rc = pm_device_runtime_enable(sensor);
		if (rc != 0 && rc != -ENOTSUP && rc != -ENOSYS && rc != -EBUSY) {
			return rc;
		}
	}

	rc = pm_device_runtime_get(sensor);
	if (rc == -ENOTSUP || rc == -ENOSYS) {
		return 0;
	}
	if (rc != 0) {
		return rc;
	}

	*held = true;
#endif

	return 0;
}

static int compass_source_runtime_put(const struct device *sensor, bool held,
				      uint32_t idle_timeout_ms)
{
	if (!held) {
		return 0;
	}

#if defined(CONFIG_PM_DEVICE) && defined(CONFIG_PM_DEVICE_RUNTIME)
	int rc;

	if (idle_timeout_ms > 0U) {
		rc = pm_device_runtime_put_async(sensor, K_MSEC(idle_timeout_ms));
		if (rc == -ENOSYS) {
			rc = pm_device_runtime_put(sensor);
		}
	} else {
		rc = pm_device_runtime_put(sensor);
	}

	return (rc == -ENOTSUP || rc == -ENOSYS || rc == -EALREADY) ? 0 : rc;
#else
	ARG_UNUSED(sensor);
	ARG_UNUSED(idle_timeout_ms);
	return 0;
#endif
}

static size_t compass_unique_sources_get(const struct compass_composite_config *cfg,
					 const struct device **sources)
{
	const struct device *configured[] = {
		cfg->accel_source,
		cfg->gyro_source,
		cfg->magn_source,
	};
	size_t count = 0U;

	for (size_t i = 0; i < ARRAY_SIZE(configured); i++) {
		bool duplicate = false;

		for (size_t j = 0; j < count; j++) {
			if (sources[j] == configured[i]) {
				duplicate = true;
				break;
			}
		}
		if (!duplicate) {
			sources[count++] = configured[i];
		}
	}

	return count;
}

static int compass_fetch_channel(const struct device *dev, enum sensor_channel chan)
{
	int rc = sensor_sample_fetch_chan(dev, chan);

	return rc == -ENOTSUP ? sensor_sample_fetch(dev) : rc;
}

static int compass_fetch_configured_sources(const struct compass_composite_config *cfg)
{
	int rc;

	if (cfg->accel_source == cfg->gyro_source &&
	    cfg->gyro_source == cfg->magn_source) {
		return sensor_sample_fetch(cfg->accel_source);
	}

	/* MMC56x3 single-shot fetches complete the magnetic conversion before
	 * returning. Read the IMU immediately afterwards to minimize source age
	 * skew during dynamic tilt compensation. */
	rc = compass_fetch_channel(cfg->magn_source, cfg->magn_channel);
	if (rc != 0) {
		return rc;
	}

	if (cfg->accel_source == cfg->gyro_source) {
		rc = sensor_sample_fetch(cfg->accel_source);
		if (rc == -ENOTSUP) {
			rc = compass_fetch_channel(cfg->accel_source, cfg->accel_channel);
			if (rc == 0) {
				rc = compass_fetch_channel(cfg->gyro_source, cfg->gyro_channel);
			}
		}
		return rc;
	}

	rc = compass_fetch_channel(cfg->accel_source, cfg->accel_channel);
	return rc != 0 ? rc : compass_fetch_channel(cfg->gyro_source, cfg->gyro_channel);
}

static int compass_fetch_and_read_sources(const struct compass_composite_config *cfg,
					  struct compass_composite_data *data,
					  float accel[3], float gyro[3], float magn[3])
{
	const struct device *unique_devices[3] = {0};
	bool source_held[3] = {false};
	struct sensor_value accel_vals[3] = {0};
	struct sensor_value gyro_vals[3] = {0};
	struct sensor_value magn_vals[3] = {0};
	size_t unique_count = 0U;
	uint32_t startup_delay_ms = 0U;
	int rc = 0;

	unique_count = compass_unique_sources_get(cfg, unique_devices);

	k_mutex_lock(&compass_source_mutex, K_FOREVER);

	if (!data->source_lifetime_active) {
		for (size_t i = 0; i < unique_count; i++) {
			bool was_suspended;

			rc = compass_source_runtime_get(unique_devices[i], &source_held[i],
							&was_suspended);
			if (rc != 0) {
				goto out;
			}
			if (was_suspended) {
				startup_delay_ms = MAX(startup_delay_ms,
						       cfg->source_startup_delay_ms);
			}
		}
	}

	if (startup_delay_ms > 0U) {
		k_sleep(K_MSEC(startup_delay_ms));
	}

	for (size_t attempt = 0; attempt < 2U; attempt++) {
		rc = compass_fetch_configured_sources(cfg);
		if (rc != 0) {
			goto out;
		}
		rc = sensor_channel_get(cfg->magn_source, cfg->magn_channel, magn_vals);
		if (rc == 0) {
			rc = sensor_channel_get(cfg->accel_source, cfg->accel_channel, accel_vals);
		}
		if (rc == 0) {
			rc = sensor_channel_get(cfg->gyro_source, cfg->gyro_channel, gyro_vals);
		}
		if (rc != 0) {
			goto out;
		}

		compass_sensor_values_to_float3(accel_vals, accel);
		compass_sensor_values_to_float3(gyro_vals, gyro);
		compass_sensor_values_to_float3(magn_vals, magn);

		if ((cfg->source_startup_delay_ms == 0U) ||
		    ((compass_norm3(accel) >= COMPASS_EPSILON) &&
		     (compass_norm3(magn) >= COMPASS_EPSILON)) ||
		    (attempt == 1U)) {
			break;
		}

		LOG_DBG("Retrying invalid first source sample after warm-up");
		k_sleep(K_MSEC(cfg->source_startup_delay_ms));
	}
out:
	if (!data->source_lifetime_active) {
		for (size_t i = unique_count; i > 0U; i--) {
			size_t index = i - 1U;
			int end_rc = compass_source_runtime_put(unique_devices[index],
							 source_held[index],
							 cfg->source_idle_timeout_ms);

			if (rc == 0 && end_rc != 0) {
				rc = end_rc;
			}
		}
	}
	k_mutex_unlock(&compass_source_mutex);

	return rc;
}

static int compass_calculate_magnetic_heading(const float accel[3], const float magn[3],
					      float *heading_deg, float *accel_norm,
					      float *field_norm)
{
	float ax = accel[0];
	float ay = accel[1];
	float az = accel[2];
	float mx = magn[0];
	float my = magn[1];
	float mz = magn[2];
	float roll;
	float pitch;
	float xh;
	float yh;
	float heading_rad;
	float a_norm;
	float m_norm;

	a_norm = compass_norm3(accel);
	if (a_norm < COMPASS_EPSILON) {
		return -ERANGE;
	}

	ax /= a_norm;
	ay /= a_norm;
	az /= a_norm;

	m_norm = compass_norm3(magn);
	if (m_norm < COMPASS_EPSILON) {
		return -ERANGE;
	}

	roll = atan2f(ay, az);
	pitch = atan2f(-ax, sqrtf((ay * ay) + (az * az)));

	xh = (mx * cosf(pitch)) + (mz * sinf(pitch));
	yh = (mx * sinf(roll) * sinf(pitch)) + (my * cosf(roll)) - (mz * sinf(roll) * cosf(pitch));

	if ((fabsf(xh) < COMPASS_EPSILON) && (fabsf(yh) < COMPASS_EPSILON)) {
		return -ERANGE;
	}

	heading_rad = atan2f(yh, xh);
	*heading_deg = compass_wrap_degrees(heading_rad * COMPASS_DEG_PER_RAD);
	*accel_norm = a_norm;
	*field_norm = m_norm;

	return 0;
}

static void compass_update_status_locked(struct compass_composite_data *data, const float accel[3],
					 float accel_norm, float field_norm)
{
	float span_x;
	float span_y;
	float span_z;
	float min_span;
	float field_ratio = 0.0f;
	float level_ratio;
	uint8_t calibrated_axes;
	bool coverage_3d_good;
	bool coverage_2d_good;
	bool coverage_good;
	bool weak_coverage;
	bool field_stable = true;
	bool full_calibrated;
	bool level_good;
	bool xy_calibrated;

	if (!data->field_magnitude_ema_valid) {
		data->field_magnitude_ema = field_norm;
		data->field_magnitude_ema_valid = true;
	} else {
		if (data->field_magnitude_ema > COMPASS_EPSILON) {
			field_ratio = fabsf(field_norm - data->field_magnitude_ema) /
				      data->field_magnitude_ema;
		}
		if (field_ratio <= COMPASS_FIELD_INSTABILITY_RATIO_MAX) {
			data->field_magnitude_ema =
				(data->field_magnitude_ema *
				 (1.0f - COMPASS_FIELD_STABILITY_EMA_ALPHA)) +
				(field_norm * COMPASS_FIELD_STABILITY_EMA_ALPHA);
		}
	}

	span_x = data->magn_max[0] - data->magn_min[0];
	span_y = data->magn_max[1] - data->magn_min[1];
	span_z = data->magn_max[2] - data->magn_min[2];
	min_span = MIN(span_x, MIN(span_y, span_z));

	field_stable = (field_ratio <= COMPASS_FIELD_INSTABILITY_RATIO_MAX);

	if (accel_norm > COMPASS_EPSILON) {
		level_ratio = fabsf(accel[2]) / accel_norm;
	} else {
		level_ratio = 0.0f;
	}

	level_good = (level_ratio >= COMPASS_LEVEL_RATIO_MIN);
	coverage_3d_good = data->magn_window_count >= COMPASS_CAL_MIN_SAMPLES &&
			   (min_span >= COMPASS_COVERAGE_SPAN_MIN_GAUSS);
	coverage_2d_good = compass_window_level_coverage_good(data) &&
			   (span_x >= COMPASS_COVERAGE_SPAN_MIN_GAUSS) &&
			   (span_y >= COMPASS_COVERAGE_SPAN_MIN_GAUSS);
	coverage_good = coverage_3d_good || coverage_2d_good;
	weak_coverage = data->magn_window_count >= COMPASS_CAL_MIN_SAMPLES &&
			((min_span >= COMPASS_COVERAGE_SPAN_WEAK_GAUSS) ||
			 (compass_window_level_coverage_good(data) &&
			  (span_x >= COMPASS_COVERAGE_SPAN_WEAK_GAUSS) &&
			  (span_y >= COMPASS_COVERAGE_SPAN_WEAK_GAUSS)));

	calibrated_axes = compass_calibrated_axes_locked(data);
	xy_calibrated = (calibrated_axes & COMPASS_MAGN_AXIS_XY_MASK) == COMPASS_MAGN_AXIS_XY_MASK;
	full_calibrated =
		(calibrated_axes & COMPASS_MAGN_AXIS_XYZ_MASK) == COMPASS_MAGN_AXIS_XYZ_MASK;

	if ((field_norm < COMPASS_EPSILON) || (accel_norm < COMPASS_EPSILON)) {
		data->cal_hint = COMPASS_CAL_HINT_KEEP_LEVEL;
		data->accuracy = COMPASS_ACCURACY_UNRELIABLE;
		return;
	}

	if (!level_good && xy_calibrated && !full_calibrated) {
		data->cal_hint = COMPASS_CAL_HINT_KEEP_LEVEL;
		data->accuracy = COMPASS_ACCURACY_LOW;
		return;
	}

	if (!coverage_good && !compass_has_effective_calibration_locked(data, level_good)) {
		data->cal_hint = COMPASS_CAL_HINT_FIGURE_EIGHT;
		data->accuracy = weak_coverage ? COMPASS_ACCURACY_LOW : COMPASS_ACCURACY_UNRELIABLE;
		return;
	}

	if (!field_stable) {
		data->cal_hint = COMPASS_CAL_HINT_NONE;
		data->accuracy = COMPASS_ACCURACY_LOW;
		return;
	}

	data->cal_hint = COMPASS_CAL_HINT_NONE;
	data->accuracy = full_calibrated ? COMPASS_ACCURACY_HIGH : COMPASS_ACCURACY_MEDIUM;
}

static int compass_composite_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	const struct compass_composite_config *cfg = dev->config;
	struct compass_composite_data *data = dev->data;
	float accel_raw[3];
	float gyro_raw[3];
	float magn_raw[3];
	float accel[3];
	float gyro[3];
	float magn[3];
	float magn_used[3];
	float magnetic_heading_deg;
	float fused_heading_deg;
	float accel_norm;
	float field_norm_raw = 0.0f;
	float field_norm_used = 0.0f;
	float dt_seconds = 0.0f;
	float alpha = 0.0f;
	float correction_gain = 0.0f;
	float mag_trust = 0.0f;
	float prediction_heading;
	float heading_error;
	float bias_est[3] = {0.0f, 0.0f, 0.0f};
	float scale_est[3] = {1.0f, 1.0f, 1.0f};
	float magn_cal_quality = 0.0f;
	bool level_good;
	bool have_effective_cal;
	uint8_t magn_cal_axes = 0U;
	uint8_t estimated_axes = 0U;
	uint8_t manual_axes = 0U;
	uint16_t fusion_alpha_milli = 0U;
	uint32_t now;
	uint32_t elapsed_ms = 0U;
	int rc;

	if (!compass_channel_supported(chan)) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	/* Keep source fetch, channel reads, and fusion state as one coherent
	 * transaction. */
	rc = compass_fetch_and_read_sources(cfg, data, accel_raw, gyro_raw, magn_raw);
	if (rc != 0) {
		LOG_DBG("Failed to fetch source samples: %d", rc);
		goto out_unlock;
	}

	compass_apply_axis_mapping(accel_raw, cfg->accel_axis_remap, cfg->accel_axis_sign, accel);
	compass_apply_axis_mapping(gyro_raw, cfg->gyro_axis_remap, cfg->gyro_axis_sign, gyro);
	compass_apply_axis_mapping(magn_raw, cfg->magn_axis_remap, cfg->magn_axis_sign, magn);

	if (!compass_vector_is_finite(accel) || !compass_vector_is_finite(gyro) ||
	    !compass_vector_is_finite(magn)) {
		rc = -ERANGE;
		LOG_DBG("Rejected non-finite source vector");
		goto out_unlock;
	}

	accel_norm = compass_norm3(accel);
	field_norm_raw = compass_norm3(magn);
	if (!isfinite(accel_norm) || !isfinite(field_norm_raw) || accel_norm < COMPASS_EPSILON ||
	    field_norm_raw < COMPASS_EPSILON) {
		rc = -ERANGE;
		LOG_DBG("Rejected invalid source vector magnitude");
		goto out_unlock;
	}

	now = k_uptime_get_32();
	if (data->fused_heading_valid) {
		elapsed_ms = now - data->last_sample_timestamp;
	}
	level_good = (fabsf(accel[2]) / accel_norm) >= COMPASS_LEVEL_RATIO_MIN;
	if (compass_magn_window_sample_due_locked(
		    data, elapsed_ms, cfg->calibration_sample_period_ms)) {
		compass_update_magn_window_locked(data, magn, level_good);
	}

	/*
	 * Apply basic min/max-based hard/soft-iron compensation when enough coverage
	 * exists. This improves heading consistency when raw magnetometer data is
	 * biased/elliptical.
	 */
	magn_cal_axes =
		compass_compute_magn_calibration(data, bias_est, scale_est, &magn_cal_quality);
	if (magn_cal_axes != 0U) {
		bool accept_estimate = data->magn_est_valid_mask == 0U ||
				       (magn_cal_axes == COMPASS_MAGN_AXIS_XYZ_MASK &&
					data->magn_est_valid_mask != COMPASS_MAGN_AXIS_XYZ_MASK) ||
				       (magn_cal_axes == data->magn_est_valid_mask &&
					magn_cal_quality >= data->magn_est_quality);

		if (accept_estimate) {
			bool estimate_changed = data->magn_est_valid_mask != magn_cal_axes;

			for (size_t i = 0; i < 3; i++) {
				if ((magn_cal_axes & BIT(i)) != 0U) {
					estimate_changed |=
						fabsf(data->magn_bias_est[i] - bias_est[i]) >
							COMPASS_EPSILON ||
						fabsf(data->magn_scale_est[i] - scale_est[i]) >
							COMPASS_EPSILON;
					data->magn_bias_est[i] = bias_est[i];
					data->magn_scale_est[i] = scale_est[i];
				}
			}
			data->magn_est_valid_mask = magn_cal_axes;
			data->magn_est_quality = magn_cal_quality;
			if (estimate_changed) {
				data->field_magnitude_ema_valid = false;
			}
		} else {
			magn_cal_axes = 0U;
		}
	}

	for (size_t i = 0; i < 3; i++) {
		float active_bias;
		float active_scale;

		compass_active_calibration_axis_locked(data, i, &active_bias, &active_scale);
		magn_used[i] = (magn[i] - active_bias) * active_scale;
	}
	if (!compass_vector_is_finite(magn_used)) {
		rc = -ERANGE;
		LOG_DBG("Rejected invalid calibrated magnetometer vector");
		goto out_unlock;
	}

	rc = compass_calculate_magnetic_heading(accel, magn_used, &magnetic_heading_deg,
						&accel_norm, &field_norm_used);
	if (rc != 0) {
		LOG_DBG("Invalid vectors for heading calculation: %d", rc);
		goto out_unlock;
	}

	/* Evaluate stability after calibration so soft/hard-iron correction is not
	 * penalized. */
	compass_update_status_locked(data, accel, accel_norm, field_norm_used);

	/*
	 * Dynamic magnetic correction gain:
	 * - user alpha controls the base blend factor
	 * - reported accuracy controls how much we trust magnetometer right now
	 */
	alpha = (float)data->fusion_alpha_milli / 1000.0f;
	mag_trust = compass_accuracy_to_trust(data->accuracy);
	have_effective_cal = compass_has_effective_calibration_locked(data, level_good);
	if (have_effective_cal && level_good) {
		mag_trust = MAX(mag_trust, 0.3f);
	}
	if (data->fused_heading_valid) {
		float reference_retention;

		dt_seconds = (float)elapsed_ms / 1000.0f;
		if (dt_seconds > 0.5f) {
			dt_seconds = 0.5f;
		}

		/*
		 * Approximate yaw rate around the gravity axis instead of trusting only
		 * gyro Z. This is more stable when the device is slightly tilted.
		 */
		float yaw_rate = gyro[2];
		if (accel_norm > COMPASS_EPSILON) {
			float ax_n = accel[0] / accel_norm;
			float ay_n = accel[1] / accel_norm;
			float az_n = accel[2] / accel_norm;

			yaw_rate = (gyro[0] * ax_n) + (gyro[1] * ay_n) + (gyro[2] * az_n);
		}

		/* Preserve the configured 100 ms blend response at any fetch rate. */
		reference_retention = 1.0f - ((1.0f - alpha) * mag_trust);
		if (dt_seconds > 0.0f) {
			correction_gain =
				1.0f - powf(CLAMP(reference_retention, 0.0f, 1.0f),
					    dt_seconds / COMPASS_FUSION_REFERENCE_PERIOD_S);
		}

		prediction_heading =
			compass_wrap_degrees(data->fused_magnetic_heading_deg +
					     (yaw_rate * dt_seconds * COMPASS_DEG_PER_RAD));
		heading_error =
			compass_angle_diff_degrees(magnetic_heading_deg, prediction_heading);
		fused_heading_deg = compass_wrap_degrees(prediction_heading +
							 (correction_gain * heading_error));
	} else {
		fused_heading_deg = magnetic_heading_deg;
	}

	data->fused_magnetic_heading_deg = fused_heading_deg;
	data->fused_heading_valid = true;
	data->last_sample_timestamp = now;
	compass_update_heading_locked(data);

	estimated_axes = data->magn_est_valid_mask;
	fusion_alpha_milli = data->fusion_alpha_milli;
	for (size_t i = 0; i < 3; i++) {
		if (data->magn_manual_override[i]) {
			manual_axes |= BIT(i);
		}
	}
	rc = 0;

out_unlock:
	k_mutex_unlock(&data->lock);
	if (rc != 0) {
		return rc;
	}

	LOG_DBG("mag_est=0x%x updated=0x%x man=0x%x alpha=%d/1000 trust=%.2f "
		"corr_gain=%.3f "
		"dt=%.3f raw_m=%.3f used_m=%.3f",
		estimated_axes, magn_cal_axes, manual_axes, fusion_alpha_milli, (double)mag_trust,
		(double)correction_gain, (double)dt_seconds, (double)field_norm_raw,
		(double)field_norm_used);

	return 0;
}

static int compass_composite_channel_get(const struct device *dev, enum sensor_channel chan,
					 struct sensor_value *val)
{
	struct compass_composite_data *data = dev->data;
	int rc;

	if (val == NULL) {
		return -EINVAL;
	}

	if (chan != COMPASS_HEADING_CHANNEL) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	if (!data->heading_valid) {
		rc = -EAGAIN;
	} else {
		rc = sensor_value_from_double(val, (double)data->heading_deg);
	}
	k_mutex_unlock(&data->lock);

	return rc;
}

static int compass_composite_attr_set(const struct device *dev, enum sensor_channel chan,
				      enum sensor_attribute attr, const struct sensor_value *val)
{
	const struct compass_composite_config *cfg = dev->config;
	struct compass_composite_data *data = dev->data;
	int axis;
	int32_t value_mdeg;
	int64_t value_micro;
	float value_float;

	if (val == NULL) {
		return -EINVAL;
	}

	switch ((int)attr) {
	case SENSOR_ATTR_COMPASS_DECLINATION:
		if (!compass_heading_channel_supported_for_attr(chan)) {
			return -ENOTSUP;
		}
		if (compass_sensor_value_to_mdeg(val, &value_mdeg) != 0) {
			return -ERANGE;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		data->declination_mdeg = value_mdeg;
		compass_update_heading_locked(data);
		k_mutex_unlock(&data->lock);
		return 0;

	case SENSOR_ATTR_COMPASS_MOUNT_OFFSET:
		if (!compass_heading_channel_supported_for_attr(chan)) {
			return -ENOTSUP;
		}
		if (compass_sensor_value_to_mdeg(val, &value_mdeg) != 0) {
			return -ERANGE;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		data->mount_offset_mdeg = value_mdeg;
		compass_update_heading_locked(data);
		k_mutex_unlock(&data->lock);
		return 0;

	case SENSOR_ATTR_COMPASS_FUSION_ALPHA:
		if (!compass_heading_channel_supported_for_attr(chan)) {
			return -ENOTSUP;
		}
		value_micro = sensor_value_to_micro(val);
		if ((value_micro < 0) || (value_micro > 1000000LL)) {
			return -EINVAL;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		data->fusion_alpha_milli = (uint16_t)((value_micro + 500LL) / 1000LL);
		k_mutex_unlock(&data->lock);
		return 0;

	case SENSOR_ATTR_COMPASS_CAL_RESET:
		if (!compass_heading_channel_supported_for_attr(chan)) {
			return -ENOTSUP;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		compass_reset_live_calibration_locked(data);
		k_mutex_unlock(&data->lock);
		return 0;

	case SENSOR_ATTR_COMPASS_RATE_OVERRIDE:
		if (!compass_heading_channel_supported_for_attr(chan)) {
			return -ENOTSUP;
		}
		if (!cfg->imu_rate_override_supported) {
			return -ENOTSUP;
		}
		if (val->val2 != 0 || val->val1 < 0) {
			return -EINVAL;
		}
		return val->val1 == 0
			       ? compass_composite_imu_rate_restore(dev)
			       : compass_composite_imu_rate_override(dev, (uint32_t)val->val1);

	case SENSOR_ATTR_COMPASS_MAG_BIAS:
		axis = compass_magn_channel_to_axis(chan);
		if (axis < 0) {
			return axis;
		}
		value_float = (float)sensor_value_to_double(val);
		if (!isfinite(value_float)) {
			return -EINVAL;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		data->magn_bias_manual[axis] = value_float;
		if (data->magn_scale_manual[axis] <= 0.0f) {
		data->magn_scale_manual[axis] = 1.0f;
		}
		data->magn_manual_override[axis] = true;
		data->field_magnitude_ema_valid = false;
		k_mutex_unlock(&data->lock);
		return 0;

	case SENSOR_ATTR_COMPASS_MAG_SCALE:
		axis = compass_magn_channel_to_axis(chan);
		if (axis < 0) {
			return axis;
		}
		value_float = (float)sensor_value_to_double(val);
		if (!isfinite(value_float)) {
			return -EINVAL;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		if (value_float <= 0.0f) {
			data->magn_manual_override[axis] = false;
			data->magn_bias_manual[axis] = 0.0f;
			data->magn_scale_manual[axis] = 1.0f;
		} else {
			data->magn_scale_manual[axis] = value_float;
			data->magn_manual_override[axis] = true;
		}
		data->field_magnitude_ema_valid = false;
		k_mutex_unlock(&data->lock);
		return 0;

	default:
		return -ENOTSUP;
	}
}

static int compass_composite_attr_get(const struct device *dev, enum sensor_channel chan,
				      enum sensor_attribute attr, struct sensor_value *val)
{
	const struct compass_composite_config *cfg = dev->config;
	struct compass_composite_data *data = dev->data;
	int axis;
	int rc = 0;

	if (val == NULL) {
		return -EINVAL;
	}

	switch ((int)attr) {
	case SENSOR_ATTR_COMPASS_DECLINATION:
		if (!compass_heading_channel_supported_for_attr(chan)) {
			return -ENOTSUP;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		rc = sensor_value_from_micro(val, (int64_t)data->declination_mdeg * 1000LL);
		k_mutex_unlock(&data->lock);
		return rc;

	case SENSOR_ATTR_COMPASS_MOUNT_OFFSET:
		if (!compass_heading_channel_supported_for_attr(chan)) {
			return -ENOTSUP;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		rc = sensor_value_from_micro(val, (int64_t)data->mount_offset_mdeg * 1000LL);
		k_mutex_unlock(&data->lock);
		return rc;

	case SENSOR_ATTR_COMPASS_FUSION_ALPHA:
		if (!compass_heading_channel_supported_for_attr(chan)) {
			return -ENOTSUP;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		rc = sensor_value_from_micro(val, (int64_t)data->fusion_alpha_milli * 1000LL);
		k_mutex_unlock(&data->lock);
		return rc;

	case SENSOR_ATTR_COMPASS_CAL_HINT:
		if (!compass_heading_channel_supported_for_attr(chan)) {
			return -ENOTSUP;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		val->val1 = data->cal_hint;
		val->val2 = 0;
		k_mutex_unlock(&data->lock);
		return 0;

	case SENSOR_ATTR_COMPASS_ACCURACY:
		if (!compass_heading_channel_supported_for_attr(chan)) {
			return -ENOTSUP;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		val->val1 = data->accuracy;
		val->val2 = 0;
		k_mutex_unlock(&data->lock);
		return 0;

	case SENSOR_ATTR_COMPASS_CAPABILITIES:
		if (!compass_heading_channel_supported_for_attr(chan)) {
			return -ENOTSUP;
		}
		val->val1 = COMPASS_CAP_TILT_COMPENSATED | COMPASS_CAP_DECLINATION |
			    COMPASS_CAP_MOUNT_OFFSET | COMPASS_CAP_CALIBRATION |
			    COMPASS_CAP_CALIBRATION_HINT | COMPASS_CAP_ACCURACY |
			    COMPASS_CAP_RAW_CALIBRATION;
		if (cfg->imu_rate_override_supported) {
			val->val1 |= COMPASS_CAP_RATE_OVERRIDE;
		}
		val->val2 = 0;
		return 0;

	case SENSOR_ATTR_COMPASS_RATE_OVERRIDE:
		if (!compass_heading_channel_supported_for_attr(chan)) {
			return -ENOTSUP;
		}
		if (!cfg->imu_rate_override_supported) {
			return -ENOTSUP;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		val->val1 = data->imu_rate_overridden ? (int32_t)data->imu_rate_override_hz : 0;
		val->val2 = 0;
		k_mutex_unlock(&data->lock);
		return 0;

	case SENSOR_ATTR_COMPASS_MAG_BIAS:
		axis = compass_magn_channel_to_axis(chan);
		if (axis < 0) {
			return axis;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		{
			float bias;
			float scale;

			compass_active_calibration_axis_locked(data, axis, &bias, &scale);
			rc = sensor_value_from_double(val, (double)bias);
		}
		k_mutex_unlock(&data->lock);
		return rc;

	case SENSOR_ATTR_COMPASS_MAG_SCALE:
		axis = compass_magn_channel_to_axis(chan);
		if (axis < 0) {
			return axis;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		{
			float bias;
			float scale;

			compass_active_calibration_axis_locked(data, axis, &bias, &scale);
			rc = sensor_value_from_double(val, (double)scale);
		}
		k_mutex_unlock(&data->lock);
		return rc;

	case SENSOR_ATTR_COMPASS_MAG_BIAS_EST:
		axis = compass_magn_channel_to_axis(chan);
		if (axis < 0) {
			return axis;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		if ((data->magn_est_valid_mask & BIT(axis)) == 0U) {
			rc = -EAGAIN;
		} else {
			rc = sensor_value_from_double(val, (double)data->magn_bias_est[axis]);
		}
		k_mutex_unlock(&data->lock);
		return rc;

	case SENSOR_ATTR_COMPASS_MAG_SCALE_EST:
		axis = compass_magn_channel_to_axis(chan);
		if (axis < 0) {
			return axis;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		if ((data->magn_est_valid_mask & BIT(axis)) == 0U) {
			rc = -EAGAIN;
		} else {
			rc = sensor_value_from_double(val, (double)data->magn_scale_est[axis]);
		}
		k_mutex_unlock(&data->lock);
		return rc;

	default:
		return -ENOTSUP;
	}
}

static int compass_composite_pm_action(const struct device *dev, enum pm_device_action action)
{
	const struct compass_composite_config *cfg = dev->config;
	struct compass_composite_data *data = dev->data;
	bool transitioned[3] = {false};
	bool source_lifetime_held = false;
	uint32_t startup_delay_ms = 0U;
	int rc = 0;

	if (action == PM_DEVICE_ACTION_TURN_ON || action == PM_DEVICE_ACTION_TURN_OFF) {
		return 0;
	}
	if (action != PM_DEVICE_ACTION_RESUME && action != PM_DEVICE_ACTION_SUSPEND) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	k_mutex_lock(&compass_source_mutex, K_FOREVER);

	if (action == PM_DEVICE_ACTION_RESUME) {
		if (data->source_lifetime_active) {
			goto out;
		}

		data->source_lifetime_count =
			compass_unique_sources_get(cfg, data->source_lifetime_devices);
		for (size_t i = 0; i < data->source_lifetime_count; i++) {
			bool was_suspended;

			if (data->source_lifetime_held[i]) {
				continue;
			}
			rc = compass_source_runtime_get(data->source_lifetime_devices[i],
							&data->source_lifetime_held[i],
							&was_suspended);
			if (rc != 0) {
				for (size_t j = i; j > 0U; j--) {
					size_t index = j - 1U;

					if (transitioned[index] &&
					    compass_source_runtime_put(
						    data->source_lifetime_devices[index],
						    data->source_lifetime_held[index], 0U) == 0) {
						data->source_lifetime_held[index] = false;
					}
				}
				goto out;
			}
			transitioned[i] = data->source_lifetime_held[i];
			if (was_suspended) {
				startup_delay_ms = MAX(startup_delay_ms,
						       cfg->source_startup_delay_ms);
			}
		}

		if (startup_delay_ms > 0U) {
			k_sleep(K_MSEC(startup_delay_ms));
		}
		data->source_lifetime_active = true;
		goto out;
	}

	for (size_t i = 0; i < data->source_lifetime_count; i++) {
		if (data->source_lifetime_held[i]) {
			source_lifetime_held = true;
			break;
		}
	}
	if (!data->source_lifetime_active && !source_lifetime_held) {
		goto out;
	}
	for (size_t i = data->source_lifetime_count; i > 0U; i--) {
		size_t index = i - 1U;
		int put_rc;

		if (!data->source_lifetime_held[index]) {
			continue;
		}
		put_rc = compass_source_runtime_put(data->source_lifetime_devices[index],
						 data->source_lifetime_held[index], 0U);

		if (put_rc != 0 && rc == 0) {
			rc = put_rc;
		} else if (put_rc == 0) {
			data->source_lifetime_held[index] = false;
			transitioned[index] = true;
		}
	}
	if (rc != 0) {
		bool restored = true;

		for (size_t i = 0; i < data->source_lifetime_count; i++) {
			bool was_suspended;
			int get_rc;

			if (!transitioned[i]) {
				continue;
			}
			get_rc = compass_source_runtime_get(data->source_lifetime_devices[i],
							&data->source_lifetime_held[i],
							&was_suspended);
			if (get_rc != 0) {
				restored = false;
				continue;
			}
			if (was_suspended) {
				startup_delay_ms = MAX(startup_delay_ms,
						       cfg->source_startup_delay_ms);
			}
		}
		if (startup_delay_ms > 0U) {
			k_sleep(K_MSEC(startup_delay_ms));
		}
		data->source_lifetime_active = restored;
		goto out;
	}

	data->source_lifetime_active = false;
	data->source_lifetime_count = 0U;

out:
	k_mutex_unlock(&compass_source_mutex);
	k_mutex_unlock(&data->lock);
	return rc;
}

static int compass_composite_init(const struct device *dev)
{
	const struct compass_composite_config *cfg = dev->config;
	struct compass_composite_data *data = dev->data;

	if ((cfg->accel_source == NULL) || (cfg->gyro_source == NULL) ||
	    (cfg->magn_source == NULL)) {
		LOG_ERR("One or more source devices are missing");
		return -EINVAL;
	}

	if (cfg->accel_channel != SENSOR_CHAN_ACCEL_XYZ) {
		LOG_ERR("accel-channel must be SENSOR_CHAN_ACCEL_XYZ");
		return -EINVAL;
	}

	if (cfg->gyro_channel != SENSOR_CHAN_GYRO_XYZ) {
		LOG_ERR("gyro-channel must be SENSOR_CHAN_GYRO_XYZ");
		return -EINVAL;
	}

	if (cfg->magn_channel != SENSOR_CHAN_MAGN_XYZ) {
		LOG_ERR("magn-channel must be SENSOR_CHAN_MAGN_XYZ");
		return -EINVAL;
	}

	if (!compass_axis_config_valid(cfg->accel_axis_remap, cfg->accel_axis_sign)) {
		LOG_ERR("Invalid accel-axis-remap or accel-axis-sign");
		return -EINVAL;
	}

	if (!compass_axis_config_valid(cfg->gyro_axis_remap, cfg->gyro_axis_sign)) {
		LOG_ERR("Invalid gyro-axis-remap or gyro-axis-sign");
		return -EINVAL;
	}

	if (!compass_axis_config_valid(cfg->magn_axis_remap, cfg->magn_axis_sign)) {
		LOG_ERR("Invalid magn-axis-remap or magn-axis-sign");
		return -EINVAL;
	}

	if (cfg->gyro_fusion_alpha_milli > 1000U) {
		LOG_ERR("gyro-fusion-alpha-milli must be in [0, 1000]");
		return -EINVAL;
	}

	k_mutex_init(&data->lock);
	data->heading_deg = 0.0f;
	data->source_lifetime_count = 0U;
	data->source_lifetime_active = false;
	memset(data->source_lifetime_held, 0, sizeof(data->source_lifetime_held));
	data->imu_rate_override_hz = 0U;
	data->imu_rate_overridden = false;
	data->fused_magnetic_heading_deg = 0.0f;
	data->last_sample_timestamp = 0U;
	data->declination_mdeg = cfg->declination_mdeg;
	data->mount_offset_mdeg = cfg->mount_offset_mdeg;
	data->fusion_alpha_milli = cfg->gyro_fusion_alpha_milli;
	data->heading_valid = false;
	data->fused_heading_valid = false;
	data->magn_window_count = 0U;
	data->magn_window_block = 0U;
	data->magn_window_level_count = 0U;
	data->magn_window_elapsed_ms = 0U;
	data->field_magnitude_ema = 0.0f;
	data->field_magnitude_ema_valid = false;
	data->magn_est_quality = 0.0f;
	data->magn_est_valid_mask = 0U;
	data->cal_hint = COMPASS_CAL_HINT_FIGURE_EIGHT;
	data->accuracy = COMPASS_ACCURACY_UNRELIABLE;
	for (size_t block = 0; block < COMPASS_MAGN_WINDOW_BLOCK_COUNT; block++) {
		data->magn_window[block].sample_count = 0U;
		data->magn_window[block].level_count = 0U;
	}

	for (size_t i = 0; i < 3; i++) {
		data->magn_bias_manual[i] = 0.0f;
		data->magn_scale_manual[i] = 1.0f;
		data->magn_manual_override[i] = false;
		data->magn_bias_est[i] = 0.0f;
		data->magn_scale_est[i] = 1.0f;
	}

	LOG_DBG("Compass composite initialized");

	return pm_device_driver_init(dev, compass_composite_pm_action);
}

static DEVICE_API(sensor, compass_composite_api) = {
	.attr_set = compass_composite_attr_set,
	.attr_get = compass_composite_attr_get,
	.sample_fetch = compass_composite_sample_fetch,
	.channel_get = compass_composite_channel_get,
};

int compass_composite_sources_get(const struct device *dev, const struct device **sources,
				  size_t capacity)
{
	const struct compass_composite_config *cfg;
	const struct device *unique[3];
	size_t count;

	if (dev == NULL || dev->api != &compass_composite_api ||
	    (sources == NULL && capacity != 0U)) {
		return -EINVAL;
	}

	cfg = dev->config;
	count = compass_unique_sources_get(cfg, unique);
	if (sources == NULL) {
		return (int)count;
	}
	if (capacity < count) {
		return -ENOSPC;
	}

	memcpy(sources, unique, count * sizeof(*sources));
	return (int)count;
}

int compass_composite_imu_rate_override(const struct device *dev, uint32_t frequency_hz)
{
	const struct compass_composite_config *cfg;
	struct compass_composite_data *data;
	struct sensor_value target;
	int rc;

	if (dev == NULL || dev->api != &compass_composite_api || frequency_hz == 0U ||
	    frequency_hz > INT32_MAX) {
		return -EINVAL;
	}

	cfg = dev->config;
	if (!cfg->imu_rate_override_supported) {
		return -ENOTSUP;
	}
	data = dev->data;
	target = (struct sensor_value){.val1 = (int32_t)frequency_hz};

	k_mutex_lock(&data->lock, K_FOREVER);
	if (data->imu_rate_overridden) {
		rc = data->imu_rate_override_hz == frequency_hz ? 0 : -EBUSY;
		goto out;
	}

	rc = sensor_attr_get(cfg->accel_source, cfg->accel_channel,
			     SENSOR_ATTR_SAMPLING_FREQUENCY, &data->saved_accel_rate);
	if (rc != 0) {
		goto out;
	}
	rc = sensor_attr_get(cfg->gyro_source, cfg->gyro_channel,
			     SENSOR_ATTR_SAMPLING_FREQUENCY, &data->saved_gyro_rate);
	if (rc != 0) {
		goto out;
	}

	rc = sensor_attr_set(cfg->accel_source, cfg->accel_channel,
			     SENSOR_ATTR_SAMPLING_FREQUENCY, &target);
	if (rc != 0) {
		goto out;
	}
	rc = sensor_attr_set(cfg->gyro_source, cfg->gyro_channel,
			     SENSOR_ATTR_SAMPLING_FREQUENCY, &target);
	if (rc != 0) {
		int rollback_rc = sensor_attr_set(cfg->accel_source, cfg->accel_channel,
						  SENSOR_ATTR_SAMPLING_FREQUENCY,
						  &data->saved_accel_rate);

		if (rollback_rc != 0) {
			LOG_ERR("Failed to roll back accel sampling frequency: %d", rollback_rc);
		}
		goto out;
	}

	data->imu_rate_override_hz = frequency_hz;
	data->imu_rate_overridden = true;

out:
	k_mutex_unlock(&data->lock);
	return rc;
}

int compass_composite_imu_rate_restore(const struct device *dev)
{
	const struct compass_composite_config *cfg;
	struct compass_composite_data *data;
	struct sensor_value override;
	int rc;

	if (dev == NULL || dev->api != &compass_composite_api) {
		return -EINVAL;
	}

	cfg = dev->config;
	if (!cfg->imu_rate_override_supported) {
		return 0;
	}
	data = dev->data;
	k_mutex_lock(&data->lock, K_FOREVER);
	if (!data->imu_rate_overridden) {
		rc = 0;
		goto out;
	}

	override = (struct sensor_value){.val1 = (int32_t)data->imu_rate_override_hz};
	rc = sensor_attr_set(cfg->accel_source, cfg->accel_channel,
			     SENSOR_ATTR_SAMPLING_FREQUENCY, &data->saved_accel_rate);
	if (rc != 0) {
		goto out;
	}
	rc = sensor_attr_set(cfg->gyro_source, cfg->gyro_channel,
			     SENSOR_ATTR_SAMPLING_FREQUENCY, &data->saved_gyro_rate);
	if (rc != 0) {
		int rollback_rc = sensor_attr_set(cfg->accel_source, cfg->accel_channel,
						  SENSOR_ATTR_SAMPLING_FREQUENCY, &override);

		if (rollback_rc != 0) {
			LOG_ERR("Failed to restore accel override after gyro error: %d", rollback_rc);
		}
		goto out;
	}

	data->imu_rate_override_hz = 0U;
	data->imu_rate_overridden = false;

out:
	k_mutex_unlock(&data->lock);
	return rc;
}

#define COMPASS_COMPOSITE_INST_ASSERT(inst)                                                        \
	BUILD_ASSERT(DT_INST_PROP(inst, gyro_fusion_alpha_milli) <= 1000,                         \
		     "gyro-fusion-alpha-milli must be <= 1000");                                  \
	BUILD_ASSERT(DT_INST_PROP(inst, source_startup_delay_ms) >= 0,                             \
		     "source-startup-delay-ms must be >= 0");                                    \
	BUILD_ASSERT(DT_INST_PROP(inst, source_idle_timeout_ms) >= 0,                              \
		     "source-idle-timeout-ms must be >= 0");                                    \
	BUILD_ASSERT(DT_INST_PROP(inst, calibration_sample_period_ms) >= 0 &&                       \
		     DT_INST_PROP(inst, calibration_sample_period_ms) <= UINT16_MAX,                 \
		     "calibration-sample-period-ms must fit uint16_t")

#define COMPASS_COMPOSITE_GYRO_AXIS_REMAP(inst, idx)                                               \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, gyro_axis_remap),                                  \
		    (DT_INST_PROP_BY_IDX(inst, gyro_axis_remap, idx)),                             \
		    (DT_INST_PROP_BY_IDX(inst, accel_axis_remap, idx)))

#define COMPASS_COMPOSITE_GYRO_AXIS_SIGN(inst, idx)                                                \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, gyro_axis_sign),                                   \
		    (DT_INST_PROP_BY_IDX(inst, gyro_axis_sign, idx)),                              \
		    (DT_INST_PROP_BY_IDX(inst, accel_axis_sign, idx)))

#define COMPASS_COMPOSITE_INIT(inst)                                                               \
	COMPASS_COMPOSITE_INST_ASSERT(inst);                                                       \
	static struct compass_composite_data compass_composite_data_##inst;                         \
	PM_DEVICE_DT_INST_DEFINE(inst, compass_composite_pm_action);                                \
	static const struct compass_composite_config compass_composite_config_##inst = {            \
		.accel_source = DEVICE_DT_GET(DT_INST_PROP(inst, accel_source)),                    \
		.gyro_source = DEVICE_DT_GET(DT_INST_PROP(inst, gyro_source)),                      \
		.magn_source = DEVICE_DT_GET(DT_INST_PROP(inst, magn_source)),                      \
		.accel_channel = (enum sensor_channel)DT_INST_PROP(inst, accel_channel),            \
		.gyro_channel = (enum sensor_channel)DT_INST_PROP(inst, gyro_channel),              \
		.magn_channel = (enum sensor_channel)DT_INST_PROP(inst, magn_channel),              \
		.source_startup_delay_ms = DT_INST_PROP(inst, source_startup_delay_ms),              \
		.source_idle_timeout_ms = DT_INST_PROP(inst, source_idle_timeout_ms),                \
		.calibration_sample_period_ms =                                                     \
			DT_INST_PROP(inst, calibration_sample_period_ms),                              \
		.accel_axis_remap = {                                                                  \
			DT_INST_PROP_BY_IDX(inst, accel_axis_remap, 0),                                 \
			DT_INST_PROP_BY_IDX(inst, accel_axis_remap, 1),                                 \
			DT_INST_PROP_BY_IDX(inst, accel_axis_remap, 2),                                 \
		},                                                                                 \
		.accel_axis_sign = {                                                                 \
			DT_INST_PROP_BY_IDX(inst, accel_axis_sign, 0),                                \
			DT_INST_PROP_BY_IDX(inst, accel_axis_sign, 1),                                \
			DT_INST_PROP_BY_IDX(inst, accel_axis_sign, 2),                                \
		},                                                                                 \
		.gyro_axis_remap = {                                                                  \
			COMPASS_COMPOSITE_GYRO_AXIS_REMAP(inst, 0),                                     \
			COMPASS_COMPOSITE_GYRO_AXIS_REMAP(inst, 1),                                     \
			COMPASS_COMPOSITE_GYRO_AXIS_REMAP(inst, 2),                                     \
		},                                                                                 \
		.gyro_axis_sign = {                                                                   \
			COMPASS_COMPOSITE_GYRO_AXIS_SIGN(inst, 0),                                      \
			COMPASS_COMPOSITE_GYRO_AXIS_SIGN(inst, 1),                                      \
			COMPASS_COMPOSITE_GYRO_AXIS_SIGN(inst, 2),                                      \
		},                                                                                 \
		.magn_axis_remap = {                                                                   \
			DT_INST_PROP_BY_IDX(inst, magn_axis_remap, 0),                                  \
			DT_INST_PROP_BY_IDX(inst, magn_axis_remap, 1),                                  \
			DT_INST_PROP_BY_IDX(inst, magn_axis_remap, 2),                                  \
		},                                                                                 \
		.magn_axis_sign = {                                                                  \
			DT_INST_PROP_BY_IDX(inst, magn_axis_sign, 0),                                 \
			DT_INST_PROP_BY_IDX(inst, magn_axis_sign, 1),                                 \
			DT_INST_PROP_BY_IDX(inst, magn_axis_sign, 2),                                 \
		},                                                                                 \
		.declination_mdeg = DT_INST_PROP(inst, declination_milli_deg),                    \
		.mount_offset_mdeg = DT_INST_PROP(inst, mount_offset_milli_deg),                  \
		.gyro_fusion_alpha_milli = DT_INST_PROP(inst, gyro_fusion_alpha_milli),           \
		.imu_rate_override_supported =                                                   \
			DT_INST_PROP(inst, imu_rate_override_supported),                            \
	};                                                                                         \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, compass_composite_init, PM_DEVICE_DT_INST_GET(inst),      \
				       &compass_composite_data_##inst,                                \
				       &compass_composite_config_##inst,                              \
				       POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,                      \
				       &compass_composite_api)

DT_INST_FOREACH_STATUS_OKAY(COMPASS_COMPOSITE_INIT)
