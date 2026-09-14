/* SPDX-License-Identifier: Apache-2.0 */
#include <desktop/session.h>
#include <desktop/package.h>
#include <llext/metadata.h>
#include <psa/crypto.h>
#include <stdio.h>

#include "desktop_private.h"

#include <errno.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/llext/llext.h>
#include <zephyr/llext/symbol.h>
#include <zephyr/logging/log.h>
#include <llext/llext.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>
#include <zui/zui.h>

LOG_MODULE_REGISTER(mbs_desktop);

#define APP_PATH "/extra/app.mba"
#define BAD_ENTRY_PATH "/extra/bad-entry.mba"

static const uint8_t app_binary[] __aligned(4) = {
#include "mba_app.inc"
};
static const uint8_t bad_entry_binary[] __aligned(4) = {
#include "mba_bad_entry.inc"
};

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
static struct fs_mount_t mount = {
	.type = FS_LITTLEFS,
	.fs_data = &storage,
	.storage_dev = (void *)DT_PARTITION_ID(DT_NODELABEL(extra_partition)),
	.mnt_point = "/extra",
};

static K_THREAD_STACK_DEFINE(app_stack, 2048);
static K_SEM_DEFINE(entered, 0, 1);
static K_SEM_DEFINE(return_from_entry, 0, 1);
static K_SEM_DEFINE(exit_notified, 0, 1);
static K_SEM_DEFINE(return_from_thread, 0, 1);
static struct zui_desktop desktop;
static uint32_t screen;
static bool desktop_visible;
static bool delay_thread_exit;
static bool poll_exit_during_start;
static bool cooperative_stop;
static bool expect_mba;
static unsigned int entry_count;
static unsigned int unload_count;
static unsigned int unload_failures;
static unsigned int error_count;
static int shown_error;
static int external_cleanup_count;
static const char *expected_resource_path;

/* UI adapter: observe the same restore/error operations the real UI receives. */
struct zui_desktop *zui_desktop_get_instance(void)
{
	return &desktop;
}

uint32_t __wrap_zui_router_current(const struct zui_router *router)
{
	zassert_equal(router, desktop.router);
	return screen;
}

int __wrap_zui_host_set_layer_enabled(struct zui_host *host, enum zui_layer layer, bool enabled)
{
	zassert_equal(host, desktop.host);
	zassert_equal(layer, ZUI_LAYER_DESKTOP);
	desktop_visible = enabled;
	return 0;
}

int __wrap_zui_host_send_layer_to_front(struct zui_host *host, enum zui_layer layer)
{
	zassert_equal(host, desktop.host);
	zassert_equal(layer, ZUI_LAYER_DESKTOP);
	return 0;
}

int zui_desktop_switch(struct zui_desktop *instance, uint32_t screen_id)
{
	zassert_equal(instance, &desktop);
	screen = screen_id;
	return 0;
}

void desktop_launcher_show_cleanup_error(struct zui_desktop *instance, int error)
{
	zassert_equal(instance, &desktop);
	zassert_true(desktop_visible, "error was presented before restoring Desktop");
	zassert_false(mbs_desktop_external_app_is_active());
	zassert_true(mbs_llext_runtime_busy(), "failed session was discarded");
	shown_error = error;
	error_count++;
	/* UI callbacks cannot reenter the lifecycle while reclamation is completing. */
	zassert_equal(desktop_mba_start(instance, APP_PATH), -EBUSY);
}

int __real_llext_unload(struct llext **ext);

int __wrap_llext_unload(struct llext **ext)
{
	unload_count++;
	if (unload_failures != 0U) {
		unload_failures--;
		return -EIO;
	}
	return __real_llext_unload(ext);
}

void __real_zui_desktop_request_app_exit(struct zui_desktop *instance);

void __wrap_zui_desktop_request_app_exit(struct zui_desktop *instance)
{
	/* Preserve the real notification, then force a possible pre-join schedule. */
	__real_zui_desktop_request_app_exit(instance);
	k_sem_give(&exit_notified);
	if (delay_thread_exit) {
		zassert_ok(k_sem_take(&return_from_thread, K_SECONDS(2)));
	}
}

int __real_desktop_app_registry_start(struct zui_desktop *instance,
				      mbs_desktop_app_handle_t handle);

int __wrap_desktop_app_registry_start(struct zui_desktop *instance,
				      mbs_desktop_app_handle_t handle)
{
	mbs_desktop_mba_status competing;
	int ret;

	/* A managed reservation cannot interleave with any launch before the
	 * new app thread becomes visible to the runtime busy checks.
	 */
	zassert_equal(mbs_desktop_mba_start("mba-test", APP_PATH, &competing), -EBUSY);
	ret = __real_desktop_app_registry_start(instance, handle);

	if (ret == 0 && poll_exit_during_start) {
		zassert_ok(k_sem_take(&exit_notified, K_SECONDS(1)));
		/* A suspended UI can consume the real notification before the launch
		 * operation returns, find it busy, and then wait indefinitely.
		 */
		zassert_ok(k_sem_take(&instance->redraw_sem, K_NO_WAIT));
		zassert_equal(desktop_app_complete_exit(instance, K_NO_WAIT), -EBUSY);
	}
	return ret;
}

void mba_test_run(void *arg)
{
	struct mbs_desktop_app_args *args = arg;

	zassert_not_null(args);
	zassert_equal(args->host, desktop.host);
	if (expect_mba) {
		zassert_not_null(args->user_data);
	}
	if (expected_resource_path != NULL) {
		char resolved[192];
		zassert_ok(mbs_desktop_app_resource_path("assets.abr", resolved, sizeof(resolved)));
		zassert_str_equal(resolved, expected_resource_path);
	}
	/* Entry arguments are mutable; they do not own the retained session. */
	args->user_data = NULL;
	entry_count++;
	k_sem_give(&entered);
	if (cooperative_stop) {
		while (!mbs_desktop_app_stop_requested()) {
			k_sleep(K_MSEC(1));
		}
	} else {
		zassert_ok(k_sem_take(&return_from_entry, K_SECONDS(2)));
	}
}
EXPORT_SYMBOL(mba_test_run);

MBS_DESKTOP_APP_DEFINE("builtin-test", "Builtin test", mba_test_run, 1024, NULL, 0);

static void write_package(const char *path, const uint8_t *data, size_t size)
{
	struct fs_file_t file;

	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC));
	zassert_equal(fs_write(&file, data, size), size);
	zassert_ok(fs_close(&file));
}

static void *setup(void)
{
#if !defined(CONFIG_FLASH_SIMULATOR)
	extern int mba_test_storage_prepare_rc;

	zassert_ok(mba_test_storage_prepare_rc, "test storage preparation failed");
#endif
	zassert_ok(fs_mount(&mount));
	write_package(APP_PATH, app_binary, sizeof(app_binary));
	write_package(BAD_ENTRY_PATH, bad_entry_binary, sizeof(bad_entry_binary));
	desktop.host = (struct zui_host *)&desktop;
	desktop.router = (struct zui_router *)&screen;
	desktop.app_shared_stack = app_stack;
	desktop.app_shared_stack_size = K_THREAD_STACK_SIZEOF(app_stack);
	k_sem_init(&desktop.redraw_sem, 0, 1);
	return NULL;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	zassert_false(mbs_llext_runtime_busy(), "previous test retained a session");
	zassert_false(mbs_desktop_external_app_is_active());
	screen = MBS_DESKTOP_VIEW_LAUNCHER;
	desktop_visible = true;
	delay_thread_exit = false;
	poll_exit_during_start = false;
	cooperative_stop = false;
	expect_mba = true;
	entry_count = 0;
	unload_count = 0;
	unload_failures = 0;
	error_count = 0;
	shown_error = 0;
	external_cleanup_count = 0;
	expected_resource_path = NULL;
	k_sem_reset(&entered);
	k_sem_reset(&return_from_entry);
	k_sem_reset(&exit_notified);
	k_sem_reset(&return_from_thread);
	k_sem_reset(&desktop.redraw_sem);
}

static void start_mba(void)
{
	zassert_ok(desktop_mba_start(&desktop, APP_PATH));
	zassert_ok(k_sem_take(&entered, K_SECONDS(1)));
	zassert_true(mbs_desktop_external_app_is_active());
	zassert_true(mbs_llext_runtime_busy());
	zassert_false(desktop_visible);
}

static int complete_app(void)
{
	k_sem_give(&return_from_entry);
	zassert_ok(k_sem_take(&exit_notified, K_SECONDS(1)));
	return desktop_app_complete_exit(&desktop, K_SECONDS(1));
}

ZTEST(mba_lifecycle, test_launch_exit_and_next_launch)
{
	start_mba();
	zassert_equal(desktop_mba_start(&desktop, APP_PATH), -EBUSY);
	zassert_ok(desktop_app_complete_exit(&desktop, K_NO_WAIT));
	zassert_equal(unload_count, 0U, "running entry was unloaded");
	zassert_ok(complete_app());
	zassert_true(desktop_visible);
	zassert_equal(screen, MBS_DESKTOP_VIEW_LAUNCHER);
	zassert_false(mbs_llext_runtime_busy());
	zassert_false(mbs_desktop_external_app_is_active());
	zassert_equal(unload_count, 1U);
	zassert_ok(desktop_app_complete_exit(&desktop, K_NO_WAIT));
	zassert_equal(unload_count, 1U, "duplicate exit unloaded twice");
	start_mba();
	zassert_ok(complete_app());
	zassert_equal(entry_count, 2U);
}

ZTEST(mba_lifecycle, test_join_precedes_unload_and_stack_reuse)
{
	delay_thread_exit = true;
	start_mba();
	k_sem_give(&return_from_entry);
	zassert_ok(k_sem_take(&exit_notified, K_SECONDS(1)));
	zassert_not_equal(desktop_app_complete_exit(&desktop, K_NO_WAIT), 0);
	zassert_true(mbs_desktop_external_app_is_active());
	zassert_true(mbs_llext_runtime_busy());
	zassert_equal(unload_count, 0U);
	zassert_false(desktop_visible);
	zassert_equal(desktop_mba_start(&desktop, APP_PATH), -EBUSY);
	k_sem_give(&return_from_thread);
	zassert_ok(desktop_app_complete_exit(&desktop, K_SECONDS(1)));
	zassert_equal(unload_count, 1U);
	zassert_false(mbs_llext_runtime_busy());
}

ZTEST(mba_lifecycle, test_early_exit_wakes_ui_after_launch_finishes)
{
	poll_exit_during_start = true;
	k_sem_give(&return_from_entry);
	zassert_ok(desktop_mba_start(&desktop, APP_PATH));
	zassert_ok(k_sem_take(&desktop.redraw_sem, K_NO_WAIT),
		   "busy launch lost the app exit wakeup");
	zassert_ok(desktop_app_complete_exit(&desktop, K_SECONDS(1)));
	zassert_false(mbs_llext_runtime_busy());
	zassert_equal(unload_count, 1U);
}

ZTEST(mba_lifecycle, test_unload_failure_retained_until_explicit_launch)
{
	const struct mbs_desktop_external_app_desc other = {
		.id = "other-external",
		.display_name = "Other external",
		.app_main = mba_test_run,
		.stack_size = 1024,
	};

	start_mba();
	unload_failures = 2;
	zassert_equal(complete_app(), -EIO);
	zassert_equal(error_count, 1U);
	zassert_equal(shown_error, -EIO);
	zassert_true(desktop_visible);
	zassert_false(mbs_desktop_external_app_is_active());
	zassert_equal(mbs_desktop_external_app_start(&other), -EBUSY,
		      "generic external launch bypassed the retained MBA");
	zassert_ok(desktop_app_complete_exit(&desktop, K_NO_WAIT));
	zassert_equal(unload_count, 1U, "idle UI polling retried unload");
	zassert_equal(desktop_mba_start(&desktop, APP_PATH), -EIO);
	zassert_equal(entry_count, 1U, "new app started while old resources remained");
	zassert_true(mbs_llext_runtime_busy());
	start_mba();
	zassert_equal(unload_count, 3U);
	zassert_ok(complete_app());
	zassert_false(mbs_llext_runtime_busy());
}

ZTEST(mba_lifecycle, test_start_failure_reclaims_or_retains_session)
{
	desktop.app_shared_stack_size = 512;
	zassert_equal(desktop_mba_start(&desktop, APP_PATH), -EINVAL);
	zassert_false(mbs_llext_runtime_busy());
	zassert_true(desktop_visible);
	zassert_equal(entry_count, 0U);
	unload_failures = 1;
	zassert_equal(desktop_mba_start(&desktop, APP_PATH), -EIO);
	zassert_true(mbs_llext_runtime_busy());
	zassert_false(mbs_desktop_external_app_is_active());
	desktop.app_shared_stack_size = K_THREAD_STACK_SIZEOF(app_stack);
	start_mba();
	zassert_ok(complete_app());
	zassert_false(mbs_llext_runtime_busy());
}

ZTEST(mba_lifecycle, test_load_failure_cleanup_retains_recoverable_handle)
{
	struct mbs_llext_app_session *session = NULL;
	struct mbs_llext_app_session *other = NULL;
	struct mbs_llext_app_info info;
	mbs_llext_app_entry_t entry = NULL;

	unload_failures = 1;
	zassert_equal(mbs_llext_app_load(BAD_ENTRY_PATH, &session), -EIO);
	zassert_not_null(session);
	zassert_ok(mbs_llext_app_get_info(session, &info));
	zassert_equal(info.last_error, -EIO);
	zassert_equal(mbs_llext_app_get_entry(session, &entry), -ENOENT);
	zassert_equal(mbs_llext_app_load(APP_PATH, &other), -EBUSY);
	zassert_is_null(other);
	zassert_ok(mbs_llext_app_unload(session));
	zassert_false(mbs_llext_runtime_busy());
	start_mba();
	zassert_ok(complete_app());
}

ZTEST(mba_lifecycle, test_failed_load_owned_by_desktop_until_next_launch)
{
	zassert_equal(desktop_mba_start(&desktop, BAD_ENTRY_PATH), -ENOEXEC);
	zassert_false(mbs_llext_runtime_busy());
	unload_failures = 2;
	zassert_equal(desktop_mba_start(&desktop, BAD_ENTRY_PATH), -EIO);
	zassert_true(mbs_llext_runtime_busy());
	zassert_equal(desktop_mba_start(&desktop, APP_PATH), -EIO);
	zassert_equal(entry_count, 0U);
	start_mba();
	zassert_ok(complete_app());
	zassert_false(mbs_llext_runtime_busy());
}

ZTEST(mba_lifecycle, test_unload_failure_revokes_entry_until_reclaimed)
{
	struct mbs_llext_app_session *session = NULL;
	mbs_llext_app_entry_t entry = NULL;

	zassert_ok(mbs_llext_app_load(APP_PATH, &session));
	zassert_ok(mbs_llext_app_get_entry(session, &entry));
	zassert_not_null(entry);
	unload_failures = 1;
	zassert_equal(mbs_llext_app_unload(session), -EIO);
	zassert_true(mbs_llext_runtime_busy());
	zassert_equal(mbs_llext_app_get_entry(session, &entry), -ENOENT,
		      "partially torn-down entry remained available");
	zassert_ok(mbs_llext_app_unload(session));
	zassert_false(mbs_llext_runtime_busy());
}

static void external_cleanup(void *user_data)
{
	zassert_equal(user_data, &external_cleanup_count);
	external_cleanup_count++;
}

ZTEST(mba_lifecycle, test_generic_external_and_builtin_lifecycle_survive)
{
	const struct mbs_desktop_external_app_desc desc = {
		.id = "external-test",
		.display_name = "External test",
		.app_main = mba_test_run,
		.stack_size = 1024,
		.user_data = &external_cleanup_count,
		.cleanup = external_cleanup,
	};

	expect_mba = false;
	zassert_ok(mbs_desktop_external_app_start(&desc));
	zassert_ok(k_sem_take(&entered, K_SECONDS(1)));
	zassert_true(mbs_desktop_external_app_is_active());
	zassert_ok(complete_app());
	zassert_equal(external_cleanup_count, 1);
	zassert_true(desktop_open_app(&desktop,
		mbs_desktop_app_registry_handle_from_id("builtin-test"),
		MBS_DESKTOP_VIEW_MAIN_MENU));
	zassert_ok(k_sem_take(&entered, K_SECONDS(1)));
	zassert_false(mbs_desktop_external_app_is_active());
	zassert_ok(complete_app());
	zassert_equal(screen, MBS_DESKTOP_VIEW_MAIN_MENU);
	zassert_equal(external_cleanup_count, 1);
	zassert_equal(unload_count, 0U);
}

static void after(void *fixture)
{
	ARG_UNUSED(fixture);
	zassert_false(mbs_llext_runtime_busy(), "test left an MBA Session behind");
	zassert_false(mbs_desktop_external_app_is_active());
	zassert_true(desktop_visible, "test left Desktop hidden");
}

static void teardown(void *fixture)
{
	ARG_UNUSED(fixture);
	zassert_ok(fs_unlink(APP_PATH));
	zassert_ok(fs_unlink(BAD_ENTRY_PATH));
	zassert_ok(fs_unmount(&mount));
#if !defined(CONFIG_FLASH_SIMULATOR)
	extern int mba_test_storage_cleanup(void);

	zassert_ok(mba_test_storage_cleanup());
#endif
}

static K_THREAD_STACK_DEFINE(request_stack, 4096);
static struct k_thread request_thread;

static void process_on_wakeup(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	zassert_ok(k_sem_take(&desktop.redraw_sem, K_SECONDS(1)));
	desktop_mba_process_requests(&desktop);
}

ZTEST(mba_lifecycle, test_managed_acceptance_allows_immediate_desktop_preemption)
{
	mbs_desktop_mba_status status;
	int priority = k_thread_priority_get(k_current_get());

	cooperative_stop = true;
	/* MCUmgr is preemptible; the default ztest thread is cooperative. */
	k_thread_priority_set(k_current_get(), K_PRIO_PREEMPT(1));
	k_thread_create(&request_thread, request_stack, K_THREAD_STACK_SIZEOF(request_stack),
		process_on_wakeup, NULL, NULL, NULL, K_PRIO_COOP(0), 0, K_NO_WAIT);
	zassert_ok(mbs_desktop_mba_start("mba-lifecycle", APP_PATH, &status));
	zassert_ok(k_thread_join(&request_thread, K_SECONDS(1)));
	k_thread_priority_set(k_current_get(), priority);
	zassert_ok(k_sem_take(&entered, K_SECONDS(1)));
	zassert_ok(mbs_desktop_mba_get_status(status.session_id, &status));
	zassert_equal(status.state, meshbus_DesktopMbaState_DESKTOP_MBA_STATE_RUNNING);
	zassert_ok(mbs_desktop_mba_stop(status.session_id, 1000, &status));
	desktop_mba_process_requests(&desktop);
	zassert_ok(k_sem_take(&exit_notified, K_SECONDS(1)));
	zassert_ok(desktop_app_complete_exit(&desktop, K_SECONDS(1)));
}

ZTEST(mba_lifecycle, test_managed_stop_reports_reclamation_and_retries_cleanup)
{
	mbs_desktop_mba_status status;
	uint64_t session;

	cooperative_stop = true;
	screen = MBS_DESKTOP_VIEW_DASHBOARD;
	zassert_ok(mbs_desktop_mba_start("mba-lifecycle", APP_PATH, &status));
	session = status.session_id;
	zassert_not_equal(session, 0U);
	zassert_equal(status.state, meshbus_DesktopMbaState_DESKTOP_MBA_STATE_ACCEPTED);
	zassert_equal(entry_count, 0U);
	zassert_equal(desktop_mba_start(&desktop, APP_PATH), -EBUSY);
	desktop_mba_process_requests(&desktop);
	zassert_ok(k_sem_take(&entered, K_SECONDS(1)));
	zassert_ok(mbs_desktop_mba_get_status(session, &status));
	zassert_equal(status.state, meshbus_DesktopMbaState_DESKTOP_MBA_STATE_RUNNING);
	zassert_equal(mbs_desktop_mba_stop(session + 1, 100, &status), -ESTALE);
	unload_failures = 1;
	zassert_ok(mbs_desktop_mba_stop(session, 1000, &status));
	zassert_false(status.resources_reclaimed);
	desktop_mba_process_requests(&desktop);
	zassert_ok(k_sem_take(&exit_notified, K_SECONDS(1)));
	zassert_equal(desktop_app_complete_exit(&desktop, K_SECONDS(1)), -EIO);
	zassert_ok(mbs_desktop_mba_get_status(session, &status));
	zassert_equal(status.state, meshbus_DesktopMbaState_DESKTOP_MBA_STATE_FAILED);
	zassert_false(status.resources_reclaimed);
	zassert_equal(mbs_desktop_mba_start("mba-lifecycle", APP_PATH, &status), -EBUSY);
	zassert_ok(mbs_desktop_mba_stop(session, 1000, &status));
	desktop_mba_process_requests(&desktop);
	zassert_ok(mbs_desktop_mba_get_status(session, &status));
	zassert_true(status.resources_reclaimed);
	zassert_equal(status.state, meshbus_DesktopMbaState_DESKTOP_MBA_STATE_ENDED);
}

ZTEST(mba_lifecycle, test_managed_timeout_cannot_authorize_replacement)
{
	mbs_desktop_mba_status status;
	uint64_t session;

	zassert_ok(mbs_desktop_mba_start("mba-lifecycle", APP_PATH, &status));
	session = status.session_id;
	desktop_mba_process_requests(&desktop);
	zassert_ok(k_sem_take(&entered, K_SECONDS(1)));
	zassert_ok(mbs_desktop_mba_stop(session, 1, &status));
	desktop_mba_process_requests(&desktop);
	k_sleep(K_MSEC(2));
	desktop_mba_process_requests(&desktop);
	zassert_ok(mbs_desktop_mba_get_status(session, &status));
	zassert_equal(status.detail, -ETIMEDOUT);
	zassert_false(status.resources_reclaimed);
	zassert_equal(mbs_desktop_mba_start("mba-lifecycle", APP_PATH, &status), -EBUSY);
	zassert_ok(complete_app());
	zassert_ok(mbs_desktop_mba_get_status(session, &status));
	zassert_true(status.resources_reclaimed);
}

#define PACKAGE_ROOT "/extra/apps/mba-lifecycle/versions/"
#define STORE        "/extra/apps/.meshbus"
#define HASH_A       "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define HASH_B       "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
static bool payload_capacity_only;
int __real_fs_statvfs(const char *, struct fs_statvfs *);
int __wrap_fs_statvfs(const char *path, struct fs_statvfs *stat)
{
	int rc = __real_fs_statvfs(path, stat);
	if (rc == 0 && payload_capacity_only) {
		stat->f_bfree = DIV_ROUND_UP(sizeof(app_binary), stat->f_frsize) + 1;
	}
	return rc;
}
static const char *fail_rename;
static const char *fail_unlink;
int __real_fs_rename(const char *, const char *);
int __wrap_fs_rename(const char *from, const char *to)
{
	if (fail_rename != NULL && strcmp(to, fail_rename) == 0) {
		fail_rename = NULL;
		return -EIO;
	}
	return __real_fs_rename(from, to);
}
int __real_fs_unlink(const char *);
int __wrap_fs_unlink(const char *path)
{
	if (fail_unlink != NULL && strcmp(path, fail_unlink) == 0) {
		fail_unlink = NULL;
		return -EIO;
	}
	return __real_fs_unlink(path);
}
int __wrap_mbs_llext_host_info_get(meshbus_LlextHostInfoResponse *info)
{
	memset(info, 0, sizeof(*info));
	strcpy(info->target, CONFIG_BOARD_TARGET);
	strcpy(info->build_revision, "fixture");
	info->metadata_version = MBS_LLEXT_APP_METADATA_VERSION;
	info->interface_abi = MBS_LLEXT_INTERFACE_ABI;
	info->image_sha256.size = 32;
	memset(info->image_sha256.bytes, 0xaa, 32);
	return 0;
}
EXPORT_SYMBOL(mbs_desktop_app_resource_path);
static void mkdir_test(const char *path)
{
	int rc = fs_mkdir(path);
	zassert_true(rc == 0 || rc == -EEXIST, "mkdir %s: %d", path, rc);
}
static void digest_test(const void *data, size_t length, char result[65])
{
	uint8_t hash[32];
	size_t size;
	zassert_equal(psa_hash_compute(PSA_ALG_SHA_256, data, length, hash, 32, &size),
		      PSA_SUCCESS);
	zassert_equal(bin2hex(hash, 32, result, 65), 64);
}
static meshbus_DesktopPackageResponse package_response;
static int package_action(int action, const char *bundle, bool purge)
{
	meshbus_DesktopPackageRequest request = meshbus_DesktopPackageRequest_init_zero;
	request.action = action;
	strcpy(request.app_id, "mba-lifecycle");
	strcpy(request.manifest_path, STORE "/incoming.json");
	request.replace = true;
	request.remove_saves = purge;
	if (bundle) {
		strcpy(request.bundle, bundle);
	}
	return mbs_desktop_package_manage(&request, &package_response);
}
static void incoming_test(const char *hash, char mba[192], char resource[192])
{
	char directory[160], mba_hash[65], resource_hash[65];
	char *json = k_malloc(4096);
	zassert_not_null(json);
	snprintf(directory, sizeof(directory), PACKAGE_ROOT "%.32s", hash);
	mkdir_test(directory);
	snprintf(mba, 192, "%s/game.mba", directory);
	snprintf(resource, 192, "%s/assets.abr", directory);
	digest_test(app_binary, sizeof(app_binary), mba_hash);
	digest_test("asset", 5, resource_hash);
	int length = snprintf(
		json, 4096,
		"{\"schema\":1,\"id\":\"mba-lifecycle\",\"version\":\"1.0.0\","
		"\"bundle\":\"%s\",\"image\":\"" HASH_A "\",\"target\":\"" CONFIG_BOARD_TARGET "\","
		"\"firmware\":\"fixture\",\"metadata_version\":%u,\"interface_abi\":%u,"
		"\"mba_path\":\"%s\",\"atomic\":true,\"operation\":0,\"purge_saves\":false,"
		"\"files\":[{\"path\":\"%s\",\"length\":%u,\"sha256\":\"%s\"},"
		"{\"path\":\"%s\",\"length\":5,\"sha256\":\"%s\"}],"
		"\"requires\":[\"mba_test_run\",\"mbs_desktop_app_resource_path\"]}",
		hash, MBS_LLEXT_APP_METADATA_VERSION, MBS_LLEXT_INTERFACE_ABI, mba, mba,
		(unsigned int)sizeof(app_binary), mba_hash, resource, resource_hash);
	zassert_true(length > 0 && length < 4096);
	write_package(STORE "/incoming.json", (uint8_t *)json, length);
	k_free(json);
}
ZTEST(mba_lifecycle, test_package_commit_recovery_and_save_preservation)
{
	char mba[192], resource[192], first_mba[192];
	struct fs_dirent entry;
	mkdir_test("/extra/apps");
	mkdir_test(STORE);
	mkdir_test("/extra/apps/mba-lifecycle");
	mkdir_test("/extra/apps/mba-lifecycle/versions");
	mkdir_test("/extra/saves");
	write_package("/extra/saves/mba-lifecycle.dat", (uint8_t *)"save", 4);
	write_package("/extra/apps/unrelated.txt", (uint8_t *)"user", 4);
	incoming_test(HASH_A, mba, resource);
	strcpy(first_mba, mba);
	payload_capacity_only = true;
	zassert_equal(package_action(1, NULL, false), -ENOSPC);
	payload_capacity_only = false;
	zassert_equal(fs_stat(STORE "/pending.json", &entry), -ENOENT);
	zassert_ok(package_action(1, NULL, false));
	zassert_equal(desktop_package_path_validate(APP_PATH), -EBUSY);
	write_package(mba, app_binary, sizeof(app_binary));
	zassert_equal(package_action(2, NULL, false), -ENOENT);
	zassert_equal(package_response.current_bundle[0], '\0');
	write_package(resource, (uint8_t *)"asset", 5);
	fail_rename = STORE "/mba-lifecycle/current.json";
	zassert_equal(package_action(2, NULL, false), -EIO);
	zassert_equal(package_response.current_bundle[0], '\0');
	zassert_ok(package_action(2, NULL, false));
	zassert_ok(desktop_package_path_validate(mba));
	incoming_test(HASH_B, mba, resource);
	zassert_ok(package_action(1, NULL, false));
	write_package(mba, app_binary, sizeof(app_binary));
	write_package(resource, (uint8_t *)"asset", 5);
	fail_unlink = STORE "/pending.json";
	zassert_equal(package_action(2, NULL, false), -EIO);
	zassert_str_equal(package_response.current_bundle, HASH_B);
	zassert_str_equal(package_response.state, "committed-recovery");
	zassert_equal(desktop_package_path_validate(mba), -EBUSY);
	zassert_ok(fs_unmount(&mount));
	zassert_ok(fs_mount(&mount));
	zassert_ok(package_action(2, NULL, false));
	zassert_ok(package_action(2, NULL, false));
	zassert_equal(desktop_package_path_validate(first_mba), -ENOENT);
	fail_unlink = STORE "/pending.json";
	zassert_equal(package_action(5, HASH_A, false), -EIO);
	zassert_ok(package_action(5, HASH_A, false));
	zassert_ok(package_action(5, HASH_A, false));
	zassert_str_equal(package_response.current_bundle, HASH_A);
	incoming_test("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc", mba,
		      resource);
	zassert_ok(package_action(1, NULL, false));
	write_package(mba, app_binary, sizeof(app_binary));
	write_package(resource, (uint8_t *)"asset", 5);
	fail_rename = STORE "/mba-lifecycle/current.json";
	zassert_equal(package_action(2, NULL, false), -EIO);
	zassert_ok(fs_stat(STORE "/garbage.json", &entry));
	zassert_ok(package_action(3, NULL, false));
	zassert_equal(fs_stat(STORE "/garbage.json", &entry), -ENOENT);
	zassert_str_equal(package_response.current_bundle, HASH_A);
	expected_resource_path = PACKAGE_ROOT "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/assets.abr";
	zassert_ok(desktop_mba_start(&desktop, first_mba));
	zassert_ok(k_sem_take(&entered, K_SECONDS(1)));
	zassert_ok(complete_app());
	expected_resource_path = NULL;
	fail_unlink = first_mba;
	zassert_equal(package_action(4, NULL, false), -EIO);
	zassert_str_equal(package_response.state, "uninstalling");
	zassert_ok(package_action(2, NULL, false));
	zassert_str_equal(package_response.state, "absent");
	zassert_ok(fs_stat("/extra/saves/mba-lifecycle.dat", &entry));
	zassert_equal(entry.size, 4);
	zassert_ok(fs_stat("/extra/apps/unrelated.txt", &entry));
	zassert_equal(fs_stat(first_mba, &entry), -ENOENT);
	zassert_equal(fs_stat(mba, &entry), -ENOENT);
	/* Explicit purge intent survives a lost final response and a default retry. */
	incoming_test(HASH_A, mba, resource);
	zassert_ok(package_action(1, NULL, false));
	write_package(mba, app_binary, sizeof(app_binary));
	write_package(resource, (uint8_t *)"asset", 5);
	zassert_ok(package_action(2, NULL, false));
	fail_unlink = STORE "/pending.json";
	zassert_equal(package_action(4, NULL, true), -EIO);
	zassert_ok(package_action(4, NULL, false));
	zassert_equal(fs_stat("/extra/saves/mba-lifecycle.dat", &entry), -ENOENT);
	zassert_ok(fs_unlink(STORE "/incoming.json"));
	zassert_ok(fs_unlink("/extra/apps/unrelated.txt"));
}

ZTEST_SUITE(mba_lifecycle, NULL, setup, before, after, teardown);
