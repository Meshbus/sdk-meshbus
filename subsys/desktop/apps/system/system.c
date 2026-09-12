/* SPDX-License-Identifier: Apache-2.0 */

#include "system_private.h"

LOG_MODULE_REGISTER(mbs_desktop_system_app, CONFIG_MBS_DESKTOP_LOG_LEVEL);

/*
 * Menu, info, and text stay registered. Telemetry contributes its largest
 * dynamic route set: settings, number editor, and reset confirmation.
 */
#define SYSTEM_ROUTER_REQUIRED_SCREENS                                                   \
	(3U + (2U * IS_ENABLED(CONFIG_MBS_DISPLAY)) +                               \
	 (3U * IS_ENABLED(CONFIG_MBS_BLUETOOTH)) +                                  \
	 (2U * IS_ENABLED(CONFIG_MBS_CLOCK)) +                                      \
	 (2U * IS_ENABLED(CONFIG_MBS_POWER)) +                                      \
	 (3U * IS_ENABLED(CONFIG_MBS_TELEMETRY)) +                                  \
	 (2U * IS_ENABLED(CONFIG_MBS_INDICATOR)))

BUILD_ASSERT(CONFIG_ZUI_ROUTER_MAX_SCREENS >= SYSTEM_ROUTER_REQUIRED_SCREENS,
	     "System app router capacity is too small for enabled screens");

static int system_create(struct system_app *app, struct zui_host *host)
{
	int ret;
	size_t menu_count = 0U;

	desktop_app_context_init(&app->ctx, host);
	app->menu_items[menu_count++] = (struct zui_list_item){
		.id = SYSTEM_MENU_INFORMATION,
		.label = DESKTOP_TEXT_SYSTEM_MENU_INFORMATION,
	};
	app->info_menu_items[0] = (struct zui_list_item){
		.id = SYSTEM_INFO_IDENTITY,
		.label = DESKTOP_TEXT_SYSTEM_MENU_IDENTITY,
	};
	app->info_menu_items[1] = (struct zui_list_item){
		.id = SYSTEM_INFO_HARDWARE,
		.label = DESKTOP_TEXT_SYSTEM_MENU_HARDWARE,
	};
	app->info_menu_items[2] = (struct zui_list_item){
		.id = SYSTEM_INFO_FIRMWARE,
		.label = DESKTOP_TEXT_SYSTEM_MENU_FIRMWARE,
	};
	app->info_menu_items[3] = (struct zui_list_item){
		.id = SYSTEM_INFO_RUNTIME,
		.label = DESKTOP_TEXT_SYSTEM_MENU_RUNTIME,
	};
	app->info_menu_items[4] = (struct zui_list_item){
		.id = SYSTEM_INFO_DEVICES,
		.label = DESKTOP_TEXT_SYSTEM_MENU_DEVICES,
	};
#if defined(CONFIG_MBS_DISPLAY)
	app->menu_items[menu_count++] = (struct zui_list_item){
		.id = SYSTEM_MENU_DISPLAY,
		.label = DESKTOP_TEXT_DISPLAY_TITLE,
	};
#endif
#if defined(CONFIG_MBS_BLUETOOTH)
	app->menu_items[menu_count++] = (struct zui_list_item){
		.id = SYSTEM_MENU_BLUETOOTH,
		.label = DESKTOP_TEXT_BLUETOOTH_TITLE,
	};
#endif
#if defined(CONFIG_MBS_CLOCK)
	app->menu_items[menu_count++] = (struct zui_list_item){
		.id = SYSTEM_MENU_CLOCK,
		.label = DESKTOP_TEXT_CLOCK_TITLE,
	};
#endif
#if defined(CONFIG_MBS_POWER)
	app->menu_items[menu_count++] = (struct zui_list_item){
		.id = SYSTEM_MENU_POWER,
		.label = DESKTOP_TEXT_POWER_TITLE,
	};
#endif
#if defined(CONFIG_MBS_TELEMETRY)
	app->menu_items[menu_count++] = (struct zui_list_item){
		.id = SYSTEM_MENU_TELEMETRY,
		.label = DESKTOP_TEXT_TELEMETRY_TITLE,
	};
#endif
#if defined(CONFIG_MBS_INDICATOR)
	app->menu_items[menu_count++] = (struct zui_list_item){
		.id = SYSTEM_MENU_INDICATOR,
		.label = DESKTOP_TEXT_SYSTEM_MENU_INDICATOR,
	};
#endif

	app->ctx.router = zui_router_create();
	app->menu = zui_sublist_create(&(struct zui_sublist_config){
		.title = DESKTOP_TEXT_SYSTEM_TITLE,
		.items = app->menu_items,
		.item_count = menu_count,
		.selected = system_menu_selected,
		.user_data = app,
	});
	app->info_menu = zui_sublist_create(&(struct zui_sublist_config){
		.title = DESKTOP_TEXT_SYSTEM_MENU_INFORMATION,
		.items = app->info_menu_items,
		.item_count = ARRAY_SIZE(app->info_menu_items),
		.selected = system_info_menu_selected,
		.user_data = app,
	});
	app->text_view = zui_text_view_create(&(struct zui_text_view_config){
		.title = DESKTOP_TEXT_SYSTEM_TITLE,
		.text = app->text,
		.font = ZUI_FONT_SECONDARY,
		.mode = ZUI_TEXT_VIEW_MODE_TEXT,
	});
#if defined(CONFIG_MBS_DISPLAY)
	app->display_form = zui_form_create(&(struct zui_form_config){
		.title = DESKTOP_TEXT_DISPLAY_TITLE,
		.items = app->display_form_items,
		.item_count = 0U,
		.changed = system_display_form_changed,
		.activated = system_display_form_activated,
		.user_data = app,
	});
	app->display_reset_modal = zui_modal_create(&(struct zui_modal_config){
		.title = DESKTOP_TEXT_COMMON_RESET,
		.text = DESKTOP_TEXT_COMMON_RESET_CONFIRM_TEXT,
		.left_button = DESKTOP_TEXT_COMMON_CANCEL,
		.right_button = DESKTOP_TEXT_COMMON_OK,
		.result = system_display_reset_modal_result,
		.user_data = app,
	});
#endif
#if defined(CONFIG_MBS_BLUETOOTH)
	app->bluetooth_form = zui_form_create(&(struct zui_form_config){
		.title = DESKTOP_TEXT_BLUETOOTH_TITLE,
		.items = app->bluetooth_form_items,
		.item_count = 0U,
		.changed = system_bluetooth_form_changed,
		.activated = system_bluetooth_form_activated,
		.user_data = app,
	});
	app->bluetooth_number_editor =
		zui_number_editor_create(&(struct zui_number_editor_config){
			.title = DESKTOP_TEXT_BLUETOOTH_SETTINGS_FIXED_PASSKEY,
			.value = 0,
			.min_value = SYSTEM_BLUETOOTH_FIXED_PASSKEY_MIN,
			.max_value = SYSTEM_BLUETOOTH_FIXED_PASSKEY_MAX,
			.max_digits = SYSTEM_BLUETOOTH_FIXED_PASSKEY_MAX_DIGITS,
			.unsigned_only = true,
			.keep_leading_zeros = true,
		});
	app->bluetooth_reset_modal = zui_modal_create(&(struct zui_modal_config){
		.title = DESKTOP_TEXT_COMMON_RESET,
		.text = DESKTOP_TEXT_COMMON_RESET_CONFIRM_TEXT,
		.left_button = DESKTOP_TEXT_COMMON_CANCEL,
		.right_button = DESKTOP_TEXT_COMMON_OK,
		.result = system_bluetooth_reset_modal_result,
		.user_data = app,
	});
#endif
#if defined(CONFIG_MBS_CLOCK)
	app->clock_form = zui_form_create(&(struct zui_form_config){
		.title = DESKTOP_TEXT_CLOCK_TITLE,
		.items = app->clock_form_items,
		.item_count = 0U,
		.changed = system_clock_form_changed,
		.activated = system_clock_form_activated,
		.user_data = app,
	});
	app->clock_reset_modal = zui_modal_create(&(struct zui_modal_config){
		.title = DESKTOP_TEXT_COMMON_RESET,
		.text = DESKTOP_TEXT_COMMON_RESET_CONFIRM_TEXT,
		.left_button = DESKTOP_TEXT_COMMON_CANCEL,
		.right_button = DESKTOP_TEXT_COMMON_OK,
		.result = system_clock_reset_modal_result,
		.user_data = app,
	});
#endif
#if defined(CONFIG_MBS_POWER)
	app->power_form = zui_form_create(&(struct zui_form_config){
		.title = DESKTOP_TEXT_POWER_TITLE,
		.items = app->power_form_items,
		.item_count = 0U,
		.changed = system_power_form_changed,
		.activated = system_power_form_activated,
		.user_data = app,
	});
	app->power_reset_modal = zui_modal_create(&(struct zui_modal_config){
		.title = DESKTOP_TEXT_COMMON_RESET,
		.text = DESKTOP_TEXT_COMMON_RESET_CONFIRM_TEXT,
		.left_button = DESKTOP_TEXT_COMMON_CANCEL,
		.right_button = DESKTOP_TEXT_COMMON_OK,
		.result = system_power_reset_modal_result,
		.user_data = app,
	});
#endif
#if defined(CONFIG_MBS_TELEMETRY)
	app->telemetry_menu_items[0] = (struct zui_list_item){
		.id = SYSTEM_TELEMETRY_MENU_READINGS,
		.label = DESKTOP_TEXT_TELEMETRY_READINGS,
	};
	app->telemetry_menu_items[1] = (struct zui_list_item){
		.id = SYSTEM_TELEMETRY_MENU_TRIGGER,
		.label = DESKTOP_TEXT_TELEMETRY_TRIGGER_SAMPLE,
	};
	app->telemetry_menu_items[2] = (struct zui_list_item){
		.id = SYSTEM_TELEMETRY_MENU_SETTINGS,
		.label = DESKTOP_TEXT_COMMON_MENU_SETTINGS,
	};
#endif
#if defined(CONFIG_MBS_INDICATOR)
	app->indicator_form = zui_form_create(&(struct zui_form_config){
		.title = DESKTOP_TEXT_INDICATOR_TITLE,
		.items = app->indicator_form_items,
		.item_count = 0U,
		.changed = system_indicator_form_changed,
		.activated = system_indicator_form_activated,
		.user_data = app,
	});
	app->indicator_reset_modal = zui_modal_create(&(struct zui_modal_config){
		.title = DESKTOP_TEXT_COMMON_RESET,
		.text = DESKTOP_TEXT_COMMON_RESET_CONFIRM_TEXT,
		.left_button = DESKTOP_TEXT_COMMON_CANCEL,
		.right_button = DESKTOP_TEXT_COMMON_OK,
		.result = system_indicator_reset_modal_result,
		.user_data = app,
	});
#endif
	app->menu_screen = zui_screen_create(&system_menu_ops, app);
	app->info_menu_screen = zui_screen_create(&system_info_menu_ops, app);
	app->text_screen = zui_screen_create(&system_text_ops, app);
#if defined(CONFIG_MBS_DISPLAY)
	app->display_form_screen = zui_screen_create(&system_display_form_ops, app);
	app->display_reset_screen = zui_screen_create(&system_display_reset_ops, app);
#endif
#if defined(CONFIG_MBS_BLUETOOTH)
	app->bluetooth_form_screen = zui_screen_create(&system_bluetooth_form_ops, app);
	app->bluetooth_number_screen = zui_screen_create(&system_bluetooth_number_ops, app);
	app->bluetooth_reset_screen = zui_screen_create(&system_bluetooth_reset_ops, app);
#endif
#if defined(CONFIG_MBS_CLOCK)
	app->clock_form_screen = zui_screen_create(&system_clock_form_ops, app);
	app->clock_reset_screen = zui_screen_create(&system_clock_reset_ops, app);
#endif
#if defined(CONFIG_MBS_POWER)
	app->power_form_screen = zui_screen_create(&system_power_form_ops, app);
	app->power_reset_screen = zui_screen_create(&system_power_reset_ops, app);
#endif
#if defined(CONFIG_MBS_INDICATOR)
	app->indicator_form_screen = zui_screen_create(&system_indicator_form_ops, app);
	app->indicator_reset_screen = zui_screen_create(&system_indicator_reset_ops, app);
#endif
	if (app->ctx.router == NULL || app->menu == NULL || app->info_menu == NULL ||
	    app->text_view == NULL || app->menu_screen == NULL ||
	    app->info_menu_screen == NULL || app->text_screen == NULL
#if defined(CONFIG_MBS_DISPLAY)
	    || app->display_form == NULL || app->display_reset_modal == NULL ||
	    app->display_form_screen == NULL || app->display_reset_screen == NULL
#endif
#if defined(CONFIG_MBS_BLUETOOTH)
	    || app->bluetooth_form == NULL || app->bluetooth_number_editor == NULL ||
	    app->bluetooth_reset_modal == NULL || app->bluetooth_form_screen == NULL ||
	    app->bluetooth_number_screen == NULL || app->bluetooth_reset_screen == NULL
#endif
#if defined(CONFIG_MBS_CLOCK)
	    || app->clock_form == NULL || app->clock_reset_modal == NULL ||
	    app->clock_form_screen == NULL || app->clock_reset_screen == NULL
#endif
#if defined(CONFIG_MBS_POWER)
	    || app->power_form == NULL || app->power_reset_modal == NULL ||
	    app->power_form_screen == NULL || app->power_reset_screen == NULL
#endif
#if defined(CONFIG_MBS_INDICATOR)
	    || app->indicator_form == NULL || app->indicator_reset_modal == NULL ||
	    app->indicator_form_screen == NULL || app->indicator_reset_screen == NULL
#endif
	) {
		return -ENOMEM;
	}

	ret = zui_router_register_screen(app->ctx.router, SYSTEM_SCREEN_MENU, app->menu_screen);
	if (ret != 0) {
		return ret;
	}
	ret = zui_router_register_screen(app->ctx.router, SYSTEM_SCREEN_INFO_MENU,
					 app->info_menu_screen);
	if (ret != 0) {
		return ret;
	}
	ret = zui_router_register_screen(app->ctx.router, SYSTEM_SCREEN_TEXT, app->text_screen);
	if (ret != 0) {
		return ret;
	}
#if defined(CONFIG_MBS_DISPLAY)
	ret = zui_router_register_screen(app->ctx.router, SYSTEM_SCREEN_DISPLAY_FORM,
					 app->display_form_screen);
	if (ret != 0) {
		return ret;
	}
	ret = zui_router_register_screen(app->ctx.router, SYSTEM_SCREEN_DISPLAY_RESET,
					 app->display_reset_screen);
	if (ret != 0) {
		return ret;
	}
#endif
#if defined(CONFIG_MBS_BLUETOOTH)
	ret = zui_router_register_screen(app->ctx.router, SYSTEM_SCREEN_BLUETOOTH_FORM,
					 app->bluetooth_form_screen);
	if (ret != 0) {
		return ret;
	}
	ret = zui_router_register_screen(app->ctx.router, SYSTEM_SCREEN_BLUETOOTH_NUMBER,
					 app->bluetooth_number_screen);
	if (ret != 0) {
		return ret;
	}
	ret = zui_router_register_screen(app->ctx.router, SYSTEM_SCREEN_BLUETOOTH_RESET,
					 app->bluetooth_reset_screen);
	if (ret != 0) {
		return ret;
	}
#endif
#if defined(CONFIG_MBS_CLOCK)
	ret = zui_router_register_screen(app->ctx.router, SYSTEM_SCREEN_CLOCK_FORM,
					 app->clock_form_screen);
	if (ret != 0) {
		return ret;
	}
	ret = zui_router_register_screen(app->ctx.router, SYSTEM_SCREEN_CLOCK_RESET,
					 app->clock_reset_screen);
	if (ret != 0) {
		return ret;
	}
#endif
#if defined(CONFIG_MBS_POWER)
	ret = zui_router_register_screen(app->ctx.router, SYSTEM_SCREEN_POWER_FORM,
					 app->power_form_screen);
	if (ret != 0) {
		return ret;
	}
	ret = zui_router_register_screen(app->ctx.router, SYSTEM_SCREEN_POWER_RESET,
					 app->power_reset_screen);
	if (ret != 0) {
		return ret;
	}
#endif
#if defined(CONFIG_MBS_INDICATOR)
	ret = zui_router_register_screen(app->ctx.router, SYSTEM_SCREEN_INDICATOR_FORM,
					 app->indicator_form_screen);
	if (ret != 0) {
		return ret;
	}
	ret = zui_router_register_screen(app->ctx.router, SYSTEM_SCREEN_INDICATOR_RESET,
					 app->indicator_reset_screen);
	if (ret != 0) {
		return ret;
	}
#endif
	ret = zui_host_attach_router(host, ZUI_LAYER_FULLSCREEN, app->ctx.router);
	if (ret != 0) {
		return ret;
	}

	system_open_menu(app);
	return 0;
}

static void system_destroy(struct system_app *app)
{
	if (app == NULL) {
		return;
	}

	if (app->ctx.router != NULL) {
		(void)zui_router_unregister_screen(app->ctx.router, SYSTEM_SCREEN_MENU);
		(void)zui_router_unregister_screen(app->ctx.router, SYSTEM_SCREEN_INFO_MENU);
		(void)zui_router_unregister_screen(app->ctx.router, SYSTEM_SCREEN_TEXT);
#if defined(CONFIG_MBS_DISPLAY)
		(void)zui_router_unregister_screen(app->ctx.router, SYSTEM_SCREEN_DISPLAY_FORM);
		(void)zui_router_unregister_screen(app->ctx.router, SYSTEM_SCREEN_DISPLAY_RESET);
#endif
#if defined(CONFIG_MBS_BLUETOOTH)
		(void)zui_router_unregister_screen(app->ctx.router, SYSTEM_SCREEN_BLUETOOTH_FORM);
		(void)zui_router_unregister_screen(app->ctx.router,
						   SYSTEM_SCREEN_BLUETOOTH_NUMBER);
		(void)zui_router_unregister_screen(app->ctx.router,
						   SYSTEM_SCREEN_BLUETOOTH_RESET);
#endif
#if defined(CONFIG_MBS_CLOCK)
		(void)zui_router_unregister_screen(app->ctx.router, SYSTEM_SCREEN_CLOCK_FORM);
		(void)zui_router_unregister_screen(app->ctx.router, SYSTEM_SCREEN_CLOCK_RESET);
#endif
#if defined(CONFIG_MBS_POWER)
		(void)zui_router_unregister_screen(app->ctx.router, SYSTEM_SCREEN_POWER_FORM);
		(void)zui_router_unregister_screen(app->ctx.router, SYSTEM_SCREEN_POWER_RESET);
#endif
#if defined(CONFIG_MBS_TELEMETRY)
		(void)zui_router_unregister_screen(app->ctx.router,
						   SYSTEM_SCREEN_TELEMETRY_READINGS);
		(void)zui_router_unregister_screen(app->ctx.router,
						   SYSTEM_SCREEN_TELEMETRY_SETTINGS);
		(void)zui_router_unregister_screen(app->ctx.router,
						   SYSTEM_SCREEN_TELEMETRY_NUMBER);
		(void)zui_router_unregister_screen(app->ctx.router,
						   SYSTEM_SCREEN_TELEMETRY_RESET);
#endif
#if defined(CONFIG_MBS_INDICATOR)
		(void)zui_router_unregister_screen(app->ctx.router, SYSTEM_SCREEN_INDICATOR_FORM);
		(void)zui_router_unregister_screen(app->ctx.router, SYSTEM_SCREEN_INDICATOR_RESET);
#endif
	}
	if (app->menu_screen != NULL) {
		zui_screen_destroy(app->menu_screen);
	}
	if (app->info_menu_screen != NULL) {
		zui_screen_destroy(app->info_menu_screen);
	}
	if (app->text_screen != NULL) {
		zui_screen_destroy(app->text_screen);
	}
#if defined(CONFIG_MBS_DISPLAY)
	if (app->display_form_screen != NULL) {
		zui_screen_destroy(app->display_form_screen);
	}
	if (app->display_reset_screen != NULL) {
		zui_screen_destroy(app->display_reset_screen);
	}
	if (app->display_reset_modal != NULL) {
		zui_modal_destroy(app->display_reset_modal);
	}
	if (app->display_form != NULL) {
		zui_form_destroy(app->display_form);
	}
#endif
#if defined(CONFIG_MBS_BLUETOOTH)
	if (app->bluetooth_form_screen != NULL) {
		zui_screen_destroy(app->bluetooth_form_screen);
	}
	if (app->bluetooth_number_screen != NULL) {
		zui_screen_destroy(app->bluetooth_number_screen);
	}
	if (app->bluetooth_reset_screen != NULL) {
		zui_screen_destroy(app->bluetooth_reset_screen);
	}
	if (app->bluetooth_reset_modal != NULL) {
		zui_modal_destroy(app->bluetooth_reset_modal);
	}
	if (app->bluetooth_number_editor != NULL) {
		zui_number_editor_destroy(app->bluetooth_number_editor);
	}
	if (app->bluetooth_form != NULL) {
		zui_form_destroy(app->bluetooth_form);
	}
#endif
#if defined(CONFIG_MBS_CLOCK)
	if (app->clock_form_screen != NULL) {
		zui_screen_destroy(app->clock_form_screen);
	}
	if (app->clock_reset_screen != NULL) {
		zui_screen_destroy(app->clock_reset_screen);
	}
	if (app->clock_reset_modal != NULL) {
		zui_modal_destroy(app->clock_reset_modal);
	}
	if (app->clock_form != NULL) {
		zui_form_destroy(app->clock_form);
	}
#endif
#if defined(CONFIG_MBS_POWER)
	if (app->power_form_screen != NULL) {
		zui_screen_destroy(app->power_form_screen);
	}
	if (app->power_reset_screen != NULL) {
		zui_screen_destroy(app->power_reset_screen);
	}
	if (app->power_reset_modal != NULL) {
		zui_modal_destroy(app->power_reset_modal);
	}
	if (app->power_form != NULL) {
		zui_form_destroy(app->power_form);
	}
#endif
#if defined(CONFIG_MBS_TELEMETRY)
	if (app->telemetry_readings_screen != NULL) {
		zui_screen_destroy(app->telemetry_readings_screen);
	}
	if (app->telemetry_settings_screen != NULL) {
		zui_screen_destroy(app->telemetry_settings_screen);
	}
	if (app->telemetry_number_screen != NULL) {
		zui_screen_destroy(app->telemetry_number_screen);
	}
	if (app->telemetry_reset_screen != NULL) {
		zui_screen_destroy(app->telemetry_reset_screen);
	}
	if (app->telemetry_reset_modal != NULL) {
		zui_modal_destroy(app->telemetry_reset_modal);
	}
	if (app->telemetry_number_editor != NULL) {
		zui_number_editor_destroy(app->telemetry_number_editor);
	}
	if (app->telemetry_settings_form != NULL) {
		zui_form_destroy(app->telemetry_settings_form);
	}
	if (app->telemetry_readings != NULL) {
		zui_sublist_destroy(app->telemetry_readings);
	}
#endif
#if defined(CONFIG_MBS_INDICATOR)
	if (app->indicator_form_screen != NULL) {
		zui_screen_destroy(app->indicator_form_screen);
	}
	if (app->indicator_reset_screen != NULL) {
		zui_screen_destroy(app->indicator_reset_screen);
	}
	if (app->indicator_reset_modal != NULL) {
		zui_modal_destroy(app->indicator_reset_modal);
	}
	if (app->indicator_form != NULL) {
		zui_form_destroy(app->indicator_form);
	}
#endif
	if (app->text_view != NULL) {
		zui_text_view_destroy(app->text_view);
	}
	if (app->menu != NULL) {
		zui_sublist_destroy(app->menu);
	}
	if (app->info_menu != NULL) {
		zui_sublist_destroy(app->info_menu);
	}
	desktop_app_context_cleanup(&app->ctx);
}

static void system_main(void *args)
{
	struct mbs_desktop_app_args *app_args = args;
	struct system_app *app;
	int ret;

	if (app_args == NULL || app_args->host == NULL) {
		return;
	}

	app = k_calloc(1U, sizeof(*app));
	if (app == NULL) {
		return;
	}

#if defined(CONFIG_MBS_DISPLAY)
	system_display_prepare_options();
#endif
#if defined(CONFIG_MBS_CLOCK)
	system_clock_prepare_options();
#endif
#if defined(CONFIG_MBS_POWER)
	system_power_prepare_options();
#endif
	ret = system_create(app, app_args->host);
	if (ret != 0) {
		LOG_WRN("Failed to start system app: %d", ret);
		system_destroy(app);
		k_free(app);
		return;
	}

	desktop_app_wait(&app->ctx);
	system_destroy(app);
	k_free(app);
}

MBS_DESKTOP_APP_DEFINE(MBS_DESKTOP_APP_ID_SYSTEM,
			   MBS_DESKTOP_APP_NAME_SYSTEM,
			   system_main,
			   2048,
			   &A_system_14x14,
			   MBS_DESKTOP_APP_MENU_INDEX_SYSTEM);
