/* SPDX-License-Identifier: Apache-2.0 */

#include "desktop_private.h"
#include "registry/apps_registry_prvi.h"

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#if defined(CONFIG_MBS_DESKTOP_LAUNCHER)
#include <llext/llext.h>
#endif
#include <zephyr/zui/zui.h>

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

#if defined(CONFIG_MBS_DESKTOP_LAUNCHER)
static struct {
	struct mbs_llext_app_session *session;
	mbs_desktop_app_handle_t handle;
} mba;

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

int desktop_mba_start(struct zui_desktop *desktop, const char *path)
{
#if !defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	ARG_UNUSED(desktop);
	ARG_UNUSED(path);
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
	ret = mbs_llext_app_load(path, &mba.session);
	if (ret != 0) {
		/* Load can return resources whose own failure cleanup did not finish. */
		goto out;
	}
	ret = mbs_llext_app_get_info(mba.session, &info);
	if (ret != 0) {
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
