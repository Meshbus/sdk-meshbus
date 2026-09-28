/* SPDX-License-Identifier: Apache-2.0 */
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
#include <llext/llext.h>
#include <llext/zbus.h>
#include <input/input.h>
#include <zephyr/settings/settings.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#if defined(CONFIG_MBS_TELEMETRY)
#include <telemetry/telemetry.h>
#endif
#if defined(CONFIG_MBS_NOTIFY)
#include <notify/notify.h>
#endif

#include "app_api.h"

/* Public API and lifecycle contract coverage for the simulated backend. */

#define TEST_APP_ID "mbs_app"
#define TEST_APP_PATH "/extra/apps/" TEST_APP_ID ".mba"
#define OTHER_EDK_APP_PATH "/extra/apps/mbs_otheredk.mba"
#define OLDER_EDK_APP_PATH "/extra/apps/mbs_olderedk.mba"
#define BAD_TARGET_APP_PATH "/extra/apps/mbs_badtargetapp.mba"
#define UNSUPPORTED_METADATA_APP_PATH "/extra/apps/mbs_unsupportedmeta.mba"
#define BAD_RESERVED_APP_PATH "/extra/apps/mbs_badreserved.mba"
#define BIG_HEAP_APP_ID "mbs_bigapp"
#define BIG_HEAP_APP_PATH "/extra/apps/" BIG_HEAP_APP_ID ".mba"
#define BAD_ICON_APP_ID "mbs_badicon"
#define BAD_ICON_APP_PATH "/extra/apps/" BAD_ICON_APP_ID ".mba"
#define BAD_SYMBOL_APP_ID "mbs_badsymapp"
#define BAD_SYMBOL_APP_PATH "/extra/apps/" BAD_SYMBOL_APP_ID ".mba"
#define TRUNCATED_APP_PATH "/extra/apps/truncated.mba"
#define PERIPHERAL_APP_PATH "/extra/apps/mbs_peripherals.mba"

static const uint8_t peripheral_ext[] __aligned(4) = {
	#include "mbs_peripherals.inc"
};

static atomic_t app_count;
static bool extra_fs_ready;

int mbs_llext_test_hook(int event)
{
	switch (event) {
	case MBS_LLEXT_TEST_EVT_APP:
		atomic_inc(&app_count);
		return 0;
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL(mbs_llext_test_hook);

static const uint8_t app_ext[] __aligned(4) = {
	#include "mbs_app.inc"
};
static const uint8_t other_edk_app_ext[] __aligned(4) = {
	#include "mbs_otheredk_app.inc"
};
static const uint8_t older_edk_app_ext[] __aligned(4) = {
	#include "mbs_olderedk_app.inc"
};
static const uint8_t bad_target_app_ext[] __aligned(4) = {
	#include "mbs_badtarget_app.inc"
};
static const uint8_t unsupported_metadata_app_ext[] __aligned(4) = {
	#include "mbs_unsupportedmeta_app.inc"
};
static const uint8_t bad_reserved_app_ext[] __aligned(4) = {
	#include "mbs_badreserved_app.inc"
};
static const uint8_t big_app_ext[] __aligned(4) = {
	#include "mbs_bigapp.inc"
};
static const uint8_t bad_icon_app_ext[] __aligned(4) = {
	#include "mbs_badicon.inc"
};
static const uint8_t bad_symbol_app_ext[] __aligned(4) = {
	#include "mbs_badsymapp.inc"
};

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

	rc = fs_mkdir("/extra/apps");
	if ((rc != 0) && (rc != -EEXIST)) {
		return rc;
	}

	extra_fs_ready = true;
	return 0;
}

static void cleanup_app_dir(void)
{
	struct fs_dir_t dir;
	struct fs_dirent entry;
	char path[MBS_LLEXT_PATH_MAX_LEN + 1];
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

	zassert_ok(ensure_extra_fs_ready(), "extra fs not ready");
	cleanup_app_dir();
	atomic_clear(&app_count);

	zassert_ok(write_file(TEST_APP_PATH, app_ext, sizeof(app_ext)));
	zassert_ok(write_file(PERIPHERAL_APP_PATH, peripheral_ext, sizeof(peripheral_ext)));
	zassert_ok(write_file(OTHER_EDK_APP_PATH, other_edk_app_ext,
			      sizeof(other_edk_app_ext)));
	zassert_ok(write_file(OLDER_EDK_APP_PATH, older_edk_app_ext,
			      sizeof(older_edk_app_ext)));
	zassert_ok(write_file(BAD_TARGET_APP_PATH, bad_target_app_ext,
			      sizeof(bad_target_app_ext)));
	zassert_ok(write_file(UNSUPPORTED_METADATA_APP_PATH, unsupported_metadata_app_ext,
			      sizeof(unsupported_metadata_app_ext)));
	zassert_ok(write_file(BAD_RESERVED_APP_PATH, bad_reserved_app_ext,
			      sizeof(bad_reserved_app_ext)));
	zassert_ok(write_file(BIG_HEAP_APP_PATH, big_app_ext, sizeof(big_app_ext)));
	zassert_ok(write_file(BAD_ICON_APP_PATH, bad_icon_app_ext, sizeof(bad_icon_app_ext)));
	zassert_ok(write_file(BAD_SYMBOL_APP_PATH, bad_symbol_app_ext,
			      sizeof(bad_symbol_app_ext)));
	zassert_true(sizeof(app_ext) > 16U, "test app artifact unexpectedly short");
	zassert_ok(write_file(TRUNCATED_APP_PATH, app_ext, 16U));

	return NULL;
}

ZTEST(mbs_llext_contract, test_native_peripheral_device_calls)
{
	struct mbs_llext_app_session *session;
	mbs_llext_app_entry_t entry;
	struct peripheral_result result = {0};

	zassert_ok(mbs_llext_app_load(PERIPHERAL_APP_PATH, &session));
	zassert_ok(mbs_llext_app_get_entry(session, &entry));
	entry(&result);
	zassert_ok(mbs_llext_app_unload(session));
	zassert_false(mbs_llext_runtime_busy());

	zassert_equal(result.ready_mask, 0xf, "all four host devices must resolve");
	zassert_ok(result.i2c_rc);
	zassert_equal(result.i2c_value, 0xa5);
	zassert_ok(result.spi_rc);
	zassert_equal(result.spi_value, 0xc3);
	zassert_ok(result.gpio_rc);
	zassert_equal(result.gpio_value, 1);
	zassert_ok(result.adc_setup_rc);
	zassert_ok(result.adc_read_rc);
	zassert_equal(result.adc_value, 1234);
	zassert_ok(result.adc_mv_rc);
	zassert_equal(result.adc_mv, 1084);
}

ZTEST(mbs_llext_contract, test_app_probe_load_entry_unload)
{
	struct mbs_llext_app_session *session;
	struct mbs_llext_app_info info;
	mbs_llext_app_entry_t entry;
	atomic_val_t app_count_before;
	int rc;

	rc = mbs_llext_app_probe(TEST_APP_PATH, &info);
	zassert_ok(rc, "app probe failed: %d", rc);
	zassert_mem_equal(info.id, TEST_APP_ID, strlen(TEST_APP_ID), "app id mismatch");
	zassert_equal(info.stack_size, 1024U, "app stack size mismatch");
	zassert_equal(info.heap_size, 32768U, "app heap size mismatch");
	zassert_equal(info.icon_data_size, MBS_LLEXT_APP_ICON_DATA_SIZE,
		      "app icon size mismatch");
	zassert_equal(info.icon_data[0], 0x00, "app icon byte 0 mismatch");
	zassert_equal(info.icon_data[1], 0x03, "app icon byte 1 mismatch");

	rc = mbs_llext_app_load(TEST_APP_PATH, &session);
	zassert_ok(rc, "app load failed: %d", rc);
	zassert_not_null(session, "app session missing");
	rc = mbs_llext_app_get_entry(session, &entry);
	zassert_ok(rc, "app get entry failed: %d", rc);
	zassert_not_null(entry, "app entry missing");

	app_count_before = atomic_get(&app_count);
	entry(NULL);
	zassert_equal(atomic_get(&app_count), app_count_before + 1,
		      "app entry did not run exactly once");

	rc = mbs_llext_app_unload(session);
	zassert_ok(rc, "app unload failed: %d", rc);
	rc = mbs_llext_app_unload(session);
	zassert_equal(rc, -ENOENT, "second app unload should report no session");
}

static void assert_rejected_before_execution(const char *path)
{
	struct mbs_llext_app_session *session = (void *)UINTPTR_MAX;
	struct mbs_llext_app_info info;
	atomic_val_t before = atomic_get(&app_count);

	zassert_equal(mbs_llext_app_probe(path, &info), -EPROTONOSUPPORT,
		      "%s must fail probe", path);
	zassert_equal(mbs_llext_app_load(path, &session), -EPROTONOSUPPORT,
		      "%s must fail load", path);
	zassert_is_null(session);
	zassert_equal(atomic_get(&app_count), before, "rejected constructor ran");
	zassert_false(mbs_llext_runtime_busy(), "rejected app consumed the only session");
	zassert_ok(mbs_llext_app_load(TEST_APP_PATH, &session), "valid app must load after rejection");
	zassert_ok(mbs_llext_app_unload(session));
}

ZTEST(mbs_llext_contract, test_app_rejects_unsupported_metadata_version)
{
	assert_rejected_before_execution(UNSUPPORTED_METADATA_APP_PATH);
}

ZTEST(mbs_llext_contract, test_app_loads_different_edk_versions)
{
	struct mbs_llext_app_session *session;
	mbs_llext_app_entry_t entry;
	const char *paths[] = {OLDER_EDK_APP_PATH, OTHER_EDK_APP_PATH};

	for (size_t i = 0; i < ARRAY_SIZE(paths); i++) {
		zassert_ok(mbs_llext_app_load(paths[i], &session));
		zassert_ok(mbs_llext_app_get_entry(session, &entry));
		entry(NULL);
		zassert_ok(mbs_llext_app_unload(session));
	}
}

ZTEST(mbs_llext_contract, test_app_probe_uses_edk_version_as_provenance)
{
	struct mbs_llext_app_info info;
	int rc;

	rc = mbs_llext_app_probe(OTHER_EDK_APP_PATH, &info);
	zassert_ok(rc, "different EDK version should not fail probe");
	zassert_true(strcmp(info.edk_version, "9.0.0") == 0,
		     "different EDK version not exposed");
	rc = mbs_llext_app_probe(OLDER_EDK_APP_PATH, &info);
	zassert_ok(rc, "older EDK version should not fail probe");
	zassert_true(strcmp(info.edk_version, "0.0.1") == 0,
		     "older EDK version not exposed");
	rc = mbs_llext_app_probe(BAD_TARGET_APP_PATH, &info);
	zassert_equal(rc, -EXDEV, "wrong target should fail probe");
	rc = mbs_llext_app_probe(BAD_RESERVED_APP_PATH, &info);
	zassert_equal(rc, -ENOEXEC, "nonzero reserved bytes should fail probe");
	rc = mbs_llext_app_probe(BAD_ICON_APP_PATH, &info);
	zassert_equal(rc, -ENOEXEC, "bad app icon size should fail probe");
	rc = mbs_llext_app_probe("/extra/apps/not_app.bin", &info);
	zassert_equal(rc, -EINVAL, "wrong app suffix should fail probe");
}

ZTEST(mbs_llext_contract, test_app_rejects_malformed_and_invalid_inputs)
{
	struct mbs_llext_app_session *session = (void *)UINTPTR_MAX;
	struct mbs_llext_app_info info;
	char oversized_path[MBS_LLEXT_PATH_MAX_LEN + 6U];
	int rc;

	memset(oversized_path, 'a', sizeof(oversized_path));
	memcpy(oversized_path, "/extra/apps/", strlen("/extra/apps/"));
	memcpy(&oversized_path[sizeof(oversized_path) - 5U], ".mba", 5U);

	rc = mbs_llext_app_probe(TRUNCATED_APP_PATH, &info);
	zassert_equal(rc, -ENOEXEC, "truncated ELF should fail probe");
	rc = mbs_llext_app_load(TRUNCATED_APP_PATH, &session);
	zassert_equal(rc, -ENOEXEC, "truncated ELF should fail load");
	zassert_is_null(session, "failed load must clear session output");

	rc = mbs_llext_app_probe(NULL, &info);
	zassert_equal(rc, -EINVAL, "NULL path should fail probe");
	rc = mbs_llext_app_probe(TEST_APP_PATH, NULL);
	zassert_equal(rc, -EINVAL, "NULL info should fail probe");
	rc = mbs_llext_app_load(oversized_path, &session);
	zassert_equal(rc, -EINVAL, "oversized path should fail load");
	rc = mbs_llext_app_load(TEST_APP_PATH, NULL);
	zassert_equal(rc, -EINVAL, "NULL session output should fail load");
	rc = mbs_llext_app_get_info(NULL, &info);
	zassert_equal(rc, -EINVAL, "NULL session should fail info lookup");
	rc = mbs_llext_app_get_entry(NULL, NULL);
	zassert_equal(rc, -EINVAL, "NULL session should fail entry lookup");
	rc = mbs_llext_app_unload(NULL);
	zassert_equal(rc, -EINVAL, "NULL session should fail unload");
}

ZTEST(mbs_llext_contract, test_app_probe_load_rejects_disabled_llext)
{
	struct mbs_llext_app_session *session;
	struct mbs_llext_app_info info;
	mbs_llext_config cfg;
	int load_rc;
	int probe_rc;
	int rc;

	rc = mbs_llext_config_get(&cfg);
	zassert_ok(rc, "config get failed: %d", rc);
	cfg.enabled = false;
	rc = mbs_llext_config_set(&cfg);
	zassert_ok(rc, "disable config failed: %d", rc);

	probe_rc = mbs_llext_app_probe(TEST_APP_PATH, &info);
	load_rc = mbs_llext_app_load(TEST_APP_PATH, &session);

	cfg.enabled = true;
	rc = mbs_llext_config_set(&cfg);
	zassert_ok(rc, "enable config restore failed: %d", rc);

	zassert_equal(probe_rc, -ENODEV, "disabled LLEXT should reject app probe");
	zassert_equal(info.last_error, -ENODEV, "disabled probe should report last_error");
	zassert_equal(load_rc, -ENODEV, "disabled LLEXT should reject app load");
}

ZTEST(mbs_llext_contract, test_config_reset_and_disable_preserve_loaded_app)
{
	struct mbs_llext_app_session *session;
	struct mbs_llext_app_info info;
	mbs_llext_app_entry_t entry;
	mbs_llext_config cfg;
	atomic_val_t count;

	zassert_equal(mbs_llext_config_get(NULL), -EINVAL);
	zassert_equal(mbs_llext_config_set(NULL), -EINVAL);
	zassert_ok(mbs_llext_app_load(TEST_APP_PATH, &session));
	zassert_ok(mbs_llext_config_get(&cfg));
	cfg.enabled = false;
	zassert_ok(mbs_llext_config_set(&cfg));
	zassert_equal(mbs_llext_app_probe(TEST_APP_PATH, &info), -ENODEV);
	zassert_true(mbs_llext_runtime_busy());
	zassert_ok(mbs_llext_app_get_entry(session, &entry));
	count = atomic_get(&app_count);
	entry(NULL);
	zassert_equal(atomic_get(&app_count), count + 1);
	zassert_ok(mbs_llext_app_unload(session));
	zassert_false(mbs_llext_runtime_busy());
	zassert_ok(mbs_llext_config_reset());
	zassert_ok(mbs_llext_config_get(&cfg));
	zassert_equal(cfg.enabled, IS_ENABLED(CONFIG_MBS_LLEXT_DEFAULT_ENABLED));
	zassert_ok(mbs_llext_app_load(TEST_APP_PATH, &session));
	zassert_ok(mbs_llext_app_unload(session));
}

ZTEST(mbs_llext_contract, test_app_load_revalidates_and_enforces_heap_budget)
{
	struct mbs_llext_app_session *session;
	struct mbs_llext_app_info info;
	int rc;

	rc = mbs_llext_app_probe(TEST_APP_PATH, &info);
	zassert_ok(rc, "initial probe failed: %d", rc);
	zassert_ok(fs_unlink(TEST_APP_PATH), "failed to remove probed app");
	rc = mbs_llext_app_load(TEST_APP_PATH, &session);
	zassert_equal(rc, -ENOENT, "load should re-read and fail missing app");
	zassert_ok(write_file(TEST_APP_PATH, app_ext, sizeof(app_ext)));

	rc = mbs_llext_app_load(BIG_HEAP_APP_PATH, &session);
	zassert_equal(rc, -E2BIG, "large app heap should be rejected");
}

ZTEST(mbs_llext_contract, test_app_duplicate_load_and_failure_cleanup_recover)
{
	struct mbs_llext_app_session *session;
	struct mbs_llext_app_session *duplicate = (void *)UINTPTR_MAX;
	int rc;

	rc = mbs_llext_app_load(TEST_APP_PATH, &session);
	zassert_ok(rc, "initial app load failed: %d", rc);
	zassert_true(mbs_llext_runtime_busy(), "loaded app should mark runtime busy");
	rc = mbs_llext_app_load(TEST_APP_PATH, &duplicate);
	zassert_equal(rc, -EBUSY, "duplicate app load should report busy");
	zassert_is_null(duplicate, "busy load must clear session output");
	zassert_ok(mbs_llext_app_unload(session), "initial app unload failed");
	zassert_false(mbs_llext_runtime_busy(), "unload should release runtime resources");

	rc = mbs_llext_app_load(BAD_SYMBOL_APP_PATH, &session);
	zassert_equal(rc, -ENOEXEC, "missing app entry symbol should fail load");
	zassert_is_null(session, "failed symbol load must clear session output");
	zassert_false(mbs_llext_runtime_busy(),
		      "failed load should release runtime resources");

	rc = mbs_llext_app_load(TEST_APP_PATH, &session);
	zassert_ok(rc, "valid load should recover after failed load: %d", rc);
	zassert_ok(mbs_llext_app_unload(session), "recovery app unload failed");
	zassert_false(mbs_llext_runtime_busy(),
		      "recovery unload should release runtime resources");
}

#if IS_ENABLED(CONFIG_MBS_NOTIFY)
static K_SEM_DEFINE(bridge_work_done, 0, 1);

static void bridge_barrier_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	k_sem_give(&bridge_work_done);
}

static K_WORK_DEFINE(bridge_barrier, bridge_barrier_handler);

static void bridge_flush_notifications(void)
{
	/* Fence notifications already submitted to the system workqueue. */
	k_sem_reset(&bridge_work_done);
	zassert_true(k_work_submit(&bridge_barrier) >= 0);
	zassert_ok(k_sem_take(&bridge_work_done, K_SECONDS(1)),
		   "bridge notification work did not finish");
}

static void bridge_publish_notify(void)
{
	mbs_notify payload = meshbus_Notify_init_zero;

	payload.which_payload_variant = MBS_NOTIFY_TAG_CHANNEL;
	zassert_ok(mbs_notify_publish(MBS_NOTIFY_TYPE_CHANNELS_CHANGED, &payload));
}

static void bridge_publish_input(void)
{
	struct mbs_input_act_event event = {0};

	zassert_ok(mbs_llext_zbus_publish(MBS_LLEXT_ZBUS_INPUT_ACTION_CHAN,
					     &event, sizeof(event)));
}

ZTEST(mbs_llext_contract, test_bridge_pending_snapshot_and_subscriber_masks)
{
	const uint64_t notify_bit =
		MBS_LLEXT_ZBUS_CH_BIT(MBS_LLEXT_ZBUS_NOTIFY_CHAN);
	const uint64_t input_bit =
		MBS_LLEXT_ZBUS_CH_BIT(MBS_LLEXT_ZBUS_INPUT_ACTION_CHAN);
	static struct k_event all_channels;
	static struct k_event notify_only;
	uint64_t pending = UINT64_MAX;

	k_event_init(&all_channels);
	k_event_init(&notify_only);
	zassert_ok(mbs_llext_zbus_subscribe(&all_channels, notify_bit | input_bit));
	zassert_ok(mbs_llext_zbus_subscribe(&notify_only, notify_bit));
	bridge_publish_notify();
	bridge_publish_notify();
	bridge_publish_input();
	bridge_flush_notifications();

	zassert_equal(k_event_wait(&all_channels, MBS_LLEXT_ZBUS_EVT_PENDING,
				   false, K_NO_WAIT), MBS_LLEXT_ZBUS_EVT_PENDING);
	zassert_ok(mbs_llext_zbus_take_pending(&all_channels, &pending));
	zassert_equal(pending, notify_bit | input_bit, "snapshot lost a pending channel");
	(void)k_event_clear(&all_channels, MBS_LLEXT_ZBUS_EVT_PENDING);
	zassert_equal(mbs_llext_zbus_take_pending(&all_channels, &pending), -ENOMSG);
	zassert_equal(pending, 0ULL, "empty snapshot did not clear output");
	zassert_ok(mbs_llext_zbus_take_pending(&notify_only, &pending));
	zassert_equal(pending, notify_bit, "subscriber mask leaked another channel");

	/* A publication after the snapshot must remain available to the next take. */
	bridge_publish_input();
	bridge_flush_notifications();
	zassert_equal(k_event_wait(&all_channels, MBS_LLEXT_ZBUS_EVT_PENDING,
				   false, K_NO_WAIT), MBS_LLEXT_ZBUS_EVT_PENDING);
	zassert_ok(mbs_llext_zbus_take_pending(&all_channels, &pending));
	zassert_equal(pending, input_bit);
	zassert_equal(mbs_llext_zbus_take_pending(&notify_only, &pending), -ENOMSG);
	zassert_ok(mbs_llext_zbus_unsubscribe(&all_channels));
	zassert_ok(mbs_llext_zbus_unsubscribe(&notify_only));
}

ZTEST(mbs_llext_contract, test_bridge_unsubscribe_and_resubscribe_clear_pending)
{
	const uint64_t notify_bit =
		MBS_LLEXT_ZBUS_CH_BIT(MBS_LLEXT_ZBUS_NOTIFY_CHAN);
	static struct k_event subscriber;
	uint64_t pending = UINT64_MAX;

	k_event_init(&subscriber);
	zassert_equal(mbs_llext_zbus_take_pending(NULL, &pending), -EINVAL);
	zassert_equal(mbs_llext_zbus_take_pending(&subscriber, NULL), -EINVAL);
	zassert_equal(mbs_llext_zbus_take_pending(&subscriber, &pending), -ENOENT);
	zassert_equal(pending, 0ULL);
	zassert_ok(mbs_llext_zbus_subscribe(&subscriber, notify_bit));
	bridge_publish_notify();
	bridge_flush_notifications();
	zassert_ok(mbs_llext_zbus_subscribe(&subscriber, notify_bit));
	zassert_equal(mbs_llext_zbus_take_pending(&subscriber, &pending), -ENOMSG,
		      "replacing a subscription retained its pending mask");

	/* Unsubscribe while notification work may still be queued. */
	bridge_publish_notify();
	zassert_ok(mbs_llext_zbus_unsubscribe(&subscriber));
	(void)k_event_clear(&subscriber, MBS_LLEXT_ZBUS_EVT_PENDING);
	bridge_publish_notify();
	bridge_flush_notifications();
	zassert_equal(k_event_wait(&subscriber, MBS_LLEXT_ZBUS_EVT_PENDING,
				   false, K_NO_WAIT), 0U,
		      "notification accessed the event after unsubscribe");
	zassert_equal(mbs_llext_zbus_take_pending(&subscriber, &pending), -ENOENT);
	zassert_ok(mbs_llext_zbus_subscribe(&subscriber, notify_bit));
	zassert_equal(mbs_llext_zbus_take_pending(&subscriber, &pending), -ENOMSG,
		      "new subscription inherited old pending channels");
	bridge_publish_notify();
	bridge_flush_notifications();
	zassert_ok(mbs_llext_zbus_take_pending(&subscriber, &pending));
	zassert_equal(pending, notify_bit);
	zassert_ok(mbs_llext_zbus_unsubscribe(&subscriber));
}

#endif

#if IS_ENABLED(CONFIG_MBS_TELEMETRY)
ZTEST(mbs_llext_contract, test_telemetry_bridge_is_subscribe_only)
{
	struct mbs_telemetry_data_event event = {
		.chan = SENSOR_CHAN_AMBIENT_TEMP,
		.value_count = 1U,
	};
	struct mbs_telemetry_data_event readback;
	static struct k_event subscriber;
	uint64_t mask = MBS_LLEXT_ZBUS_CH_BIT(MBS_LLEXT_ZBUS_TELEMETRY_DATA_CHAN);

	k_event_init(&subscriber);
	zassert_ok(mbs_llext_zbus_subscribe(&subscriber, mask));
	zassert_ok(mbs_llext_zbus_read(MBS_LLEXT_ZBUS_TELEMETRY_DATA_CHAN, &readback,
				      sizeof(readback)));
	zassert_equal(mbs_llext_zbus_publish(MBS_LLEXT_ZBUS_TELEMETRY_DATA_CHAN,
					    &event, sizeof(event)),
		      -ENOTSUP);
	zassert_ok(mbs_llext_zbus_unsubscribe(&subscriber));
}
#endif

ZTEST_SUITE(mbs_llext_contract, NULL, suite_setup, NULL, NULL, NULL);
