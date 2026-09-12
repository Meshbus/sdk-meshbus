/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <desktop/desktop.h>
#include <indicator/indicator.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/zui/zui.h>

#define RTTTL_APP_SCREEN_MENU 1U

enum rtttl_item_id {
	RTTTL_ITEM_MISSION = 1U,
	RTTTL_ITEM_YMCA = 2U,
	RTTTL_ITEM_STOP = 3U,
};

struct rtttl_sample {
	uint32_t id;
	const char *name;
	const char *rtttl;
};

struct rtttl_app {
	struct zui_host *host;
	struct zui_router *router;
	struct zui_sublist *menu;
	struct zui_screen *screen;
	struct k_sem exit_sem;
};

static const struct rtttl_sample rtttl_samples[] = {
	{
		.id = RTTTL_ITEM_MISSION,
		.name = "Mission Impossible",
		.rtttl = "Mission Impossible:o=5,d=16,b=100,b=100:32d,32d#,32d,32d#,32d,32d#,32d,32d#,32d,32d,32d#,32e,32f,32f#,32g,g,8p,g,8p,a#,p,c6,p,g,8p,g,8p,f,p,f#,p,g,8p,g,8p,a#,p,c6,p,g,8p,g,8p,f,p,f#,p,a#,g,2d,32p,a#,g,2c#,32p,a#,g,2c,p,a#4,c",
	},
	{
		.id = RTTTL_ITEM_YMCA,
		.name = "YMCA",
		.rtttl = "YMCA:o=5,d=8,b=160,b=160:c#6,a#,2p,a#,g#,f#,g#,a#,4c#6,a#,4c#6,d#6,a#,2p,a#,g#,f#,g#,a#,4c#6,a#,4c#6,d#6,b,2p,b,a#,g#,a#,b,4d#6,f#6,4d#6,4f6.,4d#6.,4c#6.,4b.,4a#,4g#",
	},
};

static const struct zui_list_item rtttl_items[] = {
	{
		.id = RTTTL_ITEM_MISSION,
		.label = "Mission Impossible",
		.detail = "Play melody",
	},
	{
		.id = RTTTL_ITEM_YMCA,
		.label = "YMCA",
		.detail = "Play melody",
	},
	{
		.id = RTTTL_ITEM_STOP,
		.label = "Stop",
		.detail = "Stop buzzer",
	},
};

static const struct rtttl_sample *rtttl_sample_find(uint32_t id)
{
	for (size_t i = 0U; i < ARRAY_SIZE(rtttl_samples); i++) {
		if (rtttl_samples[i].id == id) {
			return &rtttl_samples[i];
		}
	}

	return NULL;
}

static void rtttl_show_result(struct rtttl_app *app, const char *title, int rc)
{
	const char *text = "Error";

	if (app == NULL || app->host == NULL) {
		return;
	}

	if (rc == 0) {
		text = "Playing";
	} else if (rc == -ENODEV) {
		text = "No buzzer";
	} else if (rc == -EACCES) {
		text = "Buzzer disabled";
	}

	(void)zui_toast_show(app->host, &(struct zui_toast_config){
		.title = title,
		.text = text,
		.icon = NULL,
		.timeout_ms = 1200U,
	});
}

static void rtttl_menu_selected(struct zui_sublist *list, uint32_t id, size_t index,
				const struct zui_input_event *event, void *user_data)
{
	struct rtttl_app *app = user_data;
	const struct rtttl_sample *sample;
	int rc = -EINVAL;

	ARG_UNUSED(list);
	ARG_UNUSED(index);
	ARG_UNUSED(event);

	if (app == NULL) {
		return;
	}

	if (id == RTTTL_ITEM_STOP) {
		mbs_indicator_buzzer_stop();
		(void)zui_toast_show(app->host, &(struct zui_toast_config){
			.title = "RTTTL",
			.text = "Stopped",
			.icon = NULL,
			.timeout_ms = 1200U,
		});
		return;
	}

	sample = rtttl_sample_find(id);
	if (sample != NULL) {
		rc = mbs_indicator_buzzer_rtttl(sample->rtttl);
		printk("[rtttl-app] play %s rc=%d\n", sample->name, rc);
		rtttl_show_result(app, sample->name, rc);
	}
}

static void rtttl_menu_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct rtttl_app *app = user_data;

	if (app == NULL || app->menu == NULL) {
		return;
	}

	(void)zui_screen_draw(zui_sublist_get_screen(app->menu), draw);
}

static bool rtttl_menu_input(const struct zui_input_event *event, void *user_data)
{
	struct rtttl_app *app = user_data;

	if (event == NULL || app == NULL) {
		return false;
	}
	if (event->code == ZUI_INPUT_CODE_BACK &&
	    event->action == ZUI_INPUT_ACTION_CLICK) {
		mbs_indicator_buzzer_stop();
		k_sem_give(&app->exit_sem);
		return true;
	}

	if (app->menu != NULL &&
	    zui_screen_submit_input(zui_sublist_get_screen(app->menu), event) >= 0) {
		return true;
	}

	return event->action == ZUI_INPUT_ACTION_PRESS ||
	       event->action == ZUI_INPUT_ACTION_RELEASE;
}

static const struct zui_screen_ops rtttl_menu_ops = {
	.draw = rtttl_menu_draw,
	.input = rtttl_menu_input,
};

void rtttl_app_main(void *args)
{
	struct mbs_desktop_app_args *app_args = args;
	struct rtttl_app app = {0};

	if (app_args == NULL || app_args->host == NULL) {
		return;
	}

	printk("[rtttl-app] start\n");
	app.host = app_args->host;
	k_sem_init(&app.exit_sem, 0, 1);

	app.router = zui_router_create();
	app.menu = zui_sublist_create(&(struct zui_sublist_config){
		.title = "RTTTL",
		.items = rtttl_items,
		.item_count = ARRAY_SIZE(rtttl_items),
		.selected = rtttl_menu_selected,
		.user_data = &app,
	});
	app.screen = zui_screen_create(&rtttl_menu_ops, &app);
	if (app.router == NULL || app.menu == NULL || app.screen == NULL) {
		goto out;
	}

	if (zui_router_register_screen(app.router, RTTTL_APP_SCREEN_MENU, app.screen) != 0 ||
	    zui_router_switch(app.router, RTTTL_APP_SCREEN_MENU) != 0 ||
	    zui_host_attach_router(app.host, ZUI_LAYER_FULLSCREEN, app.router) != 0) {
		goto out;
	}

	(void)zui_host_send_layer_to_front(app.host, ZUI_LAYER_FULLSCREEN);
	(void)zui_host_set_layer_enabled(app.host, ZUI_LAYER_FULLSCREEN, true);
	(void)zui_host_request_redraw(app.host);
	(void)k_sem_take(&app.exit_sem, K_FOREVER);

out:
	mbs_indicator_buzzer_stop();
	if (app.host != NULL) {
		(void)zui_host_detach_router(app.host, ZUI_LAYER_FULLSCREEN);
		(void)zui_host_request_redraw(app.host);
	}
	if (app.router != NULL && app.screen != NULL) {
		(void)zui_router_unregister_screen(app.router, RTTTL_APP_SCREEN_MENU);
	}
	if (app.screen != NULL) {
		zui_screen_destroy(app.screen);
	}
	if (app.menu != NULL) {
		zui_sublist_destroy(app.menu);
	}
	if (app.router != NULL) {
		zui_router_destroy(app.router);
	}
	printk("[rtttl-app] exit\n");
}

LL_EXTENSION_SYMBOL(rtttl_app_main);
