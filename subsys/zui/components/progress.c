/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include "internal.h"

struct zui_progress {
	struct zui_progress_config config;
	struct zui_screen *screen;
	struct zui_icon_anim *icon_anim;
};

static float zui_progress_clamp(float value)
{
	if (value < 0.0f) {
		return 0.0f;
	}
	if (value > 1.0f) {
		return 1.0f;
	}

	return value;
}

static void zui_progress_icon_anim_updated(struct zui_icon_anim *anim, void *user_data)
{
	struct zui_progress *progress = user_data;

	ARG_UNUSED(anim);

	if (progress != NULL) {
		(void)zui_component_request_redraw(progress->screen);
	}
}

static void zui_progress_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct zui_progress *progress = user_data;
	const struct zui_icon *icon;
	uint16_t bar_width;
	int16_t bar_x;
	int16_t bar_y;

	zui_draw_reset(draw);
	if (progress == NULL) {
		return;
	}

	zui_draw_set_color(draw, ZUI_COLOR_WHITE);
	zui_draw_box(draw, &(struct zui_rect){.x = 0,
					      .y = 0,
					      .width = zui_draw_width(draw),
					      .height = zui_draw_height(draw)});
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);

	icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
					 ZUI_ASSET_ICON_ACTIVITY_LOADING_10);
	if (icon != NULL) {
		struct zui_point icon_pos = {
			.x = (int16_t)(zui_draw_width(draw) / 2U - zui_icon_width(icon) / 2U),
			.y = (int16_t)(zui_draw_height(draw) / 2U - zui_icon_height(icon) / 2U),
		};

		if (progress->icon_anim != NULL) {
			zui_draw_icon_anim(draw, icon_pos, progress->icon_anim);
		} else {
			zui_draw_icon(draw, icon_pos, icon);
		}
	}

	bar_width = MIN((uint16_t)(zui_draw_width(draw) - 16U), 112U);
	bar_x = (int16_t)((zui_draw_width(draw) - bar_width) / 2U);
	bar_y = (int16_t)(zui_draw_height(draw) - 14U);
	zui_component_draw_progress_bar_text(
		draw, bar_x, bar_y, bar_width, progress->config.value,
		progress->config.text != NULL ? progress->config.text : "");
}

static bool zui_progress_input(const struct zui_input_event *event, void *user_data)
{
	ARG_UNUSED(event);
	ARG_UNUSED(user_data);

	return true;
}

static void zui_progress_enter(void *user_data)
{
	struct zui_progress *progress = user_data;

	if (progress == NULL || progress->icon_anim == NULL) {
		return;
	}

	zui_icon_anim_start(progress->icon_anim);
	(void)zui_component_request_redraw(progress->screen);
}

static void zui_progress_exit(void *user_data)
{
	struct zui_progress *progress = user_data;

	if (progress == NULL || progress->icon_anim == NULL) {
		return;
	}

	zui_icon_anim_stop(progress->icon_anim);
	(void)zui_component_request_redraw(progress->screen);
}

static const struct zui_screen_ops zui_progress_screen_ops = {
	.draw = zui_progress_draw,
	.input = zui_progress_input,
	.enter = zui_progress_enter,
	.exit = zui_progress_exit,
};

struct zui_progress *zui_progress_create(const struct zui_progress_config *config)
{
	struct zui_progress *progress;

	if (config == NULL) {
		return NULL;
	}

	progress = zui_calloc(1U, sizeof(*progress));
	if (progress == NULL) {
		return NULL;
	}

	(void)zui_progress_update(progress, config);
	progress->icon_anim = zui_icon_anim_create(
		zui_asset_pack_icon_by_id(zui_asset_pack_default(),
					  ZUI_ASSET_ICON_ACTIVITY_LOADING_10));
	if (progress->icon_anim != NULL) {
		zui_icon_anim_set_update_callback(progress->icon_anim,
						  zui_progress_icon_anim_updated, progress);
	}
	progress->screen = zui_screen_create(&zui_progress_screen_ops, progress);
	if (progress->screen == NULL) {
		zui_progress_destroy(progress);
		return NULL;
	}

	return progress;
}

void zui_progress_destroy(struct zui_progress *progress)
{
	if (progress == NULL) {
		return;
	}

	zui_screen_destroy(progress->screen);
	zui_icon_anim_destroy(progress->icon_anim);
	zui_free(progress);
}

struct zui_screen *zui_progress_get_screen(struct zui_progress *progress)
{
	return progress == NULL ? NULL : progress->screen;
}

int zui_progress_update(struct zui_progress *progress, const struct zui_progress_config *config)
{
	if (progress == NULL || config == NULL) {
		return -EINVAL;
	}

	progress->config = *config;
	progress->config.value = zui_progress_clamp(progress->config.value);
	return zui_component_request_redraw(progress->screen);
}

float zui_progress_value(const struct zui_progress *progress)
{
	return progress == NULL ? 0.0f : progress->config.value;
}

int zui_progress_set(struct zui_progress *progress, float value, const char *text)
{
	if (progress == NULL) {
		return -EINVAL;
	}

	progress->config.value = zui_progress_clamp(value);
	progress->config.text = text;
	return zui_component_request_redraw(progress->screen);
}
