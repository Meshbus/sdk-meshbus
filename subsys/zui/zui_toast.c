/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/sys/util.h>
#include <zui/draw.h>
#include <zui/input.h>
#include <zui/toast.h>

#include "zui_host_internal.h"

#define ZUI_TOAST_DEFAULT_TIMEOUT_MS 30000U
#define ZUI_TOAST_ANIM_PERIOD_MS     16U
#define ZUI_TOAST_ANIM_STEP_PX       3
#define ZUI_TOAST_DEFAULT_WIDTH      128
#define ZUI_TOAST_DEFAULT_HEIGHT     64
#define ZUI_TOAST_RADIUS             5
#define ZUI_TOAST_ICON_SLOT_X        6
#define ZUI_TOAST_ICON_SLOT_Y        5
#define ZUI_TOAST_ICON_SLOT_W        24
#define ZUI_TOAST_ICON_SLOT_H        24
#define ZUI_TOAST_ICON_TILE_X        6
#define ZUI_TOAST_ICON_TILE_Y        5
#define ZUI_TOAST_ICON_TILE_W        24
#define ZUI_TOAST_ICON_TILE_H        24
#define ZUI_TOAST_ICON_TILE_RADIUS   4
#define ZUI_TOAST_TEXT_X             38

struct zui_toast_draw_snapshot {
	struct zui_toast_config config;
	bool used;
	enum zui_host_toast_state state;
	int16_t y;
};

static void zui_toast_geometry(uint16_t height, int16_t *visible_y, int16_t *hidden_y,
			       uint16_t *toast_h)
{
	uint16_t h = height == 0U ? ZUI_TOAST_DEFAULT_HEIGHT : height;
	uint16_t visible = h / 2U;
	uint16_t box_h = h - visible;
	uint16_t hidden = h > 0U ? h - 1U : 0U;

	if (box_h == 0U) {
		box_h = 1U;
	}
	if (visible_y != NULL) {
		*visible_y = (int16_t)visible;
	}
	if (hidden_y != NULL) {
		*hidden_y = (int16_t)hidden;
	}
	if (toast_h != NULL) {
		*toast_h = box_h;
	}
}

static struct zui_host_toast *zui_toast_find(struct zui_host *host, uint32_t toast_id)
{
	if (host == NULL || toast_id == 0U) {
		return NULL;
	}

	for (size_t i = 0; i < ARRAY_SIZE(host->toasts); i++) {
		if (host->toasts[i].used && host->toasts[i].id == toast_id) {
			return &host->toasts[i];
		}
	}

	return NULL;
}

static void zui_toast_clear_slot(struct zui_host_toast *toast)
{
	toast->id = 0U;
	memset(&toast->config, 0, sizeof(toast->config));
	toast->title[0] = '\0';
	toast->text[0] = '\0';
	toast->used = false;
	toast->state = ZUI_HOST_TOAST_HIDDEN;
	toast->hide_after_slide = false;
}

static void zui_toast_copy_string(char *dst, size_t dst_size, const char *src)
{
	if (dst == NULL || dst_size == 0U) {
		return;
	}

	if (src == NULL) {
		dst[0] = '\0';
		return;
	}

	(void)strncpy(dst, src, dst_size - 1U);
	dst[dst_size - 1U] = '\0';
}

static void zui_toast_request_redraw(struct zui_host *host)
{
	if (host != NULL) {
		(void)zui_host_request_redraw(host);
	}
}

static bool zui_toast_input_dismisses(const struct zui_input_event *event)
{
	if (event == NULL || event->action != ZUI_INPUT_ACTION_CLICK) {
		return false;
	}

	return event->code == ZUI_INPUT_CODE_SELECT || event->code == ZUI_INPUT_CODE_BACK ||
	       event->code == ZUI_INPUT_CODE_MENU || event->code == ZUI_INPUT_CODE_HOME;
}

static bool zui_toast_start_exit(struct zui_host_toast *toast)
{
	int16_t hidden_y;

	if (toast == NULL || !toast->used || toast->state == ZUI_HOST_TOAST_HIDDEN) {
		return false;
	}

	zui_toast_geometry(ZUI_TOAST_DEFAULT_HEIGHT, NULL, &hidden_y, NULL);
	toast->target_y = hidden_y;
	toast->state = ZUI_HOST_TOAST_EXITING;
	toast->hide_after_slide = true;
	(void)k_work_cancel_delayable(&toast->timeout_work);
	(void)k_work_reschedule(&toast->anim_work, K_NO_WAIT);
	return true;
}

static void zui_toast_timeout_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct zui_host_toast *toast = CONTAINER_OF(dwork, struct zui_host_toast, timeout_work);
	struct zui_host *host = toast->host;

	if (host == NULL) {
		return;
	}

	k_mutex_lock(&host->lock, K_FOREVER);
	if (zui_toast_start_exit(toast)) {
		k_mutex_unlock(&host->lock);
		zui_toast_request_redraw(host);
		return;
	}
	k_mutex_unlock(&host->lock);
}

static void zui_toast_anim_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct zui_host_toast *toast = CONTAINER_OF(dwork, struct zui_host_toast, anim_work);
	struct zui_host *host = toast->host;
	bool reschedule = false;

	if (host == NULL) {
		return;
	}

	k_mutex_lock(&host->lock, K_FOREVER);
	if (!toast->used || toast->state == ZUI_HOST_TOAST_HIDDEN) {
		k_mutex_unlock(&host->lock);
		return;
	}

	if (toast->y < toast->target_y) {
		toast->y = MIN((int16_t)(toast->y + ZUI_TOAST_ANIM_STEP_PX), toast->target_y);
	} else if (toast->y > toast->target_y) {
		toast->y = MAX((int16_t)(toast->y - ZUI_TOAST_ANIM_STEP_PX), toast->target_y);
	}

	if (toast->y == toast->target_y) {
		if (toast->hide_after_slide) {
			zui_toast_clear_slot(toast);
		} else {
			toast->state = ZUI_HOST_TOAST_VISIBLE;
		}
	} else {
		reschedule = true;
	}
	k_mutex_unlock(&host->lock);

	zui_toast_request_redraw(host);
	if (reschedule) {
		(void)k_work_reschedule(&toast->anim_work, K_MSEC(ZUI_TOAST_ANIM_PERIOD_MS));
	}
}

void zui_toast_host_init(struct zui_host *host)
{
	if (host == NULL) {
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(host->toasts); i++) {
		host->toasts[i].host = host;
		host->toasts[i].state = ZUI_HOST_TOAST_HIDDEN;
		k_work_init_delayable(&host->toasts[i].anim_work, zui_toast_anim_handler);
		k_work_init_delayable(&host->toasts[i].timeout_work, zui_toast_timeout_handler);
	}
}

void zui_toast_host_deinit(struct zui_host *host)
{
	if (host == NULL) {
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(host->toasts); i++) {
		struct k_work_sync anim_sync;
		struct k_work_sync timeout_sync;

		(void)k_work_cancel_delayable_sync(&host->toasts[i].timeout_work, &timeout_sync);
		(void)k_work_cancel_delayable_sync(&host->toasts[i].anim_work, &anim_sync);
		k_mutex_lock(&host->lock, K_FOREVER);
		zui_toast_clear_slot(&host->toasts[i]);
		k_mutex_unlock(&host->lock);
	}
}

uint32_t zui_toast_show(struct zui_host *host, const struct zui_toast_config *config)
{
	struct zui_host_toast *toast = NULL;
	int16_t visible_y;
	int16_t hidden_y;
	uint32_t timeout_ms;
	uint32_t id;

	if (host == NULL || config == NULL) {
		return 0U;
	}

	k_mutex_lock(&host->lock, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(host->toasts); i++) {
		if (!host->toasts[i].used) {
			toast = &host->toasts[i];
			break;
		}
	}
	if (toast == NULL) {
		k_mutex_unlock(&host->lock);
		return 0U;
	}

	id = zui_host_next_id(&host->next_toast_id);
	zui_toast_geometry(ZUI_TOAST_DEFAULT_HEIGHT, &visible_y, &hidden_y, NULL);
	toast->id = id;
	toast->config = *config;
	zui_toast_copy_string(toast->title, sizeof(toast->title), config->title);
	zui_toast_copy_string(toast->text, sizeof(toast->text), config->text);
	toast->config.title = toast->title[0] == '\0' ? NULL : toast->title;
	toast->config.text = toast->text[0] == '\0' ? NULL : toast->text;
	toast->used = true;
	toast->state = ZUI_HOST_TOAST_ENTERING;
	toast->y = hidden_y;
	toast->target_y = visible_y;
	toast->hide_after_slide = false;
	timeout_ms = config->timeout_ms == 0U ? ZUI_TOAST_DEFAULT_TIMEOUT_MS : config->timeout_ms;
	k_mutex_unlock(&host->lock);

	(void)k_work_cancel_delayable(&toast->timeout_work);
	(void)k_work_reschedule(&toast->timeout_work, K_MSEC(timeout_ms));
	(void)k_work_reschedule(&toast->anim_work, K_NO_WAIT);
	zui_toast_request_redraw(host);

	return id;
}

int zui_toast_dismiss(struct zui_host *host, uint32_t toast_id)
{
	struct zui_host_toast *toast;
	bool redraw;

	if (host == NULL || toast_id == 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&host->lock, K_FOREVER);
	toast = zui_toast_find(host, toast_id);
	if (toast == NULL) {
		k_mutex_unlock(&host->lock);
		return -ENOENT;
	}

	redraw = zui_toast_start_exit(toast);
	k_mutex_unlock(&host->lock);
	if (redraw) {
		zui_toast_request_redraw(host);
	}
	return 0;
}

int zui_toast_submit_input(struct zui_host *host, const struct zui_input_event *event)
{
	bool active = false;
	bool redraw = false;
	bool dismiss;

	if (host == NULL || event == NULL) {
		return -EINVAL;
	}

	dismiss = zui_toast_input_dismisses(event);

	k_mutex_lock(&host->lock, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(host->toasts); i++) {
		struct zui_host_toast *toast = &host->toasts[i];

		if (!toast->used || toast->state == ZUI_HOST_TOAST_HIDDEN) {
			continue;
		}

		active = true;
		if (dismiss && toast->state != ZUI_HOST_TOAST_EXITING) {
			redraw |= zui_toast_start_exit(toast);
		}
	}
	k_mutex_unlock(&host->lock);

	if (redraw) {
		zui_toast_request_redraw(host);
	}

	return active ? 1 : 0;
}

bool zui_toast_is_visible(const struct zui_host *host, uint32_t toast_id)
{
	struct zui_host *mutable_host = (struct zui_host *)host;
	bool visible = false;

	if (host == NULL || toast_id == 0U) {
		return false;
	}

	k_mutex_lock(&mutable_host->lock, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(host->toasts); i++) {
		if (host->toasts[i].used && host->toasts[i].id == toast_id &&
		    host->toasts[i].state != ZUI_HOST_TOAST_HIDDEN) {
			visible = true;
			break;
		}
	}
	k_mutex_unlock(&mutable_host->lock);

	return visible;
}

int zui_toast_dismiss_all(struct zui_host *host)
{
	int dismissed = 0;

	if (host == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&host->lock, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(host->toasts); i++) {
		if (host->toasts[i].used) {
			(void)zui_toast_start_exit(&host->toasts[i]);
			dismissed++;
		}
	}
	k_mutex_unlock(&host->lock);
	if (dismissed > 0) {
		zui_toast_request_redraw(host);
	}

	return dismissed;
}

static void zui_toast_draw_one(struct zui_draw_ctx *draw,
			       const struct zui_toast_draw_snapshot *toast)
{
	uint16_t width = zui_draw_width(draw);
	uint16_t height = zui_draw_height(draw);
	uint16_t toast_h;
	uint16_t radius = ZUI_TOAST_RADIUS;
	struct zui_rect rect;
	int16_t y = toast->y;
	int16_t bottom_y;
	int16_t right_x;
	uint16_t text_w;
	struct zui_rect saved_clip;
	bool restore_clip = false;

	if (width == 0U) {
		width = ZUI_TOAST_DEFAULT_WIDTH;
	}
	text_w = width > ZUI_TOAST_TEXT_X ? (uint16_t)(width - ZUI_TOAST_TEXT_X) : 1U;
	zui_toast_geometry(height, NULL, NULL, &toast_h);
	radius = MIN(radius, (uint16_t)(MIN(width, toast_h) / 2U));

	rect = (struct zui_rect){.x = 0, .y = y, .width = width, .height = toast_h};
	zui_draw_set_color(draw, ZUI_COLOR_WHITE);
	if (radius == 0U) {
		zui_draw_box(draw, &rect);
	} else {
		zui_draw_round_box(draw, &rect, radius);
		bottom_y = (int16_t)(y + toast_h - radius);
		right_x = (int16_t)(width - radius);
		zui_draw_box(draw,
			     &(struct zui_rect){
				     .x = 0, .y = bottom_y, .width = radius, .height = radius});
		zui_draw_box(draw, &(struct zui_rect){.x = right_x,
						      .y = bottom_y,
						      .width = radius,
						      .height = radius});
	}

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	if (radius == 0U) {
		zui_draw_line(draw, (struct zui_point){.x = 0, .y = y},
			      (struct zui_point){.x = (int16_t)(width - 1U), .y = y});
		zui_draw_line(draw, (struct zui_point){.x = 0, .y = y},
			      (struct zui_point){.x = 0, .y = (int16_t)(y + toast_h - 1U)});
		zui_draw_line(draw, (struct zui_point){.x = (int16_t)(width - 1U), .y = y},
			      (struct zui_point){.x = (int16_t)(width - 1U),
						 .y = (int16_t)(y + toast_h - 1U)});
	} else {
		right_x = (int16_t)(width - 1U);
		zui_draw_round_rect(draw, &rect, radius);
		zui_draw_set_color(draw, ZUI_COLOR_WHITE);
		zui_draw_line(draw, (struct zui_point){.x = 0, .y = (int16_t)(y + toast_h - 1U)},
			      (struct zui_point){.x = right_x, .y = (int16_t)(y + toast_h - 1U)});
		zui_draw_box(draw, &(struct zui_rect){.x = 0,
						      .y = (int16_t)(y + toast_h - radius),
						      .width = radius,
						      .height = radius});
		zui_draw_box(draw, &(struct zui_rect){.x = (int16_t)(width - radius),
						      .y = (int16_t)(y + toast_h - radius),
						      .width = radius,
						      .height = radius});
		zui_draw_set_color(draw, ZUI_COLOR_BLACK);
		zui_draw_line(draw, (struct zui_point){.x = 0, .y = (int16_t)(y + radius)},
			      (struct zui_point){.x = 0, .y = (int16_t)(y + toast_h - 1U)});
		zui_draw_line(draw, (struct zui_point){.x = right_x, .y = (int16_t)(y + radius)},
			      (struct zui_point){.x = right_x, .y = (int16_t)(y + toast_h - 1U)});
	}

	if (toast->config.icon != NULL) {
		uint16_t icon_w = zui_icon_width(toast->config.icon);
		uint16_t icon_h = zui_icon_height(toast->config.icon);
		struct zui_rect icon_tile = {
			.x = ZUI_TOAST_ICON_TILE_X,
			.y = (int16_t)(y + ZUI_TOAST_ICON_TILE_Y),
			.width = ZUI_TOAST_ICON_TILE_W,
			.height = ZUI_TOAST_ICON_TILE_H,
		};
		int16_t icon_x = ZUI_TOAST_ICON_SLOT_X;
		int16_t icon_y = (int16_t)(y + ZUI_TOAST_ICON_SLOT_Y);

		if (icon_w < ZUI_TOAST_ICON_SLOT_W) {
			icon_x += (int16_t)((ZUI_TOAST_ICON_SLOT_W - icon_w) / 2U);
		} else if (icon_w > ZUI_TOAST_ICON_SLOT_W) {
			icon_x -= (int16_t)((icon_w - ZUI_TOAST_ICON_SLOT_W) / 2U);
		}
		if (icon_h < ZUI_TOAST_ICON_SLOT_H) {
			icon_y += (int16_t)((ZUI_TOAST_ICON_SLOT_H - icon_h) / 2U);
		} else if (icon_h > ZUI_TOAST_ICON_SLOT_H) {
			icon_y -= (int16_t)((icon_h - ZUI_TOAST_ICON_SLOT_H) / 2U);
		}

		zui_draw_set_color(draw, ZUI_COLOR_WHITE);
		zui_draw_round_box(draw, &icon_tile, ZUI_TOAST_ICON_TILE_RADIUS);
		zui_draw_set_color(draw, ZUI_COLOR_BLACK);
		zui_draw_round_rect(draw, &icon_tile, ZUI_TOAST_ICON_TILE_RADIUS);

		saved_clip = (struct zui_rect){
			.x = ZUI_TOAST_TEXT_X, .y = y, .width = text_w, .height = toast_h};
		restore_clip = true;
		zui_draw_set_clip(draw,
				  &(struct zui_rect){.x = ZUI_TOAST_ICON_SLOT_X,
						     .y = (int16_t)(y + ZUI_TOAST_ICON_SLOT_Y),
						     .width = ZUI_TOAST_ICON_SLOT_W,
						     .height = ZUI_TOAST_ICON_SLOT_H});
		zui_draw_icon(draw, (struct zui_point){.x = icon_x, .y = icon_y},
			      toast->config.icon);
		zui_draw_set_clip(draw, &saved_clip);
	} else {
		zui_draw_set_clip(draw, &(struct zui_rect){.x = ZUI_TOAST_TEXT_X,
							   .y = y,
							   .width = text_w,
							   .height = toast_h});
		restore_clip = true;
	}

	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	if (toast->config.title != NULL && toast->config.title[0] != '\0') {
		zui_draw_text_aligned(
			draw, (struct zui_point){.x = ZUI_TOAST_TEXT_X, .y = (int16_t)(y + 6)},
			ZUI_ALIGN_LEFT, ZUI_ALIGN_TOP, toast->config.title);
	}
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	if (toast->config.text != NULL && toast->config.text[0] != '\0') {
		zui_draw_text_aligned(
			draw, (struct zui_point){.x = ZUI_TOAST_TEXT_X, .y = (int16_t)(y + 22)},
			ZUI_ALIGN_LEFT, ZUI_ALIGN_CENTER, toast->config.text);
	}

	if (restore_clip) {
		zui_draw_clear_clip(draw);
	}
}

int zui_toast_draw(struct zui_host *host, struct zui_draw_ctx *draw)
{
	struct zui_toast_draw_snapshot snapshot[ZUI_HOST_MAX_TOASTS];

	if (host == NULL || draw == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&host->lock, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(host->toasts); i++) {
		snapshot[i] = (struct zui_toast_draw_snapshot){
			.config = host->toasts[i].config,
			.used = host->toasts[i].used,
			.state = host->toasts[i].state,
			.y = host->toasts[i].y,
		};
	}
	k_mutex_unlock(&host->lock);

	for (size_t i = 0; i < ARRAY_SIZE(snapshot); i++) {
		if (snapshot[i].used && snapshot[i].state != ZUI_HOST_TOAST_HIDDEN) {
			zui_toast_draw_one(draw, &snapshot[i]);
		}
	}

	return 0;
}
