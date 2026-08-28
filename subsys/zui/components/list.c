/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include "internal.h"

struct zui_list {
	struct zui_list_config config;
	const struct zui_list_item *items;
	size_t item_count;
	size_t selected;
	size_t window_position;
	bool owns_items;
	bool active;
	struct zui_screen *screen;
};

static size_t zui_list_effective_count(const struct zui_list *list)
{
	if (list == NULL) {
		return 0U;
	}
	if (list->config.count != NULL) {
		return list->config.count(list->config.user_data);
	}

	return list->item_count;
}

static int zui_list_get_item_at(const struct zui_list *list, size_t index,
				struct zui_list_item *item)
{
	if (list == NULL || item == NULL || index >= zui_list_effective_count(list)) {
		return -EINVAL;
	}

	if (list->config.get_item != NULL) {
		return list->config.get_item(index, item, list->config.user_data);
	}

	*item = list->items[index];
	return 0;
}

static void zui_list_release_items(struct zui_list *list)
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

static int zui_list_bind_items(struct zui_list *list, const struct zui_list_config *config)
{
	struct zui_list_item *items = NULL;

	if (config->get_item != NULL || config->count != NULL) {
		if (config->get_item == NULL || config->count == NULL) {
			return -EINVAL;
		}
		zui_list_release_items(list);
		return 0;
	}

	if (config->item_count > 0U) {
		if (config->items == NULL) {
			return -EINVAL;
		}
		if (!config->copy_items) {
			zui_list_release_items(list);
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

	zui_list_release_items(list);
	list->items = items;
	list->item_count = config->item_count;
	list->owns_items = items != NULL;
	return 0;
}

static int zui_list_get_selected_item(const struct zui_list *list, struct zui_list_item *item)
{
	if (list == NULL) {
		return -EINVAL;
	}

	return zui_list_get_item_at(list, list->selected, item);
}

static void zui_list_fix_window(struct zui_list *list)
{
	size_t count;
	const size_t rows = 3U;
	size_t max_window;

	if (list == NULL) {
		return;
	}

	count = zui_list_effective_count(list);
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

static void zui_list_start_selected_anim(struct zui_list *list)
{
	struct zui_list_item item = {0};

	if (list == NULL || !list->active || zui_list_get_selected_item(list, &item) != 0 ||
	    item.icon_anim == NULL) {
		return;
	}

	zui_icon_anim_start(item.icon_anim);
}

static void zui_list_stop_selected_anim(struct zui_list *list)
{
	struct zui_list_item item = {0};

	if (list == NULL || zui_list_get_selected_item(list, &item) != 0 ||
	    item.icon_anim == NULL) {
		return;
	}

	zui_icon_anim_stop(item.icon_anim);
}

static void zui_list_draw_menu(struct zui_draw_ctx *draw, struct zui_list *list, size_t count)
{
	for (size_t row = 0U; row < 3U; row++) {
		struct zui_list_item item = {0};
		size_t item_index = (row + list->selected + count - 1U) % count;
		int16_t icon_y = row == 0U ? 3 : row == 1U ? 25 : 47;
		int16_t text_y = row == 0U ? 14 : row == 1U ? 36 : 58;

		if (zui_list_get_item_at(list, item_index, &item) != 0) {
			continue;
		}

		zui_draw_set_font(draw, row == 1U ? ZUI_FONT_PRIMARY : ZUI_FONT_SECONDARY);
		if (item.icon_anim != NULL && row == 1U) {
			zui_draw_icon_anim(draw, (struct zui_point){.x = 4, .y = icon_y},
					   item.icon_anim);
		} else if (item.icon_anim != NULL) {
			zui_draw_icon(draw, (struct zui_point){.x = 4, .y = icon_y},
				      zui_icon_anim_icon(item.icon_anim));
		} else if (item.icon != NULL) {
			zui_draw_icon(draw, (struct zui_point){.x = 4, .y = icon_y}, item.icon);
		}
		zui_draw_text(draw, (struct zui_point){.x = 22, .y = text_y},
			      item.label != NULL ? item.label : "?");
	}

	zui_component_draw_selection_frame(draw, 0, 21, 123, 21);
	zui_component_draw_scrollbar(draw, list->selected, count);
}

static void zui_list_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct zui_list *list = user_data;
	size_t count = zui_list_effective_count(list);

	zui_draw_reset(draw);
	if (list == NULL || count == 0U) {
		zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
		zui_draw_text(draw, (struct zui_point){.x = 2, .y = 32}, "Empty");
		zui_component_draw_scrollbar(draw, 0U, 0U);
		return;
	}

	zui_list_fix_window(list);
	zui_list_draw_menu(draw, list, count);
}

static bool zui_list_input(const struct zui_input_event *event, void *user_data)
{
	struct zui_list *list = user_data;
	enum zui_move move;

	if (list == NULL || event == NULL) {
		return false;
	}

	if (zui_component_input_move(event, &move) == 0) {
		(void)zui_list_move(list, move);
		return true;
	}

	if (zui_component_is_select(event) || zui_component_is_long_select(event)) {
		return zui_list_activate(list, event) == 0;
	}

	return false;
}

static void zui_list_enter(void *user_data)
{
	struct zui_list *list = user_data;

	if (list == NULL) {
		return;
	}

	list->active = true;
	zui_list_start_selected_anim(list);
}

static void zui_list_exit(void *user_data)
{
	struct zui_list *list = user_data;

	if (list == NULL) {
		return;
	}

	zui_list_stop_selected_anim(list);
	list->active = false;
}

static const struct zui_screen_ops zui_list_screen_ops = {
	.draw = zui_list_draw,
	.input = zui_list_input,
	.enter = zui_list_enter,
	.exit = zui_list_exit,
};

struct zui_list *zui_list_create(const struct zui_list_config *config)
{
	struct zui_list *list;

	if (config == NULL) {
		return NULL;
	}

	list = zui_calloc(1U, sizeof(*list));
	if (list == NULL) {
		return NULL;
	}

	if (zui_list_update(list, config) != 0) {
		zui_list_destroy(list);
		return NULL;
	}

	list->screen = zui_screen_create(&zui_list_screen_ops, list);
	if (list->screen == NULL) {
		zui_list_destroy(list);
		return NULL;
	}

	return list;
}

void zui_list_destroy(struct zui_list *list)
{
	if (list == NULL) {
		return;
	}

	if (list->active) {
		zui_list_stop_selected_anim(list);
		list->active = false;
	}
	zui_screen_destroy(list->screen);
	zui_list_release_items(list);
	zui_free(list);
}

struct zui_screen *zui_list_get_screen(struct zui_list *list)
{
	return list == NULL ? NULL : list->screen;
}

int zui_list_update(struct zui_list *list, const struct zui_list_config *config)
{
	int rc;

	if (list == NULL || config == NULL) {
		return -EINVAL;
	}

	if (list->active) {
		zui_list_stop_selected_anim(list);
	}
	rc = zui_list_bind_items(list, config);
	if (rc != 0) {
		if (list->active) {
			zui_list_start_selected_anim(list);
		}
		return rc;
	}

	list->config = *config;
	return zui_list_reload(list);
}

int zui_list_reload(struct zui_list *list)
{
	size_t count;

	if (list == NULL) {
		return -EINVAL;
	}

	count = zui_list_effective_count(list);
	if (list->active) {
		zui_list_stop_selected_anim(list);
	}
	if (count == 0U) {
		list->selected = 0U;
	} else if (list->selected >= count) {
		list->selected = count - 1U;
	}
	zui_list_fix_window(list);
	zui_list_start_selected_anim(list);

	return zui_component_request_redraw(list->screen);
}

size_t zui_list_count(const struct zui_list *list)
{
	return zui_list_effective_count(list);
}

size_t zui_list_selected(const struct zui_list *list)
{
	return list == NULL ? 0U : list->selected;
}

int zui_list_select(struct zui_list *list, size_t index)
{
	if (list == NULL) {
		return -EINVAL;
	}
	if (index >= zui_list_effective_count(list)) {
		return -ERANGE;
	}

	if (list->active && index != list->selected) {
		zui_list_stop_selected_anim(list);
	}
	list->selected = index;
	zui_list_fix_window(list);
	zui_list_start_selected_anim(list);
	return zui_component_request_redraw(list->screen);
}

int zui_list_move(struct zui_list *list, enum zui_move move)
{
	int rc;

	if (list == NULL) {
		return -EINVAL;
	}

	if (list->active) {
		zui_list_stop_selected_anim(list);
	}
	rc = zui_component_move_index_wrap(zui_list_effective_count(list), &list->selected, move);
	if (rc == 0) {
		zui_list_fix_window(list);
		if (list->active) {
			zui_list_start_selected_anim(list);
		}
		(void)zui_component_request_redraw(list->screen);
	} else if (list->active) {
		zui_list_start_selected_anim(list);
	}

	return rc;
}

int zui_list_activate(struct zui_list *list, const struct zui_input_event *event)
{
	struct zui_list_item item = {0};

	if (list == NULL) {
		return -EINVAL;
	}
	if (zui_list_get_item_at(list, list->selected, &item) != 0) {
		return -ENOENT;
	}

	if (list->config.selected != NULL) {
		list->config.selected(list, item.id, list->selected, event, list->config.user_data);
	}

	return 0;
}

int zui_list_set_item(struct zui_list *list, size_t index, const struct zui_list_item *item)
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
