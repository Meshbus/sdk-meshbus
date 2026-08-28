// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stdatomic.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zui/zui.h>

#define SAMPLE_PD_NODE      DT_NODELABEL(peripheral_power)
#define SAMPLE_BUTTONS_NODE DT_NODELABEL(buttons)
#define SAMPLE_ENCODER_NODE DT_NODELABEL(encoder)

#if DT_NODE_HAS_STATUS(SAMPLE_PD_NODE, okay) && defined(CONFIG_PM_DEVICE_RUNTIME)
#define SAMPLE_PD_BOOTSTRAP_INIT_PRIORITY 80

static int zui_toast_sample_power_bootstrap_init(void)
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

SYS_INIT(zui_toast_sample_power_bootstrap_init, POST_KERNEL, SAMPLE_PD_BOOTSTRAP_INIT_PRIORITY);
#endif

struct sample_state {
	struct k_work toggle_work;
	struct zui_host *host;
	struct zui_draw_ctx *draw;
	const struct zui_icon *icon;
	atomic_uint pending_toggles;
	uint32_t active_toast;
	uint32_t toggles;
};

static struct sample_state sample;

static void sample_resume_input_device(const struct device *dev, const char *name)
{
	int rc;

	if (!device_is_ready(dev)) {
		printk("%s input device is not ready\n", name);
		return;
	}

	rc = pm_device_runtime_get(dev);
	printk("%s input runtime get ret=%d\n", name, rc);
}

static void sample_resume_input_devices(void)
{
#if DT_NODE_HAS_STATUS(SAMPLE_BUTTONS_NODE, okay)
	sample_resume_input_device(DEVICE_DT_GET(SAMPLE_BUTTONS_NODE), "buttons");
#else
	printk("buttons input device is not enabled\n");
#endif

#if DT_NODE_HAS_STATUS(SAMPLE_ENCODER_NODE, okay)
	sample_resume_input_device(DEVICE_DT_GET(SAMPLE_ENCODER_NODE), "encoder");
#else
	printk("encoder input device is not enabled\n");
#endif
}

static void sample_draw_frame(void)
{
	char line[40];

	zui_draw_clear(sample.draw);
	zui_draw_set_font(sample.draw, ZUI_FONT_SECONDARY);
	zui_draw_text(sample.draw, (struct zui_point){.x = 2, .y = 10}, "ZUI toast");
	zui_draw_text(sample.draw, (struct zui_point){.x = 2, .y = 24}, "OK: show/dismiss");
	snprintk(line, sizeof(line), "id=%u n=%u", sample.active_toast, sample.toggles);
	zui_draw_text(sample.draw, (struct zui_point){.x = 2, .y = 38}, line);
	(void)zui_host_draw(sample.host, sample.draw);
	(void)zui_draw_present(sample.draw);
}

static void sample_toggle_work_handler(struct k_work *work)
{
	struct zui_toast_config toast = {
		.title = "Toast",
		.text = "OK dismisses",
		.icon = sample.icon,
		.timeout_ms = 3000,
	};
	unsigned int count;

	ARG_UNUSED(work);

	count = atomic_exchange(&sample.pending_toggles, 0U);
	if (count == 0U || sample.host == NULL) {
		return;
	}

	sample.toggles += count;
	if (sample.active_toast != 0U && zui_toast_is_visible(sample.host, sample.active_toast)) {
		printk("toast dismiss id=%u\n", sample.active_toast);
		(void)zui_toast_dismiss(sample.host, sample.active_toast);
		return;
	}

	sample.active_toast = zui_toast_show(sample.host, &toast);
	printk("toast show id=%u\n", sample.active_toast);
}

static void sample_input_cb(struct input_event *evt, void *user_data)
{
	struct zui_input_event zui_event = {0};
	int rc;

	ARG_UNUSED(user_data);

	rc = zui_input_from_zephyr(evt, &zui_event);
	if (rc != 0) {
		return;
	}

	if (zui_event.code == ZUI_INPUT_CODE_SELECT &&
	    (zui_event.action == ZUI_INPUT_ACTION_PRESS ||
	     zui_event.action == ZUI_INPUT_ACTION_CLICK)) {
		(void)atomic_fetch_add(&sample.pending_toggles, 1U);
		(void)k_work_submit(&sample.toggle_work);
	}
}

INPUT_CALLBACK_DEFINE(NULL, sample_input_cb, NULL);

int main(void)
{
#if DT_HAS_CHOSEN(zephyr_display)
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
#else
	const struct device *display = NULL;
#endif
	const struct zui_asset_pack *pack;

	printk("ZUI toast sample start\n");

	if (display == NULL || !device_is_ready(display)) {
		printk("display not ready\n");
		return 0;
	}

	k_work_init(&sample.toggle_work, sample_toggle_work_handler);
	sample_resume_input_devices();

	sample.host = zui_host_create(NULL);
	sample.draw = zui_draw_ctx_create(display);
	if (sample.host == NULL || sample.draw == NULL) {
		printk("allocation failed\n");
		return 0;
	}

	pack = zui_asset_pack_default();
	sample.icon = zui_asset_pack_icon_by_id(pack, ZUI_ASSET_ICON_ACTIVITY_LOADING_10);

	printk("Press OK to show toast, press OK again to dismiss\n");
	while (true) {
		if (sample.active_toast != 0U &&
		    !zui_toast_is_visible(sample.host, sample.active_toast)) {
			sample.active_toast = 0U;
		}
		sample_draw_frame();
		k_sleep(K_MSEC(25));
	}

	return 0;
}
