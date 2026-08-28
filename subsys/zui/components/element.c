/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include "internal.h"

#define ZUI_ELEMENT_MAX_ITEMS 32U
#define ZUI_ELEMENT_MAX_TEXT_LINES 32U
#define ZUI_ELEMENT_SCROLLBAR_OFFSET 4U

enum zui_element_type {
	ZUI_ELEMENT_TYPE_STRING,
	ZUI_ELEMENT_TYPE_MULTILINE_STRING,
	ZUI_ELEMENT_TYPE_TEXT_BOX,
	ZUI_ELEMENT_TYPE_TEXT_SCROLL,
	ZUI_ELEMENT_TYPE_BUTTON,
	ZUI_ELEMENT_TYPE_ICON,
	ZUI_ELEMENT_TYPE_RECT,
	ZUI_ELEMENT_TYPE_CIRCLE,
	ZUI_ELEMENT_TYPE_LINE,
};

struct zui_element_item {
	enum zui_element_type type;
	struct zui_rect rect;
	struct zui_point a;
	struct zui_point b;
	enum zui_align horizontal;
	enum zui_align vertical;
	enum zui_font font;
	const char *text;
	const struct zui_icon *icon;
	uint16_t radius;
	bool fill;
	bool strip_to_dots;
	enum zui_element_button button;
	zui_element_button_cb callback;
	void *user_data;
	size_t scroll_line;
	size_t scroll_total;
};

struct zui_element {
	struct zui_element_item items[ZUI_ELEMENT_MAX_ITEMS];
	size_t item_count;
	struct zui_screen *screen;
};

struct zui_element_text_line {
	const char *text;
	size_t len;
	enum zui_font font;
	enum zui_align horizontal;
	struct zui_font_metrics metrics;
};

#if defined(CONFIG_ZUI_TEXT_UTF8)
static size_t zui_element_utf8_char_len(const char *text, size_t remaining)
{
	const uint8_t *bytes = (const uint8_t *)text;

	if (bytes == NULL || remaining == 0U || bytes[0] == '\0') {
		return 0U;
	}
	if ((bytes[0] & 0x80U) == 0U) {
		return 1U;
	}
	if ((bytes[0] & 0xe0U) == 0xc0U && remaining >= 2U &&
	    (bytes[1] & 0xc0U) == 0x80U) {
		return 2U;
	}
	if ((bytes[0] & 0xf0U) == 0xe0U && remaining >= 3U &&
	    (bytes[1] & 0xc0U) == 0x80U &&
	    (bytes[2] & 0xc0U) == 0x80U) {
		return 3U;
	}
	if ((bytes[0] & 0xf8U) == 0xf0U && remaining >= 4U &&
	    (bytes[1] & 0xc0U) == 0x80U &&
	    (bytes[2] & 0xc0U) == 0x80U && (bytes[3] & 0xc0U) == 0x80U) {
		return 4U;
	}

	return 1U;
}

static uint16_t zui_element_utf8_glyph_width(struct zui_draw_ctx *draw, const char *text,
					     size_t len)
{
	char glyph[5];

	len = MIN(len, sizeof(glyph) - 1U);
	memcpy(glyph, text, len);
	glyph[len] = '\0';
	return zui_draw_text_width(draw, glyph);
}
#endif

static int zui_element_add_item(struct zui_element *element,
				const struct zui_element_item *item)
{
	if (element == NULL || item == NULL) {
		return -EINVAL;
	}
	if (element->item_count >= ARRAY_SIZE(element->items)) {
		return -ENOMEM;
	}

	element->items[element->item_count++] = *item;
	return zui_component_request_redraw(element->screen);
}

static const char *zui_element_apply_line_controls(const char *text, enum zui_font *font,
							  enum zui_align *horizontal)
{
	if (text == NULL || font == NULL || horizontal == NULL) {
		return text;
	}

	while (text[0] == '\e' && text[1] != '\0') {
		switch (text[1]) {
		case 'c':
			*horizontal = ZUI_ALIGN_CENTER;
			break;
		case 'r':
			*horizontal = ZUI_ALIGN_RIGHT;
			break;
		case '#':
			*font = ZUI_FONT_PRIMARY;
			break;
		case '*':
			*font = ZUI_FONT_KEYBOARD;
			break;
		default:
			return text;
		}
		text += 2;
	}

	return text;
}

static size_t zui_element_text_lines_visible(const struct zui_element_text_line *lines,
						    size_t line_count, size_t first,
						    uint16_t height)
{
	uint16_t used = 0U;
	size_t visible = 0U;

	for (size_t i = first; i < line_count; i++) {
		uint16_t leading = MAX(lines[i].metrics.leading_default, lines[i].metrics.height);

		if (visible > 0U && used + leading > height) {
			break;
		}
		used += leading;
		visible++;
		if (used >= height) {
			break;
		}
	}

	return visible;
}

static void zui_element_add_text_line(struct zui_element_text_line *lines,
					     size_t *line_count, size_t max_lines,
					     const char *text, size_t len, enum zui_font font,
					     enum zui_align horizontal,
					     const struct zui_font_metrics *metrics)
{
	if (lines == NULL || line_count == NULL || *line_count >= max_lines) {
		return;
	}

	lines[*line_count] = (struct zui_element_text_line){
		.text = text,
		.len = len,
		.font = font,
		.horizontal = horizontal,
		.metrics = metrics != NULL ? *metrics : (struct zui_font_metrics){0},
	};
	(*line_count)++;
}

static size_t zui_element_collect_lines(struct zui_draw_ctx *draw,
					       const struct zui_element_item *item,
					       struct zui_element_text_line *lines,
					       size_t max_lines, uint16_t width,
					       bool line_controls)
{
	const char *cursor = item->text != NULL ? item->text : "";
	size_t line_count = 0U;

	while (cursor[0] != '\0' && line_count < max_lines) {
		enum zui_font font = item->font;
		enum zui_align horizontal = item->horizontal;
		struct zui_font_metrics metrics = {0};
		const char *start;
#if defined(CONFIG_ZUI_TEXT_UTF8)
		const char *end;
#endif
		size_t len = 0U;
		uint16_t line_width = 0U;

		if (line_controls) {
			font = ZUI_FONT_SECONDARY;
			horizontal = ZUI_ALIGN_LEFT;
			cursor = zui_element_apply_line_controls(cursor, &font, &horizontal);
		}
		(void)zui_draw_font_metrics(draw, font, &metrics);
		zui_draw_set_font(draw, font);
		start = cursor;
#if defined(CONFIG_ZUI_TEXT_UTF8)
		end = cursor + strlen(cursor);
#endif

		if (cursor[0] == '\n') {
			zui_element_add_text_line(lines, &line_count, max_lines, cursor, 0U,
							 font, horizontal, &metrics);
			cursor++;
			continue;
		}

		while (cursor[len] != '\0' && cursor[len] != '\n') {
#if defined(CONFIG_ZUI_TEXT_UTF8)
			size_t glyph_len =
				zui_element_utf8_char_len(cursor + len,
							  (size_t)(end - (cursor + len)));
			uint16_t glyph_width =
				zui_element_utf8_glyph_width(draw, cursor + len, glyph_len);
#else
			size_t glyph_len = 1U;
			uint16_t glyph_width = zui_draw_glyph_width(draw, (uint8_t)cursor[len]);
#endif

			if (len > 0U && line_width + glyph_width > width) {
				break;
			}
			line_width += glyph_width;
			len += glyph_len;
			if (line_width >= width) {
				break;
			}
		}

		if (len == 0U && cursor[0] != '\0' && cursor[0] != '\n') {
#if defined(CONFIG_ZUI_TEXT_UTF8)
			len = zui_element_utf8_char_len(cursor, (size_t)(end - cursor));
#else
			len = 1U;
#endif
		}
		zui_element_add_text_line(lines, &line_count, max_lines, start, len, font,
						 horizontal, &metrics);
		cursor += len;
		if (cursor[0] == '\n') {
			cursor++;
		}
	}

	return line_count;
}

static void zui_element_draw_text_line(struct zui_draw_ctx *draw,
					      const struct zui_element_text_line *line,
					      struct zui_point pos, uint16_t width, bool strip)
{
	char buffer[64];
	size_t len;

	if (draw == NULL || line == NULL || line->text == NULL) {
		return;
	}

	len = MIN(line->len, sizeof(buffer) - 1U);
	memcpy(buffer, line->text, len);
	buffer[len] = '\0';
	if (strip) {
		(void)zui_draw_text_fit_width(draw, buffer, sizeof(buffer), width);
	}
	zui_draw_set_font(draw, line->font);
	zui_draw_text_aligned(draw, pos, line->horizontal, ZUI_ALIGN_TOP, buffer);
}

static void zui_element_draw_dotted_scrollbar(struct zui_draw_ctx *draw,
						     const struct zui_rect *rect,
						     size_t position, size_t total)
{
	uint16_t block_h;
	uint16_t block_y;

	if (draw == NULL || rect == NULL || rect->height == 0U) {
		return;
	}

	zui_draw_set_color(draw, ZUI_COLOR_WHITE);
	zui_draw_box(draw, rect);
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	for (uint16_t y = 0U; y < rect->height; y += 2U) {
		zui_draw_dot(draw, (struct zui_point){.x = rect->x + 1, .y = rect->y + y});
	}
	if (total == 0U) {
		return;
	}

	block_h = MAX((uint16_t)(rect->height / total), 1U);
	block_y = (uint16_t)(((uint32_t)rect->height * MIN(position, total - 1U)) / total);
	zui_draw_box(draw, &(struct zui_rect){.x = rect->x,
					      .y = rect->y + (int16_t)block_y,
					      .width = rect->width,
					      .height = block_h});
}

static void zui_element_draw_text_box(struct zui_draw_ctx *draw,
					     const struct zui_element_item *item)
{
	struct zui_element_text_line lines[ZUI_ELEMENT_MAX_TEXT_LINES];
	size_t line_count;
	size_t visible_count;
	uint16_t y;

	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	line_count = zui_element_collect_lines(draw, item, lines, ARRAY_SIZE(lines),
						      item->rect.width, false);
	if (line_count == 0U) {
		return;
	}

	visible_count = zui_element_text_lines_visible(lines, line_count, 0U,
							      item->rect.height);
	if (visible_count == 0U) {
		return;
	}

	y = item->rect.y;
	if (item->vertical == ZUI_ALIGN_CENTER) {
		uint16_t used = 0U;

		for (size_t i = 0U; i < visible_count; i++) {
			used += MAX(lines[i].metrics.leading_default, lines[i].metrics.height);
		}
		if (used < item->rect.height) {
			y += (item->rect.height - used) / 2U;
		}
	} else if (item->vertical == ZUI_ALIGN_BOTTOM) {
		uint16_t used = 0U;

		for (size_t i = 0U; i < visible_count; i++) {
			used += MAX(lines[i].metrics.leading_default, lines[i].metrics.height);
		}
		if (used < item->rect.height) {
			y += item->rect.height - used;
		}
	}

	zui_draw_set_clip(draw, &item->rect);
	for (size_t i = 0U; i < visible_count; i++) {
		int16_t x = item->rect.x;

		if (lines[i].horizontal == ZUI_ALIGN_CENTER) {
			x += item->rect.width / 2U;
		} else if (lines[i].horizontal == ZUI_ALIGN_RIGHT) {
			x += item->rect.width;
		}
		zui_element_draw_text_line(
			draw, &lines[i], (struct zui_point){.x = x, .y = (int16_t)y},
			item->rect.width, item->strip_to_dots && i + 1U == visible_count);
		y += MAX(lines[i].metrics.leading_default, lines[i].metrics.height);
	}
	zui_draw_clear_clip(draw);
}

static void zui_element_draw_text_scroll(struct zui_draw_ctx *draw,
						struct zui_element_item *item)
{
	struct zui_element_text_line lines[ZUI_ELEMENT_MAX_TEXT_LINES];
	struct zui_rect text_rect = item->rect;
	size_t line_count;
	size_t visible_count;
	uint16_t y;

	if (text_rect.width > ZUI_ELEMENT_SCROLLBAR_OFFSET) {
		text_rect.width -= ZUI_ELEMENT_SCROLLBAR_OFFSET;
	}

	line_count = zui_element_collect_lines(draw, item, lines, ARRAY_SIZE(lines),
						      text_rect.width, true);
	visible_count = zui_element_text_lines_visible(lines, line_count, item->scroll_line,
							      text_rect.height);
	item->scroll_total = line_count > visible_count ? line_count - visible_count + 1U : 1U;
	if (item->scroll_line >= item->scroll_total) {
		item->scroll_line = item->scroll_total - 1U;
	}

	y = text_rect.y;
	zui_draw_set_clip(draw, &text_rect);
	for (size_t i = item->scroll_line; i < line_count && i < item->scroll_line + visible_count;
	     i++) {
		int16_t x = text_rect.x;

		if (lines[i].horizontal == ZUI_ALIGN_CENTER) {
			x += text_rect.width / 2U;
		} else if (lines[i].horizontal == ZUI_ALIGN_RIGHT) {
			x += text_rect.width;
		}
		zui_element_draw_text_line(draw, &lines[i],
						  (struct zui_point){.x = x, .y = (int16_t)y},
						  text_rect.width, false);
		y += MAX(lines[i].metrics.leading_default, lines[i].metrics.height);
	}
	zui_draw_clear_clip(draw);

	if (item->scroll_total > 1U) {
		zui_element_draw_dotted_scrollbar(
			draw,
			&(struct zui_rect){.x = text_rect.x + (int16_t)text_rect.width + 1,
					   .y = text_rect.y,
					   .width = 3,
					   .height = text_rect.height},
			item->scroll_line, item->scroll_total);
	}
}

static void zui_element_draw_button(struct zui_draw_ctx *draw,
					   const struct zui_element_item *item)
{
	switch (item->button) {
	case ZUI_ELEMENT_BUTTON_LEFT:
		zui_component_draw_button_left(draw, item->text);
		break;
	case ZUI_ELEMENT_BUTTON_CENTER:
		zui_component_draw_button_center(draw, item->text);
		break;
	case ZUI_ELEMENT_BUTTON_RIGHT:
		zui_component_draw_button_right(draw, item->text);
		break;
	default:
		break;
	}
}

static void zui_element_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct zui_element *element = user_data;

	zui_draw_reset(draw);
	if (element == NULL) {
		return;
	}

	for (size_t i = 0U; i < element->item_count; i++) {
		struct zui_element_item *item = &element->items[i];

		switch (item->type) {
		case ZUI_ELEMENT_TYPE_STRING:
			zui_draw_set_font(draw, item->font);
			zui_draw_text_aligned(draw, item->a, item->horizontal, item->vertical,
					      item->text != NULL ? item->text : "");
			break;
		case ZUI_ELEMENT_TYPE_MULTILINE_STRING:
			zui_draw_set_font(draw, item->font);
			zui_draw_multiline_text(draw, &item->rect, item->horizontal, item->vertical,
						item->text != NULL ? item->text : "");
			break;
		case ZUI_ELEMENT_TYPE_TEXT_BOX:
			zui_element_draw_text_box(draw, item);
			break;
		case ZUI_ELEMENT_TYPE_TEXT_SCROLL:
			zui_element_draw_text_scroll(draw, item);
			break;
		case ZUI_ELEMENT_TYPE_BUTTON:
			zui_element_draw_button(draw, item);
			break;
		case ZUI_ELEMENT_TYPE_ICON:
			zui_draw_icon(draw, item->a, item->icon);
			break;
		case ZUI_ELEMENT_TYPE_RECT:
			if (item->fill) {
				if (item->radius > 0U) {
					zui_draw_round_box(draw, &item->rect, item->radius);
				} else {
					zui_draw_box(draw, &item->rect);
				}
			} else if (item->radius > 0U) {
				zui_draw_round_rect(draw, &item->rect, item->radius);
			} else {
				zui_draw_rect(draw, &item->rect);
			}
			break;
		case ZUI_ELEMENT_TYPE_CIRCLE:
			if (item->fill) {
				zui_draw_disc(draw, item->a, item->radius);
			} else {
				zui_draw_circle(draw, item->a, item->radius);
			}
			break;
		case ZUI_ELEMENT_TYPE_LINE:
			zui_draw_line(draw, item->a, item->b);
			break;
		}
	}
}

static bool zui_element_input(const struct zui_input_event *event, void *user_data)
{
	struct zui_element *element = user_data;
	enum zui_element_button button;
	bool matched = false;
	bool consumed = false;

	if (element == NULL || event == NULL) {
		return false;
	}

	if (event->action == ZUI_INPUT_ACTION_CLICK) {
		for (size_t i = 0U; i < element->item_count; i++) {
			struct zui_element_item *item = &element->items[i];

			if (item->type != ZUI_ELEMENT_TYPE_TEXT_SCROLL) {
				continue;
			}
			if (event->code == ZUI_INPUT_CODE_UP) {
				if (item->scroll_line > 0U) {
					item->scroll_line--;
				}
				consumed = true;
			} else if (event->code == ZUI_INPUT_CODE_DOWN) {
				if (item->scroll_line + 1U < MAX(item->scroll_total, 1U)) {
					item->scroll_line++;
				}
				consumed = true;
			}
		}
		if (consumed) {
			(void)zui_component_request_redraw(element->screen);
			return true;
		}
	}

	if (event->action != ZUI_INPUT_ACTION_CLICK &&
	    event->action != ZUI_INPUT_ACTION_LONG_PRESS) {
		return false;
	}

	switch (event->code) {
	case ZUI_INPUT_CODE_LEFT:
	case ZUI_INPUT_CODE_BACK:
		button = ZUI_ELEMENT_BUTTON_LEFT;
		matched = true;
		break;
	case ZUI_INPUT_CODE_SELECT:
		button = ZUI_ELEMENT_BUTTON_CENTER;
		matched = true;
		break;
	case ZUI_INPUT_CODE_RIGHT:
		button = ZUI_ELEMENT_BUTTON_RIGHT;
		matched = true;
		break;
	default:
		return false;
	}

	if (!matched) {
		return consumed;
	}

	for (size_t i = 0U; i < element->item_count; i++) {
		struct zui_element_item *item = &element->items[i];

		if (item->type == ZUI_ELEMENT_TYPE_BUTTON && item->button == button &&
		    item->callback != NULL) {
			item->callback(button, element, event, item->user_data);
			return true;
		}
	}

	return consumed;
}

static const struct zui_screen_ops zui_element_screen_ops = {
	.draw = zui_element_draw,
	.input = zui_element_input,
};

struct zui_element *zui_element_create(void)
{
	struct zui_element *element = zui_calloc(1U, sizeof(*element));

	if (element == NULL) {
		return NULL;
	}

	element->screen = zui_screen_create(&zui_element_screen_ops, element);
	if (element->screen == NULL) {
		zui_element_destroy(element);
		return NULL;
	}

	return element;
}

void zui_element_destroy(struct zui_element *element)
{
	if (element == NULL) {
		return;
	}

	zui_screen_destroy(element->screen);
	zui_free(element);
}

struct zui_screen *zui_element_get_screen(struct zui_element *element)
{
	return element == NULL ? NULL : element->screen;
}

int zui_element_reset(struct zui_element *element)
{
	if (element == NULL) {
		return -EINVAL;
	}

	element->item_count = 0U;
	return zui_component_request_redraw(element->screen);
}

int zui_element_add_string(struct zui_element *element, struct zui_point pos,
				  enum zui_align horizontal, enum zui_align vertical,
				  enum zui_font font, const char *text)
{
	return zui_element_add_item(
		element, &(struct zui_element_item){.type = ZUI_ELEMENT_TYPE_STRING,
							  .a = pos,
							  .horizontal = horizontal,
							  .vertical = vertical,
							  .font = font,
							  .text = text});
}

int zui_element_add_multiline_string(struct zui_element *element,
					    const struct zui_rect *rect, enum zui_align horizontal,
					    enum zui_align vertical, enum zui_font font,
					    const char *text)
{
	return rect == NULL
		       ? -EINVAL
		       : zui_element_add_item(
				 element, &(struct zui_element_item){
						 .type = ZUI_ELEMENT_TYPE_MULTILINE_STRING,
						 .rect = *rect,
						 .horizontal = horizontal,
						 .vertical = vertical,
						 .font = font,
						 .text = text});
}

int zui_element_add_text_box(struct zui_element *element, const struct zui_rect *rect,
				    enum zui_align horizontal, enum zui_align vertical,
				    const char *text, bool strip_to_dots)
{
	return rect == NULL ? -EINVAL
			    : zui_element_add_item(
				      element, &(struct zui_element_item){
						      .type = ZUI_ELEMENT_TYPE_TEXT_BOX,
						      .rect = *rect,
						      .horizontal = horizontal,
						      .vertical = vertical,
						      .text = text,
						      .strip_to_dots = strip_to_dots});
}

int zui_element_add_text_scroll(struct zui_element *element,
				       const struct zui_rect *rect, enum zui_font font,
				       const char *text)
{
	return rect == NULL ? -EINVAL
			    : zui_element_add_item(
				      element, &(struct zui_element_item){
						      .type = ZUI_ELEMENT_TYPE_TEXT_SCROLL,
						      .rect = *rect,
						      .font = font,
						      .text = text});
}

int zui_element_add_button(struct zui_element *element,
				  enum zui_element_button button, const char *text,
				  zui_element_button_cb callback, void *user_data)
{
	return zui_element_add_item(
		element, &(struct zui_element_item){.type = ZUI_ELEMENT_TYPE_BUTTON,
							  .button = button,
							  .text = text,
							  .callback = callback,
							  .user_data = user_data});
}

int zui_element_add_icon(struct zui_element *element, struct zui_point pos,
				const struct zui_icon *icon)
{
	return zui_element_add_item(
		element, &(struct zui_element_item){
				.type = ZUI_ELEMENT_TYPE_ICON, .a = pos, .icon = icon});
}

int zui_element_add_rect(struct zui_element *element, const struct zui_rect *rect,
				uint16_t radius, bool fill)
{
	return rect == NULL ? -EINVAL
			    : zui_element_add_item(
				      element, &(struct zui_element_item){
						      .type = ZUI_ELEMENT_TYPE_RECT,
						      .rect = *rect,
						      .radius = radius,
						      .fill = fill});
}

int zui_element_add_circle(struct zui_element *element, struct zui_point center,
				  uint16_t radius, bool fill)
{
	return zui_element_add_item(
		element, &(struct zui_element_item){.type = ZUI_ELEMENT_TYPE_CIRCLE,
							  .a = center,
							  .radius = radius,
							  .fill = fill});
}

int zui_element_add_line(struct zui_element *element, struct zui_point start,
				struct zui_point end)
{
	return zui_element_add_item(
		element, &(struct zui_element_item){
				.type = ZUI_ELEMENT_TYPE_LINE, .a = start, .b = end});
}
