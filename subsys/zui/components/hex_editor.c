/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include "internal.h"

#include <ctype.h>
#include <stdlib.h>

struct zui_hex_editor {
	struct zui_hex_editor_config config;
	uint8_t valid_mask[DIV_ROUND_UP(UINT8_MAX, 8)];
	size_t selected;
	bool selected_high_nibble;
	int8_t selected_row;
	uint8_t selected_column;
	uint8_t first_visible;
	struct zui_screen *screen;
};

struct zui_hex_editor_key {
	uint8_t value;
	uint8_t x;
	uint8_t y;
};

static const struct zui_hex_editor_key zui_hex_editor_row_1[] = {
	{.value = '0', .x = 0, .y = 12},   {.value = '1', .x = 11, .y = 12},
	{.value = '2', .x = 22, .y = 12},  {.value = '3', .x = 33, .y = 12},
	{.value = '4', .x = 44, .y = 12},  {.value = '5', .x = 55, .y = 12},
	{.value = '6', .x = 66, .y = 12},  {.value = '7', .x = 77, .y = 12},
	{.value = '\b', .x = 103, .y = 4},
};

static const struct zui_hex_editor_key zui_hex_editor_row_2[] = {
	{.value = '8', .x = 0, .y = 26},   {.value = '9', .x = 11, .y = 26},
	{.value = 'A', .x = 22, .y = 26},  {.value = 'B', .x = 33, .y = 26},
	{.value = 'C', .x = 44, .y = 26},  {.value = 'D', .x = 55, .y = 26},
	{.value = 'E', .x = 66, .y = 26},  {.value = 'F', .x = 77, .y = 26},
	{.value = '\r', .x = 95, .y = 17},
};

static const struct zui_hex_editor_key *zui_hex_editor_row(uint8_t row, size_t *count)
{
	if (row == 0U) {
		*count = ARRAY_SIZE(zui_hex_editor_row_1);
		return zui_hex_editor_row_1;
	}

	*count = ARRAY_SIZE(zui_hex_editor_row_2);
	return zui_hex_editor_row_2;
}

static uint8_t zui_hex_editor_key_value(uint8_t value)
{
	if (value >= '0' && value <= '9') {
		return value - '0';
	}

	return value - 'A' + 10U;
}

static void zui_hex_editor_set_valid(struct zui_hex_editor *editor, size_t index, bool valid)
{
	uint8_t bit;

	if (editor == NULL || index >= editor->config.byte_count || index >= UINT8_MAX) {
		return;
	}

	bit = BIT(index & 0x07U);
	if (valid) {
		editor->valid_mask[index >> 3] |= bit;
	} else {
		editor->valid_mask[index >> 3] &= (uint8_t)~bit;
	}
}

static bool zui_hex_editor_is_valid(const struct zui_hex_editor *editor, size_t index)
{
	uint8_t bit;

	if (editor == NULL || index >= editor->config.byte_count || index >= UINT8_MAX) {
		return false;
	}

	bit = BIT(index & 0x07U);
	return (editor->valid_mask[index >> 3] & bit) != 0U;
}

static void zui_hex_editor_init_valid_mask(struct zui_hex_editor *editor)
{
	if (editor == NULL) {
		return;
	}

	memset(editor->valid_mask, 0, sizeof(editor->valid_mask));
	if (editor->config.bytes == NULL || editor->config.byte_count == 0U) {
		return;
	}

	for (size_t i = 0U; i < editor->config.payload_size; i++) {
		zui_hex_editor_set_valid(editor, i, true);
	}
}

static char zui_hex_editor_nibble_char(uint8_t byte, bool high_nibble)
{
	uint8_t nibble = high_nibble ? (byte >> 4) : (byte & 0x0fU);

	return nibble < 10U ? (char)('0' + nibble) : (char)('A' + nibble - 10U);
}

static char zui_hex_editor_display_char(const struct zui_hex_editor *editor, size_t index,
					bool high_nibble)
{
	if (editor == NULL || editor->config.bytes == NULL ||
	    !zui_hex_editor_is_valid(editor, index)) {
		return '-';
	}

	return zui_hex_editor_nibble_char(editor->config.bytes[index], high_nibble);
}

static void zui_hex_editor_call_changed(struct zui_hex_editor *editor)
{
	if (editor != NULL && editor->config.changed != NULL) {
		editor->config.changed(editor, editor->config.bytes, zui_hex_editor_payload_size(editor),
				       editor->config.user_data);
	}
}

static void zui_hex_editor_draw_input(struct zui_draw_ctx *draw, struct zui_hex_editor *editor)
{
	const uint8_t max_drawable = 8U;
	const uint8_t text_x = 8U;
	const uint8_t text_y = 25U;
	bool input_selected = editor->selected_row == -1;
	bool mini_selected = editor->selected_row == -2;
	const struct zui_icon *left_icon;
	const struct zui_icon *right_icon;
	const struct zui_icon *more_icon;
	const struct zui_icon *chevron_up_icon;
	const struct zui_icon *chevron_down_icon;

	if (input_selected) {
		zui_draw_box(draw, &(struct zui_rect){.x = 0, .y = 12, .width = 127, .height = 19});
		zui_draw_invert_color(draw);
		zui_draw_round_rect(draw, &(struct zui_rect){.x = 6, .y = 14, .width = 115, .height = 15},
				    1U);
	} else {
		zui_draw_round_rect(draw, &(struct zui_rect){.x = 6, .y = 14, .width = 116, .height = 15},
				    1U);
	}

	left_icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
					      ZUI_ASSET_ICON_BUTTON_LEFT_SMALL);
	right_icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
					       ZUI_ASSET_ICON_BUTTON_RIGHT_SMALL);
	more_icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
					      ZUI_ASSET_ICON_MORE_PLACEHOLDER);
	chevron_up_icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
						    ZUI_ASSET_ICON_CHEVRON_UP_TINY);
	chevron_down_icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
						      ZUI_ASSET_ICON_CHEVRON_DOWN_TINY);
	zui_draw_icon(draw, (struct zui_point){.x = 2, .y = 19}, left_icon);
	zui_draw_icon(draw, (struct zui_point){.x = input_selected ? 122 : 123, .y = 19}, right_icon);

	for (size_t i = editor->first_visible;
	     i < editor->config.byte_count && i < editor->first_visible + max_drawable; i++) {
		size_t byte_position = i - editor->first_visible;
		int16_t x = (int16_t)(text_x + byte_position * 14U);
		bool selected = i == editor->selected;
		char high = zui_hex_editor_display_char(editor, i, true);
		char low = zui_hex_editor_display_char(editor, i, false);

		if (input_selected) {
			x -= 1;
			if (selected) {
				zui_draw_box(draw, &(struct zui_rect){.x = (int16_t)(x + 1),
								      .y = text_y - 9,
								      .width = 13,
								      .height = 11});
				zui_draw_invert_color(draw);
				zui_draw_glyph(draw, (struct zui_point){.x = (int16_t)(x + 2),
									.y = text_y}, high);
				zui_draw_glyph(draw, (struct zui_point){.x = (int16_t)(x + 8),
									.y = text_y}, low);
				zui_draw_invert_color(draw);
			} else {
				if (editor->first_visible > 0U && i == editor->first_visible) {
					zui_draw_icon(draw,
						      (struct zui_point){.x = (int16_t)(x + 2),
									 .y = text_y - 7},
						      more_icon);
				} else {
					zui_draw_glyph(draw,
						       (struct zui_point){.x = (int16_t)(x + 2),
									  .y = text_y},
						       high);
				}
				if (editor->config.byte_count - editor->first_visible > max_drawable &&
				    i == editor->first_visible + max_drawable - 1U) {
					zui_draw_icon(draw,
						      (struct zui_point){.x = (int16_t)(x + 8),
									 .y = text_y - 7},
						      more_icon);
				} else {
					zui_draw_glyph(draw,
						       (struct zui_point){.x = (int16_t)(x + 8),
									  .y = text_y},
						       low);
				}
			}
			continue;
		}

		if (selected) {
			zui_draw_rect(draw, &(struct zui_rect){.x = x,
							      .y = text_y - 9,
							      .width = 15,
							      .height = 11});
			if (mini_selected) {
				zui_draw_icon(draw, (struct zui_point){.x = (int16_t)(x + 6),
								       .y = text_y - 14},
					      chevron_up_icon);
				zui_draw_icon(draw, (struct zui_point){.x = (int16_t)(x + 6),
								       .y = text_y + 5},
					      chevron_down_icon);
				if (editor->selected_high_nibble) {
					zui_draw_glyph(draw, (struct zui_point){.x = (int16_t)(x + 8),
										.y = text_y}, low);
					zui_draw_box(draw,
						     &(struct zui_rect){.x = (int16_t)(x + 1),
									.y = text_y - 8,
									.width = 7,
									.height = 9});
					zui_draw_invert_color(draw);
					zui_draw_line(draw,
						      (struct zui_point){.x = (int16_t)(x + 14),
									 .y = text_y - 6},
						      (struct zui_point){.x = (int16_t)(x + 14),
									 .y = text_y - 2});
					zui_draw_glyph(draw, (struct zui_point){.x = (int16_t)(x + 2),
										.y = text_y}, high);
					zui_draw_invert_color(draw);
				} else {
					zui_draw_box(draw,
						     &(struct zui_rect){.x = (int16_t)(x + 7),
									.y = text_y - 8,
									.width = 7,
									.height = 9});
					zui_draw_glyph(draw, (struct zui_point){.x = (int16_t)(x + 2),
										.y = text_y}, high);
					zui_draw_invert_color(draw);
					zui_draw_line(draw, (struct zui_point){.x = x, .y = text_y - 6},
						      (struct zui_point){.x = x, .y = text_y - 2});
					zui_draw_glyph(draw, (struct zui_point){.x = (int16_t)(x + 8),
										.y = text_y}, low);
					zui_draw_invert_color(draw);
				}
				continue;
			}
		}

		if (editor->first_visible > 0U && i == editor->first_visible) {
			zui_draw_icon(draw, (struct zui_point){.x = (int16_t)(x + 2),
							       .y = text_y - 7},
				      more_icon);
		} else {
			zui_draw_glyph(draw, (struct zui_point){.x = (int16_t)(x + 2), .y = text_y},
				       high);
		}
		if (editor->config.byte_count - editor->first_visible > max_drawable &&
		    i == editor->first_visible + max_drawable - 1U) {
			zui_draw_icon(draw, (struct zui_point){.x = (int16_t)(x + 8),
							       .y = text_y - 7},
				      more_icon);
		} else {
			zui_draw_glyph(draw, (struct zui_point){.x = (int16_t)(x + 8), .y = text_y},
				       low);
		}
	}

	if (input_selected) {
		zui_draw_invert_color(draw);
	}

	if (mini_selected && editor->first_visible + MIN(editor->config.byte_count, 9U) <= 100U) {
		const struct zui_icon *hash_icon =
			zui_asset_pack_icon_by_id(zui_asset_pack_default(), ZUI_ASSET_ICON_HASHMARK);

		zui_draw_icon(draw, (struct zui_point){.x = 1, .y = text_y + 8}, hash_icon);
		for (size_t i = editor->first_visible;
		     i < editor->config.byte_count && i < editor->first_visible + max_drawable; i++) {
			size_t byte_position = i - editor->first_visible;
			char tens = (char)('0' + ((i + 1U) / 10U) % 10U);
			char ones = (char)('0' + (i + 1U) % 10U);

			zui_draw_glyph(draw,
				       (struct zui_point){.x = (int16_t)(text_x + 2U +
									byte_position * 14U),
							  .y = text_y + 15},
				       tens);
			zui_draw_glyph(draw,
				       (struct zui_point){.x = (int16_t)(text_x + 8U +
									byte_position * 14U),
							  .y = text_y + 15},
				       ones);
		}
	} else if (mini_selected) {
		char str[8];

		zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
		zui_draw_text(draw, (struct zui_point){.x = text_x, .y = text_y + 15},
			      "Selected index");
		zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
		(void)snprintf(str, sizeof(str), "%u", (unsigned int)(editor->selected + 1U));
		zui_draw_text(draw, (struct zui_point){.x = text_x + 75, .y = text_y + 15}, str);
		zui_draw_set_font(draw, ZUI_FONT_KEYBOARD);
	}
}

static void zui_hex_editor_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct zui_hex_editor *editor = user_data;

	zui_draw_reset(draw);
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_set_font(draw, ZUI_FONT_KEYBOARD);
	if (editor == NULL || editor->config.bytes == NULL) {
		return;
	}

	zui_hex_editor_draw_input(draw, editor);

	if (editor->selected_row == -2) {
		const struct zui_icon *back_icon = zui_asset_pack_icon_by_id(
			zui_asset_pack_default(), ZUI_ASSET_ICON_PIN_BACK_ARROW);

		zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
		zui_draw_icon(draw, (struct zui_point){.x = 3, .y = 1}, back_icon);
		zui_draw_text_aligned(draw, (struct zui_point){.x = 16, .y = 9},
				      ZUI_ALIGN_LEFT, ZUI_ALIGN_BOTTOM, "back to keyboard");
		zui_component_draw_button_center(draw, "Save");
		return;
	}

	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	if (editor->selected_row == -1) {
		const struct zui_icon *up_icon = zui_asset_pack_icon_by_id(
			zui_asset_pack_default(), ZUI_ASSET_ICON_ARROW_UP_SMALL);

		zui_draw_text(draw, (struct zui_point){.x = 10, .y = 9},
			      "Move up for alternate input");
		zui_draw_icon(draw, (struct zui_point){.x = 3, .y = 4}, up_icon);
	} else if (editor->config.title != NULL) {
		zui_draw_text(draw, (struct zui_point){.x = 2, .y = 9}, editor->config.title);
	}

	zui_draw_set_font(draw, ZUI_FONT_KEYBOARD);
	for (uint8_t row_index = 0U; row_index < 2U; row_index++) {
		size_t row_count;
		const struct zui_hex_editor_key *row = zui_hex_editor_row(row_index, &row_count);

		for (uint8_t column = 0U; column < row_count; column++) {
			const struct zui_hex_editor_key *key = &row[column];
			bool selected = editor->selected_row == row_index &&
					editor->selected_column == column;
			bool input_hint = editor->selected_row == -1 && row_index == 0U &&
					  editor->selected_column == column;
			const struct zui_icon *icon = NULL;

			if (key->value == '\b') {
				icon = zui_asset_pack_icon_by_id(
					zui_asset_pack_default(),
					selected ? ZUI_ASSET_ICON_KEY_BACKSPACE_SELECTED
						 : ZUI_ASSET_ICON_KEY_BACKSPACE);
			} else if (key->value == '\r') {
				icon = zui_asset_pack_icon_by_id(
					zui_asset_pack_default(),
					selected ? ZUI_ASSET_ICON_KEY_SAVE_SELECTED
						 : ZUI_ASSET_ICON_KEY_SAVE);
			}
			if (icon != NULL) {
				zui_draw_icon(draw,
					      (struct zui_point){.x = (int16_t)(7U + key->x),
								 .y = (int16_t)(31U + key->y)},
					      icon);
				continue;
			}
			if (selected) {
				zui_draw_box(draw,
					     &(struct zui_rect){.x = (int16_t)(7U + key->x - 3U),
								.y = (int16_t)(31U + key->y - 10U),
								.width = 11,
								.height = 13});
				zui_draw_set_color(draw, ZUI_COLOR_WHITE);
			} else if (input_hint) {
				zui_draw_rect(draw,
					      &(struct zui_rect){
						      .x = (int16_t)(7U + key->x - 3U),
						      .y = (int16_t)(31U + key->y - 10U),
						      .width = 11,
						      .height = 13});
			}
			zui_draw_glyph(draw,
				       (struct zui_point){.x = (int16_t)(7U + key->x),
							  .y = (int16_t)(31U + key->y)},
				       key->value);
			zui_draw_set_color(draw, ZUI_COLOR_BLACK);
		}
	}
}

static void zui_hex_editor_adjust_visible(struct zui_hex_editor *editor)
{
	if (editor->selected < editor->first_visible) {
		editor->first_visible = (uint8_t)editor->selected;
	} else if (editor->selected >= editor->first_visible + 8U) {
		editor->first_visible = (uint8_t)(editor->selected - 7U);
	}
}

static void zui_hex_editor_inc_selected_byte(struct zui_hex_editor *editor)
{
	if (editor == NULL || editor->config.byte_count == 0U) {
		return;
	}

	if (editor->selected + 1U < editor->config.byte_count) {
		editor->selected++;
		if (editor->config.byte_count > 8U &&
		    editor->selected - editor->first_visible > 6U &&
		    editor->first_visible < editor->config.byte_count - 8U) {
			editor->first_visible++;
		}
	}
}

static void zui_hex_editor_dec_selected_byte(struct zui_hex_editor *editor)
{
	if (editor == NULL || editor->selected == 0U) {
		return;
	}

	editor->selected--;
	if (editor->selected - editor->first_visible < 1U && editor->first_visible > 0U) {
		editor->first_visible--;
	}
}

static void zui_hex_editor_inc_selected_nibble(struct zui_hex_editor *editor)
{
	if (editor->config.byte_count == 0U) {
		editor->selected = 0U;
		return;
	}

	if (editor->selected_high_nibble) {
		editor->selected_high_nibble = false;
	} else if (editor->selected + 1U < editor->config.byte_count) {
		editor->selected_high_nibble = true;
		zui_hex_editor_inc_selected_byte(editor);
	}
}

static void zui_hex_editor_dec_selected_nibble(struct zui_hex_editor *editor)
{
	if (editor->config.byte_count == 0U) {
		editor->selected = 0U;
		return;
	}

	if (!editor->selected_high_nibble) {
		editor->selected_high_nibble = true;
	} else if (editor->selected > 0U) {
		editor->selected_high_nibble = false;
		zui_hex_editor_dec_selected_byte(editor);
	}
}

static void zui_hex_editor_set_nibble(struct zui_hex_editor *editor, uint8_t value)
{
	if (editor == NULL || editor->selected >= editor->config.byte_count) {
		return;
	}

	if (editor->selected_high_nibble) {
		editor->config.bytes[editor->selected] =
			(editor->config.bytes[editor->selected] & 0x0fU) | (uint8_t)(value << 4);
	} else {
		editor->config.bytes[editor->selected] =
			(editor->config.bytes[editor->selected] & 0xf0U) | value;
	}
	zui_hex_editor_set_valid(editor, editor->selected, true);
	zui_hex_editor_call_changed(editor);
}

static void zui_hex_editor_apply_nibble(struct zui_hex_editor *editor, uint8_t value)
{
	zui_hex_editor_set_nibble(editor, value);
	zui_hex_editor_inc_selected_nibble(editor);
}

static void zui_hex_editor_clear_selected(struct zui_hex_editor *editor)
{
	if (editor == NULL || editor->config.bytes == NULL || editor->config.byte_count == 0U) {
		return;
	}

	editor->config.bytes[editor->selected] = 0U;
	zui_hex_editor_set_valid(editor, editor->selected, false);
	editor->selected_high_nibble = true;
	zui_hex_editor_dec_selected_byte(editor);
	zui_hex_editor_call_changed(editor);
}

static void zui_hex_editor_change_selected_nibble(struct zui_hex_editor *editor, int8_t delta)
{
	uint8_t value;

	if (editor == NULL || editor->config.bytes == NULL || editor->config.byte_count == 0U ||
	    editor->selected >= editor->config.byte_count) {
		return;
	}

	if (editor->selected_high_nibble) {
		value = (uint8_t)(((editor->config.bytes[editor->selected] >> 4) + delta) & 0x0fU);
		editor->config.bytes[editor->selected] =
			(editor->config.bytes[editor->selected] & 0x0fU) | (uint8_t)(value << 4);
	} else {
		value = (uint8_t)(((editor->config.bytes[editor->selected] & 0x0fU) + delta) &
				  0x0fU);
		editor->config.bytes[editor->selected] =
			(editor->config.bytes[editor->selected] & 0xf0U) | value;
	}
	zui_hex_editor_set_valid(editor, editor->selected, true);
	zui_hex_editor_call_changed(editor);
}

static void zui_hex_editor_keyboard_left(struct zui_hex_editor *editor)
{
	size_t row_count;

	if (editor->selected_column > 0U) {
		editor->selected_column--;
		return;
	}

	editor->selected_row = editor->selected_row == 0 ? 1 : 0;
	(void)zui_hex_editor_row((uint8_t)editor->selected_row, &row_count);
	editor->selected_column = (uint8_t)(row_count - 1U);
}

static void zui_hex_editor_keyboard_right(struct zui_hex_editor *editor)
{
	size_t row_count;

	(void)zui_hex_editor_row((uint8_t)editor->selected_row, &row_count);
	if (editor->selected_column + 1U < row_count) {
		editor->selected_column++;
		return;
	}

	editor->selected_row = (int8_t)((editor->selected_row + 1) % 2);
	editor->selected_column = 0U;
}

static bool zui_hex_editor_keyboard_selected(const struct zui_hex_editor *editor)
{
	return editor != NULL && editor->selected_row >= 0;
}

static void zui_hex_editor_move_to_first(struct zui_hex_editor *editor)
{
	if (editor == NULL || editor->config.byte_count == 0U) {
		return;
	}

	editor->selected = 0U;
	editor->selected_high_nibble = true;
	zui_hex_editor_adjust_visible(editor);
}

static void zui_hex_editor_move_to_last(struct zui_hex_editor *editor)
{
	if (editor == NULL || editor->config.byte_count == 0U) {
		return;
	}

	editor->selected = editor->config.byte_count - 1U;
	editor->selected_high_nibble = false;
	zui_hex_editor_adjust_visible(editor);
}

static bool zui_hex_editor_input(const struct zui_input_event *event, void *user_data)
{
	struct zui_hex_editor *editor = user_data;
	size_t row_count;

	if (editor == NULL || event == NULL || editor->config.byte_count == 0U) {
		return false;
	}
	if (event->action != ZUI_INPUT_ACTION_CLICK &&
	    event->action != ZUI_INPUT_ACTION_LONG_PRESS) {
		return false;
	}

	if (event->code == ZUI_INPUT_CODE_LEFT) {
		if (zui_hex_editor_keyboard_selected(editor)) {
			zui_hex_editor_keyboard_left(editor);
		} else if (event->action == ZUI_INPUT_ACTION_LONG_PRESS) {
			zui_hex_editor_move_to_first(editor);
		} else if (editor->selected_row == -2) {
			zui_hex_editor_dec_selected_nibble(editor);
		} else {
			zui_hex_editor_dec_selected_byte(editor);
		}
		(void)zui_component_request_redraw(editor->screen);
		return true;
	}
	if (event->code == ZUI_INPUT_CODE_RIGHT) {
		if (zui_hex_editor_keyboard_selected(editor)) {
			zui_hex_editor_keyboard_right(editor);
		} else if (event->action == ZUI_INPUT_ACTION_LONG_PRESS) {
			zui_hex_editor_move_to_last(editor);
		} else if (editor->selected_row == -2) {
			zui_hex_editor_inc_selected_nibble(editor);
		} else {
			zui_hex_editor_inc_selected_byte(editor);
		}
		(void)zui_component_request_redraw(editor->screen);
		return true;
	}
	if (event->code == ZUI_INPUT_CODE_UP) {
		if (editor->selected_row > -2) {
			editor->selected_row--;
		} else {
			zui_hex_editor_change_selected_nibble(editor, 1);
		}
		(void)zui_component_request_redraw(editor->screen);
		return true;
	}
	if (event->code == ZUI_INPUT_CODE_DOWN) {
		if (editor->selected_row == -2) {
			zui_hex_editor_change_selected_nibble(editor, -1);
		} else if (zui_hex_editor_keyboard_selected(editor)) {
			if (editor->selected_row < 1) {
				editor->selected_row++;
				(void)zui_hex_editor_row((uint8_t)editor->selected_row,
							  &row_count);
				editor->selected_column =
					MIN(editor->selected_column, row_count - 1U);
			}
		} else {
			editor->selected_row = 0;
			editor->selected_high_nibble = true;
		}
		(void)zui_component_request_redraw(editor->screen);
		return true;
	}
	if (event->code == ZUI_INPUT_CODE_BACK &&
	    event->action == ZUI_INPUT_ACTION_LONG_PRESS) {
		return false;
	}
	if (event->code == ZUI_INPUT_CODE_BACK && event->action == ZUI_INPUT_ACTION_CLICK) {
		if (editor->selected_row == -2) {
			editor->selected_row = -1;
		} else {
			zui_hex_editor_clear_selected(editor);
		}
		(void)zui_component_request_redraw(editor->screen);
		return true;
	}
	if (zui_component_is_select(event) || zui_component_is_long_select(event)) {
		const struct zui_hex_editor_key *row =
			zui_hex_editor_row((uint8_t)editor->selected_row, &row_count);
		uint8_t key = row[editor->selected_column].value;

		if (!zui_hex_editor_keyboard_selected(editor)) {
			if (editor->selected_row == -2) {
				return zui_hex_editor_submit(editor) == 0;
			}
			editor->selected_row = 0;
			editor->selected_high_nibble = true;
			(void)zui_component_request_redraw(editor->screen);
			return true;
		}
		if (key == '\r') {
			return zui_hex_editor_submit(editor) == 0;
		}
		if (key == '\b') {
			zui_hex_editor_clear_selected(editor);
		} else {
			zui_hex_editor_apply_nibble(editor, zui_hex_editor_key_value(key));
		}
		(void)zui_component_request_redraw(editor->screen);
		return true;
	}

	return false;
}

static const struct zui_screen_ops zui_hex_editor_screen_ops = {
	.draw = zui_hex_editor_draw,
	.input = zui_hex_editor_input,
};

struct zui_hex_editor *zui_hex_editor_create(const struct zui_hex_editor_config *config)
{
	struct zui_hex_editor *editor;

	if (config == NULL || config->byte_count > UINT8_MAX ||
	    (config->byte_count > 0U && config->bytes == NULL)) {
		return NULL;
	}

	editor = zui_calloc(1U, sizeof(*editor));
	if (editor == NULL) {
		return NULL;
	}

	if (zui_hex_editor_update(editor, config) != 0) {
		zui_hex_editor_destroy(editor);
		return NULL;
	}

	editor->screen = zui_screen_create(&zui_hex_editor_screen_ops, editor);
	if (editor->screen == NULL) {
		zui_hex_editor_destroy(editor);
		return NULL;
	}

	return editor;
}

void zui_hex_editor_destroy(struct zui_hex_editor *editor)
{
	if (editor == NULL) {
		return;
	}

	zui_screen_destroy(editor->screen);
	zui_free(editor);
}

struct zui_screen *zui_hex_editor_get_screen(struct zui_hex_editor *editor)
{
	return editor == NULL ? NULL : editor->screen;
}

int zui_hex_editor_update(struct zui_hex_editor *editor, const struct zui_hex_editor_config *config)
{
	if (editor == NULL || config == NULL ||
	    config->byte_count > UINT8_MAX ||
	    config->payload_size > config->byte_count ||
	    (config->byte_count > 0U && config->bytes == NULL)) {
		return -EINVAL;
	}

	editor->config = *config;
	if (editor->selected >= editor->config.byte_count) {
		editor->selected = 0U;
	}
	editor->selected_high_nibble = true;
	editor->selected_row = 0;
	editor->selected_column = 0U;
	zui_hex_editor_init_valid_mask(editor);
	zui_hex_editor_adjust_visible(editor);

	return zui_component_request_redraw(editor->screen);
}

size_t zui_hex_editor_size(const struct zui_hex_editor *editor)
{
	return editor == NULL ? 0U : editor->config.byte_count;
}

size_t zui_hex_editor_payload_size(const struct zui_hex_editor *editor)
{
	if (editor == NULL) {
		return 0U;
	}

	for (size_t i = editor->config.byte_count; i > 0U; i--) {
		if (zui_hex_editor_is_valid(editor, i - 1U)) {
			return i;
		}
	}

	return 0U;
}

const uint8_t *zui_hex_editor_data(const struct zui_hex_editor *editor)
{
	return editor == NULL ? NULL : editor->config.bytes;
}

int zui_hex_editor_set_data(struct zui_hex_editor *editor, const uint8_t *bytes, size_t byte_count)
{
	if (editor == NULL || (byte_count > 0U && bytes == NULL)) {
		return -EINVAL;
	}
	if (byte_count != editor->config.byte_count) {
		return -EMSGSIZE;
	}

	if (byte_count > 0U) {
		memcpy(editor->config.bytes, bytes, byte_count);
	}
	editor->config.payload_size = byte_count;
	zui_hex_editor_init_valid_mask(editor);

	return zui_component_request_redraw(editor->screen);
}

int zui_hex_editor_submit(struct zui_hex_editor *editor)
{
	if (editor == NULL) {
		return -EINVAL;
	}

	if (editor->config.submitted != NULL) {
		editor->config.submitted(editor, editor->config.bytes,
					 zui_hex_editor_payload_size(editor), editor->config.user_data);
	}

	return 0;
}
