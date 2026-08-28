// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#define DT_DRV_COMPAT vnd_fake_sensor

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/atomic.h>

static atomic_t pm_resume_count;
static atomic_t pm_suspend_count;
static atomic_t sample_fetch_count;
static atomic_t ambient_temp_get_count;
static atomic_t press_get_count;
static atomic_t pm_suspend_error;
static atomic_t block_sample_fetch;
static atomic_t reject_all_sample_fetch;
K_SEM_DEFINE(sample_fetch_entered, 0, 1);
K_SEM_DEFINE(sample_fetch_release, 0, 1);

void fake_sensor_pm_counts_reset(void)
{
	atomic_set(&pm_resume_count, 0);
	atomic_set(&pm_suspend_count, 0);
}

atomic_val_t fake_sensor_pm_resume_count(void)
{
	return atomic_get(&pm_resume_count);
}

atomic_val_t fake_sensor_pm_suspend_count(void)
{
	return atomic_get(&pm_suspend_count);
}

void fake_sensor_pm_suspend_error_set(int error)
{
	atomic_set(&pm_suspend_error, error);
}

void fake_sensor_io_counts_reset(void)
{
	atomic_set(&sample_fetch_count, 0);
	atomic_set(&ambient_temp_get_count, 0);
	atomic_set(&press_get_count, 0);
	atomic_clear(&reject_all_sample_fetch);
}

atomic_val_t fake_sensor_sample_fetch_count(void)
{
	return atomic_get(&sample_fetch_count);
}

atomic_val_t fake_sensor_channel_get_count(enum sensor_channel chan)
{
	switch (chan) {
	case SENSOR_CHAN_AMBIENT_TEMP:
		return atomic_get(&ambient_temp_get_count);
	case SENSOR_CHAN_PRESS:
		return atomic_get(&press_get_count);
	default:
		return 0;
	}
}

void fake_sensor_sample_fetch_block(void)
{
	k_sem_reset(&sample_fetch_entered);
	k_sem_reset(&sample_fetch_release);
	atomic_set(&block_sample_fetch, 1);
}

void fake_sensor_reject_all_fetch(bool reject)
{
	atomic_set(&reject_all_sample_fetch, reject ? 1 : 0);
}

int fake_sensor_sample_fetch_wait_entered(k_timeout_t timeout)
{
	return k_sem_take(&sample_fetch_entered, timeout);
}

void fake_sensor_sample_fetch_release(void)
{
	atomic_clear(&block_sample_fetch);
	k_sem_give(&sample_fetch_release);
}

static int fake_sensor_pm_action(const struct device *dev, enum pm_device_action action)
{
	ARG_UNUSED(dev);

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		atomic_inc(&pm_resume_count);
		return 0;
	case PM_DEVICE_ACTION_SUSPEND:
		if (atomic_get(&pm_suspend_error) != 0) {
			return (int)atomic_get(&pm_suspend_error);
		}
		atomic_inc(&pm_suspend_count);
		return 0;
	case PM_DEVICE_ACTION_TURN_ON:
	case PM_DEVICE_ACTION_TURN_OFF:
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int fake_sensor_init(const struct device *dev)
{
	return pm_device_driver_init(dev, fake_sensor_pm_action);
}

static int fake_sensor_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	ARG_UNUSED(dev);
	atomic_inc(&sample_fetch_count);
	if (chan == SENSOR_CHAN_ALL && atomic_get(&reject_all_sample_fetch) != 0) {
		return -ENOTSUP;
	}
	if (atomic_get(&block_sample_fetch) != 0) {
		k_sem_give(&sample_fetch_entered);
		(void)k_sem_take(&sample_fetch_release, K_FOREVER);
	}
	return 0;
}

static int fake_sensor_channel_get(const struct device *dev, enum sensor_channel chan,
				   struct sensor_value *val)
{
	ARG_UNUSED(dev);

	if (val == NULL) {
		return -EINVAL;
	}
	if (chan == SENSOR_CHAN_AMBIENT_TEMP) {
		atomic_inc(&ambient_temp_get_count);
	} else if (chan == SENSOR_CHAN_PRESS) {
		atomic_inc(&press_get_count);
	}

	val->val1 = (int32_t)chan;
	val->val2 = 1000;
	return 0;
}

static DEVICE_API(sensor, fake_sensor_api) = {
	.sample_fetch = fake_sensor_sample_fetch,
	.channel_get = fake_sensor_channel_get,
};

#define FAKE_SENSOR_DEFINE(inst)                                                                   \
	PM_DEVICE_DT_INST_DEFINE(inst, fake_sensor_pm_action);                                      \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, fake_sensor_init, PM_DEVICE_DT_INST_GET(inst), NULL,     \
				     NULL, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,                 \
				     &fake_sensor_api);

DT_INST_FOREACH_STATUS_OKAY(FAKE_SENSOR_DEFINE)
