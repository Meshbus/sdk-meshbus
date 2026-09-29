/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "gnss_private.h"

#include <zui/zui.h>

#include "desktop_private.h"
#include "text/desktop_text.h"

void gnss_request_redraw(struct gnss_app *app)
{
	if (app != NULL && app->host != NULL) {
		zui_desktop_request_redraw(zui_host_get_user_data(app->host));
	}
}

void gnss_switch(struct gnss_app *app, uint32_t screen_id)
{
	if (app == NULL || app->router == NULL) {
		return;
	}

	if (zui_router_switch(app->router, screen_id) == 0) {
		gnss_request_redraw(app);
	}
}

void gnss_exit(struct gnss_app *app)
{
	if (app == NULL) {
		return;
	}

	(void)zui_host_detach_router(app->host, ZUI_LAYER_FULLSCREEN);
	k_sem_give(&app->exit_sem);
	gnss_request_redraw(app);
}

void gnss_toast(struct gnss_app *app, const char *text, const struct zui_icon *icon,
		       uint32_t timeout_ms)
{
	if (app == NULL) {
		return;
	}

	(void)zui_toast_show(app->host, &(struct zui_toast_config){
		.title = DESKTOP_TEXT_GNSS_TITLE,
		.text = text,
		.icon = icon,
		.timeout_ms = timeout_ms,
	});
}
bool gnss_back_to_menu_input(const struct zui_input_event *event, struct gnss_app *app,
				    struct zui_screen *screen)
{
	int ret;

	if (desktop_app_input_should_consume_edge(event)) {
		return true;
	}
	if (desktop_app_input_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		gnss_switch(app, GNSS_APP_SCREEN_MENU);
		return true;
	}
	if (desktop_app_input_is_click(event) && event->code == ZUI_INPUT_CODE_SELECT && screen ==
	    zui_sublist_get_screen(app->satellites)) {
		if (zui_sublist_activate(app->satellites, event) == 0) {
			gnss_request_redraw(app);
		}
		return true;
	}

	ret = zui_screen_submit_input(screen, event);
	if (ret > 0) {
		gnss_request_redraw(app);
		return true;
	}

	return false;
}
