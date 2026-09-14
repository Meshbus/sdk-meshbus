
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <display/u8g2.h>

LOG_MODULE_REGISTER(u8g2, LOG_LEVEL_INF);

#define REPORT_EVERY_FRAMES 30U

enum demo_mode {
	DEMO_MODE_UI = 0,
	DEMO_MODE_TEXT,
	DEMO_MODE_HVLINE,
	DEMO_MODE_BOXES,
	DEMO_MODE_CIRCLES,
	DEMO_MODE_GEOM,
	DEMO_MODE_BITMAP,
	DEMO_MODE_PIXELS,
	DEMO_MODE_COUNT,
};

/* Last computed stats, updated in report_stats() (once every REPORT_EVERY_FRAMES). */
static uint32_t g_fps_x1000;
static uint32_t g_draw_pct_x10;
static uint32_t g_flush_pct_x10;
static enum demo_mode g_stats_mode;

static const char *demo_mode_name(enum demo_mode mode)
{
	switch (mode) {
	case DEMO_MODE_UI:
		return "ui";
	case DEMO_MODE_TEXT:
		return "text";
	case DEMO_MODE_HVLINE:
		return "hvline";
	case DEMO_MODE_BOXES:
		return "boxes";
	case DEMO_MODE_CIRCLES:
		return "circles";
	case DEMO_MODE_GEOM:
		return "geom";
	case DEMO_MODE_BITMAP:
		return "bitmap";
	case DEMO_MODE_PIXELS:
		return "pixels";
	default:
		return "?";
	}
}

static uint32_t lcg_next(uint32_t *state)
{
	/* Cheap deterministic PRNG, good enough for a stress test. */
	*state = (*state * 1664525U) + 1013904223U;
	return *state;
}

static uint64_t cycles_to_ns(uint64_t cycles)
{
	return k_cyc_to_ns_floor64(cycles);
}

static void report_stats(enum demo_mode mode, uint32_t frames, uint64_t sum_frame_ns,
			 uint64_t sum_draw_ns, uint64_t sum_flush_ns)
{
	uint64_t fps_x1000;
	uint64_t draw_pct_x10;
	uint64_t flush_pct_x10;

	if (frames == 0U) {
		return;
	}

	/* fps_x1000 = frames * 1e12 / sum_frame_ns */
	fps_x1000 = (uint64_t)frames * 1000000000000ULL;
	fps_x1000 /= (sum_frame_ns ? sum_frame_ns : 1ULL);

	draw_pct_x10 = (sum_draw_ns * 1000ULL);
	draw_pct_x10 /= (sum_frame_ns ? sum_frame_ns : 1ULL);

	flush_pct_x10 = (sum_flush_ns * 1000ULL);
	flush_pct_x10 /= (sum_frame_ns ? sum_frame_ns : 1ULL);

	LOG_INF("avg(%s) fps=%u.%03u, draw=%u.%u%%, flush=%u.%u%%", demo_mode_name(mode),
		(uint32_t)(fps_x1000 / 1000ULL), (uint32_t)(fps_x1000 % 1000ULL),
		(uint32_t)(draw_pct_x10 / 10ULL), (uint32_t)(draw_pct_x10 % 10ULL),
		(uint32_t)(flush_pct_x10 / 10ULL), (uint32_t)(flush_pct_x10 % 10ULL));

	/* Expose the latest numbers to the on-screen HUD. */
	g_stats_mode = mode;
	g_fps_x1000 = (uint32_t)MIN(fps_x1000, (uint64_t)UINT32_MAX);
	g_draw_pct_x10 = (uint32_t)MIN(draw_pct_x10, (uint64_t)1000ULL);
	g_flush_pct_x10 = (uint32_t)MIN(flush_pct_x10, (uint64_t)1000ULL);
}

static enum demo_mode demo_mode_for_frame(uint32_t frame)
{
#if defined(CONFIG_U8G2_STRESS_MODE_CYCLE)
	uint32_t per = (uint32_t)CONFIG_U8G2_STRESS_MODE_FRAMES;
	if (per == 0U) {
		per = 120U;
	}
	return (enum demo_mode)((frame / per) % (uint32_t)DEMO_MODE_COUNT);
#else
	ARG_UNUSED(frame);
	return DEMO_MODE_UI;
#endif
}

static void draw_scene(u8g2_t *u8g2, uint32_t frame, enum demo_mode mode)
{
	const u8g2_uint_t w = u8g2_GetDisplayWidth(u8g2);
	const u8g2_uint_t h = u8g2_GetDisplayHeight(u8g2);
	const uint32_t level = (uint32_t)CONFIG_U8G2_STRESS_LEVEL;
	uint32_t rng = frame ^ (level * 0x9E3779B9U);
	const u8g2_uint_t header_h = (h >= 16) ? 12 : 0;
	const u8g2_uint_t footer_h = (h >= 24) ? 10 : 0;
	const u8g2_uint_t body_y0 = header_h;
	const u8g2_uint_t body_h = (u8g2_uint_t)(h - header_h - footer_h);

	/* Always start from a known background. */
	u8g2_SetMaxClipWindow(u8g2);
	u8g2_SetDrawColor(u8g2, 0);
	u8g2_DrawBox(u8g2, 0, 0, w, h);
	u8g2_SetDrawColor(u8g2, 1);

	/* Header: white bar with black text (looks crisp on mono panels). */
	if (header_h > 0) {
		u8g2_DrawBox(u8g2, 0, 0, w, header_h);
		u8g2_SetDrawColor(u8g2, 0);
		u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
		u8g2_DrawStr(u8g2, 2, 10, "U8G2");

		char right[32];
		if (g_stats_mode == mode && g_fps_x1000 != 0U) {
			(void)snprintf(right, sizeof(right), "%s %u.%02u", demo_mode_name(mode),
				       (unsigned)(g_fps_x1000 / 1000U),
				       (unsigned)((g_fps_x1000 % 1000U) / 10U));
		} else {
			(void)snprintf(right, sizeof(right), "%s --", demo_mode_name(mode));
		}
		u8g2_long_t sw = u8g2_GetStrWidth(u8g2, right);
		u8g2_uint_t x = 2;
		if (sw > 0 && (u8g2_uint_t)sw + 2U < w) {
			x = (u8g2_uint_t)(w - (u8g2_uint_t)sw - 2U);
		}
		u8g2_DrawStr(u8g2, x, 10, right);

		u8g2_SetDrawColor(u8g2, 1);
	}

	/* Subtle outer border. */
	u8g2_DrawFrame(u8g2, 0, 0, w, h);

	/* Moving clip window (body only) to exercise clipping/intersection paths. */
	u8g2_uint_t clip_x0 = 0;
	u8g2_uint_t clip_y0 = body_y0;
	u8g2_uint_t avail_w = w;
	u8g2_uint_t avail_h = body_h;
	u8g2_uint_t clip_w = (avail_w > 56) ? (u8g2_uint_t)(avail_w - 8) : avail_w;
	u8g2_uint_t clip_h = (avail_h > 32) ? (u8g2_uint_t)(avail_h - 4) : avail_h;

	if (clip_w < avail_w) {
		clip_x0 = (u8g2_uint_t)(frame % (uint32_t)(avail_w - clip_w + 1U));
	}
	if (clip_h < avail_h) {
		clip_y0 = (u8g2_uint_t)(clip_y0 + (frame % (uint32_t)(avail_h - clip_h + 1U)));
	}
	u8g2_SetClipWindow(u8g2, clip_x0, clip_y0, (u8g2_uint_t)(clip_x0 + clip_w),
			   (u8g2_uint_t)(clip_y0 + clip_h));

	/* Workload: pick a pattern and scale by stress level. */
	switch (mode) {
	case DEMO_MODE_UI: {
		u8g2_uint_t bar_w = (w > 40) ? 30 : (w / 2);
		u8g2_uint_t bar_x = 0;
		if (w > bar_w + 2) {
			bar_x = (u8g2_uint_t)(frame % (uint32_t)(w - bar_w - 1U));
		}
		u8g2_DrawBox(u8g2, bar_x, (u8g2_uint_t)(body_y0 + 2), bar_w, 6);

		/* Dense widget grid (boxes + rounded frames) */
		const u8g2_uint_t cell = 8;
		u8g2_uint_t rows = (body_h > 12) ? (body_h - 10) / cell : 0;
		u8g2_uint_t cols = (w > 2) ? (w - 2) / cell : 0;
		uint32_t max_cells = MIN((uint32_t)(rows * cols), (uint32_t)(level * 12U));
		for (uint32_t i = 0; i < max_cells; i++) {
			u8g2_uint_t cx = (u8g2_uint_t)((i % (cols ? cols : 1U)) * cell + 1U);
			u8g2_uint_t cy =
				(u8g2_uint_t)((i / (cols ? cols : 1U)) * cell + body_y0 + 10U);
			u8g2_DrawRFrame(u8g2, cx, cy, cell - 1, cell - 1, 2);
			if ((i + frame) & 1U) {
				u8g2_DrawBox(u8g2, cx + 2, cy + 2, cell - 5, cell - 5);
			}
		}

		/* Text layout + truncation/cropping (a common UI workload) */
		u8g2_SetFont(u8g2, u8g2_font_u8glib_4_tf);
		for (uint32_t i = 0; i < MIN(level, 6U); i++) {
			char line[32];
			(void)snprintf(line, sizeof(line), "row%u v=%lu", (unsigned)i,
				       (unsigned long)frame);
			u8g2_DrawStr(u8g2, 2, (u8g2_uint_t)(body_y0 + 18U + i * 7U), line);
		}
	} break;

	case DEMO_MODE_TEXT: {
		/* Text-heavy page: glyph rendering, UTF-8 decode, kerning/layout paths. */
		u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
		u8g2_DrawStr(u8g2, 2, (u8g2_uint_t)(body_y0 + 10), "FPS / draw / flush HUD");

		u8g2_SetFont(u8g2, u8g2_font_u8glib_4_tf);
		for (uint32_t i = 0; i < MIN(level * 2U, 16U); i++) {
			char line[40];
			(void)snprintf(line, sizeof(line), "%02u  frame=%lu  seed=%lx", (unsigned)i,
				       (unsigned long)frame,
				       (unsigned long)(0xC0FFEEUL ^ (unsigned long)(i * 1337UL)));
			u8g2_DrawStr(u8g2, 2, (u8g2_uint_t)(body_y0 + 22U + (u8g2_uint_t)i * 7U),
				     line);
			if ((u8g2_uint_t)(body_y0 + 22U + (u8g2_uint_t)i * 7U) >
			    (body_y0 + body_h)) {
				break;
			}
		}

		/* A couple UTF-8 strings if unicode is enabled in the vendored u8g2. */
		u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
		u8g2_DrawUTF8(u8g2, 2, (u8g2_uint_t)(body_y0 + body_h - 2U), "UTF-8: Hello 世界");
	} break;

	case DEMO_MODE_HVLINE: {
		/* Fast-path primitives (H/V lines) to stress span drawing and clipping. */
		uint32_t spans = 32U + (level * 32U);
		for (uint32_t i = 0; i < spans; i++) {
			(void)lcg_next(&rng);
			u8g2_uint_t y = (u8g2_uint_t)(body_y0 + (rng % (uint32_t)MAX(1U, body_h)));
			u8g2_DrawHLine(u8g2, 0, y, w);

			(void)lcg_next(&rng);
			u8g2_uint_t x = (u8g2_uint_t)(rng % (uint32_t)w);
			u8g2_DrawVLine(u8g2, x, body_y0, body_h);
		}
	} break;

	case DEMO_MODE_BOXES: {
		/* Rect/rounded-rect page: fill + outline patterns. */
		uint32_t boxes = 12U + (level * 10U);
		for (uint32_t i = 0; i < boxes; i++) {
			(void)lcg_next(&rng);
			u8g2_uint_t bw = (u8g2_uint_t)(8U + (rng % (uint32_t)MAX(1U, w / 2U)));
			(void)lcg_next(&rng);
			u8g2_uint_t bh = (u8g2_uint_t)(6U + (rng % (uint32_t)MAX(1U, body_h / 2U)));
			(void)lcg_next(&rng);
			u8g2_uint_t x = (u8g2_uint_t)(rng % (uint32_t)MAX(1U, (uint32_t)w - bw));
			(void)lcg_next(&rng);
			u8g2_uint_t y =
				(u8g2_uint_t)(body_y0 +
					      (rng % (uint32_t)MAX(1U, (uint32_t)body_h - bh)));

			if ((i + frame) & 1U) {
				u8g2_DrawFrame(u8g2, x, y, bw, bh);
				u8g2_DrawBox(u8g2, (u8g2_uint_t)(x + 2U), (u8g2_uint_t)(y + 2U),
					     (u8g2_uint_t)MAX(1U, (uint32_t)bw - 4U),
					     (u8g2_uint_t)MAX(1U, (uint32_t)bh - 4U));
			} else {
				u8g2_DrawRFrame(u8g2, x, y, bw, bh, 2);
				u8g2_DrawRBox(u8g2, (u8g2_uint_t)(x + 1U), (u8g2_uint_t)(y + 1U),
					      (u8g2_uint_t)MAX(1U, (uint32_t)bw - 2U),
					      (u8g2_uint_t)MAX(1U, (uint32_t)bh - 2U), 2);
			}
		}
	} break;

	case DEMO_MODE_CIRCLES: {
		/* Circle/ellipse page: arc stepping + fill routines. */
		u8g2_uint_t cx = (u8g2_uint_t)(w / 2U);
		u8g2_uint_t cy = (u8g2_uint_t)(body_y0 + body_h / 2U);
		u8g2_uint_t max_r = (u8g2_uint_t)MIN(w, body_h);
		max_r = (u8g2_uint_t)((max_r > 10U) ? (max_r / 2U) : 3U);

		for (u8g2_uint_t r = 2; r < max_r; r = (u8g2_uint_t)(r + 2U)) {
			if ((r + frame) & 1U) {
				u8g2_DrawCircle(u8g2, cx, cy, r, U8G2_DRAW_ALL);
			} else {
				u8g2_DrawEllipse(u8g2, cx, cy, r, (u8g2_uint_t)(r / 2U),
						 U8G2_DRAW_ALL);
			}
			if (r > (u8g2_uint_t)(2U + level * 2U)) {
				break;
			}
		}

		uint32_t discs = 2U + level;
		for (uint32_t i = 0; i < discs; i++) {
			(void)lcg_next(&rng);
			u8g2_uint_t x = (u8g2_uint_t)(rng % (uint32_t)w);
			(void)lcg_next(&rng);
			u8g2_uint_t y = (u8g2_uint_t)(body_y0 + (rng % (uint32_t)MAX(1U, body_h)));
			(void)lcg_next(&rng);
			u8g2_uint_t r = (u8g2_uint_t)(2U + (rng % 10U));
			u8g2_DrawDisc(u8g2, x, y, r, U8G2_DRAW_ALL);
		}
	} break;

	case DEMO_MODE_GEOM: {
		u8g2_uint_t cx = (u8g2_uint_t)(w / 2U);
		u8g2_uint_t cy = (u8g2_uint_t)(body_y0 + body_h / 2U);
		u8g2_uint_t max_r = (u8g2_uint_t)MIN(w, body_h);
		max_r = (u8g2_uint_t)((max_r > 6U) ? ((max_r / 2U) - 2U) : 1U);

		uint32_t lines = 24U + (level * 24U);
		for (uint32_t i = 0; i < lines; i++) {
			(void)lcg_next(&rng);
			u8g2_uint_t x = (u8g2_uint_t)(rng % (uint32_t)w);
			(void)lcg_next(&rng);
			u8g2_uint_t y = (u8g2_uint_t)(rng % (uint32_t)h);
			u8g2_DrawLine(u8g2, cx, cy, x, y);
		}

		for (u8g2_uint_t r = 2; r < max_r; r = (u8g2_uint_t)(r + 2U)) {
			if ((r + frame) & 2U) {
				u8g2_DrawCircle(u8g2, cx, cy, r, U8G2_DRAW_ALL);
			} else {
				u8g2_DrawEllipse(u8g2, cx, cy, r, (u8g2_uint_t)(r / 2U),
						 U8G2_DRAW_ALL);
			}
			if (r > (u8g2_uint_t)(2U + (level * 3U))) {
				break;
			}
		}

		/* Some filled primitives to stress pixel spans */
		if (w > 24 && h > 24) {
			u8g2_DrawFilledEllipse(u8g2, (u8g2_uint_t)(w - 14), 20, 10, 6,
					       U8G2_DRAW_ALL);
			u8g2_DrawTriangle(u8g2, 2, (int16_t)(h - 2), (int16_t)(w / 2), 14,
					  (int16_t)(w - 2), (int16_t)(h - 2));
		}
	} break;

	case DEMO_MODE_BITMAP: {
		static const uint8_t xbm_32x16[] = {
			0xFF, 0xFF, 0xFF, 0xFF, 0x81, 0x00, 0x00, 0x81, 0xBD, 0x7E, 0x7E,
			0xBD, 0xA5, 0x42, 0x42, 0xA5, 0xA5, 0x5A, 0x5A, 0xA5, 0xBD, 0x66,
			0x66, 0xBD, 0x81, 0x00, 0x00, 0x81, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0x81, 0x00, 0x00, 0x81, 0xBD, 0x7E, 0x7E, 0xBD,
			0xA5, 0x42, 0x42, 0xA5, 0xA5, 0x5A, 0x5A, 0xA5, 0xBD, 0x66, 0x66,
			0xBD, 0x81, 0x00, 0x00, 0x81, 0xFF, 0xFF, 0xFF, 0xFF,
		};

		u8g2_SetBitmapMode(u8g2, 1);
		uint32_t blits = 4U + (level * 2U);
		for (uint32_t i = 0; i < blits; i++) {
			(void)lcg_next(&rng);
			u8g2_uint_t x = 0;
			if (w > 32) {
				x = (u8g2_uint_t)(rng % (uint32_t)(w - 32));
			}
			(void)lcg_next(&rng);
			u8g2_uint_t y = 0;
			if (h > 16) {
				y = (u8g2_uint_t)(rng % (uint32_t)(h - 16));
			}
			u8g2_DrawXBM(u8g2, x, y, 32, 16, xbm_32x16);
		}

		/* Overlay some text to mix bitmap + font */
		u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
		u8g2_DrawStr(u8g2, 2, 22, "xbm blit storm");
	} break;

	case DEMO_MODE_PIXELS: {
		/* Many pixel writes (worst case for CPU), plus HVLines. */
		uint32_t px = 200U + (level * 300U);
		for (uint32_t i = 0; i < px; i++) {
			(void)lcg_next(&rng);
			u8g2_uint_t x = (u8g2_uint_t)(rng % (uint32_t)w);
			(void)lcg_next(&rng);
			u8g2_uint_t y = (u8g2_uint_t)(rng % (uint32_t)h);
			u8g2_DrawPixel(u8g2, x, y);
		}

		uint32_t spans = 16U + (level * 8U);
		for (uint32_t i = 0; i < spans; i++) {
			(void)lcg_next(&rng);
			u8g2_uint_t y = (u8g2_uint_t)(rng % (uint32_t)h);
			u8g2_DrawHLine(u8g2, 0, y, w);
		}
	} break;

	default:
		break;
	}

	/* Reset to full-screen drawing for the status line. */
	u8g2_SetMaxClipWindow(u8g2);

	/* Footer: small performance bars + frame counter. */
	if (footer_h > 0) {
		u8g2_uint_t y0 = (u8g2_uint_t)(h - footer_h);
		u8g2_SetFont(u8g2, u8g2_font_u8glib_4_tf);
		char buf[32];
		(void)snprintf(buf, sizeof(buf), "f%lu L%lu", (unsigned long)frame,
			       (unsigned long)level);
		u8g2_DrawStr(u8g2, 2, (u8g2_uint_t)(y0 + footer_h - 2U), buf);

		/* Right-aligned bar chart: draw% and flush% */
		u8g2_uint_t bar_w = (w > 64) ? 60 : (u8g2_uint_t)(w / 2U);
		u8g2_uint_t bar_x = (u8g2_uint_t)(w - bar_w - 2U);
		u8g2_uint_t bar_h = 3;
		u8g2_uint_t draw_px = (u8g2_uint_t)((bar_w * g_draw_pct_x10) / 1000U);
		u8g2_uint_t flush_px = (u8g2_uint_t)((bar_w * g_flush_pct_x10) / 1000U);

		u8g2_DrawFrame(u8g2, bar_x, y0 + 1, bar_w, bar_h);
		u8g2_DrawBox(u8g2, bar_x, y0 + 1, draw_px, bar_h);

		u8g2_DrawFrame(u8g2, bar_x, (u8g2_uint_t)(y0 + 5), bar_w, bar_h);
		/* flush bar uses XOR so it remains visible even if it overlaps draw */
		u8g2_SetDrawColor(u8g2, 2);
		u8g2_DrawBox(u8g2, bar_x, (u8g2_uint_t)(y0 + 5), flush_px, bar_h);
		u8g2_SetDrawColor(u8g2, 1);
	}
}

int main(void)
{
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display)) {
		LOG_ERR("Display device not ready");
		return 0;
	}

	u8g2_t u8g2;
	int err = u8g2_init(&u8g2, display, U8G2_R0);
	if (err != 0) {
		LOG_ERR("u8g2 init failed: %d", err);
		return 0;
	}

	struct display_capabilities caps;
	u8g2_get_capabilities(&caps);

	LOG_INF("Display: %ux%u fmt=%d screen_info=0x%x", caps.x_resolution, caps.y_resolution,
		u8g2_get_pixel_format(), caps.screen_info);

	uint64_t sum_draw_ns = 0;
	uint64_t sum_flush_ns = 0;
	uint64_t sum_frame_ns = 0;
	uint32_t frames = 0;
	uint32_t frame = 0;
	enum demo_mode last_mode = demo_mode_for_frame(0);

	for (;;) {
		enum demo_mode mode = demo_mode_for_frame(frame);
		if (mode != last_mode) {
			report_stats(last_mode, frames, sum_frame_ns, sum_draw_ns, sum_flush_ns);
			sum_draw_ns = 0;
			sum_flush_ns = 0;
			sum_frame_ns = 0;
			frames = 0;
			last_mode = mode;
		}

		uint64_t frame_t0 = k_cycle_get_64();
		uint64_t draw_ns = 0;
		uint64_t flush_ns = 0;

#if defined(CONFIG_U8G2_BUFFER_MODE_PAGE8)
		u8g2_FirstPage(&u8g2);
		do {
			uint64_t t0 = k_cycle_get_64();
			draw_scene(&u8g2, frame, mode);
			uint64_t t1 = k_cycle_get_64();
			draw_ns += cycles_to_ns(t1 - t0);

			t0 = k_cycle_get_64();
			uint8_t more = u8g2_NextPage(&u8g2);
			t1 = k_cycle_get_64();
			flush_ns += cycles_to_ns(t1 - t0);

			if (more == 0U) {
				break;
			}
		} while (1);
#else
		u8g2_ClearBuffer(&u8g2);
		uint64_t t0 = k_cycle_get_64();
		draw_scene(&u8g2, frame, mode);
		uint64_t t1 = k_cycle_get_64();
		draw_ns = cycles_to_ns(t1 - t0);

		t0 = k_cycle_get_64();
		u8g2_SendBuffer(&u8g2);
		t1 = k_cycle_get_64();
		flush_ns = cycles_to_ns(t1 - t0);
#endif

		uint64_t frame_t1 = k_cycle_get_64();
		uint64_t frame_ns = cycles_to_ns(frame_t1 - frame_t0);

		sum_draw_ns += draw_ns;
		sum_flush_ns += flush_ns;
		sum_frame_ns += frame_ns;
		frames++;
		frame++;

		if (frames >= REPORT_EVERY_FRAMES) {
			report_stats(mode, frames, sum_frame_ns, sum_draw_ns, sum_flush_ns);
			sum_draw_ns = 0;
			sum_flush_ns = 0;
			sum_frame_ns = 0;
			frames = 0;
		}
	}
}
