/* SPDX-License-Identifier: Apache-2.0 */

#include "apps_registry_prvi.h"
#include "desktop_private.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <zui/zui.h>

#include <string.h>

#ifndef MBS_DESKTOP_APP_REGISTRY_MAX
#define MBS_DESKTOP_APP_REGISTRY_MAX 16
#endif

LOG_MODULE_DECLARE(mbs_desktop);

#define desktop_app_alloc k_malloc
#define desktop_app_free  k_free

#ifndef CONFIG_MBS_DESKTOP_APP_THREAD_PRIORITY
#define CONFIG_MBS_DESKTOP_APP_THREAD_PRIORITY 0
#endif

enum app_runtime_state {
	APP_RUNTIME_IDLE,
	APP_RUNTIME_RUNNING,
	APP_RUNTIME_RETURNING,
	APP_RUNTIME_REAPING,
};

struct app_runtime {
	struct k_thread thread;
	k_tid_t tid;
	struct mbs_desktop_app_args args;
	struct zui_desktop *desktop;
	void *user_data;
	mbs_desktop_external_app_cleanup_t cleanup;
	mbs_desktop_app_handle_t handle;
	uint8_t state;
};

#if defined(CONFIG_MBS_DESKTOP_LAUNCHER)
#define MBS_DESKTOP_EXTERNAL_APP_INDEX_PLUS1 UINT16_MAX
struct external_app_slot {
	struct mbs_desktop_app_desc desc;
	char *id;
	char *display_name;
	void *user_data;
	mbs_desktop_external_app_cleanup_t cleanup;
	bool occupied;
};
#endif

static K_MUTEX_DEFINE(app_registry_mutex);
static const struct mbs_desktop_app_desc *apps[MBS_DESKTOP_APP_REGISTRY_MAX];
static struct app_runtime runtime;
#if defined(CONFIG_MBS_DESKTOP_LAUNCHER)
static struct external_app_slot external_app;
#endif
static size_t app_count;
/* Global generation, bumped once at first freeze (0 is reserved).
 * If a future refresh/rebuild mechanism is added, it must bump this again so
 * stale handles become invalid.
 */
static uint8_t registry_epoch;
static bool frozen;

static bool app_id_equal(const char *a, const char *b)
{
	if (a == NULL || b == NULL) {
		return false;
	}
	return strcmp(a, b) == 0;
}

static bool app_desc_valid(const struct mbs_desktop_app_desc *app)
{
	if (app == NULL || app->id == NULL || app->display_name == NULL) {
		return false;
	}

	if (app->app_main == NULL || app->stack_size == 0U) {
		return false;
	}

	return true;
}

static bool app_any_running_locked(void)
{
	return runtime.state != APP_RUNTIME_IDLE;
}

static bool app_handle_is_external(mbs_desktop_app_handle_t handle)
{
#if !defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	ARG_UNUSED(handle);
	return false;
#else
	if (!mbs_desktop_app_handle_is_valid(handle)) {
		return false;
	}

	return MBS_DESKTOP_APP_HANDLE_GET_INDEX_PLUS1(handle) ==
	       MBS_DESKTOP_EXTERNAL_APP_INDEX_PLUS1;
#endif
}

#if defined(CONFIG_MBS_DESKTOP_LAUNCHER)
static bool app_id_exists_locked(const char *id)
{
	for (size_t i = 0; i < app_count; i++) {
		if (apps[i] != NULL && app_id_equal(apps[i]->id, id)) {
			return true;
		}
	}

#if defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	if (external_app.occupied && app_id_equal(external_app.desc.id, id)) {
		return true;
	}
#endif

	return false;
}
#endif

static int app_add_locked(const struct mbs_desktop_app_desc *app)
{
	size_t i;

	if (!app_desc_valid(app)) {
		return -EINVAL;
	}

	for (i = 0; i < app_count; i++) {
		if (apps[i] == app || app_id_equal(apps[i]->id, app->id)) {
			return -EALREADY;
		}
	}

	if (app_count >= ARRAY_SIZE(apps)) {
		return -ENOMEM;
	}

	apps[app_count++] = app;

	return 0;
}

static void app_registry_freeze_locked(void)
{
	if (frozen) {
		return;
	}

	/* bump epoch once per freeze/rebuild, keep 0 reserved */
	registry_epoch++;
	if (registry_epoch == 0u) {
		registry_epoch = 1u;
	}

	STRUCT_SECTION_FOREACH(mbs_desktop_app_desc, app) {
		int a = app_add_locked(app);
		if (a == 0 || a == -EALREADY) {
			continue;
		}

		/* Keep going to collect as many apps as possible, but don't fail silently. */
		if (a == -EINVAL) {
			LOG_WRN("Skipping invalid app desc: %p id=%s", app,
				(app && app->id) ? app->id : "(null)");
		} else if (a == -ENOMEM) {
			LOG_ERR("App registry full; dropping app id=%s", (app && app->id) ? app->id : "(null)");
		} else {
			LOG_WRN("Failed to add app id=%s (%d)", (app && app->id) ? app->id : "(null)", a);
		}
	}

	frozen = true;
}

static int app_registry_get_desc_locked(mbs_desktop_app_handle_t handle,
				 const struct mbs_desktop_app_desc **desc_out)
{
	uint8_t epoch;
	uint16_t index_plus1;
	size_t index;

	if (desc_out == NULL) {
		return -EINVAL;
	}

	*desc_out = NULL;

	if (!mbs_desktop_app_handle_is_valid(handle)) {
		return -EINVAL;
	}

	epoch = MBS_DESKTOP_APP_HANDLE_GET_EPOCH(handle);
	index_plus1 = MBS_DESKTOP_APP_HANDLE_GET_INDEX_PLUS1(handle);
	if (index_plus1 == 0u) {
		return -EINVAL;
	}

	app_registry_freeze_locked();
	if (epoch != registry_epoch) {
		return -ENOENT;
	}

#if defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	if (index_plus1 == MBS_DESKTOP_EXTERNAL_APP_INDEX_PLUS1) {
		if (!external_app.occupied) {
			return -ENOENT;
		}

		*desc_out = &external_app.desc;
		return 0;
	}
#endif

	index = (size_t)index_plus1 - 1U;
	if (index >= app_count) {
		return -ENOENT;
	}

	*desc_out = apps[index];
	return 0;
}

static void app_thread_entry(void *p1, void *p2, void *p3)
{
	const struct mbs_desktop_app_desc *desc = p1;
	struct zui_desktop *desktop;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	desc->app_main(&runtime.args);

	/* The shared stack and arguments remain owned until the reaper joins us. */
	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	desktop = runtime.desktop;
	runtime.state = APP_RUNTIME_RETURNING;
	k_mutex_unlock(&app_registry_mutex);

	zui_desktop_request_app_exit(desktop);
}

int desktop_app_registry_detach_desktop_all(struct zui_desktop *desktop)
{
	int ret;

	if (desktop == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	ret = app_any_running_locked() ? -EBUSY : 0;
	k_mutex_unlock(&app_registry_mutex);
	return ret;
}

bool desktop_app_registry_is_external_handle(mbs_desktop_app_handle_t handle)
{
	return app_handle_is_external(handle);
}

int desktop_app_registry_external_prepare(const struct mbs_desktop_external_app_desc *desc,
					  mbs_desktop_app_handle_t *handle_out)
{
#if !defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	ARG_UNUSED(desc);
	if (handle_out != NULL) {
		*handle_out = MBS_DESKTOP_APP_HANDLE_INVALID;
	}
	return -ENOTSUP;
#else
	char *id_copy = NULL;
	char *display_name_copy = NULL;
	int ret = 0;

	if (desc == NULL || desc->id == NULL || desc->id[0] == '\0' ||
	    desc->display_name == NULL || desc->display_name[0] == '\0' ||
	    desc->app_main == NULL || desc->stack_size == 0U ||
	    desc->stack_size > CONFIG_MBS_DESKTOP_APP_SHARED_STACK_SIZE ||
	    handle_out == NULL) {
		return -EINVAL;
	}

	*handle_out = MBS_DESKTOP_APP_HANDLE_INVALID;

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	app_registry_freeze_locked();

	if (external_app.occupied) {
		ret = app_id_equal(external_app.desc.id, desc->id) ? -EEXIST : -EBUSY;
		goto out_unlock;
	}

	if (app_id_exists_locked(desc->id)) {
		ret = -EEXIST;
		goto out_unlock;
	}

	id_copy = desktop_app_alloc(strlen(desc->id) + 1U);
	if (id_copy == NULL) {
		ret = -ENOMEM;
		goto out_unlock;
	}
	strcpy(id_copy, desc->id);
	display_name_copy = desktop_app_alloc(strlen(desc->display_name) + 1U);
	if (display_name_copy == NULL) {
		ret = -ENOMEM;
		goto out_unlock;
	}
	strcpy(display_name_copy, desc->display_name);

	external_app.desc.id = id_copy;
	external_app.desc.display_name = display_name_copy;
	external_app.desc.app_main = desc->app_main;
	external_app.desc.stack = NULL;
	external_app.desc.stack_size = desc->stack_size;
	external_app.desc.menu_icon = NULL;
	external_app.desc.menu_index = MBS_DESKTOP_APP_MENU_INDEX_NONE;

	external_app.user_data = desc->user_data;
	external_app.cleanup = desc->cleanup;

	external_app.id = id_copy;
	external_app.display_name = display_name_copy;
	external_app.occupied = true;
	*handle_out = MBS_DESKTOP_APP_HANDLE_MAKE(registry_epoch,
						       MBS_DESKTOP_EXTERNAL_APP_INDEX_PLUS1);

	id_copy = NULL;
	display_name_copy = NULL;

out_unlock:
	k_mutex_unlock(&app_registry_mutex);
	if (id_copy != NULL) {
		desktop_app_free(id_copy);
	}
	if (display_name_copy != NULL) {
		desktop_app_free(display_name_copy);
	}

	return ret;
#endif
}

int desktop_app_registry_external_release(mbs_desktop_app_handle_t handle)
{
#if !defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	ARG_UNUSED(handle);
	return -ENOTSUP;
#else
	char *id = NULL;
	char *display_name = NULL;
	int ret = 0;

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	app_registry_freeze_locked();

	if (!app_handle_is_external(handle)) {
		ret = -EINVAL;
		goto out_unlock;
	}

	if (MBS_DESKTOP_APP_HANDLE_GET_EPOCH(handle) != registry_epoch || !external_app.occupied) {
		ret = -ENOENT;
		goto out_unlock;
	}

	if (app_any_running_locked() && runtime.handle == handle) {
		ret = -EBUSY;
		goto out_unlock;
	}

	id = external_app.id;
	display_name = external_app.display_name;
	memset(&external_app, 0, sizeof(external_app));

out_unlock:
	k_mutex_unlock(&app_registry_mutex);

	if (id != NULL) {
		desktop_app_free(id);
	}
	if (display_name != NULL) {
		desktop_app_free(display_name);
	}

	return ret;
#endif
}

size_t mbs_desktop_app_registry_count(void)
{
	size_t count;

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	app_registry_freeze_locked();
	count = app_count;
	k_mutex_unlock(&app_registry_mutex);

	return count;
}

const struct mbs_desktop_app_desc *mbs_desktop_app_registry_get(size_t index)
{
	const struct mbs_desktop_app_desc *desc = NULL;

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	app_registry_freeze_locked();
	if (index < app_count) {
		desc = apps[index];
	}
	k_mutex_unlock(&app_registry_mutex);

	return desc;
}

mbs_desktop_app_handle_t mbs_desktop_app_registry_get_handle(size_t index)
{
	mbs_desktop_app_handle_t handle = MBS_DESKTOP_APP_HANDLE_INVALID;

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	app_registry_freeze_locked();
	if (index < app_count) {
		uint16_t index_plus1 = (uint16_t)(index + 1U);
		handle = MBS_DESKTOP_APP_HANDLE_MAKE(registry_epoch, index_plus1);
	}
	k_mutex_unlock(&app_registry_mutex);

	return handle;
}

int mbs_desktop_app_registry_resolve_handle(mbs_desktop_app_handle_t handle,
				    const struct mbs_desktop_app_desc **desc_out)
{
	int ret;

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	ret = app_registry_get_desc_locked(handle, desc_out);
	k_mutex_unlock(&app_registry_mutex);
	return ret;
}

mbs_desktop_app_handle_t mbs_desktop_app_registry_handle_from_id(const char *id)
{
	mbs_desktop_app_handle_t handle = MBS_DESKTOP_APP_HANDLE_INVALID;
	size_t i;

	if (id == NULL) {
		return MBS_DESKTOP_APP_HANDLE_INVALID;
	}

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	app_registry_freeze_locked();
	for (i = 0; i < app_count; i++) {
		if (apps[i] != NULL && app_id_equal(apps[i]->id, id)) {
			handle = MBS_DESKTOP_APP_HANDLE_MAKE(registry_epoch, (uint16_t)(i + 1U));
			break;
		}
	}
	k_mutex_unlock(&app_registry_mutex);

	return handle;
}

const struct mbs_desktop_app_desc *mbs_desktop_app_registry_get_by_id(const char *id)
{
	mbs_desktop_app_handle_t h = mbs_desktop_app_registry_handle_from_id(id);
	const struct mbs_desktop_app_desc *desc;

	if (h == MBS_DESKTOP_APP_HANDLE_INVALID) {
		return NULL;
	}

	if (mbs_desktop_app_registry_resolve_handle(h, &desc) != 0) {
		return NULL;
	}

	return desc;
}

int desktop_app_registry_start(struct zui_desktop *desktop,
				   mbs_desktop_app_handle_t handle)
{
	const struct mbs_desktop_app_desc *desc = NULL;
	k_thread_stack_t *shared_stack;
	size_t shared_stack_size;
	int ret;

	if (desktop == NULL || desktop->host == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	ret = app_registry_get_desc_locked(handle, &desc);
	if (ret != 0) {
		goto out_unlock;
	}
	if (app_any_running_locked()) {
		ret = runtime.handle == handle ? -EALREADY : -EBUSY;
		goto out_unlock;
	}
	if (!app_desc_valid(desc)) {
		ret = -EINVAL;
		goto out_unlock;
	}

	shared_stack = desktop->app_shared_stack;
	shared_stack_size = desktop->app_shared_stack_size;
	if (shared_stack == NULL || shared_stack_size == 0U) {
		ret = -ENOMEM;
		goto out_unlock;
	}
	if (desc->stack_size > shared_stack_size) {
		ret = -EINVAL;
		goto out_unlock;
	}

	runtime.args = (struct mbs_desktop_app_args){
		.app_id = desc->id,
		.display_name = desc->display_name,
		.host = desktop->host,
	};
	runtime.cleanup = NULL;
	runtime.user_data = NULL;
#if defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	if (app_handle_is_external(handle)) {
		runtime.args.user_data = external_app.user_data;
		runtime.user_data = external_app.user_data;
		runtime.cleanup = external_app.cleanup;
	}
#endif
	runtime.handle = handle;
	runtime.desktop = desktop;
	runtime.state = APP_RUNTIME_RUNNING;
	runtime.tid = k_thread_create(&runtime.thread, shared_stack, shared_stack_size,
				      app_thread_entry, (void *)desc, NULL, NULL,
				      CONFIG_MBS_DESKTOP_APP_THREAD_PRIORITY, 0, K_NO_WAIT);
	if (runtime.tid == NULL) {
		runtime.state = APP_RUNTIME_IDLE;
		runtime.desktop = NULL;
		runtime.handle = MBS_DESKTOP_APP_HANDLE_INVALID;
		runtime.args = (struct mbs_desktop_app_args){0};
		runtime.user_data = NULL;
		runtime.cleanup = NULL;
		ret = -EIO;
		goto out_unlock;
	}
	k_thread_name_set(runtime.tid, desc->id);
	ret = 0;

out_unlock:
	k_mutex_unlock(&app_registry_mutex);
	return ret;
}

int desktop_app_registry_complete_exit(mbs_desktop_app_handle_t handle,
				       k_timeout_t timeout)
{
	const struct mbs_desktop_app_desc *desc;
	mbs_desktop_external_app_cleanup_t cleanup;
	void *user_data;
	k_tid_t tid;
	int ret;

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	ret = app_registry_get_desc_locked(handle, &desc);
	if (ret != 0) {
		k_mutex_unlock(&app_registry_mutex);
		return ret;
	}
	if (!app_any_running_locked() || runtime.handle != handle) {
		k_mutex_unlock(&app_registry_mutex);
		return 0;
	}
	if (runtime.state != APP_RUNTIME_RETURNING || runtime.tid == NULL) {
		k_mutex_unlock(&app_registry_mutex);
		return -EBUSY;
	}
	tid = runtime.tid;
	if (tid == k_current_get()) {
		k_mutex_unlock(&app_registry_mutex);
		return -EDEADLK;
	}
	/* Only one reaper may join/clean up this session while the lock is dropped. */
	runtime.state = APP_RUNTIME_REAPING;
	k_mutex_unlock(&app_registry_mutex);

	ret = k_thread_join(tid, timeout);
	if (ret != 0) {
		k_mutex_lock(&app_registry_mutex, K_FOREVER);
		runtime.state = APP_RUNTIME_RETURNING;
		k_mutex_unlock(&app_registry_mutex);
		return ret;
	}

	cleanup = runtime.cleanup;
	user_data = runtime.user_data;
	if (cleanup != NULL) {
		cleanup(user_data);
	}

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
#if defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	if (app_handle_is_external(handle)) {
		external_app.user_data = NULL;
		external_app.cleanup = NULL;
	}
#endif
	runtime.state = APP_RUNTIME_IDLE;
	runtime.tid = NULL;
	runtime.desktop = NULL;
	runtime.handle = MBS_DESKTOP_APP_HANDLE_INVALID;
	runtime.args = (struct mbs_desktop_app_args){0};
	runtime.user_data = NULL;
	runtime.cleanup = NULL;
	k_mutex_unlock(&app_registry_mutex);
	return 0;
}

bool desktop_app_registry_is_running(mbs_desktop_app_handle_t handle)
{
	const struct mbs_desktop_app_desc *desc;
	bool active;

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	active = app_registry_get_desc_locked(handle, &desc) == 0 &&
		 app_any_running_locked() && runtime.handle == handle;
	k_mutex_unlock(&app_registry_mutex);
	return active;
}
