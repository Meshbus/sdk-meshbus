/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT fobe_compass_heading_sensor

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <drivers/sensor/compass.h>
#include <zephyr/kernel.h>
#include <gnss/gnss.h>
#include <power/power.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#include "gnss_internal.h"

#define FAKE_HEADING_NODE DT_NODELABEL(fake_heading)
#define COMPASS_HEADING_CHANNEL ((enum sensor_channel)SENSOR_CHAN_COMPASS_HEADING)

struct fake_heading_data {
	atomic_t fetch_count;
	atomic_t resume_count;
	atomic_t suspend_count;
	atomic_t calibration_reset_count;
	atomic_t block_fetch;
};

K_SEM_DEFINE(fetch_entered, 0, 1);
K_SEM_DEFINE(fetch_release, 0, 1);
K_SEM_DEFINE(power_action_done, 0, 1);
K_THREAD_STACK_DEFINE(power_action_stack, 2048);
static struct k_thread power_action_thread;

static const struct device *const fake_heading = DEVICE_DT_GET(FAKE_HEADING_NODE);

static int fake_heading_pm_action(const struct device *dev, enum pm_device_action action)
{
	struct fake_heading_data *data = dev->data;

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		atomic_inc(&data->resume_count);
		return 0;
	case PM_DEVICE_ACTION_SUSPEND:
		atomic_inc(&data->suspend_count);
		return 0;
	case PM_DEVICE_ACTION_TURN_ON:
	case PM_DEVICE_ACTION_TURN_OFF:
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int fake_heading_init(const struct device *dev)
{
	return pm_device_driver_init(dev, fake_heading_pm_action);
}

static int fake_heading_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct fake_heading_data *data = dev->data;

	if (chan != SENSOR_CHAN_ALL && chan != COMPASS_HEADING_CHANNEL) {
		return -ENOTSUP;
	}
	atomic_inc(&data->fetch_count);
	if (atomic_get(&data->block_fetch) != 0) {
		k_sem_give(&fetch_entered);
		(void)k_sem_take(&fetch_release, K_FOREVER);
	}
	return 0;
}

static int fake_heading_channel_get(const struct device *dev, enum sensor_channel chan,
				    struct sensor_value *value)
{
	ARG_UNUSED(dev);
	if (chan != COMPASS_HEADING_CHANNEL || value == NULL) {
		return -ENOTSUP;
	}
	*value = (struct sensor_value){.val1 = 123, .val2 = 456000};
	return 0;
}

static int fake_heading_attr_set(const struct device *dev, enum sensor_channel chan,
				 enum sensor_attribute attr,
				 const struct sensor_value *value)
{
	struct fake_heading_data *data = dev->data;

	if (chan != COMPASS_HEADING_CHANNEL || value == NULL) {
		return -ENOTSUP;
	}
	if ((int)attr == SENSOR_ATTR_COMPASS_CAL_RESET) {
		atomic_inc(&data->calibration_reset_count);
		return 0;
	}
	/* A direct provider may use a fixed native output data rate. */
	if ((int)attr == SENSOR_ATTR_COMPASS_RATE_OVERRIDE) {
		return -ENOTSUP;
	}
	return -ENOTSUP;
}

static int fake_heading_attr_get(const struct device *dev, enum sensor_channel chan,
				 enum sensor_attribute attr, struct sensor_value *value)
{
	ARG_UNUSED(dev);
	if (chan != COMPASS_HEADING_CHANNEL || value == NULL) {
		return -ENOTSUP;
	}

	value->val2 = 0;
	switch ((int)attr) {
	case SENSOR_ATTR_COMPASS_CAPABILITIES:
		value->val1 = COMPASS_CAP_CALIBRATION | COMPASS_CAP_ACCURACY;
		return 0;
	case SENSOR_ATTR_COMPASS_ACCURACY:
		value->val1 = COMPASS_ACCURACY_MEDIUM;
		return 0;
	case SENSOR_ATTR_COMPASS_CAL_HINT:
		value->val1 = COMPASS_CAL_HINT_NONE;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static DEVICE_API(sensor, fake_heading_api) = {
	.attr_set = fake_heading_attr_set,
	.attr_get = fake_heading_attr_get,
	.sample_fetch = fake_heading_sample_fetch,
	.channel_get = fake_heading_channel_get,
};

static struct fake_heading_data fake_heading_data;
PM_DEVICE_DT_DEFINE(FAKE_HEADING_NODE, fake_heading_pm_action);
SENSOR_DEVICE_DT_DEFINE(FAKE_HEADING_NODE, fake_heading_init,
			PM_DEVICE_DT_GET(FAKE_HEADING_NODE), &fake_heading_data, NULL,
			POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, &fake_heading_api);

static void release_all(void)
{
	struct mbs_gnss_heading_runtime_status status;

	zassert_ok(mbs_gnss_heading_runtime_status_get(&status));
	while (status.active_client_count > 0U) {
		zassert_ok(mbs_gnss_heading_release());
		zassert_ok(mbs_gnss_heading_runtime_status_get(&status));
	}
}

static void wait_for_valid_snapshot(struct mbs_gnss_heading_snapshot *snapshot)
{
	for (size_t attempt = 0; attempt < 50U; attempt++) {
		zassert_ok(mbs_gnss_heading_snapshot_get(snapshot));
		if (snapshot->valid) {
			return;
		}
		k_sleep(K_MSEC(10));
	}
	zassert_unreachable("Direct provider sample did not complete");
}

static void *direct_setup(void)
{
	zassert_true(device_is_ready(fake_heading));
	release_all();
	return NULL;
}

static void direct_before(void *fixture)
{
	ARG_UNUSED(fixture);
	release_all();
	atomic_set(&fake_heading_data.fetch_count, 0);
	atomic_set(&fake_heading_data.resume_count, 0);
	atomic_set(&fake_heading_data.suspend_count, 0);
	atomic_set(&fake_heading_data.calibration_reset_count, 0);
	atomic_set(&fake_heading_data.block_fetch, 0);
	k_sem_reset(&fetch_entered);
	k_sem_reset(&fetch_release);
	k_sem_reset(&power_action_done);
}

static void power_action_publish_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	STRUCT_SECTION_FOREACH(mbs_power_action_callback, callback) {
		callback->callback(MBS_POWER_ACTION_SHUTDOWN, callback->user_data);
	}
	k_sem_give(&power_action_done);
}

ZTEST(mbs_gnss_heading_direct_contract, test_direct_heading_provider_uses_common_api)
{
	struct mbs_gnss_heading_runtime_status status;
	struct mbs_gnss_heading_snapshot snapshot;

	zassert_ok(mbs_gnss_heading_runtime_status_get(&status));
	zassert_false(status.active);

	zassert_ok(mbs_gnss_heading_acquire());
	zassert_ok(mbs_gnss_heading_runtime_status_get(&status));
	zassert_equal(status.capabilities,
		      COMPASS_CAP_CALIBRATION | COMPASS_CAP_ACCURACY);
	wait_for_valid_snapshot(&snapshot);
	zassert_equal(snapshot.source, MBS_GNSS_HEADING_SOURCE_ELECTRONIC);
	zassert_equal(snapshot.state, MBS_GNSS_HEADING_STATE_READY);
	zassert_equal(snapshot.accuracy, MBS_GNSS_HEADING_ACCURACY_MEDIUM);
	zassert_equal(snapshot.heading_milli_deg, 123456);
	zassert_true(atomic_get(&fake_heading_data.fetch_count) >= 1);
	zassert_equal(pm_device_runtime_usage(fake_heading), 1);

	zassert_ok(mbs_gnss_heading_release());
	zassert_equal(pm_device_runtime_usage(fake_heading), 0);
}

ZTEST(mbs_gnss_heading_direct_contract, test_direct_calibration_reset_is_temporary)
{
	zassert_ok(mbs_gnss_heading_calibration_reset());
	for (size_t attempt = 0;
	     attempt < 10U && atomic_get(&fake_heading_data.suspend_count) == 0;
	     attempt++) {
		k_sleep(K_MSEC(1));
	}
	zassert_equal(atomic_get(&fake_heading_data.calibration_reset_count), 1);
	zassert_true(atomic_get(&fake_heading_data.resume_count) >= 1);
	zassert_true(atomic_get(&fake_heading_data.suspend_count) >= 1);
	zassert_equal(pm_device_runtime_usage(fake_heading), 0);
}

/* Terminal by design: keep this lexically last because power_quiescing is irreversible. */
ZTEST(mbs_gnss_heading_direct_contract, test_zz_shutdown_waits_for_inflight_provider_io)
{
	struct mbs_gnss_heading_runtime_status status;
	mbs_gnss_config cfg;
	int fetch_count;

	atomic_set(&fake_heading_data.block_fetch, 1);
	zassert_ok(mbs_gnss_heading_acquire());
	zassert_ok(k_sem_take(&fetch_entered, K_SECONDS(1)));

	k_thread_create(&power_action_thread, power_action_stack,
			K_THREAD_STACK_SIZEOF(power_action_stack),
			power_action_publish_thread, NULL, NULL, NULL,
			K_PRIO_PREEMPT(0), 0, K_NO_WAIT);
	k_sleep(K_MSEC(10));
	zassert_equal(k_sem_take(&power_action_done, K_NO_WAIT), -EBUSY);
	zassert_equal(pm_device_runtime_usage(fake_heading), 1);

	k_sem_give(&fetch_release);
	zassert_ok(k_sem_take(&power_action_done, K_SECONDS(1)));
	zassert_ok(k_thread_join(&power_action_thread, K_SECONDS(1)));
	zassert_ok(mbs_gnss_heading_runtime_status_get(&status));
	zassert_false(status.active);
	zassert_equal(status.active_client_count, 0U);
	zassert_equal(pm_device_runtime_usage(fake_heading), 0);

	atomic_set(&fake_heading_data.block_fetch, 0);
	fetch_count = atomic_get(&fake_heading_data.fetch_count);
	k_sleep(K_MSEC(CONFIG_MBS_GNSS_HEADING_SAMPLE_INTERVAL * 2U));
	zassert_equal(atomic_get(&fake_heading_data.fetch_count), fetch_count);

	zassert_equal(mbs_gnss_heading_acquire(), -ESHUTDOWN);
	zassert_equal(mbs_gnss_heading_calibration_reset(), -ESHUTDOWN);
	zassert_ok(mbs_gnss_config_get(&cfg));
	cfg.has_electronic_compass = true;
	cfg.electronic_compass = false;
	zassert_equal(mbs_gnss_config_set_full(&cfg), -ESHUTDOWN);
	zassert_equal(pm_device_runtime_usage(fake_heading), 0);
}

ZTEST_SUITE(mbs_gnss_heading_direct_contract, NULL, direct_setup, direct_before, NULL, NULL);
