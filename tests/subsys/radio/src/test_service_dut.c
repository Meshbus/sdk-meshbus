// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <radio/radio.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

#define TEST_PARTITION_NODE DT_NODELABEL(storage_partition)
#define PRODUCT_PARTITION_NODE DT_NODELABEL(product_storage_partition)
#define SLOT0_PARTITION_NODE DT_NODELABEL(slot0_partition)
#define SLOT1_PARTITION_NODE DT_NODELABEL(slot1_partition)
#define RRAM_NODE DT_MEM_FROM_PARTITION(TEST_PARTITION_NODE)

#define PARTITION_START(node_id) DT_REG_ADDR(node_id)
#define PARTITION_END(node_id) (DT_REG_ADDR(node_id) + DT_REG_SIZE(node_id))
/*
 * Physical RSSI reads can dominate the configured work reschedule period.
 * Keep the DUT deadline bounded, but leave enough room for three real samples.
 */
#define CALIBRATION_TIMEOUT_MS 5000U

static int test_storage_prepare_rc;
static bool service_dut_result_ready;

BUILD_ASSERT(!IS_ENABLED(CONFIG_FLASH_SIMULATOR),
	     "C2 scenario must use real RRAM");
BUILD_ASSERT(!IS_ENABLED(CONFIG_MBS_RADIO_DEFAULT_ENABLED),
	     "service-DUT scenario must start with the radio disabled");
BUILD_ASSERT(IS_ENABLED(CONFIG_MBS_RADIO_DEFAULT_RECEIVE_ONLY),
	     "service-DUT scenario must never enable TX");
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

static int radio_test_storage_prepare(void) {
  test_storage_prepare_rc = test_storage_flatten();
  return 0;
}

SYS_INIT(radio_test_storage_prepare, POST_KERNEL, 99);

static void radio_service_dut_after(void *fixture)
{
	int rc;

	ARG_UNUSED(fixture);

	rc = mbs_radio_config_reset();
	zassert_ok(rc, "radio cleanup reset failed: %d", rc);
	k_sleep(K_MSEC(CONFIG_MBS_SETTINGS_PERSISTENCE_DELAY + 50));
	zassert_equal(mbs_radio_state_get(), MBS_RADIO_STATE_IDLE,
		      "radio cleanup did not leave the modem idle");

	if (service_dut_result_ready) {
		printk("MBS_RADIO_HW_RESULT pass receive_only=1 calibration=1 "
		       "recovery=1 cleanup=1\n");
	}
	service_dut_result_ready = false;
}

static void assert_state_event(bool enabled, bool receive_only,
                               enum mbs_radio_state state,
                               uint32_t min_sequence) {
  struct mbs_radio_state_event event = {0};

  zassert_ok(zbus_chan_read(&mbs_radio_state_chan, &event, K_NO_WAIT),
             "state channel read failed");
  zassert_equal(event.enabled, enabled, "enabled state mismatch");
  zassert_equal(event.receive_only, receive_only,
                "receive-only state mismatch");
  zassert_equal(event.state, state, "runtime state mismatch");
  zassert_true(event.sequence > min_sequence,
               "state sequence did not advance: %u", event.sequence);
}

ZTEST(mbs_radio_service_dut, test_receive_only_driver_state_and_recovery) {
  mbs_radio_config cfg = meshbus_RadioConfig_init_zero;
  struct mbs_radio_state_event initial = {0};
  uint32_t airtime_ms;
  uint32_t calibration_started;
  int16_t noise_floor;
  int16_t rssi = 0;

  service_dut_result_ready = false;
  zassert_ok(test_storage_prepare_rc, "test storage prepare failed: %d",
             test_storage_prepare_rc);
  zassert_ok(mbs_radio_config_get(&cfg), "radio config get failed");
  zassert_false(cfg.enabled, "radio default must remain disabled");
  zassert_true(cfg.receive_only, "radio default is not receive-only");
  zassert_equal(mbs_radio_state_get(), MBS_RADIO_STATE_IDLE,
                "radio did not start idle");
  zassert_ok(zbus_chan_read(&mbs_radio_state_chan, &initial, K_NO_WAIT),
             "initial state channel read failed");
  zassert_false(initial.enabled, "initial state event is enabled");
  zassert_true(initial.receive_only, "initial state event permits TX");
  zassert_equal(initial.state, MBS_RADIO_STATE_IDLE,
                "initial state event is not idle");

  cfg.enabled = true;
  zassert_ok(mbs_radio_config_set(&cfg), "radio enable failed");
  zassert_equal(mbs_radio_state_get(), MBS_RADIO_STATE_RECEIVE,
                "radio did not enter receive state");
  assert_state_event(true, true, MBS_RADIO_STATE_RECEIVE,
                     initial.sequence);

  printk("MBS_RADIO_HW_READY mode=receive_only frequency_hz=%llu\n",
         (unsigned long long)cfg.frequency);
  zassert_ok(mbs_radio_rssi_inst(&rssi), "instantaneous RSSI read failed");
  zassert_true(rssi >= -200 && rssi <= 0, "RSSI outside plausible range: %d",
               rssi);
  airtime_ms = mbs_radio_airtime(32U);
  zassert_true(airtime_ms > 0U, "airtime calculation failed");

  mbs_radio_noise_calibrate(6);
  calibration_started = k_uptime_get_32();
  do {
    noise_floor = mbs_radio_noise_floor();
    if (noise_floor != 0) {
      break;
    }
    k_sleep(K_MSEC(25));
  } while ((k_uptime_get_32() - calibration_started) < CALIBRATION_TIMEOUT_MS);
  zassert_true(noise_floor >= -200 && noise_floor < 0,
               "noise calibration did not complete: %d", noise_floor);
  printk(
      "MBS_RADIO_HW_MEASUREMENT rssi_dbm=%d noise_floor_dbm=%d airtime_ms=%u\n",
      rssi, noise_floor, airtime_ms);

  cfg.enabled = false;
  zassert_ok(mbs_radio_config_set(&cfg), "radio disable failed");
  zassert_equal(mbs_radio_state_get(), MBS_RADIO_STATE_IDLE,
                "radio did not enter idle state");
  assert_state_event(false, true, MBS_RADIO_STATE_IDLE, initial.sequence);

  struct mbs_radio_state_event disabled = {0};
  zassert_ok(zbus_chan_read(&mbs_radio_state_chan, &disabled, K_NO_WAIT),
             "disabled state channel read failed");
  cfg.enabled = true;
  cfg.receive_only = true;
  zassert_ok(mbs_radio_config_set(&cfg), "radio re-enable failed");
  zassert_equal(mbs_radio_state_get(), MBS_RADIO_STATE_RECEIVE,
                "radio did not recover receive state");
  assert_state_event(true, true, MBS_RADIO_STATE_RECEIVE,
                     disabled.sequence);
  mbs_radio_agc_reset();
  zassert_equal(mbs_radio_state_get(), MBS_RADIO_STATE_RECEIVE,
                "AGC reset did not restore receive state");
  zassert_ok(mbs_radio_rssi_inst(&rssi), "RSSI read after recovery failed");

  /* Leave the modem idle; the suite after-hook resets persisted config. */
  cfg.enabled = false;
  zassert_ok(mbs_radio_config_set(&cfg), "final radio disable failed");
  service_dut_result_ready = true;
}

ZTEST_SUITE(mbs_radio_service_dut, NULL, NULL, NULL,
	    radio_service_dut_after, NULL);
