// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zui/zui.h>

#define SAMPLE_PD_NODE DT_NODELABEL(peripheral_power)

#if DT_NODE_HAS_STATUS(SAMPLE_PD_NODE, okay) && defined(CONFIG_PM_DEVICE_RUNTIME)
#define SAMPLE_PD_BOOTSTRAP_INIT_PRIORITY 80

static int zui_draw_sample_power_bootstrap_init(void)
{
	const struct device *pd = DEVICE_DT_GET(SAMPLE_PD_NODE);
	int rc;

	if (!device_is_ready(pd)) {
		return -ENODEV;
	}

	rc = pm_device_runtime_get(pd);
	if (rc < 0 && rc != -ENOTSUP && rc != -ENOSYS) {
		return rc;
	}

	return 0;
}

SYS_INIT(zui_draw_sample_power_bootstrap_init, POST_KERNEL, SAMPLE_PD_BOOTSTRAP_INIT_PRIORITY);
#endif

static const uint8_t icon_bits[] = {
	0x3c, 0x42, 0xa5, 0x81, 0xa5, 0x99, 0x42, 0x3c,
};

static const uint8_t checker_bits[] = {
	0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa,
};

static const uint8_t *sample_icon_frames[] = {
	icon_bits,
};

static const struct zui_icon sample_icon = {
	.width = 8,
	.height = 8,
	.frame_count = 1,
	.frame_rate = 0,
	.frames = sample_icon_frames,
};

int main(void)
{
#if DT_HAS_CHOSEN(zephyr_display)
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
#else
	const struct device *display = NULL;
#endif
	struct zui_draw_ctx *draw;
	struct zui_rect frame = {
		.x = 0,
		.y = 0,
		.width = 128,
		.height = 64,
	};
	struct zui_rect clip = {
		.x = 66,
		.y = 33,
		.width = 56,
		.height = 22,
	};
	int rc;

	printk("ZUI draw sample start\n");

	if (display == NULL || !device_is_ready(display)) {
		printk("display not ready\n");
		return 0;
	}

	draw = zui_draw_ctx_create(display);
	if (draw == NULL) {
		printk("draw ctx create failed\n");
		return 0;
	}

	printk("draw ctx %ux%u\n", zui_draw_width(draw), zui_draw_height(draw));

	zui_draw_reset(draw);
	zui_draw_rect(draw, &frame);
	zui_draw_line(draw, (struct zui_point){.x = 0, .y = 0},
		      (struct zui_point){.x = 127, .y = 63});
	zui_draw_line(draw, (struct zui_point){.x = 0, .y = 63},
		      (struct zui_point){.x = 127, .y = 0});

	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	zui_draw_text(draw, (struct zui_point){.x = 4, .y = 10}, "ZUI draw");
	zui_draw_text_aligned(draw, (struct zui_point){.x = 64, .y = 22}, ZUI_ALIGN_CENTER,
			      ZUI_ALIGN_CENTER, "center");

	zui_draw_round_rect(draw, &(struct zui_rect){.x = 4, .y = 28, .width = 28, .height = 16},
			    3);
	zui_draw_round_box(draw, &(struct zui_rect){.x = 38, .y = 28, .width = 22, .height = 16},
			   3);
	zui_draw_circle(draw, (struct zui_point){.x = 19, .y = 54}, 7);
	zui_draw_disc(draw, (struct zui_point){.x = 49, .y = 54}, 6);
	zui_draw_triangle(draw, (struct zui_point){.x = 72, .y = 46},
			  (struct zui_point){.x = 64, .y = 60},
			  (struct zui_point){.x = 80, .y = 60});

	zui_draw_icon(draw, (struct zui_point){.x = 92, .y = 5}, &sample_icon);
	zui_draw_bitmap_alpha(draw, (struct zui_point){.x = 106, .y = 5}, 8, 8,
			      ZUI_BITMAP_FORMAT_XBM, checker_bits, true);

	zui_draw_set_clip(draw, &clip);
	zui_draw_box(draw, &(struct zui_rect){.x = 66, .y = 33, .width = 56, .height = 22});
	zui_draw_clear_clip(draw);
	zui_draw_set_color(draw, ZUI_COLOR_WHITE);
	zui_draw_text(draw, (struct zui_point){.x = 70, .y = 46}, "clip");
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_rect(draw, &clip);

	rc = zui_draw_present(draw);
	printk("draw present ret=%d\n", rc);
	printk("ZUI draw sample done\n");

	zui_draw_ctx_destroy(draw);
	return 0;
}
