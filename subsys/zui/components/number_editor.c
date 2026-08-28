/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include "internal.h"

#include <ctype.h>
#include <stdlib.h>

struct zui_number_editor {
	struct zui_number_editor_config config;
	char text[32];
	size_t selected_row;
	size_t selected_column;
	struct zui_screen *screen;
};

struct zui_number_editor_key {
	char value;
	uint8_t x;
	uint8_t y;
};

static const struct zui_number_editor_key zui_number_editor_row_1[] = {
	{.value = '0', .x = 0, .y = 12},  {.value = '1', .x = 11, .y = 12},
	{.value = '2', .x = 22, .y = 12}, {.value = '3', .x = 33, .y = 12},
	{.value = '4', .x = 44, .y = 12}, {.value = '\b', .x = 103, .y = 4},
};

static const struct zui_number_editor_key zui_number_editor_row_2[] = {
	{.value = '5', .x = 0, .y = 26},   {.value = '6', .x = 11, .y = 26},
	{.value = '7', .x = 22, .y = 26},  {.value = '8', .x = 33, .y = 26},
	{.value = '9', .x = 44, .y = 26},  {.value = '-', .x = 55, .y = 17},
	{.value = '\r', .x = 95, .y = 17},
};

static const struct zui_number_editor_key *zui_number_editor_row(size_t row, size_t *count)
{
	if (row == 0U) {
		*count = ARRAY_SIZE(zui_number_editor_row_1);
		return zui_number_editor_row_1;
	}

	*count = ARRAY_SIZE(zui_number_editor_row_2);
	return zui_number_editor_row_2;
}

static int64_t zui_number_editor_clamp(const struct zui_number_editor *editor, int64_t value)
{
	if (value < editor->config.min_value) {
		return editor->config.min_value;
	}
	if (value > editor->config.max_value) {
		return editor->config.max_value;
	}

	return value;
}

static bool zui_number_editor_uses_sign(const struct zui_number_editor *editor)
{
	return editor != NULL && !editor->config.unsigned_only && editor->config.min_value < 0;
}

static void zui_number_editor_skip_hidden_sign(struct zui_number_editor *editor, bool forward)
{
	for (size_t i = 0U; i < 16U; i++) {
		size_t row_count;
		const struct zui_number_editor_key *row =
			zui_number_editor_row(editor->selected_row, &row_count);

		if (row[editor->selected_column].value != '-' ||
		    zui_number_editor_uses_sign(editor)) {
			return;
		}

		if (forward) {
			editor->selected_column++;
			if (editor->selected_column >= row_count) {
				editor->selected_row = (editor->selected_row + 1U) % 2U;
				editor->selected_column = 0U;
			}
		} else if (editor->selected_column > 0U) {
			editor->selected_column--;
		} else {
			editor->selected_row = editor->selected_row == 0U ? 1U : 0U;
			(void)zui_number_editor_row(editor->selected_row, &row_count);
			editor->selected_column = row_count - 1U;
		}
	}
}

static void zui_number_editor_sync_text(struct zui_number_editor *editor)
{
	if (editor == NULL) {
		return;
	}

	if (editor->config.unsigned_only) {
		if (editor->config.keep_leading_zeros && editor->config.max_digits > 0U) {
			(void)snprintf(editor->text, sizeof(editor->text), "%0*llu",
				       (int)MIN(editor->config.max_digits,
						 sizeof(editor->text) - 1U),
				       (unsigned long long)MAX(editor->config.value, 0));
		} else {
			(void)snprintf(editor->text, sizeof(editor->text), "%llu",
				       (unsigned long long)MAX(editor->config.value, 0));
		}
	} else {
		(void)snprintf(editor->text, sizeof(editor->text), "%lld",
			       (long long)editor->config.value);
	}
}

static int zui_number_editor_parse_text(struct zui_number_editor *editor, int64_t *value)
{
	char *end;
	long long parsed;

	if (editor == NULL || value == NULL || editor->text[0] == '\0' ||
	    strcmp(editor->text, "-") == 0) {
		return -EINVAL;
	}

	errno = 0;
	parsed = strtoll(editor->text, &end, 10);
	if (errno != 0 || end == editor->text || *end != '\0') {
		return -EINVAL;
	}
	if (editor->config.unsigned_only && parsed < 0) {
		return -ERANGE;
	}
	if (parsed < editor->config.min_value || parsed > editor->config.max_value) {
		return -ERANGE;
	}

	*value = parsed;
	return 0;
}

static bool zui_number_editor_save_enabled(struct zui_number_editor *editor)
{
	int64_t value;

	return zui_number_editor_parse_text(editor, &value) == 0;
}

static void zui_number_editor_backspace(struct zui_number_editor *editor)
{
	size_t len = strlen(editor->text);
	int64_t value;

	if (len == 0U) {
		return;
	}

	editor->text[len - 1U] = '\0';
	if (zui_number_editor_parse_text(editor, &value) == 0) {
		editor->config.value = value;
	}
}

static void zui_number_editor_toggle_sign(struct zui_number_editor *editor)
{
	size_t len;
	int64_t value;

	if (!zui_number_editor_uses_sign(editor)) {
		return;
	}

	len = strlen(editor->text);
	if (editor->text[0] == '-') {
		memmove(editor->text, &editor->text[1], len);
	} else if (len + 1U < sizeof(editor->text)) {
		memmove(&editor->text[1], editor->text, len + 1U);
		editor->text[0] = '-';
	}
	if (zui_number_editor_parse_text(editor, &value) == 0) {
		editor->config.value = value;
	}
}

static void zui_number_editor_add_digit(struct zui_number_editor *editor, char digit)
{
	size_t len = strlen(editor->text);
	size_t max_digits = editor->config.max_digits == 0U ? 10U : editor->config.max_digits;
	int64_t value;

	if (len >= sizeof(editor->text) - 1U || len >= max_digits) {
		return;
	}
	if (strcmp(editor->text, "0") == 0 && !editor->config.keep_leading_zeros) {
		editor->text[0] = digit;
		editor->text[1] = '\0';
	} else {
		editor->text[len] = digit;
		editor->text[len + 1U] = '\0';
	}
	if (zui_number_editor_parse_text(editor, &value) == 0) {
		editor->config.value = value;
	}
}

static void zui_number_editor_draw_keyboard_icon(struct zui_draw_ctx *draw,
						struct zui_number_editor *editor,
						const struct zui_number_editor_key *key, size_t row,
						size_t column)
{
	const struct zui_icon *icon = NULL;
	bool selected = editor->selected_row == row && editor->selected_column == column;
	bool save_enabled = zui_number_editor_save_enabled(editor);

	switch (key->value) {
	case '\b':
		icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
						 selected ? ZUI_ASSET_ICON_KEY_BACKSPACE_SELECTED
							  : ZUI_ASSET_ICON_KEY_BACKSPACE);
		break;
	case '-':
		icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
						 selected ? ZUI_ASSET_ICON_KEY_SIGN_SELECTED
							  : ZUI_ASSET_ICON_KEY_SIGN);
		break;
	case '\r':
		icon = zui_asset_pack_icon_by_id(
			zui_asset_pack_default(),
			save_enabled ? (selected ? ZUI_ASSET_ICON_KEY_SAVE_SELECTED
						 : ZUI_ASSET_ICON_KEY_SAVE)
				     : (selected ? ZUI_ASSET_ICON_KEY_SAVE_BLOCKED_SELECTED
						 : ZUI_ASSET_ICON_KEY_SAVE_BLOCKED));
		break;
	default:
		break;
	}
	if (icon != NULL) {
		zui_draw_icon(draw,
			      (struct zui_point){.x = (int16_t)(7U + key->x),
						 .y = (int16_t)(31U + key->y)},
			      icon);
	}
}

static void zui_number_editor_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct zui_number_editor *editor = user_data;

	zui_draw_reset(draw);
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	if (editor != NULL && editor->config.title != NULL) {
		zui_draw_text(draw, (struct zui_point){.x = 2, .y = 9}, editor->config.title);
	}
	zui_draw_round_rect(draw, &(struct zui_rect){.x = 6, .y = 14, .width = 116, .height = 15},
			    1U);
	zui_draw_text(draw, (struct zui_point){.x = 8, .y = 25},
		      editor != NULL ? editor->text : "");

	zui_draw_set_font(draw, ZUI_FONT_KEYBOARD);
	for (size_t row_index = 0U; row_index < 2U; row_index++) {
		size_t row_count;
		const struct zui_number_editor_key *row =
			zui_number_editor_row(row_index, &row_count);

		for (size_t column = 0U; column < row_count; column++) {
			const struct zui_number_editor_key *key = &row[column];
			bool selected = editor != NULL && editor->selected_row == row_index &&
					editor->selected_column == column;

			if (key->value == '-' && !zui_number_editor_uses_sign(editor)) {
				continue;
			}
			if (key->value == '\b' || key->value == '-' || key->value == '\r') {
				zui_number_editor_draw_keyboard_icon(draw, editor, key, row_index,
								    column);
			} else {
				if (selected) {
					zui_draw_box(draw,
						     &(struct zui_rect){
							     .x = (int16_t)(7U + key->x - 3U),
							     .y = (int16_t)(31U + key->y - 10U),
							     .width = 11,
							     .height = 13});
					zui_draw_set_color(draw, ZUI_COLOR_WHITE);
				}
				zui_draw_glyph(draw,
					       (struct zui_point){.x = (int16_t)(7U + key->x),
								  .y = (int16_t)(31U + key->y)},
					       key->value);
				zui_draw_set_color(draw, ZUI_COLOR_BLACK);
			}
		}
	}
}

static bool zui_number_editor_input(const struct zui_input_event *event, void *user_data)
{
	struct zui_number_editor *editor = user_data;
	size_t row_count;

	if (editor == NULL || event == NULL) {
		return false;
	}

	if (event->action != ZUI_INPUT_ACTION_CLICK &&
	    event->action != ZUI_INPUT_ACTION_LONG_PRESS) {
		return false;
	}
	if (event->code == ZUI_INPUT_CODE_KEYPAD) {
		char key = (char)event->value;

		if (key == '.' && event->action == ZUI_INPUT_ACTION_LONG_PRESS) {
			return zui_number_editor_submit(editor) == 0;
		}
		if (event->action != ZUI_INPUT_ACTION_CLICK) {
			return false;
		}
		if (key >= '0' && key <= '9') {
			zui_number_editor_add_digit(editor, key);
		} else if (key == '*') {
			zui_number_editor_backspace(editor);
		} else if (key == '.') {
			return zui_number_editor_submit(editor) == 0;
		} else {
			return false;
		}
		(void)zui_component_request_redraw(editor->screen);
		return true;
	}

	if (event->code == ZUI_INPUT_CODE_BACK) {
		if (event->action == ZUI_INPUT_ACTION_LONG_PRESS) {
			return false;
		}
		zui_number_editor_backspace(editor);
		(void)zui_component_request_redraw(editor->screen);
		return true;
	}
	if (zui_component_is_long_select(event)) {
		return zui_number_editor_submit(editor) == 0;
	}

	(void)zui_number_editor_row(editor->selected_row, &row_count);
	if (event->code == ZUI_INPUT_CODE_LEFT) {
		if (editor->selected_column > 0U) {
			editor->selected_column--;
		} else {
			editor->selected_row = editor->selected_row == 0U ? 1U : 0U;
			(void)zui_number_editor_row(editor->selected_row, &row_count);
			editor->selected_column = row_count - 1U;
		}
		zui_number_editor_skip_hidden_sign(editor, false);
		(void)zui_component_request_redraw(editor->screen);
		return true;
	}
	if (event->code == ZUI_INPUT_CODE_RIGHT) {
		editor->selected_column++;
		if (editor->selected_column >= row_count) {
			editor->selected_row = (editor->selected_row + 1U) % 2U;
			editor->selected_column = 0U;
		}
		zui_number_editor_skip_hidden_sign(editor, true);
		(void)zui_component_request_redraw(editor->screen);
		return true;
	}
	if (event->code == ZUI_INPUT_CODE_UP && editor->selected_row > 0U) {
		editor->selected_row--;
		(void)zui_number_editor_row(editor->selected_row, &row_count);
		editor->selected_column = MIN(editor->selected_column, row_count - 1U);
		zui_number_editor_skip_hidden_sign(editor, false);
		(void)zui_component_request_redraw(editor->screen);
		return true;
	}
	if (event->code == ZUI_INPUT_CODE_DOWN && editor->selected_row < 1U) {
		editor->selected_row++;
		(void)zui_number_editor_row(editor->selected_row, &row_count);
		editor->selected_column = MIN(editor->selected_column, row_count - 1U);
		zui_number_editor_skip_hidden_sign(editor, true);
		(void)zui_component_request_redraw(editor->screen);
		return true;
	}
	if (zui_component_is_select(event)) {
		const struct zui_number_editor_key *row =
			zui_number_editor_row(editor->selected_row, &row_count);
		char key = row[editor->selected_column].value;

		if (key >= '0' && key <= '9') {
			zui_number_editor_add_digit(editor, key);
		} else if (key == '\b') {
			zui_number_editor_backspace(editor);
		} else if (key == '-') {
			zui_number_editor_toggle_sign(editor);
		} else if (key == '\r') {
			return zui_number_editor_submit(editor) == 0;
		}
		(void)zui_component_request_redraw(editor->screen);
		return true;
	}

	return false;
}

static const struct zui_screen_ops zui_number_editor_screen_ops = {
	.draw = zui_number_editor_draw,
	.input = zui_number_editor_input,
};

struct zui_number_editor *zui_number_editor_create(const struct zui_number_editor_config *config)
{
	struct zui_number_editor *editor;

	if (config == NULL) {
		return NULL;
	}

	editor = zui_calloc(1U, sizeof(*editor));
	if (editor == NULL) {
		return NULL;
	}

	if (zui_number_editor_update(editor, config) != 0) {
		zui_number_editor_destroy(editor);
		return NULL;
	}

	editor->screen = zui_screen_create(&zui_number_editor_screen_ops, editor);
	if (editor->screen == NULL) {
		zui_number_editor_destroy(editor);
		return NULL;
	}

	return editor;
}

void zui_number_editor_destroy(struct zui_number_editor *editor)
{
	if (editor == NULL) {
		return;
	}

	zui_screen_destroy(editor->screen);
	zui_free(editor);
}

struct zui_screen *zui_number_editor_get_screen(struct zui_number_editor *editor)
{
	return editor == NULL ? NULL : editor->screen;
}

int zui_number_editor_update(struct zui_number_editor *editor,
			    const struct zui_number_editor_config *config)
{
	if (editor == NULL || config == NULL || config->min_value > config->max_value) {
		return -EINVAL;
	}

	editor->config = *config;
	editor->config.value = zui_number_editor_clamp(editor, editor->config.value);
	zui_number_editor_sync_text(editor);
	zui_number_editor_skip_hidden_sign(editor, true);
	return zui_component_request_redraw(editor->screen);
}

int64_t zui_number_editor_value(const struct zui_number_editor *editor)
{
	return editor == NULL ? 0 : editor->config.value;
}

int zui_number_editor_set_value(struct zui_number_editor *editor, int64_t value)
{
	if (editor == NULL) {
		return -EINVAL;
	}
	if (editor->config.unsigned_only && value < 0) {
		return -ERANGE;
	}

	editor->config.value = zui_number_editor_clamp(editor, value);
	zui_number_editor_sync_text(editor);
	return zui_component_request_redraw(editor->screen);
}

int zui_number_editor_step(struct zui_number_editor *editor, int64_t delta)
{
	if (editor == NULL) {
		return -EINVAL;
	}
	if ((delta > 0 && editor->config.value > INT64_MAX - delta) ||
	    (delta < 0 && editor->config.value < INT64_MIN - delta)) {
		return -ERANGE;
	}

	return zui_number_editor_set_value(editor, editor->config.value + delta);
}

int zui_number_editor_submit(struct zui_number_editor *editor)
{
	int64_t value;
	int rc;

	if (editor == NULL) {
		return -EINVAL;
	}

	rc = zui_number_editor_parse_text(editor, &value);
	if (rc != 0) {
		return rc;
	}

	editor->config.value = value;
	if (editor->config.submitted != NULL) {
		editor->config.submitted(editor, value, editor->config.user_data);
	}

	return 0;
}
