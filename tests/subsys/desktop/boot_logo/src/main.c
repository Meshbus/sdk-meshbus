/* Copyright (c) 2026 FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "desktop_private.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/ztest.h>
#include <zui/zui.h>

#define WIDTH 128
#define HEIGHT 64

static uint8_t framebuffer[WIDTH * HEIGHT / 8];
static uint8_t first_frame[sizeof(framebuffer)];
static int64_t first_frame_ms;
static int64_t second_frame_ms;
static uint32_t frame_count;
static bool display_blanked = true;
static K_SEM_DEFINE(first_frame_ready, 0, 1);
static K_SEM_DEFINE(second_frame_ready, 0, 1);

static int test_display_blanking_on(const struct device *dev)
{
	ARG_UNUSED(dev);
	display_blanked = true;
	return 0;
}

static int test_display_blanking_off(const struct device *dev)
{
	ARG_UNUSED(dev);
	display_blanked = false;
	return 0;
}

static int test_display_write(const struct device *dev, uint16_t x, uint16_t y,
			      const struct display_buffer_descriptor *desc, const void *buf)
{
	ARG_UNUSED(dev);
	zassert_equal(x, 0);
	zassert_equal(desc->width, WIDTH);
	zassert_equal(desc->height, 8);
	zassert_true(y < HEIGHT && y % 8 == 0);
	zassert_equal(desc->buf_size, WIDTH);
	memcpy(&framebuffer[(y / 8) * WIDTH], buf, WIDTH);
	/* U8G2 clears panel RAM while blanked during initialization. */
	if (y == HEIGHT - 8 && !display_blanked) {
		frame_count++;
		if (frame_count == 1) {
			memcpy(first_frame, framebuffer, sizeof(first_frame));
			first_frame_ms = k_uptime_get();
			k_sem_give(&first_frame_ready);
		} else if (frame_count == 2) {
			second_frame_ms = k_uptime_get();
			k_sem_give(&second_frame_ready);
		}
	}
	return 0;
}

static void test_display_capabilities(const struct device *dev,
				      struct display_capabilities *caps)
{
	ARG_UNUSED(dev);
	*caps = (struct display_capabilities){
		.x_resolution = WIDTH,
		.y_resolution = HEIGHT,
		.supported_pixel_formats = PIXEL_FORMAT_MONO01,
		.current_pixel_format = PIXEL_FORMAT_MONO01,
		.screen_info = SCREEN_INFO_MONO_VTILED,
	};
}

static int test_display_format(const struct device *dev, enum display_pixel_format format)
{
	ARG_UNUSED(dev);
	return format == PIXEL_FORMAT_MONO01 ? 0 : -ENOTSUP;
}

static DEVICE_API(display, test_display_api) = {
	.blanking_on = test_display_blanking_on,
	.blanking_off = test_display_blanking_off,
	.write = test_display_write,
	.get_capabilities = test_display_capabilities,
	.set_pixel_format = test_display_format,
};

DEVICE_DT_DEFINE(DT_NODELABEL(test_display), NULL, NULL, NULL, NULL,
		 POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY, &test_display_api);

/* Substitute screen content and app lifecycle, preserving the runtime's real
 * router registration, first draw, input queue, thread, host, and U8G2 output.
 */
static void dashboard_draw(struct zui_draw_ctx *draw, void *user_data)
{
	ARG_UNUSED(user_data);
	zui_draw_text(draw, (struct zui_point){.x = 0, .y = 7}, "DASHBOARD");
}

static bool dashboard_input(const struct zui_input_event *event, void *user_data)
{
	struct zui_desktop *desktop = user_data;

	if (event->action == ZUI_INPUT_ACTION_CLICK && event->code == ZUI_INPUT_CODE_SELECT) {
		(void)zui_desktop_switch(desktop, MBS_DESKTOP_VIEW_MAIN_MENU);
		return true;
	}
	return false;
}

int desktop_dashboard_init(struct zui_desktop *desktop)
{
	static const struct zui_screen_ops ops = {
		.draw = dashboard_draw,
		.input = dashboard_input,
	};

	desktop->home_screen = zui_screen_create(&ops, desktop);
	return zui_desktop_register_screen(desktop, MBS_DESKTOP_VIEW_DASHBOARD,
					 desktop->home_screen);
}

int desktop_main_menu_init(struct zui_desktop *desktop)
{
	desktop->main_menu_screen = zui_screen_create(NULL, desktop);
	return zui_desktop_register_screen(desktop, MBS_DESKTOP_VIEW_MAIN_MENU,
					 desktop->main_menu_screen);
}

void desktop_dashboard_deinit(struct zui_desktop *desktop)
{
	zui_screen_destroy(desktop->home_screen);
}

void desktop_main_menu_deinit(struct zui_desktop *desktop)
{
	zui_screen_destroy(desktop->main_menu_screen);
}

int desktop_power_menu_init(struct zui_desktop *desktop)
{
	ARG_UNUSED(desktop);
	return 0;
}

void desktop_power_menu_deinit(struct zui_desktop *desktop)
{
	ARG_UNUSED(desktop);
}

void desktop_power_menu_poll(struct zui_desktop *desktop)
{
	ARG_UNUSED(desktop);
}

int desktop_app_registry_detach_desktop_all(struct zui_desktop *desktop)
{
	ARG_UNUSED(desktop);
	return 0;
}

/* This fixture has no MBA application or pending lifecycle requests. */
bool desktop_mba_stop_pending(void)
{
	return false;
}

void desktop_mba_process_requests(struct zui_desktop *desktop)
{
	ARG_UNUSED(desktop);
}

int desktop_app_complete_exit(struct zui_desktop *desktop, k_timeout_t timeout)
{
	ARG_UNUSED(desktop);
	ARG_UNUSED(timeout);
	return 0;
}

uint32_t desktop_dashboard_draw_timeout_ms(struct zui_desktop *desktop)
{
	ARG_UNUSED(desktop);
	return 250;
}

void desktop_dashboard_tab_poll(struct zui_desktop *desktop)
{
	ARG_UNUSED(desktop);
}

void desktop_dashboard_poll(struct zui_desktop *desktop)
{
	ARG_UNUSED(desktop);
}

static bool pixel_on(const uint8_t *frame, uint16_t x, uint16_t y)
{
	return (frame[(y / 8) * WIDTH + x] & BIT(y % 8)) != 0;
}

ZTEST(desktop_boot_logo, test_startup_frame_and_dashboard_handoff)
{
	struct zui_desktop *desktop = zui_desktop_get_instance();
	bool header_lit = false;

	zassert_not_null(desktop);
	zassert_ok(k_sem_take(&first_frame_ready, K_SECONDS(2)), "no initial frame");

#if CONFIG_MBS_DESKTOP_BOOT_LOGO_DURATION_MS > 0
	bool any_lit = false;
	const struct zui_input_event select = {
		.code = ZUI_INPUT_CODE_SELECT,
		.action = ZUI_INPUT_ACTION_CLICK,
	};

	/* Assert the visible placement through actual U8G2 display writes. */
	for (uint16_t y = 0; y < HEIGHT; y++) {
		for (uint16_t x = 0; x < WIDTH; x++) {
			bool lit = pixel_on(first_frame, x, y);

			if (x < 22 || x >= 106 || y < 4 || y >= 60) {
				zassert_false(lit, "logo outside margins at %u,%u", x, y);
			}
			any_lit |= lit;
		}
	}
	zassert_true(any_lit, "logo is blank");
	zassert_true(pixel_on(first_frame, 23, 34), "B stem missing");
	zassert_false(pixel_on(first_frame, 33, 40), "B upper counter filled");
	zassert_false(pixel_on(first_frame, 33, 51), "B lower counter filled");
	/* The test thread continues during the hold; this click must not replay. */
	zassert_ok(zui_desktop_submit_input(desktop, &select));
	zassert_ok(k_sem_take(&second_frame_ready, K_SECONDS(2)), "dashboard never drawn");
	zassert_true(second_frame_ms - first_frame_ms >=
		     CONFIG_MBS_DESKTOP_BOOT_LOGO_DURATION_MS,
		     "logo duration too short: %lld ms", second_frame_ms - first_frame_ms);
	zassert_true(second_frame_ms - first_frame_ms <
		     CONFIG_MBS_DESKTOP_BOOT_LOGO_DURATION_MS + 250,
		     "dashboard handoff too late");
	printk("Boot logo hold: %lld ms\n", second_frame_ms - first_frame_ms);
	k_msleep(50);
	zassert_equal(zui_router_current(desktop->router), MBS_DESKTOP_VIEW_DASHBOARD,
		      "boot input was replayed into the dashboard");
#endif
	/* Observe the dashboard pixels after the handoff (or in the first frame
	 * when the feature is disabled), not merely the router's current ID.
	 */
	for (uint16_t y = 0; y < 4; y++) {
		for (uint16_t x = 0; x < WIDTH; x++) {
			header_lit |= pixel_on(framebuffer, x, y);
		}
	}
	zassert_true(header_lit, "dashboard header missing");
	zassert_equal(zui_router_current(desktop->router), MBS_DESKTOP_VIEW_DASHBOARD);
}

ZTEST_SUITE(desktop_boot_logo, NULL, NULL, NULL, NULL, NULL);
