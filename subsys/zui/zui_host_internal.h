/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_ZUI_HOST_INTERNAL_H_
#define ZEPHYR_SUBSYS_ZUI_HOST_INTERNAL_H_

#include <zephyr/kernel.h>
#include <zephyr/zui/host.h>
#include <zephyr/zui/screen.h>
#include <zephyr/zui/toast.h>

#ifndef CONFIG_ZUI_ROUTER_MAX_SCREENS
#define CONFIG_ZUI_ROUTER_MAX_SCREENS 16
#endif

#ifndef CONFIG_ZUI_TOAST_MAX_SLOTS
#define CONFIG_ZUI_TOAST_MAX_SLOTS 1
#endif

#define ZUI_HOST_MAX_TOASTS CONFIG_ZUI_TOAST_MAX_SLOTS
#define ZUI_HOST_TOAST_TITLE_SIZE 32
#define ZUI_HOST_TOAST_TEXT_SIZE  96

enum zui_host_toast_state {
	ZUI_HOST_TOAST_HIDDEN,
	ZUI_HOST_TOAST_ENTERING,
	ZUI_HOST_TOAST_VISIBLE,
	ZUI_HOST_TOAST_EXITING,
};

struct zui_router_entry {
	uint32_t id;
	struct zui_screen *screen;
	bool used;
};

struct zui_router {
	struct zui_router_entry entries[CONFIG_ZUI_ROUTER_MAX_SCREENS];
	size_t count;
	uint32_t current_id;
	struct zui_screen *current;
	struct zui_host *host;
	enum zui_layer layer;
	bool attached;
};

struct zui_host_toast {
	uint32_t id;
	struct zui_toast_config config;
	char title[ZUI_HOST_TOAST_TITLE_SIZE];
	char text[ZUI_HOST_TOAST_TEXT_SIZE];
	bool used;
	enum zui_host_toast_state state;
	int16_t y;
	int16_t target_y;
	bool hide_after_slide;
	struct k_work_delayable anim_work;
	struct k_work_delayable timeout_work;
	struct zui_host *host;
};

struct zui_host {
	void *user_data;
	struct zui_router *routers[ZUI_LAYER_COUNT];
	bool layer_enabled[ZUI_LAYER_COUNT];
	enum zui_layer layer_order[ZUI_LAYER_COUNT];
	bool running;
	bool suspended;
	bool input_locked;
	bool redraw_requested;
	struct k_mutex lock;
	zui_host_redraw_cb redraw_callback;
	void *redraw_user_data;
	uint32_t next_toast_id;
	struct zui_host_toast toasts[ZUI_HOST_MAX_TOASTS];
};

static inline uint32_t zui_host_next_id(uint32_t *next_id)
{
	uint32_t id = *next_id;

	(*next_id)++;
	if (*next_id == 0U) {
		*next_id = 1U;
	}

	return id;
}

void zui_toast_host_init(struct zui_host *host);
void zui_toast_host_deinit(struct zui_host *host);
int zui_toast_submit_input(struct zui_host *host, const struct zui_input_event *event);
int zui_toast_draw(struct zui_host *host, struct zui_draw_ctx *draw);

#endif /* ZEPHYR_SUBSYS_ZUI_HOST_INTERNAL_H_ */
