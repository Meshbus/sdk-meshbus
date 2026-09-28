/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "system_private.h"

void system_menu_selected(struct zui_sublist *list, uint32_t id, size_t index,
				 const struct zui_input_event *event, void *user_data)
{
	struct system_app *app = user_data;

	ARG_UNUSED(list);
	ARG_UNUSED(index);
	ARG_UNUSED(event);

	if (app != NULL) {
		if (id == SYSTEM_MENU_INFORMATION) {
			system_open_info_menu(app);
			return;
		}
#if defined(CONFIG_MBS_DISPLAY)
		if (id == SYSTEM_MENU_DISPLAY) {
			system_open_display_form(app);
			return;
		}
#endif
#if defined(CONFIG_MBS_BLUETOOTH)
		if (id == SYSTEM_MENU_BLUETOOTH) {
			system_open_bluetooth_form(app);
			return;
		}
#endif
#if defined(CONFIG_MBS_CLOCK)
		if (id == SYSTEM_MENU_CLOCK) {
			system_open_clock_form(app);
			return;
		}
#endif
#if defined(CONFIG_MBS_POWER)
		if (id == SYSTEM_MENU_POWER) {
			system_open_power_form(app);
			return;
		}
#endif
#if defined(CONFIG_MBS_TELEMETRY)
		if (id == SYSTEM_MENU_TELEMETRY) {
			system_open_telemetry_menu(app);
			return;
		}
#endif
#if defined(CONFIG_MBS_INDICATOR)
		if (id == SYSTEM_MENU_INDICATOR) {
			system_open_indicator_form(app);
			return;
		}
#endif
	}
}

static void system_menu_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct system_app *app = user_data;

	(void)zui_screen_draw(zui_sublist_get_screen(app->menu), draw);
}

static void system_info_menu_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct system_app *app = user_data;

	(void)zui_screen_draw(zui_sublist_get_screen(app->info_menu), draw);
}

static bool system_menu_input(const struct zui_input_event *event, void *user_data)
{
	struct system_app *app = user_data;
	int ret;

	if (app == NULL || event == NULL) {
		return false;
	}
	if (desktop_app_input_should_consume_edge(event)) {
		return true;
	}
	if (desktop_app_input_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		desktop_app_exit(&app->ctx);
		return true;
	}

	ret = zui_screen_submit_input(zui_sublist_get_screen(app->menu), event);
	if (ret > 0) {
		desktop_app_request_redraw(&app->ctx);
		return true;
	}

	return false;
}

static bool system_info_menu_input(const struct zui_input_event *event, void *user_data)
{
	struct system_app *app = user_data;
	int ret;

	if (app == NULL || event == NULL) {
		return false;
	}
	if (desktop_app_input_should_consume_edge(event)) {
		return true;
	}
	if (desktop_app_input_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		system_open_menu(app);
		return true;
	}

	ret = zui_screen_submit_input(zui_sublist_get_screen(app->info_menu), event);
	if (ret > 0) {
		desktop_app_request_redraw(&app->ctx);
		return true;
	}

	return false;
}

const struct zui_screen_ops system_menu_ops = {
	.draw = system_menu_draw,
	.input = system_menu_input,
};

const struct zui_screen_ops system_info_menu_ops = {
	.draw = system_info_menu_draw,
	.input = system_info_menu_input,
};
