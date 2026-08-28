/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include "internal.h"

struct zui_modal {
	struct zui_modal_config config;
	struct zui_screen *screen;
};

static void zui_modal_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct zui_modal *modal = user_data;
	uint8_t text_line_spacing;

	zui_draw_reset(draw);
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	if (modal == NULL) {
		return;
	}

	if (modal->config.icon != NULL) {
		struct zui_point pos = modal->config.icon_placement.pos.x != 0 ||
						       modal->config.icon_placement.pos.y != 0
					       ? modal->config.icon_placement.pos
					       : (struct zui_point){.x = 4, .y = 18};

		zui_draw_icon(draw, pos, modal->config.icon);
	}

	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	zui_component_draw_placement_text(draw, &modal->config.title_placement,
				       modal->config.title, (struct zui_point){.x = 64, .y = 6},
				       ZUI_ALIGN_CENTER, ZUI_ALIGN_TOP, 0U);
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	text_line_spacing = zui_component_text_line_spacing(modal->config.text_line_spacing);
	zui_component_draw_placement_text(draw, &modal->config.text_placement, modal->config.text,
				       modal->config.icon != NULL
					       ? (struct zui_point){.x = 34, .y = 18}
					       : (struct zui_point){.x = 4, .y = 18},
				       ZUI_ALIGN_LEFT, ZUI_ALIGN_TOP, text_line_spacing);
	zui_component_draw_top_buttons(draw, modal->config.up_button, modal->config.down_button);
	zui_component_draw_buttons(draw, modal->config.left_button, modal->config.center_button,
				   modal->config.right_button);
}

static enum zui_modal_result zui_modal_result_from_input(const struct zui_input_event *event)
{
	if (event->code == ZUI_INPUT_CODE_UP) {
		return ZUI_MODAL_RESULT_UP;
	}
	if (event->code == ZUI_INPUT_CODE_LEFT || event->code == ZUI_INPUT_CODE_BACK) {
		return ZUI_MODAL_RESULT_LEFT;
	}
	if (event->code == ZUI_INPUT_CODE_RIGHT) {
		return ZUI_MODAL_RESULT_RIGHT;
	}
	if (event->code == ZUI_INPUT_CODE_DOWN) {
		return ZUI_MODAL_RESULT_DOWN;
	}

	return ZUI_MODAL_RESULT_CENTER;
}

static bool zui_modal_input(const struct zui_input_event *event, void *user_data)
{
	struct zui_modal *modal = user_data;
	bool has_up;
	bool has_left;
	bool has_center;
	bool has_right;
	bool has_down;

	if (modal == NULL || event == NULL) {
		return false;
	}
	if (event->action != ZUI_INPUT_ACTION_CLICK) {
		return false;
	}
	has_up = modal->config.up_button != NULL && modal->config.up_button[0] != '\0';
	has_left = modal->config.left_button != NULL && modal->config.left_button[0] != '\0';
	has_center =
		modal->config.center_button != NULL && modal->config.center_button[0] != '\0';
	has_right = modal->config.right_button != NULL && modal->config.right_button[0] != '\0';
	has_down = modal->config.down_button != NULL && modal->config.down_button[0] != '\0';
	if (event->code == ZUI_INPUT_CODE_UP && !has_up) {
		return false;
	}
	if ((event->code == ZUI_INPUT_CODE_LEFT || event->code == ZUI_INPUT_CODE_BACK) &&
	    !has_left) {
		return false;
	}
	if (event->code == ZUI_INPUT_CODE_SELECT && !has_center) {
		return false;
	}
	if (event->code == ZUI_INPUT_CODE_RIGHT && !has_right) {
		return false;
	}
	if (event->code == ZUI_INPUT_CODE_DOWN && !has_down) {
		return false;
	}
	if (event->code != ZUI_INPUT_CODE_LEFT && event->code != ZUI_INPUT_CODE_RIGHT &&
	    event->code != ZUI_INPUT_CODE_SELECT && event->code != ZUI_INPUT_CODE_BACK &&
	    event->code != ZUI_INPUT_CODE_UP && event->code != ZUI_INPUT_CODE_DOWN) {
		return false;
	}

	return zui_modal_submit(modal, zui_modal_result_from_input(event), event) == 0;
}

static const struct zui_screen_ops zui_modal_screen_ops = {
	.draw = zui_modal_draw,
	.input = zui_modal_input,
};

struct zui_modal *zui_modal_create(const struct zui_modal_config *config)
{
	struct zui_modal *modal;

	if (config == NULL) {
		return NULL;
	}

	modal = zui_calloc(1U, sizeof(*modal));
	if (modal == NULL) {
		return NULL;
	}

	(void)zui_modal_update(modal, config);
	modal->screen = zui_screen_create(&zui_modal_screen_ops, modal);
	if (modal->screen == NULL) {
		zui_modal_destroy(modal);
		return NULL;
	}

	return modal;
}

void zui_modal_destroy(struct zui_modal *modal)
{
	if (modal == NULL) {
		return;
	}

	zui_screen_destroy(modal->screen);
	zui_free(modal);
}

struct zui_screen *zui_modal_get_screen(struct zui_modal *modal)
{
	return modal == NULL ? NULL : modal->screen;
}

int zui_modal_update(struct zui_modal *modal, const struct zui_modal_config *config)
{
	if (modal == NULL || config == NULL) {
		return -EINVAL;
	}

	modal->config = *config;
	return zui_component_request_redraw(modal->screen);
}

int zui_modal_submit(struct zui_modal *modal, enum zui_modal_result result,
		       const struct zui_input_event *event)
{
	if (modal == NULL) {
		return -EINVAL;
	}

	if (modal->config.result != NULL) {
		modal->config.result(modal, result, event, modal->config.user_data);
	}

	return 0;
}
