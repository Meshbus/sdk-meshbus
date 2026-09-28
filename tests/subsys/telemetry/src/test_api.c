/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <power/power.h>
#include <telemetry/telemetry.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

#include "telemetry_dt.h"

BUILD_ASSERT(MBS_TELEMETRY_DT_SENSOR_IS_COMPASS_OWNED(
		     DT_NODELABEL(fake_sensor), DT_NODELABEL(fake_compass_overlap)),
	     "composite Compass source overlap must be detected");
BUILD_ASSERT(!MBS_TELEMETRY_DT_SENSOR_IS_COMPASS_OWNED(
		     DT_NODELABEL(test_power_domain), DT_NODELABEL(fake_compass_overlap)),
	     "an unrelated device must not be classified as Compass-owned");
BUILD_ASSERT(!MBS_TELEMETRY_DT_CHANNEL_IS_COMMON(DT_NODELABEL(invalid_private_channel)),
	     "driver-private Telemetry channels must be rejected");

static const struct device *const test_power_domain =
	DEVICE_DT_GET(DT_NODELABEL(test_power_domain));
static const struct device *const fake_sensor = DEVICE_DT_GET(DT_NODELABEL(fake_sensor));

#define TELEMETRY_SETTINGS_SUBTREE "meshbus/telemetry"
#define TELEMETRY_CONFIG_KEY TELEMETRY_SETTINGS_SUBTREE "/config"

static atomic_t block_config_delete;
K_SEM_DEFINE(config_delete_entered, 0, 1);
K_SEM_DEFINE(config_delete_release, 0, 1);

int __real_settings_delete(const char *name);

int __wrap_settings_delete(const char *name)
{
	if (name != NULL && strcmp(name, TELEMETRY_CONFIG_KEY) == 0 &&
	    atomic_cas(&block_config_delete, 1, 0)) {
		k_sem_give(&config_delete_entered);
		(void)k_sem_take(&config_delete_release, K_FOREVER);
	}

	return __real_settings_delete(name);
}

static void telemetry_test_power_action_publish(enum mbs_power_action action)
{
	STRUCT_SECTION_FOREACH(mbs_power_action_callback, callback) {
		if (callback->callback != NULL) {
			callback->callback(action, callback->user_data);
		}
	}
}

void fake_sensor_pm_counts_reset(void);
atomic_val_t fake_sensor_pm_resume_count(void);
atomic_val_t fake_sensor_pm_suspend_count(void);
void fake_sensor_pm_suspend_error_set(int error);
void fake_sensor_io_counts_reset(void);
atomic_val_t fake_sensor_sample_fetch_count(void);
atomic_val_t fake_sensor_channel_get_count(enum sensor_channel chan);
atomic_val_t fake_second_sensor_fetch_count(void);
atomic_val_t fake_second_sensor_get_count(enum sensor_channel chan);
void fake_sensor_sample_fetch_block(void);
void fake_sensor_reject_all_fetch(bool reject);
int fake_sensor_sample_fetch_wait_entered(k_timeout_t timeout);
void fake_sensor_sample_fetch_release(void);

K_THREAD_STACK_DEFINE(shutdown_release_stack, 1024);
static struct k_thread shutdown_release_thread;
K_THREAD_STACK_DEFINE(config_reset_stack, 1536);
K_THREAD_STACK_DEFINE(config_set_stack, 1536);
static struct k_thread config_reset_thread;
static struct k_thread config_set_thread;
static mbs_telemetry_config concurrent_set_cfg;
static atomic_t concurrent_set_done;
static int concurrent_reset_rc;
static int concurrent_set_rc;

static void shutdown_release_thread_fn(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	k_sleep(K_MSEC(50));
	fake_sensor_sample_fetch_release();
}

static void config_reset_thread_fn(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	concurrent_reset_rc = mbs_telemetry_config_reset();
}

static void config_set_thread_fn(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	concurrent_set_rc = mbs_telemetry_config_set(&concurrent_set_cfg);
	atomic_set(&concurrent_set_done, 1);
}

static mbs_telemetry_config valid_telemetry_config(bool enabled)
{
	mbs_telemetry_config cfg = meshbus_TelemetryConfig_init_zero;

	cfg.enabled = enabled;
	cfg.sample_interval = CONFIG_MBS_TELEMETRY_DEFAULT_SAMPLE_INTERVAL;
	return cfg;
}

static void *telemetry_suite_setup(void)
{
	enum pm_device_state state;

	zassert_true(device_is_ready(test_power_domain));
	zassert_true(device_is_ready(fake_sensor));
	zassert_ok(pm_device_state_get(test_power_domain, &state));
	zassert_equal(state, PM_DEVICE_STATE_ACTIVE);
	zassert_ok(mbs_telemetry_config_reset());
	return NULL;
}

static void telemetry_before(void *fixture)
{
	ARG_UNUSED(fixture);
	zassert_ok(mbs_telemetry_config_reset());
}

static void telemetry_suite_teardown(void *fixture)
{
	mbs_telemetry_config cfg = valid_telemetry_config(false);
	enum pm_device_state state;
	atomic_val_t fetch_count;
	int pd_usage_before_shutdown;

	ARG_UNUSED(fixture);

	/*
	 * Hold one fetch inside the system-workqueue worker while shutdown begins.
	 * The callback must drain the I/O before releasing PM and the worker must
	 * not install a fresh deadline after it unblocks.
	 */
	zassert_ok(mbs_telemetry_config_reset());
	zassert_true(settings_get_val_len(TELEMETRY_CONFIG_KEY) <= 0,
		     "config fixture was not cleared before shutdown persistence test");
	zassert_ok(mbs_telemetry_config_set(&cfg));
	fake_sensor_io_counts_reset();
	fake_sensor_sample_fetch_block();
	cfg.enabled = true;
	cfg.sample_interval = CONFIG_MBS_TELEMETRY_MIN_SAMPLE_INTERVAL;
	zassert_ok(mbs_telemetry_config_set(&cfg));
	zassert_ok(mbs_telemetry_sample_trigger());
	zassert_ok(fake_sensor_sample_fetch_wait_entered(K_SECONDS(1)),
		   "sample fetch did not enter");

	k_tid_t tid = k_thread_create(&shutdown_release_thread, shutdown_release_stack,
				    K_THREAD_STACK_SIZEOF(shutdown_release_stack),
				    shutdown_release_thread_fn, NULL, NULL, NULL,
				    K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
	zassert_not_null(tid);
	pd_usage_before_shutdown = pm_device_runtime_usage(test_power_domain);
	zassert_true(pd_usage_before_shutdown > 0);
	fake_sensor_pm_suspend_error_set(-EIO);
	telemetry_test_power_action_publish(MBS_POWER_ACTION_SHUTDOWN);
	zassert_ok(k_thread_join(&shutdown_release_thread, K_SECONDS(1)));
	zassert_true(settings_get_val_len(TELEMETRY_CONFIG_KEY) > 0,
		     "shutdown did not synchronously persist the pending config");

	fetch_count = fake_sensor_sample_fetch_count();
	zassert_equal(fetch_count, 1, "unexpected fetch count while draining shutdown");
	k_sleep(K_MSEC(CONFIG_MBS_TELEMETRY_MIN_SAMPLE_INTERVAL + 100));
	zassert_equal(fake_sensor_sample_fetch_count(), fetch_count,
		      "telemetry worker rescheduled after shutdown");
	zassert_equal(mbs_telemetry_sample_trigger(), -ESHUTDOWN);

	zassert_ok(pm_device_state_get(fake_sensor, &state));
	zassert_equal(state, PM_DEVICE_STATE_ACTIVE,
		      "provider lease was lost after a failed runtime put");
	zassert_ok(pm_device_state_get(test_power_domain, &state));
	zassert_equal(state, PM_DEVICE_STATE_ACTIVE,
		      "power-domain was released while its provider lease remained active");
	zassert_equal(pm_device_runtime_usage(test_power_domain), pd_usage_before_shutdown,
		      "early power-domain hold was dropped before provider cleanup succeeded");

	fake_sensor_pm_suspend_error_set(0);
	telemetry_test_power_action_publish(MBS_POWER_ACTION_SHUTDOWN);
	zassert_ok(pm_device_state_get(fake_sensor, &state));
	zassert_equal(state, PM_DEVICE_STATE_SUSPENDED);
	zassert_ok(pm_device_state_get(test_power_domain, &state));
	zassert_equal(state, PM_DEVICE_STATE_SUSPENDED);
	zassert_equal(pm_device_runtime_usage(test_power_domain), 0);
}

ZTEST(mbs_telemetry_contract, test_channel_value_count_boundaries)
{
	zassert_equal(mbs_telemetry_channel_value_count(SENSOR_CHAN_AMBIENT_TEMP), 1);
	zassert_equal(mbs_telemetry_channel_value_count(SENSOR_CHAN_ACCEL_XYZ), 3);
	zassert_equal(mbs_telemetry_channel_value_count(SENSOR_CHAN_GAME_ROTATION_VECTOR), 4);
	zassert_equal(mbs_telemetry_channel_value_count((enum sensor_channel)-1), 1);
}

ZTEST(mbs_telemetry_contract, test_power_domain_hold_is_independent_of_enabled_state)
{
	mbs_telemetry_config cfg = valid_telemetry_config(false);
	enum pm_device_state state;

	zassert_ok(mbs_telemetry_config_set(&cfg));
	zassert_ok(pm_device_state_get(test_power_domain, &state));
	zassert_equal(state, PM_DEVICE_STATE_ACTIVE);

	cfg.enabled = true;
	zassert_ok(mbs_telemetry_config_set(&cfg));
	zassert_ok(pm_device_state_get(test_power_domain, &state));
	zassert_equal(state, PM_DEVICE_STATE_ACTIVE);
}

ZTEST(mbs_telemetry_contract, test_config_defaults_set_get_reset_and_validation)
{
	mbs_telemetry_config cfg = valid_telemetry_config(true);
	mbs_telemetry_config got;

	zassert_equal(mbs_telemetry_config_get(NULL), -EINVAL);
	zassert_ok(mbs_telemetry_config_get(&got));
	zassert_true(got.enabled);
	zassert_equal(got.sample_interval, CONFIG_MBS_TELEMETRY_DEFAULT_SAMPLE_INTERVAL);

	zassert_ok(mbs_telemetry_config_set(&cfg));
	zassert_ok(mbs_telemetry_config_get(&got));
	zassert_true(got.enabled);
	zassert_equal(got.sample_interval, cfg.sample_interval);

	cfg.sample_interval = CONFIG_MBS_TELEMETRY_MIN_SAMPLE_INTERVAL - 1;
	zassert_equal(mbs_telemetry_config_set(&cfg), -EINVAL);
}

struct stored_telemetry_config {
	uint8_t bytes[128];
	size_t size;
};

static int stored_config_read(const char *key, size_t len, settings_read_cb read_cb,
			      void *cb_arg, void *param)
{
	struct stored_telemetry_config *stored = param;

	if (strcmp(key, "config") != 0) {
		return 0;
	}
	zassert_true(len <= sizeof(stored->bytes));
	zassert_equal(read_cb(cb_arg, stored->bytes, len), len);
	stored->size = len;
	return 0;
}

ZTEST(mbs_telemetry_contract, test_config_restores_through_registered_settings_handler)
{
	mbs_telemetry_config cfg = valid_telemetry_config(false);
	mbs_telemetry_config got;
	struct stored_telemetry_config stored = {0};

	cfg.sample_interval = CONFIG_MBS_TELEMETRY_MIN_SAMPLE_INTERVAL + 123U;
	zassert_ok(mbs_telemetry_config_set(&cfg));
	zassert_ok(settings_save_subtree(TELEMETRY_SETTINGS_SUBTREE));
	zassert_ok(settings_load_subtree_direct(TELEMETRY_SETTINGS_SUBTREE,
					       stored_config_read, &stored));
	zassert_true(stored.size > 0U);

	zassert_ok(mbs_telemetry_config_reset());
	zassert_ok(settings_save_one(TELEMETRY_CONFIG_KEY, stored.bytes, stored.size));
	zassert_ok(settings_load_subtree(TELEMETRY_SETTINGS_SUBTREE));
	zassert_ok(mbs_telemetry_config_get(&got));
	zassert_equal(got.enabled, cfg.enabled);
	zassert_equal(got.sample_interval, cfg.sample_interval);
}

ZTEST(mbs_telemetry_contract, test_reset_serializes_concurrent_set)
{
	mbs_telemetry_config got;
	k_tid_t reset_tid;
	k_tid_t set_tid;

	concurrent_set_cfg = valid_telemetry_config(false);
	concurrent_set_cfg.sample_interval =
		CONFIG_MBS_TELEMETRY_MIN_SAMPLE_INTERVAL + 123U;
	concurrent_reset_rc = -EINPROGRESS;
	concurrent_set_rc = -EINPROGRESS;
	atomic_clear(&concurrent_set_done);
	atomic_set(&block_config_delete, 1);
	k_sem_reset(&config_delete_entered);
	k_sem_reset(&config_delete_release);

	reset_tid = k_thread_create(&config_reset_thread, config_reset_stack,
				    K_THREAD_STACK_SIZEOF(config_reset_stack),
				    config_reset_thread_fn, NULL, NULL, NULL,
				    K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
	zassert_not_null(reset_tid);
	zassert_ok(k_sem_take(&config_delete_entered, K_SECONDS(1)),
		   "reset did not reach the persisted-config delete");

	set_tid = k_thread_create(&config_set_thread, config_set_stack,
				  K_THREAD_STACK_SIZEOF(config_set_stack),
				  config_set_thread_fn, NULL, NULL, NULL,
				  K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
	zassert_not_null(set_tid);
	k_sleep(K_MSEC(20));
	zassert_equal(atomic_get(&concurrent_set_done), 0,
		      "config_set escaped while reset's delete transaction was open");

	k_sem_give(&config_delete_release);
	zassert_ok(k_thread_join(&config_reset_thread, K_SECONDS(1)));
	zassert_ok(k_thread_join(&config_set_thread, K_SECONDS(1)));
	zassert_ok(concurrent_reset_rc);
	zassert_ok(concurrent_set_rc);
	zassert_ok(mbs_telemetry_config_get(&got));
	zassert_equal(got.enabled, concurrent_set_cfg.enabled);
	zassert_equal(got.sample_interval, concurrent_set_cfg.sample_interval);
}

ZTEST(mbs_telemetry_contract, test_sample_trigger_and_binding_boundaries)
{
	mbs_telemetry_config cfg = valid_telemetry_config(true);
	mbs_telemetry_config disabled_cfg = valid_telemetry_config(false);
	struct mbs_telemetry_binding binding;
	struct sensor_value value;

	zassert_ok(mbs_telemetry_config_set(&disabled_cfg));
	zassert_equal(mbs_telemetry_sample_trigger(), -ENODEV);
	zassert_equal(mbs_telemetry_bindings_count(), 3);
	zassert_equal(mbs_telemetry_binding_get(0, NULL), -EINVAL);
	zassert_ok(mbs_telemetry_binding_get(0, &binding));
	zassert_equal(binding.chan, SENSOR_CHAN_AMBIENT_TEMP);
	zassert_not_null(binding.sensor_name);
	zassert_ok(mbs_telemetry_binding_get(1, &binding));
	zassert_equal(binding.chan, SENSOR_CHAN_ACCEL_XYZ);
	zassert_ok(mbs_telemetry_binding_get(2, &binding));
	zassert_equal(binding.chan, SENSOR_CHAN_PRESS);
	zassert_equal(mbs_telemetry_binding_get(3, &binding), -ENOENT);
	zassert_equal(mbs_telemetry_channel_get(SENSOR_CHAN_AMBIENT_TEMP, NULL), -EINVAL);
	zassert_equal(mbs_telemetry_channel_get((enum sensor_channel)-1, &value), -EINVAL);
	zassert_equal(mbs_telemetry_channel_get(SENSOR_CHAN_PRIV_START, &value), -EINVAL);
	zassert_equal(mbs_telemetry_channel_get(SENSOR_CHAN_AMBIENT_TEMP, &value), -ENODEV);

	zassert_ok(mbs_telemetry_config_set(&cfg));
	zassert_ok(mbs_telemetry_sample_trigger());
	zassert_ok(mbs_telemetry_channel_get(SENSOR_CHAN_AMBIENT_TEMP, &value));
	zassert_equal(value.val1, SENSOR_CHAN_AMBIENT_TEMP);
}

static struct mbs_telemetry_data_event sampled_events[3];
static size_t sampled_event_count;
K_SEM_DEFINE(sampled_event_ready, 0, ARRAY_SIZE(sampled_events));

static void sample_listener_cb(const struct zbus_channel *chan)
{
	if (sampled_event_count < ARRAY_SIZE(sampled_events)) {
		sampled_events[sampled_event_count++] =
			*(const struct mbs_telemetry_data_event *)zbus_chan_const_msg(chan);
		k_sem_give(&sampled_event_ready);
	}
}

ZBUS_LISTENER_DEFINE(sample_listener, sample_listener_cb);

ZTEST(mbs_telemetry_contract, test_providers_fetch_once_and_keep_binding_order)
{
	mbs_telemetry_config cfg = valid_telemetry_config(false);

	zassert_ok(mbs_telemetry_config_set(&cfg));
	fake_sensor_io_counts_reset();
	sampled_event_count = 0;
	k_sem_reset(&sampled_event_ready);
	zassert_ok(zbus_chan_add_obs(&mbs_telemetry_data_chan, &sample_listener,
				    K_MSEC(100)));
	cfg.enabled = true;
	cfg.sample_interval = 60000U;
	zassert_ok(mbs_telemetry_config_set(&cfg));
	zassert_ok(mbs_telemetry_sample_trigger());

	for (size_t i = 0; i < ARRAY_SIZE(sampled_events); i++) {
		zassert_ok(k_sem_take(&sampled_event_ready, K_SECONDS(1)));
	}

	cfg.enabled = false;
	zassert_ok(mbs_telemetry_config_set(&cfg));
	zassert_ok(zbus_chan_rm_obs(&mbs_telemetry_data_chan, &sample_listener,
				   K_MSEC(100)));
	zassert_equal(fake_sensor_sample_fetch_count(), 1,
		      "shared provider was fetched more than once");
	zassert_equal(fake_sensor_channel_get_count(SENSOR_CHAN_AMBIENT_TEMP), 1);
	zassert_equal(fake_sensor_channel_get_count(SENSOR_CHAN_PRESS), 1);
	zassert_equal(fake_second_sensor_fetch_count(), 1);
	zassert_equal(fake_second_sensor_get_count(SENSOR_CHAN_ACCEL_XYZ), 1);
	zassert_equal(fake_second_sensor_get_count(SENSOR_CHAN_AMBIENT_TEMP), 0,
		      "duplicate channel must retain the first provider");
	/* Provider order, then that provider's binding order (interleaved in DT). */
	zassert_equal(sampled_events[0].chan, SENSOR_CHAN_AMBIENT_TEMP);
	zassert_equal(sampled_events[0].values[0].val1, SENSOR_CHAN_AMBIENT_TEMP);
	zassert_equal(sampled_events[1].chan, SENSOR_CHAN_PRESS);
	zassert_equal(sampled_events[2].chan, SENSOR_CHAN_ACCEL_XYZ);
	zassert_equal(sampled_events[2].value_count, 3);
	for (size_t i = 0; i < 3; i++) {
		zassert_equal(sampled_events[2].values[i].val1, 100 + i);
		zassert_equal(sampled_events[2].values[i].val2, 2000);
	}
}

ZTEST(mbs_telemetry_contract, test_channel_scoped_fetch_fallback)
{
	mbs_telemetry_config cfg = valid_telemetry_config(false);
	struct sensor_value value;

	zassert_ok(mbs_telemetry_config_set(&cfg));
	fake_sensor_io_counts_reset();
	fake_sensor_reject_all_fetch(true);
	cfg.enabled = true;
	cfg.sample_interval = 60000U;
	zassert_ok(mbs_telemetry_config_set(&cfg));
	zassert_ok(mbs_telemetry_sample_trigger());

	for (size_t i = 0; i < 100U; i++) {
		if (fake_sensor_channel_get_count(SENSOR_CHAN_PRESS) != 0) {
			break;
		}
		k_sleep(K_MSEC(10));
	}

	cfg.enabled = false;
	zassert_ok(mbs_telemetry_config_set(&cfg));
	zassert_equal(fake_sensor_sample_fetch_count(), 3,
		      "expected one ALL probe and one fetch per bound channel");
	zassert_equal(fake_sensor_channel_get_count(SENSOR_CHAN_AMBIENT_TEMP), 1);
	zassert_equal(fake_sensor_channel_get_count(SENSOR_CHAN_PRESS), 1);

	fake_sensor_io_counts_reset();
	fake_sensor_reject_all_fetch(true);
	cfg.enabled = true;
	zassert_ok(mbs_telemetry_config_set(&cfg));
	zassert_ok(mbs_telemetry_channel_get(SENSOR_CHAN_AMBIENT_TEMP, &value));
	zassert_equal(fake_sensor_sample_fetch_count(), 2,
		      "synchronous read did not fall back to channel fetch");
	zassert_equal(fake_sensor_channel_get_count(SENSOR_CHAN_AMBIENT_TEMP), 1);
}

ZTEST(mbs_telemetry_contract, test_static_binding_holds_sensor_across_reads)
{
	mbs_telemetry_config cfg = valid_telemetry_config(true);
	struct sensor_value value;
	enum pm_device_state state;

	zassert_ok(mbs_telemetry_config_set(&cfg));
	fake_sensor_pm_counts_reset();

	zassert_ok(mbs_telemetry_channel_get(SENSOR_CHAN_AMBIENT_TEMP, &value));
	zassert_equal(fake_sensor_pm_resume_count(), 0);
	zassert_equal(fake_sensor_pm_suspend_count(), 0);
	zassert_ok(pm_device_state_get(fake_sensor, &state));
	zassert_equal(state, PM_DEVICE_STATE_ACTIVE);

	cfg.enabled = false;
	zassert_ok(mbs_telemetry_config_set(&cfg));
	zassert_ok(pm_device_state_get(fake_sensor, &state));
	zassert_equal(state, PM_DEVICE_STATE_ACTIVE);
}

ZTEST(mbs_telemetry_contract, test_public_data_channel_validator)
{
	struct mbs_telemetry_data_event event = {
		.timestamp = 1234,
		.chan = SENSOR_CHAN_AMBIENT_TEMP,
		.value_count = 1,
		.values = {
			{ .val1 = 22, .val2 = 500000 },
		},
	};
	struct mbs_telemetry_data_event got;

	zassert_ok(zbus_chan_pub(&mbs_telemetry_data_chan, &event, K_NO_WAIT));
	zassert_ok(zbus_chan_read(&mbs_telemetry_data_chan, &got, K_NO_WAIT));
	zassert_equal(got.timestamp, event.timestamp);
	zassert_equal(got.chan, event.chan);
	zassert_equal(got.value_count, 1);
	zassert_equal(got.values[0].val1, 22);

	event.chan = (enum sensor_channel)-1;
	zassert_equal(zbus_chan_pub(&mbs_telemetry_data_chan, &event, K_NO_WAIT), -ENOMSG);
	event.chan = SENSOR_CHAN_PRIV_START;
	zassert_equal(zbus_chan_pub(&mbs_telemetry_data_chan, &event, K_NO_WAIT), -ENOMSG);
	event.chan = SENSOR_CHAN_HUMIDITY;
	zassert_equal(zbus_chan_pub(&mbs_telemetry_data_chan, &event, K_NO_WAIT), -ENOMSG,
		      "unbound common channel was accepted");

	event.chan = SENSOR_CHAN_AMBIENT_TEMP;
	event.value_count = 0U;
	zassert_equal(zbus_chan_pub(&mbs_telemetry_data_chan, &event, K_NO_WAIT), -ENOMSG);
	event.value_count = 2U;
	zassert_equal(zbus_chan_pub(&mbs_telemetry_data_chan, &event, K_NO_WAIT), -ENOMSG);
	event.value_count = MBS_TELEMETRY_MAX_VALUES + 1U;
	zassert_equal(zbus_chan_pub(&mbs_telemetry_data_chan, &event, K_NO_WAIT), -ENOMSG);
}

ZTEST_SUITE(mbs_telemetry_contract, NULL, telemetry_suite_setup, telemetry_before, NULL,
	    telemetry_suite_teardown);
