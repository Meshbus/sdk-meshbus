/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ZUI host and router API
 */

#ifndef MESHBUS_INCLUDE_ZUI_HOST_H_
#define MESHBUS_INCLUDE_ZUI_HOST_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zui/input.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zui_host;
struct zui_router;
struct zui_screen;
struct zui_draw_ctx;
struct zui_screen_event;

typedef void (*zui_host_redraw_cb)(struct zui_host *host, void *user_data);

enum zui_layer {
	ZUI_LAYER_DESKTOP,
	ZUI_LAYER_WINDOW,
	ZUI_LAYER_STATUS_LEFT,
	ZUI_LAYER_STATUS_RIGHT,
	ZUI_LAYER_FULLSCREEN,
	ZUI_LAYER_COUNT,
};

/** Create an empty router. Returns NULL when allocation fails. */
struct zui_router *zui_router_create(void);
/** Destroy a router and detach it from any host that currently owns it. */
void zui_router_destroy(struct zui_router *router);
int zui_router_register_screen(struct zui_router *router, uint32_t screen_id,
			       struct zui_screen *screen);
int zui_router_unregister_screen(struct zui_router *router, uint32_t screen_id);
int zui_router_switch(struct zui_router *router, uint32_t screen_id);
uint32_t zui_router_current(const struct zui_router *router);
size_t zui_router_screen_count(const struct zui_router *router);
struct zui_screen *zui_router_screen(const struct zui_router *router, uint32_t screen_id);
int zui_router_dispatch_event(struct zui_router *router, const struct zui_screen_event *event);

struct zui_host *zui_host_create(void *user_data);
void zui_host_destroy(struct zui_host *host);
void *zui_host_get_user_data(const struct zui_host *host);
int zui_host_attach_router(struct zui_host *host, enum zui_layer layer, struct zui_router *router);
int zui_host_detach_router(struct zui_host *host, enum zui_layer layer);
/** Submit input to the focused visible layer. Returns 0 when consumed, or -errno. */
int zui_host_submit_input(struct zui_host *host, const struct zui_input_event *event);
/** Request a redraw; the redraw callback runs synchronously in the caller context. */
int zui_host_request_redraw(struct zui_host *host);
int zui_host_set_redraw_callback(struct zui_host *host, zui_host_redraw_cb callback,
				 void *user_data);
/** Draw visible routers and host-owned transient UI such as toast notifications. */
int zui_host_draw(struct zui_host *host, struct zui_draw_ctx *draw);
int zui_host_run(struct zui_host *host);
int zui_host_stop(struct zui_host *host);
int zui_host_poll(struct zui_host *host);
int32_t zui_host_next_timeout_ms(const struct zui_host *host);
int zui_host_set_suspended(struct zui_host *host, bool suspended);
bool zui_host_is_suspended(const struct zui_host *host);
int zui_host_set_input_locked(struct zui_host *host, bool locked);
bool zui_host_is_input_locked(const struct zui_host *host);
int zui_host_set_layer_enabled(struct zui_host *host, enum zui_layer layer, bool enabled);
bool zui_host_layer_is_enabled(const struct zui_host *host, enum zui_layer layer);
int zui_host_send_layer_to_front(struct zui_host *host, enum zui_layer layer);
int zui_host_send_layer_to_back(struct zui_host *host, enum zui_layer layer);

#ifdef __cplusplus
}
#endif

#endif /* MESHBUS_INCLUDE_ZUI_HOST_H_ */
