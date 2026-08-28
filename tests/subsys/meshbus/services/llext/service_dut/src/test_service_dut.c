// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <zephyr/meshbus/llext.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#include "service_api.h"

/* C2 real-storage and real-loader service-DUT coverage. */

#define SETTINGS_PARTITION_NODE DT_NODELABEL(storage_partition)
#define EXTRA_PARTITION_NODE    DT_NODELABEL(extra_partition)
#define STAGE_PARTITION_NODE    DT_NODELABEL(llext_test_stage_partition)
#define PRODUCT_PARTITION_NODE  DT_NODELABEL(product_storage_partition)
#define SLOT0_PARTITION_NODE    DT_NODELABEL(slot0_partition)
#define SLOT1_PARTITION_NODE    DT_NODELABEL(slot1_partition)
#define RRAM_NODE               DT_MEM_FROM_PARTITION(EXTRA_PARTITION_NODE)

#define PARTITION_START(node_id) DT_REG_ADDR(node_id)
#define PARTITION_END(node_id)   (DT_REG_ADDR(node_id) + DT_REG_SIZE(node_id))
#define TEST_STAGE_MAGIC         0x4d424c58U
#define TEST_STAGE_PREPARED      1U
#define EXIT_SERVICE_ID          "mb_exit"
#define EXIT_SERVICE_PATH        "/extra/svcs/mb_exit.mbs"
#define BAD_SERVICE_ID           "mb_badsym"
#define BAD_SERVICE_PATH         "/extra/svcs/mb_badsym.mbs"
#define TEST_APP_ID              "mb_app"
#define TEST_APP_PATH            "/extra/apps/mb_app.mba"
#define BAD_APP_PATH             "/extra/apps/mb_badsymapp.mba"

struct test_stage_record {
	uint32_t magic;
	uint32_t phase;
	uint32_t inverse;
	uint32_t reserved;
};

static struct test_stage_record test_stage;
static int test_storage_prepare_rc;
static atomic_t exit_count;
static atomic_t app_count;

BUILD_ASSERT(!IS_ENABLED(CONFIG_FLASH_SIMULATOR), "C2 scenario must use real RRAM");
BUILD_ASSERT(DT_NODE_HAS_STATUS(SETTINGS_PARTITION_NODE, okay), "test settings missing");
BUILD_ASSERT(DT_NODE_HAS_STATUS(EXTRA_PARTITION_NODE, okay), "test filesystem missing");
BUILD_ASSERT(DT_NODE_HAS_STATUS(STAGE_PARTITION_NODE, okay), "test stage missing");
BUILD_ASSERT(DT_NODE_HAS_STATUS(PRODUCT_PARTITION_NODE, okay), "product guard missing");
BUILD_ASSERT(DT_REG_ADDR(SETTINGS_PARTITION_NODE) == 0x164000, "wrong test settings base");
BUILD_ASSERT(DT_REG_SIZE(SETTINGS_PARTITION_NODE) == 0x3000, "wrong test settings size");
BUILD_ASSERT(DT_REG_ADDR(EXTRA_PARTITION_NODE) == 0x167000, "wrong test FS base");
BUILD_ASSERT(DT_REG_SIZE(EXTRA_PARTITION_NODE) == 0xc000, "wrong test FS size");
BUILD_ASSERT(DT_REG_ADDR(STAGE_PARTITION_NODE) == 0x173000, "wrong stage base");
BUILD_ASSERT(DT_REG_SIZE(STAGE_PARTITION_NODE) == 0x1000, "wrong stage size");
BUILD_ASSERT(DT_REG_ADDR(PRODUCT_PARTITION_NODE) == 0x174000, "wrong product storage base");
BUILD_ASSERT(DT_REG_SIZE(PRODUCT_PARTITION_NODE) == 0x9000, "wrong product storage size");
BUILD_ASSERT(DT_PROP(RRAM_NODE, erase_block_size) == 4096, "unexpected erase alignment");
BUILD_ASSERT(DT_PROP(RRAM_NODE, write_block_size) == 16, "unexpected write alignment");
BUILD_ASSERT((sizeof(struct test_stage_record) % DT_PROP(RRAM_NODE, write_block_size)) == 0,
	     "test stage record is not write aligned");
BUILD_ASSERT(PARTITION_END(SLOT0_PARTITION_NODE) <= PARTITION_START(SLOT1_PARTITION_NODE),
	     "slot0 overlaps slot1");
BUILD_ASSERT(PARTITION_END(SLOT1_PARTITION_NODE) <= PARTITION_START(SETTINGS_PARTITION_NODE),
	     "slot1 overlaps test settings");
BUILD_ASSERT(PARTITION_END(SETTINGS_PARTITION_NODE) <= PARTITION_START(EXTRA_PARTITION_NODE),
	     "test settings overlaps filesystem");
BUILD_ASSERT(PARTITION_END(EXTRA_PARTITION_NODE) <= PARTITION_START(STAGE_PARTITION_NODE),
	     "test filesystem overlaps stage");
BUILD_ASSERT(PARTITION_END(STAGE_PARTITION_NODE) <= PARTITION_START(PRODUCT_PARTITION_NODE),
	     "stage overlaps product storage");
BUILD_ASSERT(PARTITION_END(PRODUCT_PARTITION_NODE) <= DT_REG_SIZE(RRAM_NODE),
	     "product storage exceeds CPUAPP RRAM");

int mb_llext_test_hook(int event)
{
	switch (event) {
	case MB_LLEXT_TEST_EVT_EXIT:
		atomic_inc(&exit_count);
		return 0;
	case MB_LLEXT_TEST_EVT_APP:
		atomic_inc(&app_count);
		return 0;
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL(mb_llext_test_hook);

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
static const uint8_t exit_service_ext[] __aligned(4) = {
#include "mb_exit_service.inc"
};
static const uint8_t bad_symbol_service_ext[] __aligned(4) = {
#include "mb_badsym_service.inc"
};
#endif

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_APP_SERVICES)
static const uint8_t app_ext[] __aligned(4) = {
#include "mb_app.inc"
};
static const uint8_t bad_symbol_app_ext[] __aligned(4) = {
#include "mb_badsymapp.inc"
};
#endif

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_APP_SERVICES)
static uint32_t elapsed_us(uint64_t started)
{
	return (uint32_t)k_cyc_to_us_floor64(k_cycle_get_64() - started);
}
#endif

static int stage_write(void)
{
	const struct flash_area *area = NULL;
	struct test_stage_record next = {
		.magic = TEST_STAGE_MAGIC,
		.phase = TEST_STAGE_PREPARED,
		.inverse = ~TEST_STAGE_PREPARED,
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

static int flatten_partition(int partition_id)
{
	const struct flash_area *area;
	int rc = flash_area_open(partition_id, &area);

	if (rc == 0) {
		rc = flash_area_flatten(area, 0, area->fa_size);
		flash_area_close(area);
	}
	return rc;
}

static int llext_test_storage_prepare(void)
{
	int rc;

	rc = flatten_partition(DT_FIXED_PARTITION_ID(SETTINGS_PARTITION_NODE));
	if (rc == 0) {
		rc = flatten_partition(DT_FIXED_PARTITION_ID(EXTRA_PARTITION_NODE));
	}
	if (rc == 0) {
		rc = stage_write();
	}
	test_storage_prepare_rc = rc;
	return 0;
}

SYS_INIT(llext_test_storage_prepare, POST_KERNEL, 98);

static int write_file(const char *path, const uint8_t *data, size_t data_len)
{
	struct fs_file_t file;
	ssize_t wrote;
	int rc;

	fs_file_t_init(&file);
	rc = fs_open(&file, path, FS_O_CREATE | FS_O_TRUNC | FS_O_WRITE);
	if (rc != 0) {
		return rc;
	}
	wrote = fs_write(&file, data, data_len);
	if (wrote < 0) {
		(void)fs_close(&file);
		return (int)wrote;
	}
	if ((size_t)wrote != data_len) {
		(void)fs_close(&file);
		return -EIO;
	}
	return fs_close(&file);
}

static int ensure_directory(const char *path)
{
	struct fs_dirent entry;
	int rc = fs_stat(path, &entry);

	if (rc == 0) {
		return entry.type == FS_DIR_ENTRY_DIR ? 0 : -ENOTDIR;
	}
	if (rc != -ENOENT) {
		return rc;
	}

	rc = fs_mkdir(path);

	return rc == -EEXIST ? 0 : rc;
}

static void cleanup_file(const char *path)
{
	struct fs_dirent entry;
	int rc = fs_unlink(path);

	zassert_true(rc == 0 || rc == -ENOENT, "cleanup failed for %s: %d", path, rc);
	zassert_equal(fs_stat(path, &entry), -ENOENT, "cleanup left %s visible", path);
}

static void *suite_setup(void)
{
	zassert_ok(test_storage_prepare_rc, "test storage preparation failed: %d",
		   test_storage_prepare_rc);
	zassert_equal(test_stage.magic, TEST_STAGE_MAGIC, "test stage marker missing");
	zassert_ok(ensure_directory("/extra/svcs"), "service directory create failed");
	zassert_ok(ensure_directory("/extra/apps"), "app directory create failed");
	atomic_clear(&exit_count);
	atomic_clear(&app_count);

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
	zassert_ok(write_file(EXIT_SERVICE_PATH, exit_service_ext, sizeof(exit_service_ext)),
		   "valid service seed failed");
	zassert_ok(write_file(BAD_SERVICE_PATH, bad_symbol_service_ext,
			      sizeof(bad_symbol_service_ext)),
		   "bad service seed failed");
	k_sleep(K_MSEC(CONFIG_MESHBUS_LLEXT_DEFAULT_BOOT_DELAY + 1000));
#endif

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_APP_SERVICES)
	zassert_ok(write_file(TEST_APP_PATH, app_ext, sizeof(app_ext)), "valid app seed failed");
	zassert_ok(write_file(BAD_APP_PATH, bad_symbol_app_ext, sizeof(bad_symbol_app_ext)),
		   "bad app seed failed");
#endif

	return NULL;
}

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
ZTEST(meshbus_llext_service_dut, test_real_littlefs_boot_service_runtime)
{
	struct meshbus_llext_service_info info;
	size_t count;

	zassert_ok(meshbus_llext_service_count(&count), "service count failed");
	zassert_equal(count, 2U, "unexpected service count");
	zassert_ok(meshbus_llext_service_status(EXIT_SERVICE_ID, &info),
		   "returning service status failed");
	zassert_equal(info.state, MESHBUS_LLEXT_STATE_EXITED, "returning service did not teardown");
	zassert_equal(info.last_error, 0, "returning service error mismatch");
	zassert_true(strcmp(info.edk_version, "0.1.0") == 0,
		     "returning service EDK version mismatch");
	zassert_true(strcmp(info.target, CONFIG_BOARD_TARGET) == 0,
		     "returning service target mismatch");
	zassert_equal(atomic_get(&exit_count), 1, "returning service entry count mismatch");

	zassert_ok(meshbus_llext_service_status(BAD_SERVICE_ID, &info),
		   "bad service status failed");
	zassert_equal(info.state, MESHBUS_LLEXT_STATE_FAULTED,
		      "missing-symbol service state mismatch");
	zassert_equal(info.last_error, -ENOEXEC, "missing-symbol service error mismatch");

	cleanup_file(EXIT_SERVICE_PATH);
	cleanup_file(BAD_SERVICE_PATH);
	zassert_ok(stage_erase(), "stage cleanup failed");
	printk("MB_LLEXT_DUT_RESULT pass mode=boot services=%u exit_count=%ld cleanup=1\n",
	       (unsigned int)count, (long)atomic_get(&exit_count));
}
#endif

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_APP_SERVICES)
ZTEST(meshbus_llext_service_dut, test_real_littlefs_app_runtime)
{
	struct meshbus_llext_app_session *session = NULL;
	struct meshbus_llext_app_info info;
	meshbus_llext_app_entry_t entry;
	uint32_t first_load_us;
	uint32_t first_unload_us;
	uint64_t started;
	int rc;

	zassert_ok(meshbus_llext_app_probe(TEST_APP_PATH, &info), "app probe failed");
	zassert_true(strcmp(info.id, TEST_APP_ID) == 0, "app id mismatch");
	zassert_true(strcmp(info.edk_version, "0.1.0") == 0,
		     "app EDK version mismatch");
	zassert_true(strcmp(info.target, CONFIG_BOARD_TARGET) == 0, "app target mismatch");

	started = k_cycle_get_64();
	zassert_ok(meshbus_llext_app_load(TEST_APP_PATH, &session), "app load failed");
	first_load_us = elapsed_us(started);
	zassert_true(meshbus_llext_runtime_busy(), "loaded app did not own runtime");
	zassert_ok(meshbus_llext_app_get_entry(session, &entry), "app entry lookup failed");
	entry(NULL);
	zassert_equal(atomic_get(&app_count), 1, "app entry count mismatch");
	started = k_cycle_get_64();
	zassert_ok(meshbus_llext_app_unload(session), "app unload failed");
	first_unload_us = elapsed_us(started);
	zassert_false(meshbus_llext_runtime_busy(), "app unload left runtime busy");

	session = (void *)UINTPTR_MAX;
	rc = meshbus_llext_app_load(BAD_APP_PATH, &session);
	zassert_equal(rc, -ENOEXEC, "missing-symbol app should fail load");
	zassert_is_null(session, "failed app load did not clear session");
	zassert_false(meshbus_llext_runtime_busy(), "failed app load leaked runtime ownership");

	zassert_ok(meshbus_llext_app_load(TEST_APP_PATH, &session),
		   "valid app did not recover after failed load");
	zassert_ok(meshbus_llext_app_unload(session), "recovery app unload failed");
	zassert_false(meshbus_llext_runtime_busy(), "recovery unload left runtime busy");

	cleanup_file(TEST_APP_PATH);
	cleanup_file(BAD_APP_PATH);
	zassert_ok(stage_erase(), "stage cleanup failed");
	printk("MB_LLEXT_DUT_MEASUREMENT mode=app load_us=%u unload_us=%u\n", first_load_us,
	       first_unload_us);
	printk("MB_LLEXT_DUT_RESULT pass mode=app entry_count=%ld recovery=1 cleanup=1\n",
	       (long)atomic_get(&app_count));
}
#endif

ZTEST_SUITE(meshbus_llext_service_dut, NULL, suite_setup, NULL, NULL, NULL);
