/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/meshbus/fs.h>
#include <zephyr/ztest.h>

#define TEST_DIR MESHBUS_FS_GAMES_PATH "/fs_test"
#define TEST_FILE TEST_DIR "/hello.mba"

struct list_capture {
	struct meshbus_fs_entry entries[4];
	size_t count;
};

static int capture_entry(const struct meshbus_fs_entry *entry, void *user_data)
{
	struct list_capture *capture = user_data;

	zassert_not_null(entry);
	zassert_not_null(capture);
	zassert_true(capture->count < ARRAY_SIZE(capture->entries));

	capture->entries[capture->count++] = *entry;
	return 0;
}

static int reject_entry(const struct meshbus_fs_entry *entry, void *user_data)
{
	zassert_not_null(entry);
	zassert_is_null(user_data);
	return -ECANCELED;
}

static void write_test_file(const char *path)
{
	static const uint8_t payload[] = {0x4d, 0x42, 0x41};
	struct fs_file_t file;
	int rc;

	fs_file_t_init(&file);
	rc = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	zassert_ok(rc, "fs_open(%s) failed: %d", path, rc);

	rc = fs_write(&file, payload, sizeof(payload));
	zassert_equal(rc, sizeof(payload), "fs_write failed: %d", rc);

	rc = fs_close(&file);
	zassert_ok(rc, "fs_close failed: %d", rc);
}

static void cleanup_test_tree(void)
{
	(void)meshbus_fs_delete(TEST_FILE);
	(void)meshbus_fs_delete(TEST_DIR);
}

ZTEST(meshbus_fs_contract, test_path_normalize_policy)
{
	char path[MESHBUS_FS_PATH_MAX_LEN + 1];
	char long_path[MESHBUS_FS_PATH_MAX_LEN + 8];

	zassert_ok(meshbus_fs_path_normalize("/extra//apps/games/", path, sizeof(path)));
	zassert_str_equal(path, MESHBUS_FS_GAMES_PATH);

	zassert_equal(meshbus_fs_path_normalize("extra/apps", path, sizeof(path)), -EINVAL);
	zassert_equal(meshbus_fs_path_normalize("/extra/apps/../other", path, sizeof(path)),
		      -EINVAL);
	zassert_equal(meshbus_fs_path_normalize("/extra/apps/./game.mba", path, sizeof(path)),
		      -EINVAL);
	zassert_equal(meshbus_fs_path_normalize("/settings/foo", path, sizeof(path)), -EACCES);
	zassert_equal(meshbus_fs_path_normalize("/extra\\apps", path, sizeof(path)), -EINVAL);

	memset(long_path, 'a', sizeof(long_path));
	memcpy(long_path, "/extra/", strlen("/extra/"));
	long_path[sizeof(long_path) - 1U] = '\0';
	zassert_equal(meshbus_fs_path_normalize(long_path, path, sizeof(path)), -ENAMETOOLONG);
}

ZTEST(meshbus_fs_contract, test_invalid_arguments_and_callback_abort)
{
	struct meshbus_fs_volume_status status;
	char path[MESHBUS_FS_PATH_MAX_LEN + 1];
	size_t total_count = 99U;
	size_t next_offset = 99U;
	int rc;

	zassert_equal(meshbus_fs_path_normalize(NULL, path, sizeof(path)), -EINVAL);
	zassert_equal(meshbus_fs_path_normalize(MESHBUS_FS_GAMES_PATH, NULL,
					       sizeof(path)), -EINVAL);
	zassert_equal(meshbus_fs_path_normalize(MESHBUS_FS_GAMES_PATH, path, 0U),
		      -EINVAL);
	zassert_equal(meshbus_fs_volume_status(NULL, &status), -EINVAL);
	zassert_equal(meshbus_fs_volume_status("extra", NULL), -EINVAL);
	zassert_equal(meshbus_fs_volume_status("unknown", &status), -ENOENT);
	zassert_equal(meshbus_fs_stat(TEST_DIR, NULL), -EINVAL);
	zassert_equal(meshbus_fs_list(TEST_DIR, 0U, 1U, NULL, NULL, NULL, NULL),
		      -EINVAL);
	zassert_equal(meshbus_fs_format(NULL, MESHBUS_FS_FORMAT_CONFIRM), -EINVAL);
	zassert_equal(meshbus_fs_format("extra", NULL), -EINVAL);
	zassert_equal(meshbus_fs_format("unknown", MESHBUS_FS_FORMAT_CONFIRM), -ENOENT);

	cleanup_test_tree();
	zassert_ok(meshbus_fs_mkdir(TEST_DIR));
	write_test_file(TEST_FILE);
	rc = meshbus_fs_list(TEST_DIR, 0U, 1U, reject_entry, NULL, &total_count,
			     &next_offset);
	zassert_equal(rc, -ECANCELED, "callback error was not propagated: %d", rc);
	zassert_equal(total_count, 0U, "failed list published partial total");
	zassert_equal(next_offset, 0U, "failed list published partial cursor");
}

ZTEST(meshbus_fs_contract, test_product_dirs_and_status)
{
	struct meshbus_fs_volume_status status;
	struct meshbus_fs_entry entry;
	int rc;

	rc = meshbus_fs_ensure_product_dirs();
	zassert_ok(rc, "ensure product dirs failed: %d", rc);

	rc = meshbus_fs_stat(MESHBUS_FS_APPS_PATH, &entry);
	zassert_ok(rc, "stat apps failed: %d", rc);
	zassert_equal(entry.type, MESHBUS_FS_ENTRY_DIR);

	rc = meshbus_fs_stat(MESHBUS_FS_GAMES_PATH, &entry);
	zassert_ok(rc, "stat games failed: %d", rc);
	zassert_equal(entry.type, MESHBUS_FS_ENTRY_DIR);

	rc = meshbus_fs_volume_status("extra", &status);
	zassert_ok(rc, "status failed: %d", rc);
	zassert_true(status.mounted);
	zassert_str_equal(status.mount_point, MESHBUS_FS_EXTRA_MOUNT_POINT);
}

ZTEST(meshbus_fs_contract, test_file_ops_and_bounded_list)
{
	struct meshbus_fs_entry entry;
	struct list_capture capture = {0};
	size_t total_count = 0U;
	size_t next_offset = 0U;
	int rc;

	cleanup_test_tree();

	rc = meshbus_fs_mkdir(TEST_DIR);
	zassert_ok(rc, "mkdir failed: %d", rc);
	zassert_ok(meshbus_fs_mkdir(TEST_DIR), "repeated mkdir must be idempotent");
	write_test_file(TEST_FILE);
	zassert_equal(meshbus_fs_mkdir(TEST_FILE), -EEXIST,
		      "mkdir over an existing file must fail");

	rc = meshbus_fs_stat(TEST_FILE, &entry);
	zassert_ok(rc, "stat file failed: %d", rc);
	zassert_equal(entry.type, MESHBUS_FS_ENTRY_FILE);
	zassert_equal(entry.size, 3U);
	zassert_str_equal(entry.name, "hello.mba");

	rc = meshbus_fs_list(TEST_DIR, 0U, ARRAY_SIZE(capture.entries), capture_entry,
			     &capture, &total_count, &next_offset);
	zassert_ok(rc, "list failed: %d", rc);
	zassert_equal(total_count, 1U);
	zassert_equal(next_offset, 0U);
	zassert_equal(capture.count, 1U);
	zassert_str_equal(capture.entries[0].path, TEST_FILE);

	rc = meshbus_fs_delete(TEST_FILE);
	zassert_ok(rc, "delete file failed: %d", rc);
	rc = meshbus_fs_delete(TEST_DIR);
	zassert_ok(rc, "delete dir failed: %d", rc);
}

ZTEST(meshbus_fs_contract, test_delete_protects_product_dirs)
{
	zassert_equal(meshbus_fs_delete(MESHBUS_FS_EXTRA_MOUNT_POINT), -EACCES);
	zassert_equal(meshbus_fs_delete(MESHBUS_FS_APPS_PATH), -EACCES);
	zassert_equal(meshbus_fs_delete(MESHBUS_FS_GAMES_PATH), -EACCES);
}

ZTEST(meshbus_fs_contract, test_format_default_disabled)
{
	zassert_equal(meshbus_fs_format("extra", MESHBUS_FS_FORMAT_CONFIRM), -ENOTSUP);
	zassert_equal(meshbus_fs_format("extra", "wrong"), -EACCES);
}

static void *meshbus_fs_setup(void)
{
	zassert_ok(meshbus_fs_ensure_product_dirs());
	cleanup_test_tree();
	return NULL;
}

static void meshbus_fs_teardown(void *fixture)
{
	ARG_UNUSED(fixture);
	cleanup_test_tree();
}

ZTEST_SUITE(meshbus_fs_contract, NULL, meshbus_fs_setup, NULL, NULL, meshbus_fs_teardown);
