// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/meshbus/meshcore.h>
#include <zephyr/meshbus/radio.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

#define TEST_PARTITION_NODE DT_NODELABEL(storage_partition)
#define PRODUCT_PARTITION_NODE DT_NODELABEL(product_storage_partition)
#define SLOT0_PARTITION_NODE DT_NODELABEL(slot0_partition)
#define SLOT1_PARTITION_NODE DT_NODELABEL(slot1_partition)
#define RRAM_NODE DT_MEM_FROM_PARTITION(TEST_PARTITION_NODE)

/* C2 real-radio and real-storage service-DUT coverage. */

#define PARTITION_START(node_id) DT_REG_ADDR(node_id)
#define PARTITION_END(node_id) (DT_REG_ADDR(node_id) + DT_REG_SIZE(node_id))
#define STATE_SETTLE_MS 100U

static int test_storage_prepare_rc;

BUILD_ASSERT(!IS_ENABLED(CONFIG_FLASH_SIMULATOR),
             "C2 scenario must use real RRAM");
BUILD_ASSERT(IS_ENABLED(CONFIG_MESHBUS_RADIO_DEFAULT_RECEIVE_ONLY),
             "service-DUT scenario must never enable TX");
BUILD_ASSERT(CONFIG_MESHBUS_MESHCORE_DEFAULT_ROLE ==
                 MESHBUS_MESHCORE_ROLE_REPEATER,
             "service-DUT scenario must default to repeater role");
BUILD_ASSERT(DT_NODE_HAS_STATUS(TEST_PARTITION_NODE, okay),
             "test storage missing");
BUILD_ASSERT(DT_NODE_HAS_STATUS(PRODUCT_PARTITION_NODE, okay),
             "product storage guard missing");
BUILD_ASSERT(DT_REG_ADDR(TEST_PARTITION_NODE) == 0x164000,
             "wrong test storage base");
BUILD_ASSERT(DT_REG_SIZE(TEST_PARTITION_NODE) == 0x10000,
             "wrong test storage size");
BUILD_ASSERT(DT_REG_ADDR(PRODUCT_PARTITION_NODE) == 0x174000,
             "wrong product storage base");
BUILD_ASSERT(DT_REG_SIZE(PRODUCT_PARTITION_NODE) == 0x9000,
             "wrong product storage size");
BUILD_ASSERT(DT_PROP(RRAM_NODE, erase_block_size) == 4096,
             "unexpected erase alignment");
BUILD_ASSERT(DT_PROP(RRAM_NODE, write_block_size) == 16,
             "unexpected write alignment");
BUILD_ASSERT((DT_REG_ADDR(TEST_PARTITION_NODE) %
              DT_PROP(RRAM_NODE, erase_block_size)) == 0,
             "test storage base is not erase aligned");
BUILD_ASSERT((DT_REG_SIZE(TEST_PARTITION_NODE) %
              DT_PROP(RRAM_NODE, erase_block_size)) == 0,
             "test storage size is not erase aligned");
BUILD_ASSERT(PARTITION_END(SLOT0_PARTITION_NODE) <=
                 PARTITION_START(SLOT1_PARTITION_NODE),
             "slot0 overlaps slot1");
BUILD_ASSERT(PARTITION_END(SLOT1_PARTITION_NODE) <=
                 PARTITION_START(TEST_PARTITION_NODE),
             "slot1 overlaps test storage");
BUILD_ASSERT(PARTITION_END(TEST_PARTITION_NODE) <=
                 PARTITION_START(PRODUCT_PARTITION_NODE),
             "test storage overlaps product storage");
BUILD_ASSERT(PARTITION_END(PRODUCT_PARTITION_NODE) <= DT_REG_SIZE(RRAM_NODE),
             "product storage exceeds CPUAPP RRAM");

static int test_storage_flatten(void) {
  const struct flash_area *area = NULL;
  int rc = flash_area_open(DT_FIXED_PARTITION_ID(TEST_PARTITION_NODE), &area);

  if (rc == 0) {
    rc = flash_area_flatten(area, 0, area->fa_size);
    flash_area_close(area);
  }
  return rc;
}

static int meshcore_test_storage_prepare(void) {
  test_storage_prepare_rc = test_storage_flatten();
  return 0;
}

SYS_INIT(meshcore_test_storage_prepare, POST_KERNEL, 99);

static void expected_default_name(
    const meshbus_meshcore_config *cfg,
    char out[CONFIG_MESHBUS_MESHCORE_NAME_PUBKEY_PREFIX_BYTES * 2U + 1U]) {
  for (size_t i = 0U; i < CONFIG_MESHBUS_MESHCORE_NAME_PUBKEY_PREFIX_BYTES;
       i++) {
    (void)snprintf(&out[i * 2U], 3U, "%02X", cfg->public_key.bytes[i]);
  }
}

static void assert_radio_state(bool enabled, enum meshbus_radio_state state,
                               uint32_t min_sequence) {
  struct meshbus_radio_state_event event = {0};

  zassert_ok(zbus_chan_read(&meshbus_radio_state_chan, &event, K_NO_WAIT),
             "radio state channel read failed");
  zassert_equal(event.enabled, enabled, "radio enabled state mismatch");
  zassert_true(event.receive_only, "radio state permits TX");
  zassert_equal(event.state, state, "radio runtime state mismatch");
  zassert_true(event.sequence > min_sequence,
               "radio state sequence did not advance: %u", event.sequence);
}

ZTEST(meshbus_meshcore_service_dut, test_identity_and_radio_pause_resume) {
  meshbus_meshcore_config initial_cfg = meshbus_MeshcoreConfig_init_zero;
  meshbus_meshcore_config resumed_cfg = meshbus_MeshcoreConfig_init_zero;
  meshbus_radio_config radio_cfg = meshbus_RadioConfig_init_zero;
  struct meshbus_radio_state_event initial_radio = {0};
  struct meshbus_radio_state_event disabled_radio = {0};
  char expected_name[CONFIG_MESHBUS_MESHCORE_NAME_PUBKEY_PREFIX_BYTES * 2U +
                     1U] = {0};
  uint32_t initial_requests_accepted;
  uint32_t initial_requests_queue_full;
  uint32_t initial_requests_no_consumer;
  uint32_t initial_requests_submit_errors;

  zassert_ok(test_storage_prepare_rc, "test storage prepare failed: %d",
             test_storage_prepare_rc);
  zassert_equal(meshbus_meshcore_active_role_get(),
                MESHBUS_MESHCORE_ROLE_REPEATER,
                "firmware role is not repeater");
  zassert_true(meshbus_meshcore_runtime_is_ready(),
               "MeshCore runtime did not complete initialization");
  zassert_ok(meshbus_meshcore_config_get(&initial_cfg),
             "MeshCore config get failed");
  zassert_equal(initial_cfg.public_key.size, MESHBUS_MESHCORE_PUBLIC_KEY_SIZE,
                "public identity was not generated");
  zassert_equal(initial_cfg.private_key.size, MESHBUS_MESHCORE_PRIVATE_KEY_SIZE,
                "private identity was not generated");
  expected_default_name(&initial_cfg, expected_name);
  zassert_str_equal(initial_cfg.name, expected_name,
                    "default identity name mismatch");
  zassert_equal(initial_cfg.path_hash_size, 1U, "unexpected path hash size");
  initial_requests_accepted = meshbus_meshcore_stats.requests_accepted;
  initial_requests_queue_full = meshbus_meshcore_stats.requests_queue_full;
  initial_requests_no_consumer = meshbus_meshcore_stats.requests_no_consumer;
  initial_requests_submit_errors =
      meshbus_meshcore_stats.requests_submit_errors;

  zassert_ok(meshbus_radio_config_get(&radio_cfg), "radio config get failed");
  zassert_true(radio_cfg.enabled, "radio is not enabled");
  zassert_true(radio_cfg.receive_only, "radio is not receive-only");
  zassert_equal(meshbus_radio_state_get(), MESHBUS_RADIO_STATE_RECEIVE,
                "radio did not enter receive state");
  zassert_ok(
      zbus_chan_read(&meshbus_radio_state_chan, &initial_radio, K_NO_WAIT),
      "initial radio state read failed");
  printk("MB_MESHCORE_DUT_READY role=%u mode=receive_only name_len=%u\n",
         (unsigned int)meshbus_meshcore_active_role_get(),
         (unsigned int)strlen(initial_cfg.name));

  radio_cfg.enabled = false;
  zassert_ok(meshbus_radio_config_set(&radio_cfg), "radio disable failed");
  zassert_equal(meshbus_radio_state_get(), MESHBUS_RADIO_STATE_IDLE,
                "radio did not enter idle state");
  assert_radio_state(false, MESHBUS_RADIO_STATE_IDLE, initial_radio.sequence);
  k_sleep(K_MSEC(STATE_SETTLE_MS));
  zassert_ok(
      zbus_chan_read(&meshbus_radio_state_chan, &disabled_radio, K_NO_WAIT),
      "disabled radio state read failed");

  radio_cfg.enabled = true;
  radio_cfg.receive_only = true;
  zassert_ok(meshbus_radio_config_set(&radio_cfg), "radio re-enable failed");
  zassert_equal(meshbus_radio_state_get(), MESHBUS_RADIO_STATE_RECEIVE,
                "radio did not recover receive state");
  assert_radio_state(true, MESHBUS_RADIO_STATE_RECEIVE,
                     disabled_radio.sequence);
  k_sleep(K_MSEC(STATE_SETTLE_MS));

  zassert_ok(meshbus_meshcore_config_get(&resumed_cfg),
             "MeshCore config get after resume failed");
  zassert_mem_equal(resumed_cfg.public_key.bytes, initial_cfg.public_key.bytes,
                    MESHBUS_MESHCORE_PUBLIC_KEY_SIZE,
                    "public identity changed");
  zassert_mem_equal(
      resumed_cfg.private_key.bytes, initial_cfg.private_key.bytes,
      MESHBUS_MESHCORE_PRIVATE_KEY_SIZE, "private identity changed");
  zassert_str_equal(resumed_cfg.name, initial_cfg.name,
                    "identity name changed");
  zassert_equal(meshbus_meshcore_stats.requests_accepted,
                initial_requests_accepted,
                "accepted request count changed without requests");
  zassert_equal(meshbus_meshcore_stats.requests_queue_full,
                initial_requests_queue_full,
                "queue-full count changed without requests");
  zassert_equal(meshbus_meshcore_stats.requests_no_consumer,
                initial_requests_no_consumer,
                "no-consumer count changed without requests");
  zassert_equal(meshbus_meshcore_stats.requests_submit_errors,
                initial_requests_submit_errors,
                "submit-error count changed without requests");

  /* Leave the modem idle and the test-owned Settings region erased. */
  radio_cfg.enabled = false;
  zassert_ok(meshbus_radio_config_set(&radio_cfg),
             "final radio disable failed");
  k_sleep(K_MSEC(CONFIG_MESHBUS_SETTINGS_PERSISTENCE_DELAY + 50));
  zassert_ok(test_storage_flatten(), "test storage cleanup failed");
  printk("MB_MESHCORE_DUT_RESULT pass role=repeater identity=1 pause_resume=1 "
         "receive_only=1 cleanup=1\n");
}

ZTEST_SUITE(meshbus_meshcore_service_dut, NULL, NULL, NULL, NULL, NULL);
