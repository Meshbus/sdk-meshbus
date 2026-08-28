/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ZUI screen API
 */

#ifndef ZEPHYR_INCLUDE_ZUI_SCREEN_H_
#define ZEPHYR_INCLUDE_ZUI_SCREEN_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/zui/input.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zui_draw_ctx;
struct zui_screen;

enum zui_screen_event_type {
	ZUI_SCREEN_EVENT_CUSTOM,
	ZUI_SCREEN_EVENT_TICK,
	ZUI_SCREEN_EVENT_BACK,
};

struct zui_screen_event {
	enum zui_screen_event_type type;
	uint32_t code;
	uintptr_t data;
};

typedef void (*zui_screen_draw_cb)(struct zui_draw_ctx *draw, void *user_data);
typedef bool (*zui_screen_input_cb)(const struct zui_input_event *event, void *user_data);
typedef void (*zui_screen_lifecycle_cb)(void *user_data);
typedef bool (*zui_screen_event_cb)(const struct zui_screen_event *event, void *user_data);

struct zui_screen_ops {
	zui_screen_draw_cb draw;
	zui_screen_input_cb input;
	zui_screen_lifecycle_cb enter;
	zui_screen_lifecycle_cb exit;
	zui_screen_event_cb event;
};

/**
 * Create a screen with an optional operation table.
 *
 * The operation table is referenced directly and must remain valid until the
 * screen is destroyed. Prefer static const operation tables.
 */
struct zui_screen *zui_screen_create(const struct zui_screen_ops *ops, void *user_data);
void zui_screen_destroy(struct zui_screen *screen);
void *zui_screen_get_user_data(const struct zui_screen *screen);
bool zui_screen_is_entered(const struct zui_screen *screen);
int zui_screen_set_tick_period(struct zui_screen *screen, uint32_t period_ms);
uint32_t zui_screen_tick_period(const struct zui_screen *screen);
/** Mark a screen dirty; attached hosts receive a synchronous redraw callback. */
int zui_screen_request_redraw(struct zui_screen *screen);
bool zui_screen_redraw_is_requested(const struct zui_screen *screen);
int zui_screen_clear_redraw(struct zui_screen *screen);
int zui_screen_draw(struct zui_screen *screen, struct zui_draw_ctx *draw);
int zui_screen_submit_input(struct zui_screen *screen, const struct zui_input_event *event);
int zui_screen_dispatch_event(struct zui_screen *screen, const struct zui_screen_event *event);
int zui_screen_enter(struct zui_screen *screen);
int zui_screen_exit(struct zui_screen *screen);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ZUI_SCREEN_H_ */
