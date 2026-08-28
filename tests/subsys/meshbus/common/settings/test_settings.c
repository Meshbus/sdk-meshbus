// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/meshbus/radio.h>
#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

#include "common/settings.h"

#define TEST_SETTINGS_SUBTREE "meshbus/test/common_settings"
#define TEST_LONG_SETTINGS_SUBTREE \
	"meshbus/test/common_settings/this/subtree/name/is/intentionally/long"

struct exported_setting {
	bool present;
	char key[96];
	size_t len;
	uint8_t value[256];
};

MB_SETTINGS_BLOB_SCHEMA_DEFINE(long_blob_schema, TEST_LONG_SETTINGS_SUBTREE, "config",
			       meshbus_RadioConfig, meshbus_radio_config);
MB_SETTINGS_INDEXED_BLOB_SCHEMA_DEFINE(long_indexed_blob_schema,
				       TEST_LONG_SETTINGS_SUBTREE, "records",
				       meshbus_RadioConfig, meshbus_radio_config);
MB_SETTINGS_INDEXED_RAW_SCHEMA_DEFINE(long_indexed_raw_schema,
				      TEST_LONG_SETTINGS_SUBTREE, "records");
MB_SETTINGS_BLOB_SCHEMA_DEFINE(blob_schema, TEST_SETTINGS_SUBTREE, "config",
			       meshbus_RadioConfig, meshbus_radio_config);
MB_SETTINGS_INDEXED_BLOB_SCHEMA_DEFINE(indexed_blob_schema, TEST_SETTINGS_SUBTREE, "records",
				       meshbus_RadioConfig, meshbus_radio_config);
MB_SETTINGS_INDEXED_RAW_SCHEMA_DEFINE(indexed_raw_schema, TEST_SETTINGS_SUBTREE, "raw_records");

static struct exported_setting exported_settings[4];
static size_t exported_setting_count;

struct blob_load_ctx {
	const struct mb_settings_blob_schema *schema;
	struct k_mutex mutex;
	struct mb_settings_blob_load_state state;
	meshbus_radio_config staging;
	uint8_t buffer[MB_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_RadioConfig_size)];
};

static meshbus_radio_config test_config(void)
{
	meshbus_radio_config cfg = meshbus_RadioConfig_init_zero;

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

static void expect_config_eq(const meshbus_radio_config *actual,
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

static int blob_load_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg,
			void *param)
{
	struct blob_load_ctx *ctx = param;

	return mb_settings_blob_handle_set_with_buffer(ctx->schema, &ctx->mutex, &ctx->state,
						       &ctx->staging, key, len, read_cb, cb_arg,
						       ctx->buffer, sizeof(ctx->buffer));
}

static int export_collect_cb(const char *name, const void *val, size_t len)
{
	struct exported_setting *entry;

	zassert_not_null(name, "exported key is NULL");
	zassert_not_null(val, "exported value is NULL");
	zassert_true(exported_setting_count < ARRAY_SIZE(exported_settings),
		     "too many exported settings");
	zassert_true(len <= sizeof(exported_settings[exported_setting_count].value),
		     "exported value too large");

	entry = &exported_settings[exported_setting_count++];
	entry->present = true;
	strncpy(entry->key, name, sizeof(entry->key) - 1U);
	entry->key[sizeof(entry->key) - 1U] = '\0';
	entry->len = len;
	memcpy(entry->value, val, len);

	return 0;
}

static const struct exported_setting *export_find(const char *key)
{
	for (size_t i = 0; i < exported_setting_count; i++) {
		if (exported_settings[i].present &&
		    strcmp(exported_settings[i].key, key) == 0) {
			return &exported_settings[i];
		}
	}

	return NULL;
}

static void reset_exported_settings(void)
{
	memset(exported_settings, 0, sizeof(exported_settings));
	exported_setting_count = 0U;
}

static void cleanup_settings(void)
{
	zassert_ok(mb_settings_blob_delete(&blob_schema), "blob cleanup failed");
	zassert_ok(mb_settings_indexed_blob_delete(&indexed_blob_schema, 3U),
		   "indexed blob cleanup failed");
	zassert_ok(mb_settings_indexed_raw_delete(&indexed_raw_schema, 4U),
		   "indexed raw cleanup failed");
	reset_exported_settings();
}

static void *suite_setup(void)
{
	cleanup_settings();
	return NULL;
}

static void test_before(void *fixture)
{
	ARG_UNUSED(fixture);
	cleanup_settings();
}

static void test_after(void *fixture)
{
	ARG_UNUSED(fixture);
	cleanup_settings();
}

ZTEST(meshbus_common_settings_helpers, test_key_builders_reject_truncation)
{
	char small_buf[32];
	char key_buf[96];
	int rc;

	rc = mb_settings_blob_key_assemble(small_buf, sizeof(small_buf), &long_blob_schema);
	zassert_equal(rc, -ENAMETOOLONG, "blob key truncation returned %d", rc);
	rc = mb_settings_blob_key_assemble(key_buf, sizeof(key_buf), &long_blob_schema);
	zassert_true(rc > 0, "blob key assemble failed: %d", rc);
	zassert_str_equal(
		key_buf,
		"meshbus/test/common_settings/this/subtree/name/is/intentionally/long/config",
		"unexpected blob key");

	rc = mb_settings_indexed_blob_key_assemble(small_buf, sizeof(small_buf),
						   &long_indexed_blob_schema, 12U);
	zassert_equal(rc, -ENAMETOOLONG, "indexed blob key truncation returned %d", rc);
	rc = mb_settings_indexed_blob_key_assemble(key_buf, sizeof(key_buf),
						   &long_indexed_blob_schema, 12U);
	zassert_true(rc > 0, "indexed blob key assemble failed: %d", rc);
	zassert_str_equal(
		key_buf,
		"meshbus/test/common_settings/this/subtree/name/is/intentionally/long/records/12",
		"unexpected indexed blob key");

	rc = mb_settings_indexed_raw_key_assemble(small_buf, sizeof(small_buf),
						  &long_indexed_raw_schema, 12U);
	zassert_equal(rc, -ENAMETOOLONG, "indexed raw key truncation returned %d", rc);
	rc = mb_settings_indexed_raw_key_assemble(key_buf, sizeof(key_buf),
						  &long_indexed_raw_schema, 12U);
	zassert_true(rc > 0, "indexed raw key assemble failed: %d", rc);
	zassert_str_equal(
		key_buf,
		"meshbus/test/common_settings/this/subtree/name/is/intentionally/long/records/12",
		"unexpected indexed raw key");
}

ZTEST(meshbus_common_settings_helpers, test_indexed_blob_parser_rejects_malformed_keys)
{
	size_t idx = 0U;
	int rc;

	rc = mb_settings_indexed_blob_parse_slot_name(&indexed_blob_schema, "records/12", &idx);
	zassert_ok(rc, "valid indexed blob slot parse failed: %d", rc);
	zassert_equal(idx, 12U, "parsed blob slot mismatch");
	rc = mb_settings_indexed_blob_parse_slot_name(&indexed_blob_schema, "records/12/1",
						      &idx);
	zassert_equal(rc, -ENOENT, "extra suffix blob slot parse returned %d", rc);
	rc = mb_settings_indexed_blob_parse_slot_name(&indexed_blob_schema, "records/x", &idx);
	zassert_equal(rc, -ENOENT, "non-numeric blob slot parse returned %d", rc);
	rc = mb_settings_indexed_blob_parse_slot_name(
		&indexed_blob_schema, "records/999999999999999999999999", &idx);
	zassert_equal(rc, -ENOENT, "overflowing blob slot parse returned %d", rc);
	rc = mb_settings_indexed_blob_parse_slot_name(&indexed_blob_schema, "other/12", &idx);
	zassert_equal(rc, -ENOENT, "wrong prefix blob slot parse returned %d", rc);
}

ZTEST(meshbus_common_settings_helpers, test_blob_round_trip_and_export)
{
	meshbus_radio_config cfg = test_config();
	meshbus_radio_config got = meshbus_RadioConfig_init_zero;
	struct blob_load_ctx ctx = {
		.schema = &blob_schema,
	};
	const struct exported_setting *exported;
	uint8_t buffer[MB_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_RadioConfig_size)];
	char key_buf[96];
	bool force = false;
	int rc;

	k_mutex_init(&ctx.mutex);
	mb_settings_blob_load_state_reset(&ctx.state, &ctx.staging, sizeof(ctx.staging));

	rc = mb_settings_blob_key_assemble(key_buf, sizeof(key_buf), &blob_schema);
	zassert_true(rc > 0, "blob key assemble failed: %d", rc);
	zassert_ok(mb_settings_blob_save_with_buffer(&blob_schema, &cfg, buffer,
						     sizeof(buffer)),
		   "blob save failed");

	zassert_ok(settings_load_subtree_direct(TEST_SETTINGS_SUBTREE, blob_load_cb, &ctx),
		   "blob subtree load failed");
	zassert_true(mb_settings_blob_commit_prepare(&blob_schema, &ctx.mutex, &ctx.state,
						     &ctx.staging, &got, NULL, &force),
		     "blob commit should see stored config");
	zassert_false(force, "force should default false without initial_apply pointer");
	expect_config_eq(&got, &cfg);

	reset_exported_settings();
	zassert_ok(mb_settings_blob_export_with_buffer(&blob_schema, &cfg, buffer,
						       sizeof(buffer), export_collect_cb),
		   "blob export failed");
	exported = export_find(key_buf);
	zassert_not_null(exported, "blob key was not exported");
	zassert_true(exported->len > 0U, "blob export should not be empty");
	zassert_equal(exported_setting_count, 1U, "unexpected blob export count");
}

ZTEST(meshbus_common_settings_helpers, test_invalid_blob_is_reported_to_consumer)
{
	static const uint8_t invalid_record[] = {'N', 'O', 'P', 'E'};
	meshbus_radio_config got = test_config();
	struct blob_load_ctx ctx = {
		.schema = &blob_schema,
	};
	char key_buf[96];
	bool force = false;
	int rc;

	k_mutex_init(&ctx.mutex);
	mb_settings_blob_load_state_reset(&ctx.state, &ctx.staging, sizeof(ctx.staging));

	rc = mb_settings_blob_key_assemble(key_buf, sizeof(key_buf), &blob_schema);
	zassert_true(rc > 0, "blob key assemble failed: %d", rc);
	zassert_ok(settings_save_one(key_buf, invalid_record, sizeof(invalid_record)),
		   "invalid blob save failed");
	zassert_ok(settings_load_subtree_direct(TEST_SETTINGS_SUBTREE, blob_load_cb, &ctx),
		   "invalid blob subtree load failed");
	zassert_false(ctx.state.seen, "invalid blob must not be staged");
	zassert_true(ctx.state.invalid, "consumer must be told that persisted data was invalid");
	zassert_false(mb_settings_blob_commit_prepare(&blob_schema, &ctx.mutex, &ctx.state,
						      &ctx.staging, &got, NULL, &force),
		      "invalid blob must not be committed");
	zassert_false(ctx.state.invalid, "commit preparation must reset load state");
}

ZTEST(meshbus_common_settings_helpers, test_zero_payload_records_are_preserved)
{
	meshbus_radio_config zero_cfg = meshbus_RadioConfig_init_zero;
	meshbus_radio_config got = test_config();
	struct blob_load_ctx ctx = {
		.schema = &blob_schema,
	};
	uint8_t buffer[MB_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_RadioConfig_size)];
	char blob_key[96];
	char indexed_key[96];
	size_t len = 0U;
	bool force = false;
	int rc;

	k_mutex_init(&ctx.mutex);
	mb_settings_blob_load_state_reset(&ctx.state, &ctx.staging, sizeof(ctx.staging));

	rc = mb_settings_blob_key_assemble(blob_key, sizeof(blob_key), &blob_schema);
	zassert_true(rc > 0, "blob key assemble failed: %d", rc);
	zassert_ok(mb_settings_blob_save_with_buffer(&blob_schema, &zero_cfg, buffer,
						     sizeof(buffer)),
		   "zero payload blob save failed");
	zassert_equal(settings_get_val_len(blob_key), MB_SETTINGS_BLOB_RECORD_HEADER_SIZE,
		      "zero payload blob should be stored as a non-empty record");

	zassert_ok(settings_load_subtree_direct(TEST_SETTINGS_SUBTREE, blob_load_cb, &ctx),
		   "zero payload blob subtree load failed");
	zassert_true(mb_settings_blob_commit_prepare(&blob_schema, &ctx.mutex, &ctx.state,
						     &ctx.staging, &got, NULL, &force),
		     "zero payload blob commit should see stored config");
	expect_config_eq(&got, &zero_cfg);

	rc = mb_settings_indexed_blob_key_assemble(indexed_key, sizeof(indexed_key),
						   &indexed_blob_schema, 3U);
	zassert_true(rc > 0, "indexed blob key assemble failed: %d", rc);
	zassert_ok(mb_settings_indexed_blob_save_with_buffer(&indexed_blob_schema, 3U,
							     &zero_cfg, buffer,
							     sizeof(buffer)),
		   "zero payload indexed blob save failed");
	zassert_ok(mb_settings_indexed_blob_load_encoded(&indexed_blob_schema, 3U, buffer,
							 sizeof(buffer), &len),
		   "zero payload indexed record load failed");
	zassert_equal(len, MB_SETTINGS_BLOB_RECORD_HEADER_SIZE,
		      "zero payload indexed blob should be stored as a non-empty record");
	got = test_config();
	zassert_ok(mb_settings_indexed_blob_load_with_buffer(&indexed_blob_schema, 3U, &got,
							     buffer, sizeof(buffer)),
		   "zero payload indexed blob decode failed");
	expect_config_eq(&got, &zero_cfg);
}

ZTEST(meshbus_common_settings_helpers, test_indexed_blob_and_raw_round_trip_and_export)
{
	meshbus_radio_config cfg = test_config();
	meshbus_radio_config got = meshbus_RadioConfig_init_zero;
	static const uint8_t raw_value[] = {0xaa, 0xbb, 0xcc, 0xdd};
	const struct exported_setting *exported;
	uint8_t buffer[MB_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_RadioConfig_size)];
	uint8_t raw_buffer[16];
	char blob_key[96];
	char raw_key[96];
	size_t raw_len = 0U;
	int rc;

	rc = mb_settings_indexed_blob_key_assemble(blob_key, sizeof(blob_key),
						   &indexed_blob_schema, 3U);
	zassert_true(rc > 0, "indexed blob key assemble failed: %d", rc);
	rc = mb_settings_indexed_raw_key_assemble(raw_key, sizeof(raw_key), &indexed_raw_schema,
						  4U);
	zassert_true(rc > 0, "indexed raw key assemble failed: %d", rc);

	zassert_ok(mb_settings_indexed_blob_save_with_buffer(&indexed_blob_schema, 3U, &cfg,
							     buffer, sizeof(buffer)),
		   "indexed blob save failed");
	zassert_ok(mb_settings_indexed_blob_load_with_buffer(&indexed_blob_schema, 3U, &got,
							     buffer, sizeof(buffer)),
		   "indexed blob load failed");
	expect_config_eq(&got, &cfg);

	zassert_ok(mb_settings_indexed_raw_save(&indexed_raw_schema, 4U, raw_value,
						sizeof(raw_value)),
		   "indexed raw save failed");
	zassert_ok(mb_settings_indexed_raw_load(&indexed_raw_schema, 4U, raw_buffer,
						sizeof(raw_buffer), &raw_len),
		   "indexed raw load failed");
	zassert_equal(raw_len, sizeof(raw_value), "indexed raw len mismatch");
	zassert_mem_equal(raw_buffer, raw_value, sizeof(raw_value), "indexed raw mismatch");

	reset_exported_settings();
	zassert_ok(mb_settings_indexed_blob_export_encoded(&indexed_blob_schema, 3U,
							   export_collect_cb, buffer,
							   sizeof(buffer)),
		   "indexed blob export failed");
	exported = export_find(blob_key);
	zassert_not_null(exported, "indexed blob key was not exported");
	zassert_true(exported->len > 0U, "indexed blob export should not be empty");

	zassert_ok(mb_settings_indexed_raw_export(&indexed_raw_schema, 4U, export_collect_cb,
						  raw_buffer, sizeof(raw_buffer)),
		   "indexed raw export failed");
	exported = export_find(raw_key);
	zassert_not_null(exported, "indexed raw key was not exported");
	zassert_equal(exported->len, sizeof(raw_value), "indexed raw export len mismatch");
	zassert_mem_equal(exported->value, raw_value, sizeof(raw_value),
			  "indexed raw export mismatch");
	zassert_equal(exported_setting_count, 2U, "unexpected indexed blob export count");
}

ZTEST_SUITE(meshbus_common_settings_helpers, NULL, suite_setup, test_before,
	    test_after, NULL);
