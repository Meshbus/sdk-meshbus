/* SPDX-License-Identifier: Apache-2.0 */

#include "meshcore_private.h"

#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "assets/assets_icons.h"
#include "desktop_private.h"
#include "text/desktop_text.h"

bool meshcore_is_click(const struct zui_input_event *event)
{
	return event != NULL && event->action == ZUI_INPUT_ACTION_CLICK;
}

bool meshcore_is_long(const struct zui_input_event *event)
{
	return event != NULL && event->action == ZUI_INPUT_ACTION_LONG_PRESS;
}

bool meshcore_should_consume_edge(const struct zui_input_event *event)
{
	if (event == NULL || (event->action != ZUI_INPUT_ACTION_PRESS &&
			      event->action != ZUI_INPUT_ACTION_RELEASE)) {
		return false;
	}

	switch (event->code) {
	case ZUI_INPUT_CODE_UP:
	case ZUI_INPUT_CODE_DOWN:
	case ZUI_INPUT_CODE_LEFT:
	case ZUI_INPUT_CODE_RIGHT:
	case ZUI_INPUT_CODE_SELECT:
	case ZUI_INPUT_CODE_BACK:
		return true;
	default:
		return false;
	}
}

void meshcore_request_redraw(struct meshcore_app *app)
{
	if (app != NULL && app->host != NULL) {
		zui_desktop_request_redraw(zui_host_get_user_data(app->host));
	}
}

void meshcore_switch(struct meshcore_app *app, uint32_t screen_id)
{
	if (app == NULL || app->router == NULL) {
		return;
	}

	if (zui_router_switch(app->router, screen_id) == 0) {
		app->current_screen = screen_id;
		meshcore_request_redraw(app);
	}
}

void meshcore_exit(struct meshcore_app *app)
{
	if (app == NULL) {
		return;
	}

	(void)zui_host_detach_router(app->host, ZUI_LAYER_FULLSCREEN);
	k_sem_give(&app->exit_sem);
	meshcore_request_redraw(app);
}

void meshcore_toast(struct meshcore_app *app, const char *text, const struct zui_icon *icon,
		    uint32_t timeout_ms)
{
	if (app == NULL) {
		return;
	}

	(void)zui_toast_show(app->host, &(struct zui_toast_config){
						.title = DESKTOP_TEXT_MESHCORE_TITLE,
						.text = text,
						.icon = icon,
						.timeout_ms = timeout_ms,
					});
}

void meshcore_apply_toast(struct meshcore_app *app, bool success, const char *success_text)
{
	meshcore_toast(app,
		       success ? (success_text != NULL ? success_text
						       : DESKTOP_TEXT_COMMON_SETTINGS_APPLIED)
			       : DESKTOP_TEXT_COMMON_SETTINGS_INVALID,
		       success ? &I_save_24x24 : &I_error_24x24, success ? 900U : 1500U);
}

void meshcore_channel_sanitize(meshbus_channel *channel)
{
	if (channel == NULL) {
		return;
	}

	channel->name[sizeof(channel->name) - 1U] = '\0';
	if (channel->secret.size > sizeof(channel->secret.bytes)) {
		channel->secret.size = sizeof(channel->secret.bytes);
	}
	if (channel->hash.size > sizeof(channel->hash.bytes)) {
		channel->hash.size = sizeof(channel->hash.bytes);
	}
}

void meshcore_hex_short(const uint8_t *bytes, size_t size, char *buf, size_t buf_size,
			size_t max_bytes)
{
	const char hex[] = "0123456789ABCDEF";
	size_t off = 0U;

	if (buf == NULL || buf_size == 0U) {
		return;
	}

	if (bytes == NULL || size == 0U) {
		(void)snprintk(buf, buf_size, "%s", DESKTOP_TEXT_COMMON_NONE);
		return;
	}

	size = MIN(size, max_bytes);
	for (size_t i = 0U; i < size && (off + 2U) < buf_size; i++) {
		buf[off++] = hex[(bytes[i] >> 4) & 0x0F];
		buf[off++] = hex[bytes[i] & 0x0F];
	}
	buf[off] = '\0';
}

void meshcore_hex_payload(char *out, size_t out_size, const uint8_t *bytes, size_t size)
{
	const char hex[] = "0123456789ABCDEF";
	size_t off = 0U;

	if (out == NULL || out_size == 0U) {
		return;
	}

	out[0] = '\0';
	if (bytes == NULL || size == 0U) {
		return;
	}

	size = MIN(size, (out_size - 1U) / 2U);
	for (size_t i = 0U; i < size && (off + 2U) < out_size; i++) {
		out[off++] = hex[(bytes[i] >> 4) & 0x0F];
		out[off++] = hex[bytes[i] & 0x0F];
	}
	out[off] = '\0';
}

char *meshcore_value_buf(struct meshcore_app *app, size_t *idx)
{
	if (app == NULL || idx == NULL || *idx >= MESHCORE_VALUE_BUF_COUNT) {
		return NULL;
	}

	return app->value_bufs[(*idx)++];
}

static void meshcore_prepare_detail_text_view(struct meshcore_app *app, const char *title)
{
	(void)zui_text_view_update(app->detail_view, &(struct zui_text_view_config){
							     .title = title,
							     .text = app->detail_text,
							     .font = ZUI_FONT_SECONDARY,
							     .mode = ZUI_TEXT_VIEW_MODE_TEXT,
						     });
}

void meshcore_show_detail(struct meshcore_app *app, const char *title, const uint8_t *bytes,
			  size_t size)
{
	if (app == NULL) {
		return;
	}

	app->return_screen = app->current_screen != 0U ? app->current_screen : MESHCORE_SCREEN_SETTINGS;
	app->detail_kind = MESHCORE_DETAIL_TEXT;
	app->detail_qr_visible = false;
	app->detail_qr_valid = false;
	meshcore_hex_payload(app->detail_text, sizeof(app->detail_text), bytes, size);
	meshcore_prepare_detail_text_view(app, title);
	meshcore_switch(app, MESHCORE_SCREEN_DETAIL);
}

void meshcore_show_channel_secret_detail(struct meshcore_app *app, const uint8_t *bytes,
					 size_t size)
{
	if (app == NULL) {
		return;
	}

	app->return_screen = app->current_screen != 0U ? app->current_screen
						       : MESHCORE_SCREEN_CHANNEL_SETTINGS;
	app->detail_kind = MESHCORE_DETAIL_CHANNEL_SECRET;
	app->detail_qr_visible = false;
	meshcore_hex_payload(app->detail_text, sizeof(app->detail_text), bytes, size);
	app->detail_qr_valid = app->detail_text[0] != '\0' &&
			       meshcore_qr_encode_text(app->detail_text, &app->detail_qr);
	meshcore_prepare_detail_text_view(app, DESKTOP_TEXT_MESHCORE_CHANNEL_SECRET_TITLE);
	meshcore_switch(app, MESHCORE_SCREEN_DETAIL);
}

void meshcore_show_modal(struct meshcore_app *app, enum meshcore_modal_kind kind, const char *title,
			 const char *text, const char *left, const char *right)
{
	if (app == NULL) {
		return;
	}

	app->modal_kind = kind;
	(void)zui_modal_update(app->modal, &(struct zui_modal_config){
						   .title = title,
						   .text = text,
						   .icon = NULL,
						   .left_button = left,
						   .right_button = right,
						   .result = meshcore_modal_result,
						   .user_data = app,
					   });
	meshcore_switch(app, MESHCORE_SCREEN_MODAL);
}
void meshcore_open_number(struct meshcore_app *app, enum meshcore_number_field field,
			  const char *title, int64_t value, int64_t min_value, int64_t max_value)
{
	if (app == NULL) {
		return;
	}

	app->number_field = field;
	app->return_screen = MESHCORE_SCREEN_SETTINGS;
	(void)zui_number_editor_update(app->number_editor,
				       &(struct zui_number_editor_config){
					       .title = title,
					       .value = value,
					       .min_value = min_value,
					       .max_value = max_value,
					       .max_digits = 10U,
					       .unsigned_only = true,
					       .submitted = meshcore_number_submitted,
					       .user_data = app,
				       });
	meshcore_switch(app, MESHCORE_SCREEN_NUMBER);
}

void meshcore_open_name_editor(struct meshcore_app *app, enum meshcore_text_mode mode,
			       const char *title, const char *text, uint32_t return_screen)
{
	if (app == NULL) {
		return;
	}

	app->text_mode = mode;
	app->return_screen = return_screen;
	(void)snprintk(app->text_buf, sizeof(app->text_buf), "%s", text != NULL ? text : "");
	(void)zui_text_editor_update(app->text_editor, &(struct zui_text_editor_config){
							       .title = title,
							       .buffer = app->text_buf,
							       .buffer_size = sizeof(app->text_buf),
							       .min_length = 0U,
							       .clear_on_enter = false,
							       .clear_default_text = false,
							       .submitted = meshcore_text_submitted,
							       .user_data = app,
						       });
	meshcore_switch(app, MESHCORE_SCREEN_TEXT);
}
void meshcore_number_submitted(struct zui_number_editor *editor, int64_t value, void *user_data)
{
	struct meshcore_app *app = user_data;

	ARG_UNUSED(editor);

	if (app == NULL) {
		return;
	}

	switch (app->number_field) {
	case MESHCORE_NUMBER_MULTI_ACKS:
		app->editing.multi_acks = (uint8_t)CLAMP(value, 0, 255);
		break;
	case MESHCORE_NUMBER_CONTACT_ADD_HOPS_LIMIT:
		app->editing.add_contact_hops_limit =
			(uint8_t)CLAMP(value, 0,
				       (int64_t)MESHCORE_CONTACT_ADD_HOPS_LIMIT_MAX);
		break;
	case MESHCORE_NUMBER_FLOOD_MAX:
		app->editing.flood_max = (uint8_t)CLAMP(value, 0, 255);
		break;
	case MESHCORE_NUMBER_TX_DELAY_FACTOR_X100:
		app->editing.tx_delay_factor = (float)CLAMP(value, 0, 200) / 100.0f;
		break;
	case MESHCORE_NUMBER_DIRECT_TX_DELAY_FACTOR_X100:
		app->editing.direct_tx_delay_factor = (float)CLAMP(value, 0, 200) / 100.0f;
		break;
	case MESHCORE_NUMBER_ADVERT_INTERVAL:
		app->editing.advert_interval = (uint32_t)MAX(value, 0);
		break;
	case MESHCORE_NUMBER_FLOOD_ADVERT_INTERVAL:
		app->editing.flood_advert_interval = (uint32_t)MAX(value, 0);
		break;
	default:
		break;
	}
	app->number_field = MESHCORE_NUMBER_NONE;
	meshcore_build_settings_form(app);
	meshcore_switch(app, app->return_screen);
}

void meshcore_text_submitted(struct zui_text_editor *editor, const char *text, void *user_data)
{
	struct meshcore_app *app = user_data;

	ARG_UNUSED(editor);

	if (app == NULL || text == NULL) {
		return;
	}

	switch (app->text_mode) {
	case MESHCORE_TEXT_NODE_NAME:
		(void)snprintk(app->editing.name, sizeof(app->editing.name), "%s", text);
		meshcore_build_settings_form(app);
		break;
	case MESHCORE_TEXT_CHANNEL_NAME:
		(void)snprintk(app->channel_editing.name, sizeof(app->channel_editing.name), "%s",
			       text);
		meshcore_build_channel_form(app);
		break;
	default:
		break;
	}
	app->text_mode = MESHCORE_TEXT_NONE;
	meshcore_switch(app, app->return_screen);
}

void meshcore_modal_result(struct zui_modal *modal, enum zui_modal_result result,
			   const struct zui_input_event *event, void *user_data)
{
	struct meshcore_app *app = user_data;

	ARG_UNUSED(modal);
	ARG_UNUSED(event);

	if (app == NULL) {
		return;
	}

	switch (app->modal_kind) {
	case MESHCORE_MODAL_ADVERT:
		if (result == ZUI_MODAL_RESULT_LEFT) {
			(void)meshbus_meshcore_advert_request(true);
			meshcore_toast(app, DESKTOP_TEXT_MESHCORE_FLOOD_ADVERT_QUEUED,
				       &I_done_24x24, 900U);
		} else if (result == ZUI_MODAL_RESULT_RIGHT) {
			(void)meshbus_meshcore_advert_request(false);
			meshcore_toast(app, DESKTOP_TEXT_MESHCORE_LOCAL_ADVERT_QUEUED,
				       &I_done_24x24, 900U);
		}
		meshcore_switch(app, MESHCORE_SCREEN_MENU);
		break;
	case MESHCORE_MODAL_RESET:
		if (result == ZUI_MODAL_RESULT_RIGHT) {
			meshcore_reset_settings(app);
		} else {
			meshcore_switch(app, MESHCORE_SCREEN_SETTINGS);
		}
		break;
	case MESHCORE_MODAL_CHANNEL_RESET:
		if (result == ZUI_MODAL_RESULT_RIGHT) {
			meshcore_reset_channel(app);
		} else {
			meshcore_switch(app, MESHCORE_SCREEN_CHANNEL_SETTINGS);
		}
		break;
	case MESHCORE_MODAL_RADIO_PRESET:
		if (result == ZUI_MODAL_RESULT_RIGHT) {
			meshcore_apply_radio_preset(app);
		} else {
			meshcore_switch(app, MESHCORE_SCREEN_RADIO_PRESET);
		}
		break;
	default:
		meshcore_switch(app, MESHCORE_SCREEN_MENU);
		break;
	}
	app->modal_kind = MESHCORE_MODAL_NONE;
}

bool meshcore_forward_input(struct zui_screen *screen, const struct zui_input_event *event)
{
	if (screen == NULL || event == NULL) {
		return false;
	}

	return zui_screen_submit_input(screen, event) > 0;
}

void meshcore_forward_enter(struct zui_screen *screen)
{
	if (screen != NULL) {
		(void)zui_screen_enter(screen);
	}
}

void meshcore_forward_exit(struct zui_screen *screen)
{
	if (screen != NULL) {
		(void)zui_screen_exit(screen);
	}
}

static void meshcore_text_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_draw(zui_text_editor_get_screen(app->text_editor), draw);
	}
}

static bool meshcore_text_input(const struct zui_input_event *event, void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app == NULL) {
		return false;
	}
	if (meshcore_should_consume_edge(event)) {
		return true;
	}
	if (meshcore_is_long(event) && event->code == ZUI_INPUT_CODE_BACK) {
		meshcore_switch(app, app->return_screen);
		return true;
	}
	if (meshcore_forward_input(zui_text_editor_get_screen(app->text_editor), event)) {
		return true;
	}
	return false;
}

static void meshcore_text_enter(void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		meshcore_forward_enter(zui_text_editor_get_screen(app->text_editor));
	}
}

static void meshcore_text_exit(void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		meshcore_forward_exit(zui_text_editor_get_screen(app->text_editor));
	}
}

static void meshcore_number_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_draw(zui_number_editor_get_screen(app->number_editor), draw);
	}
}

static bool meshcore_number_input(const struct zui_input_event *event, void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app == NULL) {
		return false;
	}
	if (meshcore_should_consume_edge(event)) {
		return true;
	}
	if (meshcore_is_long(event) && event->code == ZUI_INPUT_CODE_BACK) {
		meshcore_switch(app, app->return_screen);
		return true;
	}
	if (meshcore_forward_input(zui_number_editor_get_screen(app->number_editor), event)) {
		return true;
	}
	return false;
}

static void meshcore_number_enter(void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		meshcore_forward_enter(zui_number_editor_get_screen(app->number_editor));
	}
}

static void meshcore_number_exit(void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		meshcore_forward_exit(zui_number_editor_get_screen(app->number_editor));
	}
}

static const char *meshcore_detail_toggle_label(const struct meshcore_app *app)
{
	if (app == NULL || app->detail_kind != MESHCORE_DETAIL_CHANNEL_SECRET ||
	    !app->detail_qr_valid || app->detail_qr_visible) {
		return NULL;
	}

	return DESKTOP_TEXT_MESHCORE_CHANNEL_QRCODE_BUTTON;
}

#define MESHCORE_DETAIL_QR_CONTENT_TOP 12

static void meshcore_detail_draw_qr(struct zui_draw_ctx *draw, struct meshcore_app *app)
{
	int modules;
	int area_h;
	int qr_px;
	int left;
	int top;

	zui_draw_reset(draw);
	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	zui_draw_text(draw, (struct zui_point){.x = 2, .y = 10},
		      DESKTOP_TEXT_MESHCORE_CHANNEL_SECRET_TITLE);

	if (app == NULL || !app->detail_qr_valid) {
		return;
	}

	modules = app->detail_qr.size;
	area_h = (int)zui_draw_height(draw) - MESHCORE_DETAIL_QR_CONTENT_TOP;
	qr_px = MIN((int)zui_draw_width(draw), area_h);
	left = ((int)zui_draw_width(draw) - qr_px) / 2;
	top = MESHCORE_DETAIL_QR_CONTENT_TOP;

	zui_draw_set_color(draw, ZUI_COLOR_WHITE);
	zui_draw_box(draw, &(struct zui_rect){.x = (int16_t)left,
					      .y = (int16_t)top,
					      .width = (uint16_t)qr_px,
					      .height = (uint16_t)qr_px});
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	for (int y = 0; y < modules; y++) {
		int y0 = top + (y * qr_px) / modules;
		int y1 = top + ((y + 1) * qr_px) / modules;

		for (int x = 0; x < modules; x++) {
			int x0 = left + (x * qr_px) / modules;
			int x1 = left + ((x + 1) * qr_px) / modules;

			if (!meshcore_qr_get_module(&app->detail_qr, x, y)) {
				continue;
			}

			zui_draw_box(draw,
				     &(struct zui_rect){
					     .x = (int16_t)x0,
					     .y = (int16_t)y0,
					     .width = (uint16_t)(x1 - x0),
					     .height = (uint16_t)(y1 - y0),
				     });
		}
	}
}

static void meshcore_detail_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct meshcore_app *app = user_data;
	const char *toggle_label;

	if (app != NULL) {
		if (app->detail_kind == MESHCORE_DETAIL_CHANNEL_SECRET &&
		    app->detail_qr_visible) {
			meshcore_detail_draw_qr(draw, app);
		} else {
			(void)zui_screen_draw(zui_text_view_get_screen(app->detail_view), draw);
		}
		toggle_label = meshcore_detail_toggle_label(app);
		if (toggle_label != NULL) {
			zui_draw_button_hints(draw, &(struct zui_draw_button_hint){
							    .center = toggle_label,
						    });
		}
	}
}

static bool meshcore_detail_input(const struct zui_input_event *event, void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app == NULL) {
		return false;
	}
	if (event == NULL) {
		return false;
	}
	if (app->detail_kind == MESHCORE_DETAIL_CHANNEL_SECRET && app->detail_qr_visible) {
		if (event->action == ZUI_INPUT_ACTION_RELEASE) {
			return true;
		}
		app->detail_qr_visible = false;
		meshcore_request_redraw(app);
		return true;
	}
	if (meshcore_should_consume_edge(event)) {
		return true;
	}
	if (meshcore_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		meshcore_switch(app, app->return_screen != 0U ? app->return_screen
							      : MESHCORE_SCREEN_SETTINGS);
		return true;
	}
	if (meshcore_is_click(event) && event->code == ZUI_INPUT_CODE_SELECT &&
	    app->detail_kind == MESHCORE_DETAIL_CHANNEL_SECRET && app->detail_qr_valid) {
		app->detail_qr_visible = true;
		meshcore_request_redraw(app);
		return true;
	}
	return meshcore_forward_input(zui_text_view_get_screen(app->detail_view), event);
}

static void meshcore_detail_enter(void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		meshcore_forward_enter(zui_text_view_get_screen(app->detail_view));
	}
}

static void meshcore_detail_exit(void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		meshcore_forward_exit(zui_text_view_get_screen(app->detail_view));
	}
}

static void meshcore_modal_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_draw(zui_modal_get_screen(app->modal), draw);
	}
}

static bool meshcore_modal_input(const struct zui_input_event *event, void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app == NULL) {
		return false;
	}
	if (meshcore_should_consume_edge(event)) {
		return true;
	}
	if (meshcore_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		meshcore_modal_result(app->modal, ZUI_MODAL_RESULT_CENTER, event, app);
		return true;
	}
	if (meshcore_forward_input(zui_modal_get_screen(app->modal), event)) {
		return true;
	}
	return false;
}

static void meshcore_modal_enter(void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		meshcore_forward_enter(zui_modal_get_screen(app->modal));
	}
}

static void meshcore_modal_exit(void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		meshcore_forward_exit(zui_modal_get_screen(app->modal));
	}
}

const struct zui_screen_ops meshcore_text_ops = {
	.draw = meshcore_text_draw,
	.input = meshcore_text_input,
	.enter = meshcore_text_enter,
	.exit = meshcore_text_exit,
};

const struct zui_screen_ops meshcore_number_ops = {
	.draw = meshcore_number_draw,
	.input = meshcore_number_input,
	.enter = meshcore_number_enter,
	.exit = meshcore_number_exit,
};

const struct zui_screen_ops meshcore_detail_ops = {
	.draw = meshcore_detail_draw,
	.input = meshcore_detail_input,
	.enter = meshcore_detail_enter,
	.exit = meshcore_detail_exit,
};

const struct zui_screen_ops meshcore_modal_ops = {
	.draw = meshcore_modal_draw,
	.input = meshcore_modal_input,
	.enter = meshcore_modal_enter,
	.exit = meshcore_modal_exit,
};
