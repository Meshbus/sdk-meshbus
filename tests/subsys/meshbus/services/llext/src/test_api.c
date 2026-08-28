// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <zephyr/meshbus/llext.h>
#include <zephyr/settings/settings.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#if defined(CONFIG_MESHBUS_TELEMETRY)
#include <zephyr/meshbus/telemetry.h>
#endif

#include "service_api.h"

/* Public API and lifecycle contract coverage for the simulated backend. */

#define TEST_SERVICE_ID "mb_test"
#define TEST_SERVICE_PATH "/extra/svcs/" TEST_SERVICE_ID ".mbs"
#define EXIT_SERVICE_ID "mb_exit"
#define EXIT_SERVICE_PATH "/extra/svcs/" EXIT_SERVICE_ID ".mbs"
#define BAD_MAGIC_SERVICE_ID "mb_badmagic"
#define BAD_MAGIC_SERVICE_PATH "/extra/svcs/" BAD_MAGIC_SERVICE_ID ".mbs"
#define BAD_TARGET_SERVICE_ID "mb_badtarget"
#define BAD_TARGET_SERVICE_PATH "/extra/svcs/" BAD_TARGET_SERVICE_ID ".mbs"
#define OLD_METADATA_SERVICE_ID "mb_oldmeta"
#define OLD_METADATA_SERVICE_PATH "/extra/svcs/" OLD_METADATA_SERVICE_ID ".mbs"
#define BAD_SYMBOL_SERVICE_ID "mb_badsym"
#define BAD_SYMBOL_SERVICE_PATH "/extra/svcs/" BAD_SYMBOL_SERVICE_ID ".mbs"
#define TEST_APP_ID "mb_app"
#define TEST_APP_PATH "/extra/apps/" TEST_APP_ID ".mba"
#define OTHER_EDK_APP_PATH "/extra/apps/mb_otheredk.mba"
#define OLDER_EDK_APP_PATH "/extra/apps/mb_olderedk.mba"
#define BAD_TARGET_APP_PATH "/extra/apps/mb_badtargetapp.mba"
#define OLD_METADATA_APP_PATH "/extra/apps/mb_oldmeta.mba"
#define BAD_RESERVED_APP_PATH "/extra/apps/mb_badreserved.mba"
#define BIG_HEAP_APP_ID "mb_bigapp"
#define BIG_HEAP_APP_PATH "/extra/apps/" BIG_HEAP_APP_ID ".mba"
#define BAD_ICON_APP_ID "mb_badicon"
#define BAD_ICON_APP_PATH "/extra/apps/" BAD_ICON_APP_ID ".mba"
#define BAD_SYMBOL_APP_ID "mb_badsymapp"
#define BAD_SYMBOL_APP_PATH "/extra/apps/" BAD_SYMBOL_APP_ID ".mba"
#define TRUNCATED_APP_PATH "/extra/apps/truncated.mba"
#define IGNORED_FILE_PATH "/extra/svcs/ignored.llext"
#define IGNORED_APP_IN_SERVICE_PATH "/extra/svcs/ignored_app.mba"
#define LLEXT_BOOT_DELAY_INVALID 60001U

static atomic_t setup_count;
static atomic_t exit_count;
static atomic_t app_count;
static bool extra_fs_ready;

int mb_llext_test_hook(int event)
{
	switch (event) {
	case MB_LLEXT_TEST_EVT_SETUP:
		atomic_inc(&setup_count);
		return 0;
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
static const uint8_t test_service_ext[] __aligned(4) = {
	#include "mb_test_service.inc"
};
static const uint8_t exit_service_ext[] __aligned(4) = {
	#include "mb_exit_service.inc"
};
static const uint8_t bad_magic_service_ext[] __aligned(4) = {
	#include "mb_badmagic_service.inc"
};
static const uint8_t bad_target_service_ext[] __aligned(4) = {
	#include "mb_badtarget_service.inc"
};
static const uint8_t old_metadata_service_ext[] __aligned(4) = {
	#include "mb_oldmeta_service.inc"
};
static const uint8_t bad_symbol_service_ext[] __aligned(4) = {
	#include "mb_badsym_service.inc"
};
#endif
#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_APP_SERVICES)
static const uint8_t app_ext[] __aligned(4) = {
	#include "mb_app.inc"
};
static const uint8_t other_edk_app_ext[] __aligned(4) = {
	#include "mb_otheredk_app.inc"
};
static const uint8_t older_edk_app_ext[] __aligned(4) = {
	#include "mb_olderedk_app.inc"
};
static const uint8_t bad_target_app_ext[] __aligned(4) = {
	#include "mb_badtarget_app.inc"
};
static const uint8_t old_metadata_app_ext[] __aligned(4) = {
	#include "mb_oldmeta_app.inc"
};
static const uint8_t bad_reserved_app_ext[] __aligned(4) = {
	#include "mb_badreserved_app.inc"
};
static const uint8_t big_app_ext[] __aligned(4) = {
	#include "mb_bigapp.inc"
};
static const uint8_t bad_icon_app_ext[] __aligned(4) = {
	#include "mb_badicon.inc"
};
static const uint8_t bad_symbol_app_ext[] __aligned(4) = {
	#include "mb_badsymapp.inc"
};
#endif

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(extra_storage);
static struct fs_mount_t extra_mp = {
	.type = FS_LITTLEFS,
	.fs_data = &extra_storage,
	.storage_dev = (void *)DT_PARTITION_ID(DT_NODELABEL(extra_partition)),
	.mnt_point = "/extra",
};

static int ensure_extra_fs_ready(void)
{
	int rc;

	if (extra_fs_ready) {
		return 0;
	}

	rc = fs_mount(&extra_mp);
	if ((rc != 0) && (rc != -EBUSY)) {
		return rc;
	}

	rc = fs_mkdir("/extra/svcs");
	if ((rc != 0) && (rc != -EEXIST)) {
		return rc;
	}
	rc = fs_mkdir("/extra/apps");
	if ((rc != 0) && (rc != -EEXIST)) {
		return rc;
	}

	extra_fs_ready = true;
	return 0;
}

static void cleanup_service_dir(void)
{
	struct fs_dir_t dir;
	struct fs_dirent entry;
	char path[MESHBUS_LLEXT_PATH_MAX_LEN + 1];
	int rc;

	fs_dir_t_init(&dir);
	rc = fs_opendir(&dir, "/extra/svcs");
	if (rc != 0) {
		return;
	}

	while (true) {
		rc = fs_readdir(&dir, &entry);
		if (rc != 0 || entry.name[0] == '\0') {
			break;
		}
		if (entry.type != FS_DIR_ENTRY_FILE) {
			continue;
		}
		if (snprintk(path, sizeof(path), "/extra/svcs/%s", entry.name) >=
		    sizeof(path)) {
			continue;
		}
		(void)fs_unlink(path);
	}

	(void)fs_closedir(&dir);
}

static void cleanup_app_dir(void)
{
	struct fs_dir_t dir;
	struct fs_dirent entry;
	char path[MESHBUS_LLEXT_PATH_MAX_LEN + 1];
	int rc;

	fs_dir_t_init(&dir);
	rc = fs_opendir(&dir, "/extra/apps");
	if (rc != 0) {
		return;
	}

	while (true) {
		rc = fs_readdir(&dir, &entry);
		if (rc != 0 || entry.name[0] == '\0') {
			break;
		}
		if (entry.type != FS_DIR_ENTRY_FILE) {
			continue;
		}
		if (snprintk(path, sizeof(path), "/extra/apps/%s", entry.name) >=
		    sizeof(path)) {
			continue;
		}
		(void)fs_unlink(path);
	}

	(void)fs_closedir(&dir);
}

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

static void *suite_setup(void)
{
#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
	static const uint8_t ignored[] = {0x7f, 'E', 'L', 'F'};
#endif

	zassert_ok(ensure_extra_fs_ready(), "extra fs not ready");
	cleanup_service_dir();
	cleanup_app_dir();
	atomic_clear(&setup_count);
	atomic_clear(&exit_count);
	atomic_clear(&app_count);

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
	zassert_ok(write_file(TEST_SERVICE_PATH, test_service_ext, sizeof(test_service_ext)));
	zassert_ok(write_file(EXIT_SERVICE_PATH, exit_service_ext, sizeof(exit_service_ext)));
	zassert_ok(write_file(BAD_MAGIC_SERVICE_PATH, bad_magic_service_ext,
			      sizeof(bad_magic_service_ext)));
	zassert_ok(write_file(BAD_TARGET_SERVICE_PATH, bad_target_service_ext,
			      sizeof(bad_target_service_ext)));
	zassert_ok(write_file(OLD_METADATA_SERVICE_PATH, old_metadata_service_ext,
			      sizeof(old_metadata_service_ext)));
	zassert_ok(write_file(BAD_SYMBOL_SERVICE_PATH, bad_symbol_service_ext,
			      sizeof(bad_symbol_service_ext)));
	zassert_ok(write_file(IGNORED_FILE_PATH, ignored, sizeof(ignored)));
	zassert_ok(write_file(IGNORED_APP_IN_SERVICE_PATH, ignored, sizeof(ignored)));
#endif
#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_APP_SERVICES)
	zassert_ok(write_file(TEST_APP_PATH, app_ext, sizeof(app_ext)));
	zassert_ok(write_file(OTHER_EDK_APP_PATH, other_edk_app_ext,
			      sizeof(other_edk_app_ext)));
	zassert_ok(write_file(OLDER_EDK_APP_PATH, older_edk_app_ext,
			      sizeof(older_edk_app_ext)));
	zassert_ok(write_file(BAD_TARGET_APP_PATH, bad_target_app_ext,
			      sizeof(bad_target_app_ext)));
	zassert_ok(write_file(OLD_METADATA_APP_PATH, old_metadata_app_ext,
			      sizeof(old_metadata_app_ext)));
	zassert_ok(write_file(BAD_RESERVED_APP_PATH, bad_reserved_app_ext,
			      sizeof(bad_reserved_app_ext)));
	zassert_ok(write_file(BIG_HEAP_APP_PATH, big_app_ext, sizeof(big_app_ext)));
	zassert_ok(write_file(BAD_ICON_APP_PATH, bad_icon_app_ext, sizeof(bad_icon_app_ext)));
	zassert_ok(write_file(BAD_SYMBOL_APP_PATH, bad_symbol_app_ext,
			      sizeof(bad_symbol_app_ext)));
	zassert_true(sizeof(app_ext) > 16U, "test app artifact unexpectedly short");
	zassert_ok(write_file(TRUNCATED_APP_PATH, app_ext, 16U));
#endif

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
	k_sleep(K_MSEC(CONFIG_MESHBUS_LLEXT_DEFAULT_BOOT_DELAY + 800));
#endif
	return NULL;
}

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
ZTEST(meshbus_llext_contract, test_boot_loads_valid_services)
{
	struct meshbus_llext_service_info info;
	size_t count;
	int rc;

	rc = meshbus_llext_service_count(&count);
	zassert_ok(rc, "service count failed: %d", rc);
	zassert_equal(count, 3U, "unexpected service count");

	rc = meshbus_llext_service_status(TEST_SERVICE_ID, &info);
	zassert_ok(rc, "status failed: %d", rc);
	zassert_equal(info.state, MESHBUS_LLEXT_STATE_RUNNING, "service not running");
	zassert_equal(info.stack_size, 1024U, "stack size mismatch");
	zassert_equal(info.heap_size, 16384U, "heap size mismatch");
	zassert_true(strcmp(info.edk_version, "0.1.0") == 0,
		     "service EDK version mismatch");
	zassert_true(atomic_get(&setup_count) > 0, "service entry did not run");
}

ZTEST(meshbus_llext_contract, test_boot_mode_rejects_desktop_apps)
{
	struct meshbus_llext_app_session *session = (void *)UINTPTR_MAX;
	struct meshbus_llext_app_info info;
	int rc;

	rc = meshbus_llext_app_probe(TEST_APP_PATH, &info);
	zassert_equal(rc, -ENOTSUP, "boot-service mode must reject app probe");

	rc = meshbus_llext_app_load(TEST_APP_PATH, &session);
	zassert_equal(rc, -ENOTSUP, "boot-service mode must reject app load");
	zassert_is_null(session, "rejected app load must clear session output");
}
#endif

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_APP_SERVICES)
ZTEST(meshbus_llext_contract, test_app_probe_load_entry_unload)
{
	struct meshbus_llext_app_session *session;
	struct meshbus_llext_app_info info;
	meshbus_llext_app_entry_t entry;
	atomic_val_t app_count_before;
	int rc;

	rc = meshbus_llext_app_probe(TEST_APP_PATH, &info);
	zassert_ok(rc, "app probe failed: %d", rc);
	zassert_mem_equal(info.id, TEST_APP_ID, strlen(TEST_APP_ID), "app id mismatch");
	zassert_equal(info.stack_size, 1024U, "app stack size mismatch");
	zassert_equal(info.heap_size, 32768U, "app heap size mismatch");
	zassert_equal(info.icon_data_size, MESHBUS_LLEXT_APP_ICON_DATA_SIZE,
		      "app icon size mismatch");
	zassert_equal(info.icon_data[0], 0x00, "app icon byte 0 mismatch");
	zassert_equal(info.icon_data[1], 0x03, "app icon byte 1 mismatch");

	rc = meshbus_llext_app_load(TEST_APP_PATH, &session);
	zassert_ok(rc, "app load failed: %d", rc);
	zassert_not_null(session, "app session missing");
	rc = meshbus_llext_app_get_entry(session, &entry);
	zassert_ok(rc, "app get entry failed: %d", rc);
	zassert_not_null(entry, "app entry missing");

	app_count_before = atomic_get(&app_count);
	entry(NULL);
	zassert_equal(atomic_get(&app_count), app_count_before + 1,
		      "app entry did not run exactly once");

	rc = meshbus_llext_app_unload(session);
	zassert_ok(rc, "app unload failed: %d", rc);
	rc = meshbus_llext_app_unload(session);
	zassert_equal(rc, -ENOENT, "second app unload should report no session");
}

ZTEST(meshbus_llext_contract, test_app_probe_uses_edk_version_as_provenance)
{
	struct meshbus_llext_app_info info;
	int rc;

	rc = meshbus_llext_app_probe(OTHER_EDK_APP_PATH, &info);
	zassert_ok(rc, "different EDK version should not fail probe");
	zassert_true(strcmp(info.edk_version, "9.0.0") == 0,
		     "different EDK version not exposed");
	rc = meshbus_llext_app_probe(OLDER_EDK_APP_PATH, &info);
	zassert_ok(rc, "older EDK version should not fail probe");
	zassert_true(strcmp(info.edk_version, "0.0.1") == 0,
		     "older EDK version not exposed");
	rc = meshbus_llext_app_probe(BAD_TARGET_APP_PATH, &info);
	zassert_equal(rc, -EXDEV, "wrong target should fail probe");
	rc = meshbus_llext_app_probe(OLD_METADATA_APP_PATH, &info);
	zassert_equal(rc, -EPROTONOSUPPORT, "old metadata version should fail probe");
	rc = meshbus_llext_app_probe(BAD_RESERVED_APP_PATH, &info);
	zassert_equal(rc, -ENOEXEC, "nonzero compatibility reserved bytes should fail probe");
	rc = meshbus_llext_app_probe(BAD_ICON_APP_PATH, &info);
	zassert_equal(rc, -ENOEXEC, "bad app icon size should fail probe");
	rc = meshbus_llext_app_probe("/extra/apps/not_app.mbs", &info);
	zassert_equal(rc, -EINVAL, "wrong app suffix should fail probe");
}

ZTEST(meshbus_llext_contract, test_app_rejects_malformed_and_invalid_inputs)
{
	struct meshbus_llext_app_session *session = (void *)UINTPTR_MAX;
	struct meshbus_llext_app_info info;
	char oversized_path[MESHBUS_LLEXT_PATH_MAX_LEN + 6U];
	int rc;

	memset(oversized_path, 'a', sizeof(oversized_path));
	memcpy(oversized_path, "/extra/apps/", strlen("/extra/apps/"));
	memcpy(&oversized_path[sizeof(oversized_path) - 5U], ".mba", 5U);

	rc = meshbus_llext_app_probe(TRUNCATED_APP_PATH, &info);
	zassert_equal(rc, -ENOEXEC, "truncated ELF should fail probe");
	rc = meshbus_llext_app_load(TRUNCATED_APP_PATH, &session);
	zassert_equal(rc, -ENOEXEC, "truncated ELF should fail load");
	zassert_is_null(session, "failed load must clear session output");

	rc = meshbus_llext_app_probe(NULL, &info);
	zassert_equal(rc, -EINVAL, "NULL path should fail probe");
	rc = meshbus_llext_app_probe(TEST_APP_PATH, NULL);
	zassert_equal(rc, -EINVAL, "NULL info should fail probe");
	rc = meshbus_llext_app_load(oversized_path, &session);
	zassert_equal(rc, -EINVAL, "oversized path should fail load");
	rc = meshbus_llext_app_load(TEST_APP_PATH, NULL);
	zassert_equal(rc, -EINVAL, "NULL session output should fail load");
	rc = meshbus_llext_app_get_info(NULL, &info);
	zassert_equal(rc, -EINVAL, "NULL session should fail info lookup");
	rc = meshbus_llext_app_get_entry(NULL, NULL);
	zassert_equal(rc, -EINVAL, "NULL session should fail entry lookup");
	rc = meshbus_llext_app_unload(NULL);
	zassert_equal(rc, -EINVAL, "NULL session should fail unload");
}

ZTEST(meshbus_llext_contract, test_app_probe_load_rejects_disabled_llext)
{
	struct meshbus_llext_app_session *session;
	struct meshbus_llext_app_info info;
	meshbus_llext_config cfg;
	int load_rc;
	int probe_rc;
	int rc;

	rc = meshbus_llext_config_get(&cfg);
	zassert_ok(rc, "config get failed: %d", rc);
	cfg.enabled = false;
	rc = meshbus_llext_config_set(&cfg);
	zassert_ok(rc, "disable config failed: %d", rc);

	probe_rc = meshbus_llext_app_probe(TEST_APP_PATH, &info);
	load_rc = meshbus_llext_app_load(TEST_APP_PATH, &session);

	cfg.enabled = true;
	rc = meshbus_llext_config_set(&cfg);
	zassert_ok(rc, "enable config restore failed: %d", rc);

	zassert_equal(probe_rc, -ENODEV, "disabled LLEXT should reject app probe");
	zassert_equal(info.last_error, -ENODEV, "disabled probe should report last_error");
	zassert_equal(load_rc, -ENODEV, "disabled LLEXT should reject app load");
}

ZTEST(meshbus_llext_contract, test_app_load_revalidates_and_enforces_heap_budget)
{
	struct meshbus_llext_app_session *session;
	struct meshbus_llext_app_info info;
	int rc;

	rc = meshbus_llext_app_probe(TEST_APP_PATH, &info);
	zassert_ok(rc, "initial probe failed: %d", rc);
	zassert_ok(fs_unlink(TEST_APP_PATH), "failed to remove probed app");
	rc = meshbus_llext_app_load(TEST_APP_PATH, &session);
	zassert_equal(rc, -ENOENT, "load should re-read and fail missing app");
	zassert_ok(write_file(TEST_APP_PATH, app_ext, sizeof(app_ext)));

	rc = meshbus_llext_app_load(BIG_HEAP_APP_PATH, &session);
	zassert_equal(rc, -E2BIG, "large app heap should be rejected");
}

ZTEST(meshbus_llext_contract, test_app_duplicate_load_and_failure_cleanup_recover)
{
	struct meshbus_llext_app_session *session;
	struct meshbus_llext_app_session *duplicate = (void *)UINTPTR_MAX;
	int rc;

	rc = meshbus_llext_app_load(TEST_APP_PATH, &session);
	zassert_ok(rc, "initial app load failed: %d", rc);
	zassert_true(meshbus_llext_runtime_busy(), "loaded app should mark runtime busy");
	rc = meshbus_llext_app_load(TEST_APP_PATH, &duplicate);
	zassert_equal(rc, -EBUSY, "duplicate app load should report busy");
	zassert_is_null(duplicate, "busy load must clear session output");
	zassert_ok(meshbus_llext_app_unload(session), "initial app unload failed");
	zassert_false(meshbus_llext_runtime_busy(), "unload should release runtime resources");

	rc = meshbus_llext_app_load(BAD_SYMBOL_APP_PATH, &session);
	zassert_equal(rc, -ENOEXEC, "missing app entry symbol should fail load");
	zassert_is_null(session, "failed symbol load must clear session output");
	zassert_false(meshbus_llext_runtime_busy(),
		      "failed load should release runtime resources");

	rc = meshbus_llext_app_load(TEST_APP_PATH, &session);
	zassert_ok(rc, "valid load should recover after failed load: %d", rc);
	zassert_ok(meshbus_llext_app_unload(session), "recovery app unload failed");
	zassert_false(meshbus_llext_runtime_busy(),
		      "recovery unload should release runtime resources");
}
#endif

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
ZTEST(meshbus_llext_contract, test_returning_service_is_marked_exited)
{
	struct meshbus_llext_service_info info;
	int rc;

	k_sleep(K_MSEC(100));
	rc = meshbus_llext_service_status(EXIT_SERVICE_ID, &info);
	zassert_ok(rc, "status failed: %d", rc);
	zassert_equal(info.state, MESHBUS_LLEXT_STATE_EXITED, "returning service state mismatch");
	zassert_true(atomic_get(&exit_count) > 0, "returning service did not run");
}

ZTEST(meshbus_llext_contract, test_invalid_metadata_is_not_registered)
{
	struct meshbus_llext_service_info info;
	int rc;

	rc = meshbus_llext_service_status(BAD_MAGIC_SERVICE_ID, &info);
	zassert_equal(rc, -ENOENT, "bad magic service should be skipped");
	rc = meshbus_llext_service_status(BAD_TARGET_SERVICE_ID, &info);
	zassert_equal(rc, -ENOENT, "bad target service should be skipped");
	rc = meshbus_llext_service_status(OLD_METADATA_SERVICE_ID, &info);
	zassert_equal(rc, -ENOENT, "old metadata service should be skipped");
}

ZTEST(meshbus_llext_contract, test_missing_entry_symbol_faults)
{
	struct meshbus_llext_service_info info;
	int rc;

	rc = meshbus_llext_service_status(BAD_SYMBOL_SERVICE_ID, &info);
	zassert_ok(rc, "status failed: %d", rc);
	zassert_equal(info.state, MESHBUS_LLEXT_STATE_FAULTED, "bad symbol state mismatch");
	zassert_equal(info.last_error, -ENOEXEC, "bad symbol error mismatch");
}

ZTEST(meshbus_llext_contract, test_service_get_and_config)
{
	struct meshbus_llext_service_info info;
	meshbus_llext_config cfg;
	size_t count;
	int rc;

	rc = meshbus_llext_service_count(&count);
	zassert_ok(rc, "service count failed: %d", rc);
	zassert_equal(count, 3U, "unexpected service count");

	rc = meshbus_llext_service_get(0, &info);
	zassert_ok(rc, "service get failed: %d", rc);
	rc = meshbus_llext_service_get(count, &info);
	zassert_equal(rc, -ENOENT, "out-of-range get should fail");
	rc = meshbus_llext_service_get(0, NULL);
	zassert_equal(rc, -EINVAL, "NULL get should fail");
	rc = meshbus_llext_service_count(NULL);
	zassert_equal(rc, -EINVAL, "NULL count should fail");

	rc = meshbus_llext_config_get(&cfg);
	zassert_ok(rc, "config get failed: %d", rc);
	zassert_true(cfg.enabled, "config enabled mismatch");
	zassert_equal(cfg.boot_delay, CONFIG_MESHBUS_LLEXT_DEFAULT_BOOT_DELAY,
		      "boot delay mismatch");

	cfg.boot_delay = LLEXT_BOOT_DELAY_INVALID;
	rc = meshbus_llext_config_set(&cfg);
	zassert_equal(rc, -EINVAL, "invalid config should fail");
}
#endif

#if IS_ENABLED(CONFIG_MESHBUS_TELEMETRY)
ZTEST(meshbus_llext_contract, test_telemetry_bridge_is_subscribe_only)
{
	struct meshbus_telemetry_data_event event = {
		.chan = SENSOR_CHAN_AMBIENT_TEMP,
		.value_count = 1U,
	};
	struct meshbus_telemetry_data_event readback;
	struct k_event subscriber;
	uint64_t mask = MESHBUS_LLEXT_ZBUS_CH_BIT(MESHBUS_LLEXT_ZBUS_TELEMETRY_DATA_CHAN);

	k_event_init(&subscriber);
	zassert_ok(meshbus_llext_zbus_subscribe(&subscriber, mask));
	zassert_ok(meshbus_llext_zbus_read(MESHBUS_LLEXT_ZBUS_TELEMETRY_DATA_CHAN, &readback,
				      sizeof(readback)));
	zassert_equal(meshbus_llext_zbus_publish(MESHBUS_LLEXT_ZBUS_TELEMETRY_DATA_CHAN,
					    &event, sizeof(event)),
		      -ENOTSUP);
	zassert_ok(meshbus_llext_zbus_unsubscribe(&subscriber));
}
#endif

ZTEST_SUITE(meshbus_llext_contract, NULL, suite_setup, NULL, NULL, NULL);
