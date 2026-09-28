/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT fobe_compass_vector_sensor

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <drivers/sensor/compass_composite.h>
#include <zephyr/kernel.h>
#include <gnss/gnss.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#define FAKE_IMU_NODE  DT_NODELABEL(fake_imu)
#define FAKE_MAGN_NODE DT_NODELABEL(fake_magn)
#define COMPASS_NODE   DT_NODELABEL(compass)

enum fake_vector_role {
	FAKE_VECTOR_ROLE_IMU,
	FAKE_VECTOR_ROLE_MAGNETOMETER,
};

struct fake_vector_config {
	enum fake_vector_role role;
};

struct fake_vector_data {
	atomic_t fetch_count;
	atomic_t resume_count;
	atomic_t suspend_count;
	atomic_t fetch_error;
	atomic_t gyro_attr_set_error;
	atomic_t resume_error;
	atomic_t suspend_error;
	atomic_t fail_resume_after_suspend;
	struct sensor_value accel_rate;
	struct sensor_value gyro_rate;
};

static const struct device *const fake_imu = DEVICE_DT_GET(FAKE_IMU_NODE);
static const struct device *const fake_magn = DEVICE_DT_GET(FAKE_MAGN_NODE);
static const struct device *const compass = DEVICE_DT_GET(COMPASS_NODE);

static int fake_vector_pm_action(const struct device *dev, enum pm_device_action action)
{
	struct fake_vector_data *data = dev->data;

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		if (atomic_get(&data->resume_error) != 0) {
			return (int)atomic_get(&data->resume_error);
		}
		atomic_inc(&data->resume_count);
		return 0;
	case PM_DEVICE_ACTION_SUSPEND:
		if (atomic_get(&data->suspend_error) != 0) {
			return (int)atomic_get(&data->suspend_error);
		}
		atomic_inc(&data->suspend_count);
		if (atomic_cas(&data->fail_resume_after_suspend, 1, 0)) {
			atomic_set(&data->resume_error, -EIO);
		}
		return 0;
	case PM_DEVICE_ACTION_TURN_ON:
	case PM_DEVICE_ACTION_TURN_OFF:
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int fake_vector_init(const struct device *dev)
{
	struct fake_vector_data *data = dev->data;

	data->accel_rate = (struct sensor_value){.val1 = 12, .val2 = 500000};
	data->gyro_rate = data->accel_rate;
	return pm_device_driver_init(dev, fake_vector_pm_action);
}

static int fake_vector_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct fake_vector_data *data = dev->data;

	ARG_UNUSED(chan);
	atomic_inc(&data->fetch_count);
	return (int)atomic_get(&data->fetch_error);
}

static int fake_vector_channel_get(const struct device *dev, enum sensor_channel chan,
				   struct sensor_value *val)
{
	const struct fake_vector_config *cfg = dev->config;

	if (val == NULL) {
		return -EINVAL;
	}

	for (size_t i = 0; i < 3U; i++) {
		val[i] = (struct sensor_value){0};
	}
	if (cfg->role == FAKE_VECTOR_ROLE_IMU && chan == SENSOR_CHAN_ACCEL_XYZ) {
		val[2].val1 = 1;
		return 0;
	}
	if (cfg->role == FAKE_VECTOR_ROLE_IMU && chan == SENSOR_CHAN_GYRO_XYZ) {
		return 0;
	}
	if (cfg->role == FAKE_VECTOR_ROLE_MAGNETOMETER && chan == SENSOR_CHAN_MAGN_XYZ) {
		val[0].val1 = 1;
		return 0;
	}
	return -ENOTSUP;
}

static int fake_vector_attr_set(const struct device *dev, enum sensor_channel chan,
				enum sensor_attribute attr, const struct sensor_value *val)
{
	const struct fake_vector_config *cfg = dev->config;
	struct fake_vector_data *data = dev->data;

	if (val == NULL || cfg->role != FAKE_VECTOR_ROLE_IMU ||
	    attr != SENSOR_ATTR_SAMPLING_FREQUENCY) {
		return -ENOTSUP;
	}
	if (chan == SENSOR_CHAN_ACCEL_XYZ) {
		data->accel_rate = *val;
		return 0;
	}
	if (chan == SENSOR_CHAN_GYRO_XYZ) {
		if (atomic_get(&data->gyro_attr_set_error) != 0) {
			return (int)atomic_get(&data->gyro_attr_set_error);
		}
		data->gyro_rate = *val;
		return 0;
	}
	return -ENOTSUP;
}

static int fake_vector_attr_get(const struct device *dev, enum sensor_channel chan,
				enum sensor_attribute attr, struct sensor_value *val)
{
	const struct fake_vector_config *cfg = dev->config;
	struct fake_vector_data *data = dev->data;

	if (val == NULL || cfg->role != FAKE_VECTOR_ROLE_IMU ||
	    attr != SENSOR_ATTR_SAMPLING_FREQUENCY) {
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

static DEVICE_API(sensor, fake_vector_api) = {
	.sample_fetch = fake_vector_sample_fetch,
	.channel_get = fake_vector_channel_get,
	.attr_set = fake_vector_attr_set,
	.attr_get = fake_vector_attr_get,
};

#define FAKE_VECTOR_DEFINE(inst)                                                         \
	static struct fake_vector_data fake_vector_data_##inst;                           \
	static const struct fake_vector_config fake_vector_config_##inst = {              \
		.role = DT_INST_ENUM_IDX(inst, role),                                        \
	};                                                                                 \
	PM_DEVICE_DT_INST_DEFINE(inst, fake_vector_pm_action);                              \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, fake_vector_init, PM_DEVICE_DT_INST_GET(inst),   \
				     &fake_vector_data_##inst, &fake_vector_config_##inst,    \
				     POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, &fake_vector_api);

DT_INST_FOREACH_STATUS_OKAY(FAKE_VECTOR_DEFINE)

static struct fake_vector_data *fake_data(const struct device *dev)
{
	return dev->data;
}

static void fake_counters_reset(const struct device *dev)
{
	struct fake_vector_data *data = fake_data(dev);

	atomic_set(&data->fetch_count, 0);
	atomic_set(&data->resume_count, 0);
	atomic_set(&data->suspend_count, 0);
	atomic_set(&data->fetch_error, 0);
	atomic_set(&data->gyro_attr_set_error, 0);
	atomic_set(&data->resume_error, 0);
	atomic_set(&data->suspend_error, 0);
	atomic_set(&data->fail_resume_after_suspend, 0);
	data->accel_rate = (struct sensor_value){.val1 = 12, .val2 = 500000};
	data->gyro_rate = data->accel_rate;
}

static void wait_for_sequence_after(uint32_t sequence,
				    struct mbs_gnss_heading_snapshot *snapshot)
{
	for (size_t attempt = 0; attempt < 50U; attempt++) {
		zassert_ok(mbs_gnss_heading_snapshot_get(snapshot));
		if (snapshot->sequence > sequence &&
		    snapshot->state != MBS_GNSS_HEADING_STATE_STARTING) {
			return;
		}
		k_sleep(K_MSEC(10));
	}
	zassert_unreachable("Compass sample did not complete");
}

static void compass_release_all(void)
{
	struct mbs_gnss_heading_runtime_status status;

	zassert_ok(mbs_gnss_heading_runtime_status_get(&status));
	while (status.active_client_count > 0U) {
		zassert_ok(mbs_gnss_heading_release());
		zassert_ok(mbs_gnss_heading_runtime_status_get(&status));
	}
}

static void *compass_setup(void)
{
	zassert_true(device_is_ready(fake_imu));
	zassert_true(device_is_ready(fake_magn));
	zassert_true(device_is_ready(compass));
	compass_release_all();
	return NULL;
}

static void compass_before(void *fixture)
{
	mbs_gnss_config cfg;

	ARG_UNUSED(fixture);
	atomic_set(&fake_data(fake_imu)->gyro_attr_set_error, 0);
	atomic_set(&fake_data(fake_imu)->resume_error, 0);
	atomic_set(&fake_data(fake_imu)->suspend_error, 0);
	atomic_set(&fake_data(fake_magn)->resume_error, 0);
	atomic_set(&fake_data(fake_magn)->suspend_error, 0);
	compass_release_all();
	zassert_ok(mbs_gnss_config_get(&cfg));
	cfg.enabled = false;
	cfg.has_electronic_compass = true;
	cfg.electronic_compass = true;
	zassert_ok(mbs_gnss_config_set(&cfg));
	fake_counters_reset(fake_imu);
	fake_counters_reset(fake_magn);
}

ZTEST(mbs_gnss_heading_contract, test_composite_sources_are_unique)
{
	const struct device *sources[3];

	zassert_equal(compass_composite_sources_get(compass, NULL, 0U), 2);
	zassert_equal(compass_composite_sources_get(compass, sources, ARRAY_SIZE(sources)), 2);
	zassert_equal_ptr(sources[0], fake_imu);
	zassert_equal_ptr(sources[1], fake_magn);
}

ZTEST(mbs_gnss_heading_contract, test_idle_service_does_not_poll_or_power_provider)
{
	struct mbs_gnss_heading_runtime_status status;
	struct mbs_gnss_heading_snapshot before;
	struct mbs_gnss_heading_snapshot after;

	zassert_ok(mbs_gnss_heading_runtime_status_get(&status));
	zassert_false(status.active);
	zassert_equal(status.active_client_count, 0U);
	zassert_equal(status.sample_interval_ms, CONFIG_MBS_GNSS_HEADING_SAMPLE_INTERVAL);
	zassert_ok(mbs_gnss_heading_snapshot_get(&before));
	zassert_equal(before.source, MBS_GNSS_HEADING_SOURCE_ELECTRONIC);
	k_sleep(K_MSEC(CONFIG_MBS_GNSS_HEADING_SAMPLE_INTERVAL * 3U));
	zassert_ok(mbs_gnss_heading_snapshot_get(&after));
	zassert_equal(after.sequence, before.sequence);
	zassert_equal(after.state, MBS_GNSS_HEADING_STATE_IDLE);
	zassert_false(after.valid);
	zassert_equal(atomic_get(&fake_data(fake_imu)->fetch_count), 0);
	zassert_equal(atomic_get(&fake_data(fake_magn)->fetch_count), 0);
	zassert_equal(pm_device_runtime_usage(fake_imu), 0);
	zassert_equal(pm_device_runtime_usage(fake_magn), 0);
	zassert_equal(mbs_gnss_heading_release(), -EALREADY);
}

ZTEST(mbs_gnss_heading_contract, test_page_lease_is_refcounted_and_restores_provider)
{
	struct mbs_gnss_heading_runtime_status status;
	struct mbs_gnss_heading_snapshot before;
	struct mbs_gnss_heading_snapshot after;
	struct sensor_value rate;

	zassert_ok(mbs_gnss_heading_snapshot_get(&before));
	zassert_ok(mbs_gnss_heading_acquire());
	zassert_ok(mbs_gnss_heading_runtime_status_get(&status));
	zassert_true(status.active);
	zassert_equal(status.active_client_count, 1U);
	zassert_true((status.capabilities & COMPASS_CAP_RATE_OVERRIDE) != 0U);
	zassert_equal(pm_device_runtime_usage(fake_imu), 1);
	zassert_equal(pm_device_runtime_usage(fake_magn), 1);
	zassert_ok(sensor_attr_get(fake_imu, SENSOR_CHAN_ACCEL_XYZ,
				   SENSOR_ATTR_SAMPLING_FREQUENCY, &rate));
	zassert_equal(rate.val1, CONFIG_MBS_GNSS_HEADING_PROVIDER_FAST_FREQUENCY);
	zassert_ok(sensor_attr_get(fake_imu, SENSOR_CHAN_GYRO_XYZ,
				   SENSOR_ATTR_SAMPLING_FREQUENCY, &rate));
	zassert_equal(rate.val1, CONFIG_MBS_GNSS_HEADING_PROVIDER_FAST_FREQUENCY);

	wait_for_sequence_after(before.sequence, &after);
	zassert_true(after.valid);
	zassert_equal(after.state, MBS_GNSS_HEADING_STATE_READY);
	zassert_equal(after.last_error, 0);
	zassert_true(after.source_timestamp_ms > 0U);
	zassert_within(after.heading_milli_deg, 0, 1);
	zassert_equal(after.source, MBS_GNSS_HEADING_SOURCE_ELECTRONIC);

	zassert_ok(mbs_gnss_heading_acquire());
	zassert_ok(mbs_gnss_heading_runtime_status_get(&status));
	zassert_equal(status.active_client_count, 2U);
	zassert_equal(atomic_get(&fake_data(fake_imu)->resume_count), 1);
	zassert_equal(atomic_get(&fake_data(fake_magn)->resume_count), 1);
	zassert_ok(mbs_gnss_heading_release());
	zassert_ok(mbs_gnss_heading_runtime_status_get(&status));
	zassert_true(status.active);
	zassert_equal(status.active_client_count, 1U);

	zassert_ok(mbs_gnss_heading_release());
	zassert_ok(mbs_gnss_heading_runtime_status_get(&status));
	zassert_false(status.active);
	zassert_equal(status.active_client_count, 0U);
	zassert_equal(pm_device_runtime_usage(fake_imu), 0);
	zassert_equal(pm_device_runtime_usage(fake_magn), 0);
	zassert_ok(sensor_attr_get(fake_imu, SENSOR_CHAN_ACCEL_XYZ,
				   SENSOR_ATTR_SAMPLING_FREQUENCY, &rate));
	zassert_equal(rate.val1, 12);
	zassert_equal(rate.val2, 500000);
	zassert_ok(sensor_attr_get(fake_imu, SENSOR_CHAN_GYRO_XYZ,
				   SENSOR_ATTR_SAMPLING_FREQUENCY, &rate));
	zassert_equal(rate.val1, 12);
	zassert_equal(rate.val2, 500000);
	zassert_ok(mbs_gnss_heading_snapshot_get(&after));
	zassert_equal(after.state, MBS_GNSS_HEADING_STATE_IDLE);
	zassert_false(after.valid);
}

ZTEST(mbs_gnss_heading_contract, test_fetch_error_commits_new_invalid_snapshot)
{
	struct mbs_gnss_heading_snapshot before;
	struct mbs_gnss_heading_snapshot after;

	zassert_ok(mbs_gnss_heading_acquire());
	zassert_ok(mbs_gnss_heading_snapshot_get(&before));
	atomic_set(&fake_data(fake_magn)->fetch_error, -EIO);
	wait_for_sequence_after(before.sequence, &after);
	zassert_false(after.valid);
	zassert_equal(after.state, MBS_GNSS_HEADING_STATE_ERROR);
	zassert_equal(after.last_error, -EIO);
}

ZTEST(mbs_gnss_heading_contract, test_source_change_waits_for_widget_release)
{
	mbs_gnss_config cfg;
	struct mbs_gnss_heading_snapshot snapshot;

	zassert_ok(mbs_gnss_heading_acquire());
	zassert_ok(mbs_gnss_config_get(&cfg));
	cfg.has_electronic_compass = true;
	cfg.electronic_compass = false;
	zassert_equal(mbs_gnss_config_set(&cfg), -EBUSY);
	zassert_ok(mbs_gnss_heading_snapshot_get(&snapshot));
	zassert_equal(snapshot.source, MBS_GNSS_HEADING_SOURCE_ELECTRONIC);

	zassert_ok(mbs_gnss_heading_release());
	zassert_ok(mbs_gnss_config_set(&cfg));
	zassert_ok(mbs_gnss_heading_snapshot_get(&snapshot));
	zassert_equal(snapshot.source, MBS_GNSS_HEADING_SOURCE_COURSE);
}

ZTEST(mbs_gnss_heading_contract, test_active_runtime_uses_fixed_cadence)
{
	struct mbs_gnss_heading_snapshot before;
	struct mbs_gnss_heading_snapshot after;

	zassert_ok(mbs_gnss_heading_snapshot_get(&before));
	zassert_ok(mbs_gnss_heading_acquire());
	k_sleep(K_MSEC(CONFIG_MBS_GNSS_HEADING_SAMPLE_INTERVAL * 4U + 10U));
	zassert_ok(mbs_gnss_heading_snapshot_get(&after));
	zassert_true(after.sequence >= before.sequence + 3U,
		     "samples before=%u after=%u", before.sequence, after.sequence);
}

ZTEST(mbs_gnss_heading_contract, test_profile_failure_rolls_back_power_and_imu_rate)
{
	struct mbs_gnss_heading_runtime_status status;
	struct sensor_value rate;

	atomic_set(&fake_data(fake_imu)->gyro_attr_set_error, -EIO);
	zassert_equal(mbs_gnss_heading_acquire(), -EIO);
	zassert_ok(mbs_gnss_heading_runtime_status_get(&status));
	zassert_false(status.active);
	zassert_equal(status.active_client_count, 0U);
	zassert_equal(pm_device_runtime_usage(fake_imu), 0);
	zassert_equal(pm_device_runtime_usage(fake_magn), 0);
	zassert_ok(sensor_attr_get(fake_imu, SENSOR_CHAN_ACCEL_XYZ,
				   SENSOR_ATTR_SAMPLING_FREQUENCY, &rate));
	zassert_equal(rate.val1, 12);
	zassert_equal(rate.val2, 500000);
}

ZTEST(mbs_gnss_heading_contract, test_provider_resume_failure_rolls_back_first_source)
{
	struct mbs_gnss_heading_runtime_status status;

	atomic_set(&fake_data(fake_magn)->resume_error, -EIO);
	zassert_equal(mbs_gnss_heading_acquire(), -EIO);
	zassert_ok(mbs_gnss_heading_runtime_status_get(&status));
	zassert_false(status.active);
	zassert_equal(status.active_client_count, 0U);
	zassert_equal(pm_device_runtime_usage(fake_imu), 0);
	zassert_equal(pm_device_runtime_usage(fake_magn), 0);
}

ZTEST(mbs_gnss_heading_contract, test_suspend_failure_keeps_last_lease_retryable)
{
	struct mbs_gnss_heading_runtime_status status;
	struct mbs_gnss_heading_snapshot snapshot;

	zassert_ok(mbs_gnss_heading_acquire());
	atomic_set(&fake_data(fake_magn)->suspend_error, -EIO);
	zassert_equal(mbs_gnss_heading_release(), -EIO);

	zassert_ok(mbs_gnss_heading_runtime_status_get(&status));
	zassert_true(status.active);
	zassert_equal(status.active_client_count, 1U);
	zassert_equal(pm_device_runtime_usage(fake_imu), 1);
	zassert_equal(pm_device_runtime_usage(fake_magn), 1);
	zassert_ok(mbs_gnss_heading_snapshot_get(&snapshot));
	zassert_equal(snapshot.source, MBS_GNSS_HEADING_SOURCE_ELECTRONIC);
	zassert_equal(snapshot.state, MBS_GNSS_HEADING_STATE_ERROR);
	zassert_equal(snapshot.last_error, -EIO);

	atomic_set(&fake_data(fake_magn)->suspend_error, 0);
	zassert_ok(mbs_gnss_heading_release());
	zassert_ok(mbs_gnss_heading_runtime_status_get(&status));
	zassert_false(status.active);
	zassert_equal(status.active_client_count, 0U);
	zassert_equal(pm_device_runtime_usage(fake_imu), 0);
	zassert_equal(pm_device_runtime_usage(fake_magn), 0);
}

ZTEST(mbs_gnss_heading_contract, test_calibration_reset_temporarily_powers_provider)
{
	struct mbs_gnss_heading_runtime_status status;

	zassert_ok(mbs_gnss_heading_calibration_reset());
	zassert_ok(mbs_gnss_heading_runtime_status_get(&status));
	zassert_false(status.active);
	zassert_equal(pm_device_runtime_usage(fake_imu), 0);
	zassert_equal(pm_device_runtime_usage(fake_magn), 0);
	zassert_equal(atomic_get(&fake_data(fake_imu)->resume_count), 1);
	zassert_equal(atomic_get(&fake_data(fake_magn)->resume_count), 1);
	zassert_equal(atomic_get(&fake_data(fake_imu)->fetch_count), 0);
	zassert_equal(atomic_get(&fake_data(fake_magn)->fetch_count), 0);
}

ZTEST(mbs_gnss_heading_contract, test_course_acquire_triggers_gnss_acquisition)
{
	struct mbs_gnss_heading_runtime_status status;
	mbs_gnss_config cfg;

	zassert_ok(mbs_gnss_config_get(&cfg));
	cfg.has_electronic_compass = true;
	cfg.electronic_compass = false;
	zassert_ok(mbs_gnss_config_set(&cfg));

	/* A disabled GNSS error is propagated instead of creating a dead lease. */
	zassert_equal(mbs_gnss_heading_acquire(), -EACCES);
	zassert_ok(mbs_gnss_heading_runtime_status_get(&status));
	zassert_false(status.active);

	cfg.enabled = true;
	zassert_ok(mbs_gnss_config_set(&cfg));
	zassert_ok(mbs_gnss_heading_acquire());
	zassert_ok(mbs_gnss_heading_runtime_status_get(&status));
	zassert_true(status.active);
	zassert_equal(status.active_client_count, 1U);
	zassert_ok(mbs_gnss_heading_release());

	cfg.enabled = false;
	cfg.electronic_compass = true;
	zassert_ok(mbs_gnss_config_set(&cfg));
}

ZTEST_SUITE(mbs_gnss_heading_contract, NULL, compass_setup, compass_before, NULL, NULL);
