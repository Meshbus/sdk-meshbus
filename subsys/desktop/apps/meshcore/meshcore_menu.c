/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "meshcore_private.h"

#include <zephyr/sys/util.h>

#include "text/desktop_text.h"

void meshcore_prepare_menu(struct meshcore_app *app)
{
	if (app == NULL) {
		return;
	}

	app->menu_items[0] = (struct zui_list_item){
		.id = MESHCORE_MENU_ADVERT,
		.label = DESKTOP_TEXT_MESHCORE_ADVERT,
	};
	app->menu_items[1] = (struct zui_list_item){
		.id = MESHCORE_MENU_RADIO,
		.label = DESKTOP_TEXT_MESHCORE_MENU_RADIO_PRESET,
	};
	app->menu_items[2] = (struct zui_list_item){
		.id = MESHCORE_MENU_CHANNELS,
		.label = DESKTOP_TEXT_MESHCORE_MENU_CHANNELS,
	};
	app->menu_items[3] = (struct zui_list_item){
		.id = MESHCORE_MENU_SETTINGS,
		.label = DESKTOP_TEXT_COMMON_MENU_SETTINGS,
	};
	(void)zui_sublist_update(app->menu, &(struct zui_sublist_config){
						    .title = DESKTOP_TEXT_MESHCORE_TITLE,
						    .items = app->menu_items,
						    .item_count = ARRAY_SIZE(app->menu_items),
						    .selected = meshcore_menu_selected,
						    .user_data = app,
					    });
}
void meshcore_menu_selected(struct zui_sublist *list, uint32_t id, size_t index,
			    const struct zui_input_event *event, void *user_data)
{
	struct meshcore_app *app = user_data;

	ARG_UNUSED(list);
	ARG_UNUSED(index);
	ARG_UNUSED(event);

	if (app == NULL) {
		return;
	}

	switch (id) {
	case MESHCORE_MENU_ADVERT:
		meshcore_show_modal(app, MESHCORE_MODAL_ADVERT, DESKTOP_TEXT_MESHCORE_ADVERT,
				    DESKTOP_TEXT_MESHCORE_ADVERT_SELECT_TEXT,
				    DESKTOP_TEXT_MESHCORE_ADVERT_BUTTON_FLOOD,
				    DESKTOP_TEXT_MESHCORE_ADVERT_BUTTON_ZEROHOP);
		break;
	case MESHCORE_MENU_RADIO:
		app->radio_preset_selected_idx = meshcore_radio_preset_match();
		meshcore_build_radio_preset_list(app);
		meshcore_switch(app, MESHCORE_SCREEN_RADIO_PRESET);
		break;
	case MESHCORE_MENU_CHANNELS:
		meshcore_build_channels_list(app);
		meshcore_switch(app, MESHCORE_SCREEN_CHANNELS);
		break;
	case MESHCORE_MENU_SETTINGS:
		meshcore_reload_node(app);
		meshcore_build_settings_form(app);
		meshcore_switch(app, MESHCORE_SCREEN_SETTINGS);
		break;
	default:
		break;
	}
}
static void meshcore_menu_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_draw(zui_sublist_get_screen(app->menu), draw);
	}
}

static bool meshcore_menu_input(const struct zui_input_event *event, void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app == NULL) {
		return false;
	}
	if (desktop_app_input_should_consume_edge(event)) {
		return true;
	}
	if (desktop_app_input_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		meshcore_exit(app);
		return true;
	}
	return meshcore_forward_input(zui_sublist_get_screen(app->menu), event);
}

static void meshcore_menu_enter(void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		meshcore_forward_enter(zui_sublist_get_screen(app->menu));
	}
}

static void meshcore_menu_exit(void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		meshcore_forward_exit(zui_sublist_get_screen(app->menu));
	}
}

const struct zui_screen_ops meshcore_menu_ops = {
	.draw = meshcore_menu_draw,
	.input = meshcore_menu_input,
	.enter = meshcore_menu_enter,
	.exit = meshcore_menu_exit,
};
