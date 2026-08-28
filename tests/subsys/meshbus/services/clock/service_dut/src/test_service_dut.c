// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/meshbus/clock.h>
#include <zephyr/meshbus/time.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/ztest.h>

#define TEST_RETAINED_NODE   DT_ALIAS(clocktestretained)
#define TEST_RETAINED_REGION DT_PARENT(TEST_RETAINED_NODE)
#define TEST_STAGE_MAGIC     0x4d42434cU
#define TEST_TARGET_MS       1775000123456ULL

struct clock_test_stage {
	uint32_t magic;
	uint32_t inverse;
};

static const struct device *const retained = DEVICE_DT_GET(TEST_RETAINED_NODE);

BUILD_ASSERT(DT_NODE_HAS_STATUS(TEST_RETAINED_NODE, okay),
	     "clock retained-memory marker is missing");
BUILD_ASSERT(DT_REG_ADDR(TEST_RETAINED_REGION) == 0x2002e000, "wrong retained-memory base");
BUILD_ASSERT(DT_REG_SIZE(TEST_RETAINED_REGION) == 0x1000, "wrong retained-memory size");

static bool stage_valid(const struct clock_test_stage *stage)
{
	return stage->magic == TEST_STAGE_MAGIC && stage->inverse == ~TEST_STAGE_MAGIC;
}

ZTEST(meshbus_clock_service_dut, test_realtime_reboot_ownership)
{
	struct clock_test_stage stage = {0};
	uint64_t applied_ms = 0U;
	int rc;

	zassert_true(device_is_ready(retained), "retained-memory device is not ready");
	rc = retained_mem_read(retained, 0U, (uint8_t *)&stage, sizeof(stage));
	zassert_ok(rc, "retained stage read failed: %d", rc);

	if (!stage_valid(&stage)) {
		stage.magic = TEST_STAGE_MAGIC;
		stage.inverse = ~TEST_STAGE_MAGIC;
		zassert_ok(meshbus_clock_time_set_unix_ms(TEST_TARGET_MS, &applied_ms),
			   "C2 realtime set failed");
		zassert_true(applied_ms >= TEST_TARGET_MS, "applied realtime went backwards");
		zassert_true(meshbus_time_realtime_is_valid(),
			     "set realtime was not accepted as synchronized");
		zassert_ok(retained_mem_write(retained, 0U, (const uint8_t *)&stage, sizeof(stage)),
			   "retained stage write failed");
		printk("MB_CLOCK_HW_READY phase=reboot realtime_ms=%llu\n",
		       (unsigned long long)applied_ms);
		sys_reboot(SYS_REBOOT_WARM);
		zassert_unreachable("software reboot returned");
	}

	/* Meshbus owns no persistent wall-clock source. A reboot must require a
	 * fresh synchronization instead of silently reusing the prior offset.
	 */
	zassert_false(meshbus_time_realtime_is_valid(),
		      "reboot unexpectedly retained synchronized wall-clock state");
	zassert_ok(retained_mem_clear(retained), "retained stage cleanup failed");
	printk("MB_CLOCK_HW_RESULT pass reboot_requires_resync=1\n");
}

ZTEST_SUITE(meshbus_clock_service_dut, NULL, NULL, NULL, NULL, NULL);
