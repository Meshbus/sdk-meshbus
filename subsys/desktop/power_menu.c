/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "desktop_private.h"

#include "assets/assets_icons.h"
#include "text/desktop_text.h"

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#if defined(CONFIG_MBS_DISPLAY)
#include <display/display.h>
#endif
#if defined(CONFIG_MBS_POWER)
#include <power/power.h>
#endif
#include <zui/zui.h>

LOG_MODULE_DECLARE(mbs_desktop, CONFIG_MBS_DESKTOP_LOG_LEVEL);

enum desktop_power_item {
	DESKTOP_POWER_ITEM_SLEEP = 0,
	DESKTOP_POWER_ITEM_SHUTDOWN,
	DESKTOP_POWER_ITEM_REBOOT,
	DESKTOP_POWER_ITEM_COUNT,
};

#define MBS_DESKTOP_POWER_ACTION_DELAY_MS	  350U
#define MBS_DESKTOP_POWER_RELEASE_TIMEOUT_MS 2000U
#define MBS_DESKTOP_POWER_TOAST_TIMEOUT_MS   1200U

void desktop_power_menu_deinit(struct zui_desktop *desktop);

static bool desktop_power_deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
	return (int32_t)(now_ms - deadline_ms) >= 0;
}

static const char *desktop_power_action_text(enum desktop_power_item item)
{
	switch (item) {
	case DESKTOP_POWER_ITEM_SLEEP:
		return DESKTOP_TEXT_VIEW_POWER_SLEEP_SELECTED;
	case DESKTOP_POWER_ITEM_SHUTDOWN:
		return DESKTOP_TEXT_VIEW_POWER_SHUTDOWN_SELECTED;
	case DESKTOP_POWER_ITEM_REBOOT:
		return DESKTOP_TEXT_VIEW_POWER_REBOOT_SELECTED;
	default:
		return DESKTOP_TEXT_VIEW_POWER_INVALID;
	}
}

static const struct zui_icon *desktop_power_action_toast_icon(enum desktop_power_item item)
{
	switch (item) {
	case DESKTOP_POWER_ITEM_SLEEP:
		return &I_done_24x24;
	case DESKTOP_POWER_ITEM_SHUTDOWN:
	case DESKTOP_POWER_ITEM_REBOOT:
		return &I_loading_24x24;
	default:
		return &I_error_24x24;
	}
}

static void desktop_power_show_toast(struct zui_desktop *desktop, const char *text,
				     const struct zui_icon *icon)
{
	if (desktop == NULL || desktop->host == NULL) {
		return;
	}

	(void)zui_toast_show(desktop->host, &(struct zui_toast_config){
		.title = DESKTOP_TEXT_POWER_TITLE,
		.text = text,
		.icon = icon,
		.timeout_ms = MBS_DESKTOP_POWER_TOAST_TIMEOUT_MS,
	});
}

static int desktop_power_execute(enum desktop_power_item item)
{
	switch (item) {
	case DESKTOP_POWER_ITEM_SLEEP:
#if defined(CONFIG_MBS_DISPLAY)
		mbs_display_active(false);
		return 0;
#else
		return -ENOTSUP;
#endif
	case DESKTOP_POWER_ITEM_SHUTDOWN:
#if defined(CONFIG_MBS_POWER)
		return mbs_power_shutdown();
#else
		return -ENOTSUP;
#endif
	case DESKTOP_POWER_ITEM_REBOOT:
#if defined(CONFIG_MBS_POWER)
		return mbs_power_reboot();
#else
		return -ENOTSUP;
#endif
	default:
		return -EINVAL;
	}
}

void desktop_power_menu_poll(struct zui_desktop *desktop)
{
	enum desktop_power_item item;
	uint32_t pressed_mask;
	uint32_t now_ms;
	int rc;

	if (desktop == NULL || !desktop->power_action_pending) {
		return;
	}

	now_ms = k_uptime_get_32();
	if (!desktop_power_deadline_reached(now_ms, desktop->power_action_deadline_ms)) {
		return;
	}

	pressed_mask = desktop_input_pressed_mask_get();
	if (pressed_mask != 0U &&
	    !desktop_power_deadline_reached(now_ms, desktop->power_action_release_deadline_ms)) {
		return;
	}
	if (pressed_mask != 0U) {
		LOG_WRN("Power action proceeding with pressed input mask 0x%08x", pressed_mask);
	}

	desktop->power_action_pending = false;
	item = (enum desktop_power_item)desktop->power_action_item;
	rc = desktop_power_execute(item);
	LOG_INF("Power menu action %u: %s (%d)", (unsigned int)item,
		desktop_power_action_text(item), rc);
	if (rc == 0) {
		return;
	}

	desktop_power_show_toast(desktop, DESKTOP_TEXT_VIEW_POWER_INVALID, &I_error_24x24);
}

static void desktop_power_selected(struct zui_actions *actions, uint32_t id,
				   const struct zui_input_event *event, void *user_data)
{
	struct zui_desktop *desktop = user_data;
	enum desktop_power_item item = (enum desktop_power_item)id;
	int rc;

	ARG_UNUSED(actions);
	ARG_UNUSED(event);

	if (desktop == NULL) {
		return;
	}

	if (item == DESKTOP_POWER_ITEM_SLEEP) {
		(void)zui_desktop_switch(desktop, MBS_DESKTOP_VIEW_DASHBOARD);
		rc = desktop_power_execute(item);
		LOG_INF("Power menu action %u: %s (%d)", id, desktop_power_action_text(item), rc);
		if (rc == 0) {
			return;
		}
	} else {
		uint32_t now_ms = k_uptime_get_32();

		desktop->power_action_item = (uint8_t)item;
		desktop->power_action_pending = true;
		desktop->power_action_deadline_ms = now_ms + MBS_DESKTOP_POWER_ACTION_DELAY_MS;
		desktop->power_action_release_deadline_ms =
			now_ms + MBS_DESKTOP_POWER_RELEASE_TIMEOUT_MS;
		desktop_power_show_toast(desktop, desktop_power_action_text(item),
					 desktop_power_action_toast_icon(item));
		zui_desktop_request_redraw(desktop);
		return;
	}

	desktop_power_show_toast(desktop, DESKTOP_TEXT_VIEW_POWER_INVALID, &I_error_24x24);
}

static void desktop_power_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct zui_desktop *desktop = user_data;

	(void)zui_screen_draw(zui_actions_get_screen(desktop->power_menu), draw);
}

static bool desktop_power_input(const struct zui_input_event *event, void *user_data)
{
	struct zui_desktop *desktop = user_data;
	int ret;

	if (desktop == NULL || event == NULL) {
		return false;
	}
	if (desktop_shell_should_consume_edge_event(event)) {
		return true;
	}
	if (desktop_input_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		(void)zui_desktop_switch(desktop, MBS_DESKTOP_VIEW_DASHBOARD);
		return true;
	}

	ret = zui_screen_submit_input(zui_actions_get_screen(desktop->power_menu), event);
	if (ret > 0) {
		zui_desktop_request_redraw(desktop);
		return true;
	}

	return false;
}

static void desktop_power_enter(void *user_data)
{
	struct zui_desktop *desktop = user_data;

	LOG_DBG("Desktop POWER_MENU enter");
	(void)zui_actions_select(desktop->power_menu, DESKTOP_POWER_ITEM_SLEEP);
	(void)zui_screen_enter(zui_actions_get_screen(desktop->power_menu));
	zui_desktop_request_redraw(desktop);
}

static void desktop_power_exit(void *user_data)
{
	struct zui_desktop *desktop = user_data;

	(void)zui_screen_exit(zui_actions_get_screen(desktop->power_menu));
}

static const struct zui_screen_ops desktop_power_ops = {
	.draw = desktop_power_draw,
	.input = desktop_power_input,
	.enter = desktop_power_enter,
	.exit = desktop_power_exit,
};

int desktop_power_menu_init(struct zui_desktop *desktop)
{
	static const struct zui_action_item power_items[] = {
		{.id = DESKTOP_POWER_ITEM_SLEEP,
		 .label = DESKTOP_TEXT_VIEW_POWER_SLEEP,
		 .icon = &I_sleep_24x24},
		{.id = DESKTOP_POWER_ITEM_SHUTDOWN,
		 .label = DESKTOP_TEXT_VIEW_POWER_SHUTDOWN,
		 .icon = &I_shutdown_24x24},
		{.id = DESKTOP_POWER_ITEM_REBOOT,
		 .label = DESKTOP_TEXT_VIEW_POWER_REBOOT,
		 .icon = &I_reboot_24x24},
	};
	int ret;

	if (desktop == NULL) {
		return -EINVAL;
	}

	desktop->power_action_pending = false;

	desktop->power_menu = zui_actions_create(&(struct zui_actions_config){
		.title = DESKTOP_TEXT_POWER_TITLE,
		.items = power_items,
		.item_count = ARRAY_SIZE(power_items),
		.selected = desktop_power_selected,
		.user_data = desktop,
	});
	if (desktop->power_menu == NULL) {
		desktop_power_menu_deinit(desktop);
		return -ENOMEM;
	}

	desktop->power_menu_screen = zui_screen_create(&desktop_power_ops, desktop);
	if (desktop->power_menu_screen == NULL) {
		desktop_power_menu_deinit(desktop);
		return -ENOMEM;
	}

	ret = zui_desktop_register_screen(desktop, MBS_DESKTOP_VIEW_POWER_MENU,
					   desktop->power_menu_screen);
	if (ret != 0) {
		desktop_power_menu_deinit(desktop);
		return ret;
	}

	return ret;
}

void desktop_power_menu_deinit(struct zui_desktop *desktop)
{
	if (desktop == NULL) {
		return;
	}

	desktop->power_action_pending = false;
	if (desktop->router != NULL) {
		(void)zui_router_unregister_screen(desktop->router, MBS_DESKTOP_VIEW_POWER_MENU);
	}
	if (desktop->power_menu_screen != NULL) {
		zui_screen_destroy(desktop->power_menu_screen);
		desktop->power_menu_screen = NULL;
	}
	if (desktop->power_menu != NULL) {
		zui_actions_destroy(desktop->power_menu);
		desktop->power_menu = NULL;
	}
}
