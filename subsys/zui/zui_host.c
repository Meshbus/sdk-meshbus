/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zui/core.h>
#include <zui/host.h>

#include "zui_host_internal.h"
#include "zui_mem.h"
#include "zui_screen_internal.h"

static struct zui_host *zui_default_host;
static bool zui_default_host_owned;

static void zui_host_screen_invalidated(struct zui_screen *screen, void *user_data)
{
	ARG_UNUSED(screen);

	(void)zui_host_request_redraw(user_data);
}

static bool zui_layer_valid(enum zui_layer layer)
{
	return layer >= 0 && layer < ZUI_LAYER_COUNT;
}

static struct zui_router_entry *zui_router_find_entry(const struct zui_router *router,
						      uint32_t screen_id)
{
	if (router == NULL || screen_id == 0U) {
		return NULL;
	}

	for (size_t i = 0; i < ARRAY_SIZE(router->entries); i++) {
		if (router->entries[i].used && router->entries[i].id == screen_id) {
			return (struct zui_router_entry *)&router->entries[i];
		}
	}

	return NULL;
}

static void zui_router_set_screen_invalidation(struct zui_router *router, struct zui_screen *screen)
{
	if (router == NULL || screen == NULL) {
		return;
	}

	if (router->attached && router->host != NULL) {
		(void)zui_screen_set_invalidate_callback(screen, zui_host_screen_invalidated,
							 router->host);
	} else {
		(void)zui_screen_set_invalidate_callback(screen, NULL, NULL);
	}
}

static void zui_router_set_host(struct zui_router *router, struct zui_host *host,
				enum zui_layer layer)
{
	if (router == NULL) {
		return;
	}

	router->host = host;
	router->layer = layer;
	router->attached = host != NULL;

	for (size_t i = 0; i < ARRAY_SIZE(router->entries); i++) {
		if (router->entries[i].used) {
			zui_router_set_screen_invalidation(router, router->entries[i].screen);
		}
	}
}

static struct zui_host *zui_router_lock_attached_host(struct zui_router *router)
{
	struct zui_host *host;

	if (router == NULL || !router->attached || router->host == NULL) {
		return NULL;
	}

	host = router->host;
	k_mutex_lock(&host->lock, K_FOREVER);
	return host;
}

static void zui_host_unlock_if_locked(struct zui_host *host)
{
	if (host != NULL) {
		k_mutex_unlock(&host->lock);
	}
}

struct zui_router *zui_router_create(void)
{
	struct zui_router *router = zui_malloc(sizeof(*router));

	if (router == NULL) {
		return NULL;
	}

	memset(router, 0, sizeof(*router));
	return router;
}

void zui_router_destroy(struct zui_router *router)
{
	struct zui_host *host;

	if (router == NULL) {
		return;
	}

	host = zui_router_lock_attached_host(router);
	if (router->current != NULL) {
		(void)zui_screen_exit(router->current);
	}
	if (router->attached && router->host != NULL && zui_layer_valid(router->layer) &&
	    router->host->routers[router->layer] == router) {
		router->host->routers[router->layer] = NULL;
	}
	zui_router_set_host(router, NULL, router->layer);
	zui_host_unlock_if_locked(host);

	zui_free(router);
}

int zui_router_register_screen(struct zui_router *router, uint32_t screen_id,
			       struct zui_screen *screen)
{
	struct zui_host *host;
	int ret = -ENOMEM;

	if (router == NULL || screen_id == 0U || screen == NULL) {
		return -EINVAL;
	}

	host = zui_router_lock_attached_host(router);
	if (zui_router_find_entry(router, screen_id) != NULL) {
		ret = -EEXIST;
		goto out;
	}

	for (size_t i = 0; i < ARRAY_SIZE(router->entries); i++) {
		if (!router->entries[i].used) {
			router->entries[i].id = screen_id;
			router->entries[i].screen = screen;
			router->entries[i].used = true;
			router->count++;
			zui_router_set_screen_invalidation(router, screen);
			ret = 0;
			goto out;
		}
	}

out:
	zui_host_unlock_if_locked(host);
	return ret;
}

int zui_router_unregister_screen(struct zui_router *router, uint32_t screen_id)
{
	struct zui_host *host;
	struct zui_router_entry *entry;
	int ret = 0;

	if (router == NULL || screen_id == 0U) {
		return -EINVAL;
	}

	host = zui_router_lock_attached_host(router);
	entry = zui_router_find_entry(router, screen_id);
	if (entry == NULL) {
		ret = -ENOENT;
		goto out;
	}

	if (router->current_id == screen_id) {
		(void)zui_screen_exit(router->current);
		router->current = NULL;
		router->current_id = 0U;
	}

	(void)zui_screen_set_invalidate_callback(entry->screen, NULL, NULL);
	memset(entry, 0, sizeof(*entry));
	router->count--;

out:
	zui_host_unlock_if_locked(host);
	return ret;
}

int zui_router_switch(struct zui_router *router, uint32_t screen_id)
{
	struct zui_host *host;
	struct zui_router_entry *entry;
	int ret;

	if (router == NULL || screen_id == 0U) {
		return -EINVAL;
	}

	host = zui_router_lock_attached_host(router);
	entry = zui_router_find_entry(router, screen_id);
	if (entry == NULL) {
		ret = -ENOENT;
		goto out;
	}

	if (router->current_id == screen_id) {
		ret = 0;
		goto out;
	}

	if (router->current != NULL) {
		(void)zui_screen_exit(router->current);
	}

	router->current = entry->screen;
	router->current_id = screen_id;
	ret = zui_screen_enter(router->current);

out:
	zui_host_unlock_if_locked(host);
	return ret;
}

uint32_t zui_router_current(const struct zui_router *router)
{
	if (router == NULL) {
		return 0U;
	}

	return router->current_id;
}

size_t zui_router_screen_count(const struct zui_router *router)
{
	if (router == NULL) {
		return 0U;
	}

	return router->count;
}

struct zui_screen *zui_router_screen(const struct zui_router *router, uint32_t screen_id)
{
	struct zui_router_entry *entry = zui_router_find_entry(router, screen_id);

	return entry == NULL ? NULL : entry->screen;
}

int zui_router_dispatch_event(struct zui_router *router, const struct zui_screen_event *event)
{
	struct zui_host *host;
	int ret;

	if (router == NULL || event == NULL) {
		return -EINVAL;
	}

	host = zui_router_lock_attached_host(router);
	if (router->current == NULL) {
		ret = -ENODEV;
		goto out;
	}

	ret = zui_screen_dispatch_event(router->current, event);

out:
	zui_host_unlock_if_locked(host);
	return ret;
}

static int zui_host_find_layer_order_index(const struct zui_host *host, enum zui_layer layer)
{
	for (size_t i = 0; i < ARRAY_SIZE(host->layer_order); i++) {
		if (host->layer_order[i] == layer) {
			return (int)i;
		}
	}

	return -ENOENT;
}

static void zui_host_move_layer(struct zui_host *host, enum zui_layer layer, bool front)
{
	int index = zui_host_find_layer_order_index(host, layer);
	enum zui_layer saved;

	if (index < 0) {
		return;
	}

	saved = host->layer_order[index];
	if (front) {
		for (size_t i = (size_t)index; i < ARRAY_SIZE(host->layer_order) - 1U; i++) {
			host->layer_order[i] = host->layer_order[i + 1U];
		}
		host->layer_order[ARRAY_SIZE(host->layer_order) - 1U] = saved;
	} else {
		for (size_t i = (size_t)index; i > 0U; i--) {
			host->layer_order[i] = host->layer_order[i - 1U];
		}
		host->layer_order[0] = saved;
	}
}

static size_t zui_host_first_visible_layer_index(const struct zui_host *host)
{
	for (size_t i = 0U; i < ARRAY_SIZE(host->layer_order); i++) {
		enum zui_layer layer = host->layer_order[i];
		struct zui_router *router = host->routers[layer];

		if (layer == ZUI_LAYER_FULLSCREEN && host->layer_enabled[layer] && router != NULL &&
		    router->current != NULL) {
			return i;
		}
	}

	return 0U;
}

struct zui_host *zui_host_create(void *user_data)
{
	struct zui_host *host = zui_malloc(sizeof(*host));

	if (host == NULL) {
		return NULL;
	}

	memset(host, 0, sizeof(*host));
	k_mutex_init(&host->lock);
	host->user_data = user_data;
	host->next_toast_id = 1U;

	for (size_t i = 0; i < ZUI_LAYER_COUNT; i++) {
		host->layer_enabled[i] = true;
		host->layer_order[i] = (enum zui_layer)i;
	}
	zui_toast_host_init(host);
	if (zui_default_host == NULL) {
		zui_default_host = host;
		zui_default_host_owned = false;
	}

	return host;
}

void zui_host_destroy(struct zui_host *host)
{
	if (host == NULL) {
		return;
	}

	zui_toast_host_deinit(host);
	k_mutex_lock(&host->lock, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(host->routers); i++) {
		if (host->routers[i] != NULL) {
			zui_router_set_host(host->routers[i], NULL, (enum zui_layer)i);
			host->routers[i] = NULL;
		}
	}
	if (zui_default_host == host) {
		zui_default_host = NULL;
		zui_default_host_owned = false;
	}
	k_mutex_unlock(&host->lock);
	zui_free(host);
}

void *zui_host_get_user_data(const struct zui_host *host)
{
	if (host == NULL) {
		return NULL;
	}

	return host->user_data;
}

int zui_host_attach_router(struct zui_host *host, enum zui_layer layer, struct zui_router *router)
{
	int ret = 0;

	if (host == NULL || router == NULL || !zui_layer_valid(layer)) {
		return -EINVAL;
	}

	k_mutex_lock(&host->lock, K_FOREVER);
	if (host->routers[layer] != NULL && host->routers[layer] != router) {
		ret = -EALREADY;
		goto out;
	}
	if (router->attached && router->host != host) {
		ret = -EALREADY;
		goto out;
	}
	if (router->attached && router->layer != layer) {
		ret = -EALREADY;
		goto out;
	}

	host->routers[layer] = router;
	zui_router_set_host(router, host, layer);

out:
	k_mutex_unlock(&host->lock);
	return ret;
}

int zui_host_detach_router(struct zui_host *host, enum zui_layer layer)
{
	int ret = 0;

	if (host == NULL || !zui_layer_valid(layer)) {
		return -EINVAL;
	}

	k_mutex_lock(&host->lock, K_FOREVER);
	if (host->routers[layer] == NULL) {
		ret = -ENOENT;
		goto out;
	}

	zui_router_set_host(host->routers[layer], NULL, layer);
	host->routers[layer] = NULL;

out:
	k_mutex_unlock(&host->lock);
	return ret;
}

static bool zui_keypad_fallback_event(const struct zui_input_event *event,
				      struct zui_input_event *fallback)
{
	enum zui_input_code code;

	if (event == NULL || fallback == NULL || event->code != ZUI_INPUT_CODE_KEYPAD) {
		return false;
	}

	switch (event->action) {
	case ZUI_INPUT_ACTION_PRESS:
	case ZUI_INPUT_ACTION_RELEASE:
	case ZUI_INPUT_ACTION_CLICK:
	case ZUI_INPUT_ACTION_LONG_PRESS:
		break;
	default:
		return false;
	}

	switch (event->value) {
	case '2':
		code = ZUI_INPUT_CODE_UP;
		break;
	case '4':
		code = ZUI_INPUT_CODE_LEFT;
		break;
	case '.':
		code = ZUI_INPUT_CODE_SELECT;
		break;
	case '6':
		code = ZUI_INPUT_CODE_RIGHT;
		break;
	case '8':
		code = ZUI_INPUT_CODE_DOWN;
		break;
	case '*':
		code = ZUI_INPUT_CODE_BACK;
		break;
	default:
		return false;
	}

	*fallback = *event;
	fallback->code = code;
	fallback->value = 1;
	return true;
}

int zui_host_submit_input(struct zui_host *host, const struct zui_input_event *event)
{
	int ret = -ENODEV;

	if (host == NULL || event == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&host->lock, K_FOREVER);
	if (host->suspended || host->input_locked) {
		k_mutex_unlock(&host->lock);
		return -EBUSY;
	}

	if (zui_toast_submit_input(host, event) > 0) {
		ret = 0;
		goto out;
	}

	for (size_t i = ARRAY_SIZE(host->layer_order); i > 0U; i--) {
		enum zui_layer layer = host->layer_order[i - 1U];
		struct zui_router *router = host->routers[layer];
		int rc;

		if (!host->layer_enabled[layer] || router == NULL || router->current == NULL) {
			continue;
		}

		rc = zui_screen_submit_input(router->current, event);
		if (rc > 0) {
			ret = 0;
			goto out;
		}
		if (rc == 0 || rc == -ENOSYS) {
			struct zui_input_event fallback;

			if (zui_keypad_fallback_event(event, &fallback)) {
				rc = zui_screen_submit_input(router->current, &fallback);
				if (rc > 0) {
					ret = 0;
					goto out;
				}
			}
		}

		ret = rc == 0 || rc == -ENOSYS ? -ENODATA : rc;
		goto out;
	}

out:
	k_mutex_unlock(&host->lock);
	return ret;
}

int zui_host_request_redraw(struct zui_host *host)
{
	zui_host_redraw_cb callback;
	void *user_data;

	if (host == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&host->lock, K_FOREVER);
	host->redraw_requested = true;
	callback = host->redraw_callback;
	user_data = host->redraw_user_data;
	k_mutex_unlock(&host->lock);

	if (callback != NULL) {
		callback(host, user_data);
	}
	return 0;
}

int zui_host_set_redraw_callback(struct zui_host *host, zui_host_redraw_cb callback,
				 void *user_data)
{
	if (host == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&host->lock, K_FOREVER);
	host->redraw_callback = callback;
	host->redraw_user_data = user_data;
	k_mutex_unlock(&host->lock);
	return 0;
}

int zui_host_draw(struct zui_host *host, struct zui_draw_ctx *draw)
{
	int ret = 0;
	int toast_rc;
	size_t first_layer;

	if (host == NULL || draw == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&host->lock, K_FOREVER);
	first_layer = zui_host_first_visible_layer_index(host);

	for (size_t i = first_layer; i < ARRAY_SIZE(host->layer_order); i++) {
		enum zui_layer layer = host->layer_order[i];
		struct zui_router *router = host->routers[layer];
		int rc;

		if (!host->layer_enabled[layer] || router == NULL || router->current == NULL) {
			continue;
		}

		rc = zui_screen_draw(router->current, draw);
		if (rc != 0 && ret == 0) {
			ret = rc;
		}
	}

	toast_rc = zui_toast_draw(host, draw);
	if (toast_rc != 0 && ret == 0) {
		ret = toast_rc;
	}
	host->redraw_requested = false;
	k_mutex_unlock(&host->lock);
	return ret;
}

int zui_host_run(struct zui_host *host)
{
	if (host == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&host->lock, K_FOREVER);
	host->running = true;
	k_mutex_unlock(&host->lock);
	return 0;
}

int zui_host_stop(struct zui_host *host)
{
	if (host == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&host->lock, K_FOREVER);
	host->running = false;
	k_mutex_unlock(&host->lock);
	return 0;
}

int zui_host_poll(struct zui_host *host)
{
	size_t first_layer;
	uint32_t now_ms;
	int dispatched = 0;

	if (host == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&host->lock, K_FOREVER);
	if (!host->running || host->suspended) {
		k_mutex_unlock(&host->lock);
		return 0;
	}

	first_layer = zui_host_first_visible_layer_index(host);
	now_ms = k_uptime_get_32();

	for (size_t i = first_layer; i < ARRAY_SIZE(host->layer_order); i++) {
		enum zui_layer layer = host->layer_order[i];
		struct zui_router *router = host->routers[layer];
		int rc;

		if (!host->layer_enabled[layer] || router == NULL || router->current == NULL) {
			continue;
		}

		rc = zui_screen_poll_tick(router->current, now_ms);
		if (rc < 0 && rc != -ENOSYS) {
			k_mutex_unlock(&host->lock);
			return rc;
		}
		if (rc > 0) {
			dispatched++;
		}
	}

	k_mutex_unlock(&host->lock);
	return dispatched;
}

int32_t zui_host_next_timeout_ms(const struct zui_host *host)
{
	struct zui_host *mutable_host = (struct zui_host *)host;
	size_t first_layer;
	uint32_t now_ms;
	int32_t best = -ENODATA;

	if (host == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&mutable_host->lock, K_FOREVER);
	if (!host->running || host->suspended) {
		k_mutex_unlock(&mutable_host->lock);
		return -EBUSY;
	}

	first_layer = zui_host_first_visible_layer_index(host);
	now_ms = k_uptime_get_32();

	for (size_t i = first_layer; i < ARRAY_SIZE(host->layer_order); i++) {
		enum zui_layer layer = host->layer_order[i];
		struct zui_router *router = host->routers[layer];
		int32_t timeout;

		if (!host->layer_enabled[layer] || router == NULL || router->current == NULL) {
			continue;
		}

		timeout = zui_screen_next_tick_timeout_ms(router->current, now_ms);
		if (timeout < 0) {
			continue;
		}
		if (best < 0 || timeout < best) {
			best = timeout;
		}
	}

	k_mutex_unlock(&mutable_host->lock);
	return best;
}

int zui_host_set_suspended(struct zui_host *host, bool suspended)
{
	if (host == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&host->lock, K_FOREVER);
	host->suspended = suspended;
	k_mutex_unlock(&host->lock);
	return 0;
}

bool zui_host_is_suspended(const struct zui_host *host)
{
	struct zui_host *mutable_host = (struct zui_host *)host;
	bool suspended;

	if (host == NULL) {
		return false;
	}

	k_mutex_lock(&mutable_host->lock, K_FOREVER);
	suspended = host->suspended;
	k_mutex_unlock(&mutable_host->lock);
	return suspended;
}

int zui_host_set_input_locked(struct zui_host *host, bool locked)
{
	if (host == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&host->lock, K_FOREVER);
	host->input_locked = locked;
	k_mutex_unlock(&host->lock);
	return 0;
}

bool zui_host_is_input_locked(const struct zui_host *host)
{
	struct zui_host *mutable_host = (struct zui_host *)host;
	bool locked;

	if (host == NULL) {
		return false;
	}

	k_mutex_lock(&mutable_host->lock, K_FOREVER);
	locked = host->input_locked;
	k_mutex_unlock(&mutable_host->lock);
	return locked;
}

int zui_host_set_layer_enabled(struct zui_host *host, enum zui_layer layer, bool enabled)
{
	struct zui_router *router;

	if (host == NULL || !zui_layer_valid(layer)) {
		return -EINVAL;
	}

	k_mutex_lock(&host->lock, K_FOREVER);
	if (host->layer_enabled[layer] == enabled) {
		k_mutex_unlock(&host->lock);
		return 0;
	}

	router = host->routers[layer];
	if (!enabled && router != NULL && router->current != NULL) {
		(void)zui_screen_exit(router->current);
	}
	host->layer_enabled[layer] = enabled;
	if (enabled && router != NULL && router->current != NULL) {
		(void)zui_screen_enter(router->current);
	}
	k_mutex_unlock(&host->lock);
	return 0;
}

bool zui_host_layer_is_enabled(const struct zui_host *host, enum zui_layer layer)
{
	struct zui_host *mutable_host = (struct zui_host *)host;
	bool enabled;

	if (host == NULL || !zui_layer_valid(layer)) {
		return false;
	}

	k_mutex_lock(&mutable_host->lock, K_FOREVER);
	enabled = host->layer_enabled[layer];
	k_mutex_unlock(&mutable_host->lock);
	return enabled;
}

int zui_host_send_layer_to_front(struct zui_host *host, enum zui_layer layer)
{
	if (host == NULL || !zui_layer_valid(layer)) {
		return -EINVAL;
	}

	k_mutex_lock(&host->lock, K_FOREVER);
	zui_host_move_layer(host, layer, true);
	k_mutex_unlock(&host->lock);
	return 0;
}

int zui_host_send_layer_to_back(struct zui_host *host, enum zui_layer layer)
{
	if (host == NULL || !zui_layer_valid(layer)) {
		return -EINVAL;
	}

	k_mutex_lock(&host->lock, K_FOREVER);
	zui_host_move_layer(host, layer, false);
	k_mutex_unlock(&host->lock);
	return 0;
}

int zui_init(void)
{
	struct zui_host *host;

	if (zui_default_host != NULL) {
		return zui_host_run(zui_default_host);
	}

	host = zui_host_create(NULL);
	if (host == NULL) {
		return -ENOMEM;
	}

	zui_default_host = host;
	zui_default_host_owned = true;
	return zui_host_run(host);
}

int zui_deinit(void)
{
	struct zui_host *host;

	if (zui_default_host == NULL) {
		return 0;
	}

	if (!zui_default_host_owned) {
		return -EBUSY;
	}

	host = zui_default_host;
	zui_default_host = NULL;
	zui_default_host_owned = false;
	zui_host_destroy(host);
	return 0;
}

struct zui_host *zui_get_default_host(void)
{
	return zui_default_host;
}

struct zui_version zui_get_version(void)
{
	return (struct zui_version){
		.major = 0,
		.minor = 1,
		.patch = 0,
	};
}

int zui_get_runtime_stats(struct zui_runtime_stats *stats)
{
	if (stats == NULL) {
		return -EINVAL;
	}

	*stats = (struct zui_runtime_stats){0};
#if !IS_ENABLED(CONFIG_ZUI_USE_SYSTEM_HEAP) && defined(CONFIG_SYS_HEAP_RUNTIME_STATS)
	struct sys_memory_stats heap_stats = {0};

	zui_heap_stats(&heap_stats);
	stats->heap_stats_available = true;
	stats->heap_free_bytes = heap_stats.free_bytes;
	stats->heap_allocated_bytes = heap_stats.allocated_bytes;
	stats->heap_max_allocated_bytes = heap_stats.max_allocated_bytes;
#endif

	return 0;
}
