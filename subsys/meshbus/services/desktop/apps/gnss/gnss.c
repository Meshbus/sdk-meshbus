/* SPDX-License-Identifier: Apache-2.0 */

#include "gnss_private.h"

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/meshbus/desktop.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "apps/app_ids.h"
#include "assets/assets_icons.h"
#include "text/desktop_text.h"

LOG_MODULE_REGISTER(meshbus_desktop_gnss, CONFIG_MESHBUS_DESKTOP_LOG_LEVEL);

static int gnss_app_create(struct gnss_app *app, struct zui_host *host)
{
	int ret;

	memset(app, 0, sizeof(*app));
	app->host = host;
	k_sem_init(&app->exit_sem, 0, 1);
	(void)snprintk(app->status_text, sizeof(app->status_text), "%s",
		       DESKTOP_TEXT_GNSS_STATUS_LOADING);
	(void)snprintk(app->detail_header, sizeof(app->detail_header), "%s",
		       DESKTOP_TEXT_GNSS_SATELLITE);
	(void)snprintk(app->detail_text, sizeof(app->detail_text), "%s",
		       DESKTOP_TEXT_GNSS_NO_SATELLITE_SELECTED);

	app->menu_items[0] = (struct zui_list_item){.id = GNSS_MENU_ITEM_STATUS,
						    .label = DESKTOP_TEXT_GNSS_STATUS};
	app->menu_items[1] = (struct zui_list_item){.id = GNSS_MENU_ITEM_SATELLITES,
						    .label = DESKTOP_TEXT_GNSS_SATELLITES};
	app->menu_items[2] = (struct zui_list_item){.id = GNSS_MENU_ITEM_ACQUISITION,
						    .label = DESKTOP_TEXT_GNSS_ACQUISITION};
	app->menu_items[3] = (struct zui_list_item){.id = GNSS_MENU_ITEM_SETTINGS,
						    .label = DESKTOP_TEXT_COMMON_MENU_SETTINGS};

	app->router = zui_router_create();
	app->menu = zui_sublist_create(&(struct zui_sublist_config){
		.title = DESKTOP_TEXT_GNSS_TITLE,
		.items = app->menu_items,
		.item_count = ARRAY_SIZE(app->menu_items),
		.selected = gnss_menu_selected,
		.user_data = app,
	});
	app->satellites = zui_sublist_create(&(struct zui_sublist_config){
		.title = DESKTOP_TEXT_GNSS_SATELLITES,
		.items = app->sat_items,
		.item_count = 0U,
		.selected = gnss_satellites_selected,
		.user_data = app,
	});
	app->settings_form = zui_form_create(&(struct zui_form_config){
		.title = DESKTOP_TEXT_GNSS_TITLE,
		.activated = gnss_form_activated,
		.changed = gnss_form_changed,
		.user_data = app,
	});
	app->status_view = zui_text_view_create(&(struct zui_text_view_config){
		.title = DESKTOP_TEXT_GNSS_STATUS_TITLE,
		.text = app->status_text,
		.font = ZUI_FONT_SECONDARY,
		.mode = ZUI_TEXT_VIEW_MODE_TEXT,
	});
	app->number_editor = zui_number_editor_create(&(struct zui_number_editor_config){
		.title = DESKTOP_TEXT_GNSS_HEADER_UPDATE_INTERVAL_MS,
		.value = 0,
		.min_value = 0,
		.max_value = GNSS_UPDATE_INTERVAL_MAX,
		.max_digits = 8U,
		.unsigned_only = true,
		.submitted = gnss_number_submitted,
		.user_data = app,
	});
	app->reset_modal = zui_modal_create(&(struct zui_modal_config){
		.title = DESKTOP_TEXT_COMMON_RESET,
		.text = DESKTOP_TEXT_COMMON_RESET_CONFIRM_TEXT,
		.left_button = DESKTOP_TEXT_COMMON_CANCEL,
		.right_button = DESKTOP_TEXT_COMMON_OK,
		.result = gnss_reset_modal_result,
		.user_data = app,
	});
	app->menu_screen = zui_screen_create(&gnss_menu_ops, app);
	app->status_screen = zui_screen_create(&gnss_status_ops, app);
	app->satellites_screen = zui_screen_create(&gnss_satellites_ops, app);
	app->detail_screen = zui_screen_create(&gnss_detail_ops, app);
	app->settings_screen = zui_screen_create(&gnss_settings_ops, app);
	app->number_screen = zui_screen_create(&gnss_number_ops, app);
	app->reset_screen = zui_screen_create(&gnss_reset_ops, app);
	if (app->router == NULL || app->menu == NULL || app->satellites == NULL ||
	    app->settings_form == NULL || app->status_view == NULL || app->number_editor == NULL ||
	    app->reset_modal == NULL || app->menu_screen == NULL || app->status_screen == NULL ||
	    app->satellites_screen == NULL || app->detail_screen == NULL ||
	    app->settings_screen == NULL || app->number_screen == NULL ||
	    app->reset_screen == NULL) {
		return -ENOMEM;
	}

	ret = zui_router_register_screen(app->router, GNSS_APP_SCREEN_MENU, app->menu_screen);
	if (ret != 0) {
		return ret;
	}
	ret = zui_router_register_screen(app->router, GNSS_APP_SCREEN_STATUS, app->status_screen);
	if (ret != 0) {
		return ret;
	}
	ret = zui_router_register_screen(app->router, GNSS_APP_SCREEN_SATELLITES,
					 app->satellites_screen);
	if (ret != 0) {
		return ret;
	}
	ret = zui_router_register_screen(app->router, GNSS_APP_SCREEN_DETAIL, app->detail_screen);
	if (ret != 0) {
		return ret;
	}
	ret = zui_router_register_screen(app->router, GNSS_APP_SCREEN_SETTINGS,
					 app->settings_screen);
	if (ret != 0) {
		return ret;
	}
	ret = zui_router_register_screen(app->router, GNSS_APP_SCREEN_NUMBER, app->number_screen);
	if (ret != 0) {
		return ret;
	}
	ret = zui_router_register_screen(app->router, GNSS_APP_SCREEN_RESET, app->reset_screen);
	if (ret != 0) {
		return ret;
	}
	ret = zui_host_attach_router(host, ZUI_LAYER_FULLSCREEN, app->router);
	if (ret != 0) {
		return ret;
	}
	gnss_switch(app, GNSS_APP_SCREEN_MENU);
	return 0;
}

static void gnss_app_destroy(struct gnss_app *app)
{
	if (app == NULL) {
		return;
	}

	if (app->host != NULL) {
		(void)zui_host_detach_router(app->host, ZUI_LAYER_FULLSCREEN);
	}
	if (app->router != NULL) {
		(void)zui_router_unregister_screen(app->router, GNSS_APP_SCREEN_MENU);
		(void)zui_router_unregister_screen(app->router, GNSS_APP_SCREEN_STATUS);
		(void)zui_router_unregister_screen(app->router, GNSS_APP_SCREEN_SATELLITES);
		(void)zui_router_unregister_screen(app->router, GNSS_APP_SCREEN_DETAIL);
		(void)zui_router_unregister_screen(app->router, GNSS_APP_SCREEN_SETTINGS);
		(void)zui_router_unregister_screen(app->router, GNSS_APP_SCREEN_NUMBER);
		(void)zui_router_unregister_screen(app->router, GNSS_APP_SCREEN_RESET);
	}
	zui_screen_destroy(app->menu_screen);
	zui_screen_destroy(app->status_screen);
	zui_screen_destroy(app->satellites_screen);
	zui_screen_destroy(app->detail_screen);
	zui_screen_destroy(app->settings_screen);
	zui_screen_destroy(app->number_screen);
	zui_screen_destroy(app->reset_screen);
	zui_modal_destroy(app->reset_modal);
	zui_number_editor_destroy(app->number_editor);
	zui_text_view_destroy(app->status_view);
	zui_form_destroy(app->settings_form);
	zui_sublist_destroy(app->satellites);
	zui_sublist_destroy(app->menu);
	zui_router_destroy(app->router);
}

static void gnss_app_main(void *args)
{
	struct meshbus_desktop_app_args *app_args = args;
	struct gnss_app *app;
	int ret;

	if (app_args == NULL || app_args->host == NULL) {
		return;
	}

	app = k_calloc(1U, sizeof(*app));
	if (app == NULL) {
		return;
	}

	ret = gnss_app_create(app, app_args->host);
	if (ret != 0) {
		LOG_WRN("Failed to start migrated GNSS app: %d", ret);
		gnss_app_destroy(app);
		k_free(app);
		return;
	}

	(void)k_sem_take(&app->exit_sem, K_FOREVER);
	gnss_app_destroy(app);
	k_free(app);
}

MESHBUS_DESKTOP_APP_DEFINE(MESHBUS_DESKTOP_APP_ID_GNSS,
			   MESHBUS_DESKTOP_APP_NAME_GNSS,
			   gnss_app_main,
			   4096,
			   &A_gnss_14x14,
			   MESHBUS_DESKTOP_APP_MENU_INDEX_GNSS);
