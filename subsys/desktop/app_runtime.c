/* SPDX-License-Identifier: Apache-2.0 */

#include "desktop_private.h"
#include "registry/apps_registry_prvi.h"
#include <desktop/session.h>
#include <zephyr/random/random.h>
#include <string.h>
#include <stdio.h>

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#if defined(CONFIG_MBS_DESKTOP_LAUNCHER)
#include <llext/llext.h>
#endif
#include <zui/zui.h>

LOG_MODULE_DECLARE(mbs_desktop);

/* Reject concurrent/reentrant lifecycle operations instead of holding a lock
 * across loading, thread join, extension code, or UI callbacks.
 */
static atomic_t lifecycle_busy;

static void lifecycle_end(struct zui_desktop *desktop)
{
	atomic_clear(&lifecycle_busy);
	/* An app can return before start finishes. If Desktop already consumed
	 * that notification while this operation was busy, restore its wakeup.
	 */
	if (desktop != NULL && atomic_get(&desktop->app_exit_pending)) {
		k_sem_give(&desktop->redraw_sem);
	}
}

bool desktop_app_lifecycle_acquire(void)
{
	return atomic_cas(&lifecycle_busy, 0, 1);
}

void desktop_app_lifecycle_release(void)
{
	lifecycle_end(zui_desktop_get_instance());
}

#if defined(CONFIG_MBS_DESKTOP_LAUNCHER)
static struct {
	struct mbs_llext_app_session *session;
	mbs_desktop_app_handle_t handle;
	uint64_t owner;
	char path[MBS_LLEXT_PATH_MAX_LEN + 1];
} mba;

#define MBA_STATE(name) meshbus_DesktopMbaState_DESKTOP_MBA_STATE_##name
static struct k_spinlock managed_lock;
static mbs_desktop_mba_status managed = {
	.protocol_version = 1U,
	.resources_reclaimed = true,
};
static bool managed_start_pending;
static bool managed_stop_queued;
static int64_t managed_stop_deadline;
static uint64_t managed_epoch;
static uint32_t managed_sequence;

static void managed_finish(int detail)
{
	k_spinlock_key_t key = k_spin_lock(&managed_lock);

	if (managed.session_id != 0U && mba.owner == managed.session_id &&
	    !managed.resources_reclaimed &&
	    managed.state != MBA_STATE(ACCEPTED)) {
		managed.resources_reclaimed = mba.session == NULL &&
			!mbs_desktop_app_handle_is_valid(mba.handle);
		managed.detail = detail != 0 ? detail : managed.detail;
		managed.state = managed.detail != 0 ? MBA_STATE(FAILED) : MBA_STATE(ENDED);
		if (managed.resources_reclaimed) {
			mba.owner = 0U;
		}
	}
	k_spin_unlock(&managed_lock, key);
}

static int mba_reclaim(void)
{
	int ret;

	if (mba.session == NULL) {
		return 0;
	}

	ret = mbs_llext_app_unload(mba.session);
	if (ret == 0) {
		mba.session = NULL;
	}
	return ret;
}
#endif

static void app_restore_desktop(struct zui_desktop *desktop)
{
	desktop->active_app = NULL;
	desktop->active_app_handle = MBS_DESKTOP_APP_HANDLE_INVALID;
	if (desktop->host != NULL && desktop->router != NULL) {
		(void)zui_host_set_layer_enabled(desktop->host, ZUI_LAYER_DESKTOP, true);
		(void)zui_host_send_layer_to_front(desktop->host, ZUI_LAYER_DESKTOP);
		(void)zui_desktop_switch(desktop, desktop->app_return_screen_id);
	}
}

static int app_open(struct zui_desktop *desktop, mbs_desktop_app_handle_t handle,
		    uint32_t return_screen_id)
{
	const struct mbs_desktop_app_desc *app;
	int ret;

	if (desktop == NULL || desktop->host == NULL ||
	    !mbs_desktop_app_handle_is_valid(handle)) {
		return -EINVAL;
	}
	if (mbs_desktop_app_handle_is_valid(desktop->active_app_handle)) {
		return -EBUSY;
	}

	ret = mbs_desktop_app_registry_resolve_handle(handle, &app);
	if (ret != 0) {
		return ret;
	}
	if (app == NULL || app->app_main == NULL) {
		return -EINVAL;
	}

	desktop->active_app_handle = handle;
	desktop->active_app = app;
	desktop->app_return_screen_id = return_screen_id;
	(void)zui_host_set_layer_enabled(desktop->host, ZUI_LAYER_DESKTOP, false);
	ret = desktop_app_registry_start(desktop, handle);
	if (ret != 0) {
		LOG_WRN("App start failed: handle=0x%08x (%d)", (unsigned int)handle, ret);
		app_restore_desktop(desktop);
	}
	return ret;
}

bool desktop_open_app(struct zui_desktop *desktop, mbs_desktop_app_handle_t handle,
		      uint32_t return_screen_id)
{
	int ret;

	if (!atomic_cas(&lifecycle_busy, 0, 1)) {
		return false;
	}
	ret = app_open(desktop, handle, return_screen_id);
	lifecycle_end(desktop);
	return ret == 0;
}

#if defined(CONFIG_MBS_DESKTOP_LAUNCHER)
static int launcher_ready(struct zui_desktop *desktop)
{
	if (desktop == NULL || desktop->host == NULL) {
		return -ENODEV;
	}
	if (desktop->router == NULL ||
	    zui_router_current(desktop->router) != MBS_DESKTOP_VIEW_LAUNCHER ||
	    mbs_desktop_app_handle_is_valid(desktop->active_app_handle)) {
		return -EBUSY;
	}
	return 0;
}

static int external_app_start(struct zui_desktop *desktop,
			      const struct mbs_desktop_external_app_desc *desc,
			      mbs_desktop_app_handle_t *handle_out)
{
	mbs_desktop_app_handle_t handle;
	int ret;

	ret = launcher_ready(desktop);
	if (ret != 0) {
		return ret;
	}
	ret = desktop_app_registry_external_prepare(desc, &handle);
	if (ret != 0) {
		return ret;
	}
	ret = app_open(desktop, handle, MBS_DESKTOP_VIEW_LAUNCHER);
	if (ret != 0) {
		int release_ret = desktop_app_registry_external_release(handle);

		if (release_ret != 0) {
			LOG_ERR("Failed to release unstarted app: %d", release_ret);
		}
		return ret;
	}
	if (handle_out != NULL) {
		*handle_out = handle;
	}
	return 0;
}
#endif

int mbs_desktop_external_app_start(const struct mbs_desktop_external_app_desc *desc)
{
#if !defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	ARG_UNUSED(desc);
	return -ENOTSUP;
#else
	struct zui_desktop *desktop = zui_desktop_get_instance();
	int ret;

	if (!atomic_cas(&lifecycle_busy, 0, 1)) {
		return -EBUSY;
	}
	/* A generic external launch must not bypass a retained MBA Session. */
	ret = mba.session != NULL ? -EBUSY :
		external_app_start(desktop, desc, NULL);
	lifecycle_end(desktop);
	return ret;
#endif
}

static int mba_start_expected(struct zui_desktop *desktop, const char *path, const char *expected_id)
{
#if !defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	ARG_UNUSED(desktop);
	ARG_UNUSED(path);
	ARG_UNUSED(expected_id);
	return -ENOTSUP;
#else
	struct mbs_llext_app_info info;
	mbs_llext_app_entry_t entry;
	int ret;
	int cleanup_ret;

	if (path == NULL) {
		return -EINVAL;
	}
	if (!atomic_cas(&lifecycle_busy, 0, 1)) {
		return -EBUSY;
	}
	/* Share the gate with managed reservation, including the UI path. */
	k_spinlock_key_t key = k_spin_lock(&managed_lock);
	bool reserved = expected_id == NULL && !managed.resources_reclaimed;

	k_spin_unlock(&managed_lock, key);
	if (reserved) {
		ret = -EBUSY;
		goto out;
	}
	ret = launcher_ready(desktop);
	if (ret != 0) {
		goto out;
	}
	if (mbs_desktop_app_handle_is_valid(mba.handle)) {
		ret = -EBUSY;
		goto out;
	}
	/* Only an explicit new launch retries a previously failed reclamation. */
	ret = mba_reclaim();
	if (ret != 0) {
		goto out;
	}
	if (strlen(path) >= sizeof(mba.path)) {
		ret = -ENAMETOOLONG;
		goto out;
	}
	strcpy(mba.path, path);
	mba.owner = expected_id != NULL ? managed.session_id : 0U;
#if defined(CONFIG_MBS_DESKTOP_PACKAGES)
	ret = desktop_package_path_validate(path);
	if (ret != 0) {
		goto out;
	}
#endif
	ret = mbs_llext_app_load(path, &mba.session);
	if (ret != 0) {
		/* Load can return resources whose own failure cleanup did not finish. */
		goto out;
	}
	ret = mbs_llext_app_get_info(mba.session, &info);
	if (ret != 0) {
		goto reclaim;
	}
	if (expected_id != NULL && strcmp(expected_id, info.id) != 0) {
		ret = -EINVAL;
		goto reclaim;
	}
	ret = mbs_llext_app_get_entry(mba.session, &entry);
	if (ret != 0) {
		goto reclaim;
	}
	ret = external_app_start(desktop,
		&(const struct mbs_desktop_external_app_desc){
			.id = info.id,
			.display_name = info.name,
			.app_main = entry,
			.stack_size = info.stack_size,
			.user_data = mba.session,
		}, &mba.handle);
	if (ret == 0) {
		goto out;
	}

reclaim:
	cleanup_ret = mba_reclaim();
	if (cleanup_ret != 0) {
		LOG_ERR("MBA start failed: %d; reclaim failed: %d", ret, cleanup_ret);
		ret = cleanup_ret;
	}
out:
	lifecycle_end(desktop);
	return ret;
#endif
}

int desktop_mba_start(struct zui_desktop *desktop, const char *path)
{
	return mba_start_expected(desktop, path, NULL);
}

bool mbs_desktop_external_app_is_active(void)
{
#if !defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	return false;
#else
	struct zui_desktop *desktop = zui_desktop_get_instance();
	mbs_desktop_app_handle_t handle;

	if (desktop == NULL) {
		return false;
	}
	handle = desktop->active_app_handle;
	return desktop_app_registry_is_external_handle(handle) &&
	       desktop_app_registry_is_running(handle);
#endif
}

void zui_desktop_request_app_exit(struct zui_desktop *desktop)
{
	if (desktop != NULL) {
		atomic_set(&desktop->app_exit_pending, 1);
		k_sem_give(&desktop->redraw_sem);
	}
}

int desktop_app_complete_exit(struct zui_desktop *desktop, k_timeout_t timeout)
{
	mbs_desktop_app_handle_t handle;
	int ret;
	int cleanup_ret = 0;

	if (desktop == NULL) {
		return -EINVAL;
	}
	if (!atomic_cas(&lifecycle_busy, 0, 1)) {
		return -EBUSY;
	}
	if (!atomic_get(&desktop->app_exit_pending)) {
		ret = 0;
		goto out;
	}
	handle = desktop->active_app_handle;
	ret = desktop_app_registry_complete_exit(handle, timeout);
	if (ret != 0) {
		/* Keep the exit pending: neither the stack nor the MBA can be reused. */
		goto out;
	}

#if defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	if (handle == mba.handle) {
		cleanup_ret = mba_reclaim();
	}
#endif
	if (desktop_app_registry_is_external_handle(handle)) {
		ret = desktop_app_registry_external_release(handle);
		if (ret != 0) {
			LOG_ERR("Failed to release external app: %d", ret);
			goto out;
		}
	}
#if defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	if (handle == mba.handle) {
		mba.handle = MBS_DESKTOP_APP_HANDLE_INVALID;
	}
#endif
#if defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	managed_finish(cleanup_ret);
#endif
	atomic_clear(&desktop->app_exit_pending);
	app_restore_desktop(desktop);
#if defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	if (cleanup_ret != 0) {
		LOG_ERR("MBA reclaim failed: %d; retained for the next launch", cleanup_ret);
		desktop_launcher_show_cleanup_error(desktop, cleanup_ret);
	}
#endif
	ret = cleanup_ret;
out:
	lifecycle_end(desktop);
	return ret;
}

int mbs_desktop_mba_get_status(uint64_t session, mbs_desktop_mba_status *status)
{
	if (status == NULL) {
		return -EINVAL;
	}
#if !defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	ARG_UNUSED(session);
	return -ENOTSUP;
#else
	k_spinlock_key_t key = k_spin_lock(&managed_lock);

	if (session != 0U && session != managed.session_id) {
		k_spin_unlock(&managed_lock, key);
		return -ESTALE;
	}
	*status = managed;
	k_spin_unlock(&managed_lock, key);
	/* A manually launched app also forbids replacement, even if the last
	 * managed Session ended. Do not claim global reclamation while it runs.
	 */
	if (mbs_llext_runtime_busy() || mbs_desktop_external_app_is_active()) {
		status->resources_reclaimed = false;
	}
	return 0;
#endif
}

int mbs_desktop_mba_start(const char *id, const char *path, mbs_desktop_mba_status *status)
{
#if !defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	ARG_UNUSED(id); ARG_UNUSED(path); ARG_UNUSED(status);
	return -ENOTSUP;
#else
	struct zui_desktop *desktop = zui_desktop_get_instance();
	k_spinlock_key_t key;
	uint64_t epoch;
	int ret = 0;

	if (id == NULL || path == NULL || status == NULL || id[0] == '\0' ||
	    strlen(id) >= sizeof(managed.app_id) || strlen(path) >= sizeof(managed.path)) {
		return -EINVAL;
	}
	if (desktop == NULL || desktop->host == NULL) {
		return -ENODEV;
	}
	if (!atomic_cas(&lifecycle_busy, 0, 1)) {
		return -EBUSY;
	}
	if (mbs_llext_runtime_busy() || mbs_desktop_app_handle_is_valid(desktop->active_app_handle)) {
		ret = -EBUSY;
		goto out;
	}
	epoch = (uint64_t)sys_rand32_get() << 32;
	key = k_spin_lock(&managed_lock);
	if (managed_start_pending || !managed.resources_reclaimed) {
		k_spin_unlock(&managed_lock, key);
		ret = -EBUSY;
		goto out;
	}
	if (managed_sequence == UINT32_MAX) {
		k_spin_unlock(&managed_lock, key);
		ret = -EOVERFLOW;
		goto out;
	}
	if (managed_sequence == 0U) {
		managed_epoch = epoch;
	}
	managed = (mbs_desktop_mba_status){
		.protocol_version = 1U,
		.session_id = managed_epoch | ++managed_sequence,
		.state = MBA_STATE(ACCEPTED),
	};
	strcpy(managed.app_id, id);
	strcpy(managed.path, path);
	managed_start_pending = true;
	managed_stop_queued = false;
	*status = managed;
	/* Publish the queued request only after releasing the lifecycle gate.
	 * Desktop can preempt MCUmgr immediately when the spinlock is released,
	 * even before the explicit wakeup below.
	 */
	lifecycle_end(desktop);
	k_spin_unlock(&managed_lock, key);
	k_sem_give(&desktop->redraw_sem);
	return 0;
out:
	lifecycle_end(desktop);
	return ret;
#endif
}

int mbs_desktop_mba_stop(uint64_t session, uint32_t timeout_ms, mbs_desktop_mba_status *status)
{
#if !defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	ARG_UNUSED(session); ARG_UNUSED(timeout_ms); ARG_UNUSED(status);
	return -ENOTSUP;
#else
	struct zui_desktop *desktop = zui_desktop_get_instance();
	k_spinlock_key_t key;

	if (status == NULL || session == 0U || timeout_ms == 0U || timeout_ms > 30000U) {
		return -EINVAL;
	}
	if (desktop == NULL) {
		return -ENODEV;
	}
	key = k_spin_lock(&managed_lock);
	if (session != managed.session_id) {
		k_spin_unlock(&managed_lock, key);
		return -ESTALE;
	}
	if (managed_start_pending || managed.state == MBA_STATE(STARTING)) {
		k_spin_unlock(&managed_lock, key);
		return -EBUSY;
	}
	if (!managed.resources_reclaimed) {
		managed.state = MBA_STATE(STOPPING);
		managed.detail = 0;
		managed_stop_deadline = k_uptime_get() + timeout_ms;
		managed_stop_queued = true;
	}
	*status = managed;
	k_spin_unlock(&managed_lock, key);
	k_sem_give(&desktop->redraw_sem);
	return mbs_desktop_mba_get_status(session, status);
#endif
}

bool desktop_mba_stop_pending(void)
{
#if !defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	return false;
#else
	k_spinlock_key_t key = k_spin_lock(&managed_lock);
	bool pending = managed.state == MBA_STATE(STOPPING);

	k_spin_unlock(&managed_lock, key);
	return pending;
#endif
}

void desktop_mba_process_requests(struct zui_desktop *desktop)
{
#if !defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	ARG_UNUSED(desktop);
#else
	mbs_desktop_mba_status request;
	k_spinlock_key_t key = k_spin_lock(&managed_lock);
	bool start = managed_start_pending;
	bool stop = managed_stop_queued;
	int ret = 0;

	request = managed;
	managed_start_pending = false;
	managed_stop_queued = false;
	if (start) {
		managed.state = MBA_STATE(STARTING);
	}
	k_spin_unlock(&managed_lock, key);
	if (start) {
		if (mbs_desktop_app_handle_is_valid(desktop->active_app_handle)) {
			ret = -EBUSY;
		} else {
			ret = zui_desktop_switch(desktop, MBS_DESKTOP_VIEW_LAUNCHER);
			if (ret == 0) {
				ret = mba_start_expected(desktop, request.path, request.app_id);
			}
		}
		key = k_spin_lock(&managed_lock);
		managed.detail = ret;
		managed.state = ret == 0 ? MBA_STATE(RUNNING) : MBA_STATE(FAILED);
		managed.resources_reclaimed = ret != 0 && (mba.owner != request.session_id ||
			(mba.session == NULL && !mbs_desktop_app_handle_is_valid(mba.handle)));
		k_spin_unlock(&managed_lock, key);
	} else if (stop) {
		if (!atomic_cas(&lifecycle_busy, 0, 1)) {
			ret = -EBUSY;
		} else {
			if (mba.owner != request.session_id) {
				ret = -ESTALE;
			} else if (mbs_desktop_app_handle_is_valid(mba.handle)) {
				ret = desktop_app_registry_request_stop(mba.handle);
			} else {
				ret = mba_reclaim();
				managed_finish(ret);
			}
			lifecycle_end(desktop);
		}
		if (ret != 0) {
			key = k_spin_lock(&managed_lock);
			managed.state = MBA_STATE(FAILED);
			managed.detail = ret;
			k_spin_unlock(&managed_lock, key);
		}
	}
	key = k_spin_lock(&managed_lock);
	if (managed.state == MBA_STATE(STOPPING) && k_uptime_get() >= managed_stop_deadline) {
		managed.state = MBA_STATE(FAILED);
		managed.detail = -ETIMEDOUT;
	}
	k_spin_unlock(&managed_lock, key);
#endif
}

int mbs_desktop_app_resource_path(const char *name, char *path, size_t capacity)
{
#if !defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	ARG_UNUSED(name);
	ARG_UNUSED(path);
	ARG_UNUSED(capacity);
	return -ENOTSUP;
#else
	const char *slash;
	int length;

	/* The app owns this read: path is set before its thread starts, and
	 * cannot change until that thread has joined and reclamation finishes.
	 */
	if (name == NULL || path == NULL || capacity == 0U || name[0] == '\0' ||
	    name[0] == '.' || strchr(name, '/') != NULL || strchr(name, '\\') != NULL) {
		return -EINVAL;
	}
	if (mba.session == NULL) {
		return -ENODEV;
	}
	slash = strrchr(mba.path, '/');
	if (slash == NULL) {
		return -EINVAL;
	}
	length = snprintf(path, capacity, "%.*s/%s", (int)(slash - mba.path), mba.path, name);
	return length < 0 || (size_t)length >= capacity ? -ENAMETOOLONG : 0;
#endif
}
