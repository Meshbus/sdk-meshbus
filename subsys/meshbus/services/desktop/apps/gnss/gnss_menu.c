/* SPDX-License-Identifier: Apache-2.0 */

#include "gnss_private.h"

#include <errno.h>

#include <zephyr/sys/util.h>

#include "assets/assets_icons.h"
#include "text/desktop_text.h"

void gnss_menu_selected(struct zui_sublist *list, uint32_t id, size_t index,
			       const struct zui_input_event *event, void *user_data)
{
	struct gnss_app *app = user_data;
	int rc;

	ARG_UNUSED(list);
	ARG_UNUSED(index);
	ARG_UNUSED(event);

	switch (id) {
	case GNSS_MENU_ITEM_STATUS:
		gnss_open_status(app);
		break;
	case GNSS_MENU_ITEM_SATELLITES:
		gnss_open_satellites(app);
		break;
	case GNSS_MENU_ITEM_ACQUISITION:
		rc = meshbus_gnss_acquisition();
		if (rc == 0) {
			gnss_toast(app, DESKTOP_TEXT_GNSS_ACQUISITION_NOW, &I_done_24x24, 900U);
		} else if (rc == -EBUSY) {
			gnss_toast(app, DESKTOP_TEXT_GNSS_ACQUISITION_PROCESSING, &I_done_24x24,
				   1200U);
		} else {
			gnss_toast(app, DESKTOP_TEXT_GNSS_FAILED, &I_error_24x24, 1500U);
		}
		break;
	case GNSS_MENU_ITEM_SETTINGS:
		gnss_open_settings(app);
		break;
	default:
		break;
	}
}
static void gnss_menu_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct gnss_app *app = user_data;

	(void)zui_screen_draw(zui_sublist_get_screen(app->menu), draw);
}

static bool gnss_menu_input(const struct zui_input_event *event, void *user_data)
{
	struct gnss_app *app = user_data;
	int ret;

	if (app == NULL || event == NULL) {
		return false;
	}
	if (gnss_should_consume_edge(event)) {
		return true;
	}
	if (gnss_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		gnss_exit(app);
		return true;
	}
	if (gnss_is_click(event) && event->code == ZUI_INPUT_CODE_SELECT) {
		if (zui_sublist_activate(app->menu, event) == 0) {
			gnss_request_redraw(app);
		}
		return true;
	}
	ret = zui_screen_submit_input(zui_sublist_get_screen(app->menu), event);
	if (ret > 0) {
		gnss_request_redraw(app);
		return true;
	}

	return false;
}
const struct zui_screen_ops gnss_menu_ops = {
	.draw = gnss_menu_draw,
	.input = gnss_menu_input,
};
