/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "radio_private.h"

#include <zephyr/sys/util.h>

#include "assets/assets_icons.h"
#include "text/desktop_text.h"

void radio_menu_selected(struct zui_sublist *list, uint32_t id, size_t index,
				const struct zui_input_event *event, void *user_data)
{
	struct radio_app *app = user_data;
	mbs_radio_config cfg;

	ARG_UNUSED(list);
	ARG_UNUSED(index);
	ARG_UNUSED(event);

	if (app == NULL) {
		return;
	}

	if ((id == RADIO_MENU_ITEM_PACKET || id == RADIO_MENU_ITEM_CW ||
	     id == RADIO_MENU_ITEM_NOISE || id == RADIO_MENU_ITEM_CALIBRATE_NF ||
	     id == RADIO_MENU_ITEM_AGC_RESET) &&
	    radio_require_enabled(app)) {
		return;
	}

	switch (id) {
	case RADIO_MENU_ITEM_PACKET:
		radio_switch(app, RADIO_SCREEN_PACKET);
		break;
	case RADIO_MENU_ITEM_CW:
		if (mbs_radio_config_get(&cfg) == 0) {
			app->cw_frequency_hz = (uint32_t)cfg.frequency;
			app->cw_tx_power = (uint8_t)CLAMP((int32_t)cfg.tx_power, 0, 22);
		}
		radio_cw_refresh(app);
		radio_switch(app, RADIO_SCREEN_CW);
		break;
	case RADIO_MENU_ITEM_NOISE:
		radio_switch(app, RADIO_SCREEN_NOISE);
		break;
	case RADIO_MENU_ITEM_CALIBRATE_NF:
		mbs_radio_noise_calibrate(RADIO_MENU_NF_CAL_THRESHOLD_DB);
		radio_toast(app, DESKTOP_TEXT_RADIO_TITLE, DESKTOP_TEXT_RADIO_CALIBRATING,
			    &I_done_24x24, 2000U);
		break;
	case RADIO_MENU_ITEM_AGC_RESET:
		radio_submit_deferred_action(app, RADIO_DEFERRED_ACTION_AGC_RESET);
		break;
	case RADIO_MENU_ITEM_SETTINGS:
		app->settings_reload = true;
		radio_settings_load(app);
		radio_settings_refresh(app);
		radio_switch(app, RADIO_SCREEN_SETTINGS);
		break;
	default:
		break;
	}
}
static void radio_menu_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct radio_app *app = user_data;

	(void)zui_screen_draw(zui_sublist_get_screen(app->menu), draw);
}

static bool radio_menu_input(const struct zui_input_event *event, void *user_data)
{
	struct radio_app *app = user_data;

	if (app == NULL || event == NULL) {
		return false;
	}
	if (desktop_app_input_should_consume_edge(event)) {
		return true;
	}
	if (desktop_app_input_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		app->exit_requested = true;
		(void)zui_host_detach_router(app->host, ZUI_LAYER_FULLSCREEN);
		k_sem_give(&app->exit_sem);
		return true;
	}
	if (zui_screen_submit_input(zui_sublist_get_screen(app->menu), event) > 0) {
		radio_request_redraw(app);
		return true;
	}

	return false;
}

static void radio_menu_enter(void *user_data)
{
	struct radio_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_enter(zui_sublist_get_screen(app->menu));
	}
}

static void radio_menu_exit(void *user_data)
{
	struct radio_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_exit(zui_sublist_get_screen(app->menu));
	}
}
const struct zui_screen_ops radio_menu_ops = {
	.draw = radio_menu_draw,
	.input = radio_menu_input,
	.enter = radio_menu_enter,
	.exit = radio_menu_exit,
};
