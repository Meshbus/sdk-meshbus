/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include "internal.h"

#include <zephyr/kernel.h>

#define ZUI_FORM_SCROLL_INTERVAL_MS 333U

struct zui_form {
	struct zui_form_config config;
	struct zui_form_item *items;
	size_t item_count;
	size_t selected;
	size_t window_position;
	size_t scroll_counter;
	struct k_work_delayable scroll_work;
	bool edit_mode;
	bool active;
	bool scroll_work_running;
	struct zui_screen *screen;
};

static void zui_form_scroll_work_handler(struct k_work *work);

static int zui_form_find(const struct zui_form *form, uint32_t id)
{
	if (form == NULL) {
		return -EINVAL;
	}

	for (size_t i = 0U; i < form->item_count; i++) {
		if (form->items[i].id == id) {
			return (int)i;
		}
	}

	return -ENOENT;
}

static int zui_form_copy_items(struct zui_form *form, const struct zui_form_config *config)
{
	struct zui_form_item *items = NULL;

	if (config->item_count > 0U) {
		if (config->items == NULL) {
			return -EINVAL;
		}
		items = zui_malloc(sizeof(*items) * config->item_count);
		if (items == NULL) {
			return -ENOMEM;
		}
		memcpy(items, config->items, sizeof(*items) * config->item_count);
	}

	zui_free(form->items);
	form->items = items;
	form->item_count = config->item_count;
	return 0;
}

static const char *zui_form_item_value(const struct zui_form_item *item)
{
	if (item == NULL) {
		return NULL;
	}
	if (item->value_text != NULL) {
		return item->value_text;
	}
	if (item->options != NULL && item->option_index < item->option_count) {
		return item->options[item->option_index];
	}

	return NULL;
}

static void zui_form_fix_window(struct zui_form *form)
{
	const size_t rows = 4U;
	size_t max_window;

	if (form == NULL) {
		return;
	}

	if (form->item_count == 0U || form->item_count <= rows) {
		form->window_position = 0U;
		return;
	}

	if (form->selected >= form->item_count) {
		form->selected = form->item_count - 1U;
	}

	max_window = form->item_count - rows;
	if (form->window_position > form->selected) {
		form->window_position = form->selected;
	}
	if (form->selected >= form->window_position + rows) {
		form->window_position = form->selected - (rows - 1U);
	}
	if (form->window_position > max_window) {
		form->window_position = max_window;
	}
}

static void zui_form_reset_scroll(struct zui_form *form)
{
	if (form != NULL) {
		form->scroll_counter = 0U;
	}
}

static void zui_form_scroll_start(struct zui_form *form)
{
	if (form == NULL || form->scroll_work_running) {
		return;
	}

	form->scroll_work_running = true;
	(void)k_work_reschedule(&form->scroll_work, K_MSEC(ZUI_FORM_SCROLL_INTERVAL_MS));
}

static void zui_form_scroll_stop(struct zui_form *form)
{
	if (form == NULL) {
		return;
	}
	if (!form->scroll_work_running) {
		return;
	}

	form->scroll_work_running = false;
	(void)k_work_cancel_delayable(&form->scroll_work);
}

static void zui_form_scroll_set_needed(struct zui_form *form, bool needed)
{
	if (form == NULL || !form->active) {
		return;
	}

	if (needed) {
		zui_form_scroll_start(form);
	} else {
		zui_form_scroll_stop(form);
	}
}

#if defined(CONFIG_ZUI_TEXT_UTF8)
static size_t zui_form_utf8_prev_boundary(const char *text, size_t end)
{
	size_t start;

	if (text == NULL || end == 0U) {
		return 0U;
	}

	start = end - 1U;
	while (start > 0U && (((uint8_t)text[start] & 0xc0U) == 0x80U)) {
		start--;
	}

	return start;
}

static size_t zui_form_utf8_next_boundary(const char *text, size_t len, size_t index)
{
	if (text == NULL || index >= len) {
		return len;
	}

	while (index < len && (((uint8_t)text[index] & 0xc0U) == 0x80U)) {
		index++;
	}

	return index;
}

static uint16_t zui_form_utf8_glyph_width(struct zui_draw_ctx *draw, const char *text, size_t len)
{
	char glyph[5];

	len = MIN(len, sizeof(glyph) - 1U);
	memcpy(glyph, text, len);
	glyph[len] = '\0';
	return zui_draw_text_width(draw, glyph);
}
#endif

static void zui_form_draw_scrolled_text(struct zui_draw_ctx *draw, struct zui_point pos,
					uint16_t width, const char *text, size_t scroll,
					bool ellipsis)
{
	char buf[64];
	size_t src_len;
	size_t avail_width;
	size_t scroll_size;
	size_t right_width = 0U;
	size_t start = 0U;
	size_t end;
	size_t max_copy;
	size_t n;

	if (draw == NULL || text == NULL || width == 0U) {
		return;
	}
	if (zui_draw_text_width(draw, text) <= width) {
		zui_draw_text(draw, pos, text);
		return;
	}

	src_len = strlen(text);
	avail_width = width;
	if (ellipsis) {
		uint16_t dots_px = zui_draw_text_width(draw, "...");

		avail_width = avail_width > dots_px ? avail_width - dots_px : 0U;
	}

	scroll_size = src_len;
#if defined(CONFIG_ZUI_TEXT_UTF8)
	for (size_t end_idx = src_len; end_idx > 0U;) {
		size_t glyph_start = zui_form_utf8_prev_boundary(text, end_idx);

		right_width +=
			zui_form_utf8_glyph_width(draw, text + glyph_start, end_idx - glyph_start);
		if (right_width > avail_width) {
			break;
		}
		scroll_size = glyph_start;
		end_idx = glyph_start;
		if (scroll_size == 0U) {
			break;
		}
	}
#else
	for (size_t i = src_len; i > 0U; i--) {
		right_width += zui_draw_glyph_width(draw, (uint8_t)text[i - 1U]);
		if (right_width > avail_width) {
			break;
		}
		if (scroll_size == 0U) {
			break;
		}
		scroll_size--;
		if (scroll_size == 0U) {
			break;
		}
	}
#endif

	if (scroll_size != 0U) {
		scroll_size += 3U;
		start = scroll % scroll_size;
		if (start > src_len) {
			start = src_len;
		}
#if defined(CONFIG_ZUI_TEXT_UTF8)
		start = zui_form_utf8_next_boundary(text, src_len, start);
#endif
	}

	end = src_len;
	max_copy = sizeof(buf) - 1U;
	if (ellipsis && max_copy >= 3U) {
		max_copy -= 3U;
	}
	if (end - start > max_copy) {
		end = start + max_copy;
#if defined(CONFIG_ZUI_TEXT_UTF8)
		end = zui_form_utf8_prev_boundary(text, end);
		if (end < start) {
			end = start;
		}
#endif
	}

	while (end > start) {
		n = end - start;
		memcpy(buf, text + start, n);
		buf[n] = '\0';
		if (zui_draw_text_width(draw, buf) <= avail_width) {
			break;
		}
#if defined(CONFIG_ZUI_TEXT_UTF8)
		end = zui_form_utf8_prev_boundary(text, end);
#else
		end--;
#endif
	}

	n = end - start;
	memcpy(buf, text + start, n);
	buf[n] = '\0';
	if (ellipsis) {
		(void)strncat(buf, "...", sizeof(buf) - strlen(buf) - 1U);
	}
	zui_draw_text(draw, pos, buf);
}

static void zui_form_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct zui_form *form = user_data;
	const uint8_t item_height = 16U;
	const uint8_t item_width = 123U;
	const int16_t value_left_x = 73;
	const int16_t value_right_x = 115;
	const int16_t value_right_padding = 2;
	uint16_t left_arrow_slot_width;
	bool needs_scroll = false;

	zui_draw_reset(draw);
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	if (form == NULL || form->item_count == 0U) {
		zui_form_scroll_set_needed(form, false);
		zui_draw_text(draw, (struct zui_point){.x = 6, .y = 28}, "Empty");
		zui_component_draw_scrollbar(draw, 0U, 0U);
		return;
	}

	left_arrow_slot_width = (uint16_t)(zui_draw_text_width(draw, "<") + 2U);
	for (size_t row = 0U; row < 4U && form->window_position + row < form->item_count; row++) {
		size_t index = form->window_position + row;
		const struct zui_form_item *item = &form->items[index];
		bool selected = index == form->selected;
		int16_t item_y = (int16_t)(row * item_height);
		int16_t text_y = (int16_t)(item_y + item_height - 4U);
		const char *value = zui_form_item_value(item);
		int16_t value_text_x = value_left_x + (int16_t)left_arrow_slot_width;
		uint16_t value_text_width =
			value_right_x > value_text_x + value_right_padding ?
				(uint16_t)(value_right_x - value_text_x - value_right_padding) :
				0U;

		if (selected) {
			zui_draw_set_color(draw, ZUI_COLOR_BLACK);
			zui_draw_round_box(draw,
					   &(struct zui_rect){.x = 0,
							      .y = (int16_t)(item_y + 1),
							      .width = item_width,
							      .height = item_height - 2U},
					   1);
			zui_draw_set_color(draw, ZUI_COLOR_WHITE);
		} else {
			zui_draw_set_color(draw, ZUI_COLOR_BLACK);
		}

		if (selected && form->edit_mode && item->option_count > 1U &&
		    item->options != NULL) {
			zui_draw_round_rect(
				draw,
				&(struct zui_rect){.x = value_left_x - 4,
						   .y = (int16_t)(item_y + 2),
						   .width = item_width - (value_left_x - 4) - 1,
						   .height = item_height - 4U},
				1);
		}

		if (selected && item->label != NULL &&
		    zui_draw_text_width(draw, item->label) > value_left_x - 10) {
			needs_scroll = true;
			zui_form_draw_scrolled_text(draw, (struct zui_point){.x = 6, .y = text_y},
						    value_left_x - 10, item->label,
						    form->scroll_counter, false);
		} else {
			zui_component_draw_fit_text(draw, (struct zui_point){.x = 6, .y = text_y},
						 value_left_x - 10, item->label);
		}
		if (selected && form->edit_mode && item->option_count > 0U &&
		    item->option_index > 0U) {
			zui_draw_text(draw, (struct zui_point){.x = value_left_x, .y = text_y},
				      "<");
		}
		if (value != NULL && value[0] != '\0' && value_text_width > 0U) {
			if (selected && zui_draw_text_width(draw, value) > value_text_width) {
				needs_scroll = true;
				zui_form_draw_scrolled_text(
					draw, (struct zui_point){.x = value_text_x, .y = text_y},
					value_text_width, value, form->scroll_counter, false);
			} else {
				char buffer[48];
				enum zui_align horizontal =
					item->value_align == ZUI_FORM_VALUE_ALIGN_RIGHT ?
						ZUI_ALIGN_RIGHT :
						ZUI_ALIGN_CENTER;
				int16_t x =
					item->value_align == ZUI_FORM_VALUE_ALIGN_RIGHT ?
						(int16_t)(value_text_x + value_text_width) :
						(int16_t)(value_text_x + value_text_width / 2U);

				(void)snprintf(buffer, sizeof(buffer), "%s", value);
				(void)zui_draw_text_fit_width(draw, buffer, sizeof(buffer),
							      value_text_width);
				zui_draw_text_aligned(
					draw, (struct zui_point){.x = x, .y = text_y},
					horizontal, ZUI_ALIGN_BOTTOM, buffer);
			}
		}
		if (selected && form->edit_mode && item->option_count > 0U &&
		    item->option_index + 1U < item->option_count) {
			zui_draw_text(draw, (struct zui_point){.x = value_right_x, .y = text_y},
				      ">");
		}
	}

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_component_draw_scrollbar(draw, form->selected, form->item_count);
	zui_form_scroll_set_needed(form, needs_scroll);
}

static void zui_form_scroll_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct zui_form *form = CONTAINER_OF(dwork, struct zui_form, scroll_work);

	form->scroll_counter++;
	(void)zui_component_request_redraw(form->screen);
	if (form->scroll_work_running) {
		(void)k_work_reschedule(&form->scroll_work, K_MSEC(ZUI_FORM_SCROLL_INTERVAL_MS));
	}
}

static bool zui_form_input(const struct zui_input_event *event, void *user_data)
{
	struct zui_form *form = user_data;

	if (form == NULL || event == NULL) {
		return false;
	}

	bool nav_event = event->action == ZUI_INPUT_ACTION_CLICK;

	if (!nav_event && event->action != ZUI_INPUT_ACTION_LONG_PRESS) {
		return false;
	}

	if (event->code == ZUI_INPUT_CODE_BACK && form->edit_mode) {
		form->edit_mode = false;
		zui_form_reset_scroll(form);
		(void)zui_component_request_redraw(form->screen);
		return true;
	}
	if (event->code == ZUI_INPUT_CODE_UP ||
	    (event->code == ZUI_INPUT_CODE_LEFT && !form->edit_mode)) {
		(void)zui_form_move(form, ZUI_MOVE_PREVIOUS);
		return true;
	}
	if (event->code == ZUI_INPUT_CODE_DOWN ||
	    (event->code == ZUI_INPUT_CODE_RIGHT && !form->edit_mode)) {
		(void)zui_form_move(form, ZUI_MOVE_NEXT);
		return true;
	}
	if (form->edit_mode &&
	    (event->code == ZUI_INPUT_CODE_LEFT || event->code == ZUI_INPUT_CODE_RIGHT)) {
		const struct zui_form_item *item;

		if (form->item_count == 0U) {
			return true;
		}
		item = &form->items[form->selected];
		(void)zui_form_move_option(form, item->id,
					   event->code == ZUI_INPUT_CODE_LEFT ? ZUI_MOVE_PREVIOUS
									      : ZUI_MOVE_NEXT);
		zui_form_reset_scroll(form);
		return true;
	}
	if (zui_component_is_select(event) || zui_component_is_long_select(event)) {
		const struct zui_form_item *item;

		if (form->item_count == 0U) {
			return false;
		}
		item = &form->items[form->selected];
		if (event->action != ZUI_INPUT_ACTION_LONG_PRESS && form->edit_mode) {
			form->edit_mode = false;
			zui_form_reset_scroll(form);
			(void)zui_component_request_redraw(form->screen);
			return true;
		}
		if (event->action != ZUI_INPUT_ACTION_LONG_PRESS &&
		    item->option_count > 1U && item->options != NULL) {
			form->edit_mode = true;
			zui_form_reset_scroll(form);
			(void)zui_component_request_redraw(form->screen);
			return true;
		}
		return zui_form_activate(form, event) == 0;
	}

	return false;
}

static void zui_form_enter(void *user_data)
{
	struct zui_form *form = user_data;

	if (form == NULL) {
		return;
	}

	form->active = true;
	zui_form_reset_scroll(form);
}

static void zui_form_exit(void *user_data)
{
	struct zui_form *form = user_data;

	if (form == NULL) {
		return;
	}

	zui_form_scroll_stop(form);
	form->active = false;
	zui_form_reset_scroll(form);
}

static const struct zui_screen_ops zui_form_screen_ops = {
	.draw = zui_form_draw,
	.input = zui_form_input,
	.enter = zui_form_enter,
	.exit = zui_form_exit,
};

struct zui_form *zui_form_create(const struct zui_form_config *config)
{
	struct zui_form *form;

	if (config == NULL) {
		return NULL;
	}

	form = zui_calloc(1U, sizeof(*form));
	if (form == NULL) {
		return NULL;
	}

	k_work_init_delayable(&form->scroll_work, zui_form_scroll_work_handler);
	if (zui_form_update(form, config) != 0) {
		zui_form_destroy(form);
		return NULL;
	}

	form->screen = zui_screen_create(&zui_form_screen_ops, form);
	if (form->screen == NULL) {
		zui_form_destroy(form);
		return NULL;
	}

	return form;
}

void zui_form_destroy(struct zui_form *form)
{
	struct k_work_sync sync;

	if (form == NULL) {
		return;
	}

	form->scroll_work_running = false;
	(void)k_work_cancel_delayable_sync(&form->scroll_work, &sync);
	zui_screen_destroy(form->screen);
	zui_free(form->items);
	zui_free(form);
}

struct zui_screen *zui_form_get_screen(struct zui_form *form)
{
	return form == NULL ? NULL : form->screen;
}

int zui_form_update(struct zui_form *form, const struct zui_form_config *config)
{
	int rc;

	if (form == NULL || config == NULL) {
		return -EINVAL;
	}

	rc = zui_form_copy_items(form, config);
	if (rc != 0) {
		return rc;
	}

	form->config = *config;
	zui_form_reset_scroll(form);
	if (form->item_count == 0U) {
		form->selected = 0U;
		form->window_position = 0U;
	} else if (form->selected >= form->item_count) {
		form->selected = form->item_count - 1U;
	}
	zui_form_fix_window(form);

	return zui_component_request_redraw(form->screen);
}

size_t zui_form_count(const struct zui_form *form)
{
	return form == NULL ? 0U : form->item_count;
}

size_t zui_form_selected(const struct zui_form *form)
{
	return form == NULL ? 0U : form->selected;
}

int zui_form_select(struct zui_form *form, size_t index)
{
	if (form == NULL) {
		return -EINVAL;
	}
	if (index >= form->item_count) {
		return -ERANGE;
	}

	form->selected = index;
	zui_form_fix_window(form);
	zui_form_reset_scroll(form);
	return zui_component_request_redraw(form->screen);
}

int zui_form_move(struct zui_form *form, enum zui_move move)
{
	int rc;

	if (form == NULL) {
		return -EINVAL;
	}

	rc = zui_component_move_index_wrap(form->item_count, &form->selected, move);
	if (rc == 0) {
		zui_form_fix_window(form);
		zui_form_reset_scroll(form);
		(void)zui_component_request_redraw(form->screen);
	}

	return rc;
}

size_t zui_form_option(const struct zui_form *form, uint32_t id)
{
	int index = zui_form_find(form, id);

	return index < 0 ? 0U : form->items[index].option_index;
}

int zui_form_set_option(struct zui_form *form, uint32_t id, size_t option_index)
{
	int index = zui_form_find(form, id);
	struct zui_form_item *item;

	if (index < 0) {
		return index;
	}

	item = &form->items[index];
	if (option_index >= item->option_count) {
		return -ERANGE;
	}

	item->option_index = option_index;
	if (form->config.changed != NULL) {
		form->config.changed(form, id, option_index, form->config.user_data);
	}

	return zui_component_request_redraw(form->screen);
}

int zui_form_move_option(struct zui_form *form, uint32_t id, enum zui_move move)
{
	int index = zui_form_find(form, id);
	struct zui_form_item *item;
	size_t option_index;
	int rc;

	if (index < 0) {
		return index;
	}

	item = &form->items[index];
	option_index = item->option_index;
	rc = zui_component_move_index(item->option_count, &option_index, move);
	if (rc != 0) {
		return rc;
	}

	return zui_form_set_option(form, id, option_index);
}

const char *zui_form_value_text(const struct zui_form *form, uint32_t id)
{
	int index = zui_form_find(form, id);

	return index < 0 ? NULL : zui_form_item_value(&form->items[index]);
}

int zui_form_set_value_text(struct zui_form *form, uint32_t id, const char *value_text)
{
	int index = zui_form_find(form, id);

	if (index < 0) {
		return index;
	}

	form->items[index].value_text = value_text;
	return zui_component_request_redraw(form->screen);
}

void *zui_form_item_user_data(const struct zui_form *form, uint32_t id)
{
	int index = zui_form_find(form, id);

	return index < 0 ? NULL : form->items[index].user_data;
}

int zui_form_activate(struct zui_form *form, const struct zui_input_event *event)
{
	if (form == NULL) {
		return -EINVAL;
	}
	if (form->item_count == 0U) {
		return -ENOENT;
	}

	if (form->config.activated != NULL) {
		form->config.activated(form, form->items[form->selected].id, event,
				       form->config.user_data);
	}

	return 0;
}

int zui_form_set_item(struct zui_form *form, size_t index, const struct zui_form_item *item)
{
	if (form == NULL || item == NULL) {
		return -EINVAL;
	}
	if (index >= form->item_count) {
		return -ERANGE;
	}

	form->items[index] = *item;
	return zui_component_request_redraw(form->screen);
}
