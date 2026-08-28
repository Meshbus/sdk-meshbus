// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/drivers/gnss.h>
#include <zephyr/kernel.h>
#include <zephyr/meshbus/gnss.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

#include "services/gnss/gnss_internal.h"

#define GNSS_CONFIG_KEY "meshbus/gnss/config"

static atomic_t block_config_delete;
K_SEM_DEFINE(config_delete_entered, 0, 1);
K_SEM_DEFINE(config_delete_release, 0, 1);
K_THREAD_STACK_DEFINE(config_reset_stack, 1536);
K_THREAD_STACK_DEFINE(config_set_stack, 1536);
static struct k_thread config_reset_thread;
static struct k_thread config_set_thread;
static meshbus_gnss_config concurrent_set_cfg;
static atomic_t concurrent_set_done;
static int concurrent_reset_rc;
static int concurrent_set_rc;

int __real_settings_delete(const char *name);

int __wrap_settings_delete(const char *name)
{
	if (name != NULL && strcmp(name, GNSS_CONFIG_KEY) == 0 &&
	    atomic_cas(&block_config_delete, 1, 0)) {
		k_sem_give(&config_delete_entered);
		(void)k_sem_take(&config_delete_release, K_FOREVER);
	}

	return __real_settings_delete(name);
}

static meshbus_gnss_config valid_disabled_config(void)
{
	meshbus_gnss_config cfg = meshbus_GnssConfig_init_zero;

	cfg.enabled = false;
	cfg.nav_mode = meshbus_GnssConfig_GnssNavMode_BALANCED_DYNAMICS;
	cfg.fix_rate = 1;
	cfg.system_mask = 0x01;
	cfg.update_interval = CONFIG_MESHBUS_GNSS_MIN_UPDATE_INTERVAL;
	cfg.min_active_time = CONFIG_MESHBUS_GNSS_MIN_ACTIVE_TIME;
	cfg.time_sync = false;
	return cfg;
}

static void config_reset_thread_fn(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	concurrent_reset_rc = meshbus_gnss_config_reset();
}

static void config_set_thread_fn(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	concurrent_set_rc = meshbus_gnss_config_set_full(&concurrent_set_cfg);
	atomic_set(&concurrent_set_done, 1);
}

static void *gnss_suite_setup(void)
{
	zassert_ok(meshbus_gnss_config_reset());
	meshbus_gnss_satellites_cache_clear();
	return NULL;
}

static void gnss_before(void *fixture)
{
	ARG_UNUSED(fixture);
	zassert_ok(meshbus_gnss_config_reset());
	meshbus_gnss_satellites_cache_clear();
}

ZTEST(meshbus_gnss_contract, test_config_defaults_and_validation)
{
	meshbus_gnss_config cfg;

	zassert_equal(meshbus_gnss_config_get(NULL), -EINVAL);
	zassert_ok(meshbus_gnss_config_get(&cfg));
	zassert_false(cfg.enabled);
	zassert_true(cfg.time_sync);
	zassert_true(cfg.has_electronic_compass);
	zassert_true(cfg.electronic_compass);
	zassert_equal(meshbus_gnss_state_get(), MESHBUS_GNSS_STATE_SLEEP);

	cfg = valid_disabled_config();
	cfg.update_interval = CONFIG_MESHBUS_GNSS_MIN_UPDATE_INTERVAL - 1;
	zassert_equal(meshbus_gnss_config_set(&cfg), -EINVAL);

	cfg = valid_disabled_config();
	cfg.nav_mode = (meshbus_GnssConfig_GnssNavMode)99;
	zassert_equal(meshbus_gnss_config_set(&cfg), -EINVAL);

	cfg = valid_disabled_config();
	cfg.fix_rate = 3;
	zassert_equal(meshbus_gnss_config_set(&cfg), -EINVAL);

	cfg = valid_disabled_config();
	cfg.system_mask = 0;
	zassert_equal(meshbus_gnss_config_set(&cfg), -EINVAL);
}

ZTEST(meshbus_gnss_contract, test_disabled_config_set_get_and_enabled_device_apply)
{
	meshbus_gnss_config cfg = valid_disabled_config();
	meshbus_gnss_config got;

	cfg.update_interval = CONFIG_MESHBUS_GNSS_MIN_UPDATE_INTERVAL + 1000;
	cfg.min_active_time = CONFIG_MESHBUS_GNSS_MIN_ACTIVE_TIME;
	zassert_ok(meshbus_gnss_config_set(&cfg));
	zassert_ok(meshbus_gnss_config_get(&got));
	zassert_equal(got.enabled, cfg.enabled);
	zassert_equal(got.update_interval, cfg.update_interval);
	zassert_equal(got.system_mask, cfg.system_mask);
	zassert_true(got.has_electronic_compass);
	zassert_true(got.electronic_compass);

	cfg.enabled = true;
	zassert_ok(meshbus_gnss_config_set(&cfg));
}

ZTEST(meshbus_gnss_contract, test_abi_v1_set_ignores_legacy_tail_padding)
{
	meshbus_gnss_config full = valid_disabled_config();
	meshbus_gnss_config legacy;
	meshbus_gnss_config got;
	const uint8_t *legacy_bytes = (const uint8_t *)&legacy;

	zassert_equal(meshbus_gnss_config_set_full(NULL), -EINVAL);
	full.has_electronic_compass = true;
	full.electronic_compass = false;
	zassert_ok(meshbus_gnss_config_set_full(&full));
	zassert_ok(meshbus_gnss_config_get(&got));
	zassert_false(got.electronic_compass);

	/* Model an old LLEXT object whose tail padding was never initialized. */
	memset(&legacy, 0xa5, sizeof(legacy));
	legacy.enabled = full.enabled;
	legacy.nav_mode = full.nav_mode;
	legacy.fix_rate = full.fix_rate;
	legacy.system_mask = full.system_mask;
	legacy.update_interval = full.update_interval;
	legacy.min_active_time = full.min_active_time;
	legacy.time_sync = full.time_sync;
	zassert_equal(legacy_bytes[offsetof(meshbus_gnss_config, has_electronic_compass)], 0xa5);
	zassert_equal(legacy_bytes[offsetof(meshbus_gnss_config, electronic_compass)], 0xa5);

	zassert_ok(meshbus_gnss_config_set(&legacy));
	zassert_ok(meshbus_gnss_config_get(&got));
	zassert_true(got.has_electronic_compass);
	zassert_false(got.electronic_compass,
		      "ABI-v1 setter consumed legacy tail padding");

	full.electronic_compass = true;
	zassert_ok(meshbus_gnss_config_set_full(&full));
	zassert_ok(meshbus_gnss_config_get(&got));
	zassert_true(got.electronic_compass,
		     "firmware full setter did not update Compass preference");
}

ZTEST(meshbus_gnss_contract, test_reset_serializes_concurrent_full_set)
{
	meshbus_gnss_config got;
	k_tid_t reset_tid;
	k_tid_t set_tid;

	concurrent_set_cfg = valid_disabled_config();
	concurrent_set_cfg.update_interval = CONFIG_MESHBUS_GNSS_MIN_UPDATE_INTERVAL + 777U;
	concurrent_set_cfg.has_electronic_compass = true;
	concurrent_set_cfg.electronic_compass = false;
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
		      "full config set escaped while reset's delete transaction was open");

	k_sem_give(&config_delete_release);
	zassert_ok(k_thread_join(&config_reset_thread, K_SECONDS(1)));
	zassert_ok(k_thread_join(&config_set_thread, K_SECONDS(1)));
	zassert_ok(concurrent_reset_rc);
	zassert_ok(concurrent_set_rc);
	zassert_ok(meshbus_gnss_config_get(&got));
	zassert_equal(got.update_interval, concurrent_set_cfg.update_interval);
	zassert_false(got.electronic_compass);
}

ZTEST(meshbus_gnss_contract, test_heading_ignores_electronic_preference_without_dts_provider)
{
	meshbus_gnss_config cfg;
	struct meshbus_gnss_heading_runtime_status status;
	struct meshbus_gnss_heading_snapshot snapshot;

	zassert_ok(meshbus_gnss_heading_runtime_status_get(&status));
	zassert_false(status.electronic_available);
	zassert_equal(meshbus_gnss_heading_acquire(), -EACCES);

	zassert_ok(meshbus_gnss_config_get(&cfg));
	cfg.enabled = true;
	zassert_ok(meshbus_gnss_config_set(&cfg));
	zassert_ok(meshbus_gnss_heading_acquire());

	for (size_t attempt = 0; attempt < 20U; attempt++) {
		zassert_ok(meshbus_gnss_heading_snapshot_get(&snapshot));
		if (snapshot.state != MESHBUS_GNSS_HEADING_STATE_STARTING) {
			break;
		}
		k_sleep(K_MSEC(10));
	}
	zassert_equal(snapshot.source, MESHBUS_GNSS_HEADING_SOURCE_COURSE);
	zassert_equal(snapshot.state, MESHBUS_GNSS_HEADING_STATE_WAITING_FOR_FIX);
	zassert_false(snapshot.valid);

	cfg.has_electronic_compass = true;
	cfg.electronic_compass = false;
	zassert_ok(meshbus_gnss_config_set_full(&cfg));
	zassert_ok(meshbus_gnss_heading_release());
}

ZTEST(meshbus_gnss_contract, test_acquisition_and_cached_getters_without_fix)
{
	struct navigation_data nav;
	struct gnss_info info;
	struct gnss_time time;
	struct meshbus_gnss_data_event event;
	uint32_t source_timestamp_ms;

	zassert_equal(meshbus_gnss_acquisition(), -EACCES);
	zassert_equal(meshbus_gnss_position_get(NULL), -EINVAL);
	zassert_equal(meshbus_gnss_position_get(&nav), -ENODATA);
	zassert_equal(meshbus_gnss_info_get(NULL), -EINVAL);
	zassert_equal(meshbus_gnss_info_get(&info), -ENODATA);
	zassert_equal(meshbus_gnss_time_get(NULL), -EINVAL);
	zassert_equal(meshbus_gnss_time_get(&time), -ENODATA);
	zassert_equal(meshbus_gnss_fix_snapshot_get(NULL, &source_timestamp_ms), -EINVAL);
	zassert_equal(meshbus_gnss_fix_snapshot_get(&event, NULL), -EINVAL);
	zassert_equal(meshbus_gnss_fix_snapshot_get(&event, &source_timestamp_ms), -ENODATA);
}

ZTEST(meshbus_gnss_contract, test_satellite_cache_boundaries_and_public_channel_validator)
{
	struct gnss_satellite satellite;
	struct gnss_satellite satellites[1];
	uint16_t count = 99;
	struct meshbus_gnss_data_event event = { 0 };

	zassert_equal(meshbus_gnss_satellites_count(), 0);
	zassert_equal(meshbus_gnss_satellites_get(NULL, ARRAY_SIZE(satellites), &count), -EINVAL);
	zassert_equal(meshbus_gnss_satellites_get(satellites, ARRAY_SIZE(satellites), NULL),
		      -EINVAL);
	zassert_equal(meshbus_gnss_satellites_get(satellites, ARRAY_SIZE(satellites), &count),
		      -ENODATA);
	zassert_equal(count, 0);
	zassert_equal(meshbus_gnss_satellite_get_by_index(0, NULL), -EINVAL);
	zassert_equal(meshbus_gnss_satellite_get_by_index(0, &satellite), -ENODATA);

	event.valid = false;
	event.info.fix_status = GNSS_FIX_STATUS_NO_FIX;
	zassert_ok(zbus_chan_pub(&meshbus_gnss_data_chan, &event, K_NO_WAIT));
	zassert_ok(zbus_chan_read(&meshbus_gnss_data_chan, &event, K_NO_WAIT));
	zassert_false(event.valid);

	event.valid = true;
	event.info.fix_status = GNSS_FIX_STATUS_NO_FIX;
	zassert_not_equal(zbus_chan_pub(&meshbus_gnss_data_chan, &event, K_NO_WAIT), 0);
}

ZTEST_SUITE(meshbus_gnss_contract, NULL, gnss_suite_setup, gnss_before, NULL, NULL);
