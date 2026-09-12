/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ZUI drawing API
 */

#ifndef MESHBUS_INCLUDE_ZUI_DRAW_H_
#define MESHBUS_INCLUDE_ZUI_DRAW_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zui/zui_types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zui_draw_ctx;
struct device;
struct zui_icon;
struct zui_icon_anim;

enum zui_bitmap_format {
	/** Row-major 1bpp data, least-significant bit first in each byte. */
	ZUI_BITMAP_FORMAT_MONO,
	/** XBM-compatible row-major 1bpp data, least-significant bit first. */
	ZUI_BITMAP_FORMAT_XBM,
	/** Vertical page-major 1bpp data, 8 pixels high per byte, LSB at top. */
	ZUI_BITMAP_FORMAT_MONO_VLSB,
};

enum zui_draw_direction {
	ZUI_DRAW_DIRECTION_LEFT_TO_RIGHT,
	ZUI_DRAW_DIRECTION_TOP_TO_BOTTOM,
	ZUI_DRAW_DIRECTION_RIGHT_TO_LEFT,
	ZUI_DRAW_DIRECTION_BOTTOM_TO_TOP,
};

enum zui_icon_transform {
	ZUI_ICON_TRANSFORM_NONE = 0,
	ZUI_ICON_TRANSFORM_FLIP_X = (1U << 0),
	ZUI_ICON_TRANSFORM_FLIP_Y = (1U << 1),
	ZUI_ICON_TRANSFORM_ROTATE_90 = (1U << 2),
	ZUI_ICON_TRANSFORM_ROTATE_180 = (1U << 3),
	ZUI_ICON_TRANSFORM_ROTATE_270 = (1U << 4),
};

enum zui_draw_text_box_flags {
	ZUI_DRAW_TEXT_BOX_NO_FLAGS = 0,
	ZUI_DRAW_TEXT_BOX_STRIP_TO_DOTS = (1U << 0),
	ZUI_DRAW_TEXT_BOX_FRAME = (1U << 1),
};

struct zui_draw_button_hint {
	const char *up;
	const char *down;
	const char *left;
	const char *center;
	const char *right;
};

struct zui_font_metrics {
	uint16_t height;
	uint16_t leading_default;
	uint16_t leading_min;
};

struct zui_framebuffer_view {
	/** Set to sizeof(struct zui_framebuffer_view) for ABI extension checks. */
	size_t struct_size;
	const uint8_t *data;
	uint16_t width;
	uint16_t height;
	/** Bytes between row-major rows or vertical page-major pages. */
	uint16_t stride;
	enum zui_bitmap_format format;
};

struct zui_draw_ctx *zui_draw_ctx_create(const struct device *display);
void zui_draw_ctx_destroy(struct zui_draw_ctx *ctx);
int zui_draw_present(struct zui_draw_ctx *ctx);
void zui_draw_reset(struct zui_draw_ctx *ctx);
void zui_draw_clear(struct zui_draw_ctx *ctx);
uint16_t zui_draw_width(const struct zui_draw_ctx *ctx);
uint16_t zui_draw_height(const struct zui_draw_ctx *ctx);
uint16_t zui_draw_font_height(const struct zui_draw_ctx *ctx);
int zui_draw_font_metrics(struct zui_draw_ctx *ctx, enum zui_font font,
			  struct zui_font_metrics *metrics);
void zui_draw_set_color(struct zui_draw_ctx *ctx, enum zui_color color);
void zui_draw_set_font(struct zui_draw_ctx *ctx, enum zui_font font);
void zui_draw_set_font_data(struct zui_draw_ctx *ctx, const uint8_t *font_data);
void zui_draw_invert_color(struct zui_draw_ctx *ctx);
void zui_draw_set_direction(struct zui_draw_ctx *ctx, enum zui_draw_direction direction);
void zui_draw_set_clip(struct zui_draw_ctx *ctx, const struct zui_rect *clip);
void zui_draw_clear_clip(struct zui_draw_ctx *ctx);
void zui_draw_text(struct zui_draw_ctx *ctx, struct zui_point pos, const char *text);
void zui_draw_text_aligned(struct zui_draw_ctx *ctx, struct zui_point pos,
			   enum zui_align horizontal, enum zui_align vertical, const char *text);
uint16_t zui_draw_text_width(struct zui_draw_ctx *ctx, const char *text);
uint16_t zui_draw_glyph_width(struct zui_draw_ctx *ctx, uint32_t codepoint);
void zui_draw_glyph(struct zui_draw_ctx *ctx, struct zui_point pos, uint32_t codepoint);
void zui_draw_dot(struct zui_draw_ctx *ctx, struct zui_point pos);
void zui_draw_line(struct zui_draw_ctx *ctx, struct zui_point start, struct zui_point end);
void zui_draw_rect(struct zui_draw_ctx *ctx, const struct zui_rect *rect);
void zui_draw_box(struct zui_draw_ctx *ctx, const struct zui_rect *rect);
void zui_draw_circle(struct zui_draw_ctx *ctx, struct zui_point center, uint16_t radius);
void zui_draw_disc(struct zui_draw_ctx *ctx, struct zui_point center, uint16_t radius);
void zui_draw_round_rect(struct zui_draw_ctx *ctx, const struct zui_rect *rect, uint16_t radius);
void zui_draw_round_rect_stroked(struct zui_draw_ctx *ctx, const struct zui_rect *rect,
				 uint16_t radius, uint16_t stroke_width);
void zui_draw_round_box(struct zui_draw_ctx *ctx, const struct zui_rect *rect, uint16_t radius);
void zui_draw_triangle(struct zui_draw_ctx *ctx, struct zui_point a, struct zui_point b,
		       struct zui_point c);
/**
 * Return the payload size in bytes for a bitmap format and dimensions.
 *
 * `ZUI_BITMAP_FORMAT_MONO` and `ZUI_BITMAP_FORMAT_XBM` use row-major bytes with
 * stride `ceil(width / 8)`. `ZUI_BITMAP_FORMAT_MONO_VLSB` uses vertical pages
 * with stride `width` and page count `ceil(height / 8)`, matching common
 * 128x64 monochrome game framebuffers.
 *
 * Returns 0 for invalid formats or zero dimensions.
 */
size_t zui_bitmap_payload_size(enum zui_bitmap_format format, uint16_t width, uint16_t height);

/**
 * Read one bit from raw 1bpp bitmap data.
 *
 * The caller owns `data` and must provide at least `zui_bitmap_payload_size()`
 * bytes for the same format and dimensions. Out-of-range coordinates, invalid
 * formats, zero dimensions, or a NULL payload return false.
 */
bool zui_bitmap_bit_get(enum zui_bitmap_format format, const uint8_t *data, uint16_t width,
			uint16_t height, uint16_t x, uint16_t y);

/** Draw raw MONO/XBM/MONO_VLSB bitmap data. Compressed icon payloads are decoded internally. */
void zui_draw_bitmap(struct zui_draw_ctx *ctx, struct zui_point pos, uint16_t width,
		     uint16_t height, enum zui_bitmap_format format, const uint8_t *data);
void zui_draw_bitmap_alpha(struct zui_draw_ctx *ctx, struct zui_point pos, uint16_t width,
			   uint16_t height, enum zui_bitmap_format format, const uint8_t *data,
			   bool alpha);
void zui_draw_bitmap_transformed(struct zui_draw_ctx *ctx, struct zui_point pos, uint16_t width,
				 uint16_t height, enum zui_bitmap_format format,
				 const uint8_t *data, enum zui_icon_transform transform,
				 bool alpha);
void zui_draw_framebuffer(struct zui_draw_ctx *ctx, struct zui_point pos,
			  const struct zui_framebuffer_view *view);
void zui_draw_icon(struct zui_draw_ctx *ctx, struct zui_point pos, const struct zui_icon *icon);
void zui_draw_icon_frame(struct zui_draw_ctx *ctx, struct zui_point pos,
			 const struct zui_icon *icon, uint32_t frame);
void zui_draw_icon_transformed(struct zui_draw_ctx *ctx, struct zui_point pos,
			       const struct zui_icon *icon, enum zui_icon_transform transform);
void zui_draw_icon_anim(struct zui_draw_ctx *ctx, struct zui_point pos,
			const struct zui_icon_anim *anim);

void zui_draw_progress_bar(struct zui_draw_ctx *ctx, const struct zui_rect *rect, uint8_t percent);
void zui_draw_progress_bar_text(struct zui_draw_ctx *ctx, const struct zui_rect *rect,
				uint8_t percent, const char *text);
void zui_draw_scrollbar(struct zui_draw_ctx *ctx, const struct zui_rect *rect, size_t position,
			size_t total);
void zui_draw_button_hints(struct zui_draw_ctx *ctx, const struct zui_draw_button_hint *hints);
void zui_draw_multiline_text(struct zui_draw_ctx *ctx, const struct zui_rect *rect,
			     enum zui_align horizontal, enum zui_align vertical, const char *text);
int zui_draw_text_fit_width(struct zui_draw_ctx *ctx, char *text, size_t text_size,
			    uint16_t max_width);
void zui_draw_text_line_scrolled(struct zui_draw_ctx *ctx, struct zui_point pos, uint16_t width,
				 const char *text, size_t scroll, bool ellipsis);
void zui_draw_text_box(struct zui_draw_ctx *ctx, const struct zui_rect *rect, const char *text,
		       size_t scroll_line, enum zui_draw_text_box_flags flags);
void zui_draw_bubble_frame(struct zui_draw_ctx *ctx, const struct zui_rect *rect);
void zui_draw_bubble(struct zui_draw_ctx *ctx, const struct zui_rect *rect, struct zui_point tail,
		     const char *text);

#ifdef __cplusplus
}
#endif

#endif /* MESHBUS_INCLUDE_ZUI_DRAW_H_ */
