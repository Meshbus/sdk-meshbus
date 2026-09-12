// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>
#include <zui/zui.h>

static const uint8_t bitmap_8x8[] = {
	0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81,
};

static const uint8_t *icon_frames[] = {
	bitmap_8x8,
};

static const struct zui_icon icon_8x8 = {
	.width = 8,
	.height = 8,
	.frame_count = 1,
	.frame_rate = 0,
	.frames = icon_frames,
};

ZTEST(zui_draw, test_context_lifecycle_and_dimensions)
{
	struct zui_draw_ctx *ctx = zui_draw_ctx_create(NULL);

	zassert_not_null(ctx);
	zassert_equal(zui_draw_width(ctx), CONFIG_ZUI_DISPLAY_WIDTH);
	zassert_equal(zui_draw_height(ctx), CONFIG_ZUI_DISPLAY_HEIGHT);
	zassert_equal(zui_draw_width(NULL), 0);
	zassert_equal(zui_draw_height(NULL), 0);
	zassert_equal(zui_draw_present(NULL), -EINVAL);
	zassert_equal(zui_draw_present(ctx), -ENODEV);

	zui_draw_reset(ctx);
	zui_draw_clear(ctx);
	zui_draw_ctx_destroy(ctx);
	zui_draw_ctx_destroy(NULL);
}

ZTEST(zui_draw, test_font_metrics_and_text_queries)
{
	struct zui_draw_ctx *ctx = zui_draw_ctx_create(NULL);
	struct zui_font_metrics metrics;

	zassert_not_null(ctx);
	zassert_equal(zui_draw_font_metrics(NULL, ZUI_FONT_SECONDARY, &metrics), 0);
	zassert_equal(metrics.height, 7);
	zassert_equal(metrics.leading_default, 11);
	zassert_equal(zui_draw_font_metrics(ctx, ZUI_FONT_COUNT, &metrics), -EINVAL);
	zassert_equal(zui_draw_font_metrics(ctx, ZUI_FONT_SECONDARY, NULL), -EINVAL);

	zassert_equal(zui_draw_font_height(ctx), 7);
	zui_draw_set_font(ctx, ZUI_FONT_BIG_NUMBERS);
	zassert_equal(zui_draw_font_height(ctx), 15);
	zui_draw_set_font(ctx, ZUI_FONT_COUNT);
	zassert_equal(zui_draw_font_height(ctx), 15);
	zassert_true(zui_draw_text_width(ctx, "abc") > 0);
	zassert_equal(zui_draw_text_width(ctx, NULL), 0);
	zassert_true(zui_draw_glyph_width(ctx, 'A') > 0);

	zui_draw_ctx_destroy(ctx);
}

ZTEST(zui_draw, test_void_draw_calls_tolerate_invalid_arguments)
{
	struct zui_draw_ctx *ctx = zui_draw_ctx_create(NULL);
	struct zui_rect rect = {
		.x = 1,
		.y = 2,
		.width = 10,
		.height = 8,
	};

	zassert_not_null(ctx);

	zui_draw_set_color(ctx, ZUI_COLOR_BLACK);
	zui_draw_set_color(ctx, (enum zui_color)99);
	zui_draw_invert_color(ctx);
	zui_draw_set_direction(ctx, ZUI_DRAW_DIRECTION_TOP_TO_BOTTOM);
	zui_draw_set_direction(ctx, (enum zui_draw_direction)99);
	zui_draw_set_clip(ctx, &rect);
	zui_draw_set_clip(ctx, NULL);
	zui_draw_clear_clip(ctx);

	zui_draw_text(ctx, (struct zui_point){.x = 0, .y = 8}, "hello");
	zui_draw_text(ctx, (struct zui_point){0}, NULL);
	zui_draw_text_aligned(ctx, (struct zui_point){.x = 64, .y = 16}, ZUI_ALIGN_CENTER,
			      ZUI_ALIGN_TOP, "aligned");
	zui_draw_glyph(ctx, (struct zui_point){.x = 0, .y = 8}, 'A');
	zui_draw_dot(ctx, (struct zui_point){.x = 1, .y = 1});
	zui_draw_line(ctx, (struct zui_point){.x = 0, .y = 0},
		      (struct zui_point){.x = 10, .y = 10});
	zui_draw_rect(ctx, &rect);
	zui_draw_rect(ctx, NULL);
	zui_draw_box(ctx, &rect);
	zui_draw_circle(ctx, (struct zui_point){.x = 10, .y = 10}, 3);
	zui_draw_disc(ctx, (struct zui_point){.x = 14, .y = 10}, 3);
	zui_draw_round_rect(ctx, &rect, 2);
	zui_draw_round_rect_stroked(ctx, &rect, 2, 2);
	zui_draw_round_box(ctx, &rect, 2);
	zui_draw_triangle(ctx, (struct zui_point){.x = 0, .y = 0},
			  (struct zui_point){.x = 4, .y = 8}, (struct zui_point){.x = 8, .y = 0});

	zui_draw_clear(NULL);
	zui_draw_reset(NULL);
	zui_draw_set_color(NULL, ZUI_COLOR_BLACK);
	zui_draw_set_font(NULL, ZUI_FONT_SECONDARY);
	zui_draw_set_clip(NULL, &rect);
	zui_draw_clear_clip(NULL);
	zui_draw_ctx_destroy(ctx);
}

ZTEST(zui_draw, test_bitmap_icon_and_element_helpers_validate_arguments)
{
	struct zui_draw_ctx *ctx = zui_draw_ctx_create(NULL);
	struct zui_rect rect = {
		.x = 0,
		.y = 0,
		.width = 40,
		.height = 12,
	};
	struct zui_draw_button_hint hints = {
		.left = "Back",
		.center = "OK",
		.right = "Next",
	};
	char text[16] = "LongTextValue";
	char utf8_text[8] = "A\xe4\xb8\xad";
	char full_utf8[4] = {'A', (char)0xe4, (char)0xb8, (char)0xad};

	zassert_not_null(ctx);

	zui_draw_bitmap(ctx, (struct zui_point){.x = 0, .y = 0}, 8, 8, ZUI_BITMAP_FORMAT_XBM,
			bitmap_8x8);
	zui_draw_bitmap_alpha(ctx, (struct zui_point){.x = 10, .y = 0}, 8, 8,
			      ZUI_BITMAP_FORMAT_MONO, bitmap_8x8, true);
	zui_draw_bitmap_transformed(ctx, (struct zui_point){.x = 20, .y = 0}, 8, 8,
				    ZUI_BITMAP_FORMAT_XBM, bitmap_8x8, ZUI_ICON_TRANSFORM_ROTATE_90,
				    true);
	zui_draw_bitmap(ctx, (struct zui_point){0}, 0, 8, ZUI_BITMAP_FORMAT_XBM, bitmap_8x8);
	zui_draw_bitmap(ctx, (struct zui_point){0}, 8, 8, ZUI_BITMAP_FORMAT_XBM, NULL);
	zui_draw_framebuffer(ctx, (struct zui_point){0}, NULL);
	zui_draw_icon(ctx, (struct zui_point){.x = 0, .y = 16}, &icon_8x8);
	zui_draw_icon_frame(ctx, (struct zui_point){.x = 10, .y = 16}, &icon_8x8, 3);
	zui_draw_icon_transformed(ctx, (struct zui_point){.x = 20, .y = 16}, &icon_8x8,
				  ZUI_ICON_TRANSFORM_FLIP_X);
	zui_draw_icon(ctx, (struct zui_point){0}, NULL);
	zui_draw_icon_anim(ctx, (struct zui_point){0}, NULL);

	zui_draw_progress_bar(ctx, &rect, 50);
	zui_draw_progress_bar_text(ctx, &rect, 75, "75%");
	zui_draw_scrollbar(ctx, &rect, 1, 4);
	zui_draw_button_hints(ctx, &hints);
	zui_draw_multiline_text(ctx, &rect, ZUI_ALIGN_LEFT, ZUI_ALIGN_TOP, "a\nb");
	zui_draw_text_line_scrolled(ctx, (struct zui_point){.x = 0, .y = 8}, 20, "scroll text", 2,
				    true);
	zui_draw_text_box(ctx, &rect, "boxed", 0, ZUI_DRAW_TEXT_BOX_FRAME);
	zui_draw_bubble_frame(ctx, &rect);
	zui_draw_bubble(ctx, &rect, (struct zui_point){.x = 4, .y = 16}, "bubble");

	zassert_equal(zui_draw_text_fit_width(NULL, text, sizeof(text), 20), -EINVAL);
	zassert_equal(zui_draw_text_fit_width(ctx, NULL, sizeof(text), 20), -EINVAL);
	zassert_true(zui_draw_text_fit_width(ctx, text, sizeof(text), 18) >= 0);
	zassert_true(strlen(text) < strlen("LongTextValue"));

	zassert_true(zui_draw_text_fit_width(ctx, utf8_text, sizeof(utf8_text), 13) >= 0);
	zassert_mem_equal(utf8_text, "A", 2);

	zassert_true(zui_draw_text_fit_width(ctx, full_utf8, sizeof(full_utf8), 128) >= 0);
	zassert_mem_equal(full_utf8, "A", 2);

	zui_draw_ctx_destroy(ctx);
}

ZTEST(zui_draw, test_bitmap_payload_size_and_bit_get)
{
	static const uint8_t row_major_10x2[] = {
		BIT(0),
		BIT(1),
		BIT(3),
		0,
	};
	static const uint8_t vlsb_10x10[] = {
		BIT(0), 0, 0, 0, 0, 0, 0, 0, 0, BIT(7),
		0, 0, 0, BIT(1), 0, 0, 0, 0, 0, 0,
	};
	struct zui_draw_ctx *ctx = zui_draw_ctx_create(NULL);
	struct zui_framebuffer_view view = {
		.struct_size = sizeof(view),
		.data = vlsb_10x10,
		.width = 10,
		.height = 10,
		.stride = 10,
		.format = ZUI_BITMAP_FORMAT_MONO_VLSB,
	};

	zassert_not_null(ctx);

	zassert_equal(zui_bitmap_payload_size(ZUI_BITMAP_FORMAT_MONO, 10, 2), 4);
	zassert_equal(zui_bitmap_payload_size(ZUI_BITMAP_FORMAT_XBM, 10, 2), 4);
	zassert_equal(zui_bitmap_payload_size(ZUI_BITMAP_FORMAT_MONO_VLSB, 10, 10), 20);
	zassert_equal(zui_bitmap_payload_size(ZUI_BITMAP_FORMAT_MONO_VLSB, 128, 64), 1024);
	zassert_equal(zui_bitmap_payload_size((enum zui_bitmap_format)99, 10, 10), 0);
	zassert_equal(zui_bitmap_payload_size(ZUI_BITMAP_FORMAT_MONO, 0, 10), 0);

	zassert_true(zui_bitmap_bit_get(ZUI_BITMAP_FORMAT_MONO, row_major_10x2, 10, 2, 0, 0));
	zassert_true(zui_bitmap_bit_get(ZUI_BITMAP_FORMAT_MONO, row_major_10x2, 10, 2, 9, 0));
	zassert_true(zui_bitmap_bit_get(ZUI_BITMAP_FORMAT_XBM, row_major_10x2, 10, 2, 3, 1));
	zassert_false(zui_bitmap_bit_get(ZUI_BITMAP_FORMAT_MONO, row_major_10x2, 10, 2, 4, 1));

	zassert_true(zui_bitmap_bit_get(ZUI_BITMAP_FORMAT_MONO_VLSB, vlsb_10x10, 10, 10, 0, 0));
	zassert_true(zui_bitmap_bit_get(ZUI_BITMAP_FORMAT_MONO_VLSB, vlsb_10x10, 10, 10, 9, 7));
	zassert_true(zui_bitmap_bit_get(ZUI_BITMAP_FORMAT_MONO_VLSB, vlsb_10x10, 10, 10, 3, 9));
	zassert_false(zui_bitmap_bit_get(ZUI_BITMAP_FORMAT_MONO_VLSB, vlsb_10x10, 10, 10, 3, 8));

	zassert_false(zui_bitmap_bit_get(ZUI_BITMAP_FORMAT_MONO, NULL, 10, 2, 0, 0));
	zassert_false(zui_bitmap_bit_get(ZUI_BITMAP_FORMAT_MONO, row_major_10x2, 10, 2, 10, 0));
	zassert_false(zui_bitmap_bit_get(ZUI_BITMAP_FORMAT_MONO, row_major_10x2, 10, 2, 0, 2));
	zassert_false(zui_bitmap_bit_get((enum zui_bitmap_format)99, row_major_10x2, 10, 2, 0,
					 0));

	zui_draw_framebuffer(ctx, (struct zui_point){0}, &view);
	view.stride = 9;
	zui_draw_framebuffer(ctx, (struct zui_point){0}, &view);
	view.stride = 10;
	view.struct_size = 0;
	zui_draw_framebuffer(ctx, (struct zui_point){0}, &view);
	view.struct_size = sizeof(view);
	view.data = NULL;
	zui_draw_framebuffer(ctx, (struct zui_point){0}, &view);

	zui_draw_ctx_destroy(ctx);
}

ZTEST_SUITE(zui_draw, NULL, NULL, NULL, NULL, NULL);
