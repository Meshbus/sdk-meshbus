// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/meshbus/radio.h>
#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

#define RADIO_SETTINGS_SUBTREE "meshbus/radio"
#define RADIO_SETTINGS_KEY_CONFIG "config"
#define RADIO_SETTINGS_CONFIG_KEY RADIO_SETTINGS_SUBTREE "/config"
#define RADIO_SETTINGS_RECORD_MAX 256U

struct persisted_setting {
	bool present;
	size_t len;
	uint8_t value[RADIO_SETTINGS_RECORD_MAX];
};

static meshbus_radio_config radio_defaults(void)
{
	meshbus_radio_config cfg = meshbus_RadioConfig_init_zero;

	cfg.enabled = IS_ENABLED(CONFIG_MESHBUS_RADIO_DEFAULT_ENABLED);
	cfg.frequency = CONFIG_MESHBUS_RADIO_DEFAULT_FREQUENCY;
	cfg.bandwidth = CONFIG_MESHBUS_RADIO_DEFAULT_BANDWIDTH;
	cfg.spread_factor = CONFIG_MESHBUS_RADIO_DEFAULT_SPREAD_FACTOR;
	cfg.coding_rate = CONFIG_MESHBUS_RADIO_DEFAULT_CODING_RATE;
	cfg.preamble_length = CONFIG_MESHBUS_RADIO_DEFAULT_PREAMBLE_LENGTH;
	cfg.tx_power = CONFIG_MESHBUS_RADIO_DEFAULT_TX_POWER;
	cfg.receive_only = IS_ENABLED(CONFIG_MESHBUS_RADIO_DEFAULT_RECEIVE_ONLY);
	cfg.rx_boosted = IS_ENABLED(CONFIG_MESHBUS_RADIO_DEFAULT_RX_BOOSTED);
	cfg.crc = IS_ENABLED(CONFIG_MESHBUS_RADIO_DEFAULT_CRC);
	cfg.duty_cycle = IS_ENABLED(CONFIG_MESHBUS_RADIO_DEFAULT_DUTY_CYCLE);
	cfg.duty_cycle_rx_time = CONFIG_MESHBUS_RADIO_DEFAULT_DUTY_CYCLE_RX_TIME;
	cfg.duty_cycle_sleep_time = CONFIG_MESHBUS_RADIO_DEFAULT_DUTY_CYCLE_SLEEP_TIME;

	return cfg;
}

static meshbus_radio_config radio_custom_disabled_config(void)
{
	meshbus_radio_config cfg = radio_defaults();

	cfg.enabled = false;
	cfg.frequency = 868100000ULL;
	cfg.bandwidth = 125000U;
	cfg.spread_factor = 9U;
	cfg.coding_rate = 7U;
	cfg.preamble_length = 12U;
	cfg.tx_power = 10;
	cfg.receive_only = true;
	cfg.rx_boosted = false;
	cfg.crc = false;
	cfg.duty_cycle = true;
	cfg.duty_cycle_rx_time = 50U;
	cfg.duty_cycle_sleep_time = 950U;

	return cfg;
}

static meshbus_radio_config radio_alternate_disabled_config(void)
{
	meshbus_radio_config cfg = radio_custom_disabled_config();

	cfg.frequency = 915000000ULL;
	cfg.spread_factor = 10U;
	cfg.coding_rate = 6U;
	cfg.preamble_length = 16U;
	cfg.tx_power = 14;
	cfg.duty_cycle_rx_time = 75U;
	cfg.duty_cycle_sleep_time = 925U;

	return cfg;
}

static void radio_expect_config_eq(const meshbus_radio_config *actual,
				      const meshbus_radio_config *expected)
{
	zassert_equal(actual->enabled, expected->enabled, "enabled mismatch");
	zassert_equal(actual->frequency, expected->frequency, "frequency mismatch");
	zassert_equal(actual->bandwidth, expected->bandwidth, "bandwidth mismatch");
	zassert_equal(actual->spread_factor, expected->spread_factor, "spread_factor mismatch");
	zassert_equal(actual->coding_rate, expected->coding_rate, "coding_rate mismatch");
	zassert_equal(actual->preamble_length, expected->preamble_length,
		      "preamble_length mismatch");
	zassert_equal(actual->tx_power, expected->tx_power, "tx_power mismatch");
	zassert_equal(actual->receive_only, expected->receive_only, "receive_only mismatch");
	zassert_equal(actual->rx_boosted, expected->rx_boosted, "rx_boosted mismatch");
	zassert_equal(actual->crc, expected->crc, "crc mismatch");
	zassert_equal(actual->duty_cycle, expected->duty_cycle, "duty_cycle mismatch");
	zassert_equal(actual->duty_cycle_rx_time, expected->duty_cycle_rx_time,
		      "duty_cycle_rx_time mismatch");
	zassert_equal(actual->duty_cycle_sleep_time, expected->duty_cycle_sleep_time,
		      "duty_cycle_sleep_time mismatch");
}

static void radio_wait_for_persistence(void)
{
	k_sleep(K_MSEC(CONFIG_MESHBUS_SETTINGS_PERSISTENCE_DELAY + 50));
}

static int radio_capture_config_cb(const char *key, size_t len,
				   settings_read_cb read_cb, void *cb_arg,
				   void *param)
{
	struct persisted_setting *record = param;
	const char *next = NULL;
	ssize_t bytes_read;

	if (!settings_name_steq(key, RADIO_SETTINGS_KEY_CONFIG, &next) ||
	    next != NULL) {
		return 0;
	}
	if (len > sizeof(record->value)) {
		return -E2BIG;
	}

	bytes_read = read_cb(cb_arg, record->value, len);
	if (bytes_read < 0) {
		return (int)bytes_read;
	}
	if ((size_t)bytes_read != len) {
		return -EIO;
	}

	record->present = true;
	record->len = len;
	return 0;
}

static int radio_read_persisted_config(struct persisted_setting *record)
{
	memset(record, 0, sizeof(*record));
	return settings_load_subtree_direct(RADIO_SETTINGS_SUBTREE,
					    radio_capture_config_cb, record);
}

static void radio_expect_store_present(struct persisted_setting *record_out)
{
	struct persisted_setting record;

	zassert_ok(radio_read_persisted_config(&record),
		   "read persisted radio config failed");
	zassert_true(record.present, "persisted radio config missing");
	zassert_true(record.len > 0U, "persisted radio config is empty");

	if (record_out != NULL) {
		*record_out = record;
	}
}

static void radio_delete_test_residue_keys(void)
{
	static const char *const keys[] = {
		RADIO_SETTINGS_CONFIG_KEY "/99",
		RADIO_SETTINGS_CONFIG_KEY "/0",
		RADIO_SETTINGS_CONFIG_KEY "/33",
		RADIO_SETTINGS_CONFIG_KEY "/4294967296",
		RADIO_SETTINGS_CONFIG_KEY "/999999999999999999999999",
		RADIO_SETTINGS_CONFIG_KEY "/2",
		RADIO_SETTINGS_CONFIG_KEY "/1",
	};

	for (size_t i = 0; i < ARRAY_SIZE(keys); i++) {
		(void)settings_delete(keys[i]);
	}
}

static void radio_expect_store_empty(void)
{
	ssize_t len = settings_get_val_len(RADIO_SETTINGS_CONFIG_KEY);

	zassert_true(len == -ENOENT || len == 0,
		     "radio config blob should be deleted or tombstoned: len=%zd", len);
}

static void radio_reset_state(void)
{
	zassert_ok(meshbus_radio_config_reset(), "radio reset failed");
	radio_delete_test_residue_keys();
}

static void *suite_setup(void)
{
	radio_reset_state();
	return NULL;
}

static void test_before(void *fixture)
{
	ARG_UNUSED(fixture);
	radio_reset_state();
}

ZTEST(meshbus_radio_contract, test_config_set_persists_record)
{
	meshbus_radio_config cfg = radio_custom_disabled_config();
	meshbus_radio_config got = meshbus_RadioConfig_init_zero;

	zassert_ok(meshbus_radio_config_set(&cfg), "config_set failed");
	radio_wait_for_persistence();

	zassert_ok(meshbus_radio_config_get(&got), "config_get failed");
	radio_expect_config_eq(&got, &cfg);
	radio_expect_store_present(NULL);
}

ZTEST(meshbus_radio_contract, test_state_event_reflects_disabled_config)
{
	meshbus_radio_config cfg = radio_custom_disabled_config();
	struct meshbus_radio_state_event before = {0};
	struct meshbus_radio_state_event after = {0};

	zassert_ok(zbus_chan_read(&meshbus_radio_state_chan, &before, K_NO_WAIT),
		   "state channel read before config_set failed");
	zassert_ok(meshbus_radio_config_set(&cfg), "config_set failed");
	zassert_ok(zbus_chan_read(&meshbus_radio_state_chan, &after, K_NO_WAIT),
		   "state channel read after config_set failed");

	zassert_false(after.enabled, "state event enabled mismatch");
	zassert_true(after.receive_only, "state event receive_only mismatch");
	zassert_equal(after.state, MESHBUS_RADIO_STATE_IDLE, "state event state mismatch");
	zassert_true(after.sequence > before.sequence,
		     "state event sequence did not advance: before=%u after=%u",
		     before.sequence, after.sequence);
}

ZTEST(meshbus_radio_contract, test_cached_status_is_bounded_without_hardware_io)
{
	struct meshbus_radio_status status = {0};

	zassert_equal(meshbus_radio_status_get(NULL), -EINVAL,
		      "NULL status destination was accepted");
	zassert_ok(meshbus_radio_status_get(&status), "cached status get failed");
	zassert_equal(status.state, MESHBUS_RADIO_STATE_IDLE,
		      "disabled status state mismatch");
	zassert_false(status.receiving, "disabled radio reported channel activity");
	zassert_false(status.has_rssi_inst_dbm,
		      "disabled radio exposed an instantaneous RSSI sample");
}

ZTEST(meshbus_radio_contract, test_cw_rejects_disabled_radio_without_starting)
{
	struct meshbus_radio_cw_request_event request = {
		.frequency_hz = 915125000U,
		.duration_s = 1U,
		.tx_power_dbm = 0,
	};
	struct meshbus_radio_cw_done_event before = {0};
	struct meshbus_radio_cw_done_event after = {0};

	zassert_ok(zbus_chan_read(&meshbus_radio_cw_done_chan, &before, K_NO_WAIT),
		   "continuous-wave completion channel read before request failed");
	zassert_not_equal(zbus_chan_pub(&meshbus_radio_cw_chan, &request, K_NO_WAIT),
			  0, "disabled continuous wave was accepted");
	zassert_ok(zbus_chan_read(&meshbus_radio_cw_done_chan, &after, K_NO_WAIT),
		   "continuous-wave completion channel read after request failed");
	zassert_equal(after.sequence, before.sequence,
		      "rejected request published a completion event");
}

ZTEST(meshbus_radio_contract, test_cw_validates_parameters)
{
	struct meshbus_radio_cw_request_event request = {
		.frequency_hz = 399999999U,
		.duration_s = 1U,
		.tx_power_dbm = 0,
	};

	zassert_not_equal(zbus_chan_pub(&meshbus_radio_cw_chan, &request, K_NO_WAIT),
			  0, "frequency below range accepted");
	request.frequency_hz = 2500000001U;
	zassert_not_equal(zbus_chan_pub(&meshbus_radio_cw_chan, &request, K_NO_WAIT),
			  0, "frequency above range accepted");
	request.frequency_hz = 915125000U;
	request.tx_power_dbm = -1;
	zassert_not_equal(zbus_chan_pub(&meshbus_radio_cw_chan, &request, K_NO_WAIT),
			  0, "negative power accepted");
	request.tx_power_dbm = 23;
	zassert_not_equal(zbus_chan_pub(&meshbus_radio_cw_chan, &request, K_NO_WAIT),
			  0, "power above range accepted");
	request.tx_power_dbm = 0;
	request.duration_s = 0U;
	zassert_not_equal(zbus_chan_pub(&meshbus_radio_cw_chan, &request, K_NO_WAIT),
			  0, "zero duration accepted");
}

ZTEST(meshbus_radio_contract, test_settings_reload_restores_config)
{
	meshbus_radio_config cfg = radio_custom_disabled_config();
	meshbus_radio_config alternate = radio_alternate_disabled_config();
	meshbus_radio_config got = meshbus_RadioConfig_init_zero;
	struct persisted_setting record;

	zassert_ok(meshbus_radio_config_set(&cfg), "initial config_set failed");
	radio_wait_for_persistence();
	radio_expect_store_present(&record);

	zassert_ok(meshbus_radio_config_set(&alternate), "alternate config_set failed");
	radio_wait_for_persistence();
	zassert_ok(settings_save_one(RADIO_SETTINGS_CONFIG_KEY, record.value,
				     record.len),
		   "restore persisted config record failed");

	zassert_ok(settings_load_subtree(RADIO_SETTINGS_SUBTREE), "settings load failed");
	zassert_ok(meshbus_radio_config_get(&got), "config_get failed");
	radio_expect_config_eq(&got, &cfg);
}

ZTEST(meshbus_radio_contract, test_missing_settings_keep_defaults)
{
	meshbus_radio_config got = meshbus_RadioConfig_init_zero;
	meshbus_radio_config defaults = radio_defaults();

	zassert_ok(settings_load_subtree(RADIO_SETTINGS_SUBTREE), "settings load failed");
	zassert_ok(meshbus_radio_config_get(&got), "config_get failed");
	radio_expect_config_eq(&got, &defaults);
}

ZTEST(meshbus_radio_contract, test_invalid_entries_are_ignored)
{
	meshbus_radio_config cfg = radio_custom_disabled_config();
	meshbus_radio_config got = meshbus_RadioConfig_init_zero;
	static const uint8_t malformed_blob[] = {0xff, 0xff, 0xff};
	static const uint8_t residue_value[] = {0x02};

	zassert_ok(meshbus_radio_config_set(&cfg), "config_set failed");
	radio_wait_for_persistence();
	zassert_ok(settings_delete(RADIO_SETTINGS_CONFIG_KEY),
		   "delete settings failed");

	zassert_ok(settings_save_one(RADIO_SETTINGS_CONFIG_KEY "/99", residue_value,
				     sizeof(residue_value)),
		  "save unknown tag failed");
	zassert_ok(settings_save_one(RADIO_SETTINGS_CONFIG_KEY "/0", residue_value,
				     sizeof(residue_value)),
		  "save zero tag failed");
	zassert_ok(settings_save_one(RADIO_SETTINGS_CONFIG_KEY "/33", residue_value,
				     sizeof(residue_value)),
		  "save tag above bitmask capacity failed");
	zassert_ok(settings_save_one(RADIO_SETTINGS_CONFIG_KEY "/4294967296", residue_value,
				     sizeof(residue_value)),
		  "save tag above uint32 failed");
	zassert_ok(settings_save_one(RADIO_SETTINGS_CONFIG_KEY "/999999999999999999999999",
				     residue_value, sizeof(residue_value)),
		  "save overflowing tag failed");
	zassert_ok(settings_save_one(RADIO_SETTINGS_CONFIG_KEY, malformed_blob,
				     sizeof(malformed_blob)),
		   "save malformed blob failed");

	zassert_ok(settings_load_subtree(RADIO_SETTINGS_SUBTREE), "settings load failed");
	zassert_ok(meshbus_radio_config_get(&got), "config_get failed");
	radio_expect_config_eq(&got, &cfg);
	radio_delete_test_residue_keys();
	zassert_ok(settings_delete(RADIO_SETTINGS_CONFIG_KEY),
		   "delete malformed blob failed");
}

ZTEST(meshbus_radio_contract, test_reset_restores_defaults_and_deletes_keys)
{
	meshbus_radio_config cfg = radio_custom_disabled_config();
	meshbus_radio_config got = meshbus_RadioConfig_init_zero;
	meshbus_radio_config defaults = radio_defaults();

	zassert_ok(meshbus_radio_config_set(&cfg), "config_set failed");
	radio_wait_for_persistence();
	radio_expect_store_present(NULL);

	zassert_ok(meshbus_radio_config_reset(), "config_reset failed");
	zassert_ok(meshbus_radio_config_get(&got), "config_get failed");
	radio_expect_config_eq(&got, &defaults);
	radio_expect_store_empty();
}

ZTEST(meshbus_radio_contract, test_reset_succeeds_without_persisted_keys)
{
	meshbus_radio_config got = meshbus_RadioConfig_init_zero;
	meshbus_radio_config defaults = radio_defaults();

	radio_expect_store_empty();
	zassert_ok(meshbus_radio_config_reset(), "config_reset failed on empty store");
	zassert_ok(meshbus_radio_config_get(&got), "config_get failed");
	radio_expect_config_eq(&got, &defaults);
	radio_expect_store_empty();
}

ZTEST_SUITE(meshbus_radio_contract, NULL, suite_setup, test_before, NULL, NULL);
