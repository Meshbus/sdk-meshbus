/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/display/u8g2.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/zui/assets.h>
#include <zephyr/zui/draw.h>

#include "zui_compress.h"
#include "zui_mem.h"

struct zui_draw_ctx {
	u8g2_t u8g2;
	const struct device *display;
	uint16_t width;
	uint16_t height;
	enum zui_color color;
	enum zui_font font;
	enum zui_draw_direction direction;
	bool backend_ready;
	bool clip_enabled;
	struct zui_rect clip;
	CompressIcon *compress_icon;
};

static const struct zui_font_metrics zui_draw_font_params[ZUI_FONT_COUNT] = {
#if defined(CONFIG_ZUI_FONT_CJK_WQY12_GB2312)
	[ZUI_FONT_PRIMARY] =
		{
			.height = 12,
			.leading_default = 14,
			.leading_min = 12,
		},
	[ZUI_FONT_SECONDARY] =
		{
			.height = 12,
			.leading_default = 14,
			.leading_min = 12,
		},
#else
	[ZUI_FONT_PRIMARY] =
		{
			.height = 8,
			.leading_default = 12,
			.leading_min = 11,
		},
	[ZUI_FONT_SECONDARY] =
		{
			.height = 7,
			.leading_default = 11,
			.leading_min = 9,
		},
#endif
	[ZUI_FONT_KEYBOARD] =
		{
			.height = 7,
			.leading_default = 11,
			.leading_min = 9,
		},
	[ZUI_FONT_BIG_NUMBERS] =
		{
			.height = 15,
			.leading_default = 18,
			.leading_min = 16,
		},
};

static bool zui_draw_font_valid(enum zui_font font)
{
	return font >= 0 && font < ZUI_FONT_COUNT;
}

static bool zui_draw_color_valid(enum zui_color color)
{
	return color == ZUI_COLOR_WHITE || color == ZUI_COLOR_BLACK || color == ZUI_COLOR_INVERT;
}

static bool zui_draw_bitmap_format_valid(enum zui_bitmap_format format)
{
	return format == ZUI_BITMAP_FORMAT_MONO || format == ZUI_BITMAP_FORMAT_XBM ||
	       format == ZUI_BITMAP_FORMAT_MONO_VLSB;
}

static const uint8_t *zui_draw_font_ptr(enum zui_font font)
{
	switch (font) {
	case ZUI_FONT_PRIMARY:
#if defined(CONFIG_ZUI_FONT_CJK_WQY12_GB2312)
		return u8g2_font_wqy12_t_gb2312;
#else
		return u8g2_font_helvB08_tr;
#endif
	case ZUI_FONT_SECONDARY:
#if defined(CONFIG_ZUI_FONT_CJK_WQY12_GB2312)
		return u8g2_font_wqy12_t_gb2312;
#else
		return u8g2_font_haxrcorp4089_tr;
#endif
	case ZUI_FONT_KEYBOARD:
		return u8g2_font_profont11_mr;
	case ZUI_FONT_BIG_NUMBERS:
		return u8g2_font_profont22_tn;
	default:
		return NULL;
	}
}

static bool zui_draw_rect_valid(const struct zui_rect *rect)
{
	return rect != NULL && rect->width > 0U && rect->height > 0U;
}

static const uint8_t *zui_draw_icon_payload(struct zui_draw_ctx *ctx, const struct zui_icon *icon,
					    uint32_t frame)
{
	const uint8_t *data;
	uint8_t *decoded = NULL;
	size_t expected_len;
	size_t data_len;
	uint32_t frame_index;

	if (icon == NULL || zui_icon_frame_count(icon) == 0U) {
		return NULL;
	}

	frame_index = frame % zui_icon_frame_count(icon);
	data = zui_icon_frame_data(icon, frame_index);
	if (data == NULL) {
		return NULL;
	}

	expected_len = zui_bitmap_payload_size(ZUI_BITMAP_FORMAT_XBM, zui_icon_width(icon),
					       zui_icon_height(icon));
	data_len = zui_icon_frame_size(icon, frame_index);
	if (data_len == 0U) {
		return data;
	}
	if (data_len == expected_len &&
	    !(data_len >= 4U && data[0] == 0x01 && data[1] == 0x00 &&
	      (uint16_t)(data[2] | ((uint16_t)data[3] << 8)) == data_len - 4U)) {
		return data;
	}

	if (ctx == NULL) {
		return NULL;
	}

	if (ctx->compress_icon == NULL) {
		ctx->compress_icon = compress_icon_alloc(0U);
		if (ctx->compress_icon == NULL) {
			return NULL;
		}
	}

	if (!compress_icon_decode(ctx->compress_icon, data, data_len, expected_len, &decoded)) {
		return NULL;
	}

	return decoded;
}

static void zui_draw_backend_set_font(struct zui_draw_ctx *ctx)
{
	const uint8_t *font;

	if (ctx == NULL || !ctx->backend_ready) {
		return;
	}

	font = zui_draw_font_ptr(ctx->font);
	if (font == NULL) {
		return;
	}

	u8g2_SetFontMode(&ctx->u8g2, 1);
	u8g2_SetFont(&ctx->u8g2, font);
}

static void zui_draw_backend_set_direction(struct zui_draw_ctx *ctx)
{
	if (ctx == NULL || !ctx->backend_ready) {
		return;
	}

	u8g2_SetFontDirection(&ctx->u8g2, (uint8_t)ctx->direction);
}

static void zui_draw_backend_set_color(struct zui_draw_ctx *ctx)
{
	if (ctx == NULL || !ctx->backend_ready) {
		return;
	}

	u8g2_SetDrawColor(&ctx->u8g2, (uint8_t)ctx->color);
}

static void zui_draw_apply_defaults(struct zui_draw_ctx *ctx)
{
	ctx->color = ZUI_COLOR_BLACK;
	ctx->font = ZUI_FONT_SECONDARY;
	ctx->direction = ZUI_DRAW_DIRECTION_LEFT_TO_RIGHT;
	ctx->clip_enabled = false;
	ctx->clip = (struct zui_rect){0};

	zui_draw_backend_set_color(ctx);
	zui_draw_backend_set_font(ctx);
	zui_draw_backend_set_direction(ctx);
	if (ctx->backend_ready) {
		u8g2_SetMaxClipWindow(&ctx->u8g2);
	}
}

static struct zui_point zui_draw_transform_point(struct zui_point pos, uint16_t width,
						 uint16_t height, enum zui_icon_transform transform)
{
	struct zui_point out = pos;

	if ((transform & ZUI_ICON_TRANSFORM_FLIP_X) != 0U) {
		out.x = (int16_t)(width - 1U - (uint16_t)out.x);
	}
	if ((transform & ZUI_ICON_TRANSFORM_FLIP_Y) != 0U) {
		out.y = (int16_t)(height - 1U - (uint16_t)out.y);
	}

	if ((transform & ZUI_ICON_TRANSFORM_ROTATE_90) != 0U) {
		out.x = (int16_t)(height - 1U - (uint16_t)pos.y);
		out.y = pos.x;
	} else if ((transform & ZUI_ICON_TRANSFORM_ROTATE_180) != 0U) {
		out.x = (int16_t)(width - 1U - (uint16_t)pos.x);
		out.y = (int16_t)(height - 1U - (uint16_t)pos.y);
	} else if ((transform & ZUI_ICON_TRANSFORM_ROTATE_270) != 0U) {
		out.x = pos.y;
		out.y = (int16_t)(width - 1U - (uint16_t)pos.x);
	}

	return out;
}

size_t zui_bitmap_payload_size(enum zui_bitmap_format format, uint16_t width, uint16_t height)
{
	if (width == 0U || height == 0U || !zui_draw_bitmap_format_valid(format)) {
		return 0U;
	}

	switch (format) {
	case ZUI_BITMAP_FORMAT_MONO:
	case ZUI_BITMAP_FORMAT_XBM:
		return (((size_t)width + 7U) / 8U) * height;
	case ZUI_BITMAP_FORMAT_MONO_VLSB:
		return (size_t)width * (((size_t)height + 7U) / 8U);
	default:
		return 0U;
	}
}

static bool zui_framebuffer_bit_get(enum zui_bitmap_format format, const uint8_t *data,
				    uint16_t width, uint16_t height, uint16_t stride,
				    uint16_t x, uint16_t y)
{
	size_t offset;
	uint8_t byte;

	if (data == NULL || width == 0U || height == 0U || stride == 0U || x >= width ||
	    y >= height || !zui_draw_bitmap_format_valid(format)) {
		return false;
	}

	switch (format) {
	case ZUI_BITMAP_FORMAT_MONO:
	case ZUI_BITMAP_FORMAT_XBM:
		offset = (size_t)y * stride + (x / 8U);
		byte = data[offset];
		return (byte & BIT(x & 0x7U)) != 0U;
	case ZUI_BITMAP_FORMAT_MONO_VLSB:
		offset = (size_t)(y / 8U) * stride + x;
		byte = data[offset];
		return (byte & BIT(y & 0x7U)) != 0U;
	default:
		return false;
	}
}

bool zui_bitmap_bit_get(enum zui_bitmap_format format, const uint8_t *data, uint16_t width,
			uint16_t height, uint16_t x, uint16_t y)
{
	uint16_t stride;

	if (format == ZUI_BITMAP_FORMAT_MONO || format == ZUI_BITMAP_FORMAT_XBM) {
		stride = (uint16_t)(((uint32_t)width + 7U) / 8U);
	} else if (format == ZUI_BITMAP_FORMAT_MONO_VLSB) {
		stride = width;
	} else {
		return false;
	}

	return zui_framebuffer_bit_get(format, data, width, height, stride, x, y);
}

static bool zui_framebuffer_view_valid(const struct zui_framebuffer_view *view)
{
	uint16_t min_stride;

	if (view == NULL || view->struct_size < sizeof(*view) || view->data == NULL ||
	    view->width == 0U || view->height == 0U ||
	    !zui_draw_bitmap_format_valid(view->format)) {
		return false;
	}

	if (view->format == ZUI_BITMAP_FORMAT_MONO || view->format == ZUI_BITMAP_FORMAT_XBM) {
		min_stride = (uint16_t)(((uint32_t)view->width + 7U) / 8U);
	} else {
		min_stride = view->width;
	}

	return view->stride >= min_stride;
}

static bool zui_draw_framebuffer_fast_fullscreen(struct zui_draw_ctx *ctx, struct zui_point pos,
						 const struct zui_framebuffer_view *view)
{
	uint8_t *dst;
	size_t len;

	if (ctx == NULL || view == NULL || !ctx->backend_ready || ctx->clip_enabled ||
	    pos.x != 0 || pos.y != 0 || view->format != ZUI_BITMAP_FORMAT_MONO_VLSB ||
	    view->width != ctx->width || view->height != ctx->height ||
	    view->stride != view->width || (ctx->color != ZUI_COLOR_BLACK &&
					    ctx->color != ZUI_COLOR_WHITE)) {
		return false;
	}

	if (u8g2_GetBufferTileHeight(&ctx->u8g2) != (view->height / 8U) ||
	    u8g2_GetBufferTileWidth(&ctx->u8g2) * 8U != view->width) {
		return false;
	}

	dst = u8g2_GetBufferPtr(&ctx->u8g2);
	if (dst == NULL) {
		return false;
	}

	len = zui_bitmap_payload_size(view->format, view->width, view->height);
	if (ctx->color == ZUI_COLOR_BLACK) {
		memcpy(dst, view->data, len);
	} else {
		for (size_t i = 0U; i < len; i++) {
			dst[i] = (uint8_t)~view->data[i];
		}
	}

	return true;
}

struct zui_draw_ctx *zui_draw_ctx_create(const struct device *display)
{
	struct zui_draw_ctx *ctx = zui_calloc(1U, sizeof(*ctx));

	if (ctx == NULL) {
		return NULL;
	}

	ctx->display = display;
	ctx->width = CONFIG_ZUI_DISPLAY_WIDTH;
	ctx->height = CONFIG_ZUI_DISPLAY_HEIGHT;

	if (display != NULL && device_is_ready(display)) {
		struct display_capabilities caps;
		int rc = u8g2_init(&ctx->u8g2, display, U8G2_R0);

		if (rc != 0) {
			zui_free(ctx);
			return NULL;
		}

		display_get_capabilities(display, &caps);
		ctx->width = caps.x_resolution;
		ctx->height = caps.y_resolution;
		ctx->backend_ready = true;
	}

	zui_draw_apply_defaults(ctx);
	zui_draw_clear(ctx);
	return ctx;
}

void zui_draw_ctx_destroy(struct zui_draw_ctx *ctx)
{
	if (ctx == NULL) {
		return;
	}

	compress_icon_free(ctx->compress_icon);
	if (ctx->backend_ready) {
		u8g2_deinit();
	}

	zui_free(ctx);
}

int zui_draw_present(struct zui_draw_ctx *ctx)
{
	if (ctx == NULL) {
		return -EINVAL;
	}

	if (!ctx->backend_ready) {
		return -ENODEV;
	}

	u8g2_SendBuffer(&ctx->u8g2);
	return 0;
}

void zui_draw_reset(struct zui_draw_ctx *ctx)
{
	if (ctx == NULL) {
		return;
	}

	zui_draw_clear(ctx);
	zui_draw_apply_defaults(ctx);
}

void zui_draw_clear(struct zui_draw_ctx *ctx)
{
	if (ctx == NULL || !ctx->backend_ready) {
		return;
	}

	u8g2_ClearBuffer(&ctx->u8g2);
}

uint16_t zui_draw_width(const struct zui_draw_ctx *ctx)
{
	return ctx == NULL ? 0U : ctx->width;
}

uint16_t zui_draw_height(const struct zui_draw_ctx *ctx)
{
	return ctx == NULL ? 0U : ctx->height;
}

uint16_t zui_draw_font_height(const struct zui_draw_ctx *ctx)
{
	struct zui_font_metrics metrics;

	if (ctx == NULL ||
	    zui_draw_font_metrics((struct zui_draw_ctx *)ctx, ctx->font, &metrics) != 0) {
		return 0U;
	}

	return metrics.height;
}

int zui_draw_font_metrics(struct zui_draw_ctx *ctx, enum zui_font font,
			  struct zui_font_metrics *metrics)
{
	ARG_UNUSED(ctx);

	if (metrics == NULL || !zui_draw_font_valid(font)) {
		return -EINVAL;
	}

	*metrics = zui_draw_font_params[font];
	return 0;
}

void zui_draw_set_color(struct zui_draw_ctx *ctx, enum zui_color color)
{
	if (ctx == NULL || !zui_draw_color_valid(color)) {
		return;
	}

	ctx->color = color;
	zui_draw_backend_set_color(ctx);
}

void zui_draw_set_font(struct zui_draw_ctx *ctx, enum zui_font font)
{
	if (ctx == NULL || !zui_draw_font_valid(font)) {
		return;
	}

	ctx->font = font;
	zui_draw_backend_set_font(ctx);
}

void zui_draw_set_font_data(struct zui_draw_ctx *ctx, const uint8_t *font_data)
{
	if (ctx == NULL || font_data == NULL || !ctx->backend_ready) {
		return;
	}

	u8g2_SetFontMode(&ctx->u8g2, 1);
	u8g2_SetFont(&ctx->u8g2, font_data);
}

void zui_draw_invert_color(struct zui_draw_ctx *ctx)
{
	if (ctx == NULL) {
		return;
	}

	if (ctx->color == ZUI_COLOR_WHITE) {
		ctx->color = ZUI_COLOR_BLACK;
	} else if (ctx->color == ZUI_COLOR_BLACK) {
		ctx->color = ZUI_COLOR_WHITE;
	}
	zui_draw_backend_set_color(ctx);
}

void zui_draw_set_direction(struct zui_draw_ctx *ctx, enum zui_draw_direction direction)
{
	if (ctx == NULL || direction < ZUI_DRAW_DIRECTION_LEFT_TO_RIGHT ||
	    direction > ZUI_DRAW_DIRECTION_BOTTOM_TO_TOP) {
		return;
	}

	ctx->direction = direction;
	zui_draw_backend_set_direction(ctx);
}

void zui_draw_set_clip(struct zui_draw_ctx *ctx, const struct zui_rect *clip)
{
	if (ctx == NULL || !zui_draw_rect_valid(clip)) {
		return;
	}

	ctx->clip_enabled = true;
	ctx->clip = *clip;
	if (ctx->backend_ready) {
		u8g2_SetClipWindow(&ctx->u8g2, clip->x, clip->y, clip->x + clip->width,
				   clip->y + clip->height);
	}
}

void zui_draw_clear_clip(struct zui_draw_ctx *ctx)
{
	if (ctx == NULL) {
		return;
	}

	ctx->clip_enabled = false;
	ctx->clip = (struct zui_rect){0};
	if (ctx->backend_ready) {
		u8g2_SetMaxClipWindow(&ctx->u8g2);
	}
}

void zui_draw_text(struct zui_draw_ctx *ctx, struct zui_point pos, const char *text)
{
	if (ctx == NULL || text == NULL || !ctx->backend_ready) {
		return;
	}

	u8g2_DrawUTF8(&ctx->u8g2, pos.x, pos.y, text);
}

void zui_draw_text_aligned(struct zui_draw_ctx *ctx, struct zui_point pos,
			   enum zui_align horizontal, enum zui_align vertical, const char *text)
{
	if (ctx == NULL || text == NULL) {
		return;
	}

	switch (horizontal) {
	case ZUI_ALIGN_RIGHT:
		pos.x -= zui_draw_text_width(ctx, text);
		break;
	case ZUI_ALIGN_CENTER:
		pos.x -= zui_draw_text_width(ctx, text) / 2U;
		break;
	default:
		break;
	}

	switch (vertical) {
	case ZUI_ALIGN_TOP:
		pos.y +=
			ctx->backend_ready ? u8g2_GetAscent(&ctx->u8g2) : zui_draw_font_height(ctx);
		break;
	case ZUI_ALIGN_CENTER:
		pos.y += (ctx->backend_ready ? u8g2_GetAscent(&ctx->u8g2)
					     : zui_draw_font_height(ctx)) /
			 2U;
		break;
	default:
		break;
	}

	zui_draw_text(ctx, pos, text);
}

uint16_t zui_draw_text_width(struct zui_draw_ctx *ctx, const char *text)
{
	if (ctx == NULL || text == NULL) {
		return 0U;
	}

	if (ctx->backend_ready) {
		return u8g2_GetUTF8Width(&ctx->u8g2, text);
	}

	return (uint16_t)(strlen(text) * 6U);
}

uint16_t zui_draw_glyph_width(struct zui_draw_ctx *ctx, uint32_t codepoint)
{
	if (ctx == NULL) {
		return 0U;
	}

	if (ctx->backend_ready) {
		return u8g2_GetGlyphWidth(&ctx->u8g2, (uint16_t)codepoint);
	}

	return codepoint == 0U ? 0U : 6U;
}

#if defined(CONFIG_ZUI_TEXT_UTF8)
static bool zui_draw_utf8_is_continuation(uint8_t byte)
{
	return (byte & 0xc0U) == 0x80U;
}

static size_t zui_draw_utf8_char_len(const char *text, size_t remaining)
{
	const uint8_t *bytes = (const uint8_t *)text;

	if (bytes == NULL || remaining == 0U || bytes[0] == '\0') {
		return 0U;
	}
	if ((bytes[0] & 0x80U) == 0U) {
		return 1U;
	}
	if ((bytes[0] & 0xe0U) == 0xc0U && remaining >= 2U &&
	    zui_draw_utf8_is_continuation(bytes[1])) {
		return 2U;
	}
	if ((bytes[0] & 0xe0U) == 0xc0U) {
		return 0U;
	}
	if ((bytes[0] & 0xf0U) == 0xe0U) {
		if (remaining >= 3U && zui_draw_utf8_is_continuation(bytes[1]) &&
		    zui_draw_utf8_is_continuation(bytes[2])) {
			return 3U;
		}
		return 0U;
	}
	if ((bytes[0] & 0xf8U) == 0xf0U) {
		if (remaining >= 4U && zui_draw_utf8_is_continuation(bytes[1]) &&
		    zui_draw_utf8_is_continuation(bytes[2]) &&
		    zui_draw_utf8_is_continuation(bytes[3])) {
			return 4U;
		}
		return 0U;
	}

	return 1U;
}

static size_t zui_draw_utf8_prev_start(const char *text, size_t end)
{
	size_t start;

	if (text == NULL || end == 0U) {
		return 0U;
	}

	start = end - 1U;
	while (start > 0U && zui_draw_utf8_is_continuation((uint8_t)text[start])) {
		start--;
	}

	return start;
}

static size_t zui_draw_utf8_trim_to_boundary(const char *text, size_t len)
{
	size_t start;
	size_t char_len;

	if (text == NULL || len == 0U) {
		return 0U;
	}

	start = zui_draw_utf8_prev_start(text, len);
	char_len = zui_draw_utf8_char_len(text + start, len - start);
	if (char_len == len - start) {
		return len;
	}

	return start;
}

static void zui_draw_text_truncate_utf8(char *text, size_t *len)
{
	size_t trim_len;
	size_t prev;

	if (text == NULL || len == NULL || *len == 0U) {
		return;
	}

	trim_len = zui_draw_utf8_trim_to_boundary(text, *len);
	if (trim_len < *len) {
		text[trim_len] = '\0';
		*len = trim_len;
		return;
	}

	prev = zui_draw_utf8_prev_start(text, *len);
	text[prev] = '\0';
	*len = prev;
}
#endif

static size_t zui_draw_text_fit_prepare(char *text, size_t text_size)
{
	size_t len = strnlen(text, text_size);

	if (len == text_size) {
		text[text_size - 1U] = '\0';
		len = text_size - 1U;
	}

#if defined(CONFIG_ZUI_TEXT_UTF8)
	len = zui_draw_utf8_trim_to_boundary(text, len);
	text[len] = '\0';
#endif

	return len;
}

static void zui_draw_text_fit_truncate(char *text, size_t *len)
{
	if (text == NULL || len == NULL || *len == 0U) {
		return;
	}

#if defined(CONFIG_ZUI_TEXT_UTF8)
	zui_draw_text_truncate_utf8(text, len);
#else
	text[--(*len)] = '\0';
#endif
}

void zui_draw_glyph(struct zui_draw_ctx *ctx, struct zui_point pos, uint32_t codepoint)
{
	if (ctx == NULL || !ctx->backend_ready) {
		return;
	}

	u8g2_DrawGlyph(&ctx->u8g2, pos.x, pos.y, (uint16_t)codepoint);
}

void zui_draw_dot(struct zui_draw_ctx *ctx, struct zui_point pos)
{
	if (ctx == NULL || !ctx->backend_ready) {
		return;
	}

	u8g2_DrawPixel(&ctx->u8g2, pos.x, pos.y);
}

void zui_draw_line(struct zui_draw_ctx *ctx, struct zui_point start, struct zui_point end)
{
	if (ctx == NULL || !ctx->backend_ready) {
		return;
	}

	u8g2_DrawLine(&ctx->u8g2, start.x, start.y, end.x, end.y);
}

void zui_draw_rect(struct zui_draw_ctx *ctx, const struct zui_rect *rect)
{
	if (ctx == NULL || !zui_draw_rect_valid(rect) || !ctx->backend_ready) {
		return;
	}

	u8g2_DrawFrame(&ctx->u8g2, rect->x, rect->y, rect->width, rect->height);
}

void zui_draw_box(struct zui_draw_ctx *ctx, const struct zui_rect *rect)
{
	if (ctx == NULL || !zui_draw_rect_valid(rect) || !ctx->backend_ready) {
		return;
	}

	u8g2_DrawBox(&ctx->u8g2, rect->x, rect->y, rect->width, rect->height);
}

void zui_draw_circle(struct zui_draw_ctx *ctx, struct zui_point center, uint16_t radius)
{
	if (ctx == NULL || radius == 0U || !ctx->backend_ready) {
		return;
	}

	u8g2_DrawCircle(&ctx->u8g2, center.x, center.y, radius, U8G2_DRAW_ALL);
}

void zui_draw_disc(struct zui_draw_ctx *ctx, struct zui_point center, uint16_t radius)
{
	if (ctx == NULL || radius == 0U || !ctx->backend_ready) {
		return;
	}

	u8g2_DrawDisc(&ctx->u8g2, center.x, center.y, radius, U8G2_DRAW_ALL);
}

void zui_draw_round_rect(struct zui_draw_ctx *ctx, const struct zui_rect *rect, uint16_t radius)
{
	if (ctx == NULL || !zui_draw_rect_valid(rect) || !ctx->backend_ready) {
		return;
	}

	u8g2_DrawRFrame(&ctx->u8g2, rect->x, rect->y, rect->width, rect->height, radius);
}

void zui_draw_round_rect_stroked(struct zui_draw_ctx *ctx, const struct zui_rect *rect,
				 uint16_t radius, uint16_t stroke_width)
{
	if (ctx == NULL || !zui_draw_rect_valid(rect) || stroke_width == 0U) {
		return;
	}

	for (uint16_t i = 0U; i < stroke_width; i++) {
		struct zui_rect inset = {
			.x = rect->x + i,
			.y = rect->y + i,
			.width = rect->width > (2U * i) ? rect->width - (2U * i) : 0U,
			.height = rect->height > (2U * i) ? rect->height - (2U * i) : 0U,
		};

		zui_draw_round_rect(ctx, &inset, radius > i ? radius - i : 0U);
	}
}

void zui_draw_round_box(struct zui_draw_ctx *ctx, const struct zui_rect *rect, uint16_t radius)
{
	if (ctx == NULL || !zui_draw_rect_valid(rect) || !ctx->backend_ready) {
		return;
	}

	u8g2_DrawRBox(&ctx->u8g2, rect->x, rect->y, rect->width, rect->height, radius);
}

void zui_draw_triangle(struct zui_draw_ctx *ctx, struct zui_point a, struct zui_point b,
		       struct zui_point c)
{
	if (ctx == NULL || !ctx->backend_ready) {
		return;
	}

	u8g2_DrawTriangle(&ctx->u8g2, a.x, a.y, b.x, b.y, c.x, c.y);
}

void zui_draw_bitmap(struct zui_draw_ctx *ctx, struct zui_point pos, uint16_t width,
		     uint16_t height, enum zui_bitmap_format format, const uint8_t *data)
{
	zui_draw_bitmap_alpha(ctx, pos, width, height, format, data, false);
}

void zui_draw_bitmap_alpha(struct zui_draw_ctx *ctx, struct zui_point pos, uint16_t width,
			   uint16_t height, enum zui_bitmap_format format, const uint8_t *data,
			   bool alpha)
{
	zui_draw_bitmap_transformed(ctx, pos, width, height, format, data, ZUI_ICON_TRANSFORM_NONE,
				    alpha);
}

void zui_draw_bitmap_transformed(struct zui_draw_ctx *ctx, struct zui_point pos, uint16_t width,
				 uint16_t height, enum zui_bitmap_format format,
				 const uint8_t *data, enum zui_icon_transform transform, bool alpha)
{
	uint8_t saved_color;

	if (ctx == NULL || !ctx->backend_ready || width == 0U || height == 0U || data == NULL ||
	    !zui_draw_bitmap_format_valid(format)) {
		return;
	}

	saved_color = ctx->u8g2.draw_color;
	u8g2_SetBitmapMode(&ctx->u8g2, alpha ? 1 : 0);

	for (uint16_t y = 0U; y < height; y++) {
		for (uint16_t x = 0U; x < width; x++) {
			struct zui_point dst = zui_draw_transform_point(
				(struct zui_point){.x = (int16_t)x, .y = (int16_t)y}, width, height,
				transform);
			bool bit = zui_bitmap_bit_get(format, data, width, height, x, y);

			if (bit) {
				u8g2_SetDrawColor(&ctx->u8g2, saved_color);
				u8g2_DrawPixel(&ctx->u8g2, pos.x + dst.x, pos.y + dst.y);
			} else if (!alpha) {
				u8g2_SetDrawColor(&ctx->u8g2, saved_color == 0U ? 1U : 0U);
				u8g2_DrawPixel(&ctx->u8g2, pos.x + dst.x, pos.y + dst.y);
			}
		}
	}

	u8g2_SetDrawColor(&ctx->u8g2, saved_color);
	u8g2_SetBitmapMode(&ctx->u8g2, 0);
}

void zui_draw_framebuffer(struct zui_draw_ctx *ctx, struct zui_point pos,
			  const struct zui_framebuffer_view *view)
{
	uint8_t saved_color;

	if (ctx == NULL || !ctx->backend_ready || !zui_framebuffer_view_valid(view)) {
		return;
	}

	if (zui_draw_framebuffer_fast_fullscreen(ctx, pos, view)) {
		return;
	}

	saved_color = ctx->u8g2.draw_color;
	u8g2_SetBitmapMode(&ctx->u8g2, 0);

	for (uint16_t y = 0U; y < view->height; y++) {
		for (uint16_t x = 0U; x < view->width; x++) {
			bool bit = zui_framebuffer_bit_get(view->format, view->data, view->width,
							   view->height, view->stride, x, y);

			u8g2_SetDrawColor(&ctx->u8g2, bit ? saved_color :
							       (saved_color == 0U ? 1U : 0U));
			u8g2_DrawPixel(&ctx->u8g2, pos.x + (int16_t)x, pos.y + (int16_t)y);
		}
	}

	u8g2_SetDrawColor(&ctx->u8g2, saved_color);
	u8g2_SetBitmapMode(&ctx->u8g2, 0);
}

void zui_draw_icon(struct zui_draw_ctx *ctx, struct zui_point pos, const struct zui_icon *icon)
{
	zui_draw_icon_frame(ctx, pos, icon, 0U);
}

void zui_draw_icon_frame(struct zui_draw_ctx *ctx, struct zui_point pos,
			 const struct zui_icon *icon, uint32_t frame)
{
	const uint8_t *data;

	if (ctx == NULL || icon == NULL || zui_icon_width(icon) == 0U ||
	    zui_icon_height(icon) == 0U) {
		return;
	}

	data = zui_draw_icon_payload(ctx, icon, frame);
	zui_draw_bitmap_alpha(ctx, pos, zui_icon_width(icon), zui_icon_height(icon),
			      ZUI_BITMAP_FORMAT_XBM, data, true);
}

void zui_draw_icon_transformed(struct zui_draw_ctx *ctx, struct zui_point pos,
			       const struct zui_icon *icon, enum zui_icon_transform transform)
{
	const uint8_t *data;

	if (ctx == NULL || icon == NULL || zui_icon_width(icon) == 0U ||
	    zui_icon_height(icon) == 0U) {
		return;
	}

	data = zui_draw_icon_payload(ctx, icon, 0U);
	zui_draw_bitmap_transformed(ctx, pos, zui_icon_width(icon), zui_icon_height(icon),
				    ZUI_BITMAP_FORMAT_XBM, data, transform, true);
}

void zui_draw_icon_anim(struct zui_draw_ctx *ctx, struct zui_point pos,
			const struct zui_icon_anim *anim)
{
	if (anim == NULL) {
		return;
	}

	zui_draw_icon_frame(ctx, pos, zui_icon_anim_icon(anim), zui_icon_anim_frame(anim));
}

void zui_draw_progress_bar(struct zui_draw_ctx *ctx, const struct zui_rect *rect, uint8_t percent)
{
	struct zui_rect fill;

	if (!zui_draw_rect_valid(rect)) {
		return;
	}

	zui_draw_rect(ctx, rect);
	if (rect->width <= 2U || rect->height <= 2U) {
		return;
	}

	fill = (struct zui_rect){
		.x = rect->x + 1,
		.y = rect->y + 1,
		.width = (uint16_t)(((uint32_t)(rect->width - 2U) * MIN(percent, 100U)) / 100U),
		.height = rect->height - 2U,
	};
	zui_draw_box(ctx, &fill);
}

void zui_draw_progress_bar_text(struct zui_draw_ctx *ctx, const struct zui_rect *rect,
				uint8_t percent, const char *text)
{
	zui_draw_progress_bar(ctx, rect, percent);
	if (rect != NULL && text != NULL) {
		zui_draw_text_aligned(ctx,
				      (struct zui_point){.x = rect->x + rect->width / 2,
							 .y = rect->y + rect->height / 2},
				      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER, text);
	}
}

void zui_draw_scrollbar(struct zui_draw_ctx *ctx, const struct zui_rect *rect, size_t position,
			size_t total)
{
	struct zui_rect thumb;
	uint16_t travel;

	if (!zui_draw_rect_valid(rect)) {
		return;
	}

	zui_draw_rect(ctx, rect);
	if (total <= 1U || rect->height <= 4U) {
		return;
	}

	travel = rect->height - 4U;
	thumb = (struct zui_rect){
		.x = rect->x + 1,
		.y = rect->y + 1 + (uint16_t)((MIN(position, total - 1U) * travel) / (total - 1U)),
		.width = rect->width > 2U ? rect->width - 2U : 1U,
		.height = 3U,
	};
	zui_draw_box(ctx, &thumb);
}

static void zui_draw_legacy_button_left(struct zui_draw_ctx *ctx, const char *text)
{
	const struct zui_icon *icon;
	uint16_t text_width;
	uint16_t button_width;
	uint16_t height;
	uint16_t y;

	if (ctx == NULL || text == NULL || text[0] == '\0') {
		return;
	}

	icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(), ZUI_ASSET_ICON_BUTTON_LEFT);
	text_width = zui_draw_text_width(ctx, text);
	button_width = (uint16_t)(text_width + 9U + zui_icon_width(icon));
	height = 12U;
	y = zui_draw_height(ctx);

	zui_draw_box(ctx, &(struct zui_rect){.x = 0,
					     .y = (int16_t)(y - height),
					     .width = button_width,
					     .height = height});
	zui_draw_line(ctx, (struct zui_point){.x = (int16_t)button_width, .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)button_width, .y = (int16_t)(y - height)});
	zui_draw_line(ctx, (struct zui_point){.x = (int16_t)(button_width + 1U), .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)(button_width + 1U),
					 .y = (int16_t)(y - height + 1U)});
	zui_draw_line(ctx, (struct zui_point){.x = (int16_t)(button_width + 2U), .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)(button_width + 2U),
					 .y = (int16_t)(y - height + 2U)});
	zui_draw_invert_color(ctx);
	zui_draw_icon(ctx,
		      (struct zui_point){.x = 3, .y = (int16_t)(y - zui_icon_height(icon) - 3U)},
		      icon);
	zui_draw_text(ctx,
		      (struct zui_point){.x = (int16_t)(6U + zui_icon_width(icon)),
					 .y = (int16_t)(y - 3U)},
		      text);
	zui_draw_invert_color(ctx);
}

static void zui_draw_legacy_button_right(struct zui_draw_ctx *ctx, const char *text)
{
	const struct zui_icon *icon;
	uint16_t text_width;
	uint16_t button_width;
	uint16_t height;
	uint16_t x;
	uint16_t y;

	if (ctx == NULL || text == NULL || text[0] == '\0') {
		return;
	}

	icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(), ZUI_ASSET_ICON_BUTTON_RIGHT);
	text_width = zui_draw_text_width(ctx, text);
	button_width = (uint16_t)(text_width + 9U + zui_icon_width(icon));
	height = 12U;
	x = zui_draw_width(ctx);
	y = zui_draw_height(ctx);

	zui_draw_box(ctx, &(struct zui_rect){.x = (int16_t)(x - button_width),
					     .y = (int16_t)(y - height),
					     .width = button_width,
					     .height = height});
	zui_draw_line(ctx,
		      (struct zui_point){.x = (int16_t)(x - button_width - 1U), .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)(x - button_width - 1U),
					 .y = (int16_t)(y - height)});
	zui_draw_line(ctx,
		      (struct zui_point){.x = (int16_t)(x - button_width - 2U), .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)(x - button_width - 2U),
					 .y = (int16_t)(y - height + 1U)});
	zui_draw_line(ctx,
		      (struct zui_point){.x = (int16_t)(x - button_width - 3U), .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)(x - button_width - 3U),
					 .y = (int16_t)(y - height + 2U)});
	zui_draw_invert_color(ctx);
	zui_draw_text(
		ctx,
		(struct zui_point){.x = (int16_t)(x - button_width + 3U), .y = (int16_t)(y - 3U)},
		text);
	zui_draw_icon(ctx,
		      (struct zui_point){.x = (int16_t)(x - 3U - zui_icon_width(icon)),
					 .y = (int16_t)(y - zui_icon_height(icon) - 3U)},
		      icon);
	zui_draw_invert_color(ctx);
}

static void zui_draw_legacy_button_center(struct zui_draw_ctx *ctx, const char *text)
{
	const struct zui_icon *icon;
	uint16_t text_width;
	uint16_t button_width;
	uint16_t height;
	int16_t x;
	uint16_t y;

	if (ctx == NULL || text == NULL || text[0] == '\0') {
		return;
	}

	icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(), ZUI_ASSET_ICON_BUTTON_SELECT);
	text_width = zui_draw_text_width(ctx, text);
	button_width = (uint16_t)(text_width + 5U + zui_icon_width(icon));
	height = 12U;
	x = (int16_t)((zui_draw_width(ctx) - button_width) / 2U);
	y = zui_draw_height(ctx);

	zui_draw_box(ctx, &(struct zui_rect){.x = x,
					     .y = (int16_t)(y - height),
					     .width = button_width,
					     .height = height});
	zui_draw_line(ctx, (struct zui_point){.x = (int16_t)(x - 1), .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)(x - 1), .y = (int16_t)(y - height)});
	zui_draw_line(ctx, (struct zui_point){.x = (int16_t)(x - 2), .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)(x - 2), .y = (int16_t)(y - height + 1U)});
	zui_draw_line(ctx, (struct zui_point){.x = (int16_t)(x - 3), .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)(x - 3), .y = (int16_t)(y - height + 2U)});
	zui_draw_line(
		ctx, (struct zui_point){.x = (int16_t)(x + button_width), .y = (int16_t)y},
		(struct zui_point){.x = (int16_t)(x + button_width), .y = (int16_t)(y - height)});
	zui_draw_line(ctx,
		      (struct zui_point){.x = (int16_t)(x + button_width + 1), .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)(x + button_width + 1),
					 .y = (int16_t)(y - height + 1U)});
	zui_draw_line(ctx,
		      (struct zui_point){.x = (int16_t)(x + button_width + 2), .y = (int16_t)y},
		      (struct zui_point){.x = (int16_t)(x + button_width + 2),
					 .y = (int16_t)(y - height + 2U)});
	zui_draw_invert_color(ctx);
	zui_draw_icon(ctx,
		      (struct zui_point){.x = (int16_t)(x + 1),
					 .y = (int16_t)(y - zui_icon_height(icon) - 3U)},
		      icon);
	zui_draw_text(ctx,
		      (struct zui_point){.x = (int16_t)(x + 4U + zui_icon_width(icon)),
					 .y = (int16_t)(y - 3U)},
		      text);
	zui_draw_invert_color(ctx);
}

void zui_draw_button_hints(struct zui_draw_ctx *ctx, const struct zui_draw_button_hint *hints)
{
	if (ctx == NULL || hints == NULL) {
		return;
	}

	zui_draw_set_font(ctx, ZUI_FONT_SECONDARY);
	zui_draw_legacy_button_left(ctx, hints->left);
	zui_draw_legacy_button_center(ctx, hints->center);
	zui_draw_legacy_button_right(ctx, hints->right);
}

void zui_draw_multiline_text(struct zui_draw_ctx *ctx, const struct zui_rect *rect,
			     enum zui_align horizontal, enum zui_align vertical, const char *text)
{
	char line[64];
	const char *start;
	uint16_t line_height;
	int16_t y;

	if (ctx == NULL || rect == NULL || text == NULL) {
		return;
	}

	line_height = MAX(zui_draw_font_height(ctx), 1U);
	y = rect->y;
	if (vertical == ZUI_ALIGN_CENTER) {
		y = rect->y + (int16_t)(rect->height / 2U);
	}

	start = text;
	while (*start != '\0' && y < rect->y + rect->height) {
		const char *end = strchr(start, '\n');
		size_t len = end == NULL ? strlen(start) : (size_t)(end - start);
		struct zui_point pos = {
			.x = rect->x,
			.y = y,
		};

		len = MIN(len, sizeof(line) - 1U);
		memcpy(line, start, len);
		line[len] = '\0';
		if (horizontal == ZUI_ALIGN_CENTER) {
			pos.x = rect->x + rect->width / 2;
		} else if (horizontal == ZUI_ALIGN_RIGHT) {
			pos.x = rect->x + rect->width;
		}
		zui_draw_text_aligned(ctx, pos, horizontal, ZUI_ALIGN_TOP, line);
		y += line_height;
		if (end == NULL) {
			break;
		}
		start = end + 1;
	}
}

int zui_draw_text_fit_width(struct zui_draw_ctx *ctx, char *text, size_t text_size,
			    uint16_t max_width)
{
	static const char ellipsis[] = "...";
	size_t len;

	if (ctx == NULL || text == NULL || text_size == 0U) {
		return -EINVAL;
	}

	len = zui_draw_text_fit_prepare(text, text_size);

	if (zui_draw_text_width(ctx, text) <= max_width) {
		return 0;
	}

	while (len > 0U && zui_draw_text_width(ctx, text) > max_width) {
		zui_draw_text_fit_truncate(text, &len);
	}

	if (text_size > sizeof(ellipsis) && max_width >= zui_draw_text_width(ctx, ellipsis)) {
		while (len > 0U &&
		       zui_draw_text_width(ctx, text) + zui_draw_text_width(ctx, ellipsis) >
			       max_width) {
			zui_draw_text_fit_truncate(text, &len);
		}
		(void)strncat(text, ellipsis, text_size - strlen(text) - 1U);
	}

	return 1;
}

void zui_draw_text_line_scrolled(struct zui_draw_ctx *ctx, struct zui_point pos, uint16_t width,
				 const char *text, size_t scroll, bool ellipsis)
{
	char line[64];
	size_t len;

	if (ctx == NULL || text == NULL) {
		return;
	}

	len = strlen(text);
	if (scroll >= len) {
		scroll = len;
	}
	(void)snprintk(line, sizeof(line), "%s", &text[scroll]);
	if (ellipsis) {
		(void)zui_draw_text_fit_width(ctx, line, sizeof(line), width);
	}
	zui_draw_text(ctx, pos, line);
}

void zui_draw_text_box(struct zui_draw_ctx *ctx, const struct zui_rect *rect, const char *text,
		       size_t scroll_line, enum zui_draw_text_box_flags flags)
{
	struct zui_rect inner;
	uint16_t line_height;
	int16_t y;
	const char *start;
	size_t line_index = 0U;

	if (ctx == NULL || text == NULL || !zui_draw_rect_valid(rect)) {
		return;
	}

	if ((flags & ZUI_DRAW_TEXT_BOX_FRAME) != 0U) {
		zui_draw_rect(ctx, rect);
		inner = (struct zui_rect){
			.x = rect->x + 2,
			.y = rect->y + 2,
			.width = rect->width > 4U ? rect->width - 4U : rect->width,
			.height = rect->height > 4U ? rect->height - 4U : rect->height,
		};
	} else {
		inner = *rect;
	}

	line_height = MAX(zui_draw_font_height(ctx), 1U);
	y = inner.y;
	start = text;

	while (*start != '\0' && y < inner.y + inner.height) {
		const char *end = strchr(start, '\n');
		size_t len = end == NULL ? strlen(start) : (size_t)(end - start);
		char line[64];

		if (line_index++ < scroll_line) {
			if (end == NULL) {
				break;
			}
			start = end + 1;
			continue;
		}

		len = MIN(len, sizeof(line) - 1U);
		memcpy(line, start, len);
		line[len] = '\0';
		if ((flags & ZUI_DRAW_TEXT_BOX_STRIP_TO_DOTS) != 0U) {
			(void)zui_draw_text_fit_width(ctx, line, sizeof(line), inner.width);
		}
		zui_draw_text(ctx, (struct zui_point){.x = inner.x, .y = y}, line);

		y += line_height;
		if (end == NULL) {
			break;
		}
		start = end + 1;
	}
}

void zui_draw_bubble_frame(struct zui_draw_ctx *ctx, const struct zui_rect *rect)
{
	zui_draw_round_rect(ctx, rect, 3U);
}

void zui_draw_bubble(struct zui_draw_ctx *ctx, const struct zui_rect *rect, struct zui_point tail,
		     const char *text)
{
	if (!zui_draw_rect_valid(rect)) {
		return;
	}

	zui_draw_bubble_frame(ctx, rect);
	zui_draw_line(ctx, tail, (struct zui_point){.x = rect->x + 4, .y = rect->y + rect->height});
	zui_draw_line(ctx, tail,
		      (struct zui_point){.x = rect->x + 12, .y = rect->y + rect->height});
	zui_draw_multiline_text(ctx, rect, ZUI_ALIGN_LEFT, ZUI_ALIGN_TOP, text);
}
