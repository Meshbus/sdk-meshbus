// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stdbool.h>

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

static int zui_input_sample_power_bootstrap_init(void)
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

SYS_INIT(zui_input_sample_power_bootstrap_init, POST_KERNEL, SAMPLE_PD_BOOTSTRAP_INIT_PRIORITY);
#endif

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

struct sample_state {
	struct k_mutex lock;
	struct k_work work;
	struct zui_host *host;
	struct zui_draw_ctx *draw;
	struct zui_input_event zui_event;
	uint8_t raw_type;
	uint16_t raw_code;
	int32_t raw_value;
	int convert_rc;
	uint32_t count;
	bool ready;
};

static struct sample_state sample;

static void sample_draw_state(void)
{
	char line[40];

	if (sample.draw == NULL) {
		return;
	}

	zui_draw_reset(sample.draw);
	zui_draw_set_font(sample.draw, ZUI_FONT_SECONDARY);
	zui_draw_text(sample.draw, (struct zui_point){.x = 2, .y = 8}, "ZUI input");

	snprintk(line, sizeof(line), "raw t=%u c=%u v=%d", sample.raw_type, sample.raw_code,
		 sample.raw_value);
	zui_draw_text(sample.draw, (struct zui_point){.x = 2, .y = 22}, line);

	if (sample.convert_rc == 0) {
		snprintk(line, sizeof(line), "%s / %s", zui_input_code_name(sample.zui_event.code),
			 zui_input_action_name(sample.zui_event.action));
		zui_draw_text(sample.draw, (struct zui_point){.x = 2, .y = 36}, line);
		snprintk(line, sizeof(line), "seq=%u n=%u", sample.zui_event.sequence,
			 sample.count);
	} else {
		snprintk(line, sizeof(line), "convert ret=%d", sample.convert_rc);
	}
	zui_draw_text(sample.draw, (struct zui_point){.x = 2, .y = 50}, line);

	(void)zui_draw_present(sample.draw);
}

static bool sample_screen_input(const struct zui_input_event *event, void *user_data)
{
	ARG_UNUSED(user_data);

	printk("screen input seq=%u code=%s action=%s value=%d\n", event->sequence,
	       zui_input_code_name(event->code), zui_input_action_name(event->action),
	       event->value);
	return true;
}

static const struct zui_screen_ops sample_screen_ops = {
	.input = sample_screen_input,
};

static void sample_work_handler(struct k_work *work)
{
	struct zui_input_event event;
	uint8_t raw_type;
	uint16_t raw_code;
	int32_t raw_value;
	int rc;

	ARG_UNUSED(work);

	k_mutex_lock(&sample.lock, K_FOREVER);
	event = sample.zui_event;
	raw_type = sample.raw_type;
	raw_code = sample.raw_code;
	raw_value = sample.raw_value;
	rc = sample.convert_rc;
	k_mutex_unlock(&sample.lock);

	if (rc == 0 && sample.host != NULL) {
		printk("input raw t=%u code=%u value=%d -> seq=%u code=%s action=%s\n", raw_type,
		       raw_code, raw_value, event.sequence, zui_input_code_name(event.code),
		       zui_input_action_name(event.action));
		(void)zui_host_submit_input(sample.host, &event);
	} else {
		printk("input raw t=%u code=%u value=%d -> ret=%d\n", raw_type, raw_code, raw_value,
		       rc);
	}

	k_mutex_lock(&sample.lock, K_FOREVER);
	sample_draw_state();
	k_mutex_unlock(&sample.lock);
}

static void sample_input_cb(struct input_event *evt, void *user_data)
{
	struct zui_input_event zui_event = {0};
	int rc;

	ARG_UNUSED(user_data);

	if (!sample.ready) {
		return;
	}

	rc = zui_input_from_zephyr(evt, &zui_event);

	k_mutex_lock(&sample.lock, K_FOREVER);
	sample.raw_type = evt->type;
	sample.raw_code = evt->code;
	sample.raw_value = evt->value;
	sample.convert_rc = rc;
	if (rc == 0) {
		sample.zui_event = zui_event;
		sample.count++;
	}
	k_mutex_unlock(&sample.lock);

	(void)k_work_submit(&sample.work);
}

INPUT_CALLBACK_DEFINE(NULL, sample_input_cb, NULL);

int main(void)
{
#if DT_HAS_CHOSEN(zephyr_display)
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
#else
	const struct device *display = NULL;
#endif
	struct zui_router *router;
	struct zui_screen *screen;

	printk("ZUI input sample start\n");

	k_mutex_init(&sample.lock);
	k_work_init(&sample.work, sample_work_handler);
	sample_resume_input_devices();

	sample.host = zui_host_create(NULL);
	router = zui_router_create();
	screen = zui_screen_create(&sample_screen_ops, NULL);
	if (sample.host == NULL || router == NULL || screen == NULL) {
		printk("allocation failed\n");
		return 0;
	}

	(void)zui_router_register_screen(router, 1, screen);
	(void)zui_router_switch(router, 1);
	(void)zui_host_attach_router(sample.host, ZUI_LAYER_DESKTOP, router);

	if (display == NULL || !device_is_ready(display)) {
		printk("display not ready, logging input only\n");
	} else {
		sample.draw = zui_draw_ctx_create(display);
	}

	k_mutex_lock(&sample.lock, K_FOREVER);
	sample.ready = true;
	sample_draw_state();
	k_mutex_unlock(&sample.lock);

	printk("Press physical keys to inspect ZUI input mapping\n");
	while (true) {
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
