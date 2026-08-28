/* SPDX-License-Identifier: Apache-2.0 */

#include "radio_private.h"

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/meshbus/desktop.h>
#include <zephyr/sys/util.h>

#include "apps/app_ids.h"
#include "assets/assets_icons.h"
#include "text/desktop_text.h"

LOG_MODULE_REGISTER(meshbus_desktop_radio, CONFIG_MESHBUS_DESKTOP_LOG_LEVEL);

static int radio_register_screen(struct radio_app *app, uint32_t id, struct zui_screen *screen)
{
	return zui_router_register_screen(app->router, id, screen);
}

static int radio_app_create(struct radio_app *app, struct zui_host *host)
{
	int ret;

	memset(app, 0, sizeof(*app));
	app->host = host;
	app->current_screen = RADIO_SCREEN_MENU;
	app->settings_reload = true;
	app->cw_duration_s = 1U;
	atomic_clear(&app->cw_running);
	k_sem_init(&app->exit_sem, 0, 1);
	k_work_init_delayable(&app->tick_work, radio_tick_work);
	k_work_init(&app->action_work, radio_deferred_action_work);
	k_work_init(&app->cw_done_work, radio_cw_done_work);
	radio_settings_defaults(&app->applied);
	app->editing = app->applied;
	app->cw_tx_power = app->applied.tx_power;
	app->cw_frequency_hz = app->applied.frequency_hz;
	app->capture_model.hex_visible_lines = 1U;
	radio_noise_reset(&app->noise);

	app->menu_items[0] = (struct zui_list_item){.id = RADIO_MENU_ITEM_PACKET,
						    .label = DESKTOP_TEXT_RADIO_PACKET};
	app->menu_items[1] = (struct zui_list_item){.id = RADIO_MENU_ITEM_CW,
						    .label = DESKTOP_TEXT_RADIO_CONTINUOUS_WAVE};
	app->menu_items[2] = (struct zui_list_item){.id = RADIO_MENU_ITEM_NOISE,
						    .label = DESKTOP_TEXT_RADIO_NOISE_ANALYZER};
	app->menu_items[3] = (struct zui_list_item){.id = RADIO_MENU_ITEM_CALIBRATE_NF,
						    .label = DESKTOP_TEXT_RADIO_CALIBRATE_NF};
	app->menu_items[4] = (struct zui_list_item){.id = RADIO_MENU_ITEM_AGC_RESET,
						    .label = DESKTOP_TEXT_RADIO_AGC_RESET};
	app->menu_items[5] = (struct zui_list_item){.id = RADIO_MENU_ITEM_SETTINGS,
						    .label = DESKTOP_TEXT_RADIO_SETTINGS};
	app->packet_items[0] = (struct zui_list_item){.id = RADIO_PACKET_ITEM_TEXT,
						      .label = DESKTOP_TEXT_RADIO_SEND_AS_TEXT};
	app->packet_items[1] = (struct zui_list_item){.id = RADIO_PACKET_ITEM_HEX,
						      .label = DESKTOP_TEXT_RADIO_SEND_AS_HEX};
	app->packet_items[2] = (struct zui_list_item){.id = RADIO_PACKET_ITEM_CAPTURE,
						      .label = DESKTOP_TEXT_RADIO_PACKET_CAPTURE};

	app->router = zui_router_create();
	app->menu = zui_sublist_create(&(struct zui_sublist_config){
		.title = DESKTOP_TEXT_RADIO_TITLE,
		.items = app->menu_items,
		.item_count = ARRAY_SIZE(app->menu_items),
		.selected = radio_menu_selected,
		.user_data = app,
	});
	app->packet_menu = zui_sublist_create(&(struct zui_sublist_config){
		.title = DESKTOP_TEXT_RADIO_PACKET,
		.items = app->packet_items,
		.item_count = ARRAY_SIZE(app->packet_items),
		.selected = radio_packet_selected,
		.user_data = app,
	});
	app->settings_form = zui_form_create(&(struct zui_form_config){
		.title = DESKTOP_TEXT_RADIO_TITLE,
		.changed = radio_settings_changed,
		.activated = radio_settings_activated,
		.user_data = app,
	});
	app->cw_form = zui_form_create(&(struct zui_form_config){
		.title = DESKTOP_TEXT_RADIO_CONTINUOUS_WAVE,
		.changed = radio_cw_changed,
		.activated = radio_cw_activated,
		.user_data = app,
	});
	app->number_editor = zui_number_editor_create(&(struct zui_number_editor_config){
		.title = DESKTOP_TEXT_RADIO_HEADER_FREQUENCY_HZ,
		.value = app->applied.frequency_hz,
		.min_value = RADIO_FREQUENCY_MIN_HZ,
		.max_value = RADIO_FREQUENCY_MAX_HZ,
		.max_digits = 10U,
		.unsigned_only = true,
		.submitted = radio_number_submitted,
		.user_data = app,
	});
	app->text_editor = zui_text_editor_create(&(struct zui_text_editor_config){
		.title = DESKTOP_TEXT_RADIO_SEND_AS_TEXT,
		.buffer = app->send_text,
		.buffer_size = sizeof(app->send_text),
		.min_length = 1U,
		.submitted = radio_text_submitted,
		.user_data = app,
	});
	app->hex_editor = zui_hex_editor_create(&(struct zui_hex_editor_config){
		.title = DESKTOP_TEXT_RADIO_SEND_AS_HEX,
		.bytes = app->send_hex,
		.byte_count = sizeof(app->send_hex),
		.submitted = radio_hex_submitted,
		.user_data = app,
	});
	app->reset_modal = zui_modal_create(&(struct zui_modal_config){
		.title = DESKTOP_TEXT_COMMON_RESET,
		.text = DESKTOP_TEXT_COMMON_RESET_CONFIRM_TEXT,
		.left_button = DESKTOP_TEXT_COMMON_CANCEL,
		.right_button = DESKTOP_TEXT_COMMON_OK,
		.result = radio_reset_modal_result,
		.user_data = app,
	});
	app->menu_screen = zui_screen_create(&radio_menu_ops, app);
	app->packet_screen = zui_screen_create(&radio_packet_ops, app);
	app->capture_screen = zui_screen_create(&radio_capture_ops, app);
	app->cw_screen = zui_screen_create(&radio_cw_ops, app);
	app->noise_screen = zui_screen_create(&radio_noise_ops, app);
	app->settings_screen = zui_screen_create(&radio_settings_ops, app);
	app->number_screen = zui_screen_create(&radio_number_ops, app);
	app->text_screen = zui_screen_create(&radio_text_ops, app);
	app->hex_screen = zui_screen_create(&radio_hex_ops, app);
	app->reset_screen = zui_screen_create(&radio_reset_ops, app);
	if (app->router == NULL || app->menu == NULL || app->packet_menu == NULL ||
	    app->settings_form == NULL || app->cw_form == NULL || app->number_editor == NULL ||
	    app->text_editor == NULL || app->hex_editor == NULL || app->reset_modal == NULL ||
	    app->menu_screen == NULL || app->packet_screen == NULL ||
	    app->capture_screen == NULL || app->cw_screen == NULL || app->noise_screen == NULL ||
	    app->settings_screen == NULL || app->number_screen == NULL ||
	    app->text_screen == NULL || app->hex_screen == NULL || app->reset_screen == NULL) {
		return -ENOMEM;
	}

	ret = radio_register_screen(app, RADIO_SCREEN_MENU, app->menu_screen);
	if (ret != 0) {
		return ret;
	}
	ret = radio_register_screen(app, RADIO_SCREEN_PACKET, app->packet_screen);
	if (ret != 0) {
		return ret;
	}
	ret = radio_register_screen(app, RADIO_SCREEN_CAPTURE, app->capture_screen);
	if (ret != 0) {
		return ret;
	}
	ret = radio_register_screen(app, RADIO_SCREEN_CW, app->cw_screen);
	if (ret != 0) {
		return ret;
	}
	ret = radio_register_screen(app, RADIO_SCREEN_NOISE, app->noise_screen);
	if (ret != 0) {
		return ret;
	}
	ret = radio_register_screen(app, RADIO_SCREEN_SETTINGS, app->settings_screen);
	if (ret != 0) {
		return ret;
	}
	ret = radio_register_screen(app, RADIO_SCREEN_NUMBER, app->number_screen);
	if (ret != 0) {
		return ret;
	}
	ret = radio_register_screen(app, RADIO_SCREEN_TEXT_EDITOR, app->text_screen);
	if (ret != 0) {
		return ret;
	}
	ret = radio_register_screen(app, RADIO_SCREEN_HEX_EDITOR, app->hex_screen);
	if (ret != 0) {
		return ret;
	}
	ret = radio_register_screen(app, RADIO_SCREEN_RESET, app->reset_screen);
	if (ret != 0) {
		return ret;
	}
	radio_capture_attach_app(app);
	radio_capture_set_enabled(false);
	radio_cw_attach_app(app);
	ret = zui_host_attach_router(host, ZUI_LAYER_FULLSCREEN, app->router);
	if (ret != 0) {
		return ret;
	}
	radio_switch(app, RADIO_SCREEN_MENU);
	return 0;
}

static void radio_app_destroy(struct radio_app *app)
{
	struct k_work_sync sync;

	if (app == NULL) {
		return;
	}

	(void)k_work_cancel_delayable_sync(&app->tick_work, &sync);
	(void)k_work_cancel_sync(&app->action_work, &sync);
	radio_cw_detach_app(app);
	radio_cw_wait_idle();
	(void)k_work_cancel_sync(&app->cw_done_work, &sync);
	radio_capture_set_enabled(false);
	atomic_clear(&app->capture_enabled);
	if (radio_capture_is_attached(app)) {
		radio_capture_wait_idle();
		radio_capture_detach_app(app);
	}
	radio_cw_dismiss_progress(app);
	if (app->host != NULL) {
		(void)zui_host_detach_router(app->host, ZUI_LAYER_FULLSCREEN);
	}
	if (app->router != NULL) {
		(void)zui_router_unregister_screen(app->router, RADIO_SCREEN_MENU);
		(void)zui_router_unregister_screen(app->router, RADIO_SCREEN_PACKET);
		(void)zui_router_unregister_screen(app->router, RADIO_SCREEN_CAPTURE);
		(void)zui_router_unregister_screen(app->router, RADIO_SCREEN_CW);
		(void)zui_router_unregister_screen(app->router, RADIO_SCREEN_NOISE);
		(void)zui_router_unregister_screen(app->router, RADIO_SCREEN_SETTINGS);
		(void)zui_router_unregister_screen(app->router, RADIO_SCREEN_NUMBER);
		(void)zui_router_unregister_screen(app->router, RADIO_SCREEN_TEXT_EDITOR);
		(void)zui_router_unregister_screen(app->router, RADIO_SCREEN_HEX_EDITOR);
		(void)zui_router_unregister_screen(app->router, RADIO_SCREEN_RESET);
	}
	zui_screen_destroy(app->menu_screen);
	zui_screen_destroy(app->packet_screen);
	zui_screen_destroy(app->capture_screen);
	zui_screen_destroy(app->cw_screen);
	zui_screen_destroy(app->noise_screen);
	zui_screen_destroy(app->settings_screen);
	zui_screen_destroy(app->number_screen);
	zui_screen_destroy(app->text_screen);
	zui_screen_destroy(app->hex_screen);
	zui_screen_destroy(app->reset_screen);
	zui_modal_destroy(app->reset_modal);
	zui_hex_editor_destroy(app->hex_editor);
	zui_text_editor_destroy(app->text_editor);
	zui_number_editor_destroy(app->number_editor);
	zui_form_destroy(app->cw_form);
	zui_form_destroy(app->settings_form);
	zui_sublist_destroy(app->packet_menu);
	zui_sublist_destroy(app->menu);
	zui_router_destroy(app->router);
}

static void radio_app_main(void *args)
{
	struct meshbus_desktop_app_args *app_args = args;
	struct radio_app *app;
	int ret;

	if (app_args == NULL || app_args->host == NULL) {
		return;
	}

	app = k_calloc(1U, sizeof(*app));
	if (app == NULL) {
		return;
	}

	ret = radio_app_create(app, app_args->host);
	if (ret != 0) {
		LOG_WRN("Failed to start migrated Radio app: %d", ret);
		radio_app_destroy(app);
		k_free(app);
		return;
	}

	(void)k_sem_take(&app->exit_sem, K_FOREVER);
	radio_app_destroy(app);
	k_free(app);
}

MESHBUS_DESKTOP_APP_DEFINE(MESHBUS_DESKTOP_APP_ID_RADIO,
			   MESHBUS_DESKTOP_APP_NAME_RADIO,
			   radio_app_main,
			   4096,
			   &A_radio_14x14,
			   MESHBUS_DESKTOP_APP_MENU_INDEX_RADIO);
