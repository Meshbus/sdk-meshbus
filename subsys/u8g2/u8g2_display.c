/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/display/u8g2.h>

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "u8g2_dump.h"

struct u8g2_context {
	const struct device *display;
	struct display_capabilities caps;
	enum display_pixel_format pixel_format;

	/* u8g2 instance that owns the buffer so we can read rotation/state. */
	u8g2_t *u8g2;

	bool mono_vtiled;
	bool mono_msb_first;
	bool mono_invert;
	struct k_mutex output_mutex;

	/* Scratch used for format conversion; size == caps.x_resolution bytes. */
	uint8_t *scratch;
	size_t scratch_len;

	/* u8x8 display information constructed from display capabilities. */
	u8x8_display_info_t u8x8_info;

	/* Buffer owned by the adapter (u8g2_init()). */
	uint8_t *u8g2_buf;
	size_t u8g2_buf_len;
	uint8_t tile_buf_height;
};

/* Single-instance sample adapter: u8g2's u8x8 has no user_ptr enabled. */
static struct u8g2_context *g_ctx;
static atomic_t output_invert_override;
static atomic_t output_invert_override_valid;

static bool display_desired_invert(void)
{
	if (atomic_get(&output_invert_override_valid) != 0) {
		return atomic_get(&output_invert_override) != 0;
	}

	return IS_ENABLED(CONFIG_U8G2_INVERT);
}

static inline uint8_t bit_reverse8(uint8_t b)
{
	b = (uint8_t)((b & 0xF0U) >> 4) | (uint8_t)((b & 0x0FU) << 4);
	b = (uint8_t)((b & 0xCCU) >> 2) | (uint8_t)((b & 0x33U) << 2);
	b = (uint8_t)((b & 0xAAU) >> 1) | (uint8_t)((b & 0x55U) << 1);
	return b;
}

static void convert_mono_bytes(uint8_t *dst, const uint8_t *src, size_t len,
				 bool invert, bool msb_first)
{
	if (!invert && !msb_first) {
		if (dst != src) {
			memcpy(dst, src, len);
		}
		return;
	}

	for (size_t i = 0; i < len; i++) {
		uint8_t b = src[i];

		if (invert) {
			b ^= 0xFFU;
		}
		if (msb_first) {
			b = bit_reverse8(b);
		}
		dst[i] = b;
	}
}

static bool pixel_format_supported(const struct display_capabilities *caps,
				  enum display_pixel_format fmt)
{
	return (caps->supported_pixel_formats & (uint32_t)fmt) != 0U;
}

static bool mono_invert_required(enum display_pixel_format fmt, bool desired_invert)
{
	/*
	 * U8G2's buffer in this demo is treated as MONO01 semantics:
	 *   0 = black, 1 = white.
	 *
	 * Zephyr's pixel formats:
	 *   MONO01: 0 = black, 1 = white
	 *   MONO10: 1 = black, 0 = white
	 *
	 * So to get the desired on-screen polarity we may need a byte-wise XOR.
	 */
	if (!desired_invert) {
		return fmt == PIXEL_FORMAT_MONO10;
	}

	return fmt == PIXEL_FORMAT_MONO01;
}

static int choose_mono_pixel_format(struct u8g2_context *ctx, bool desired_invert)
{
	/* Prefer a format that avoids per-byte inversion when possible. */
	enum display_pixel_format preferred = desired_invert ?
		PIXEL_FORMAT_MONO10 : PIXEL_FORMAT_MONO01;
	enum display_pixel_format fallback = desired_invert ?
		PIXEL_FORMAT_MONO01 : PIXEL_FORMAT_MONO10;

	if (pixel_format_supported(&ctx->caps, preferred)) {
		ctx->pixel_format = preferred;
		return 0;
	}

	if (pixel_format_supported(&ctx->caps, fallback)) {
		ctx->pixel_format = fallback;
		return 0;
	}

	return -ENOTSUP;
}

static int display_output_invert_apply(struct u8g2_context *ctx, bool desired_invert)
{
	int err = choose_mono_pixel_format(ctx, desired_invert);

	if (err != 0) {
		return err;
	}

	err = display_set_pixel_format(ctx->display, ctx->pixel_format);
	if (err != 0) {
		/* Keep rendering through the active format when the driver cannot switch. */
		display_get_capabilities(ctx->display, &ctx->caps);
		ctx->pixel_format = ctx->caps.current_pixel_format;
		if (ctx->pixel_format != PIXEL_FORMAT_MONO01 &&
		    ctx->pixel_format != PIXEL_FORMAT_MONO10) {
			return err;
		}
	} else {
		ctx->caps.current_pixel_format = ctx->pixel_format;
	}

	ctx->mono_invert = mono_invert_required(ctx->pixel_format, desired_invert);
	return 0;
}

static int display_write_row(struct u8g2_context *ctx,
					uint16_t x_px, uint16_t y_px,
					size_t width_bytes, const uint8_t *src)
{
	struct display_buffer_descriptor desc = {
		.width = (uint16_t)width_bytes,
		.height = 8,
		.pitch = (uint16_t)width_bytes,
		.buf_size = width_bytes,
	};

	const uint8_t *payload = src;
	bool needs_convert = ctx->mono_invert || ctx->mono_msb_first;

	if (needs_convert) {
		if (width_bytes > ctx->scratch_len || ctx->scratch == NULL) {
			return -ENOMEM;
		}
		convert_mono_bytes(ctx->scratch, src, width_bytes,
				  ctx->mono_invert, ctx->mono_msb_first);
		payload = ctx->scratch;
	}

	return display_write(ctx->display, x_px, y_px, &desc, payload);
}

static uint8_t gpio_and_delay_cb(
	U8X8_UNUSED u8x8_t *u8x8,
	uint8_t msg,
	U8X8_UNUSED uint8_t arg_int,
	U8X8_UNUSED void *arg_ptr)
{
	/* No GPIO/pin toggling in this backend. */
	switch (msg) {
	case U8X8_MSG_GPIO_AND_DELAY_INIT:
	case U8X8_MSG_DELAY_MILLI:
	case U8X8_MSG_DELAY_10MICRO:
	case U8X8_MSG_DELAY_100NANO:
	case U8X8_MSG_DELAY_NANO:
		return 1;
	default:
		return 1;
	}
}

static uint8_t display_cb_locked(struct u8g2_context *ctx, u8x8_t *u8x8, uint8_t msg,
				 uint8_t arg_int, void *arg_ptr)
{
	switch (msg) {
	case U8X8_MSG_DISPLAY_SETUP_MEMORY:
		u8x8_d_helper_display_setup_memory(u8x8, &ctx->u8x8_info);
		return 1;

	case U8X8_MSG_DISPLAY_INIT:
		/* Display device is initialized by Zephyr. */
		return 1;

	case U8X8_MSG_DISPLAY_SET_POWER_SAVE:
		if (arg_int) {
			(void)display_blanking_on(ctx->display);
		} else {
			(void)display_blanking_off(ctx->display);
		}
		return 1;

	case U8X8_MSG_DISPLAY_SET_FLIP_MODE:
	case U8X8_MSG_DISPLAY_SET_CONTRAST:
		/* Not supported through Display API in a generic way. */
		return 1;

	case U8X8_MSG_DISPLAY_REFRESH:
		/* Most Zephyr display drivers refresh on write; keep as no-op. */
		return 1;

	case U8X8_MSG_DISPLAY_DRAW_TILE: {
		const u8x8_tile_t *tile = (const u8x8_tile_t *)arg_ptr;
		const uint8_t *src = tile->tile_ptr;

		if (!ctx->mono_vtiled) {
			return 0;
		}

		/* Each tile is 8 bytes; for VTILED mono, 1 byte == 1 pixel column (8px tall). */
		const size_t tile_seq_bytes = (size_t)tile->cnt * 8U;
		const uint8_t repeat = arg_int;

		/* Fast-path: common flushes are full-width rows, arg_int==1, x_pos==0. */
		if (repeat == 1U && tile->x_pos == 0U && tile_seq_bytes == ctx->caps.x_resolution) {
			int err = display_write_row(ctx, 0U, (uint16_t)tile->y_pos * 8U,
						   tile_seq_bytes, src);
			return (err == 0) ? 1 : 0;
		}

		/* Optimized clear/fill path: cnt==1, repeat spans full width. */
		if (tile->cnt == 1U && tile->x_pos == 0U &&
		    ((size_t)repeat * 8U) == ctx->caps.x_resolution) {
			if (ctx->scratch == NULL || ctx->scratch_len < ctx->caps.x_resolution) {
				return 0;
			}

			for (size_t i = 0; i < ctx->caps.x_resolution; i += 8U) {
				memcpy(&ctx->scratch[i], src, 8U);
			}

			/* Apply optional invert/bit-order conversion in-place. */
			convert_mono_bytes(ctx->scratch, ctx->scratch, ctx->caps.x_resolution,
					  ctx->mono_invert, ctx->mono_msb_first);

			struct display_buffer_descriptor desc = {
				.width = (uint16_t)ctx->caps.x_resolution,
				.height = 8,
				.pitch = (uint16_t)ctx->caps.x_resolution,
				.buf_size = ctx->caps.x_resolution,
			};
			int err = display_write(ctx->display, 0U, (uint16_t)tile->y_pos * 8U,
						&desc, ctx->scratch);
			return (err == 0) ? 1 : 0;
		}

		/* Generic path: repeat the tile sequence 'arg_int' times. */
		for (uint8_t r = 0; r < repeat; r++) {
			uint16_t x_tile = (uint16_t)tile->x_pos + (uint16_t)r * (uint16_t)tile->cnt;
			uint16_t x_px = x_tile * 8U;
			uint16_t y_px = (uint16_t)tile->y_pos * 8U;

			int err = display_write_row(ctx, x_px, y_px, tile_seq_bytes, src);
			if (err != 0) {
				return 0;
			}
		}

		return 1;
	}

	default:
		return 0;
	}
}

static uint8_t display_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
	struct u8g2_context *ctx = g_ctx;

	if (ctx == NULL) {
		return 0;
	}

	k_mutex_lock(&ctx->output_mutex, K_FOREVER);
	uint8_t result = display_cb_locked(ctx, u8x8, msg, arg_int, arg_ptr);
	k_mutex_unlock(&ctx->output_mutex);

	return result;
}

static uint32_t rotation_cb_to_orientation(const u8g2_cb_t *cb)
{
	/* Keep values aligned with the SSD1306 dump orientation encoding (0..3). */
	if (cb == U8G2_R0) {
		return 0U; /* horizontal */
	}
	if (cb == U8G2_R2) {
		return 1U; /* horizontal flip */
	}
	if (cb == U8G2_R3) {
		return 2U; /* vertical */
	}
	if (cb == U8G2_R1) {
		return 3U; /* vertical flip */
	}
	return 0U;
}

int u8g2_display_get_dump_info(struct u8g2_dump_info *info)
{
	if (info == NULL) {
		return -EINVAL;
	}

	struct u8g2_context *ctx = g_ctx;
	if (ctx == NULL || ctx->u8g2_buf == NULL) {
		return -ENODEV;
	}

	/* The dump tooling expects a full framebuffer in SSD1306 page format. */
	const size_t full_len = (size_t)ctx->caps.x_resolution * (size_t)(ctx->caps.y_resolution / 8U);
	if (ctx->tile_buf_height != (uint8_t)(ctx->caps.y_resolution / 8U) ||
	    ctx->u8g2_buf_len != full_len) {
		return -ENOTSUP;
	}

	info->buf = ctx->u8g2_buf;
	info->len = ctx->u8g2_buf_len;
	info->width = (uint16_t)ctx->caps.x_resolution;
	info->height = (uint16_t)ctx->caps.y_resolution;
	info->orientation = rotation_cb_to_orientation((ctx->u8g2 != NULL) ? ctx->u8g2->cb : NULL);
	return 0;
}

static void display_deinit(struct u8g2_context *ctx)
{
	if (ctx == NULL) {
		return;
	}

	if (g_ctx == ctx) {
		g_ctx = NULL;
	}

	if (ctx->u8g2_buf != NULL) {
		k_free(ctx->u8g2_buf);
		ctx->u8g2_buf = NULL;
		ctx->u8g2_buf_len = 0U;
		ctx->tile_buf_height = 0U;
	}

	if (ctx->scratch != NULL) {
		k_free(ctx->scratch);
		ctx->scratch = NULL;
		ctx->scratch_len = 0U;
	}
}

static int display_init(struct u8g2_context *ctx, const struct device *display)
{
	if (ctx == NULL || display == NULL) {
		return -EINVAL;
	}

	memset(ctx, 0, sizeof(*ctx));
	ctx->display = display;
	k_mutex_init(&ctx->output_mutex);

	display_get_capabilities(display, &ctx->caps);

	const bool desired_invert = display_desired_invert();
	int err = display_output_invert_apply(ctx, desired_invert);
	if (err != 0) {
		return err;
	}

	ctx->mono_vtiled = (ctx->caps.screen_info & SCREEN_INFO_MONO_VTILED) != 0U;
	ctx->mono_msb_first = (ctx->caps.screen_info & SCREEN_INFO_MONO_MSB_FIRST) != 0U;

	if (!ctx->mono_vtiled) {
		return -ENOTSUP;
	}

	if ((ctx->caps.x_resolution % 8U) != 0U) {
		return -EINVAL;
	}

	if ((ctx->caps.y_resolution % 8U) != 0U) {
		ctx->caps.y_resolution = (ctx->caps.y_resolution / 8U) * 8U;
	}

	ctx->scratch_len = ctx->caps.x_resolution;
	ctx->scratch = k_malloc(ctx->scratch_len);
	if (ctx->scratch == NULL) {
		return -ENOMEM;
	}

	/* Build a minimal u8x8_display_info_t based on Zephyr capabilities. */
	memset(&ctx->u8x8_info, 0, sizeof(ctx->u8x8_info));
	ctx->u8x8_info.tile_width = (uint8_t)(ctx->caps.x_resolution / 8U);
	ctx->u8x8_info.tile_height = (uint8_t)(ctx->caps.y_resolution / 8U);
	ctx->u8x8_info.pixel_width = (uint16_t)ctx->caps.x_resolution;
	ctx->u8x8_info.pixel_height = (uint16_t)ctx->caps.y_resolution;
	ctx->u8x8_info.default_x_offset = 0;
	ctx->u8x8_info.flipmode_x_offset = 0;

	return 0;
}

static int display_setup_u8g2(
	u8g2_t *u8g2,
	struct u8g2_context *ctx,
	uint8_t *buf,
	uint8_t tile_buf_height,
	const u8g2_cb_t *rotation)
{
	if (u8g2 == NULL || ctx == NULL || buf == NULL || rotation == NULL) {
		return -EINVAL;
	}

	g_ctx = ctx;

	u8g2_SetupDisplay(u8g2, display_cb,
			 u8x8_cad_empty, u8x8_byte_empty,
			 gpio_and_delay_cb);

	u8g2_SetupBuffer(u8g2, buf, tile_buf_height,
			 u8g2_ll_hvline_vertical_top_lsb, rotation);

	u8g2_InitDisplay(u8g2);
	/* Keep panel blanked during initial framebuffer prime to avoid startup snow. */
	u8g2_SetPowerSave(u8g2, 1);
	u8g2_ClearBuffer(u8g2);
	u8g2_SendBuffer(u8g2);
	u8g2_SetPowerSave(u8g2, 0);

	return 0;
}

int u8g2_init(u8g2_t *u8g2, const struct device *display, const u8g2_cb_t *rotation)
{
	if (u8g2 == NULL || display == NULL || rotation == NULL) {
		return -EINVAL;
	}

	/* Replace any existing global instance. */
	u8g2_deinit();

	struct u8g2_context *ctx = k_malloc(sizeof(*ctx));
	if (ctx == NULL) {
		return -ENOMEM;
	}
	memset(ctx, 0, sizeof(*ctx));

	int err = display_init(ctx, display);
	if (err != 0) {
		k_free(ctx);
		return err;
	}

	uint8_t tile_buf_height;
#if defined(CONFIG_U8G2_BUFFER_MODE_PAGE8)
	tile_buf_height = 1U;
#else
	tile_buf_height = (uint8_t)(ctx->caps.y_resolution / 8U);
#endif
	if (tile_buf_height == 0U) {
		display_deinit(ctx);
		k_free(ctx);
		return -EINVAL;
	}

	const size_t buf_len = (size_t)ctx->caps.x_resolution * (size_t)tile_buf_height;
	ctx->u8g2_buf = k_malloc(buf_len);
	if (ctx->u8g2_buf == NULL) {
		display_deinit(ctx);
		k_free(ctx);
		return -ENOMEM;
	}
	ctx->u8g2_buf_len = buf_len;
	ctx->tile_buf_height = tile_buf_height;
	ctx->u8g2 = u8g2;

	err = display_setup_u8g2(u8g2, ctx, ctx->u8g2_buf,
					    tile_buf_height, rotation);
	if (err != 0) {
		display_deinit(ctx);
		k_free(ctx);
		return err;
	}

	/* Publish the instance for callbacks and getters. */
	g_ctx = ctx;

	return 0;
}

void u8g2_deinit(void)
{
	if (g_ctx == NULL) {
		return;
	}

	struct u8g2_context *ctx = g_ctx;
	g_ctx = NULL;

	display_deinit(ctx);
	k_free(ctx);
}

void u8g2_get_capabilities(struct display_capabilities *caps)
{
	if (caps == NULL || g_ctx == NULL) {
		return;
	}

	*caps = g_ctx->caps;
}

enum display_pixel_format u8g2_get_pixel_format(void)
{
	if (g_ctx == NULL) {
		return 0;
	}

	return g_ctx->pixel_format;
}

int u8g2_set_output_invert(bool invert)
{
	atomic_set(&output_invert_override, invert ? 1 : 0);
	atomic_set(&output_invert_override_valid, 1);

	struct u8g2_context *ctx = g_ctx;

	if (ctx == NULL) {
		return 0;
	}

	k_mutex_lock(&ctx->output_mutex, K_FOREVER);
	int err = display_output_invert_apply(ctx, invert);
	k_mutex_unlock(&ctx->output_mutex);

	return err;
}
