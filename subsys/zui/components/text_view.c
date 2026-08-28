/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include "internal.h"

#include <ctype.h>
#include <stdlib.h>

struct zui_text_view {
	struct zui_text_view_config config;
	size_t scroll;
	size_t max_scroll;
	bool focus_end_pending;
	struct zui_screen *screen;
};

#define ZUI_TEXT_VIEW_TITLE_X                       2
#define ZUI_TEXT_VIEW_TITLE_Y                       10
#define ZUI_TEXT_VIEW_FRAME_X                       0
#define ZUI_TEXT_VIEW_FRAME_Y                       13
#define ZUI_TEXT_VIEW_FRAME_W                       128U
#define ZUI_TEXT_VIEW_FRAME_H                       64U
#define ZUI_TEXT_VIEW_CONTENT_X                     2
#define ZUI_TEXT_VIEW_CONTENT_Y                     16
#define ZUI_TEXT_VIEW_TEXT_WIDTH                    122U
#define ZUI_TEXT_VIEW_TEXT_HEIGHT                   48U
#define ZUI_TEXT_VIEW_SCROLLBAR_X                   126
#define ZUI_TEXT_VIEW_MAX_LINE_TEXT                 64U

static enum zui_font zui_text_view_font(const struct zui_text_view *view)
{
	if (view != NULL && view->config.mode == ZUI_TEXT_VIEW_MODE_HEX) {
		return ZUI_FONT_KEYBOARD;
	}

	return view == NULL ? ZUI_FONT_SECONDARY : view->config.font;
}

static uint16_t zui_text_view_line_height(struct zui_draw_ctx *draw, enum zui_font font,
					  uint8_t line_spacing)
{
	uint16_t line_height = MAX(zui_draw_font_height(draw), 1U);

	if (font == ZUI_FONT_SECONDARY) {
		line_height++;
	}

	return line_height + line_spacing;
}

#if defined(CONFIG_ZUI_TEXT_UTF8)
static size_t zui_text_view_utf8_char_len(const char *text, size_t remaining)
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

static uint16_t zui_text_view_utf8_glyph_width(struct zui_draw_ctx *draw, const char *text,
					       size_t len)
{
	char glyph[5];

	len = MIN(len, sizeof(glyph) - 1U);
	memcpy(glyph, text, len);
	glyph[len] = '\0';
	return zui_draw_text_width(draw, glyph);
}
#endif

static const char *zui_text_view_next_line(struct zui_draw_ctx *draw, const char *text)
{
	const char *cursor = text;
#if defined(CONFIG_ZUI_TEXT_UTF8)
	const char *end;
#endif
	uint16_t line_width = 0U;

	if (text == NULL || text[0] == '\0') {
		return text;
	}

#if defined(CONFIG_ZUI_TEXT_UTF8)
	end = text + strlen(text);
#endif
	while (cursor[0] != '\0') {
		uint16_t glyph_width;
		size_t glyph_len;

		if (cursor[0] == '\n') {
			return cursor + 1;
		}

#if defined(CONFIG_ZUI_TEXT_UTF8)
		glyph_len = zui_text_view_utf8_char_len(cursor, (size_t)(end - cursor));
		glyph_width = zui_text_view_utf8_glyph_width(draw, cursor, glyph_len);
#else
		glyph_len = 1U;
		glyph_width = zui_draw_glyph_width(draw, (uint8_t)cursor[0]);
#endif
		if (line_width + glyph_width > ZUI_TEXT_VIEW_TEXT_WIDTH) {
			return cursor == text ? cursor + glyph_len : cursor;
		}

		line_width += glyph_width;
		cursor += glyph_len;
	}

	return cursor;
}

static size_t zui_text_view_visual_line_count(struct zui_draw_ctx *draw, const char *text)
{
	const char *cursor = text;
	size_t count = 0U;

	if (text == NULL || text[0] == '\0') {
		return 0U;
	}

	while (cursor[0] != '\0') {
		const char *next = zui_text_view_next_line(draw, cursor);

		count++;
		if (next <= cursor) {
			break;
		}
		cursor = next;
	}

	return count;
}

static const char *zui_text_view_line_at(struct zui_draw_ctx *draw, const char *text,
					 size_t line)
{
	const char *cursor = text;

	if (text == NULL) {
		return NULL;
	}

	for (size_t i = 0U; i < line && cursor[0] != '\0'; i++) {
		const char *next = zui_text_view_next_line(draw, cursor);

		if (next <= cursor) {
			break;
		}
		cursor = next;
	}

	return cursor;
}

static void zui_text_view_copy_line(struct zui_draw_ctx *draw, const char *start, char *line,
				    size_t line_size)
{
	const char *end;
	size_t len;

	if (line == NULL || line_size == 0U) {
		return;
	}

	line[0] = '\0';
	if (start == NULL || start[0] == '\0') {
		return;
	}

	end = zui_text_view_next_line(draw, start);
	len = (size_t)(end - start);
	if (len > 0U && start[len - 1U] == '\n') {
		len--;
	}
	len = MIN(len, line_size - 1U);
	memcpy(line, start, len);
	line[len] = '\0';
}

static void zui_text_view_update_scroll_bounds(struct zui_text_view *view, struct zui_draw_ctx *draw,
					       size_t lines_on_screen)
{
	size_t line_count;
	size_t scroll_total;

	if (view == NULL) {
		return;
	}

	line_count = zui_text_view_visual_line_count(draw, view->config.text);
	scroll_total = line_count + 1U > lines_on_screen ? line_count + 1U - lines_on_screen : 0U;
	view->max_scroll = scroll_total > 0U ? scroll_total - 1U : 0U;

	if (view->focus_end_pending) {
		view->scroll = view->max_scroll;
		view->focus_end_pending = false;
	} else if (view->scroll > view->max_scroll) {
		view->scroll = view->max_scroll;
	}
}

static void zui_text_view_draw_scrollbar(struct zui_draw_ctx *draw, size_t pos, size_t total)
{
	const uint16_t height = ZUI_TEXT_VIEW_TEXT_HEIGHT;
	uint16_t block_y;
	uint16_t block_h;

	if (draw == NULL || total == 0U) {
		return;
	}

	zui_draw_set_color(draw, ZUI_COLOR_WHITE);
	zui_draw_box(draw,
		     &(struct zui_rect){
			     .x = ZUI_TEXT_VIEW_SCROLLBAR_X - 1,
			     .y = ZUI_TEXT_VIEW_CONTENT_Y,
			     .width = 3,
			     .height = height,
		     });
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	for (uint16_t y = 0U; y < height; y += 2U) {
		zui_draw_dot(draw, (struct zui_point){
					   .x = ZUI_TEXT_VIEW_SCROLLBAR_X,
					   .y = (int16_t)(ZUI_TEXT_VIEW_CONTENT_Y + y),
				   });
	}

	block_y = (uint16_t)(((uint32_t)height * MIN(pos, total - 1U)) / total);
	block_h = MAX((uint16_t)(height / total), 1U);
	zui_draw_box(draw,
		     &(struct zui_rect){
			     .x = ZUI_TEXT_VIEW_SCROLLBAR_X - 1,
			     .y = (int16_t)(ZUI_TEXT_VIEW_CONTENT_Y + block_y),
			     .width = 3,
			     .height = block_h,
		     });
}

static void zui_text_view_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct zui_text_view *view = user_data;
	enum zui_font font;
	uint16_t line_height;
	size_t lines_on_screen;
	const char *line_start;
	size_t scrollbar_total;

	zui_draw_reset(draw);
	if (view == NULL) {
		return;
	}

	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	zui_draw_text(draw, (struct zui_point){.x = ZUI_TEXT_VIEW_TITLE_X,
					       .y = ZUI_TEXT_VIEW_TITLE_Y},
		      view->config.title != NULL ? view->config.title : "");

	font = zui_text_view_font(view);
	zui_draw_set_font(draw, font);
	line_height = zui_text_view_line_height(
		draw, font, zui_component_text_line_spacing(view->config.text_line_spacing));
	lines_on_screen = MAX(ZUI_TEXT_VIEW_TEXT_HEIGHT / line_height, 1U);
	zui_text_view_update_scroll_bounds(view, draw, lines_on_screen);

	zui_draw_round_rect(draw,
			    &(struct zui_rect){.x = ZUI_TEXT_VIEW_FRAME_X,
					       .y = ZUI_TEXT_VIEW_FRAME_Y,
					       .width = ZUI_TEXT_VIEW_FRAME_W,
					       .height = ZUI_TEXT_VIEW_FRAME_H},
			    5U);
	line_start = zui_text_view_line_at(draw, view->config.text, view->scroll);
	for (size_t line_index = 0U;
	     line_start != NULL && line_start[0] != '\0' && line_index < lines_on_screen;
	     line_index++) {
		char line[ZUI_TEXT_VIEW_MAX_LINE_TEXT];
		const char *next = zui_text_view_next_line(draw, line_start);

		zui_text_view_copy_line(draw, line_start, line, sizeof(line));
		zui_draw_text(draw,
			      (struct zui_point){
				      .x = ZUI_TEXT_VIEW_CONTENT_X,
				      .y = (int16_t)(ZUI_TEXT_VIEW_CONTENT_Y +
						     zui_draw_font_height(draw) +
						     line_index * line_height),
			      },
			      line);
		if (next <= line_start) {
			break;
		}
		line_start = next;
	}

	scrollbar_total = view->max_scroll > 0U ? view->max_scroll + 1U : 0U;
	zui_text_view_draw_scrollbar(draw, view->scroll, scrollbar_total);
}

static bool zui_text_view_input(const struct zui_input_event *event, void *user_data)
{
	struct zui_text_view *view = user_data;

	if (event == NULL) {
		return false;
	}
	if (event->action != ZUI_INPUT_ACTION_CLICK && event->action != ZUI_INPUT_ACTION_RELEASE) {
		return false;
	}
	if (event->action == ZUI_INPUT_ACTION_RELEASE) {
		return true;
	}
	if (view == NULL) {
		return false;
	}

	if (event->code == ZUI_INPUT_CODE_UP || event->code == ZUI_INPUT_CODE_LEFT) {
		return zui_text_view_scroll_by(view, -1) == 0;
	}
	if (event->code == ZUI_INPUT_CODE_DOWN || event->code == ZUI_INPUT_CODE_RIGHT) {
		return zui_text_view_scroll_by(view, 1) == 0;
	}

	return false;
}

static const struct zui_screen_ops zui_text_view_screen_ops = {
	.draw = zui_text_view_draw,
	.input = zui_text_view_input,
};

struct zui_text_view *zui_text_view_create(const struct zui_text_view_config *config)
{
	struct zui_text_view *view;

	if (config == NULL) {
		return NULL;
	}

	view = zui_calloc(1U, sizeof(*view));
	if (view == NULL) {
		return NULL;
	}

	if (zui_text_view_update(view, config) != 0) {
		zui_text_view_destroy(view);
		return NULL;
	}

	view->screen = zui_screen_create(&zui_text_view_screen_ops, view);
	if (view->screen == NULL) {
		zui_text_view_destroy(view);
		return NULL;
	}

	return view;
}

void zui_text_view_destroy(struct zui_text_view *view)
{
	if (view == NULL) {
		return;
	}

	zui_screen_destroy(view->screen);
	zui_free(view);
}

struct zui_screen *zui_text_view_get_screen(struct zui_text_view *view)
{
	return view == NULL ? NULL : view->screen;
}

int zui_text_view_update(struct zui_text_view *view, const struct zui_text_view_config *config)
{
	if (view == NULL || config == NULL) {
		return -EINVAL;
	}

	view->config = *config;
	view->max_scroll = zui_component_line_count(config->text);
	view->focus_end_pending = config->focus_end;
	view->scroll = config->focus_end ? view->max_scroll : 0U;
	return zui_component_request_redraw(view->screen);
}

size_t zui_text_view_scroll(const struct zui_text_view *view)
{
	return view == NULL ? 0U : view->scroll;
}

int zui_text_view_set_scroll(struct zui_text_view *view, size_t line)
{
	size_t max_line;

	if (view == NULL) {
		return -EINVAL;
	}

	max_line = view->max_scroll > 0U ? view->max_scroll : zui_component_line_count(view->config.text);
	view->scroll = MIN(line, max_line);
	return zui_component_request_redraw(view->screen);
}

int zui_text_view_scroll_by(struct zui_text_view *view, int32_t lines)
{
	if (view == NULL) {
		return -EINVAL;
	}
	if (lines < 0 && view->scroll < (size_t)-lines) {
		return zui_text_view_set_scroll(view, 0U);
	}

	return zui_text_view_set_scroll(view, view->scroll + lines);
}
