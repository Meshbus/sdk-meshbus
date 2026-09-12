/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_ZUI_COMPONENTS_INTERNAL_H_
#define ZEPHYR_SUBSYS_ZUI_COMPONENTS_INTERNAL_H_

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/sys/util.h>
#include <zui/draw.h>
#include <zui/screen.h>
#include <zui/components.h>

#include "../zui_mem.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZUI_COMPONENT_FILE_PICKER_MAX_ENTRIES CONFIG_ZUI_FILE_PICKER_MAX_ENTRIES
#define ZUI_COMPONENT_FILE_PICKER_PATH_SIZE   CONFIG_ZUI_FILE_PICKER_PATH_SIZE
#define ZUI_COMPONENT_FILE_PICKER_NAME_SIZE   CONFIG_ZUI_FILE_PICKER_NAME_SIZE
#define ZUI_COMPONENT_FILE_PICKER_VISIBLE_ITEMS CONFIG_ZUI_FILE_PICKER_VISIBLE_ITEMS

static inline bool zui_component_is_select(const struct zui_input_event *event)
{
	return event != NULL && event->code == ZUI_INPUT_CODE_SELECT &&
	       event->action == ZUI_INPUT_ACTION_CLICK;
}

static inline bool zui_component_is_long_select(const struct zui_input_event *event)
{
	return event != NULL && event->code == ZUI_INPUT_CODE_SELECT &&
	       event->action == ZUI_INPUT_ACTION_LONG_PRESS;
}

static inline int zui_component_request_redraw(struct zui_screen *screen)
{
	return screen == NULL ? 0 : zui_screen_request_redraw(screen);
}

static inline int zui_component_move_index(size_t count, size_t *index, enum zui_move move)
{
	if (index == NULL) {
		return -EINVAL;
	}
	if (count == 0U) {
		*index = 0U;
		return -ENOENT;
	}

	switch (move) {
	case ZUI_MOVE_PREVIOUS:
		if (*index > 0U) {
			(*index)--;
		}
		break;
	case ZUI_MOVE_NEXT:
		if (*index + 1U < count) {
			(*index)++;
		}
		break;
	case ZUI_MOVE_FIRST:
		*index = 0U;
		break;
	case ZUI_MOVE_LAST:
		*index = count - 1U;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static inline int zui_component_move_index_wrap(size_t count, size_t *index, enum zui_move move)
{
	if (index == NULL) {
		return -EINVAL;
	}
	if (count == 0U) {
		*index = 0U;
		return -ENOENT;
	}

	switch (move) {
	case ZUI_MOVE_PREVIOUS:
		*index = *index > 0U ? *index - 1U : count - 1U;
		break;
	case ZUI_MOVE_NEXT:
		*index = *index + 1U < count ? *index + 1U : 0U;
		break;
	case ZUI_MOVE_FIRST:
		*index = 0U;
		break;
	case ZUI_MOVE_LAST:
		*index = count - 1U;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static inline int zui_component_input_move(const struct zui_input_event *event, enum zui_move *move)
{
	if (event == NULL || move == NULL) {
		return -EINVAL;
	}

	if (event->action != ZUI_INPUT_ACTION_CLICK) {
		return -ENOTSUP;
	}

	switch (event->code) {
	case ZUI_INPUT_CODE_UP:
	case ZUI_INPUT_CODE_LEFT:
		*move = ZUI_MOVE_PREVIOUS;
		return 0;
	case ZUI_INPUT_CODE_DOWN:
	case ZUI_INPUT_CODE_RIGHT:
		*move = ZUI_MOVE_NEXT;
		return 0;
	case ZUI_INPUT_CODE_HOME:
		*move = ZUI_MOVE_FIRST;
		return 0;
	case ZUI_INPUT_CODE_MENU:
		*move = ZUI_MOVE_LAST;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static inline void zui_component_draw_title(struct zui_draw_ctx *draw, const char *title)
{
	if (draw == NULL) {
		return;
	}

	zui_draw_reset(draw);
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	if (title != NULL) {
		zui_draw_text(draw, (struct zui_point){.x = 2, .y = 8}, title);
	}
}

static inline void zui_component_draw_row(struct zui_draw_ctx *draw, size_t row, bool selected,
				       const char *label, const char *detail,
				       const struct zui_icon *icon)
{
	struct zui_point pos = {
		.x = 3,
		.y = (int16_t)(20 + row * 12U),
	};

	if (draw == NULL || row >= 4U) {
		return;
	}

	if (selected) {
		zui_draw_box(draw, &(struct zui_rect){.x = 0,
						      .y = (int16_t)(pos.y - 9),
						      .width = zui_draw_width(draw),
						      .height = 11});
		zui_draw_set_color(draw, ZUI_COLOR_WHITE);
	}

	if (icon != NULL) {
		zui_draw_icon(draw, (struct zui_point){.x = 3, .y = (int16_t)(pos.y - 8)}, icon);
		pos.x = 16;
	}
	zui_draw_text(draw, pos, label != NULL ? label : "");

	if (detail != NULL) {
		zui_draw_text_aligned(draw, (struct zui_point){.x = 126, .y = pos.y},
				      ZUI_ALIGN_RIGHT, ZUI_ALIGN_BOTTOM, detail);
	}

	if (selected) {
		zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	}
}

static inline void zui_component_draw_scrollbar(struct zui_draw_ctx *draw, size_t pos,
						size_t total)
{
	uint16_t width;
	uint16_t height;

	if (draw == NULL) {
		return;
	}

	width = zui_draw_width(draw);
	height = zui_draw_height(draw);
	zui_draw_set_color(draw, ZUI_COLOR_WHITE);
	zui_draw_box(draw,
		     &(struct zui_rect){
			     .x = (int16_t)(width - 3U), .y = 0, .width = 3, .height = height});
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	for (uint16_t y = 0U; y < height; y += 2U) {
		zui_draw_dot(draw, (struct zui_point){.x = (int16_t)(width - 2U), .y = (int16_t)y});
	}
	if (total > 0U) {
		uint16_t block_y = (uint16_t)(((uint32_t)height * pos) / total);
		uint16_t block_h = MAX((uint16_t)(height / total), 1U);

		zui_draw_box(draw, &(struct zui_rect){.x = (int16_t)(width - 3U),
						      .y = (int16_t)block_y,
						      .width = 3,
						      .height = block_h});
	}
}

static inline void zui_component_draw_selection_frame(struct zui_draw_ctx *draw, int16_t x,
						      int16_t y, uint16_t width,
						      uint16_t height)
{
	if (draw == NULL || width < 2U || height < 2U) {
		return;
	}

	zui_draw_line(draw, (struct zui_point){.x = x + 2, .y = y},
		      (struct zui_point){.x = x + width - 2, .y = y});
	zui_draw_line(draw, (struct zui_point){.x = x + 1, .y = y + height - 1},
		      (struct zui_point){.x = x + width, .y = y + height - 1});
	zui_draw_line(draw, (struct zui_point){.x = x + 2, .y = y + height},
		      (struct zui_point){.x = x + width - 1, .y = y + height});
	zui_draw_line(draw, (struct zui_point){.x = x, .y = y + 2},
		      (struct zui_point){.x = x, .y = y + height - 2});
	zui_draw_line(draw, (struct zui_point){.x = x + width - 1, .y = y + 1},
		      (struct zui_point){.x = x + width - 1, .y = y + height - 2});
	zui_draw_line(draw, (struct zui_point){.x = x + width, .y = y + 2},
		      (struct zui_point){.x = x + width, .y = y + height - 2});
	zui_draw_dot(draw, (struct zui_point){.x = x + 1, .y = y + 1});
}

static inline void zui_component_draw_fit_text(struct zui_draw_ctx *draw, struct zui_point pos,
					    uint16_t width, const char *text)
{
	char buffer[48];

	if (draw == NULL || text == NULL) {
		return;
	}

	(void)snprintf(buffer, sizeof(buffer), "%s", text);
	(void)zui_draw_text_fit_width(draw, buffer, sizeof(buffer), width);
	zui_draw_text(draw, pos, buffer);
}

static inline void zui_component_draw_button_left(struct zui_draw_ctx *draw, const char *text)
{
	const struct zui_icon *icon;
	uint16_t text_width;
	uint16_t button_width;
	uint16_t height;
	uint16_t y;

	if (draw == NULL || text == NULL) {
		return;
	}

	icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(), ZUI_ASSET_ICON_BUTTON_LEFT);
	text_width = zui_draw_text_width(draw, text);
	button_width = (uint16_t)(text_width + 3U * 2U + zui_icon_width(icon) + 3U);
	height = 12U;
	y = zui_draw_height(draw);

	zui_draw_box(draw, &(struct zui_rect){.x = 0,
					      .y = (int16_t)(y - height),
					      .width = button_width,
					      .height = height});
	zui_draw_line(draw, (struct zui_point){.x = (int16_t)button_width, .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)button_width, .y = (int16_t)(y - height)});
	zui_draw_line(draw, (struct zui_point){.x = (int16_t)(button_width + 1U), .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)(button_width + 1U),
					 .y = (int16_t)(y - height + 1U)});
	zui_draw_line(draw, (struct zui_point){.x = (int16_t)(button_width + 2U), .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)(button_width + 2U),
					 .y = (int16_t)(y - height + 2U)});
	zui_draw_invert_color(draw);
	zui_draw_icon(draw,
		      (struct zui_point){.x = 3, .y = (int16_t)(y - zui_icon_height(icon) - 3U)},
		      icon);
	zui_draw_text(draw,
		      (struct zui_point){.x = (int16_t)(3U + zui_icon_width(icon) + 3U),
					 .y = (int16_t)(y - 3U)},
		      text);
	zui_draw_invert_color(draw);
}

static inline void zui_component_draw_button_right(struct zui_draw_ctx *draw, const char *text)
{
	const struct zui_icon *icon;
	uint16_t text_width;
	uint16_t button_width;
	uint16_t height;
	uint16_t x;
	uint16_t y;

	if (draw == NULL || text == NULL) {
		return;
	}

	icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(), ZUI_ASSET_ICON_BUTTON_RIGHT);
	text_width = zui_draw_text_width(draw, text);
	button_width = (uint16_t)(text_width + 3U * 2U + zui_icon_width(icon) + 3U);
	height = 12U;
	x = zui_draw_width(draw);
	y = zui_draw_height(draw);

	zui_draw_box(draw, &(struct zui_rect){.x = (int16_t)(x - button_width),
					      .y = (int16_t)(y - height),
					      .width = button_width,
					      .height = height});
	zui_draw_line(draw,
		      (struct zui_point){.x = (int16_t)(x - button_width - 1U), .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)(x - button_width - 1U),
					 .y = (int16_t)(y - height)});
	zui_draw_line(draw,
		      (struct zui_point){.x = (int16_t)(x - button_width - 2U), .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)(x - button_width - 2U),
					 .y = (int16_t)(y - height + 1U)});
	zui_draw_line(draw,
		      (struct zui_point){.x = (int16_t)(x - button_width - 3U), .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)(x - button_width - 3U),
					 .y = (int16_t)(y - height + 2U)});
	zui_draw_invert_color(draw);
	zui_draw_text(
		draw,
		(struct zui_point){.x = (int16_t)(x - button_width + 3U), .y = (int16_t)(y - 3U)},
		text);
	zui_draw_icon(draw,
		      (struct zui_point){.x = (int16_t)(x - 3U - zui_icon_width(icon)),
					 .y = (int16_t)(y - zui_icon_height(icon) - 3U)},
		      icon);
	zui_draw_invert_color(draw);
}

static inline void zui_component_draw_button_center(struct zui_draw_ctx *draw, const char *text)
{
	const struct zui_icon *icon;
	uint16_t text_width;
	uint16_t button_width;
	uint16_t height;
	int16_t x;
	uint16_t y;

	if (draw == NULL || text == NULL) {
		return;
	}

	icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(), ZUI_ASSET_ICON_BUTTON_SELECT);
	text_width = zui_draw_text_width(draw, text);
	button_width = (uint16_t)(text_width + 1U * 2U + zui_icon_width(icon) + 3U);
	height = 12U;
	x = (int16_t)((zui_draw_width(draw) - button_width) / 2U);
	y = zui_draw_height(draw);

	zui_draw_box(draw, &(struct zui_rect){.x = x,
					      .y = (int16_t)(y - height),
					      .width = button_width,
					      .height = height});
	zui_draw_line(draw, (struct zui_point){.x = (int16_t)(x - 1), .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)(x - 1), .y = (int16_t)(y - height)});
	zui_draw_line(draw, (struct zui_point){.x = (int16_t)(x - 2), .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)(x - 2), .y = (int16_t)(y - height + 1U)});
	zui_draw_line(draw, (struct zui_point){.x = (int16_t)(x - 3), .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)(x - 3), .y = (int16_t)(y - height + 2U)});
	zui_draw_line(
		draw, (struct zui_point){.x = (int16_t)(x + button_width), .y = (int16_t)y},
		(struct zui_point){.x = (int16_t)(x + button_width), .y = (int16_t)(y - height)});
	zui_draw_line(draw,
		      (struct zui_point){.x = (int16_t)(x + button_width + 1), .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)(x + button_width + 1),
					 .y = (int16_t)(y - height + 1U)});
	zui_draw_line(draw,
		      (struct zui_point){.x = (int16_t)(x + button_width + 2), .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)(x + button_width + 2),
					 .y = (int16_t)(y - height + 2U)});
	zui_draw_invert_color(draw);
	zui_draw_icon(draw,
		      (struct zui_point){.x = (int16_t)(x + 1),
					 .y = (int16_t)(y - zui_icon_height(icon) - 3U)},
		      icon);
	zui_draw_text(draw,
		      (struct zui_point){.x = (int16_t)(x + 1 + zui_icon_width(icon) + 3U),
					 .y = (int16_t)(y - 3U)},
		      text);
	zui_draw_invert_color(draw);
}

static inline void zui_component_draw_button_up(struct zui_draw_ctx *draw, const char *text)
{
	const struct zui_icon *icon;
	uint16_t text_width;
	uint16_t button_width;
	uint16_t height;

	if (draw == NULL || text == NULL) {
		return;
	}

	icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(), ZUI_ASSET_ICON_BUTTON_UP);
	text_width = zui_draw_text_width(draw, text);
	button_width = (uint16_t)(text_width + 3U * 2U + zui_icon_width(icon) + 3U);
	height = 12U;

	zui_draw_box(draw, &(struct zui_rect){.x = 0, .y = 0, .width = button_width, .height = height});
	zui_draw_line(draw, (struct zui_point){.x = (int16_t)button_width, .y = 0},
		      (struct zui_point){.x = (int16_t)button_width, .y = (int16_t)(height - 1U)});
	zui_draw_line(draw, (struct zui_point){.x = (int16_t)(button_width + 1U), .y = 0},
		      (struct zui_point){.x = (int16_t)(button_width + 1U),
					 .y = (int16_t)(height - 2U)});
	zui_draw_line(draw, (struct zui_point){.x = (int16_t)(button_width + 2U), .y = 0},
		      (struct zui_point){.x = (int16_t)(button_width + 2U),
					 .y = (int16_t)(height - 3U)});
	zui_draw_invert_color(draw);
	zui_draw_icon(draw,
		      (struct zui_point){.x = 3, .y = (int16_t)(height - zui_icon_height(icon) - 3U)},
		      icon);
	zui_draw_text(draw,
		      (struct zui_point){.x = (int16_t)(3U + zui_icon_width(icon) + 3U),
					 .y = (int16_t)(height - 3U)},
		      text);
	zui_draw_invert_color(draw);
}

static inline void zui_component_draw_button_down(struct zui_draw_ctx *draw, const char *text)
{
	const struct zui_icon *icon;
	uint16_t text_width;
	uint16_t button_width;
	uint16_t height;
	uint16_t x;

	if (draw == NULL || text == NULL) {
		return;
	}

	icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(), ZUI_ASSET_ICON_BUTTON_DOWN);
	text_width = zui_draw_text_width(draw, text);
	button_width = (uint16_t)(text_width + 3U * 2U + zui_icon_width(icon) + 3U);
	height = 12U;
	x = zui_draw_width(draw);

	zui_draw_box(draw, &(struct zui_rect){.x = (int16_t)(x - button_width),
					      .y = 0,
					      .width = button_width,
					      .height = height});
	zui_draw_line(draw,
		      (struct zui_point){.x = (int16_t)(x - button_width - 1U), .y = 0},
		      (struct zui_point){.x = (int16_t)(x - button_width - 1U),
					 .y = (int16_t)(height - 1U)});
	zui_draw_line(draw,
		      (struct zui_point){.x = (int16_t)(x - button_width - 2U), .y = 0},
		      (struct zui_point){.x = (int16_t)(x - button_width - 2U),
					 .y = (int16_t)(height - 2U)});
	zui_draw_line(draw,
		      (struct zui_point){.x = (int16_t)(x - button_width - 3U), .y = 0},
		      (struct zui_point){.x = (int16_t)(x - button_width - 3U),
					 .y = (int16_t)(height - 3U)});
	zui_draw_invert_color(draw);
	zui_draw_text(draw,
		      (struct zui_point){.x = (int16_t)(x - button_width + 3U),
					 .y = (int16_t)(height - 3U)},
		      text);
	zui_draw_icon(draw,
		      (struct zui_point){.x = (int16_t)(x - 3U - zui_icon_width(icon)),
					 .y = (int16_t)(height - zui_icon_height(icon) - 4U)},
		      icon);
	zui_draw_invert_color(draw);
}

static inline void zui_component_draw_buttons(struct zui_draw_ctx *draw, const char *left,
					      const char *center, const char *right)
{
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	if (left != NULL && left[0] != '\0') {
		zui_component_draw_button_left(draw, left);
	}
	if (center != NULL && center[0] != '\0') {
		zui_component_draw_button_center(draw, center);
	}
	if (right != NULL && right[0] != '\0') {
		zui_component_draw_button_right(draw, right);
	}
}

static inline void zui_component_draw_top_buttons(struct zui_draw_ctx *draw, const char *up,
						  const char *down)
{
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	if (up != NULL && up[0] != '\0') {
		zui_component_draw_button_up(draw, up);
	}
	if (down != NULL && down[0] != '\0') {
		zui_component_draw_button_down(draw, down);
	}
}

static inline void zui_component_draw_progress_bar_text(struct zui_draw_ctx *draw, int16_t x,
							int16_t y, uint16_t width,
							float value, const char *text)
{
	uint16_t fill_width;

	if (draw == NULL || width <= 2U) {
		return;
	}

	value = CLAMP(value, 0.0f, 1.0f);
	fill_width = (uint16_t)((float)(width - 2U) * value + 0.5f);

	zui_draw_set_color(draw, ZUI_COLOR_WHITE);
	zui_draw_box(draw,
		     &(struct zui_rect){.x = x + 1, .y = y + 1, .width = width - 2U, .height = 9});
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_round_rect(draw, &(struct zui_rect){.x = x, .y = y, .width = width, .height = 11},
			    3U);
	zui_draw_box(draw,
		     &(struct zui_rect){.x = x + 1, .y = y + 1, .width = fill_width, .height = 9});
	zui_draw_set_color(draw, ZUI_COLOR_XOR);
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	zui_draw_text_aligned(
		draw, (struct zui_point){.x = (int16_t)(x + width / 2U), .y = (int16_t)(y + 2)},
		ZUI_ALIGN_CENTER, ZUI_ALIGN_TOP, text != NULL ? text : "");
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
}

static inline bool zui_component_text_placement_has_value(const struct zui_text_placement *placement)
{
	return placement != NULL &&
	       (placement->text != NULL || placement->pos.x != 0 || placement->pos.y != 0 ||
		placement->horizontal != 0 || placement->vertical != 0);
}

static inline uint8_t zui_component_text_line_spacing(uint8_t line_spacing)
{
	return line_spacing == 0U ? ZUI_TEXT_LINE_SPACING_DEFAULT : line_spacing;
}

static inline void zui_component_draw_placement_text(struct zui_draw_ctx *draw,
						  const struct zui_text_placement *placement,
						  const char *fallback,
						  struct zui_point default_pos,
						  enum zui_align default_horizontal,
						  enum zui_align default_vertical,
						  uint8_t line_spacing)
{
	const char *text = fallback;
	struct zui_point pos = default_pos;
	enum zui_align horizontal = default_horizontal;
	enum zui_align vertical = default_vertical;
	uint16_t line_height;
	size_t lines = 0U;
	int16_t y;

	if (draw == NULL) {
		return;
	}

	if (zui_component_text_placement_has_value(placement)) {
		text = placement->text != NULL ? placement->text : fallback;
		pos = placement->pos;
		horizontal = placement->horizontal;
		vertical = placement->vertical;
	}
	if (text == NULL || text[0] == '\0') {
		return;
	}

	line_height = MAX(zui_draw_font_height(draw), 1U) + line_spacing;
	for (const char *p = text; p[0] != '\0'; p++) {
		if (p == text || p[0] == '\n') {
			lines++;
		}
	}
	y = pos.y;
	if (vertical == ZUI_ALIGN_BOTTOM && lines > 0U) {
		y -= (int16_t)(line_height * (lines - 1U));
	} else if (vertical == ZUI_ALIGN_CENTER && lines > 0U) {
		y -= (int16_t)((line_height * (lines - 1U)) / 2U);
	}

	for (const char *start = text; start[0] != '\0' && y <= (int16_t)zui_draw_height(draw);) {
		char line[64];
		const char *end = strchr(start, '\n');
		size_t len = end == NULL ? strlen(start) : (size_t)(end - start);

		len = MIN(len, sizeof(line) - 1U);
		memcpy(line, start, len);
		line[len] = '\0';
		zui_draw_text_aligned(draw, (struct zui_point){.x = pos.x, .y = y}, horizontal,
				      ZUI_ALIGN_TOP, line);
		if (end == NULL) {
			break;
		}
		start = end + 1;
		y += (int16_t)line_height;
	}
}

static inline size_t zui_component_line_count(const char *text)
{
	size_t count = 1U;

	if (text == NULL || text[0] == '\0') {
		return 0U;
	}

	for (const char *p = text; *p != '\0'; p++) {
		if (*p == '\n') {
			count++;
		}
	}

	return count;
}

static inline int zui_component_copy_text(char *dst, size_t dst_size, const char *src)
{
	if (dst == NULL || dst_size == 0U) {
		return -EINVAL;
	}

	if (src == NULL) {
		dst[0] = '\0';
		return 0;
	}

	(void)snprintf(dst, dst_size, "%s", src);
	return strlen(src) >= dst_size ? -ENOSPC : 0;
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_ZUI_COMPONENTS_INTERNAL_H_ */
