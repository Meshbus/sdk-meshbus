/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "system_private.h"

#if defined(CONFIG_BT)
void system_bluetooth_addr(char *out, size_t out_size)
{
	if (out == NULL || out_size == 0U) {
		return;
	}

	(void)snprintk(out, out_size, "%s", DESKTOP_TEXT_COMMON_NOT_AVAILABLE);

#if defined(CONFIG_BT_ID_MAX) && (CONFIG_BT_ID_MAX > 0)
	bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
	size_t count = ARRAY_SIZE(addrs);

	bt_id_get(addrs, &count);
	if (count > 0U) {
		const uint8_t *v = addrs[0].a.val;
		bool all_zero = true;

		for (size_t i = 0U; i < sizeof(addrs[0].a.val); i++) {
			if (v[i] != 0U) {
				all_zero = false;
				break;
			}
		}
		if (!all_zero) {
			(void)snprintk(out, out_size, "%02X:%02X:%02X:%02X:%02X:%02X",
				       v[5], v[4], v[3], v[2], v[1], v[0]);
		}
	}
#endif
}
#endif

#if defined(CONFIG_MBS_BLUETOOTH) || defined(CONFIG_MBS_INDICATOR) || \
	defined(CONFIG_MBS_TELEMETRY)
size_t system_bool_idx(bool value)
{
	return value ? 1U : 0U;
}

bool system_bool_from_idx(size_t option_index)
{
	return option_index != 0U;
}
#endif

void system_open_menu(struct system_app *app)
{
	app->text_back_screen = 0U;
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_MENU);
}

void system_open_info_menu(struct system_app *app)
{
	app->text_back_screen = 0U;
	(void)zui_sublist_update(app->info_menu, &(struct zui_sublist_config){
		.title = DESKTOP_TEXT_SYSTEM_MENU_INFORMATION,
		.items = app->info_menu_items,
		.item_count = ARRAY_SIZE(app->info_menu_items),
		.selected = system_info_menu_selected,
		.user_data = app,
	});
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_INFO_MENU);
}

void system_text_update(struct system_app *app, const char *title)
{
	(void)zui_text_view_update(app->text_view, &(struct zui_text_view_config){
		.title = title,
		.text = app->text,
		.font = ZUI_FONT_SECONDARY,
		.mode = ZUI_TEXT_VIEW_MODE_TEXT,
	});
}

static void system_text_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct system_app *app = user_data;

	(void)zui_screen_draw(zui_text_view_get_screen(app->text_view), draw);
}

static bool system_text_input(const struct zui_input_event *event, void *user_data)
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
		if (app->text_back_screen != 0U) {
			desktop_app_switch(&app->ctx, app->text_back_screen);
		} else {
			system_open_menu(app);
		}
		return true;
	}

	ret = zui_screen_submit_input(zui_text_view_get_screen(app->text_view), event);
	if (ret > 0) {
		desktop_app_request_redraw(&app->ctx);
		return true;
	}

	return false;
}

const struct zui_screen_ops system_text_ops = {
	.draw = system_text_draw,
	.input = system_text_input,
};
