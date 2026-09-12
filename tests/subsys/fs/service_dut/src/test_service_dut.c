// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>
#include <zephyr/init.h>
#include <fs/fs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/ztest.h>

#define TEST_PARTITION_NODE    DT_NODELABEL(extra_partition)
#define STAGE_PARTITION_NODE   DT_NODELABEL(fs_test_stage_partition)
#define PRODUCT_PARTITION_NODE DT_NODELABEL(product_storage_partition)
#define SLOT1_PARTITION_NODE   DT_NODELABEL(slot1_partition)
#define RRAM_NODE              DT_MEM_FROM_PARTITION(TEST_PARTITION_NODE)

#define PARTITION_END(node_id) (DT_REG_ADDR(node_id) + DT_REG_SIZE(node_id))
#define TEST_STAGE_MAGIC       0x4d424653U
#define TEST_STAGE_PREPARED    1U
#define TEST_STAGE_VERIFY      2U
#define TEST_DIR               MBS_FS_GAMES_PATH "/c2"
#define TEST_FILE              TEST_DIR "/persist.bin"

struct test_stage_record {
	uint32_t magic;
	uint32_t phase;
	uint32_t inverse;
	uint32_t reserved;
};

static struct test_stage_record test_stage;
static int test_storage_prepare_rc;

BUILD_ASSERT(!IS_ENABLED(CONFIG_FLASH_SIMULATOR), "C2 scenario must use real RRAM");
BUILD_ASSERT(DT_NODE_HAS_STATUS(TEST_PARTITION_NODE, okay), "test filesystem missing");
BUILD_ASSERT(DT_NODE_HAS_STATUS(STAGE_PARTITION_NODE, okay), "test stage missing");
BUILD_ASSERT(DT_NODE_HAS_STATUS(PRODUCT_PARTITION_NODE, okay), "product storage guard missing");
BUILD_ASSERT(DT_REG_ADDR(TEST_PARTITION_NODE) == 0x164000, "wrong test FS base");
BUILD_ASSERT(DT_REG_SIZE(TEST_PARTITION_NODE) == 0xf000, "wrong test FS size");
BUILD_ASSERT(DT_REG_ADDR(STAGE_PARTITION_NODE) == 0x173000, "wrong stage base");
BUILD_ASSERT(DT_REG_SIZE(STAGE_PARTITION_NODE) == 0x1000, "wrong stage size");
BUILD_ASSERT(DT_REG_ADDR(PRODUCT_PARTITION_NODE) == 0x174000, "wrong product storage base");
BUILD_ASSERT(DT_REG_SIZE(PRODUCT_PARTITION_NODE) == 0x9000, "wrong product storage size");
BUILD_ASSERT(DT_PROP(RRAM_NODE, erase_block_size) == 4096, "unexpected erase alignment");
BUILD_ASSERT(DT_PROP(RRAM_NODE, write_block_size) == 16, "unexpected write alignment");
BUILD_ASSERT((sizeof(struct test_stage_record) % DT_PROP(RRAM_NODE, write_block_size)) == 0,
	     "test stage record is not write aligned");
BUILD_ASSERT((DT_REG_ADDR(TEST_PARTITION_NODE) % DT_PROP(RRAM_NODE, erase_block_size)) == 0,
	     "test filesystem base is not erase aligned");
BUILD_ASSERT((DT_REG_SIZE(TEST_PARTITION_NODE) % DT_PROP(RRAM_NODE, erase_block_size)) == 0,
	     "test filesystem size is not erase aligned");
BUILD_ASSERT(PARTITION_END(SLOT1_PARTITION_NODE) <= DT_REG_ADDR(TEST_PARTITION_NODE),
	     "slot1 overlaps test filesystem");
BUILD_ASSERT(PARTITION_END(TEST_PARTITION_NODE) <= DT_REG_ADDR(STAGE_PARTITION_NODE),
	     "test filesystem overlaps stage");
BUILD_ASSERT(PARTITION_END(STAGE_PARTITION_NODE) <= DT_REG_ADDR(PRODUCT_PARTITION_NODE),
	     "stage overlaps product storage");
BUILD_ASSERT(PARTITION_END(PRODUCT_PARTITION_NODE) <= DT_REG_SIZE(RRAM_NODE),
	     "product storage exceeds CPUAPP RRAM");

static bool stage_valid(const struct test_stage_record *stage)
{
	return stage->magic == TEST_STAGE_MAGIC &&
	       (stage->phase == TEST_STAGE_PREPARED || stage->phase == TEST_STAGE_VERIFY) &&
	       stage->inverse == ~stage->phase;
}

static int stage_write(uint32_t phase)
{
	const struct flash_area *area = NULL;
	struct test_stage_record next = {
		.magic = TEST_STAGE_MAGIC,
		.phase = phase,
		.inverse = ~phase,
	};
	int rc = flash_area_open(DT_FIXED_PARTITION_ID(STAGE_PARTITION_NODE), &area);

	if (rc == 0) {
		rc = flash_area_flatten(area, 0, area->fa_size);
	}
	if (rc == 0) {
		rc = flash_area_write(area, 0, &next, sizeof(next));
	}
	if (area != NULL) {
		flash_area_close(area);
	}
	if (rc == 0) {
		test_stage = next;
	}
	return rc;
}

static int stage_erase(void)
{
	const struct flash_area *area;
	int rc = flash_area_open(DT_FIXED_PARTITION_ID(STAGE_PARTITION_NODE), &area);

	if (rc == 0) {
		rc = flash_area_flatten(area, 0, area->fa_size);
		flash_area_close(area);
	}
	return rc;
}

static int fs_test_storage_prepare(void)
{
	const struct flash_area *area = NULL;
	int rc = flash_area_open(DT_FIXED_PARTITION_ID(STAGE_PARTITION_NODE), &area);

	if (rc == 0) {
		rc = flash_area_read(area, 0, &test_stage, sizeof(test_stage));
		flash_area_close(area);
	}
	if (rc != 0) {
		test_storage_prepare_rc = rc;
		return 0;
	}
	if (stage_valid(&test_stage)) {
		return 0;
	}

	rc = flash_area_open(DT_FIXED_PARTITION_ID(TEST_PARTITION_NODE), &area);
	if (rc == 0) {
		rc = flash_area_flatten(area, 0, area->fa_size);
		flash_area_close(area);
	}
	if (rc == 0) {
		rc = stage_write(TEST_STAGE_PREPARED);
	}
	test_storage_prepare_rc = rc;
	return 0;
}

SYS_INIT(fs_test_storage_prepare, POST_KERNEL, 98);

static void write_payload(void)
{
	static const uint8_t payload[] = {0x4d, 0x42, 0x46, 0x53};
	struct fs_file_t file;
	int rc;

	fs_file_t_init(&file);
	rc = fs_open(&file, TEST_FILE, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	zassert_ok(rc, "test file open failed: %d", rc);
	rc = fs_write(&file, payload, sizeof(payload));
	zassert_equal(rc, sizeof(payload), "test file write failed: %d", rc);
	zassert_ok(fs_close(&file), "test file close failed");
}

static void verify_payload(void)
{
	static const uint8_t expected[] = {0x4d, 0x42, 0x46, 0x53};
	uint8_t payload[sizeof(expected)] = {0};
	struct fs_file_t file;
	int rc;

	fs_file_t_init(&file);
	rc = fs_open(&file, TEST_FILE, FS_O_READ);
	zassert_ok(rc, "persisted file open failed: %d", rc);
	rc = fs_read(&file, payload, sizeof(payload));
	zassert_equal(rc, sizeof(payload), "persisted file read failed: %d", rc);
	zassert_mem_equal(payload, expected, sizeof(expected), "persisted bytes mismatch");
	zassert_ok(fs_close(&file), "persisted file close failed");
}

ZTEST(mbs_fs_service_dut, test_real_littlefs_reboot_format_and_cleanup)
{
	struct mbs_fs_volume_status status;
	struct mbs_fs_entry entry;

	zassert_ok(test_storage_prepare_rc, "test storage preparation failed: %d",
		   test_storage_prepare_rc);
	zassert_true(stage_valid(&test_stage), "test stage marker is invalid");
	zassert_ok(mbs_fs_volume_status("extra", &status), "volume status failed");
	zassert_true(status.mounted && status.has_capacity, "test filesystem is not mounted");

	if (test_stage.phase == TEST_STAGE_PREPARED) {
		zassert_ok(mbs_fs_mkdir(TEST_DIR), "test directory create failed");
		write_payload();
		zassert_ok(stage_write(TEST_STAGE_VERIFY), "verify stage write failed");
		printk("MBS_FS_HW_READY phase=persisted total=%llu free=%llu\n",
		       (unsigned long long)status.total_bytes,
		       (unsigned long long)status.free_bytes);
		sys_reboot(SYS_REBOOT_WARM);
		zassert_unreachable("software reboot returned");
	}

	verify_payload();
	zassert_ok(mbs_fs_format("extra", MBS_FS_FORMAT_CONFIRM),
		   "test filesystem format failed");
	zassert_equal(mbs_fs_stat(TEST_FILE, &entry), -ENOENT, "format left test file visible");
	zassert_ok(mbs_fs_stat(MBS_FS_GAMES_PATH, &entry),
		   "format did not recreate product directories");
	zassert_ok(stage_erase(), "stage cleanup failed");
	printk("MBS_FS_HW_RESULT pass formatted=1 product_dirs=1\n");
}

ZTEST_SUITE(mbs_fs_service_dut, NULL, NULL, NULL, NULL, NULL);
