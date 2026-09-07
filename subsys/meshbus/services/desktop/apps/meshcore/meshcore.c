/* SPDX-License-Identifier: Apache-2.0 */

#include "meshcore_private.h"

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/meshbus/desktop.h>
#include <zephyr/sys/util.h>

#include "apps/app_ids.h"
#include "assets/assets_icons.h"
#include "text/desktop_text.h"

LOG_MODULE_REGISTER(meshbus_desktop_meshcore, CONFIG_MESHBUS_DESKTOP_LOG_LEVEL);

static int meshcore_register_screen(struct meshcore_app *app, uint32_t id,
				    struct zui_screen *screen)
{
	return zui_router_register_screen(app->router, id, screen);
}

static int meshcore_app_create(struct meshcore_app *app, struct zui_host *host)
{
	int ret;

	if (app == NULL || host == NULL) {
		return -EINVAL;
	}

	k_work_init(&app->settings_work, meshcore_settings_work);
	atomic_clear(&app->settings_busy);
	app->host = host;
	app->selected_channel_idx = UINT8_MAX;
	app->radio_preset_selected_idx = MESHCORE_RADIO_PRESET_CUSTOM;
	app->router = zui_router_create();
	k_sem_init(&app->exit_sem, 0, 1);
	meshcore_reload_node(app);

	app->menu = zui_sublist_create(&(struct zui_sublist_config){
		.title = DESKTOP_TEXT_MESHCORE_TITLE,
		.selected = meshcore_menu_selected,
		.user_data = app,
	});
	app->radio_preset = zui_sublist_create(&(struct zui_sublist_config){
		.title = DESKTOP_TEXT_MESHCORE_RADIO_PRESET_TITLE,
		.selected = meshcore_radio_selected,
		.user_data = app,
	});
	app->channels = zui_sublist_create(&(struct zui_sublist_config){
		.title = DESKTOP_TEXT_MESHCORE_CHANNELS_TITLE,
		.selected = meshcore_channel_selected,
		.user_data = app,
	});
	app->settings_form = zui_form_create(&(struct zui_form_config){
		.title = DESKTOP_TEXT_MESHCORE_TITLE,
		.activated = meshcore_settings_activated,
		.user_data = app,
	});
	app->channel_form = zui_form_create(&(struct zui_form_config){
		.title = DESKTOP_TEXT_MESHCORE_CHANNELS_TITLE,
		.activated = meshcore_channel_form_activated,
		.user_data = app,
	});
	app->text_editor = zui_text_editor_create(&(struct zui_text_editor_config){
		.title = DESKTOP_TEXT_MESHCORE_SETTINGS_NAME,
		.buffer = app->text_buf,
		.buffer_size = sizeof(app->text_buf),
		.submitted = meshcore_text_submitted,
		.user_data = app,
	});
	app->number_editor = zui_number_editor_create(&(struct zui_number_editor_config){
		.title = DESKTOP_TEXT_MESHCORE_NUMBER_INPUT_MULTI_ACKS,
		.value = 0,
		.min_value = 0,
		.max_value = 255,
		.unsigned_only = true,
		.submitted = meshcore_number_submitted,
		.user_data = app,
	});
	app->detail_view = zui_text_view_create(&(struct zui_text_view_config){
		.title = DESKTOP_TEXT_MESHCORE_PUBLIC_KEY_TITLE,
		.text = app->detail_text,
		.font = ZUI_FONT_SECONDARY,
		.mode = ZUI_TEXT_VIEW_MODE_TEXT,
	});
	app->modal = zui_modal_create(&(struct zui_modal_config){
		.title = DESKTOP_TEXT_MESHCORE_TITLE,
		.text = "",
		.center_button = DESKTOP_TEXT_COMMON_CANCEL,
		.result = meshcore_modal_result,
		.user_data = app,
	});
	app->menu_screen = zui_screen_create(&meshcore_menu_ops, app);
	app->settings_screen = zui_screen_create(&meshcore_settings_ops, app);
	app->radio_screen = zui_screen_create(&meshcore_radio_ops, app);
	app->channels_screen = zui_screen_create(&meshcore_channels_ops, app);
	app->channel_settings_screen = zui_screen_create(&meshcore_channel_form_ops, app);
	app->text_screen = zui_screen_create(&meshcore_text_ops, app);
	app->number_screen = zui_screen_create(&meshcore_number_ops, app);
	app->detail_screen = zui_screen_create(&meshcore_detail_ops, app);
	app->modal_screen = zui_screen_create(&meshcore_modal_ops, app);
	if (app->router == NULL || app->menu == NULL || app->radio_preset == NULL ||
	    app->channels == NULL || app->settings_form == NULL || app->channel_form == NULL ||
	    app->text_editor == NULL || app->number_editor == NULL || app->detail_view == NULL ||
	    app->modal == NULL || app->menu_screen == NULL || app->settings_screen == NULL ||
	    app->radio_screen == NULL || app->channels_screen == NULL ||
	    app->channel_settings_screen == NULL || app->text_screen == NULL ||
	    app->number_screen == NULL || app->detail_screen == NULL || app->modal_screen == NULL) {
		return -ENOMEM;
	}

	meshcore_prepare_menu(app);
	meshcore_build_radio_preset_list(app);
	meshcore_build_channels_list(app);
	meshcore_build_settings_form(app);

	ret = meshcore_register_screen(app, MESHCORE_SCREEN_MENU, app->menu_screen);
	if (ret != 0) {
		return ret;
	}
	ret = meshcore_register_screen(app, MESHCORE_SCREEN_SETTINGS, app->settings_screen);
	if (ret != 0) {
		return ret;
	}
	ret = meshcore_register_screen(app, MESHCORE_SCREEN_RADIO_PRESET, app->radio_screen);
	if (ret != 0) {
		return ret;
	}
	ret = meshcore_register_screen(app, MESHCORE_SCREEN_CHANNELS, app->channels_screen);
	if (ret != 0) {
		return ret;
	}
	ret = meshcore_register_screen(app, MESHCORE_SCREEN_CHANNEL_SETTINGS,
				       app->channel_settings_screen);
	if (ret != 0) {
		return ret;
	}
	ret = meshcore_register_screen(app, MESHCORE_SCREEN_TEXT, app->text_screen);
	if (ret != 0) {
		return ret;
	}
	ret = meshcore_register_screen(app, MESHCORE_SCREEN_NUMBER, app->number_screen);
	if (ret != 0) {
		return ret;
	}
	ret = meshcore_register_screen(app, MESHCORE_SCREEN_DETAIL, app->detail_screen);
	if (ret != 0) {
		return ret;
	}
	ret = meshcore_register_screen(app, MESHCORE_SCREEN_MODAL, app->modal_screen);
	if (ret != 0) {
		return ret;
	}
	ret = zui_host_attach_router(host, ZUI_LAYER_FULLSCREEN, app->router);
	if (ret != 0) {
		return ret;
	}

	meshcore_switch(app, MESHCORE_SCREEN_MENU);
	return 0;
}

static void meshcore_app_destroy(struct meshcore_app *app)
{
	if (app == NULL) {
		return;
	}

	struct k_work_sync sync;

	(void)k_work_cancel_sync(&app->settings_work, &sync);

	if (app->host != NULL) {
		(void)zui_host_detach_router(app->host, ZUI_LAYER_FULLSCREEN);
	}
	if (app->router != NULL) {
		(void)zui_router_unregister_screen(app->router, MESHCORE_SCREEN_MENU);
		(void)zui_router_unregister_screen(app->router, MESHCORE_SCREEN_SETTINGS);
		(void)zui_router_unregister_screen(app->router, MESHCORE_SCREEN_RADIO_PRESET);
		(void)zui_router_unregister_screen(app->router, MESHCORE_SCREEN_CHANNELS);
		(void)zui_router_unregister_screen(app->router, MESHCORE_SCREEN_CHANNEL_SETTINGS);
		(void)zui_router_unregister_screen(app->router, MESHCORE_SCREEN_TEXT);
		(void)zui_router_unregister_screen(app->router, MESHCORE_SCREEN_NUMBER);
		(void)zui_router_unregister_screen(app->router, MESHCORE_SCREEN_DETAIL);
		(void)zui_router_unregister_screen(app->router, MESHCORE_SCREEN_MODAL);
	}
	zui_screen_destroy(app->menu_screen);
	zui_screen_destroy(app->settings_screen);
	zui_screen_destroy(app->radio_screen);
	zui_screen_destroy(app->channels_screen);
	zui_screen_destroy(app->channel_settings_screen);
	zui_screen_destroy(app->text_screen);
	zui_screen_destroy(app->number_screen);
	zui_screen_destroy(app->detail_screen);
	zui_screen_destroy(app->modal_screen);
	zui_modal_destroy(app->modal);
	zui_text_view_destroy(app->detail_view);
	zui_number_editor_destroy(app->number_editor);
	zui_text_editor_destroy(app->text_editor);
	zui_form_destroy(app->channel_form);
	zui_form_destroy(app->settings_form);
	zui_sublist_destroy(app->channels);
	zui_sublist_destroy(app->radio_preset);
	zui_sublist_destroy(app->menu);
	zui_router_destroy(app->router);
}

static void meshcore_app_main(void *args)
{
	struct meshbus_desktop_app_args *app_args = args;
	struct meshcore_app *app;
	int ret;

	if (app_args == NULL || app_args->host == NULL) {
		return;
	}

	app = k_calloc(1U, sizeof(*app));
	if (app == NULL) {
		return;
	}

	ret = meshcore_app_create(app, app_args->host);
	if (ret != 0) {
		LOG_WRN("Failed to start migrated MeshCore app: %d", ret);
		meshcore_app_destroy(app);
		k_free(app);
		return;
	}

	(void)k_sem_take(&app->exit_sem, K_FOREVER);
	meshcore_app_destroy(app);
	k_free(app);
}

MESHBUS_DESKTOP_APP_DEFINE(MESHBUS_DESKTOP_APP_ID_MESHCORE, MESHBUS_DESKTOP_APP_NAME_MESHCORE,
			   meshcore_app_main, 4096, &A_meshcore_14x14,
			   MESHBUS_DESKTOP_APP_MENU_INDEX_MESHCORE);
