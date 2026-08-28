// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

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

static int zui_assets_sample_power_bootstrap_init(void)
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

SYS_INIT(zui_assets_sample_power_bootstrap_init, POST_KERNEL, SAMPLE_PD_BOOTSTRAP_INIT_PRIORITY);
#endif

static void draw_asset(struct zui_draw_ctx *draw, const struct zui_asset_pack *pack,
		       enum zui_asset_icon_id id, struct zui_point pos)
{
	const struct zui_icon *icon = zui_asset_pack_icon_by_id(pack, id);
	const char *name = zui_asset_pack_icon_name(pack, id);

	printk("asset id=%u name=%s icon=%p %ux%u frames=%u fps=%u\n", id,
	       name == NULL ? "-" : name, icon, zui_icon_width(icon), zui_icon_height(icon),
	       zui_icon_frame_count(icon), zui_icon_frame_rate(icon));
	zui_draw_icon(draw, pos, icon);
}

int main(void)
{
#if DT_HAS_CHOSEN(zephyr_display)
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
#else
	const struct device *display = NULL;
#endif
	const struct zui_asset_pack *pack = zui_asset_pack_default();
	const struct zui_icon *loading;
	struct zui_draw_ctx *draw;
	int rc;

	printk("ZUI assets sample start\n");
	printk("default asset count=%zu\n", zui_asset_pack_icon_count(pack));

	if (display == NULL || !device_is_ready(display)) {
		printk("display not ready\n");
		return 0;
	}

	draw = zui_draw_ctx_create(display);
	if (draw == NULL) {
		printk("draw ctx create failed\n");
		return 0;
	}

	zui_draw_reset(draw);
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	zui_draw_text(draw, (struct zui_point){.x = 2, .y = 8}, "ZUI assets");
	draw_asset(draw, pack, ZUI_ASSET_ICON_NAV_BACK, (struct zui_point){.x = 2, .y = 13});
	draw_asset(draw, pack, ZUI_ASSET_ICON_FILE_DIR, (struct zui_point){.x = 18, .y = 13});
	draw_asset(draw, pack, ZUI_ASSET_ICON_FILE_DOCUMENT, (struct zui_point){.x = 34, .y = 13});
	draw_asset(draw, pack, ZUI_ASSET_ICON_STATUS_UNKNOWN, (struct zui_point){.x = 92, .y = 2});

	draw_asset(draw, pack, ZUI_ASSET_ICON_KEY_SAVE, (struct zui_point){.x = 2, .y = 30});
	draw_asset(draw, pack, ZUI_ASSET_ICON_FILE_DOCUMENT, (struct zui_point){.x = 22, .y = 30});
	draw_asset(draw, pack, ZUI_ASSET_ICON_HASHMARK, (struct zui_point){.x = 42, .y = 30});
	draw_asset(draw, pack, ZUI_ASSET_ICON_STATUS_UNKNOWN, (struct zui_point){.x = 62, .y = 30});
	draw_asset(draw, pack, ZUI_ASSET_ICON_BUTTON_SELECT, (struct zui_point){.x = 82, .y = 30});

	loading = zui_asset_pack_icon_by_id(pack, ZUI_ASSET_ICON_ACTIVITY_LOADING_10);
	for (uint32_t frame = 0U; frame < zui_icon_frame_count(loading); frame++) {
		zui_draw_icon_frame(draw, (struct zui_point){.x = 2 + frame * 12, .y = 53}, loading,
				    frame);
	}

	rc = zui_draw_present(draw);
	printk("assets present ret=%d\n", rc);
	printk("ZUI assets sample done\n");

	zui_draw_ctx_destroy(draw);
	return 0;
}
