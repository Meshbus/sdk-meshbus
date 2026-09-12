/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ZUI shared public value types
 */

#ifndef MESHBUS_INCLUDE_ZUI_ZUI_TYPES_H_
#define MESHBUS_INCLUDE_ZUI_ZUI_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum zui_color {
	ZUI_COLOR_WHITE = 0,
	ZUI_COLOR_BLACK = 1,
	ZUI_COLOR_INVERT = 2,
	ZUI_COLOR_XOR = ZUI_COLOR_INVERT,
};

enum zui_font {
	ZUI_FONT_PRIMARY,
	ZUI_FONT_SECONDARY,
	ZUI_FONT_KEYBOARD,
	ZUI_FONT_BIG_NUMBERS,
	ZUI_FONT_COUNT,
	ZUI_FONT_TOTAL_NUMBER = ZUI_FONT_COUNT,
};

enum zui_align {
	ZUI_ALIGN_LEFT,
	ZUI_ALIGN_RIGHT,
	ZUI_ALIGN_TOP,
	ZUI_ALIGN_BOTTOM,
	ZUI_ALIGN_CENTER,
};

struct zui_point {
	int16_t x;
	int16_t y;
};

struct zui_rect {
	int16_t x;
	int16_t y;
	uint16_t width;
	uint16_t height;
};

struct zui_icon {
	const uint16_t width;
	const uint16_t height;
	const uint8_t frame_count;
	const uint8_t frame_rate;
	const uint8_t *const *frames;
	const uint16_t *frame_sizes;
};

struct zui_text_placement {
	const char *text;
	struct zui_point pos;
	enum zui_align horizontal;
	enum zui_align vertical;
};

struct zui_icon_placement {
	const struct zui_icon *icon;
	struct zui_point pos;
};

#ifdef __cplusplus
}
#endif

#endif /* MESHBUS_INCLUDE_ZUI_ZUI_TYPES_H_ */
