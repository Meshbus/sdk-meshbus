/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include "internal.h"

struct zui_sublist {
	struct zui_sublist_config config;
	const struct zui_list_item *items;
	size_t item_count;
	size_t selected;
	size_t window_position;
	bool owns_items;
	struct zui_screen *screen;
};

static size_t zui_sublist_effective_count(const struct zui_sublist *list)
{
	if (list == NULL) {
		return 0U;
	}
	if (list->config.count != NULL) {
		return list->config.count(list->config.user_data);
	}

	return list->item_count;
}

static int zui_sublist_get_item_at(const struct zui_sublist *list, size_t index,
				   struct zui_list_item *item)
{
	if (list == NULL || item == NULL || index >= zui_sublist_effective_count(list)) {
		return -EINVAL;
	}

	if (list->config.get_item != NULL) {
		return list->config.get_item(index, item, list->config.user_data);
	}

	*item = list->items[index];
	return 0;
}

static void zui_sublist_release_items(struct zui_sublist *list)
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

static int zui_sublist_bind_items(struct zui_sublist *list,
				  const struct zui_sublist_config *config)
{
	struct zui_list_item *items = NULL;

	if (config->get_item != NULL || config->count != NULL) {
		if (config->get_item == NULL || config->count == NULL) {
			return -EINVAL;
		}
		zui_sublist_release_items(list);
		return 0;
	}

	if (config->item_count > 0U) {
		if (config->items == NULL) {
			return -EINVAL;
		}
		if (!config->copy_items) {
			zui_sublist_release_items(list);
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

	zui_sublist_release_items(list);
	list->items = items;
	list->item_count = config->item_count;
	list->owns_items = items != NULL;
	return 0;
}

static bool zui_sublist_has_title(const struct zui_sublist *list)
{
	return list != NULL && list->config.title != NULL && list->config.title[0] != '\0';
}

static size_t zui_sublist_visible_rows(const struct zui_sublist *list)
{
	return zui_sublist_has_title(list) ? 3U : 4U;
}

static void zui_sublist_fix_window(struct zui_sublist *list)
{
	size_t count;
	size_t rows;
	size_t max_window;

	if (list == NULL) {
		return;
	}

	count = zui_sublist_effective_count(list);
	rows = zui_sublist_visible_rows(list);
	if (count == 0U || count <= rows) {
		list->window_position = 0U;
		return;
	}

	max_window = count - rows;
	if (list->selected >= count) {
		list->selected = count - 1U;
	}
	if (list->window_position > list->selected) {
		list->window_position = list->selected;
	}
	if (list->selected - list->window_position > rows - 1U) {
		list->window_position = list->selected - (rows - 1U);
	}
	if (list->window_position > max_window) {
		list->window_position = max_window;
	}
}

static void zui_sublist_draw_item_text(struct zui_draw_ctx *draw, int16_t x, int16_t y,
				       uint16_t width, const char *text)
{
	char buffer[48];

	if (text == NULL || width == 0U) {
		return;
	}

	(void)snprintf(buffer, sizeof(buffer), "%s", text);
	(void)zui_draw_text_fit_width(draw, buffer, sizeof(buffer), width);
	zui_draw_text(draw, (struct zui_point){.x = x, .y = y}, buffer);
}

static void zui_sublist_draw_rows(struct zui_draw_ctx *draw, struct zui_sublist *list,
				  size_t count)
{
	const uint8_t item_height = 16U;
	const uint8_t item_width = zui_draw_width(draw) > 5U ? zui_draw_width(draw) - 5U : 0U;
	const bool has_title = zui_sublist_has_title(list);
	const size_t rows = zui_sublist_visible_rows(list);
	const int16_t y_offset = has_title ? 16 : 0;

	if (has_title) {
		zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
		zui_component_draw_fit_text(draw, (struct zui_point){.x = 4, .y = 11}, 120,
					 list->config.title);
	}

	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	for (size_t row = 0U; row < rows; row++) {
		struct zui_list_item item = {0};
		size_t item_index = list->window_position + row;
		int16_t row_y = (int16_t)(y_offset + (int16_t)(row * item_height));
		int16_t text_y = (int16_t)(row_y + item_height - 4);
		int16_t label_x = 6;
		uint16_t right_w = 0U;

		if (item_index >= count || zui_sublist_get_item_at(list, item_index, &item) != 0) {
			continue;
		}

		if (item_index == list->selected) {
			zui_draw_set_color(draw, ZUI_COLOR_BLACK);
			zui_draw_round_box(draw,
					   &(struct zui_rect){.x = 0,
							      .y = (int16_t)(row_y + 1),
							      .width = item_width,
							      .height = item_height - 2U},
					   1U);
			zui_draw_set_color(draw, ZUI_COLOR_WHITE);
		} else {
			zui_draw_set_color(draw, ZUI_COLOR_BLACK);
		}

		if (item.icon != NULL) {
			int16_t icon_y = (int16_t)(row_y + 1 +
				((int16_t)(item_height - 2U) - (int16_t)zui_icon_height(item.icon)) / 2);

			if (icon_y < 0) {
				icon_y = 0;
			}
			zui_draw_icon(draw, (struct zui_point){.x = 2, .y = icon_y}, item.icon);
			label_x = (int16_t)(6 + zui_icon_width(item.icon) + 2);
		}

		if (item.detail != NULL && item.detail[0] != '\0') {
			right_w = zui_draw_text_width(draw, item.detail);
		}

		if (right_w > 0U) {
			int16_t right_x = (int16_t)item_width - 6 - (int16_t)right_w;

			if (right_x < label_x) {
				right_x = label_x;
			}
			zui_draw_text(draw, (struct zui_point){.x = right_x, .y = text_y},
				      item.detail);
		}

		{
			int32_t available = (int32_t)item_width - label_x - 6;

			if (right_w > 0U) {
				available -= (int32_t)right_w + 6;
			}
			if (available < 0) {
				available = 0;
			}
			zui_sublist_draw_item_text(draw, label_x, text_y, (uint16_t)available,
						   item.label != NULL ? item.label : "?");
		}

		if (item_index == list->selected) {
			zui_draw_set_color(draw, ZUI_COLOR_BLACK);
		}
	}

	zui_component_draw_scrollbar(draw, list->selected, count);
}

static void zui_sublist_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct zui_sublist *list = user_data;
	size_t count = zui_sublist_effective_count(list);

	zui_draw_reset(draw);
	if (list == NULL || count == 0U) {
		zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
		zui_draw_text(draw, (struct zui_point){.x = 2, .y = 32}, "Empty");
		zui_component_draw_scrollbar(draw, 0U, 0U);
		return;
	}

	zui_sublist_fix_window(list);
	zui_sublist_draw_rows(draw, list, count);
}

static bool zui_sublist_input(const struct zui_input_event *event, void *user_data)
{
	struct zui_sublist *list = user_data;
	enum zui_move move;

	if (list == NULL || event == NULL) {
		return false;
	}

	if (zui_component_input_move(event, &move) == 0) {
		(void)zui_sublist_move(list, move);
		return true;
	}

	if (zui_component_is_select(event) || zui_component_is_long_select(event)) {
		return zui_sublist_activate(list, event) == 0;
	}

	return false;
}

static const struct zui_screen_ops zui_sublist_screen_ops = {
	.draw = zui_sublist_draw,
	.input = zui_sublist_input,
};

struct zui_sublist *zui_sublist_create(const struct zui_sublist_config *config)
{
	struct zui_sublist *list;

	if (config == NULL) {
		return NULL;
	}

	list = zui_calloc(1U, sizeof(*list));
	if (list == NULL) {
		return NULL;
	}

	if (zui_sublist_update(list, config) != 0) {
		zui_sublist_destroy(list);
		return NULL;
	}

	list->screen = zui_screen_create(&zui_sublist_screen_ops, list);
	if (list->screen == NULL) {
		zui_sublist_destroy(list);
		return NULL;
	}

	return list;
}

void zui_sublist_destroy(struct zui_sublist *list)
{
	if (list == NULL) {
		return;
	}

	zui_screen_destroy(list->screen);
	zui_sublist_release_items(list);
	zui_free(list);
}

struct zui_screen *zui_sublist_get_screen(struct zui_sublist *list)
{
	return list == NULL ? NULL : list->screen;
}

int zui_sublist_update(struct zui_sublist *list, const struct zui_sublist_config *config)
{
	int rc;

	if (list == NULL || config == NULL) {
		return -EINVAL;
	}

	rc = zui_sublist_bind_items(list, config);
	if (rc != 0) {
		return rc;
	}

	list->config = *config;
	return zui_sublist_reload(list);
}

int zui_sublist_reload(struct zui_sublist *list)
{
	size_t count;

	if (list == NULL) {
		return -EINVAL;
	}

	count = zui_sublist_effective_count(list);
	if (count == 0U) {
		list->selected = 0U;
	} else if (list->selected >= count) {
		list->selected = count - 1U;
	}
	zui_sublist_fix_window(list);

	return zui_component_request_redraw(list->screen);
}

size_t zui_sublist_count(const struct zui_sublist *list)
{
	return zui_sublist_effective_count(list);
}

size_t zui_sublist_selected(const struct zui_sublist *list)
{
	return list == NULL ? 0U : list->selected;
}

int zui_sublist_select(struct zui_sublist *list, size_t index)
{
	if (list == NULL) {
		return -EINVAL;
	}
	if (index >= zui_sublist_effective_count(list)) {
		return -ERANGE;
	}

	list->selected = index;
	zui_sublist_fix_window(list);
	return zui_component_request_redraw(list->screen);
}

int zui_sublist_move(struct zui_sublist *list, enum zui_move move)
{
	int rc;

	if (list == NULL) {
		return -EINVAL;
	}

	rc = zui_component_move_index_wrap(zui_sublist_effective_count(list), &list->selected, move);
	if (rc == 0) {
		zui_sublist_fix_window(list);
		(void)zui_component_request_redraw(list->screen);
	}

	return rc;
}

int zui_sublist_activate(struct zui_sublist *list, const struct zui_input_event *event)
{
	struct zui_list_item item = {0};

	if (list == NULL) {
		return -EINVAL;
	}
	if (zui_sublist_get_item_at(list, list->selected, &item) != 0) {
		return -ENOENT;
	}

	if (list->config.selected != NULL) {
		list->config.selected(list, item.id, list->selected, event, list->config.user_data);
	}

	return 0;
}

int zui_sublist_set_item(struct zui_sublist *list, size_t index, const struct zui_list_item *item)
{
	if (list == NULL || item == NULL) {
		return -EINVAL;
	}
	if (list->config.get_item != NULL || list->config.count != NULL) {
		return -ENOTSUP;
	}
	if (index >= list->item_count) {
		return -ERANGE;
	}
	if (!list->owns_items) {
		return -EACCES;
	}

	((struct zui_list_item *)list->items)[index] = *item;
	return zui_component_request_redraw(list->screen);
}
