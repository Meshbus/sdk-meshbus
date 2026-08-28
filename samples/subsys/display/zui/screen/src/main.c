// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zui/zui.h>

#define SAMPLE_PD_NODE DT_NODELABEL(peripheral_power)

#if DT_NODE_HAS_STATUS(SAMPLE_PD_NODE, okay) && defined(CONFIG_PM_DEVICE_RUNTIME)
#define SAMPLE_PD_BOOTSTRAP_INIT_PRIORITY 80

static int zui_screen_sample_power_bootstrap_init(void)
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

SYS_INIT(zui_screen_sample_power_bootstrap_init, POST_KERNEL, SAMPLE_PD_BOOTSTRAP_INIT_PRIORITY);
#endif

static int sample_fill_display(bool lit)
{
#if DT_HAS_CHOSEN(zephyr_display)
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	struct display_capabilities caps;
	struct display_buffer_descriptor desc;
	uint8_t *buf;
	size_t buf_size;
	uint8_t pixel;
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

	if (caps.current_pixel_format == PIXEL_FORMAT_MONO10) {
		pixel = lit ? 0x00U : 0xFFU;
	} else {
		pixel = lit ? 0xFFU : 0x00U;
	}

	memset(buf, pixel, buf_size);
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
	ARG_UNUSED(lit);
	return 0;
#endif
}

struct sample_screen_state {
	const char *name;
	struct zui_screen *screen;
	bool lit;
	uint32_t draws;
	uint32_t inputs;
	uint32_t events;
};

struct sample_forward_state {
	const char *name;
	struct zui_screen *target;
};

static void sample_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct sample_screen_state *state = user_data;
	int rc;

	ARG_UNUSED(draw);

	state->draws++;
	rc = sample_fill_display(state->lit);
	printk("%s draw lit=%d count=%u ret=%d\n", state->name, state->lit, state->draws, rc);
}

static bool sample_input(const struct zui_input_event *event, void *user_data)
{
	struct sample_screen_state *state = user_data;
	int rc;

	state->inputs++;
	state->lit = !state->lit;
	(void)zui_screen_request_redraw(state->screen);
	rc = zui_screen_draw(state->screen, (struct zui_draw_ctx *)state);

	printk("%s input code=%u action=%u count=%u redraw=%d draw_ret=%d\n", state->name,
	       (uint32_t)event->code, (uint32_t)event->action, state->inputs,
	       zui_screen_redraw_is_requested(state->screen), rc);
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

static const struct zui_screen_ops sample_ops = {
	.draw = sample_draw,
	.input = sample_input,
	.enter = sample_enter,
	.exit = sample_exit,
	.event = sample_event,
};

static void sample_forward_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct sample_forward_state *state = user_data;
	int rc = zui_screen_draw(state->target, draw);

	printk("%s forward draw ret=%d\n", state->name, rc);
}

static bool sample_forward_input(const struct zui_input_event *event, void *user_data)
{
	struct sample_forward_state *state = user_data;
	int rc = zui_screen_submit_input(state->target, event);

	printk("%s forward input ret=%d\n", state->name, rc);
	return rc > 0;
}

static bool sample_forward_event(const struct zui_screen_event *event, void *user_data)
{
	struct sample_forward_state *state = user_data;
	int rc = zui_screen_dispatch_event(state->target, event);

	printk("%s forward event ret=%d\n", state->name, rc);
	return rc > 0;
}

static const struct zui_screen_ops sample_forward_ops = {
	.draw = sample_forward_draw,
	.input = sample_forward_input,
	.event = sample_forward_event,
};

int main(void)
{
	struct zui_host *host;
	struct zui_router *router;
	struct zui_screen *forward;
	struct sample_screen_state home_state = {
		.name = "home",
		.lit = false,
	};
	struct sample_screen_state detail_state = {
		.name = "detail",
		.lit = true,
	};
	struct zui_input_event input = {
		.code = ZUI_INPUT_CODE_SELECT,
		.action = ZUI_INPUT_ACTION_CLICK,
	};
	struct zui_screen_event event = {
		.type = ZUI_SCREEN_EVENT_CUSTOM,
		.code = 200,
	};
	struct sample_forward_state forward_state = {
		.name = "container",
	};

	printk("ZUI screen sample start\n");
	printk("display clear ret=%d\n", sample_fill_display(false));

	host = zui_host_create(NULL);
	router = zui_router_create();
	home_state.screen = zui_screen_create(&sample_ops, &home_state);
	detail_state.screen = zui_screen_create(&sample_ops, &detail_state);
	forward_state.target = detail_state.screen;
	forward = zui_screen_create(&sample_forward_ops, &forward_state);

	if (host == NULL || router == NULL || home_state.screen == NULL ||
	    detail_state.screen == NULL || forward == NULL) {
		printk("allocation failed\n");
		return 0;
	}

	(void)zui_router_register_screen(router, 1, home_state.screen);
	(void)zui_router_register_screen(router, 2, detail_state.screen);
	(void)zui_router_register_screen(router, 3, forward);
	(void)zui_host_attach_router(host, ZUI_LAYER_DESKTOP, router);

	(void)zui_router_switch(router, 1);
	(void)zui_screen_draw(home_state.screen, (struct zui_draw_ctx *)&home_state);
	k_sleep(K_MSEC(700));
	(void)zui_host_submit_input(host, &input);
	k_sleep(K_MSEC(700));
	(void)zui_router_dispatch_event(router, &event);

	(void)zui_router_switch(router, 2);
	(void)zui_screen_draw(detail_state.screen, (struct zui_draw_ctx *)&detail_state);
	k_sleep(K_MSEC(700));
	(void)zui_host_submit_input(host, &input);
	k_sleep(K_MSEC(700));

	(void)zui_router_switch(router, 3);
	(void)zui_screen_draw(forward, (struct zui_draw_ctx *)&forward_state);
	k_sleep(K_MSEC(700));
	(void)zui_host_submit_input(host, &input);
	(void)zui_router_dispatch_event(router, &event);
	k_sleep(K_MSEC(700));

	printk("ZUI screen sample done\n");

	zui_host_destroy(host);
	zui_router_destroy(router);
	zui_screen_destroy(forward);
	zui_screen_destroy(home_state.screen);
	zui_screen_destroy(detail_state.screen);

	return 0;
}
