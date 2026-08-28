/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include "internal.h"

#include <ctype.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/zui/core.h>
#include <zephyr/zui/predictive.h>
#include <zephyr/zui/toast.h>

enum zui_text_editor_input_mode {
	ZUI_TEXT_EDITOR_INPUT_VIRTUAL_ALPHA,
	ZUI_TEXT_EDITOR_INPUT_VIRTUAL_SYMBOL,
	ZUI_TEXT_EDITOR_INPUT_HARDWARE_TITLE,
	ZUI_TEXT_EDITOR_INPUT_HARDWARE_LOWER,
	ZUI_TEXT_EDITOR_INPUT_HARDWARE_UPPER,
	ZUI_TEXT_EDITOR_INPUT_HARDWARE_NUMBER,
#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
	ZUI_TEXT_EDITOR_INPUT_PREDICTIVE,
#endif
	ZUI_TEXT_EDITOR_INPUT_MODE_COUNT,
};

struct zui_text_editor {
	struct zui_text_editor_config config;
	zui_text_editor_validate_cb validator;
	void *validator_user_data;
	size_t cursor;
	uint8_t selected_row;
	uint8_t selected_column;
	enum zui_text_editor_input_mode input_mode;
	bool input_focus;
	bool clear_default_text;
	char multi_tap_key;
	uint8_t multi_tap_index;
	size_t multi_tap_cursor;
	uint32_t multi_tap_time_ms;
#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
	char predictive_sequence[CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_MAX_SEQUENCE_LEN + 1U];
	uint8_t predictive_len;
	uint8_t predictive_display_sequence_len;
	uint8_t predictive_pending_slots;
	uint8_t predictive_candidate_index;
	size_t predictive_start;
	size_t predictive_word_len;
	bool predictive_active;
#endif
	uint32_t validator_toast_id;
	struct zui_host *validator_toast_host;
	char validator_text[64];
	struct zui_screen *screen;
};

#define ZUI_TEXT_EDITOR_ENTER_KEY     '\r'
#define ZUI_TEXT_EDITOR_BACKSPACE_KEY '\b'
#define ZUI_TEXT_EDITOR_TOGGLE_KEY    '\t'
#define ZUI_TEXT_EDITOR_SPACE_KEY     ((char)0x1f)

struct zui_text_editor_key {
	char text;
	uint8_t x;
	uint8_t y;
};

static void zui_text_editor_show_validator_toast(struct zui_text_editor *editor);
static void zui_text_editor_multi_tap_reset(struct zui_text_editor *editor);
#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
static void zui_text_editor_predictive_reset(struct zui_text_editor *editor);
#endif

static bool zui_text_editor_input_mode_is_virtual(enum zui_text_editor_input_mode mode)
{
	return mode == ZUI_TEXT_EDITOR_INPUT_VIRTUAL_ALPHA ||
	       mode == ZUI_TEXT_EDITOR_INPUT_VIRTUAL_SYMBOL;
}

static bool zui_text_editor_uses_virtual_keyboard(const struct zui_text_editor *editor)
{
	return editor != NULL && zui_text_editor_input_mode_is_virtual(editor->input_mode);
}

static bool zui_text_editor_uses_symbol_keyboard(const struct zui_text_editor *editor)
{
	return editor != NULL && editor->input_mode == ZUI_TEXT_EDITOR_INPUT_VIRTUAL_SYMBOL;
}

static const char *zui_text_editor_input_mode_label(enum zui_text_editor_input_mode mode)
{
	switch (mode) {
	case ZUI_TEXT_EDITOR_INPUT_VIRTUAL_ALPHA:
		return "a";
	case ZUI_TEXT_EDITOR_INPUT_VIRTUAL_SYMBOL:
		return "#";
	case ZUI_TEXT_EDITOR_INPUT_HARDWARE_TITLE:
		return "Abc";
	case ZUI_TEXT_EDITOR_INPUT_HARDWARE_LOWER:
		return "abc";
	case ZUI_TEXT_EDITOR_INPUT_HARDWARE_UPPER:
		return "ABC";
	case ZUI_TEXT_EDITOR_INPUT_HARDWARE_NUMBER:
		return "123";
#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
	case ZUI_TEXT_EDITOR_INPUT_PREDICTIVE:
		return "T9";
#endif
	default:
		return "?";
	}
}

static char zui_text_editor_input_mode_icon(const struct zui_text_editor *editor)
{
#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
	if (editor != NULL && editor->input_mode == ZUI_TEXT_EDITOR_INPUT_PREDICTIVE) {
		return 'p';
	}
#endif

	return 'm';
}

static char zui_text_editor_virtual_toggle_glyph(const struct zui_text_editor *editor)
{
	return zui_text_editor_uses_symbol_keyboard(editor) ? 'a' : '#';
}

static const struct zui_text_editor_key zui_text_editor_keys_row_1[] = {
	{'q', 1, 8},  {'w', 10, 8},  {'e', 19, 8},  {'r', 28, 8},  {'t', 37, 8},
	{'y', 46, 8}, {'u', 55, 8},  {'i', 64, 8},  {'o', 73, 8},  {'p', 82, 8},
	{'0', 92, 8}, {'1', 102, 8}, {'2', 111, 8}, {'3', 120, 8},
};

static const struct zui_text_editor_key zui_text_editor_keys_row_2[] = {
	{'a', 1, 20},   {'s', 10, 20},
	{'d', 19, 20},  {'f', 28, 20},
	{'g', 37, 20},  {'h', 46, 20},
	{'j', 55, 20},  {'k', 64, 20},
	{'l', 73, 20},  {ZUI_TEXT_EDITOR_BACKSPACE_KEY, 82, 12},
	{'4', 102, 20}, {'5', 111, 20},
	{'6', 120, 20},
};

static const struct zui_text_editor_key zui_text_editor_keys_row_3[] = {
	{ZUI_TEXT_EDITOR_TOGGLE_KEY, 1, 32},
	{'z', 12, 32},
	{'x', 20, 32},
	{'c', 28, 32},
	{'v', 35, 32},
	{'b', 43, 32},
	{'n', 51, 32},
	{'m', 59, 32},
	{ZUI_TEXT_EDITOR_SPACE_KEY, 66, 32},
	{ZUI_TEXT_EDITOR_ENTER_KEY, 74, 23},
	{'7', 102, 32},
	{'8', 111, 32},
	{'9', 120, 32},
};

static const struct zui_text_editor_key zui_text_editor_sym_row_1[] = {
	{'!', 2, 8},   {'@', 12, 8},  {'#', 22, 8},  {'$', 32, 8}, {'%', 42, 8},
	{'^', 52, 8},  {'&', 62, 8},  {'(', 72, 8},  {')', 82, 8}, {'_', 92, 8},
	{'<', 102, 8}, {'>', 111, 8}, {':', 120, 8},
};

static const struct zui_text_editor_key zui_text_editor_sym_row_2[] = {
	{'~', 2, 20},   {'+', 12, 20},  {'-', 22, 20},
	{'=', 32, 20},  {'[', 42, 20},  {']', 52, 20},
	{'{', 62, 20},  {'}', 72, 20},  {ZUI_TEXT_EDITOR_BACKSPACE_KEY, 82, 12},
	{'"', 102, 20}, {'/', 111, 20}, {'\\', 120, 20},
};

static const struct zui_text_editor_key zui_text_editor_sym_row_3[] = {
	{ZUI_TEXT_EDITOR_TOGGLE_KEY, 1, 32},
	{'.', 15, 32},
	{',', 29, 32},
	{';', 41, 32},
	{'`', 53, 32},
	{'\'', 65, 32},
	{ZUI_TEXT_EDITOR_ENTER_KEY, 74, 23},
	{'|', 102, 32},
	{'?', 111, 32},
	{'*', 120, 32},
};

static const struct zui_text_editor_key *zui_text_editor_row(const struct zui_text_editor *editor,
							     uint8_t row, size_t *count)
{
	bool symbol_mode = zui_text_editor_uses_symbol_keyboard(editor);

	switch (row) {
	case 0:
		*count = symbol_mode ? ARRAY_SIZE(zui_text_editor_sym_row_1)
				     : ARRAY_SIZE(zui_text_editor_keys_row_1);
		return symbol_mode ? zui_text_editor_sym_row_1 : zui_text_editor_keys_row_1;
	case 1:
		*count = symbol_mode ? ARRAY_SIZE(zui_text_editor_sym_row_2)
				     : ARRAY_SIZE(zui_text_editor_keys_row_2);
		return symbol_mode ? zui_text_editor_sym_row_2 : zui_text_editor_keys_row_2;
	default:
		*count = symbol_mode ? ARRAY_SIZE(zui_text_editor_sym_row_3)
				     : ARRAY_SIZE(zui_text_editor_keys_row_3);
		return symbol_mode ? zui_text_editor_sym_row_3 : zui_text_editor_keys_row_3;
	}
}

static uint8_t zui_text_editor_key_width(char key)
{
	switch (key) {
	case ZUI_TEXT_EDITOR_ENTER_KEY:
		return 24U;
	case ZUI_TEXT_EDITOR_BACKSPACE_KEY:
		return 16U;
	case ZUI_TEXT_EDITOR_TOGGLE_KEY:
		return 9U;
	default:
		return 7U;
	}
}

static uint8_t zui_text_editor_key_center_x(const struct zui_text_editor_key *key)
{
	return key == NULL ? 0U : key->x + zui_text_editor_key_width(key->text) / 2U;
}

static uint8_t zui_text_editor_selected_center_x(const struct zui_text_editor *editor)
{
	const struct zui_text_editor_key *row;
	size_t row_count;

	row = zui_text_editor_row(editor, editor->selected_row, &row_count);
	if (editor->selected_column >= row_count) {
		return 0U;
	}

	return zui_text_editor_key_center_x(&row[editor->selected_column]);
}

static void zui_text_editor_select_nearest_column(struct zui_text_editor *editor,
						  uint8_t row_index, uint8_t target_x)
{
	const struct zui_text_editor_key *row;
	size_t row_count;
	uint8_t best_column = 0U;
	uint8_t best_distance = UINT8_MAX;

	row = zui_text_editor_row(editor, row_index, &row_count);
	for (size_t column = 0U; column < row_count; column++) {
		uint8_t center = zui_text_editor_key_center_x(&row[column]);
		uint8_t distance = center > target_x ? center - target_x : target_x - center;

		if (distance < best_distance) {
			best_column = column;
			best_distance = distance;
		}
	}

	editor->selected_column = best_column;
}

static void zui_text_editor_set_input_mode(struct zui_text_editor *editor,
					   enum zui_text_editor_input_mode mode)
{
	bool was_virtual;
	uint8_t target_x = 0U;

	if (editor == NULL || mode >= ZUI_TEXT_EDITOR_INPUT_MODE_COUNT) {
		return;
	}

	was_virtual = zui_text_editor_uses_virtual_keyboard(editor);
	if (was_virtual) {
		target_x = zui_text_editor_selected_center_x(editor);
	}

	editor->input_mode = mode;
	zui_text_editor_multi_tap_reset(editor);
#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
	zui_text_editor_predictive_reset(editor);
#endif

	if (zui_text_editor_uses_virtual_keyboard(editor)) {
		editor->input_focus = false;
		if (was_virtual) {
			zui_text_editor_select_nearest_column(editor, editor->selected_row, target_x);
		} else {
			editor->selected_row = 2U;
			editor->selected_column = 0U;
		}
	} else {
		editor->input_focus = true;
	}
}

static void zui_text_editor_toggle_virtual_input_mode(struct zui_text_editor *editor)
{
	if (editor == NULL) {
		return;
	}

	zui_text_editor_set_input_mode(editor, zui_text_editor_uses_symbol_keyboard(editor)
						       ? ZUI_TEXT_EDITOR_INPUT_VIRTUAL_ALPHA
						       : ZUI_TEXT_EDITOR_INPUT_VIRTUAL_SYMBOL);
}

static bool zui_text_editor_next_keypad_input_mode(struct zui_text_editor *editor)
{
	enum zui_text_editor_input_mode mode;

	if (editor == NULL) {
		return false;
	}

	switch (editor->input_mode) {
	case ZUI_TEXT_EDITOR_INPUT_HARDWARE_TITLE:
		if (IS_ENABLED(CONFIG_ZUI_TEXT_EDITOR_KEYPAD_MULTI_TAP)) {
			mode = ZUI_TEXT_EDITOR_INPUT_HARDWARE_LOWER;
		} else {
#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
			mode = ZUI_TEXT_EDITOR_INPUT_PREDICTIVE;
#else
			mode = ZUI_TEXT_EDITOR_INPUT_VIRTUAL_ALPHA;
#endif
		}
		break;
	case ZUI_TEXT_EDITOR_INPUT_HARDWARE_LOWER:
		if (IS_ENABLED(CONFIG_ZUI_TEXT_EDITOR_KEYPAD_MULTI_TAP)) {
			mode = ZUI_TEXT_EDITOR_INPUT_HARDWARE_UPPER;
		} else {
#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
			mode = ZUI_TEXT_EDITOR_INPUT_PREDICTIVE;
#else
			mode = ZUI_TEXT_EDITOR_INPUT_VIRTUAL_ALPHA;
#endif
		}
		break;
	case ZUI_TEXT_EDITOR_INPUT_HARDWARE_UPPER:
		if (IS_ENABLED(CONFIG_ZUI_TEXT_EDITOR_KEYPAD_MULTI_TAP)) {
			mode = ZUI_TEXT_EDITOR_INPUT_HARDWARE_NUMBER;
		} else {
#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
			mode = ZUI_TEXT_EDITOR_INPUT_PREDICTIVE;
#else
			mode = ZUI_TEXT_EDITOR_INPUT_VIRTUAL_ALPHA;
#endif
		}
		break;
	case ZUI_TEXT_EDITOR_INPUT_HARDWARE_NUMBER:
#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
		if (IS_ENABLED(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)) {
			mode = ZUI_TEXT_EDITOR_INPUT_PREDICTIVE;
			break;
		}
#endif
		mode = ZUI_TEXT_EDITOR_INPUT_VIRTUAL_ALPHA;
		break;
#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
	case ZUI_TEXT_EDITOR_INPUT_PREDICTIVE:
#endif
		mode = ZUI_TEXT_EDITOR_INPUT_VIRTUAL_ALPHA;
		break;
	case ZUI_TEXT_EDITOR_INPUT_VIRTUAL_ALPHA:
	case ZUI_TEXT_EDITOR_INPUT_VIRTUAL_SYMBOL:
	default:
		if (IS_ENABLED(CONFIG_ZUI_TEXT_EDITOR_KEYPAD_MULTI_TAP)) {
			mode = ZUI_TEXT_EDITOR_INPUT_HARDWARE_TITLE;
#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
		} else if (IS_ENABLED(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)) {
			mode = ZUI_TEXT_EDITOR_INPUT_PREDICTIVE;
#endif
		} else {
			return false;
		}
		break;
	}

	zui_text_editor_set_input_mode(editor, mode);
	return true;
}

static char zui_text_editor_shifted(char ch)
{
	switch (ch) {
	case '1':
		return '!';
	case '2':
		return '@';
	case '3':
		return '#';
	case '4':
		return '$';
	case '5':
		return '%';
	case '6':
		return '^';
	case '7':
		return '&';
	case '8':
		return '*';
	case '9':
		return '(';
	case '0':
		return ')';
	default:
		return islower((unsigned char)ch) ? (char)toupper((unsigned char)ch) : ch;
	}
}

static void zui_text_editor_cursor_clamp(struct zui_text_editor *editor)
{
	size_t len;

	if (editor == NULL || editor->config.buffer == NULL) {
		return;
	}

	len = strlen(editor->config.buffer);
	editor->cursor = MIN(editor->cursor, len);
}

static void zui_text_editor_multi_tap_reset(struct zui_text_editor *editor)
{
	if (editor == NULL) {
		return;
	}

	editor->multi_tap_key = '\0';
	editor->multi_tap_index = 0U;
	editor->multi_tap_cursor = 0U;
	editor->multi_tap_time_ms = 0U;
}

static void zui_text_editor_enter(void *user_data)
{
	struct zui_text_editor *editor = user_data;

	if (editor != NULL && editor->config.clear_on_enter && editor->config.buffer != NULL &&
	    editor->config.buffer_size > 0U) {
		editor->config.buffer[0] = '\0';
		editor->cursor = 0U;
		zui_text_editor_multi_tap_reset(editor);
#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
		zui_text_editor_predictive_reset(editor);
#endif
	}
}

static bool zui_text_editor_insert(struct zui_text_editor *editor, char ch)
{
	size_t len;

	if (editor == NULL || editor->config.buffer == NULL || editor->config.buffer_size == 0U) {
		return false;
	}

	if (editor->clear_default_text) {
		editor->config.buffer[0] = '\0';
		editor->cursor = 0U;
	}

	len = strlen(editor->config.buffer);
	zui_text_editor_cursor_clamp(editor);
	if (len + 1U >= editor->config.buffer_size) {
		return false;
	}

	memmove(&editor->config.buffer[editor->cursor + 1U], &editor->config.buffer[editor->cursor],
		len - editor->cursor + 1U);
	editor->config.buffer[editor->cursor] = ch;
	editor->cursor++;
	editor->clear_default_text = false;
	if (editor->config.changed != NULL) {
		editor->config.changed(editor, editor->config.buffer, editor->config.user_data);
	}
	return true;
}

#if defined(CONFIG_ZUI_TEXT_EDITOR_KEYPAD_MULTI_TAP) || defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
static bool zui_text_editor_replace_previous(struct zui_text_editor *editor, char ch)
{
	if (editor == NULL || editor->config.buffer == NULL) {
		return false;
	}

	zui_text_editor_cursor_clamp(editor);
	if (editor->cursor == 0U || editor->cursor > strlen(editor->config.buffer)) {
		return false;
	}

	editor->config.buffer[editor->cursor - 1U] = ch;
	if (editor->config.changed != NULL) {
		editor->config.changed(editor, editor->config.buffer, editor->config.user_data);
	}
	return true;
}
#endif

#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
static bool zui_text_editor_replace_range(struct zui_text_editor *editor, size_t start,
					  size_t old_len, const char *text)
{
	size_t len;
	size_t new_len;

	if (editor == NULL || editor->config.buffer == NULL || text == NULL) {
		return false;
	}

	len = strlen(editor->config.buffer);
	if (start > len) {
		return false;
	}

	old_len = MIN(old_len, len - start);
	new_len = strlen(text);
	if (len - old_len + new_len >= editor->config.buffer_size) {
		return false;
	}

	memmove(&editor->config.buffer[start + new_len], &editor->config.buffer[start + old_len],
		len - start - old_len + 1U);
	memcpy(&editor->config.buffer[start], text, new_len);
	editor->cursor = start + new_len;
	editor->clear_default_text = false;
	if (editor->config.changed != NULL) {
		editor->config.changed(editor, editor->config.buffer, editor->config.user_data);
	}

	return true;
}
#endif

static bool zui_text_editor_backspace(struct zui_text_editor *editor)
{
	size_t len;

	if (editor == NULL || editor->config.buffer == NULL) {
		return false;
	}
	if (editor->clear_default_text) {
		editor->config.buffer[0] = '\0';
		editor->cursor = 0U;
		editor->clear_default_text = false;
		zui_text_editor_multi_tap_reset(editor);
#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
		zui_text_editor_predictive_reset(editor);
#endif
		return true;
	}

	len = strlen(editor->config.buffer);
	zui_text_editor_cursor_clamp(editor);
	if (len == 0U || editor->cursor == 0U) {
		return false;
	}

	memmove(&editor->config.buffer[editor->cursor - 1U], &editor->config.buffer[editor->cursor],
		len - editor->cursor + 1U);
	editor->cursor--;
	zui_text_editor_multi_tap_reset(editor);
#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
	zui_text_editor_predictive_reset(editor);
#endif
	if (editor->config.changed != NULL) {
		editor->config.changed(editor, editor->config.buffer, editor->config.user_data);
	}
	return true;
}

static size_t zui_text_editor_text_len(const struct zui_text_editor *editor)
{
	if (editor == NULL || editor->config.buffer == NULL) {
		return 0U;
	}

	return strlen(editor->config.buffer);
}

#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
static uint16_t zui_text_editor_text_range_width(struct zui_draw_ctx *draw, const char *text,
						 size_t start, size_t len)
{
	char slice[80];
	size_t n;

	if (draw == NULL || text == NULL || len == 0U) {
		return 0U;
	}

	n = MIN(len, sizeof(slice) - 1U);
	memcpy(slice, &text[start], n);
	slice[n] = '\0';

	return zui_draw_text_width(draw, slice);
}
#endif

#if defined(CONFIG_ZUI_TEXT_EDITOR_KEYPAD_MULTI_TAP) || defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
static const char *zui_text_editor_multi_tap_chars(char key)
{
	switch (key) {
	case '0':
		return " 0";
	case '1':
		return ".,!?1";
	case '2':
		return "abc2";
	case '3':
		return "def3";
	case '4':
		return "ghi4";
	case '5':
		return "jkl5";
	case '6':
		return "mno6";
	case '7':
		return "pqrs7";
	case '8':
		return "tuv8";
	case '9':
		return "wxyz9";
	default:
		return NULL;
	}
}

static char zui_text_editor_multi_tap_transform(const struct zui_text_editor *editor, char ch,
						bool replacing)
{
	if (editor == NULL || !isalpha((unsigned char)ch)) {
		return ch;
	}

	if (editor->input_mode == ZUI_TEXT_EDITOR_INPUT_HARDWARE_LOWER) {
		return (char)tolower((unsigned char)ch);
	}
	if (editor->input_mode == ZUI_TEXT_EDITOR_INPUT_HARDWARE_UPPER) {
		return (char)toupper((unsigned char)ch);
	}
	if ((!replacing && (strlen(editor->config.buffer) == 0U || editor->clear_default_text)) ||
	    (replacing && editor->cursor == 1U)) {
		return (char)toupper((unsigned char)ch);
	}

	return ch;
}

static bool zui_text_editor_multi_tap(struct zui_text_editor *editor, char key)
{
	const char *chars;
	uint32_t now_ms;
	bool replacing;
	char ch;

	if (editor == NULL) {
		return false;
	}

	if (key == '*') {
		(void)zui_text_editor_backspace(editor);
		return true;
	}

	chars = zui_text_editor_multi_tap_chars(key);
	if (chars == NULL) {
		return false;
	}

	now_ms = k_uptime_get_32();
	zui_text_editor_cursor_clamp(editor);
	replacing = editor->multi_tap_key == key &&
		    (now_ms - editor->multi_tap_time_ms) <= CONFIG_ZUI_KEYPAD_MULTI_TAP_TIMEOUT_MS &&
		    editor->cursor == editor->multi_tap_cursor && editor->cursor > 0U &&
		    !editor->clear_default_text;
	if (replacing) {
		editor->multi_tap_index = (editor->multi_tap_index + 1U) % strlen(chars);
		ch = zui_text_editor_multi_tap_transform(editor, chars[editor->multi_tap_index],
							 true);
		if (!zui_text_editor_replace_previous(editor, ch)) {
			zui_text_editor_multi_tap_reset(editor);
			return true;
		}
	} else {
		editor->multi_tap_index = 0U;
		ch = zui_text_editor_multi_tap_transform(editor, chars[0], false);
		if (!zui_text_editor_insert(editor, ch)) {
			zui_text_editor_multi_tap_reset(editor);
			return true;
		}
	}

	editor->multi_tap_key = key;
	editor->multi_tap_cursor = editor->cursor;
	editor->multi_tap_time_ms = now_ms;
	return true;
}
#endif

static bool zui_text_editor_keypad_submit(struct zui_text_editor *editor)
{
	int rc;

	if (editor == NULL) {
		return false;
	}

	rc = zui_text_editor_submit(editor);
	if (rc == -EINVAL) {
		zui_text_editor_show_validator_toast(editor);
	}

	return rc == 0;
}

#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
static void zui_text_editor_predictive_reset(struct zui_text_editor *editor)
{
	if (editor == NULL) {
		return;
	}

	editor->predictive_sequence[0] = '\0';
	editor->predictive_len = 0U;
	editor->predictive_display_sequence_len = 0U;
	editor->predictive_pending_slots = 0U;
	editor->predictive_candidate_index = 0U;
	editor->predictive_start = 0U;
	editor->predictive_word_len = 0U;
	editor->predictive_active = false;
}

static bool zui_text_editor_predictive_mode(const struct zui_text_editor *editor)
{
	return editor != NULL && editor->input_mode == ZUI_TEXT_EDITOR_INPUT_PREDICTIVE;
}

static void zui_text_editor_predictive_commit(struct zui_text_editor *editor)
{
	zui_text_editor_predictive_reset(editor);
}

static bool zui_text_editor_predictive_replacement(struct zui_text_editor *editor,
						   const char *sequence, char *replacement,
						   size_t replacement_size)
{
	const char *candidate;
	size_t candidate_count;

	if (editor == NULL || sequence == NULL || replacement == NULL || replacement_size == 0U) {
		return false;
	}

	candidate_count = zui_predictive_candidate_count(sequence);
	if (candidate_count == 0U) {
		return false;
	}

	if (editor->predictive_candidate_index >= candidate_count) {
		editor->predictive_candidate_index = 0U;
	}

	candidate = zui_predictive_candidate(sequence, editor->predictive_candidate_index);
	if (candidate == NULL) {
		return false;
	}

	if (zui_component_copy_text(replacement, replacement_size, candidate) != 0) {
		return false;
	}
	if (editor->predictive_start == 0U && isalpha((unsigned char)replacement[0])) {
		replacement[0] = (char)toupper((unsigned char)replacement[0]);
	}

	return true;
}

static bool zui_text_editor_predictive_display_sequence(struct zui_text_editor *editor,
							char *sequence, size_t sequence_size,
							uint8_t *sequence_len)
{
	if (editor == NULL || sequence == NULL || sequence_size == 0U || sequence_len == NULL) {
		return false;
	}

	for (uint8_t len = editor->predictive_len; len > 0U; len--) {
		if (len >= sequence_size) {
			continue;
		}
		memcpy(sequence, editor->predictive_sequence, len);
		sequence[len] = '\0';
		if (zui_predictive_candidate_count(sequence) > 0U) {
			*sequence_len = len;
			return true;
		}
	}

	sequence[0] = '\0';
	*sequence_len = 0U;
	return false;
}

static bool zui_text_editor_predictive_apply(struct zui_text_editor *editor)
{
	char replacement[CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_MAX_SEQUENCE_LEN + 1U];
	char display_sequence[CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_MAX_SEQUENCE_LEN + 1U];
	uint8_t display_sequence_len = 0U;
	size_t replacement_len;

	if (editor == NULL || !editor->predictive_active) {
		return false;
	}

	if (editor->predictive_len == 0U) {
		bool replaced = zui_text_editor_replace_range(editor, editor->predictive_start,
							      editor->predictive_word_len, "");

		zui_text_editor_predictive_reset(editor);
		return replaced;
	}

	if (zui_predictive_candidate_count(editor->predictive_sequence) == 0U) {
		if (!zui_predictive_has_sequence_prefix(editor->predictive_sequence)) {
			return false;
		}
		if (!zui_text_editor_predictive_display_sequence(editor, display_sequence,
								sizeof(display_sequence),
								&display_sequence_len)) {
			if (!zui_text_editor_replace_range(editor, editor->predictive_start,
							   editor->predictive_word_len, "")) {
				return false;
			}
			editor->predictive_word_len = 0U;
			editor->predictive_display_sequence_len = 0U;
			editor->predictive_pending_slots = editor->predictive_len;
			return true;
		}
	} else {
		memcpy(display_sequence, editor->predictive_sequence, editor->predictive_len + 1U);
		display_sequence_len = editor->predictive_len;
	}

	if (!zui_text_editor_predictive_replacement(editor, display_sequence, replacement,
						   sizeof(replacement))) {
		return false;
	}

	replacement_len = strlen(replacement);
	if (!zui_text_editor_replace_range(editor, editor->predictive_start,
					   editor->predictive_word_len, replacement)) {
		return false;
	}

	editor->predictive_word_len = replacement_len;
	editor->predictive_display_sequence_len = display_sequence_len;
	editor->predictive_pending_slots =
		(uint8_t)(editor->predictive_len - editor->predictive_display_sequence_len);
	return true;
}

static bool zui_text_editor_predictive_digit(struct zui_text_editor *editor, char key)
{
	if (editor == NULL || key < '2' || key > '9') {
		return false;
	}

	if (!editor->predictive_active) {
		if (editor->clear_default_text) {
			editor->config.buffer[0] = '\0';
			editor->cursor = 0U;
			editor->clear_default_text = false;
		}
		zui_text_editor_cursor_clamp(editor);
		editor->predictive_active = true;
		editor->predictive_start = editor->cursor;
		editor->predictive_word_len = 0U;
		editor->predictive_len = 0U;
		editor->predictive_display_sequence_len = 0U;
		editor->predictive_pending_slots = 0U;
		editor->predictive_candidate_index = 0U;
		editor->predictive_sequence[0] = '\0';
	}

	if (editor->predictive_len >= CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_MAX_SEQUENCE_LEN) {
		return true;
	}

	editor->predictive_sequence[editor->predictive_len++] = key;
	editor->predictive_sequence[editor->predictive_len] = '\0';
	editor->predictive_candidate_index = 0U;
	if (!zui_predictive_has_sequence_prefix(editor->predictive_sequence)) {
		editor->predictive_sequence[--editor->predictive_len] = '\0';
		if (editor->predictive_len == 0U && editor->predictive_word_len == 0U) {
			zui_text_editor_predictive_reset(editor);
		}
		return true;
	}
	if (!zui_text_editor_predictive_apply(editor)) {
		editor->predictive_sequence[--editor->predictive_len] = '\0';
		if (editor->predictive_len == 0U && editor->predictive_word_len == 0U) {
			zui_text_editor_predictive_reset(editor);
		}
		return true;
	}

	return true;
}

static bool zui_text_editor_predictive_cycle(struct zui_text_editor *editor, int direction)
{
	char sequence[CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_MAX_SEQUENCE_LEN + 1U];
	size_t candidate_count;
	uint8_t sequence_len;

	if (editor == NULL || !editor->predictive_active) {
		return true;
	}

	sequence_len = editor->predictive_display_sequence_len;
	if (sequence_len == 0U || sequence_len > editor->predictive_len) {
		sequence_len = editor->predictive_len;
	}
	if (sequence_len == 0U || sequence_len >= sizeof(sequence)) {
		return true;
	}

	memcpy(sequence, editor->predictive_sequence, sequence_len);
	sequence[sequence_len] = '\0';
	candidate_count = zui_predictive_candidate_count(sequence);
	if (candidate_count <= 1U) {
		return true;
	}

	if (direction < 0) {
		editor->predictive_candidate_index =
			editor->predictive_candidate_index == 0U
				? (uint8_t)(candidate_count - 1U)
				: (uint8_t)(editor->predictive_candidate_index - 1U);
	} else {
		editor->predictive_candidate_index =
			(uint8_t)((editor->predictive_candidate_index + 1U) % candidate_count);
	}
	(void)zui_text_editor_predictive_apply(editor);
	return true;
}

static bool zui_text_editor_predictive_next(struct zui_text_editor *editor)
{
	return zui_text_editor_predictive_cycle(editor, 1);
}

static bool zui_text_editor_predictive_previous(struct zui_text_editor *editor)
{
	return zui_text_editor_predictive_cycle(editor, -1);
}

static bool zui_text_editor_predictive_accept_space(struct zui_text_editor *editor)
{
	if (editor == NULL) {
		return false;
	}

	if (editor->predictive_active) {
		zui_text_editor_predictive_commit(editor);
	}

	(void)zui_text_editor_insert(editor, ' ');
	return true;
}

static bool zui_text_editor_predictive_backspace(struct zui_text_editor *editor)
{
	if (editor == NULL || !editor->predictive_active) {
		return zui_text_editor_backspace(editor);
	}

	if (editor->predictive_len == 0U) {
		return true;
	}

	editor->predictive_sequence[--editor->predictive_len] = '\0';
	editor->predictive_candidate_index = 0U;
	(void)zui_text_editor_predictive_apply(editor);
	return true;
}
#endif

static bool zui_text_editor_hardware_keypad(struct zui_text_editor *editor, char key)
{
	if (editor == NULL) {
		return false;
	}

#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
	if (zui_text_editor_predictive_mode(editor)) {
		if (key == '.') {
			zui_text_editor_predictive_commit(editor);
			return zui_text_editor_next_keypad_input_mode(editor);
		}
		if (key == '*') {
			return zui_text_editor_predictive_backspace(editor);
		}
		if (key == '1') {
			if (editor->predictive_len == 0U) {
				return zui_text_editor_multi_tap(editor, key);
			}
			zui_text_editor_multi_tap_reset(editor);
			return zui_text_editor_predictive_next(editor);
		}
		if (key == '0') {
			zui_text_editor_multi_tap_reset(editor);
			return zui_text_editor_predictive_accept_space(editor);
		}
		zui_text_editor_multi_tap_reset(editor);
		return zui_text_editor_predictive_digit(editor, key);
	}
#endif

	if (key == '.') {
		return zui_text_editor_next_keypad_input_mode(editor);
	}
	if (key == '*') {
#if defined(CONFIG_ZUI_TEXT_EDITOR_KEYPAD_MULTI_TAP)
		(void)zui_text_editor_backspace(editor);
		return true;
#else
		return false;
#endif
	}
	if (!isdigit((unsigned char)key)) {
		return false;
	}
#if defined(CONFIG_ZUI_TEXT_EDITOR_KEYPAD_MULTI_TAP)
	if (editor->input_mode == ZUI_TEXT_EDITOR_INPUT_HARDWARE_NUMBER) {
		(void)zui_text_editor_insert(editor, key);
		zui_text_editor_multi_tap_reset(editor);
		return true;
	}

	return zui_text_editor_multi_tap(editor, key);
#else
	return false;
#endif
}

static bool zui_text_editor_save_enabled(const struct zui_text_editor *editor)
{
	return editor != NULL && editor->config.buffer != NULL &&
	       zui_text_editor_text_len(editor) >= editor->config.min_length;
}

static void zui_text_editor_dismiss_validator_toast(struct zui_text_editor *editor)
{
	if (editor == NULL || editor->validator_toast_id == 0U) {
		return;
	}

	(void)zui_toast_dismiss(editor->validator_toast_host, editor->validator_toast_id);
	editor->validator_toast_id = 0U;
	editor->validator_toast_host = NULL;
}

static void zui_text_editor_show_validator_toast(struct zui_text_editor *editor)
{
	struct zui_host *host;
	uint32_t id;

	if (editor == NULL) {
		return;
	}

	if (editor->validator_text[0] == '\0') {
		(void)snprintf(editor->validator_text, sizeof(editor->validator_text),
			       "Invalid input");
	}

	host = zui_get_default_host();
	if (host == NULL) {
		return;
	}

	zui_text_editor_dismiss_validator_toast(editor);
	id = zui_toast_show(host, &(struct zui_toast_config){
					  .title = "Invalid",
					  .text = editor->validator_text,
					  .timeout_ms = 1500U,
				  });
	if (id != 0U) {
		editor->validator_toast_id = id;
		editor->validator_toast_host = host;
	}
}

static void zui_text_editor_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct zui_text_editor *editor = user_data;
	const char *text =
		editor == NULL || editor->config.buffer == NULL ? "" : editor->config.buffer;
	size_t text_len = strlen(text);
	uint16_t field_width = zui_draw_width(draw) - 8U;
	uint8_t start_x = 4U;

	zui_draw_reset(draw);
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	if (editor != NULL && editor->config.title != NULL) {
		zui_draw_text(draw, (struct zui_point){.x = 2, .y = 8}, editor->config.title);
	}
	zui_draw_round_rect(draw, &(struct zui_rect){.x = 1, .y = 12, .width = 126, .height = 15},
			    1U);

	if (editor != NULL && editor->input_focus) {
		zui_draw_set_color(draw, ZUI_COLOR_XOR);
		zui_draw_box(draw, &(struct zui_rect){.x = 0, .y = 10, .width = 128, .height = 19});
		zui_draw_set_color(draw, ZUI_COLOR_WHITE);
	} else if (editor != NULL && editor->clear_default_text) {
		uint16_t width = MIN((uint16_t)(zui_draw_text_width(draw, text) + 2U), 124U);

		zui_draw_round_box(draw,
				   &(struct zui_rect){.x = (int16_t)(start_x - 1U),
						      .y = 14,
						      .width = width,
						      .height = 10},
				   1U);
		zui_draw_set_color(draw, ZUI_COLOR_WHITE);
	}

	zui_text_editor_cursor_clamp(editor);
	if (editor != NULL && text != NULL) {
		char slice[80];
		size_t left = 0U;
		size_t right = text_len;
		size_t cursor = editor == NULL ? text_len : editor->cursor;
		uint16_t ellipsis_width = zui_draw_text_width(draw, "...");
		uint16_t prefix_width = 0U;
		uint16_t suffix_width = 0U;
		uint16_t cursor_gap = 2U;
		uint16_t x = start_x;

		while ((right - left) >= sizeof(slice)) {
			if ((cursor - left) > (right - cursor) && left < cursor) {
				left++;
			} else if (right > cursor) {
				right--;
			} else {
				break;
			}
		}

		for (;;) {
			size_t n = MIN(right - left, sizeof(slice) - 1U);
			uint32_t total_width;

			memcpy(slice, &text[left], n);
			slice[n] = '\0';
			prefix_width = left > 0U ? ellipsis_width : 0U;
			suffix_width = right < text_len ? ellipsis_width : 0U;
			total_width = prefix_width + zui_draw_text_width(draw, slice) + cursor_gap +
				      suffix_width;
			if (total_width <= field_width) {
				break;
			}
			if ((cursor - left) > (right - cursor) && left < cursor) {
				left++;
			} else if (right > cursor) {
				right--;
			} else if (left < cursor) {
				left++;
			} else {
				break;
			}
		}
		if (cursor < left) {
			cursor = left;
		}
		if (cursor > right) {
			cursor = right;
		}

		if (left > 0U) {
			zui_draw_text(draw, (struct zui_point){.x = (int16_t)x, .y = 22}, "...");
			x += prefix_width;
		}

		size_t n = MIN(cursor - left, sizeof(slice) - 1U);
		memcpy(slice, &text[left], n);
		slice[n] = '\0';
		zui_draw_text(draw, (struct zui_point){.x = (int16_t)x, .y = 22}, slice);
		uint16_t left_width = zui_draw_text_width(draw, slice);

		zui_draw_line(draw,
			      (struct zui_point){.x = (int16_t)(x + left_width), .y = 14},
			      (struct zui_point){.x = (int16_t)(x + left_width), .y = 24});
		if (editor != NULL && editor->input_focus) {
			zui_draw_line(
				draw,
				(struct zui_point){.x = (int16_t)(x + left_width + 1U),
						   .y = 14},
				(struct zui_point){.x = (int16_t)(x + left_width + 1U),
						   .y = 24});
		}

		n = MIN(right - cursor, sizeof(slice) - 1U);
		memcpy(slice, &text[cursor], n);
		slice[n] = '\0';
		zui_draw_text(draw,
			      (struct zui_point){.x = (int16_t)(x + left_width + cursor_gap),
						 .y = 22},
			      slice);
		if (right < text_len) {
			uint16_t tail_x = x + left_width + cursor_gap + zui_draw_text_width(draw, slice);

			zui_draw_text(draw, (struct zui_point){.x = (int16_t)tail_x, .y = 22},
				      "...");
		}
#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
		if (editor != NULL && editor->predictive_active &&
		    (editor->predictive_word_len > 0U || editor->predictive_pending_slots > 0U)) {
			size_t predictive_end =
				editor->predictive_start + editor->predictive_word_len;
			size_t underline_start = MAX(editor->predictive_start, left);
			size_t underline_end = MIN(predictive_end, right);
			uint16_t underline_x;
			uint16_t underline_width = 0U;

			if (editor->predictive_start <= right && predictive_end >= left) {
				underline_x =
					x + zui_text_editor_text_range_width(
						    draw, text, left, underline_start - left);
				if (underline_start >= cursor) {
					underline_x += cursor_gap;
				}

				if (underline_start < underline_end) {
					underline_width = zui_text_editor_text_range_width(
						draw, text, underline_start,
						underline_end - underline_start);
				}

				if (editor->predictive_pending_slots > 0U && predictive_end <= right) {
					if (predictive_end >= cursor) {
						underline_width += cursor_gap;
					}
					underline_width +=
						editor->predictive_pending_slots *
						zui_draw_text_width(draw, " ");
				}
				if (underline_width > 0U) {
					zui_draw_line(
						draw,
						(struct zui_point){.x = (int16_t)underline_x,
								   .y = 24},
						(struct zui_point){
							.x = (int16_t)(underline_x +
								       underline_width),
							.y = 24});
				}
			}
		}
#endif
	} else {
		zui_draw_line(draw, (struct zui_point){.x = start_x, .y = 14},
			      (struct zui_point){.x = start_x, .y = 24});
	}
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);

	zui_draw_set_font(draw, ZUI_FONT_KEYBOARD);
	if (editor != NULL && !zui_text_editor_uses_virtual_keyboard(editor)) {
		const char *label = zui_text_editor_input_mode_label(editor->input_mode);

		zui_draw_round_rect(draw,
				    &(struct zui_rect){.x = 1,
						       .y = 52,
						       .width = 9,
						       .height = 11},
				    1U);
		zui_draw_glyph(draw, (struct zui_point){.x = 3, .y = 60},
			       zui_text_editor_input_mode_icon(editor));
		zui_draw_text(draw, (struct zui_point){.x = 14, .y = 61}, label);
	} else if (editor != NULL) {
		for (uint8_t row_index = 0U; row_index < 3U; row_index++) {
			size_t row_count;
			const struct zui_text_editor_key *row =
				zui_text_editor_row(editor, row_index, &row_count);

			for (size_t column = 0U; column < row_count; column++) {
				const struct zui_text_editor_key *key = &row[column];
				bool selected = editor->selected_row == row_index &&
						editor->selected_column == column &&
						!editor->input_focus;

				if (key->text == ZUI_TEXT_EDITOR_ENTER_KEY) {
					bool save_enabled = zui_text_editor_save_enabled(editor);
					const struct zui_icon *icon = zui_asset_pack_icon_by_id(
						zui_asset_pack_default(),
						save_enabled
							? (selected ? ZUI_ASSET_ICON_KEY_SAVE_SELECTED
								    : ZUI_ASSET_ICON_KEY_SAVE)
							: (selected
								   ? ZUI_ASSET_ICON_KEY_SAVE_BLOCKED_SELECTED
								   : ZUI_ASSET_ICON_KEY_SAVE_BLOCKED));

					zui_draw_icon(
						draw,
						(struct zui_point){.x = (int16_t)(1U + key->x),
								   .y = (int16_t)(29U + key->y)},
						icon);
				} else if (key->text == ZUI_TEXT_EDITOR_BACKSPACE_KEY) {
					const struct zui_icon *icon = zui_asset_pack_icon_by_id(
						zui_asset_pack_default(),
						selected ? ZUI_ASSET_ICON_KEY_BACKSPACE_SELECTED
							 : ZUI_ASSET_ICON_KEY_BACKSPACE);

					zui_draw_icon(
						draw,
						(struct zui_point){.x = (int16_t)(1U + key->x),
								   .y = (int16_t)(29U + key->y)},
						icon);
				} else {
					char glyph = key->text;

					if (selected) {
						if (glyph == ZUI_TEXT_EDITOR_TOGGLE_KEY) {
							zui_draw_round_box(
								draw,
								&(struct zui_rect){
									.x = (int16_t)(1U + key->x -
										       1U),
									.y = (int16_t)(29U +
										       key->y - 9U),
									.width = 9,
									.height = 11},
								1U);
						} else {
							zui_draw_box(
								draw,
								&(struct zui_rect){
									.x = (int16_t)(1U + key->x -
										       1U),
									.y = (int16_t)(29U +
										       key->y - 8U),
									.width = 7,
									.height = 10});
						}
						zui_draw_set_color(draw, ZUI_COLOR_WHITE);
					}
					if (glyph == ZUI_TEXT_EDITOR_TOGGLE_KEY) {
						zui_draw_round_rect(
							draw,
							&(struct zui_rect){
								.x = (int16_t)(1U + key->x - 1U),
								.y = (int16_t)(29U + key->y - 9U),
								.width = 9,
								.height = 11},
							1U);
						zui_draw_glyph(
							draw,
							(struct zui_point){
								.x = (int16_t)(1U + key->x + 1U),
								.y = (int16_t)(29U + key->y +
									       (zui_text_editor_uses_symbol_keyboard(
											editor)
												? -1
												: 1))},
							zui_text_editor_virtual_toggle_glyph(editor));
						zui_draw_set_color(draw, ZUI_COLOR_BLACK);
						continue;
					} else if (glyph == ZUI_TEXT_EDITOR_SPACE_KEY) {
						if (zui_text_editor_uses_symbol_keyboard(editor)) {
							zui_draw_set_color(draw, ZUI_COLOR_BLACK);
							continue;
						}
						glyph = '_';
					} else if (!zui_text_editor_uses_symbol_keyboard(editor) &&
						   (editor->clear_default_text || text_len == 0U)) {
						glyph = (char)toupper((unsigned char)glyph);
					}
					zui_draw_glyph(
						draw,
						(struct zui_point){.x = (int16_t)(1U + key->x),
								   .y = (int16_t)(29U + key->y)},
						glyph);
					zui_draw_set_color(draw, ZUI_COLOR_BLACK);
				}
			}
		}
	}

}

static bool zui_text_editor_input(const struct zui_input_event *event, void *user_data)
{
	struct zui_text_editor *editor = user_data;
	size_t row_count;

	if (editor == NULL || event == NULL) {
		return false;
	}
	if (event->action != ZUI_INPUT_ACTION_CLICK &&
	    event->action != ZUI_INPUT_ACTION_LONG_PRESS) {
		return false;
	}
	if (event->code == ZUI_INPUT_CODE_KEYPAD) {
		bool handled = false;

		if (event->value == '.') {
			if (event->action == ZUI_INPUT_ACTION_CLICK) {
#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
				if (zui_text_editor_predictive_mode(editor)) {
					zui_text_editor_predictive_commit(editor);
				}
#endif
				handled = zui_text_editor_next_keypad_input_mode(editor);
			} else if (event->action == ZUI_INPUT_ACTION_LONG_PRESS) {
				handled = IS_ENABLED(CONFIG_ZUI_TEXT_EDITOR_KEYPAD_MULTI_TAP) ||
						  IS_ENABLED(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
						  ? zui_text_editor_keypad_submit(editor)
						  : false;
			}
		} else if (event->action == ZUI_INPUT_ACTION_CLICK &&
			   !zui_text_editor_uses_virtual_keyboard(editor)) {
			handled = zui_text_editor_hardware_keypad(editor, (char)event->value);
		}
		if (handled) {
			(void)zui_component_request_redraw(editor->screen);
			return true;
		}
		return false;
	}
	if (event->code == ZUI_INPUT_CODE_BACK &&
	    event->action == ZUI_INPUT_ACTION_LONG_PRESS) {
		return false;
	}
	zui_text_editor_multi_tap_reset(editor);
	if (!zui_text_editor_uses_virtual_keyboard(editor)) {
#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
		if (zui_text_editor_predictive_mode(editor) &&
		    event->code == ZUI_INPUT_CODE_BACK) {
			(void)zui_text_editor_predictive_backspace(editor);
			(void)zui_component_request_redraw(editor->screen);
			return true;
		}
		if (zui_text_editor_predictive_mode(editor) &&
		    (event->code == ZUI_INPUT_CODE_LEFT || event->code == ZUI_INPUT_CODE_RIGHT) &&
		    editor->predictive_active) {
			if (event->code == ZUI_INPUT_CODE_LEFT) {
				(void)zui_text_editor_predictive_previous(editor);
			} else {
				(void)zui_text_editor_predictive_next(editor);
			}
			(void)zui_component_request_redraw(editor->screen);
			return true;
		}
		if (zui_text_editor_predictive_mode(editor) && event->code == ZUI_INPUT_CODE_SELECT) {
			zui_text_editor_predictive_commit(editor);
		}
#endif
		if (event->code == ZUI_INPUT_CODE_LEFT || event->code == ZUI_INPUT_CODE_RIGHT) {
			size_t len = strlen(editor->config.buffer);

			if (event->code == ZUI_INPUT_CODE_LEFT) {
				editor->cursor = event->action == ZUI_INPUT_ACTION_LONG_PRESS
							 ? 0U
							 : (editor->cursor > 0U ? editor->cursor - 1U
									       : 0U);
			} else {
				editor->cursor = event->action == ZUI_INPUT_ACTION_LONG_PRESS
							 ? len
							 : MIN(editor->cursor + 1U, len);
			}
			(void)zui_component_request_redraw(editor->screen);
			return true;
		}
		if (event->code == ZUI_INPUT_CODE_BACK) {
			(void)zui_text_editor_backspace(editor);
			(void)zui_component_request_redraw(editor->screen);
			return true;
		}
		if (event->code == ZUI_INPUT_CODE_SELECT) {
			bool submitted = zui_text_editor_keypad_submit(editor);

			(void)zui_component_request_redraw(editor->screen);
			return submitted;
		}
		if (event->code == ZUI_INPUT_CODE_UP || event->code == ZUI_INPUT_CODE_DOWN) {
			return true;
		}
	}
	if (event->code == ZUI_INPUT_CODE_UP) {
		if (editor->input_focus) {
			return true;
		}
		if (editor->selected_row == 0U) {
			editor->input_focus = true;
			editor->clear_default_text = false;
		} else {
			uint8_t target_x = zui_text_editor_selected_center_x(editor);

			editor->selected_row--;
			zui_text_editor_select_nearest_column(editor, editor->selected_row,
							      target_x);
		}
		(void)zui_component_request_redraw(editor->screen);
		return true;
	}
	if (event->code == ZUI_INPUT_CODE_DOWN) {
		if (editor->input_focus) {
			editor->input_focus = false;
		} else if (editor->selected_row < 2U) {
			uint8_t target_x = zui_text_editor_selected_center_x(editor);

			editor->selected_row++;
			zui_text_editor_select_nearest_column(editor, editor->selected_row,
							      target_x);
		}
		(void)zui_component_request_redraw(editor->screen);
		return true;
	}
	if (event->code == ZUI_INPUT_CODE_LEFT) {
		if (editor->input_focus) {
			if (event->action == ZUI_INPUT_ACTION_LONG_PRESS) {
				editor->cursor = 0U;
			} else if (editor->cursor > 0U) {
				editor->cursor--;
			}
		} else if (editor->selected_column > 0U) {
			editor->selected_column--;
		} else {
			editor->selected_row =
				editor->selected_row == 0U ? 2U : editor->selected_row - 1U;
			(void)zui_text_editor_row(editor, editor->selected_row, &row_count);
			editor->selected_column = row_count - 1U;
		}
		(void)zui_component_request_redraw(editor->screen);
		return true;
	}
	if (event->code == ZUI_INPUT_CODE_RIGHT) {
		if (editor->input_focus) {
			size_t len = strlen(editor->config.buffer);

			if (event->action == ZUI_INPUT_ACTION_LONG_PRESS) {
				editor->cursor = len;
			} else if (editor->cursor < len) {
				editor->cursor++;
			}
		} else {
			(void)zui_text_editor_row(editor, editor->selected_row, &row_count);
			editor->selected_column++;
			if (editor->selected_column >= row_count) {
				editor->selected_row = (editor->selected_row + 1U) % 3U;
				editor->selected_column = 0U;
			}
		}
		(void)zui_component_request_redraw(editor->screen);
		return true;
	}
	if (event->code == ZUI_INPUT_CODE_BACK) {
		(void)zui_text_editor_backspace(editor);
		(void)zui_component_request_redraw(editor->screen);
		return true;
	}
	if (event->code == ZUI_INPUT_CODE_SELECT) {
		const struct zui_text_editor_key *row;
		char selected;

		if (editor->input_focus) {
			editor->input_focus = false;
			(void)zui_component_request_redraw(editor->screen);
			return true;
		}
		row = zui_text_editor_row(editor, editor->selected_row, &row_count);
		selected = row[editor->selected_column].text;
		if (selected == ZUI_TEXT_EDITOR_TOGGLE_KEY) {
			if (event->action == ZUI_INPUT_ACTION_LONG_PRESS) {
				selected = ' ';
			} else {
				zui_text_editor_toggle_virtual_input_mode(editor);
				(void)zui_component_request_redraw(editor->screen);
				return true;
			}
		} else if (selected == ZUI_TEXT_EDITOR_SPACE_KEY) {
			selected = ' ';
		} else if (selected == ZUI_TEXT_EDITOR_BACKSPACE_KEY) {
			(void)zui_text_editor_backspace(editor);
			(void)zui_component_request_redraw(editor->screen);
			return true;
		} else if (selected == ZUI_TEXT_EDITOR_ENTER_KEY) {
			int rc = zui_text_editor_submit(editor);

			if (rc == -EINVAL) {
				zui_text_editor_show_validator_toast(editor);
			}
			(void)zui_component_request_redraw(editor->screen);
			return rc == 0;
		} else if (event->action == ZUI_INPUT_ACTION_LONG_PRESS) {
			selected = zui_text_editor_shifted(selected);
		} else if (!zui_text_editor_uses_symbol_keyboard(editor) &&
			   (strlen(editor->config.buffer) == 0U || editor->clear_default_text)) {
			selected = (char)toupper((unsigned char)selected);
		}

		(void)zui_text_editor_insert(editor, selected);
		(void)zui_component_request_redraw(editor->screen);
		return true;
	}

	return false;
}

static const struct zui_screen_ops zui_text_editor_screen_ops = {
	.draw = zui_text_editor_draw,
	.input = zui_text_editor_input,
	.enter = zui_text_editor_enter,
};

struct zui_text_editor *zui_text_editor_create(const struct zui_text_editor_config *config)
{
	struct zui_text_editor *editor;

	if (config == NULL || config->buffer == NULL || config->buffer_size == 0U) {
		return NULL;
	}

	editor = zui_calloc(1U, sizeof(*editor));
	if (editor == NULL) {
		return NULL;
	}

	if (zui_text_editor_update(editor, config) != 0) {
		zui_text_editor_destroy(editor);
		return NULL;
	}

	editor->screen = zui_screen_create(&zui_text_editor_screen_ops, editor);
	if (editor->screen == NULL) {
		zui_text_editor_destroy(editor);
		return NULL;
	}

	return editor;
}

void zui_text_editor_destroy(struct zui_text_editor *editor)
{
	if (editor == NULL) {
		return;
	}

	zui_text_editor_dismiss_validator_toast(editor);
	zui_screen_destroy(editor->screen);
	zui_free(editor);
}

struct zui_screen *zui_text_editor_get_screen(struct zui_text_editor *editor)
{
	return editor == NULL ? NULL : editor->screen;
}

int zui_text_editor_update(struct zui_text_editor *editor,
			   const struct zui_text_editor_config *config)
{
	if (editor == NULL || config == NULL || config->buffer == NULL ||
	    config->buffer_size == 0U) {
		return -EINVAL;
	}

	editor->config = *config;
	editor->validator = config->validate;
	editor->validator_user_data = config->user_data;
	editor->config.buffer[config->buffer_size - 1U] = '\0';
	editor->cursor = strlen(editor->config.buffer);
	editor->selected_row = 0U;
	editor->selected_column = 0U;
	editor->input_mode = ZUI_TEXT_EDITOR_INPUT_VIRTUAL_ALPHA;
	editor->input_focus = false;
	editor->clear_default_text = config->clear_default_text;
	zui_text_editor_multi_tap_reset(editor);
#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
	zui_text_editor_predictive_reset(editor);
#endif
	zui_text_editor_dismiss_validator_toast(editor);
	editor->validator_text[0] = '\0';

	return zui_component_request_redraw(editor->screen);
}

const char *zui_text_editor_text(const struct zui_text_editor *editor)
{
	return editor == NULL ? NULL : editor->config.buffer;
}

int zui_text_editor_set_text(struct zui_text_editor *editor, const char *text)
{
	int rc;

	if (editor == NULL) {
		return -EINVAL;
	}

	rc = zui_component_copy_text(editor->config.buffer, editor->config.buffer_size, text);
	editor->cursor = strlen(editor->config.buffer);
	editor->clear_default_text = false;
	zui_text_editor_multi_tap_reset(editor);
#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
	zui_text_editor_predictive_reset(editor);
#endif
	if (editor->config.changed != NULL) {
		editor->config.changed(editor, editor->config.buffer, editor->config.user_data);
	}
	(void)zui_component_request_redraw(editor->screen);

	return rc;
}

int zui_text_editor_set_validator(struct zui_text_editor *editor,
				  zui_text_editor_validate_cb validate, void *user_data)
{
	if (editor == NULL) {
		return -EINVAL;
	}

	editor->validator = validate;
	editor->validator_user_data = user_data;
	return 0;
}

zui_text_editor_validate_cb zui_text_editor_get_validator(const struct zui_text_editor *editor,
							  void **user_data)
{
	if (editor == NULL) {
		if (user_data != NULL) {
			*user_data = NULL;
		}
		return NULL;
	}

	if (user_data != NULL) {
		*user_data = editor->validator_user_data;
	}

	return editor->validator;
}

int zui_text_editor_submit(struct zui_text_editor *editor)
{
	if (editor == NULL) {
		return -EINVAL;
	}
	editor->validator_text[0] = '\0';
	if (strlen(editor->config.buffer) < editor->config.min_length) {
		(void)snprintf(editor->validator_text, sizeof(editor->validator_text),
			       "Invalid input");
		return -EINVAL;
	}
	if (editor->validator != NULL &&
	    !editor->validator(editor->config.buffer, editor->validator_text,
			       sizeof(editor->validator_text), editor->validator_user_data)) {
		if (editor->validator_text[0] == '\0') {
			(void)snprintf(editor->validator_text, sizeof(editor->validator_text),
				       "Invalid input");
		}
		return -EINVAL;
	}

	if (editor->config.submitted != NULL) {
		editor->config.submitted(editor, editor->config.buffer, editor->config.user_data);
	}
	zui_text_editor_multi_tap_reset(editor);
#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
	zui_text_editor_predictive_reset(editor);
#endif

	return 0;
}

int zui_text_editor_validate_file(struct zui_text_editor *editor, const char *base_path,
				  const char *extension, const char *current_name)
{
	const char *text;

	ARG_UNUSED(base_path);
	ARG_UNUSED(extension);

	if (editor == NULL) {
		return -EINVAL;
	}

	text = editor->config.buffer;
	if (text[0] == '\0' || strcmp(text, ".") == 0 || strcmp(text, "..") == 0 ||
	    strchr(text, '/') != NULL || strchr(text, '\\') != NULL) {
		return -EINVAL;
	}
	if (current_name != NULL && strcmp(text, current_name) == 0) {
		return -EALREADY;
	}

	return 0;
}
