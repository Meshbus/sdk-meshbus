/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include "internal.h"

struct zui_actions {
	struct zui_actions_config config;
	const struct zui_action_item *items;
	size_t item_count;
	size_t selected;
	bool owns_items;
	bool freeze_input;
	struct zui_screen *screen;
};

#define ZUI_ACTIONS_VISIBLE_MAX 3U
#define ZUI_ACTIONS_TITLE_Y     8
#define ZUI_ACTIONS_CENTER_W    48U
#define ZUI_ACTIONS_CENTER_H    39U
#define ZUI_ACTIONS_CENTER_Y    14
#define ZUI_ACTIONS_SIDE_W      32U
#define ZUI_ACTIONS_SIDE_H      30U
#define ZUI_ACTIONS_SIDE_Y      19
#define ZUI_ACTIONS_EDGE_X      4
#define ZUI_ACTIONS_PAIR_W      50U
#define ZUI_ACTIONS_PAIR_H      39U
#define ZUI_ACTIONS_PAIR_GAP    8U
#define ZUI_ACTIONS_PAIR_Y      14
#define ZUI_ACTIONS_DOT_Y       57
#define ZUI_ACTIONS_ICON_W      24U
#define ZUI_ACTIONS_ICON_H      24U
#define ZUI_ACTIONS_LABEL_H     8U
#define ZUI_ACTIONS_LABEL_GAP   1U
#define ZUI_ACTIONS_SELECTED_ICON_Y_OFFSET 2

static void zui_actions_release_items(struct zui_actions *list)
{
	if (list == NULL) {
		return;
	}

	if (list->owns_items) {
		zui_free((void *)list->items);
	}
	list->items = NULL;
	list->item_count = 0U;
	list->owns_items = false;
}

static int zui_actions_bind_items(struct zui_actions *list,
				  const struct zui_actions_config *config)
{
	struct zui_action_item *items = NULL;

	if (config->item_count > 0U) {
		if (config->items == NULL) {
			return -EINVAL;
		}
		if (!config->copy_items) {
			zui_actions_release_items(list);
			list->items = config->items;
			list->item_count = config->item_count;
			return 0;
		}
		if (config->item_count > SIZE_MAX / sizeof(*items)) {
			return -EOVERFLOW;
		}
		items = zui_malloc(sizeof(*items) * config->item_count);
		if (items == NULL) {
			return -ENOMEM;
		}
		memcpy(items, config->items, sizeof(*items) * config->item_count);
	}

	zui_actions_release_items(list);
	list->items = items;
	list->item_count = config->item_count;
	list->owns_items = items != NULL;
	return 0;
}

static size_t zui_actions_visible_count(const struct zui_actions *list)
{
	return MIN(list->item_count, (size_t)ZUI_ACTIONS_VISIBLE_MAX);
}

static size_t zui_actions_visible_index(const struct zui_actions *list, size_t slot,
					size_t visible_count)
{
	if (list->item_count <= 2U) {
		return slot;
	}

	if (slot == 0U) {
		return list->selected > 0U ? list->selected - 1U : list->item_count - 1U;
	}
	if (slot == 1U) {
		return list->selected;
	}

	return list->selected + 1U < list->item_count ? list->selected + 1U : 0U;
}

static bool zui_actions_slot_is_prominent(size_t slot, size_t visible_count)
{
	return visible_count == 1U || (visible_count >= 3U && slot == 1U);
}

static struct zui_rect zui_actions_slot_rect(uint16_t draw_width, size_t slot,
					     size_t visible_count)
{
	if (visible_count == 1U) {
		return (struct zui_rect){
			.x = (int16_t)((draw_width - ZUI_ACTIONS_CENTER_W) / 2U),
			.y = ZUI_ACTIONS_CENTER_Y,
			.width = ZUI_ACTIONS_CENTER_W,
			.height = ZUI_ACTIONS_CENTER_H,
		};
	}

	if (visible_count == 2U) {
		uint16_t total_width = 2U * ZUI_ACTIONS_PAIR_W + ZUI_ACTIONS_PAIR_GAP;
		int16_t x = (int16_t)((draw_width - total_width) / 2U);

		return (struct zui_rect){
			.x = x + (int16_t)(slot * (ZUI_ACTIONS_PAIR_W + ZUI_ACTIONS_PAIR_GAP)),
			.y = ZUI_ACTIONS_PAIR_Y,
			.width = ZUI_ACTIONS_PAIR_W,
			.height = ZUI_ACTIONS_PAIR_H,
		};
	}

	if (slot == 1U) {
		return (struct zui_rect){
			.x = (int16_t)((draw_width - ZUI_ACTIONS_CENTER_W) / 2U),
			.y = ZUI_ACTIONS_CENTER_Y,
			.width = ZUI_ACTIONS_CENTER_W,
			.height = ZUI_ACTIONS_CENTER_H,
		};
	}

	return (struct zui_rect){
		.x = slot == 0U ? ZUI_ACTIONS_EDGE_X
				: (int16_t)(draw_width - ZUI_ACTIONS_EDGE_X -
					    ZUI_ACTIONS_SIDE_W),
		.y = ZUI_ACTIONS_SIDE_Y,
		.width = ZUI_ACTIONS_SIDE_W,
		.height = ZUI_ACTIONS_SIDE_H,
	};
}

static void zui_actions_draw_fit_center(struct zui_draw_ctx *draw, struct zui_rect rect,
					const char *text)
{
	char buffer[24];

	if (draw == NULL || text == NULL) {
		return;
	}

	(void)snprintf(buffer, sizeof(buffer), "%s", text);
	(void)zui_draw_text_fit_width(draw, buffer, sizeof(buffer), rect.width);
	zui_draw_text_aligned(draw,
			      (struct zui_point){.x = rect.x + (int16_t)(rect.width / 2U),
						 .y = rect.y + (int16_t)(rect.height / 2U)},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER, buffer);
}

static void zui_actions_draw_fit_bottom(struct zui_draw_ctx *draw, struct zui_rect rect,
					const char *text)
{
	char buffer[24];

	if (draw == NULL || text == NULL) {
		return;
	}

	(void)snprintf(buffer, sizeof(buffer), "%s", text);
	(void)zui_draw_text_fit_width(draw, buffer, sizeof(buffer), rect.width);
	zui_draw_text_aligned(draw,
			      (struct zui_point){.x = rect.x + (int16_t)(rect.width / 2U),
						 .y = rect.y + (int16_t)rect.height - 1},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_BOTTOM, buffer);
}

static void zui_actions_draw_item(struct zui_draw_ctx *draw, const struct zui_action_item *item,
				  struct zui_rect rect, bool selected, bool prominent)
{
	const uint16_t icon_box_w = ZUI_ACTIONS_ICON_W;
	const uint16_t icon_box_h = ZUI_ACTIONS_ICON_H;
	struct zui_rect content = {
		.x = rect.x + 3,
		.y = rect.y + 3,
		.width = rect.width > 6U ? rect.width - 6U : 0U,
		.height = rect.height > 6U ? rect.height - 6U : 0U,
	};
	bool has_icon = item->icon != NULL;
	uint16_t radius = 3U;

	if (selected) {
		zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	} else {
		if (prominent) {
			zui_draw_round_rect(draw, &rect, radius);
		}
		zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	}

	zui_draw_set_clip(draw, &content);
	if (has_icon) {
		uint16_t icon_w = zui_icon_width(item->icon);
		uint16_t icon_h = zui_icon_height(item->icon);
		uint16_t label_space =
			selected ? (uint16_t)(ZUI_ACTIONS_LABEL_H + ZUI_ACTIONS_LABEL_GAP) : 0U;
		uint16_t icon_area_h =
			content.height > label_space ? content.height - label_space : content.height;
		int16_t icon_y_offset = selected ? ZUI_ACTIONS_SELECTED_ICON_Y_OFFSET : 0;
		struct zui_point pos = {
			.x = content.x + ((int16_t)content.width - (int16_t)icon_w) / 2,
			.y = content.y + ((int16_t)icon_area_h - (int16_t)icon_h) / 2 -
			     icon_y_offset,
		};
		struct zui_rect icon_clip = {
			.x = content.x + (int16_t)((content.width - MIN(content.width, icon_box_w)) /
						   2U),
			.y = content.y + (int16_t)((icon_area_h - MIN(icon_area_h, icon_box_h)) /
						   2U) -
			     icon_y_offset,
			.width = MIN(content.width, icon_box_w),
			.height = MIN(icon_area_h, icon_box_h),
		};

		zui_draw_set_clip(draw, &icon_clip);
		zui_draw_icon(draw, pos, item->icon);
		zui_draw_set_clip(draw, &content);
		if (selected) {
			zui_actions_draw_fit_bottom(
				draw,
				(struct zui_rect){.x = content.x,
						  .y = content.y + (int16_t)icon_area_h +
						       ZUI_ACTIONS_LABEL_GAP,
						  .width = content.width,
						  .height = ZUI_ACTIONS_LABEL_H},
				item->label != NULL ? item->label : "");
		}
	} else if (selected) {
		zui_actions_draw_fit_center(draw, content, item->label != NULL ? item->label : "");
	}
	zui_draw_clear_clip(draw);
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
}

static void zui_actions_draw_position(struct zui_draw_ctx *draw, const struct zui_actions *list)
{
	uint16_t dot_width;
	int16_t x;

	if (draw == NULL || list == NULL || list->item_count <= 1U || list->item_count > 8U) {
		return;
	}

	dot_width = (uint16_t)(list->item_count * 5U - 2U);
	x = (int16_t)((zui_draw_width(draw) - dot_width) / 2U);
	for (size_t i = 0U; i < list->item_count; i++) {
		int16_t dot_x = x + (int16_t)(i * 5U);

		if (i == list->selected) {
			zui_draw_box(draw, &(struct zui_rect){.x = dot_x,
							      .y = ZUI_ACTIONS_DOT_Y - 1,
							      .width = 3,
							      .height = 3});
		} else {
			zui_draw_dot(draw,
				     (struct zui_point){.x = dot_x + 1,
							.y = ZUI_ACTIONS_DOT_Y});
		}
	}
}

static void zui_actions_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct zui_actions *list = user_data;
	uint16_t draw_width;
	size_t visible_count;

	zui_draw_reset(draw);
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	if (list == NULL || list->item_count == 0U) {
		return;
	}

	draw_width = zui_draw_width(draw);
	visible_count = zui_actions_visible_count(list);

	if (list->config.title != NULL) {
		zui_component_draw_fit_text(draw, (struct zui_point){.x = 2, .y = ZUI_ACTIONS_TITLE_Y},
					 draw_width > 42U ? draw_width - 42U : draw_width,
					 list->config.title);
	}

	for (size_t slot = 0U; slot < visible_count; slot++) {
		size_t index = zui_actions_visible_index(list, slot, visible_count);
		const struct zui_action_item *item = &list->items[index];
		bool prominent = zui_actions_slot_is_prominent(slot, visible_count);
		struct zui_rect rect = zui_actions_slot_rect(draw_width, slot, visible_count);

		zui_actions_draw_item(draw, item, rect, index == list->selected, prominent);
	}
	zui_actions_draw_position(draw, list);
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
}

static bool zui_actions_input(const struct zui_input_event *event, void *user_data)
{
	struct zui_actions *list = user_data;
	enum zui_move move;

	if (list == NULL || event == NULL) {
		return false;
	}
	if (event->code == ZUI_INPUT_CODE_SELECT && (event->action == ZUI_INPUT_ACTION_PRESS ||
						     event->action == ZUI_INPUT_ACTION_RELEASE)) {
		list->freeze_input = event->action == ZUI_INPUT_ACTION_PRESS;
		return true;
	}
	if (zui_component_is_select(event) || zui_component_is_long_select(event)) {
		return zui_actions_activate(list, event) == 0;
	}
	if (!list->freeze_input && zui_component_input_move(event, &move) == 0) {
		(void)zui_actions_move(list, move);
		return true;
	}

	return false;
}

static const struct zui_screen_ops zui_actions_screen_ops = {
	.draw = zui_actions_draw,
	.input = zui_actions_input,
};

struct zui_actions *zui_actions_create(const struct zui_actions_config *config)
{
	struct zui_actions *list;

	if (config == NULL) {
		return NULL;
	}

	list = zui_calloc(1U, sizeof(*list));
	if (list == NULL) {
		return NULL;
	}

	if (zui_actions_update(list, config) != 0) {
		zui_actions_destroy(list);
		return NULL;
	}

	list->screen = zui_screen_create(&zui_actions_screen_ops, list);
	if (list->screen == NULL) {
		zui_actions_destroy(list);
		return NULL;
	}

	return list;
}

void zui_actions_destroy(struct zui_actions *list)
{
	if (list == NULL) {
		return;
	}

	zui_screen_destroy(list->screen);
	zui_actions_release_items(list);
	zui_free(list);
}

struct zui_screen *zui_actions_get_screen(struct zui_actions *list)
{
	return list == NULL ? NULL : list->screen;
}

int zui_actions_update(struct zui_actions *list, const struct zui_actions_config *config)
{
	int rc;

	if (list == NULL || config == NULL) {
		return -EINVAL;
	}

	rc = zui_actions_bind_items(list, config);
	if (rc != 0) {
		return rc;
	}

	list->config = *config;
	if (list->item_count == 0U) {
		list->selected = 0U;
	} else if (list->selected >= list->item_count) {
		list->selected = list->item_count - 1U;
	}

	return zui_component_request_redraw(list->screen);
}

size_t zui_actions_selected(const struct zui_actions *list)
{
	return list == NULL ? 0U : list->selected;
}

int zui_actions_select(struct zui_actions *list, size_t index)
{
	if (list == NULL) {
		return -EINVAL;
	}
	if (index >= list->item_count) {
		return -ERANGE;
	}

	list->selected = index;
	return zui_component_request_redraw(list->screen);
}

int zui_actions_move(struct zui_actions *list, enum zui_move move)
{
	int rc;

	if (list == NULL) {
		return -EINVAL;
	}

	rc = zui_component_move_index_wrap(list->item_count, &list->selected, move);
	if (rc == 0) {
		(void)zui_component_request_redraw(list->screen);
	}

	return rc;
}

int zui_actions_activate(struct zui_actions *list, const struct zui_input_event *event)
{
	if (list == NULL) {
		return -EINVAL;
	}
	if (list->item_count == 0U) {
		return -ENOENT;
	}

	if (event != NULL && event->action != ZUI_INPUT_ACTION_CLICK &&
	    event->action != ZUI_INPUT_ACTION_LONG_PRESS) {
		return 0;
	}

	if (list->config.selected != NULL) {
		list->config.selected(list, list->items[list->selected].id, event,
				      list->config.user_data);
	}

	return 0;
}
