// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/init.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zui/zui.h>

#define SAMPLE_PD_NODE DT_NODELABEL(peripheral_power)

#if DT_NODE_HAS_STATUS(SAMPLE_PD_NODE, okay) && defined(CONFIG_PM_DEVICE_RUNTIME)
#define SAMPLE_PD_BOOTSTRAP_INIT_PRIORITY 80

static int zui_host_sample_power_bootstrap_init(void)
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

SYS_INIT(zui_host_sample_power_bootstrap_init, POST_KERNEL, SAMPLE_PD_BOOTSTRAP_INIT_PRIORITY);
#endif

static int sample_clear_display(void)
{
#if DT_HAS_CHOSEN(zephyr_display)
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	struct display_capabilities caps;
	struct display_buffer_descriptor desc;
	uint8_t *buf;
	size_t buf_size;
	uint8_t black;
	int rc;

	if (!device_is_ready(display)) {
		return -ENODEV;
	}

	display_get_capabilities(display, &caps);
	if ((caps.current_pixel_format != PIXEL_FORMAT_MONO01 &&
	     caps.current_pixel_format != PIXEL_FORMAT_MONO10) ||
	    (caps.y_resolution & 0x7U) != 0U) {
		return -ENOTSUP;
	}

	buf_size = ((size_t)caps.x_resolution * caps.y_resolution) / 8U;
	buf = k_malloc(buf_size);
	if (buf == NULL) {
		return -ENOMEM;
	}

	black = caps.current_pixel_format == PIXEL_FORMAT_MONO10 ? 0xFFU : 0x00U;
	memset(buf, black, buf_size);

	desc = (struct display_buffer_descriptor){
		.buf_size = buf_size,
		.width = caps.x_resolution,
		.height = caps.y_resolution,
		.pitch = caps.x_resolution,
	};

	rc = display_write(display, 0U, 0U, &desc, buf);
	if (rc == 0) {
		rc = display_blanking_off(display);
	}

	k_free(buf);
	return rc;
#else
	return 0;
#endif
}

static const struct device *sample_display(void)
{
#if DT_HAS_CHOSEN(zephyr_display)
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	return device_is_ready(display) ? display : NULL;
#else
	return NULL;
#endif
}

static void sample_run_toast_animation(struct zui_host *host, uint32_t toast_id)
{
	const struct device *display = sample_display();
	struct zui_draw_ctx *draw;
	int rc = 0;

	if (display == NULL) {
		printk("toast animation skipped: no display\n");
		return;
	}

	draw = zui_draw_ctx_create(display);
	if (draw == NULL) {
		printk("toast animation skipped: draw ctx alloc failed\n");
		return;
	}

	for (uint32_t frame = 0U; frame < 60U; frame++) {
		zui_draw_clear(draw);
		(void)zui_host_draw(host, draw);
		rc = zui_draw_present(draw);
		if (rc != 0) {
			break;
		}
		k_sleep(K_MSEC(25));
	}

	printk("toast animation ret=%d visible=%d\n", rc, zui_toast_is_visible(host, toast_id));
	zui_draw_ctx_destroy(draw);
}

struct sample_screen_state {
	const char *name;
	uint32_t inputs;
	uint32_t events;
};

static void sample_enter(void *user_data)
{
	struct sample_screen_state *state = user_data;

	printk("enter %s\n", state->name);
}

static void sample_exit(void *user_data)
{
	struct sample_screen_state *state = user_data;

	printk("exit %s\n", state->name);
}

static bool sample_input(const struct zui_input_event *event, void *user_data)
{
	struct sample_screen_state *state = user_data;

	state->inputs++;
	printk("%s input code=%u action=%u count=%u\n", state->name, (uint32_t)event->code,
	       (uint32_t)event->action, state->inputs);
	return true;
}

static bool sample_event(const struct zui_screen_event *event, void *user_data)
{
	struct sample_screen_state *state = user_data;

	state->events++;
	printk("%s event type=%u code=%u count=%u\n", state->name, (uint32_t)event->type,
	       event->code, state->events);
	return true;
}

static const struct zui_screen_ops sample_ops = {
	.input = sample_input,
	.enter = sample_enter,
	.exit = sample_exit,
	.event = sample_event,
};

int main(void)
{
	struct zui_host *host;
	struct zui_router *router;
	struct zui_screen *home;
	struct zui_screen *app;
	struct sample_screen_state home_state = {.name = "home"};
	struct sample_screen_state app_state = {.name = "app"};
	struct zui_input_event input = {
		.code = ZUI_INPUT_CODE_SELECT,
		.action = ZUI_INPUT_ACTION_CLICK,
	};
	struct zui_screen_event event = {
		.type = ZUI_SCREEN_EVENT_CUSTOM,
		.code = 100,
	};
	struct zui_toast_config toast = {
		.title = "Host",
		.text = "router ready",
		.timeout_ms = 700,
	};
	uint32_t toast_id;

	printk("ZUI host/router sample start\n");
	printk("display clear ret=%d\n", sample_clear_display());

	host = zui_host_create(NULL);
	router = zui_router_create();
	home = zui_screen_create(&sample_ops, &home_state);
	app = zui_screen_create(&sample_ops, &app_state);

	if (host == NULL || router == NULL || home == NULL || app == NULL) {
		printk("allocation failed\n");
		return 0;
	}

	(void)zui_router_register_screen(router, 1, home);
	(void)zui_router_register_screen(router, 2, app);
	(void)zui_host_attach_router(host, ZUI_LAYER_DESKTOP, router);

	(void)zui_router_switch(router, 1);
	(void)zui_host_submit_input(host, &input);
	(void)zui_router_dispatch_event(router, &event);

	(void)zui_router_switch(router, 2);
	(void)zui_host_submit_input(host, &input);
	(void)zui_host_set_input_locked(host, true);
	printk("input while locked ret=%d\n", zui_host_submit_input(host, &input));
	(void)zui_host_set_input_locked(host, false);

	toast_id = zui_toast_show(host, &toast);
	printk("toast id=%u visible=%d\n", toast_id, zui_toast_is_visible(host, toast_id));
	sample_run_toast_animation(host, toast_id);

	printk("ZUI host/router sample done\n");

	zui_host_destroy(host);
	zui_router_destroy(router);
	zui_screen_destroy(home);
	zui_screen_destroy(app);

	return 0;
}
