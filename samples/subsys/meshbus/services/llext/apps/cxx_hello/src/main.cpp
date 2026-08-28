/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <zephyr/meshbus/desktop.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/zui/zui.h>

namespace {

constexpr uint32_t screen_id = 1U;
constexpr uint16_t width = 128U;
constexpr uint16_t height = 64U;
constexpr uint16_t fps = 25U;
constexpr uint16_t box_size = 8U;

struct App {
	zui_host *host;
	zui_router *router;
	zui_screen *screen;
	zui_action_state actions;
	k_sem exit_sem;
	uint8_t framebuffer[width * height / 8U];
	uint8_t frame;
};

void set_pixel(App *app, uint16_t x, uint16_t y)
{
	if (x >= width || y >= height) {
		return;
	}

	app->framebuffer[(y / 8U) * width + x] |= static_cast<uint8_t>(BIT(y & 0x7U));
}

void fill_framebuffer(App *app, const zui_action_state *actions)
{
	uint16_t x = static_cast<uint16_t>((app->frame * 3U) % (width - box_size));
	uint16_t y = static_cast<uint16_t>(24U + ((app->frame / 2U) % 12U));

	if ((actions->down & ZUI_ACTION_LEFT) != 0U && x > 0U) {
		x--;
	}
	if ((actions->down & ZUI_ACTION_RIGHT) != 0U && x + box_size < width) {
		x++;
	}
	if ((actions->down & ZUI_ACTION_UP) != 0U && y > 0U) {
		y--;
	}
	if ((actions->down & ZUI_ACTION_DOWN) != 0U && y + box_size < height) {
		y++;
	}

	memset(app->framebuffer, 0, sizeof(app->framebuffer));
	for (uint16_t py = 0U; py < box_size; py++) {
		for (uint16_t px = 0U; px < box_size; px++) {
			set_pixel(app, static_cast<uint16_t>(x + px), static_cast<uint16_t>(y + py));
		}
	}
}

void tick(App *app)
{
	if ((app->actions.pressed & ZUI_ACTION_PRIMARY) != 0U) {
		app->frame = static_cast<uint8_t>(app->frame + 8U);
	} else {
		app->frame++;
	}
	fill_framebuffer(app, &app->actions);
}

void draw(zui_draw_ctx *draw, void *user_data)
{
	App *app = static_cast<App *>(user_data);
	const zui_framebuffer_view view = {
		.struct_size = sizeof(view),
		.data = app->framebuffer,
		.width = width,
		.height = height,
		.stride = width,
		.format = ZUI_BITMAP_FORMAT_MONO_VLSB,
	};

	zui_draw_framebuffer(draw, zui_point{0, 0}, &view);
	zui_draw_text(draw, zui_point{2, 10}, "C++ MBA");
}

bool input(const zui_input_event *event, void *user_data)
{
	App *app = static_cast<App *>(user_data);
	int rc = zui_action_state_update(&app->actions, event);

	if (rc == -ENOTSUP) {
		return false;
	}
	if (rc != 0) {
		return false;
	}

	if ((app->actions.long_pressed & ZUI_ACTION_CANCEL) != 0U) {
		k_sem_give(&app->exit_sem);
	}

	(void)zui_screen_request_redraw(app->screen);
	return true;
}

bool event(const zui_screen_event *event, void *user_data)
{
	App *app = static_cast<App *>(user_data);

	if (event->type != ZUI_SCREEN_EVENT_TICK) {
		return false;
	}

	for (uint32_t i = 0U; i < event->code; i++) {
		tick(app);
	}
	zui_action_state_clear_edges(&app->actions);
	(void)zui_screen_request_redraw(app->screen);
	return true;
}

const zui_screen_ops screen_ops = {
	.draw = draw,
	.input = input,
	.event = event,
};

} // namespace

extern "C" void cxx_hello_app_main(void *args)
{
	meshbus_desktop_app_args *app_args = static_cast<meshbus_desktop_app_args *>(args);
	App app{};
	int rc;

	if (app_args == nullptr || app_args->host == nullptr) {
		return;
	}

	printk("[cxx-hello-app] start\n");
	app.host = app_args->host;
	k_sem_init(&app.exit_sem, 0, 1);
	fill_framebuffer(&app, &app.actions);

	app.router = zui_router_create();
	app.screen = zui_screen_create(&screen_ops, &app);
	if (app.router == nullptr || app.screen == nullptr) {
		goto out;
	}
	(void)zui_screen_set_tick_period(app.screen, MAX(1000U / fps, 1U));

	rc = zui_router_register_screen(app.router, screen_id, app.screen);
	if (rc != 0 || zui_router_switch(app.router, screen_id) != 0 ||
	    zui_host_attach_router(app.host, ZUI_LAYER_FULLSCREEN, app.router) != 0) {
		goto out;
	}
	(void)zui_host_send_layer_to_front(app.host, ZUI_LAYER_FULLSCREEN);
	(void)zui_host_set_layer_enabled(app.host, ZUI_LAYER_FULLSCREEN, true);
	(void)zui_host_request_redraw(app.host);

	while (k_sem_take(&app.exit_sem, K_MSEC(250)) != 0) {
	}

out:
	if (app.host != nullptr) {
		(void)zui_host_detach_router(app.host, ZUI_LAYER_FULLSCREEN);
		(void)zui_host_request_redraw(app.host);
	}
	if (app.router != nullptr && app.screen != nullptr) {
		(void)zui_router_unregister_screen(app.router, screen_id);
	}
	if (app.screen != nullptr) {
		zui_screen_destroy(app.screen);
	}
	if (app.router != nullptr) {
		zui_router_destroy(app.router);
	}
	printk("[cxx-hello-app] exit\n");
}

LL_EXTENSION_SYMBOL(cxx_hello_app_main);
