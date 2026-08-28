/* SPDX-License-Identifier: Apache-2.0 */

#include "apps_registry_prvi.h"
#include "desktop_private.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <zephyr/zui/zui.h>

#include <string.h>

#ifndef MESHBUS_DESKTOP_APP_REGISTRY_MAX
#define MESHBUS_DESKTOP_APP_REGISTRY_MAX 16
#endif

LOG_MODULE_DECLARE(meshbus_desktop);

#define desktop_app_alloc k_malloc
#define desktop_app_free  k_free

#ifndef CONFIG_MESHBUS_DESKTOP_APP_THREAD_PRIORITY
#define CONFIG_MESHBUS_DESKTOP_APP_THREAD_PRIORITY 0
#endif

enum app_runtime_state {
	APP_RUNTIME_IDLE,
	APP_RUNTIME_RUNNING,
	APP_RUNTIME_RETURNING,
};

struct app_runtime {
	struct k_thread thread;
	k_tid_t tid;
	struct meshbus_desktop_app_args *args;
	struct zui_desktop *desktop;
	void *user_data;
	meshbus_desktop_external_app_cleanup_t cleanup;
	struct k_sem exit_sem;
	uint8_t state;
};

struct app_entry {
	const struct meshbus_desktop_app_desc *desc;
	struct app_runtime runtime;
};

#if defined(CONFIG_MESHBUS_DESKTOP_LAUNCHER)
#define MESHBUS_DESKTOP_EXTERNAL_APP_INDEX_PLUS1 UINT16_MAX
struct external_app_slot {
	struct app_entry entry;
	struct meshbus_desktop_app_desc desc;
	char *id;
	char *display_name;
	bool occupied;
};
#endif

static K_MUTEX_DEFINE(app_registry_mutex);
static struct app_entry apps[MESHBUS_DESKTOP_APP_REGISTRY_MAX];
#if defined(CONFIG_MESHBUS_DESKTOP_LAUNCHER)
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

static bool app_desc_valid(const struct meshbus_desktop_app_desc *app)
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
	for (size_t i = 0U; i < app_count; i++) {
		if (apps[i].runtime.state != APP_RUNTIME_IDLE) {
			return true;
		}
	}

#if defined(CONFIG_MESHBUS_DESKTOP_LAUNCHER)
	return external_app.occupied &&
	       external_app.entry.runtime.state != APP_RUNTIME_IDLE;
#else
	return false;
#endif
}

static bool app_handle_is_external(meshbus_desktop_app_handle_t handle)
{
#if !defined(CONFIG_MESHBUS_DESKTOP_LAUNCHER)
	ARG_UNUSED(handle);
	return false;
#else
	if (!meshbus_desktop_app_handle_is_valid(handle)) {
		return false;
	}

	return MESHBUS_DESKTOP_APP_HANDLE_GET_INDEX_PLUS1(handle) ==
	       MESHBUS_DESKTOP_EXTERNAL_APP_INDEX_PLUS1;
#endif
}

#if defined(CONFIG_MESHBUS_DESKTOP_LAUNCHER)
static bool app_id_exists_locked(const char *id)
{
	for (size_t i = 0; i < app_count; i++) {
		if (apps[i].desc != NULL && app_id_equal(apps[i].desc->id, id)) {
			return true;
		}
	}

#if defined(CONFIG_MESHBUS_DESKTOP_LAUNCHER)
	if (external_app.occupied && app_id_equal(external_app.desc.id, id)) {
		return true;
	}
#endif

	return false;
}
#endif

#if defined(CONFIG_MESHBUS_DESKTOP_LAUNCHER)
static void external_app_slot_clear_locked(void)
{
	external_app.entry.desc = NULL;
	external_app.entry.runtime.tid = NULL;
	external_app.entry.runtime.args = NULL;
	external_app.entry.runtime.desktop = NULL;
	external_app.entry.runtime.user_data = NULL;
	external_app.entry.runtime.cleanup = NULL;
	external_app.entry.runtime.state = APP_RUNTIME_IDLE;

	external_app.desc.id = NULL;
	external_app.desc.display_name = NULL;
	external_app.desc.app_main = NULL;
	external_app.desc.stack = NULL;
	external_app.desc.stack_size = 0U;
	external_app.desc.menu_icon = NULL;
	external_app.desc.menu_index = MESHBUS_DESKTOP_APP_MENU_INDEX_NONE;

	external_app.id = NULL;
	external_app.display_name = NULL;
	external_app.occupied = false;
}
#endif

static int app_add_locked(const struct meshbus_desktop_app_desc *app)
{
	size_t i;

	if (!app_desc_valid(app)) {
		return -EINVAL;
	}

	for (i = 0; i < app_count; i++) {
		if (apps[i].desc == app || app_id_equal(apps[i].desc->id, app->id)) {
			return -EALREADY;
		}
	}

	if (app_count >= ARRAY_SIZE(apps)) {
		return -ENOMEM;
	}

	apps[app_count].desc = app;
	apps[app_count].runtime.tid = NULL;
	apps[app_count].runtime.args = NULL;
	apps[app_count].runtime.desktop = NULL;
	apps[app_count].runtime.user_data = NULL;
	apps[app_count].runtime.cleanup = NULL;
	apps[app_count].runtime.state = APP_RUNTIME_IDLE;
	k_sem_init(&apps[app_count].runtime.exit_sem, 0, 1);
	app_count++;

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

	STRUCT_SECTION_FOREACH(meshbus_desktop_app_desc, app) {
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

static int app_registry_get_entry_locked(meshbus_desktop_app_handle_t handle,
				 struct app_entry **entry_out)
{
	uint8_t epoch;
	uint16_t index_plus1;
	size_t index;

	if (entry_out == NULL) {
		return -EINVAL;
	}

	*entry_out = NULL;

	if (!meshbus_desktop_app_handle_is_valid(handle)) {
		return -EINVAL;
	}

	epoch = MESHBUS_DESKTOP_APP_HANDLE_GET_EPOCH(handle);
	index_plus1 = MESHBUS_DESKTOP_APP_HANDLE_GET_INDEX_PLUS1(handle);
	if (index_plus1 == 0u) {
		return -EINVAL;
	}

	app_registry_freeze_locked();
	if (epoch != registry_epoch) {
		return -ENOENT;
	}

#if defined(CONFIG_MESHBUS_DESKTOP_LAUNCHER)
	if (index_plus1 == MESHBUS_DESKTOP_EXTERNAL_APP_INDEX_PLUS1) {
		if (!external_app.occupied || external_app.entry.desc == NULL) {
			return -ENOENT;
		}

		*entry_out = &external_app.entry;
		return 0;
	}
#endif

	index = (size_t)index_plus1 - 1U;
	if (index >= app_count) {
		return -ENOENT;
	}

	*entry_out = &apps[index];
	return 0;
}

static void app_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct app_entry *entry = p1;

	if (entry == NULL || entry->desc == NULL) {
		return;
	}

	struct meshbus_desktop_app_args *args = NULL;
	struct zui_desktop *desktop = NULL;

	/* Snapshot args under lock for consistency. */
	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	args = entry->runtime.args;
	k_mutex_unlock(&app_registry_mutex);

	if (entry->desc->app_main != NULL) {
		entry->desc->app_main(args);
	}

	/* Keep cleanup state owned by the registry until the reaper has joined us. */
	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	desktop = entry->runtime.desktop;
	entry->runtime.args = NULL;
	k_mutex_unlock(&app_registry_mutex);

	if (args != NULL) {
		desktop_app_free(args);
	}

	/* The app thread still owns the shared stack until another thread joins it. */
	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	entry->runtime.state = APP_RUNTIME_RETURNING;
	k_mutex_unlock(&app_registry_mutex);

	zui_desktop_request_app_exit(desktop);
}

int desktop_app_registry_detach_desktop(meshbus_desktop_app_handle_t handle)
{
	struct app_entry *entry = NULL;
	int ret;

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	ret = app_registry_get_entry_locked(handle, &entry);
	if (ret == 0 && entry != NULL && entry->runtime.state != APP_RUNTIME_IDLE) {
		ret = -EBUSY;
	} else if (ret == 0 && entry != NULL) {
		entry->runtime.desktop = NULL;
	}
	k_mutex_unlock(&app_registry_mutex);

	return ret;
}

int desktop_app_registry_detach_desktop_all(struct zui_desktop *desktop)
{
	int ret = 0;

	if (desktop == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	app_registry_freeze_locked();
	if (app_any_running_locked()) {
		ret = -EBUSY;
		goto out_unlock;
	}

	for (size_t i = 0; i < app_count; i++) {
		if (apps[i].runtime.desktop == desktop) {
			apps[i].runtime.desktop = NULL;
		}
	}
#if defined(CONFIG_MESHBUS_DESKTOP_LAUNCHER)
	if (external_app.occupied && external_app.entry.runtime.desktop == desktop) {
		external_app.entry.runtime.desktop = NULL;
	}
#endif

out_unlock:
	k_mutex_unlock(&app_registry_mutex);

	return ret;
}

bool desktop_app_registry_is_external_handle(meshbus_desktop_app_handle_t handle)
{
	return app_handle_is_external(handle);
}

int desktop_app_registry_external_prepare(const struct meshbus_desktop_external_app_desc *desc,
					  meshbus_desktop_app_handle_t *handle_out)
{
#if !defined(CONFIG_MESHBUS_DESKTOP_LAUNCHER)
	ARG_UNUSED(desc);
	if (handle_out != NULL) {
		*handle_out = MESHBUS_DESKTOP_APP_HANDLE_INVALID;
	}
	return -ENOTSUP;
#else
	char *id_copy = NULL;
	char *display_name_copy = NULL;
	int ret = 0;

	if (desc == NULL || desc->id == NULL || desc->id[0] == '\0' ||
	    desc->display_name == NULL || desc->display_name[0] == '\0' ||
	    desc->app_main == NULL || desc->stack_size == 0U ||
	    desc->stack_size > CONFIG_MESHBUS_DESKTOP_APP_SHARED_STACK_SIZE ||
	    handle_out == NULL) {
		return -EINVAL;
	}

	*handle_out = MESHBUS_DESKTOP_APP_HANDLE_INVALID;

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
	external_app.desc.menu_index = MESHBUS_DESKTOP_APP_MENU_INDEX_NONE;

	external_app.entry.desc = &external_app.desc;
	external_app.entry.runtime.tid = NULL;
	external_app.entry.runtime.args = NULL;
	external_app.entry.runtime.desktop = NULL;
	external_app.entry.runtime.user_data = desc->user_data;
	external_app.entry.runtime.cleanup = desc->cleanup;
	external_app.entry.runtime.state = APP_RUNTIME_IDLE;
	k_sem_init(&external_app.entry.runtime.exit_sem, 0, 1);

	external_app.id = id_copy;
	external_app.display_name = display_name_copy;
	external_app.occupied = true;
	*handle_out = MESHBUS_DESKTOP_APP_HANDLE_MAKE(registry_epoch,
						       MESHBUS_DESKTOP_EXTERNAL_APP_INDEX_PLUS1);

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

int desktop_app_registry_external_release(meshbus_desktop_app_handle_t handle)
{
#if !defined(CONFIG_MESHBUS_DESKTOP_LAUNCHER)
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

	if (MESHBUS_DESKTOP_APP_HANDLE_GET_EPOCH(handle) != registry_epoch || !external_app.occupied) {
		ret = -ENOENT;
		goto out_unlock;
	}

	if (external_app.entry.runtime.state != APP_RUNTIME_IDLE) {
		ret = -EBUSY;
		goto out_unlock;
	}

	id = external_app.id;
	display_name = external_app.display_name;
	external_app_slot_clear_locked();

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

size_t meshbus_desktop_app_registry_count(void)
{
	size_t count;

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	app_registry_freeze_locked();
	count = app_count;
	k_mutex_unlock(&app_registry_mutex);

	return count;
}

const struct meshbus_desktop_app_desc *meshbus_desktop_app_registry_get(size_t index)
{
	const struct meshbus_desktop_app_desc *desc = NULL;

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	app_registry_freeze_locked();
	if (index < app_count) {
		desc = apps[index].desc;
	}
	k_mutex_unlock(&app_registry_mutex);

	return desc;
}

meshbus_desktop_app_handle_t meshbus_desktop_app_registry_get_handle(size_t index)
{
	meshbus_desktop_app_handle_t handle = MESHBUS_DESKTOP_APP_HANDLE_INVALID;

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	app_registry_freeze_locked();
	if (index < app_count) {
		uint16_t index_plus1 = (uint16_t)(index + 1U);
		handle = MESHBUS_DESKTOP_APP_HANDLE_MAKE(registry_epoch, index_plus1);
	}
	k_mutex_unlock(&app_registry_mutex);

	return handle;
}

int meshbus_desktop_app_registry_resolve_handle(meshbus_desktop_app_handle_t handle,
				    const struct meshbus_desktop_app_desc **desc_out)
{
	struct app_entry *entry = NULL;
	int ret;

	if (desc_out == NULL) {
		return -EINVAL;
	}

	*desc_out = NULL;

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	ret = app_registry_get_entry_locked(handle, &entry);
	if (ret == 0 && entry != NULL) {
		*desc_out = entry->desc;
	}
	k_mutex_unlock(&app_registry_mutex);

	return ret;
}

meshbus_desktop_app_handle_t meshbus_desktop_app_registry_handle_from_id(const char *id)
{
	meshbus_desktop_app_handle_t handle = MESHBUS_DESKTOP_APP_HANDLE_INVALID;
	size_t i;

	if (id == NULL) {
		return MESHBUS_DESKTOP_APP_HANDLE_INVALID;
	}

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	app_registry_freeze_locked();
	for (i = 0; i < app_count; i++) {
		if (apps[i].desc != NULL && app_id_equal(apps[i].desc->id, id)) {
			handle = MESHBUS_DESKTOP_APP_HANDLE_MAKE(registry_epoch, (uint16_t)(i + 1U));
			break;
		}
	}
	k_mutex_unlock(&app_registry_mutex);

	return handle;
}

const struct meshbus_desktop_app_desc *meshbus_desktop_app_registry_get_by_id(const char *id)
{
	meshbus_desktop_app_handle_t h = meshbus_desktop_app_registry_handle_from_id(id);
	const struct meshbus_desktop_app_desc *desc;

	if (h == MESHBUS_DESKTOP_APP_HANDLE_INVALID) {
		return NULL;
	}

	if (meshbus_desktop_app_registry_resolve_handle(h, &desc) != 0) {
		return NULL;
	}

	return desc;
}

int desktop_app_registry_start(struct zui_desktop *desktop,
				   meshbus_desktop_app_handle_t handle)
{
	struct app_entry *entry = NULL;
	struct meshbus_desktop_app_args *args;
	k_thread_stack_t *shared_stack;
	size_t shared_stack_size;
	int ret;

	if (desktop == NULL || desktop->host == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	ret = app_registry_get_entry_locked(handle, &entry);
	if (ret != 0 || entry == NULL || entry->desc == NULL) {
		k_mutex_unlock(&app_registry_mutex);
		return (ret != 0) ? ret : -ENOENT;
	}

	if (entry->runtime.state != APP_RUNTIME_IDLE) {
		k_mutex_unlock(&app_registry_mutex);
		return -EALREADY;
	}

	if (!app_desc_valid(entry->desc)) {
		k_mutex_unlock(&app_registry_mutex);
		return -EINVAL;
	}

	if (app_any_running_locked()) {
		k_mutex_unlock(&app_registry_mutex);
		return -EBUSY;
	}

	shared_stack = desktop->app_shared_stack;
	shared_stack_size = desktop->app_shared_stack_size;
	if (shared_stack == NULL || shared_stack_size == 0U) {
		k_mutex_unlock(&app_registry_mutex);
		return -ENOMEM;
	}
	if (entry->desc->stack_size > shared_stack_size) {
		k_mutex_unlock(&app_registry_mutex);
		return -EINVAL;
	}

	args = desktop_app_alloc(sizeof(*args));
	if (args == NULL) {
		k_mutex_unlock(&app_registry_mutex);
		return -ENOMEM;
	}

	args->app_id = entry->desc->id;
	args->display_name = entry->desc->display_name;
	args->host = desktop->host;
	args->user_data = entry->runtime.user_data;

	entry->runtime.args = args;
	entry->runtime.desktop = desktop;
	entry->runtime.state = APP_RUNTIME_RUNNING;
	entry->runtime.tid = NULL;
	k_sem_reset(&entry->runtime.exit_sem);

	entry->runtime.tid = k_thread_create(&entry->runtime.thread,
					     shared_stack,
					     shared_stack_size,
					     app_thread_entry,
					     entry, NULL, NULL,
					     CONFIG_MESHBUS_DESKTOP_APP_THREAD_PRIORITY,
					     0,
					     K_NO_WAIT);

	if (entry->runtime.tid == NULL) {
		entry->runtime.state = APP_RUNTIME_IDLE;
		entry->runtime.args = NULL;
		entry->runtime.desktop = NULL;
		k_mutex_unlock(&app_registry_mutex);
		desktop_app_free(args);
		return -EIO;
	}

	if (entry->desc->id != NULL) {
		k_thread_name_set(entry->runtime.tid, entry->desc->id);
	}

	k_mutex_unlock(&app_registry_mutex);

	return 0;
}

int desktop_app_registry_complete_exit(meshbus_desktop_app_handle_t handle,
				       k_timeout_t timeout)
{
	struct app_entry *entry = NULL;
	meshbus_desktop_external_app_cleanup_t cleanup;
	void *user_data;
	k_tid_t tid;
	int ret;

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	ret = app_registry_get_entry_locked(handle, &entry);
	if (ret != 0 || entry == NULL) {
		k_mutex_unlock(&app_registry_mutex);
		return (ret != 0) ? ret : -ENOENT;
	}
	if (entry->runtime.state == APP_RUNTIME_IDLE) {
		k_mutex_unlock(&app_registry_mutex);
		return 0;
	}
	if (entry->runtime.state != APP_RUNTIME_RETURNING || entry->runtime.tid == NULL) {
		k_mutex_unlock(&app_registry_mutex);
		return -EBUSY;
	}

	tid = entry->runtime.tid;
	k_mutex_unlock(&app_registry_mutex);

	if (tid == k_current_get()) {
		return -EDEADLK;
	}

	ret = k_thread_join(tid, timeout);
	if (ret != 0) {
		return ret;
	}

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	if (entry->runtime.state != APP_RUNTIME_RETURNING || entry->runtime.tid != tid) {
		k_mutex_unlock(&app_registry_mutex);
		return -EAGAIN;
	}
	cleanup = entry->runtime.cleanup;
	user_data = entry->runtime.user_data;
	k_mutex_unlock(&app_registry_mutex);

	if (cleanup != NULL) {
		cleanup(user_data);
	}

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	if (entry->runtime.state != APP_RUNTIME_RETURNING || entry->runtime.tid != tid) {
		k_mutex_unlock(&app_registry_mutex);
		return -EAGAIN;
	}
	entry->runtime.state = APP_RUNTIME_IDLE;
	entry->runtime.tid = NULL;
	entry->runtime.desktop = NULL;
	entry->runtime.user_data = NULL;
	entry->runtime.cleanup = NULL;
	k_mutex_unlock(&app_registry_mutex);

	k_sem_give(&entry->runtime.exit_sem);
	return 0;
}

int desktop_app_registry_wait_exit(meshbus_desktop_app_handle_t handle,
				      k_timeout_t timeout)
{
	struct app_entry *entry = NULL;
	bool active;
	int ret;

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	ret = app_registry_get_entry_locked(handle, &entry);
	if (ret != 0 || entry == NULL) {
		k_mutex_unlock(&app_registry_mutex);
		return (ret != 0) ? ret : -ENOENT;
	}
	active = entry->runtime.state != APP_RUNTIME_IDLE;
	k_mutex_unlock(&app_registry_mutex);

	if (!active) {
		return 0;
	}

	return k_sem_take(&entry->runtime.exit_sem, timeout);
}

bool desktop_app_registry_is_running(meshbus_desktop_app_handle_t handle)
{
	struct app_entry *entry = NULL;
	bool active = false;

	k_mutex_lock(&app_registry_mutex, K_FOREVER);
	if (app_registry_get_entry_locked(handle, &entry) == 0 && entry != NULL) {
		active = entry->runtime.state != APP_RUNTIME_IDLE;
	}
	k_mutex_unlock(&app_registry_mutex);

	return active;
}
