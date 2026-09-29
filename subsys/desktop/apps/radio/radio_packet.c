/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "radio_private.h"

#include <string.h>

#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#include "assets/assets_icons.h"
#include "text/desktop_text.h"

static size_t radio_text_payload_len(const char *payload)
{
	return payload == NULL ? 0U : strnlen(payload, RADIO_SEND_TEXT_MAX_LEN);
}

static void radio_send_payload(struct radio_app *app, const uint8_t *data, size_t len)
{
	uint32_t timeout_ms;
	uint32_t airtime_ms;
	int rc;

	if (app == NULL || data == NULL || len == 0U || len > MBS_RADIO_MAX_PAYLOAD) {
		radio_toast(app, DESKTOP_TEXT_RADIO_TITLE, DESKTOP_TEXT_RADIO_REJECTED,
			    &I_error_24x24, 1500U);
		return;
	}

	memset(&app->publish, 0, sizeof(app->publish));
	memcpy(app->publish.data, data, len);
	app->publish.len = (uint16_t)len;
	airtime_ms = mbs_radio_airtime((uint16_t)len);
	rc = zbus_chan_pub(&mbs_radio_publish_chan, &app->publish, K_NO_WAIT);
	if (rc == 0) {
		(void)snprintk(app->toast_text, sizeof(app->toast_text),
			       DESKTOP_TEXT_RADIO_FORMAT_LEN_TOA, (unsigned int)len,
			       (unsigned int)airtime_ms);
		timeout_ms = MAX(1200U, airtime_ms + 400U);
		radio_toast(app, DESKTOP_TEXT_RADIO_TITLE, app->toast_text, &I_done_24x24,
			    timeout_ms);
		radio_switch(app, RADIO_SCREEN_MENU);
		return;
	}

	(void)snprintk(app->toast_text, sizeof(app->toast_text),
		       DESKTOP_TEXT_RADIO_FORMAT_ERROR_CODE, rc);
	radio_toast(app, DESKTOP_TEXT_RADIO_TITLE, app->toast_text, &I_error_24x24, 1500U);
}

void radio_text_submitted(struct zui_text_editor *editor, const char *text,
				 void *user_data)
{
	struct radio_app *app = user_data;

	ARG_UNUSED(editor);

	radio_send_payload(app, (const uint8_t *)text, radio_text_payload_len(text));
}

void radio_hex_submitted(struct zui_hex_editor *editor, const uint8_t *bytes,
				size_t byte_count, void *user_data)
{
	struct radio_app *app = user_data;

	ARG_UNUSED(editor);

	if (app == NULL) {
		return;
	}

	radio_send_payload(app, bytes, byte_count);
}

static void radio_open_text_editor(struct radio_app *app)
{
	if (app == NULL || radio_require_tx_allowed(app)) {
		return;
	}

	memset(app->send_text, 0, sizeof(app->send_text));
	(void)snprintk(app->text_title, sizeof(app->text_title),
		       DESKTOP_TEXT_RADIO_FORMAT_TEXT_MESSAGE_HEADER, 0U,
		       (unsigned int)(sizeof(app->send_text) - 1U));
	(void)zui_text_editor_update(app->text_editor, &(struct zui_text_editor_config){
		.title = app->text_title,
		.buffer = app->send_text,
		.buffer_size = sizeof(app->send_text),
		.min_length = 1U,
		.clear_on_enter = true,
		.clear_default_text = true,
		.submitted = radio_text_submitted,
		.user_data = app,
	});
	radio_switch(app, RADIO_SCREEN_TEXT_EDITOR);
}

static void radio_open_hex_editor(struct radio_app *app)
{
	if (app == NULL || radio_require_tx_allowed(app)) {
		return;
	}

	memset(app->send_hex, 0, sizeof(app->send_hex));
	(void)snprintk(app->hex_title, sizeof(app->hex_title),
		       DESKTOP_TEXT_RADIO_FORMAT_HEX_PAYLOAD_HEADER, 0U,
		       (unsigned int)sizeof(app->send_hex));
	(void)zui_hex_editor_update(app->hex_editor, &(struct zui_hex_editor_config){
		.title = app->hex_title,
		.bytes = app->send_hex,
		.byte_count = sizeof(app->send_hex),
		.submitted = radio_hex_submitted,
		.user_data = app,
	});
	radio_switch(app, RADIO_SCREEN_HEX_EDITOR);
}
void radio_packet_selected(struct zui_sublist *list, uint32_t id, size_t index,
				  const struct zui_input_event *event, void *user_data)
{
	struct radio_app *app = user_data;

	ARG_UNUSED(list);
	ARG_UNUSED(index);
	ARG_UNUSED(event);

	switch (id) {
	case RADIO_PACKET_ITEM_TEXT:
		radio_open_text_editor(app);
		break;
	case RADIO_PACKET_ITEM_HEX:
		radio_open_hex_editor(app);
		break;
	case RADIO_PACKET_ITEM_CAPTURE:
		radio_switch(app, RADIO_SCREEN_CAPTURE);
		break;
	default:
		break;
	}
}
static void radio_packet_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct radio_app *app = user_data;

	(void)zui_screen_draw(zui_sublist_get_screen(app->packet_menu), draw);
}

static bool radio_packet_input(const struct zui_input_event *event, void *user_data)
{
	struct radio_app *app = user_data;

	if (app == NULL || event == NULL) {
		return false;
	}
	if (desktop_app_input_should_consume_edge(event)) {
		return true;
	}
	if (desktop_app_input_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		radio_switch(app, RADIO_SCREEN_MENU);
		return true;
	}
	if (zui_screen_submit_input(zui_sublist_get_screen(app->packet_menu), event) > 0) {
		radio_request_redraw(app);
		return true;
	}

	return false;
}

static void radio_packet_enter(void *user_data)
{
	struct radio_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_enter(zui_sublist_get_screen(app->packet_menu));
	}
}

static void radio_packet_exit(void *user_data)
{
	struct radio_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_exit(zui_sublist_get_screen(app->packet_menu));
	}
}
static void radio_text_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct radio_app *app = user_data;

	(void)zui_screen_draw(zui_text_editor_get_screen(app->text_editor), draw);
}

static bool radio_text_input(const struct zui_input_event *event, void *user_data)
{
	struct radio_app *app = user_data;

	if (app == NULL || event == NULL) {
		return false;
	}
	if (desktop_app_input_should_consume_edge(event)) {
		return true;
	}
	if (desktop_app_input_is_long_press(event) && event->code == ZUI_INPUT_CODE_BACK) {
		radio_switch(app, RADIO_SCREEN_PACKET);
		return true;
	}
	if (zui_screen_submit_input(zui_text_editor_get_screen(app->text_editor), event) > 0) {
		radio_request_redraw(app);
		return true;
	}

	return false;
}

static void radio_text_enter(void *user_data)
{
	struct radio_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_enter(zui_text_editor_get_screen(app->text_editor));
	}
}

static void radio_text_exit(void *user_data)
{
	struct radio_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_exit(zui_text_editor_get_screen(app->text_editor));
	}
}

static void radio_hex_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct radio_app *app = user_data;

	(void)zui_screen_draw(zui_hex_editor_get_screen(app->hex_editor), draw);
}

static bool radio_hex_input(const struct zui_input_event *event, void *user_data)
{
	struct radio_app *app = user_data;

	if (app == NULL || event == NULL) {
		return false;
	}
	if (desktop_app_input_should_consume_edge(event)) {
		return true;
	}
	if (desktop_app_input_is_long_press(event) && event->code == ZUI_INPUT_CODE_BACK) {
		radio_switch(app, RADIO_SCREEN_PACKET);
		return true;
	}
	if (zui_screen_submit_input(zui_hex_editor_get_screen(app->hex_editor), event) > 0) {
		radio_request_redraw(app);
		return true;
	}

	return false;
}

static void radio_hex_enter(void *user_data)
{
	struct radio_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_enter(zui_hex_editor_get_screen(app->hex_editor));
	}
}

static void radio_hex_exit(void *user_data)
{
	struct radio_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_exit(zui_hex_editor_get_screen(app->hex_editor));
	}
}
const struct zui_screen_ops radio_packet_ops = {
	.draw = radio_packet_draw,
	.input = radio_packet_input,
	.enter = radio_packet_enter,
	.exit = radio_packet_exit,
};
const struct zui_screen_ops radio_text_ops = {
	.draw = radio_text_draw,
	.input = radio_text_input,
	.enter = radio_text_enter,
	.exit = radio_text_exit,
};
const struct zui_screen_ops radio_hex_ops = {
	.draw = radio_hex_draw,
	.input = radio_hex_input,
	.enter = radio_hex_enter,
	.exit = radio_hex_exit,
};
