/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/meshbus/channel.h>
#include <zephyr/meshbus/desktop.h>
#include <zephyr/meshbus/message.h>
#include <zephyr/meshbus/contact.h>
#include <zephyr/meshbus/radio.h>
#include <zephyr/meshbus/time.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/zui/zui.h>

#include "apps/app_ids.h"
#include "assets/assets_icons.h"
#include "desktop_private.h"
#include "messages_types.h"
#include "services/messages_cache.h"
#include "text/desktop_text.h"

LOG_MODULE_REGISTER(meshbus_desktop_messages, CONFIG_MESHBUS_DESKTOP_LOG_LEVEL);

#define MESSAGES_SCREEN_MENU    1U
#define MESSAGES_SCREEN_INBOX   2U
#define MESSAGES_SCREEN_DETAIL  3U
#define MESSAGES_SCREEN_COMPOSE 4U
#define MESSAGES_SCREEN_TARGET  5U
#define MESSAGES_SCREEN_CONTENT 6U

#define MESSAGES_MENU_INBOX   1U
#define MESSAGES_MENU_COMPOSE 2U

#define MESSAGES_COMPOSE_TO      1U
#define MESSAGES_COMPOSE_ROUTE   2U
#define MESSAGES_COMPOSE_CONTENT 3U
#define MESSAGES_COMPOSE_EDIT    4U
#define MESSAGES_COMPOSE_SEND    5U

#define MESSAGES_STATIC_TEXT_MAX 128U
#define MESSAGES_TEXT_MAX        512U
#define MESSAGES_LABEL_MAX       48U
#define MESSAGES_RIGHT_MAX       10U
#define MESSAGES_FORM_ITEMS_MAX  5U
#define MESSAGES_VALUE_COUNT     5U
#define MESSAGES_VALUE_MAX       48U
#define MESSAGES_TARGET_VISIBLE_MAX        3U
#define MESSAGES_TARGET_BUFFER_MAX         4U
#define MESSAGES_TARGET_LOAD_DELAY         K_MSEC(1)
#define MESSAGES_TARGET_INITIAL_LOAD_DELAY K_MSEC(250)
#define MESSAGES_RESPONSE_REFRESH_DELAY    K_MSEC(1)
#define MESSAGES_DETAIL_SOURCE_PREFIX_BYTES 4U
#define MESSAGES_DETAIL_TITLE_X 2
#define MESSAGES_DETAIL_TITLE_Y 10
#define MESSAGES_DETAIL_TIME_X  126
#define MESSAGES_DETAIL_FRAME_X 0
#define MESSAGES_DETAIL_FRAME_Y 13
#define MESSAGES_DETAIL_FRAME_W 128U
#define MESSAGES_DETAIL_FRAME_H 54U
#define MESSAGES_DETAIL_TEXT_X  2
#define MESSAGES_DETAIL_TEXT_Y  16
#define MESSAGES_DETAIL_TEXT_W  122U
#define MESSAGES_DETAIL_TEXT_H  48U
#define MESSAGES_DETAIL_SCROLLBAR_X 125
#define MESSAGES_DETAIL_TRAILING_PAD_THRESHOLD 32U

struct messages_entry {
	struct desktop_messages_cache_key key;
	meshbus_message_content message;
	bool unread;
	bool timestamp_realtime;
	char label[MESSAGES_LABEL_MAX];
	char detail[MESSAGES_RIGHT_MAX];
};

struct messages_target_row {
	struct zui_messages_target_entry target;
	const struct zui_icon *icon;
	bool source_is_node;
	uint8_t source_index;
	char label[MESSAGES_LABEL_MAX];
};

enum messages_target_load_mode {
	MESSAGES_TARGET_LOAD_IDLE = 0,
	MESSAGES_TARGET_LOAD_INITIAL,
	MESSAGES_TARGET_LOAD_NEXT,
	MESSAGES_TARGET_LOAD_PREV,
};

struct messages_app {
	struct zui_host *host;
	struct zui_router *router;
	struct k_sem exit_sem;
	bool exit_requested;

	struct zui_sublist *menu;
	struct zui_sublist *inbox;
	struct zui_sublist *targets;
	struct zui_form *compose;
	struct zui_text_editor *content_editor;

	struct zui_screen *menu_screen;
	struct zui_screen *inbox_screen;
	struct zui_screen *target_screen;
	struct zui_screen *compose_screen;
	struct zui_screen *content_screen;
	struct zui_screen *detail_screen;

	struct zui_list_item menu_items[2];
	struct zui_form_item compose_items[MESSAGES_FORM_ITEMS_MAX];
	char compose_values[MESSAGES_VALUE_COUNT][MESSAGES_VALUE_MAX];
	char content[MESSAGES_STATIC_TEXT_MAX];
	char detail_title[MESSAGES_LABEL_MAX];
	char detail_time[MESSAGES_RIGHT_MAX];
	char detail_text[MESSAGES_TEXT_MAX];
	size_t detail_scroll;
	size_t detail_max_scroll;
	struct messages_entry entries[CONFIG_MESHBUS_DESKTOP_MESSAGE_CACHE_COUNT];
	size_t entry_count;
	size_t selected_entry;
	bool selected_entry_valid;
	struct k_work_delayable response_refresh_work;
	uint32_t cache_update_seq;
	struct k_work_delayable target_load_work;
	struct messages_target_row target_rows[MESSAGES_TARGET_BUFFER_MAX];
	size_t target_count;
	enum messages_target_load_mode target_load_mode;
	bool target_load_active;
	bool target_loading;
	bool target_scan_node;
	int16_t target_scan_index;
	uint8_t target_channel_size;
	uint8_t target_node_size;
	bool target_has_prev;
	bool target_has_next;
	bool compose_target_valid;
	struct zui_messages_target_entry compose_target;
	enum zui_messages_route_mode compose_route;
	uint8_t compose_canned_idx;

	uint32_t current_screen;
	bool compose_custom;
};

static void messages_menu_selected(struct zui_sublist *list, uint32_t id, size_t index,
				   const struct zui_input_event *event, void *user_data);
static void messages_inbox_selected(struct zui_sublist *list, uint32_t id, size_t index,
				    const struct zui_input_event *event, void *user_data);
static size_t messages_inbox_count(void *user_data);
static int messages_inbox_item(size_t index, struct zui_list_item *item, void *user_data);
static void messages_update_inbox(struct messages_app *app, bool preserve_selection);
static void messages_update_targets(struct messages_app *app, size_t selected);
static void messages_target_selected(struct zui_sublist *list, uint32_t id, size_t index,
				     const struct zui_input_event *event, void *user_data);
static void messages_compose_changed(struct zui_form *form, uint32_t id, size_t option_index,
				     void *user_data);
static void messages_compose_activated(struct zui_form *form, uint32_t id,
				       const struct zui_input_event *event, void *user_data);
static void messages_content_submitted(struct zui_text_editor *editor, const char *text,
				       void *user_data);
static void messages_send_activate(struct messages_app *app);

static atomic_ptr_t messages_active_app;
static atomic_t messages_response_inflight;
K_SEM_DEFINE(messages_response_idle, 0, K_SEM_MAX_LIMIT);

static bool messages_is_click(const struct zui_input_event *event)
{
	return event != NULL && event->action == ZUI_INPUT_ACTION_CLICK;
}

static bool messages_should_consume_edge(const struct zui_input_event *event)
{
	if (event == NULL ||
	    (event->action != ZUI_INPUT_ACTION_PRESS && event->action != ZUI_INPUT_ACTION_RELEASE)) {
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

static void messages_request_redraw(struct messages_app *app)
{
	if (app != NULL && app->host != NULL) {
		zui_desktop_request_redraw(zui_host_get_user_data(app->host));
	}
}

static void messages_switch(struct messages_app *app, uint32_t screen_id)
{
	if (app == NULL || app->router == NULL) {
		return;
	}
	if (zui_router_switch(app->router, screen_id) == 0) {
		app->current_screen = screen_id;
		messages_request_redraw(app);
	}
}

static void messages_exit(struct messages_app *app)
{
	if (app == NULL || app->exit_requested) {
		return;
	}

	app->exit_requested = true;
	if (app->host != NULL) {
		(void)zui_host_detach_router(app->host, ZUI_LAYER_FULLSCREEN);
	}
	k_sem_give(&app->exit_sem);
	messages_request_redraw(app);
}

static void messages_toast(struct messages_app *app, const char *title, const char *text,
			   const struct zui_icon *icon, uint32_t timeout_ms)
{
	if (app == NULL) {
		return;
	}

	(void)zui_toast_show(app->host, &(struct zui_toast_config){
		.title = title,
		.text = text,
		.icon = icon,
		.timeout_ms = timeout_ms,
	});
}

static bool messages_radio_tx_ready(struct messages_app *app)
{
	meshbus_radio_config cfg;
	int rc;

	rc = meshbus_radio_config_get(&cfg);
	LOG_DBG("messages app: meshbus_radio_config_get() -> %d", rc);
	if (rc != 0) {
		messages_toast(app, DESKTOP_TEXT_MESSAGES_SEND_FAILED,
			       DESKTOP_TEXT_MESSAGES_SEND_FAILED_MESSAGE, &I_error_24x24, 1500U);
		return false;
	}
	if (!cfg.enabled) {
		messages_toast(app, DESKTOP_TEXT_MESSAGES_SEND_FAILED,
			       DESKTOP_TEXT_RADIO_DISABLED, &I_error_24x24, 1500U);
		return false;
	}
	if (cfg.receive_only) {
		messages_toast(app, DESKTOP_TEXT_MESSAGES_SEND_FAILED,
			       DESKTOP_TEXT_RADIO_RX_ONLY_MODE, &I_error_24x24, 1500U);
		return false;
	}

	return true;
}

static int messages_register_screen(struct messages_app *app, uint32_t id,
				    struct zui_screen *screen)
{
	return zui_router_register_screen(app->router, id, screen);
}

static void messages_compose_set_value(struct messages_app *app, size_t index, const char *value)
{
	if (app == NULL || index >= ARRAY_SIZE(app->compose_values)) {
		return;
	}

	(void)snprintk(app->compose_values[index], sizeof(app->compose_values[index]),
		       "%s", value != NULL ? value : DESKTOP_TEXT_COMMON_EMPTY);
}

static void messages_reset_compose_draft(struct messages_app *app)
{
	if (app == NULL) {
		return;
	}

	app->compose_target_valid = false;
	memset(&app->compose_target, 0, sizeof(app->compose_target));
	app->compose_route = MESSAGES_ROUTE_DIRECT;
	app->compose_canned_idx = 0U;
	app->compose_custom = false;
	(void)snprintk(app->content, sizeof(app->content), "%s",
		       DESKTOP_TEXT_MESSAGES_CANNED_VALUES[0]);
}

static const char *messages_current_content(const struct messages_app *app)
{
	if (app == NULL) {
		return DESKTOP_TEXT_COMMON_EMPTY;
	}
	if (app->compose_custom) {
		return app->content;
	}

	return DESKTOP_TEXT_MESSAGES_CANNED_VALUES[app->compose_canned_idx %
						   DESKTOP_TEXT_MESSAGES_CANNED_COUNT];
}

static void messages_sync_content_buffer(struct messages_app *app)
{
	if (app == NULL || app->compose_custom) {
		return;
	}

	(void)snprintk(app->content, sizeof(app->content), "%s",
		       messages_current_content(app));
}

static uint8_t messages_next_attempt(void)
{
	static uint8_t attempt = 1U;
	uint8_t ret = attempt++;

	if (attempt == 0U) {
		attempt = 1U;
	}
	return ret;
}

static void messages_hex_prefix(char *buf, size_t buf_size, const uint8_t *bytes, size_t len)
{
	if (buf == NULL || buf_size == 0U) {
		return;
	}
	if (bytes == NULL || len == 0U) {
		(void)snprintk(buf, buf_size, "%s", DESKTOP_TEXT_COMMON_UNKNOWN);
	} else if (len >= 3U) {
		(void)snprintk(buf, buf_size, "%02X%02X%02X", bytes[0], bytes[1], bytes[2]);
	} else if (len == 2U) {
		(void)snprintk(buf, buf_size, "%02X%02X", bytes[0], bytes[1]);
	} else {
		(void)snprintk(buf, buf_size, "%02X", bytes[0]);
	}
}

static bool messages_is_channel_message(const meshbus_message_content *msg)
{
	return msg != NULL &&
	       (msg->type == meshbus_MessageContent_MessageType_SEND_CHANNEL ||
		msg->type == meshbus_MessageContent_MessageType_RECEIVE_CHANNEL);
}

static bool messages_is_flood_node_message(const meshbus_message_content *msg)
{
	return msg != NULL && msg->type == meshbus_MessageContent_MessageType_RECEIVE_NODE &&
	       msg->route == meshbus_MessageContent_MessageRoute_ROUTE_FLOOD;
}

static const char *messages_detail_kind(const meshbus_message_content *msg)
{
	if (messages_is_channel_message(msg)) {
		return DESKTOP_TEXT_WIDGET_MESSAGES_CHANNEL;
	}
	if (messages_is_flood_node_message(msg)) {
		return DESKTOP_TEXT_WIDGET_MESSAGES_FLOOD;
	}

	return DESKTOP_TEXT_WIDGET_MESSAGES_DIRECT;
}

static const char *messages_row_kind(const meshbus_message_content *msg)
{
	if (messages_is_channel_message(msg)) {
		return DESKTOP_TEXT_MESSAGES_CHANNEL_SHORT;
	}
	if (messages_is_flood_node_message(msg)) {
		return DESKTOP_TEXT_MESSAGES_FLOOD_SHORT;
	}

	return DESKTOP_TEXT_MESSAGES_PERSON_SHORT;
}

static void messages_detail_source(char *buf, size_t buf_size, const meshbus_message_content *msg)
{
	size_t target_len;
	size_t prefix_len;
	size_t off;

	if (buf == NULL || buf_size == 0U) {
		return;
	}
	if (msg == NULL) {
		(void)snprintk(buf, buf_size, "%s", DESKTOP_TEXT_COMMON_UNKNOWN);
		return;
	}

	(void)snprintk(buf, buf_size, "%s@", messages_detail_kind(msg));
	off = strnlen(buf, buf_size);
	if (off >= buf_size) {
		return;
	}

	target_len = MIN((size_t)msg->target.size, (size_t)sizeof(msg->target.bytes));
	prefix_len = MIN(target_len, (size_t)MESSAGES_DETAIL_SOURCE_PREFIX_BYTES);
	if (prefix_len >= 4U) {
		(void)snprintk(&buf[off], buf_size - off, "%02X%02X%02X%02X",
			       msg->target.bytes[0], msg->target.bytes[1],
			       msg->target.bytes[2], msg->target.bytes[3]);
	} else if (prefix_len > 0U) {
		messages_hex_prefix(&buf[off], buf_size - off, msg->target.bytes, prefix_len);
	} else {
		(void)snprintk(&buf[off], buf_size - off, "%s", DESKTOP_TEXT_COMMON_UNKNOWN);
	}
}

static void messages_format_time_ago(char *buf, size_t buf_size, uint64_t timestamp_ms,
				     bool timestamp_realtime)
{
	uint64_t now_ms;
	uint64_t age_ms;

	if (buf == NULL || buf_size == 0U) {
		return;
	}
	if (timestamp_realtime && meshbus_time_realtime_is_valid()) {
		if (meshbus_time_timestamp_ms_get(&now_ms) != 0 || now_ms == 0U) {
			now_ms = 1U;
		}
	} else {
		now_ms = (uint64_t)k_uptime_get();
		if (now_ms == 0U) {
			now_ms = 1U;
		}
	}

	age_ms = now_ms >= timestamp_ms ? now_ms - timestamp_ms : 0U;
	if (age_ms < 60U * 1000U) {
		(void)snprintk(buf, buf_size, "%s", DESKTOP_TEXT_MESSAGES_TIME_AGO_NOW);
	} else if (age_ms < 3600U * 1000U) {
		(void)snprintk(buf, buf_size, DESKTOP_TEXT_MESSAGES_TIME_AGO_MIN_FORMAT,
			       (unsigned int)(age_ms / (60U * 1000U)));
	} else if (age_ms < 24U * 3600U * 1000U) {
		(void)snprintk(buf, buf_size, DESKTOP_TEXT_MESSAGES_TIME_AGO_HOUR_FORMAT,
			       (unsigned int)(age_ms / (3600U * 1000U)));
	} else {
		(void)snprintk(buf, buf_size, DESKTOP_TEXT_MESSAGES_TIME_AGO_DAY_FORMAT,
			       (unsigned int)(age_ms / (24U * 3600U * 1000U)));
	}
}

static bool messages_source(char *buf, size_t buf_size, const meshbus_message_content *msg)
{
	if (buf == NULL || buf_size == 0U || msg == NULL) {
		return false;
	}
	if (messages_is_channel_message(msg) && msg->target.size >= 3U) {
		messages_hex_prefix(buf, buf_size, msg->target.bytes, msg->target.size);
		return true;
	}
	if (msg->sender_name[0] != '\0') {
		(void)snprintk(buf, buf_size, "%.*s", (int)(buf_size - 1U), msg->sender_name);
		return true;
	}

	messages_hex_prefix(buf, buf_size, msg->target.bytes, msg->target.size);
	return true;
}

static void messages_detail_payload_text(char *buf, size_t buf_size,
					 const meshbus_message_content *msg)
{
	bool trailing_pad;
	size_t len;
	size_t max_len;

	if (buf == NULL || buf_size == 0U) {
		return;
	}

	if (buf_size <= 3U || msg == NULL || msg->payload.size == 0U) {
		(void)snprintk(buf, buf_size, "> %s", DESKTOP_TEXT_COMMON_EMPTY);
		return;
	}

	buf[0] = '>';
	buf[1] = ' ';
	len = MIN((size_t)msg->payload.size, (size_t)sizeof(msg->payload.bytes));
	trailing_pad = len >= MESSAGES_DETAIL_TRAILING_PAD_THRESHOLD;
	max_len = buf_size > (trailing_pad ? 5U : 3U) ? buf_size - (trailing_pad ? 5U : 3U) : 0U;
	len = MIN(len, max_len);
	memcpy(&buf[2], msg->payload.bytes, len);
	if (trailing_pad) {
		buf[2U + len] = '\n';
		buf[3U + len] = '\n';
		buf[4U + len] = '\n';
		buf[5U + len] = '\0';
	} else {
		buf[2U + len] = '\0';
	}
}

static uint16_t messages_detail_line_height(struct zui_draw_ctx *draw)
{
	return MAX(zui_draw_font_height(draw), 1U);
}

#if defined(CONFIG_ZUI_TEXT_UTF8)
static size_t messages_utf8_char_len(const char *text, size_t remaining)
{
	const uint8_t *bytes = (const uint8_t *)text;

	if (bytes == NULL || remaining == 0U || bytes[0] == '\0') {
		return 0U;
	}
	if ((bytes[0] & 0x80U) == 0U) {
		return 1U;
	}
	if ((bytes[0] & 0xe0U) == 0xc0U && remaining >= 2U &&
	    (bytes[1] & 0xc0U) == 0x80U) {
		return 2U;
	}
	if ((bytes[0] & 0xf0U) == 0xe0U && remaining >= 3U &&
	    (bytes[1] & 0xc0U) == 0x80U &&
	    (bytes[2] & 0xc0U) == 0x80U) {
		return 3U;
	}
	if ((bytes[0] & 0xf8U) == 0xf0U && remaining >= 4U &&
	    (bytes[1] & 0xc0U) == 0x80U &&
	    (bytes[2] & 0xc0U) == 0x80U && (bytes[3] & 0xc0U) == 0x80U) {
		return 4U;
	}

	return 1U;
}

static uint16_t messages_utf8_glyph_width(struct zui_draw_ctx *draw, const char *text,
					  size_t len)
{
	char glyph[5];

	len = MIN(len, sizeof(glyph) - 1U);
	memcpy(glyph, text, len);
	glyph[len] = '\0';
	return zui_draw_text_width(draw, glyph);
}
#endif

static const char *messages_detail_next_line(struct zui_draw_ctx *draw, const char *text)
{
	const char *cursor = text;
#if defined(CONFIG_ZUI_TEXT_UTF8)
	const char *end;
#endif
	uint16_t line_width = 0U;

	if (text == NULL || text[0] == '\0') {
		return text;
	}

#if defined(CONFIG_ZUI_TEXT_UTF8)
	end = text + strlen(text);
#endif
	while (cursor[0] != '\0') {
		uint16_t glyph_width;
		size_t glyph_len;

		if (cursor[0] == '\n') {
			return cursor + 1;
		}

#if defined(CONFIG_ZUI_TEXT_UTF8)
		glyph_len = messages_utf8_char_len(cursor, (size_t)(end - cursor));
		glyph_width = messages_utf8_glyph_width(draw, cursor, glyph_len);
#else
		glyph_len = 1U;
		glyph_width = zui_draw_glyph_width(draw, (uint8_t)cursor[0]);
#endif
		if (line_width + glyph_width > MESSAGES_DETAIL_TEXT_W) {
			return cursor == text ? cursor + glyph_len : cursor;
		}

		line_width += glyph_width;
		cursor += glyph_len;
	}

	return cursor;
}

static size_t messages_detail_visual_line_count(struct zui_draw_ctx *draw, const char *text)
{
	const char *cursor = text;
	size_t count = 0U;

	if (text == NULL || text[0] == '\0') {
		return 0U;
	}

	while (cursor[0] != '\0') {
		const char *next = messages_detail_next_line(draw, cursor);

		count++;
		if (next <= cursor) {
			break;
		}
		cursor = next;
	}

	return count;
}

static const char *messages_detail_line_at(struct zui_draw_ctx *draw, const char *text,
					   size_t line)
{
	const char *cursor = text;

	if (text == NULL) {
		return NULL;
	}

	for (size_t i = 0U; i < line && cursor[0] != '\0'; i++) {
		const char *next = messages_detail_next_line(draw, cursor);

		if (next <= cursor) {
			break;
		}
		cursor = next;
	}

	return cursor;
}

static void messages_detail_copy_line(struct zui_draw_ctx *draw, const char *start, char *line,
				      size_t line_size)
{
	const char *end;
	size_t len;

	if (line == NULL || line_size == 0U) {
		return;
	}

	line[0] = '\0';
	if (start == NULL || start[0] == '\0') {
		return;
	}

	end = messages_detail_next_line(draw, start);
	len = (size_t)(end - start);
	if (len > 0U && start[len - 1U] == '\n') {
		len--;
	}
	len = MIN(len, line_size - 1U);
	memcpy(line, start, len);
	line[len] = '\0';
}

static void messages_target_label(char *buf, size_t buf_size,
				  const struct zui_messages_target_entry *target)
{
	uint8_t p0 = 0U;
	uint8_t p1 = 0U;
	uint8_t p2 = 0U;

	if (buf == NULL || buf_size == 0U || target == NULL) {
		return;
	}
	if (target->kind == MESSAGES_TARGET_KIND_CHANNEL) {
		(void)snprintk(buf, buf_size, DESKTOP_TEXT_MESSAGES_TARGET_CHANNEL_FORMAT,
			       target->name, (unsigned int)target->channel_idx);
		return;
	}

	p0 = target->node_key_prefix[0];
	if (CONFIG_MESHBUS_CONTACT_PREFIX_BYTES > 1U) {
		p1 = target->node_key_prefix[1];
	}
	if (CONFIG_MESHBUS_CONTACT_PREFIX_BYTES > 2U) {
		p2 = target->node_key_prefix[2];
	}
	(void)snprintk(buf, buf_size, DESKTOP_TEXT_MESSAGES_TARGET_CONTACT_FORMAT, target->name, p0,
		       p1, p2);
}

static bool messages_target_from_channel(struct zui_messages_target_entry *target, size_t index,
					 const meshbus_channel *channel)
{
	if (target == NULL || channel == NULL ||
	    channel->secret.size < MESHBUS_MESSAGE_CHANNEL_SECRET_PREFIX_BYTES) {
		return false;
	}

	memset(target, 0, sizeof(*target));
	target->kind = MESSAGES_TARGET_KIND_CHANNEL;
	target->channel_idx = (uint8_t)MIN(index, (size_t)UINT8_MAX);
	memcpy(target->channel_secret_prefix, channel->secret.bytes,
	       sizeof(target->channel_secret_prefix));
	if (channel->name[0] != '\0') {
		(void)snprintk(target->name, sizeof(target->name), "%.*s",
			       (int)(sizeof(target->name) - 1U), channel->name);
	} else {
		messages_hex_prefix(target->name, sizeof(target->name),
				    target->channel_secret_prefix,
				    sizeof(target->channel_secret_prefix));
	}

	return true;
}

static bool messages_target_from_node(struct zui_messages_target_entry *target,
				      const meshbus_contact *node)
{
	if (target == NULL || node == NULL ||
	    node->public_key.size < CONFIG_MESHBUS_CONTACT_PREFIX_BYTES) {
		return false;
	}

	memset(target, 0, sizeof(*target));
	target->kind = MESSAGES_TARGET_KIND_NODE;
	memcpy(target->node_key_prefix, node->public_key.bytes, sizeof(target->node_key_prefix));
	if (node->alias[0] != '\0') {
		(void)snprintk(target->name, sizeof(target->name), "%.*s",
			       (int)(sizeof(target->name) - 1U), node->alias);
	} else if (node->name[0] != '\0') {
		(void)snprintk(target->name, sizeof(target->name), "%.*s",
			       (int)(sizeof(target->name) - 1U), node->name);
	} else {
		messages_hex_prefix(target->name, sizeof(target->name), target->node_key_prefix,
				    sizeof(target->node_key_prefix));
	}

	return true;
}

static void messages_prepare_target_row(struct messages_target_row *row,
					const struct zui_messages_target_entry *target,
					bool source_is_node, uint8_t source_index)
{
	if (row == NULL || target == NULL) {
		return;
	}

	memset(row, 0, sizeof(*row));
	row->target = *target;
	row->icon = target->kind == MESSAGES_TARGET_KIND_CHANNEL ? &I_store_8x8 : &I_radio_7x8;
	row->source_is_node = source_is_node;
	row->source_index = source_index;
	messages_target_label(row->label, sizeof(row->label), &row->target);
}

static void messages_compose_enforce_target_route(struct messages_app *app)
{
	if (app != NULL && app->compose_target_valid &&
	    app->compose_target.kind == MESSAGES_TARGET_KIND_CHANNEL) {
		app->compose_route = MESSAGES_ROUTE_FLOOD;
	}
}

static bool messages_target_from_channel_prefix(struct zui_messages_target_entry *target,
						const uint8_t *prefix, size_t prefix_len)
{
	uint8_t channel_size;

	if (target == NULL || prefix == NULL || prefix_len == 0U) {
		return false;
	}

	channel_size = meshbus_channel_store_size();
	for (uint8_t i = 0U; i < channel_size; i++) {
		meshbus_channel channel = meshbus_Channel_init_zero;

		if (meshbus_channel_get(i, &channel) != 0 ||
		    channel.secret.size < prefix_len) {
			continue;
		}
		if (memcmp(channel.secret.bytes, prefix, prefix_len) != 0) {
			continue;
		}

		return messages_target_from_channel(target, i, &channel);
	}

	return false;
}

static bool messages_target_from_node_prefix(struct zui_messages_target_entry *target,
					     const uint8_t *prefix, size_t prefix_len,
					     const char *fallback_name)
{
	meshbus_contact contact = meshbus_Contact_init_zero;

	if (target == NULL || prefix == NULL || prefix_len < CONFIG_MESHBUS_CONTACT_PREFIX_BYTES) {
		return false;
	}

	if (meshbus_contact_find_by_prefix(prefix, &contact) == 0) {
		return messages_target_from_node(target, &contact);
	}

	memset(target, 0, sizeof(*target));
	target->kind = MESSAGES_TARGET_KIND_NODE;
	memcpy(target->node_key_prefix, prefix, sizeof(target->node_key_prefix));
	if (fallback_name != NULL && fallback_name[0] != '\0') {
		(void)snprintk(target->name, sizeof(target->name), "%.*s",
			       (int)(sizeof(target->name) - 1U), fallback_name);
	} else {
		messages_hex_prefix(target->name, sizeof(target->name), target->node_key_prefix,
				    sizeof(target->node_key_prefix));
	}

	return true;
}

static bool messages_reply_target_from_entry(struct zui_messages_target_entry *target,
					     enum zui_messages_route_mode *route,
					     const struct messages_entry *entry)
{
	const meshbus_message_content *message;
	size_t target_len;

	if (target == NULL || route == NULL || entry == NULL) {
		return false;
	}

	message = &entry->message;
	target_len = MIN((size_t)message->target.size, (size_t)sizeof(message->target.bytes));
	if (messages_is_channel_message(message)) {
		if (!messages_target_from_channel_prefix(target, message->target.bytes, target_len)) {
			return false;
		}
		*route = MESSAGES_ROUTE_FLOOD;
		return true;
	}

	if (!messages_target_from_node_prefix(target, message->target.bytes, target_len,
					      message->sender_name)) {
		return false;
	}
	*route = MESSAGES_ROUTE_DIRECT;
	return true;
}

static void messages_append_target_row(struct messages_app *app,
				       const struct messages_target_row *row)
{
	if (app == NULL || row == NULL) {
		return;
	}
	if (app->target_count >= ARRAY_SIZE(app->target_rows)) {
		memmove(&app->target_rows[0], &app->target_rows[1],
			(ARRAY_SIZE(app->target_rows) - 1U) * sizeof(app->target_rows[0]));
		app->target_count = ARRAY_SIZE(app->target_rows) - 1U;
	}

	app->target_rows[app->target_count++] = *row;
}

static void messages_prepend_target_row(struct messages_app *app,
					const struct messages_target_row *row)
{
	if (app == NULL || row == NULL) {
		return;
	}
	if (app->target_count >= ARRAY_SIZE(app->target_rows)) {
		app->target_count = ARRAY_SIZE(app->target_rows) - 1U;
	}
	memmove(&app->target_rows[1], &app->target_rows[0],
		app->target_count * sizeof(app->target_rows[0]));
	app->target_rows[0] = *row;
	app->target_count++;
}

static bool messages_target_scan_next(struct messages_app *app, struct messages_target_row *row,
				      bool *done)
{
	if (app == NULL || row == NULL || done == NULL) {
		return false;
	}

	*done = false;
	while (true) {
		if (!app->target_scan_node) {
			if (app->target_scan_index >= app->target_channel_size) {
				app->target_scan_node = true;
				app->target_scan_index = 0;
				continue;
			}

			meshbus_channel channel = meshbus_Channel_init_zero;
			struct zui_messages_target_entry target;
			uint8_t index = (uint8_t)app->target_scan_index++;
			int rc = meshbus_channel_get(index, &channel);

			LOG_DBG("messages app: meshbus_channel_get(index=%u) -> %d",
				(unsigned int)index, rc);
			if (rc == 0 && messages_target_from_channel(&target, index, &channel)) {
				messages_prepare_target_row(row, &target, false, index);
				return true;
			}
			return false;
		}

		if (app->target_scan_index >= app->target_node_size) {
			*done = true;
			return false;
		}

		meshbus_contact node = meshbus_Contact_init_zero;
		struct zui_messages_target_entry target;
		uint8_t index = (uint8_t)app->target_scan_index++;
		int rc = meshbus_contact_get(index, &node);

		LOG_DBG("messages app: meshbus_contact_get(index=%u) -> %d",
			(unsigned int)index, rc);
		if (rc == 0 && messages_target_from_node(&target, &node)) {
			messages_prepare_target_row(row, &target, true, index);
			return true;
		}
		return false;
	}
}

static bool messages_target_scan_prev(struct messages_app *app, struct messages_target_row *row,
				      bool *done)
{
	if (app == NULL || row == NULL || done == NULL) {
		return false;
	}

	*done = false;
	while (true) {
		if (app->target_scan_node) {
			if (app->target_scan_index < 0) {
				app->target_scan_node = false;
				app->target_scan_index = (int16_t)app->target_channel_size - 1;
				continue;
			}

			meshbus_contact node = meshbus_Contact_init_zero;
			struct zui_messages_target_entry target;
			uint8_t index = (uint8_t)app->target_scan_index--;
			int rc = meshbus_contact_get(index, &node);

			LOG_DBG("messages app: meshbus_contact_get(index=%u) -> %d",
				(unsigned int)index, rc);
			if (rc == 0 && messages_target_from_node(&target, &node)) {
				messages_prepare_target_row(row, &target, true, index);
				return true;
			}
			return false;
		}

		if (app->target_scan_index < 0) {
			*done = true;
			return false;
		}

		meshbus_channel channel = meshbus_Channel_init_zero;
		struct zui_messages_target_entry target;
		uint8_t index = (uint8_t)app->target_scan_index--;
		int rc = meshbus_channel_get(index, &channel);

		LOG_DBG("messages app: meshbus_channel_get(index=%u) -> %d", (unsigned int)index,
			rc);
		if (rc == 0 && messages_target_from_channel(&target, index, &channel)) {
			messages_prepare_target_row(row, &target, false, index);
			return true;
		}
		return false;
	}
}

static void messages_target_load_stop(struct messages_app *app, bool clear_loading)
{
	if (app == NULL) {
		return;
	}

	app->target_load_mode = MESSAGES_TARGET_LOAD_IDLE;
	app->target_load_active = false;
	if (clear_loading) {
		app->target_loading = false;
	}
}

static void messages_target_schedule_load(struct messages_app *app, k_timeout_t delay)
{
	if (app == NULL || !app->target_load_active) {
		return;
	}

	(void)k_work_schedule(&app->target_load_work, delay);
}

static void messages_target_load_work(struct k_work *work)
{
	struct k_work_delayable *delayable = k_work_delayable_from_work(work);
	struct messages_app *app = CONTAINER_OF(delayable, struct messages_app, target_load_work);
	struct messages_target_row row;
	bool done = false;
	bool found = false;
	bool was_loading;
	bool should_update;
	size_t selected = 0U;

	if (app == NULL || !app->target_load_active ||
	    app->current_screen != MESSAGES_SCREEN_TARGET) {
		return;
	}

	was_loading = app->target_loading;
	if (app->target_load_mode == MESSAGES_TARGET_LOAD_INITIAL ||
	    app->target_load_mode == MESSAGES_TARGET_LOAD_NEXT) {
		found = messages_target_scan_next(app, &row, &done);
		if (found) {
			messages_append_target_row(app, &row);
			app->target_has_next = true;
		}
	} else if (app->target_load_mode == MESSAGES_TARGET_LOAD_PREV) {
		found = messages_target_scan_prev(app, &row, &done);
		if (found) {
			messages_prepend_target_row(app, &row);
			app->target_has_prev = true;
		}
	} else {
		messages_target_load_stop(app, true);
		return;
	}

	if (app->target_loading && app->target_count >= MESSAGES_TARGET_VISIBLE_MAX) {
		app->target_loading = false;
	}

	switch (app->target_load_mode) {
	case MESSAGES_TARGET_LOAD_INITIAL:
		if (done) {
			app->target_has_next = false;
			messages_target_load_stop(app, true);
		} else if (app->target_count >= MESSAGES_TARGET_BUFFER_MAX) {
			messages_target_load_stop(app, true);
			app->target_has_next = true;
		} else {
			messages_target_schedule_load(app, MESSAGES_TARGET_LOAD_DELAY);
		}
		selected = 0U;
		break;
	case MESSAGES_TARGET_LOAD_NEXT:
		if (done) {
			app->target_has_next = false;
			messages_target_load_stop(app, true);
		} else if (found) {
			app->target_has_prev = true;
			messages_target_load_stop(app, true);
		} else {
			messages_target_schedule_load(app, MESSAGES_TARGET_LOAD_DELAY);
		}
		selected = app->target_count > 0U ? app->target_count - 1U : 0U;
		break;
	case MESSAGES_TARGET_LOAD_PREV:
		if (done) {
			app->target_has_prev = false;
			messages_target_load_stop(app, true);
		} else if (found) {
			app->target_has_next = true;
			messages_target_load_stop(app, true);
		} else {
			messages_target_schedule_load(app, MESSAGES_TARGET_LOAD_DELAY);
		}
		selected = 0U;
		break;
	default:
		messages_target_load_stop(app, true);
		return;
	}

	should_update = done || (was_loading != app->target_loading) ||
			(found && !app->target_loading);
	if (should_update) {
		messages_update_targets(app, selected);
		messages_request_redraw(app);
	}
}

static void messages_target_start_initial_load(struct messages_app *app)
{
	if (app == NULL) {
		return;
	}

	app->target_count = 0U;
	app->target_load_mode = MESSAGES_TARGET_LOAD_INITIAL;
	app->target_load_active = true;
	app->target_loading = true;
	app->target_scan_node = false;
	app->target_scan_index = 0;
	app->target_has_prev = false;
	app->target_has_next = true;
	app->target_channel_size = meshbus_channel_store_size();
	app->target_node_size = meshbus_contact_store_size();

	LOG_DBG("messages app: target async initial channel_size=%u contact_size=%u",
		(unsigned int)app->target_channel_size, (unsigned int)app->target_node_size);
}

static bool messages_target_start_next_load(struct messages_app *app)
{
	struct messages_target_row *last;

	if (app == NULL || app->target_load_active || app->target_count == 0U ||
	    !app->target_has_next) {
		return false;
	}

	last = &app->target_rows[app->target_count - 1U];
	app->target_load_mode = MESSAGES_TARGET_LOAD_NEXT;
	app->target_load_active = true;
	app->target_loading = true;
	app->target_scan_node = last->source_is_node;
	app->target_scan_index = (int16_t)last->source_index + 1;
	if (!last->source_is_node && app->target_scan_index >= app->target_channel_size) {
		app->target_scan_node = true;
		app->target_scan_index = 0;
	}
	messages_target_schedule_load(app, K_NO_WAIT);
	return true;
}

static bool messages_target_start_prev_load(struct messages_app *app)
{
	struct messages_target_row *first;

	if (app == NULL || app->target_load_active || app->target_count == 0U ||
	    !app->target_has_prev) {
		return false;
	}

	first = &app->target_rows[0];
	app->target_load_mode = MESSAGES_TARGET_LOAD_PREV;
	app->target_load_active = true;
	app->target_loading = true;
	app->target_scan_node = first->source_is_node;
	app->target_scan_index = (int16_t)first->source_index - 1;
	if (!first->source_is_node && app->target_scan_index < 0) {
		app->target_has_prev = false;
		messages_target_load_stop(app, true);
		return false;
	}
	messages_target_schedule_load(app, K_NO_WAIT);
	return true;
}

static void messages_open_targets(struct messages_app *app)
{
	if (app == NULL) {
		return;
	}

	(void)k_work_cancel_delayable(&app->target_load_work);
	messages_target_start_initial_load(app);
	messages_update_targets(app, 0U);
	messages_switch(app, MESSAGES_SCREEN_TARGET);
}

static int messages_send_execute(struct messages_app *app)
{
	const char *text;
	size_t len;
	int rc;

	if (app == NULL || !app->compose_target_valid) {
		return -EINVAL;
	}
	messages_compose_enforce_target_route(app);
	if (!messages_radio_tx_ready(app)) {
		return -EACCES;
	}

	text = messages_current_content(app);
	len = strlen(text);
	if (len == 0U || len > CONFIG_MESHBUS_MESSAGE_TX_MAX_LEN) {
		return -EINVAL;
	}

	if (app->compose_target.kind == MESSAGES_TARGET_KIND_CHANNEL) {
		rc = meshbus_message_send_to_channel(app->compose_target.channel_idx,
						     (const uint8_t *)text, len);
		LOG_DBG("messages app: meshbus_message_send_to_channel(channel=%u, len=%u) -> %d",
			(unsigned int)app->compose_target.channel_idx, (unsigned int)len, rc);
		return rc;
	}

	rc = meshbus_message_send_to_node(app->compose_target.node_key_prefix,
					  (const uint8_t *)text, len,
					  app->compose_route == MESSAGES_ROUTE_FLOOD,
					  messages_next_attempt(), NULL);
	LOG_DBG("messages app: meshbus_message_send_to_node(len=%u, flood=%d) -> %d",
		(unsigned int)len, app->compose_route == MESSAGES_ROUTE_FLOOD, rc);
	return rc;
}

static void messages_prepare_entry_row(struct messages_entry *entry)
{
	char source[24];

	if (entry == NULL) {
		return;
	}

	(void)messages_source(source, sizeof(source), &entry->message);
	(void)snprintk(entry->label, sizeof(entry->label),
		       DESKTOP_TEXT_MESSAGES_INBOX_ROW_FORMAT,
		       messages_row_kind(&entry->message), source);
	messages_format_time_ago(entry->detail, sizeof(entry->detail), entry->message.timestamp,
				 entry->timestamp_realtime);
}

static void messages_append_cached_entry(struct messages_app *app,
					 const struct desktop_messages_cache_entry *entry)
{
	if (app == NULL || entry == NULL || entry->message.payload.size == 0U ||
	    app->entry_count >= ARRAY_SIZE(app->entries)) {
		return;
	}

	app->entries[app->entry_count] = (struct messages_entry){
		.key = entry->key,
		.message = entry->message,
		.unread = entry->unread,
		.timestamp_realtime = entry->timestamp_realtime,
	};
	messages_prepare_entry_row(&app->entries[app->entry_count]);
	app->entry_count++;
}

static bool messages_find_entry_index_by_key(struct messages_app *app,
					     const struct desktop_messages_cache_key *key,
					     size_t *index_out)
{
	if (app == NULL || key == NULL || index_out == NULL) {
		return false;
	}

	for (size_t i = 0U; i < app->entry_count; i++) {
		if (desktop_messages_cache_key_equal(&app->entries[i].key, key)) {
			*index_out = i;
			return true;
		}
	}

	return false;
}

static void messages_load_cached_received(struct messages_app *app, bool preserve_selected)
{
	uint32_t count;
	uint32_t copy_count;
	int64_t start_ms;
	struct desktop_messages_cache_key selected_key = {0};
	size_t selected_index = 0U;
	bool had_selected = false;

	if (app == NULL) {
		return;
	}

	if (preserve_selected && app->selected_entry_valid && app->selected_entry < app->entry_count) {
		selected_key = app->entries[app->selected_entry].key;
		had_selected = true;
	}

	start_ms = k_uptime_get();
	app->entry_count = 0U;
	app->selected_entry_valid = false;
	count = desktop_messages_cache_received_count();
	copy_count = MIN(count, (uint32_t)ARRAY_SIZE(app->entries));

	for (uint32_t pos = copy_count; pos > 0U; pos--) {
		struct desktop_messages_cache_entry entry;

		if (desktop_messages_cache_copy_received(pos - 1U, &entry)) {
			messages_append_cached_entry(app, &entry);
		}
	}
	app->cache_update_seq = desktop_messages_cache_update_seq();

	if (had_selected &&
	    messages_find_entry_index_by_key(app, &selected_key, &selected_index)) {
		app->selected_entry = selected_index;
		app->selected_entry_valid = true;
	}

	LOG_DBG("messages app: loaded desktop_messages_cache received=%u copied=%u elapsed=%lldms",
		(unsigned int)count, (unsigned int)app->entry_count,
		(long long)(k_uptime_get() - start_ms));
}

static void messages_response_refresh_work(struct k_work *work)
{
	struct k_work_delayable *delayable = k_work_delayable_from_work(work);
	struct messages_app *app =
		CONTAINER_OF(delayable, struct messages_app, response_refresh_work);
	uint32_t update_seq;

	if (app == NULL || app->exit_requested) {
		return;
	}

	update_seq = desktop_messages_cache_update_seq();
	if (update_seq == app->cache_update_seq) {
		return;
	}

	messages_load_cached_received(app, true);
	if (app->current_screen == MESSAGES_SCREEN_INBOX) {
		messages_update_inbox(app, true);
		messages_request_redraw(app);
	}
}

static void messages_response_listener_done(void)
{
	if (atomic_dec(&messages_response_inflight) == 1) {
		k_sem_give(&messages_response_idle);
	}
}

static void messages_response_wait_idle(void)
{
	while (k_sem_take(&messages_response_idle, K_NO_WAIT) == 0) {
	}
	while (atomic_get(&messages_response_inflight) > 0) {
		(void)k_sem_take(&messages_response_idle, K_FOREVER);
	}
}

static void messages_response_listener_cb(const struct zbus_channel *chan)
{
	struct messages_app *app;

	atomic_inc(&messages_response_inflight);
	if (chan != &meshbus_message_response_chan) {
		goto out;
	}

	app = (struct messages_app *)atomic_ptr_get(&messages_active_app);
	if (app == NULL) {
		goto out;
	}

	LOG_DBG("messages app: meshbus_message_response_chan refresh signal");
	(void)k_work_schedule(&app->response_refresh_work, MESSAGES_RESPONSE_REFRESH_DELAY);

out:
	messages_response_listener_done();
}

ZBUS_LISTENER_DEFINE_WITH_ENABLE(meshbus_desktop_messages_response_listener,
				 messages_response_listener_cb, false);
ZBUS_CHAN_ADD_OBS(meshbus_message_response_chan,
		  meshbus_desktop_messages_response_listener, 3);

static void messages_update_compose(struct messages_app *app)
{
	size_t idx = 0U;
	const char *content;
	char target[MESSAGES_VALUE_MAX];

	if (app == NULL || app->compose == NULL) {
		return;
	}

	messages_compose_enforce_target_route(app);
	content = messages_current_content(app);
	if (app->compose_target_valid) {
		messages_target_label(target, sizeof(target), &app->compose_target);
	} else {
		(void)snprintk(target, sizeof(target), "%s", DESKTOP_TEXT_MESSAGES_NO_TARGET);
	}

#define ADD_FORM(_id, _label, _value, _options, _option_count, _option_index)              \
	do {                                                                               \
		if (idx < ARRAY_SIZE(app->compose_items)) {                                \
			messages_compose_set_value(app, idx, (_value));                    \
			app->compose_items[idx++] = (struct zui_form_item){                \
				.id = (_id),                                                \
				.label = (_label),                                          \
				.value_text = app->compose_values[idx - 1U],                 \
				.options = (_options),                                      \
				.option_count = (_option_count),                            \
				.option_index = (_option_index),                            \
			};                                                                 \
		}                                                                          \
	} while (0)

	ADD_FORM(MESSAGES_COMPOSE_TO, DESKTOP_TEXT_MESSAGES_LABEL_TO, target, NULL, 0U, 0U);
	ADD_FORM(MESSAGES_COMPOSE_ROUTE, DESKTOP_TEXT_MESSAGES_LABEL_ROUTE,
		 DESKTOP_TEXT_MESSAGES_ROUTE_VALUES[app->compose_route], DESKTOP_TEXT_MESSAGES_ROUTE_VALUES,
		 app->compose_target_valid && app->compose_target.kind == MESSAGES_TARGET_KIND_CHANNEL ?
			 1U :
			 DESKTOP_TEXT_MESSAGES_ROUTE_COUNT,
		 app->compose_route);
	ADD_FORM(MESSAGES_COMPOSE_CONTENT, DESKTOP_TEXT_MESSAGES_LABEL_CONTENT, content,
		 DESKTOP_TEXT_MESSAGES_CONTENT_VALUES, DESKTOP_TEXT_MESSAGES_CONTENT_OPTION_COUNT,
		 app->compose_custom ? DESKTOP_TEXT_MESSAGES_CONTENT_OPTION_COUNT - 1U :
				       app->compose_canned_idx % DESKTOP_TEXT_MESSAGES_CANNED_COUNT);
	ADD_FORM(MESSAGES_COMPOSE_EDIT, DESKTOP_TEXT_MESSAGES_LABEL_EDIT_CONTENT,
		 DESKTOP_TEXT_COMMON_EMPTY, NULL, 0U, 0U);
	ADD_FORM(MESSAGES_COMPOSE_SEND, DESKTOP_TEXT_MESSAGES_ACTION_SEND, DESKTOP_TEXT_COMMON_EMPTY,
		 NULL, 0U, 0U);
#undef ADD_FORM

	(void)zui_form_update(app->compose, &(struct zui_form_config){
		.title = DESKTOP_TEXT_MESSAGES_NEW_MESSAGE,
		.items = app->compose_items,
		.item_count = idx,
		.changed = messages_compose_changed,
		.activated = messages_compose_activated,
		.user_data = app,
	});
}

static void messages_update_inbox(struct messages_app *app, bool preserve_selection)
{
	size_t selected = 0U;

	if (app == NULL || app->inbox == NULL) {
		return;
	}
	if (preserve_selection) {
		selected = zui_sublist_selected(app->inbox);
	}

	(void)zui_sublist_update(app->inbox, &(struct zui_sublist_config){
		.title = DESKTOP_TEXT_MESSAGES_INBOX,
		.count = messages_inbox_count,
		.get_item = messages_inbox_item,
		.selected = messages_inbox_selected,
		.user_data = app,
	});

	if (app->entry_count > 0U && selected >= app->entry_count) {
		selected = app->entry_count - 1U;
	}
	(void)zui_sublist_select(app->inbox, selected);
}

static size_t messages_inbox_count(void *user_data)
{
	struct messages_app *app = user_data;

	if (app == NULL || app->entry_count == 0U) {
		return 1U;
	}

	return app->entry_count;
}

static int messages_inbox_item(size_t index, struct zui_list_item *item, void *user_data)
{
	struct messages_app *app = user_data;
	struct messages_entry *entry;
	size_t newest_index;

	if (app == NULL || item == NULL) {
		return -EINVAL;
	}

	memset(item, 0, sizeof(*item));
	if (app->entry_count == 0U) {
		if (index > 0U) {
			return -ENOENT;
		}
		*item = (struct zui_list_item){
			.id = 0U,
			.label = DESKTOP_TEXT_MESSAGES_NO_MORE_MESSAGES,
		};
		return 0;
	}
	if (index >= app->entry_count) {
		return -ENOENT;
	}

	newest_index = app->entry_count - 1U - index;
	entry = &app->entries[newest_index];
	*item = (struct zui_list_item){
		.id = (uint32_t)index + 1U,
		.label = entry->label,
		.detail = entry->detail,
		.icon = entry->unread ? &I_message_8x8 : NULL,
	};
	return 0;
}

static size_t messages_target_count(void *user_data)
{
	struct messages_app *app = user_data;

	if (app == NULL) {
		return 1U;
	}
	if (app->target_loading && app->target_count < MESSAGES_TARGET_VISIBLE_MAX) {
		return 1U;
	}
	if (app->target_count == 0U) {
		return 1U;
	}

	return app->target_count;
}

static int messages_target_item(size_t index, struct zui_list_item *item, void *user_data)
{
	struct messages_app *app = user_data;
	struct messages_target_row *row;

	if (app == NULL || item == NULL) {
		return -EINVAL;
	}

	memset(item, 0, sizeof(*item));
	if (app->target_loading && app->target_count < MESSAGES_TARGET_VISIBLE_MAX) {
		if (index > 0U) {
			return -ENOENT;
		}
		*item = (struct zui_list_item){
			.id = 0U,
			.label = DESKTOP_TEXT_MESSAGES_LOADING,
		};
		return 0;
	}
	if (app->target_count == 0U) {
		if (index > 0U) {
			return -ENOENT;
		}
		*item = (struct zui_list_item){
			.id = 0U,
			.label = DESKTOP_TEXT_MESSAGES_NO_TARGET,
		};
		return 0;
	}
	if (index >= app->target_count) {
		return -ENOENT;
	}

	row = &app->target_rows[index];
	*item = (struct zui_list_item){
		.id = (uint32_t)index + 1U,
		.label = row->label,
		.icon = row->icon,
	};
	return 0;
}

static void messages_update_targets(struct messages_app *app, size_t selected)
{
	if (app == NULL || app->targets == NULL) {
		return;
	}

	(void)zui_sublist_update(app->targets, &(struct zui_sublist_config){
		.title = DESKTOP_TEXT_MESSAGES_SELECT_TARGET,
		.count = messages_target_count,
		.get_item = messages_target_item,
		.selected = messages_target_selected,
		.user_data = app,
	});
	if (app->target_count > 0U && selected >= app->target_count) {
		selected = app->target_count - 1U;
	}
	if (app->target_loading && app->target_count < MESSAGES_TARGET_VISIBLE_MAX) {
		selected = 0U;
	}
	(void)zui_sublist_select(app->targets, selected);
}

static void messages_prepare_detail(struct messages_app *app)
{
	struct messages_entry *entry;

	if (app == NULL) {
		return;
	}

	app->detail_scroll = 0U;
	app->detail_max_scroll = 0U;

	if (!app->selected_entry_valid || app->selected_entry >= app->entry_count) {
		(void)snprintk(app->detail_title, sizeof(app->detail_title), "%s",
			       DESKTOP_TEXT_COMMON_EMPTY);
		app->detail_time[0] = '\0';
		(void)snprintk(app->detail_text, sizeof(app->detail_text), "%s",
			       DESKTOP_TEXT_MESSAGES_DETAIL_NO_MESSAGE);
	} else {
		entry = &app->entries[app->selected_entry];
		messages_detail_source(app->detail_title, sizeof(app->detail_title), &entry->message);
		messages_format_time_ago(app->detail_time, sizeof(app->detail_time),
					 entry->message.timestamp, entry->timestamp_realtime);
		if (entry->unread) {
			entry->unread = false;
			messages_prepare_entry_row(entry);
			desktop_messages_cache_mark_read(&entry->key);
			LOG_DBG("messages app: desktop_messages_cache_mark_read() -> 0");
		}
		messages_detail_payload_text(app->detail_text, sizeof(app->detail_text),
					     &entry->message);
	}
}

static bool messages_forward_input(struct zui_screen *screen, const struct zui_input_event *event)
{
	return zui_screen_submit_input(screen, event) > 0;
}

static void messages_forward_enter(struct zui_screen *screen)
{
	if (screen != NULL) {
		(void)zui_screen_enter(screen);
	}
}

static void messages_forward_exit(struct zui_screen *screen)
{
	if (screen != NULL) {
		(void)zui_screen_exit(screen);
	}
}

static void messages_menu_selected(struct zui_sublist *list, uint32_t id, size_t index,
				   const struct zui_input_event *event, void *user_data)
{
	struct messages_app *app = user_data;

	ARG_UNUSED(list);
	ARG_UNUSED(index);

	if (app == NULL || !messages_is_click(event)) {
		return;
	}

	switch (id) {
	case MESSAGES_MENU_INBOX:
		messages_switch(app, MESSAGES_SCREEN_INBOX);
		break;
	case MESSAGES_MENU_COMPOSE:
		messages_reset_compose_draft(app);
		messages_update_compose(app);
		(void)zui_form_select(app->compose, 0U);
		messages_switch(app, MESSAGES_SCREEN_COMPOSE);
		break;
	default:
		break;
	}
}

static void messages_inbox_selected(struct zui_sublist *list, uint32_t id, size_t index,
				    const struct zui_input_event *event, void *user_data)
{
	struct messages_app *app = user_data;
	size_t newest_index;

	ARG_UNUSED(list);
	ARG_UNUSED(id);

	if (app == NULL || !messages_is_click(event) || app->entry_count == 0U ||
	    index >= app->entry_count) {
		return;
	}

	newest_index = app->entry_count - 1U - index;
	app->selected_entry = newest_index;
	app->selected_entry_valid = true;
	messages_prepare_detail(app);
	messages_switch(app, MESSAGES_SCREEN_DETAIL);
}

static void messages_target_selected(struct zui_sublist *list, uint32_t id, size_t index,
				     const struct zui_input_event *event, void *user_data)
{
	struct messages_app *app = user_data;
	struct messages_target_row *row;

	ARG_UNUSED(list);
	ARG_UNUSED(id);

	if (app == NULL || !messages_is_click(event) || app->target_count == 0U ||
	    (app->target_loading && app->target_count < MESSAGES_TARGET_VISIBLE_MAX) ||
	    index >= app->target_count) {
		return;
	}

	row = &app->target_rows[index];
	app->compose_target = row->target;
	app->compose_target_valid = true;
	messages_compose_enforce_target_route(app);
	messages_update_compose(app);
	messages_switch(app, MESSAGES_SCREEN_COMPOSE);
}

static void messages_compose_changed(struct zui_form *form, uint32_t id, size_t option_index,
				     void *user_data)
{
	struct messages_app *app = user_data;

	ARG_UNUSED(form);

	if (app == NULL) {
		return;
	}

	if (id == MESSAGES_COMPOSE_ROUTE) {
		if (app->compose_target_valid &&
		    app->compose_target.kind == MESSAGES_TARGET_KIND_CHANNEL) {
			app->compose_route = MESSAGES_ROUTE_FLOOD;
		} else {
			app->compose_route = (enum zui_messages_route_mode)
				(option_index % DESKTOP_TEXT_MESSAGES_ROUTE_COUNT);
		}
		messages_update_compose(app);
		messages_request_redraw(app);
	} else if (id == MESSAGES_COMPOSE_CONTENT) {
		if (option_index < DESKTOP_TEXT_MESSAGES_CANNED_COUNT) {
			app->compose_canned_idx = (uint8_t)option_index;
			app->compose_custom = false;
		} else {
			app->compose_custom = true;
		}
		messages_update_compose(app);
		messages_request_redraw(app);
	}
}

static void messages_compose_activated(struct zui_form *form, uint32_t id,
				       const struct zui_input_event *event, void *user_data)
{
	struct messages_app *app = user_data;

	ARG_UNUSED(form);

	if (app == NULL || !messages_is_click(event)) {
		return;
	}

	switch (id) {
	case MESSAGES_COMPOSE_TO:
		messages_open_targets(app);
		break;
	case MESSAGES_COMPOSE_EDIT:
		messages_sync_content_buffer(app);
		(void)zui_text_editor_update(app->content_editor,
					      &(struct zui_text_editor_config){
						      .title = DESKTOP_TEXT_MESSAGES_LABEL_EDIT_CONTENT,
						      .buffer = app->content,
						      .buffer_size = sizeof(app->content),
						      .submitted = messages_content_submitted,
						      .user_data = app,
					      });
		messages_switch(app, MESSAGES_SCREEN_CONTENT);
		break;
	case MESSAGES_COMPOSE_SEND:
		if (!app->compose_target_valid) {
			messages_toast(app, DESKTOP_TEXT_MESSAGES_SEND_FAILED,
				       DESKTOP_TEXT_MESSAGES_NO_TARGET, &I_error_24x24, 1500U);
			break;
		}
		if (!messages_radio_tx_ready(app)) {
			break;
		}
		messages_send_activate(app);
		break;
	default:
		break;
	}
}

static void messages_content_submitted(struct zui_text_editor *editor, const char *text,
				       void *user_data)
{
	struct messages_app *app = user_data;

	ARG_UNUSED(editor);

	if (app == NULL) {
		return;
	}

	if (text != NULL) {
		(void)snprintk(app->content, sizeof(app->content), "%s", text);
	}
	app->compose_custom = true;
	messages_update_compose(app);
	messages_switch(app, MESSAGES_SCREEN_COMPOSE);
}

static void messages_open_reply(struct messages_app *app)
{
	struct zui_messages_target_entry target;
	enum zui_messages_route_mode route;

	if (app == NULL || !app->selected_entry_valid || app->selected_entry >= app->entry_count) {
		return;
	}

	if (!messages_reply_target_from_entry(&target, &route, &app->entries[app->selected_entry])) {
		messages_toast(app, DESKTOP_TEXT_MESSAGES_SEND_FAILED,
			       DESKTOP_TEXT_MESSAGES_NO_TARGET, &I_error_24x24, 1500U);
		return;
	}

	app->compose_target = target;
	app->compose_target_valid = true;
	app->compose_route = route;
	messages_compose_enforce_target_route(app);
	messages_update_compose(app);
	messages_switch(app, MESSAGES_SCREEN_COMPOSE);
}

static void messages_send_activate(struct messages_app *app)
{
	int rc;

	if (app == NULL) {
		return;
	}

	rc = messages_send_execute(app);
	if (rc == 0) {
		messages_toast(app, DESKTOP_TEXT_MESSAGES_SENT, DESKTOP_TEXT_MESSAGES_SENT_MESSAGE,
			       &I_done_24x24, 900U);
		messages_switch(app, MESSAGES_SCREEN_MENU);
	} else {
		LOG_WRN("messages send failed rc=%d", rc);
		messages_toast(app, DESKTOP_TEXT_MESSAGES_SEND_FAILED,
			       DESKTOP_TEXT_MESSAGES_SEND_FAILED_MESSAGE, &I_error_24x24, 1500U);
		messages_switch(app, MESSAGES_SCREEN_COMPOSE);
	}
}

static void messages_menu_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct messages_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_draw(zui_sublist_get_screen(app->menu), draw);
	}
}

static bool messages_menu_input(const struct zui_input_event *event, void *user_data)
{
	struct messages_app *app = user_data;

	if (app == NULL) {
		return false;
	}
	if (event == NULL) {
		return false;
	}
	if (messages_should_consume_edge(event)) {
		return true;
	}
	if (messages_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		messages_exit(app);
		return true;
	}
	if (messages_forward_input(zui_sublist_get_screen(app->menu), event)) {
		messages_request_redraw(app);
		return true;
	}

	return false;
}

static void messages_menu_enter(void *user_data)
{
	struct messages_app *app = user_data;

	if (app != NULL) {
		messages_forward_enter(zui_sublist_get_screen(app->menu));
	}
}

static void messages_menu_exit(void *user_data)
{
	struct messages_app *app = user_data;

	if (app != NULL) {
		messages_forward_exit(zui_sublist_get_screen(app->menu));
	}
}

static void messages_inbox_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct messages_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_draw(zui_sublist_get_screen(app->inbox), draw);
	}
}

static bool messages_inbox_input(const struct zui_input_event *event, void *user_data)
{
	struct messages_app *app = user_data;

	if (app == NULL) {
		return false;
	}
	if (event == NULL) {
		return false;
	}
	if (messages_should_consume_edge(event)) {
		return true;
	}
	if (messages_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		messages_switch(app, MESSAGES_SCREEN_MENU);
		return true;
	}
	if (messages_forward_input(zui_sublist_get_screen(app->inbox), event)) {
		messages_request_redraw(app);
		return true;
	}

	return false;
}

static void messages_inbox_enter(void *user_data)
{
	struct messages_app *app = user_data;

	if (app != NULL) {
		messages_load_cached_received(app, true);
		messages_update_inbox(app, true);
		messages_forward_enter(zui_sublist_get_screen(app->inbox));
	}
}

static void messages_inbox_exit(void *user_data)
{
	struct messages_app *app = user_data;

	if (app != NULL) {
		messages_forward_exit(zui_sublist_get_screen(app->inbox));
	}
}

static void messages_detail_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct messages_app *app = user_data;
	const char *line_start;
	uint16_t line_height;
	size_t lines_on_screen;
	size_t line_count;

	zui_draw_reset(draw);
	if (app == NULL) {
		return;
	}

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	zui_draw_text_aligned(draw, (struct zui_point){.x = MESSAGES_DETAIL_TITLE_X,
						       .y = MESSAGES_DETAIL_TITLE_Y},
			      ZUI_ALIGN_LEFT, ZUI_ALIGN_BOTTOM, app->detail_title);
	if (app->detail_time[0] != '\0') {
		zui_draw_text_aligned(draw, (struct zui_point){.x = MESSAGES_DETAIL_TIME_X,
							       .y = MESSAGES_DETAIL_TITLE_Y},
				      ZUI_ALIGN_RIGHT, ZUI_ALIGN_BOTTOM, app->detail_time);
	}

	zui_draw_round_rect(draw,
			    &(struct zui_rect){.x = MESSAGES_DETAIL_FRAME_X,
					       .y = MESSAGES_DETAIL_FRAME_Y,
					       .width = MESSAGES_DETAIL_FRAME_W,
					       .height = MESSAGES_DETAIL_FRAME_H},
			    5U);

	line_height = messages_detail_line_height(draw);
	lines_on_screen = MAX(MESSAGES_DETAIL_TEXT_H / line_height, 1U);
	line_count = messages_detail_visual_line_count(draw, app->detail_text);
	app->detail_max_scroll =
		line_count > lines_on_screen ? line_count - lines_on_screen : 0U;
	if (app->detail_scroll > app->detail_max_scroll) {
		app->detail_scroll = app->detail_max_scroll;
	}

	line_start = messages_detail_line_at(draw, app->detail_text, app->detail_scroll);
	zui_draw_set_clip(draw, &(struct zui_rect){.x = MESSAGES_DETAIL_TEXT_X,
						   .y = MESSAGES_DETAIL_TEXT_Y,
						   .width = MESSAGES_DETAIL_TEXT_W,
						   .height = MESSAGES_DETAIL_TEXT_H});
	for (size_t line_index = 0U;
	     line_start != NULL && line_start[0] != '\0' && line_index < lines_on_screen;
	     line_index++) {
		char line[64];
		const char *next = messages_detail_next_line(draw, line_start);

		messages_detail_copy_line(draw, line_start, line, sizeof(line));
		zui_draw_text(draw,
			      (struct zui_point){
				      .x = MESSAGES_DETAIL_TEXT_X,
				      .y = (int16_t)(MESSAGES_DETAIL_TEXT_Y +
						     zui_draw_font_height(draw) +
						     line_index * line_height),
			      },
			      line);
		if (next <= line_start) {
			break;
		}
		line_start = next;
	}
	zui_draw_clear_clip(draw);

	if (app->detail_max_scroll > 0U) {
		zui_draw_scrollbar(draw, &(struct zui_rect){.x = MESSAGES_DETAIL_SCROLLBAR_X,
							    .y = MESSAGES_DETAIL_TEXT_Y,
							    .width = 3U,
							    .height = MESSAGES_DETAIL_TEXT_H},
				   app->detail_scroll, app->detail_max_scroll + 1U);
	}

	if (app->selected_entry_valid && app->selected_entry < app->entry_count) {
		zui_draw_button_hints(draw, &(struct zui_draw_button_hint){
			.center = DESKTOP_TEXT_COMMON_BUTTON_REPLY,
		});
	}
}

static bool messages_detail_input(const struct zui_input_event *event, void *user_data)
{
	struct messages_app *app = user_data;

	if (app == NULL) {
		return false;
	}
	if (event == NULL) {
		return false;
	}
	if (messages_should_consume_edge(event)) {
		return true;
	}
	if (messages_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		messages_switch(app, MESSAGES_SCREEN_INBOX);
		return true;
	}
	if (messages_is_click(event) && event->code == ZUI_INPUT_CODE_SELECT) {
		messages_open_reply(app);
		return true;
	}
	if (messages_is_click(event)) {
		if ((event->code == ZUI_INPUT_CODE_DOWN || event->code == ZUI_INPUT_CODE_RIGHT) &&
		    app->detail_scroll < app->detail_max_scroll) {
			app->detail_scroll++;
			messages_request_redraw(app);
			return true;
		}
		if ((event->code == ZUI_INPUT_CODE_UP || event->code == ZUI_INPUT_CODE_LEFT) &&
		    app->detail_scroll > 0U) {
			app->detail_scroll--;
			messages_request_redraw(app);
			return true;
		}
	}

	return false;
}

static void messages_detail_enter(void *user_data)
{
	ARG_UNUSED(user_data);
}

static void messages_detail_exit(void *user_data)
{
	ARG_UNUSED(user_data);
}

static void messages_compose_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct messages_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_draw(zui_form_get_screen(app->compose), draw);
	}
}

static bool messages_compose_input(const struct zui_input_event *event, void *user_data)
{
	struct messages_app *app = user_data;

	if (app == NULL) {
		return false;
	}
	if (messages_should_consume_edge(event)) {
		return true;
	}
	if (messages_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		messages_switch(app, MESSAGES_SCREEN_MENU);
		return true;
	}
	if (messages_forward_input(zui_form_get_screen(app->compose), event)) {
		messages_request_redraw(app);
		return true;
	}

	return false;
}

static void messages_compose_enter(void *user_data)
{
	struct messages_app *app = user_data;

	if (app != NULL) {
		messages_update_compose(app);
		messages_forward_enter(zui_form_get_screen(app->compose));
	}
}

static void messages_compose_exit(void *user_data)
{
	struct messages_app *app = user_data;

	if (app != NULL) {
		messages_forward_exit(zui_form_get_screen(app->compose));
	}
}

static void messages_target_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct messages_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_draw(zui_sublist_get_screen(app->targets), draw);
	}
}

static bool messages_target_input(const struct zui_input_event *event, void *user_data)
{
	struct messages_app *app = user_data;
	size_t selected;

	if (app == NULL) {
		return false;
	}
	if (messages_should_consume_edge(event)) {
		return true;
	}
	if (messages_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		messages_switch(app, MESSAGES_SCREEN_COMPOSE);
		return true;
	}
	if (messages_is_click(event) && app->target_count > 0U) {
		selected = zui_sublist_selected(app->targets);
		if (event->code == ZUI_INPUT_CODE_DOWN && selected + 1U >= app->target_count) {
			if (messages_target_start_next_load(app)) {
				messages_request_redraw(app);
			}
			return true;
		}
		if (event->code == ZUI_INPUT_CODE_UP && selected == 0U) {
			if (messages_target_start_prev_load(app)) {
				messages_request_redraw(app);
			}
			return true;
		}
	}
	if (messages_forward_input(zui_sublist_get_screen(app->targets), event)) {
		messages_request_redraw(app);
		return true;
	}

	return false;
}

static void messages_target_enter(void *user_data)
{
	struct messages_app *app = user_data;

	if (app != NULL) {
		messages_forward_enter(zui_sublist_get_screen(app->targets));
		if (app->target_load_active &&
		    app->target_load_mode == MESSAGES_TARGET_LOAD_INITIAL) {
			messages_target_schedule_load(app, MESSAGES_TARGET_INITIAL_LOAD_DELAY);
		}
	}
}

static void messages_target_exit(void *user_data)
{
	struct messages_app *app = user_data;

	if (app != NULL) {
		app->target_load_active = false;
		app->target_loading = false;
		(void)k_work_cancel_delayable(&app->target_load_work);
		messages_forward_exit(zui_sublist_get_screen(app->targets));
	}
}

static void messages_content_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct messages_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_draw(zui_text_editor_get_screen(app->content_editor), draw);
	}
}

static bool messages_content_input(const struct zui_input_event *event, void *user_data)
{
	struct messages_app *app = user_data;

	if (app == NULL) {
		return false;
	}
	if (messages_should_consume_edge(event)) {
		return true;
	}
	if (event != NULL && event->action == ZUI_INPUT_ACTION_LONG_PRESS &&
	    event->code == ZUI_INPUT_CODE_BACK) {
		messages_switch(app, MESSAGES_SCREEN_COMPOSE);
		return true;
	}
	if (messages_forward_input(zui_text_editor_get_screen(app->content_editor), event)) {
		messages_request_redraw(app);
		return true;
	}

	return false;
}

static void messages_content_enter(void *user_data)
{
	struct messages_app *app = user_data;

	if (app != NULL) {
		messages_forward_enter(zui_text_editor_get_screen(app->content_editor));
	}
}

static void messages_content_exit(void *user_data)
{
	struct messages_app *app = user_data;

	if (app != NULL) {
		messages_forward_exit(zui_text_editor_get_screen(app->content_editor));
	}
}

static const struct zui_screen_ops messages_menu_ops = {
	.draw = messages_menu_draw,
	.input = messages_menu_input,
	.enter = messages_menu_enter,
	.exit = messages_menu_exit,
};

static const struct zui_screen_ops messages_inbox_ops = {
	.draw = messages_inbox_draw,
	.input = messages_inbox_input,
	.enter = messages_inbox_enter,
	.exit = messages_inbox_exit,
};

static const struct zui_screen_ops messages_detail_ops = {
	.draw = messages_detail_draw,
	.input = messages_detail_input,
	.enter = messages_detail_enter,
	.exit = messages_detail_exit,
};

static const struct zui_screen_ops messages_compose_ops = {
	.draw = messages_compose_draw,
	.input = messages_compose_input,
	.enter = messages_compose_enter,
	.exit = messages_compose_exit,
};

static const struct zui_screen_ops messages_target_ops = {
	.draw = messages_target_draw,
	.input = messages_target_input,
	.enter = messages_target_enter,
	.exit = messages_target_exit,
};

static const struct zui_screen_ops messages_content_ops = {
	.draw = messages_content_draw,
	.input = messages_content_input,
	.enter = messages_content_enter,
	.exit = messages_content_exit,
};

static int messages_app_create(struct messages_app *app, struct zui_host *host)
{
	int ret;

	memset(app, 0, sizeof(*app));
	app->host = host;
	app->current_screen = MESSAGES_SCREEN_MENU;
	k_sem_init(&app->exit_sem, 0, 1);
	k_work_init_delayable(&app->target_load_work, messages_target_load_work);
	k_work_init_delayable(&app->response_refresh_work, messages_response_refresh_work);
	messages_reset_compose_draft(app);

	app->menu_items[0] = (struct zui_list_item){.id = MESSAGES_MENU_INBOX,
						    .label = DESKTOP_TEXT_MESSAGES_INBOX};
	app->menu_items[1] = (struct zui_list_item){.id = MESSAGES_MENU_COMPOSE,
						    .label = DESKTOP_TEXT_MESSAGES_NEW_MESSAGE};

	app->router = zui_router_create();
	app->menu = zui_sublist_create(&(struct zui_sublist_config){
		.title = DESKTOP_TEXT_MESSAGES_TITLE,
		.items = app->menu_items,
		.item_count = ARRAY_SIZE(app->menu_items),
		.selected = messages_menu_selected,
		.user_data = app,
	});
	app->inbox = zui_sublist_create(&(struct zui_sublist_config){
		.title = DESKTOP_TEXT_MESSAGES_INBOX,
		.count = messages_inbox_count,
		.get_item = messages_inbox_item,
		.selected = messages_inbox_selected,
		.user_data = app,
	});
	app->targets = zui_sublist_create(&(struct zui_sublist_config){
		.title = DESKTOP_TEXT_MESSAGES_SELECT_TARGET,
		.count = messages_target_count,
		.get_item = messages_target_item,
		.selected = messages_target_selected,
		.user_data = app,
	});
	app->compose = zui_form_create(&(struct zui_form_config){
		.title = DESKTOP_TEXT_MESSAGES_NEW_MESSAGE,
		.changed = messages_compose_changed,
		.activated = messages_compose_activated,
		.user_data = app,
	});
	app->content_editor = zui_text_editor_create(&(struct zui_text_editor_config){
		.title = DESKTOP_TEXT_MESSAGES_LABEL_EDIT_CONTENT,
		.buffer = app->content,
		.buffer_size = sizeof(app->content),
		.submitted = messages_content_submitted,
		.user_data = app,
	});

	app->menu_screen = zui_screen_create(&messages_menu_ops, app);
	app->inbox_screen = zui_screen_create(&messages_inbox_ops, app);
	app->detail_screen = zui_screen_create(&messages_detail_ops, app);
	app->compose_screen = zui_screen_create(&messages_compose_ops, app);
	app->target_screen = zui_screen_create(&messages_target_ops, app);
	app->content_screen = zui_screen_create(&messages_content_ops, app);

	if (app->router == NULL || app->menu == NULL || app->inbox == NULL ||
	    app->targets == NULL || app->compose == NULL || app->content_editor == NULL ||
	    app->menu_screen == NULL || app->inbox_screen == NULL || app->detail_screen == NULL ||
	    app->compose_screen == NULL || app->target_screen == NULL ||
	    app->content_screen == NULL) {
		return -ENOMEM;
	}

	messages_update_compose(app);

	ret = messages_register_screen(app, MESSAGES_SCREEN_MENU, app->menu_screen);
	if (ret != 0) {
		return ret;
	}
	ret = messages_register_screen(app, MESSAGES_SCREEN_INBOX, app->inbox_screen);
	if (ret != 0) {
		return ret;
	}
	ret = messages_register_screen(app, MESSAGES_SCREEN_DETAIL, app->detail_screen);
	if (ret != 0) {
		return ret;
	}
	ret = messages_register_screen(app, MESSAGES_SCREEN_COMPOSE, app->compose_screen);
	if (ret != 0) {
		return ret;
	}
	ret = messages_register_screen(app, MESSAGES_SCREEN_TARGET, app->target_screen);
	if (ret != 0) {
		return ret;
	}
	ret = messages_register_screen(app, MESSAGES_SCREEN_CONTENT, app->content_screen);
	if (ret != 0) {
		return ret;
	}

	ret = zui_host_attach_router(host, ZUI_LAYER_FULLSCREEN, app->router);
	if (ret != 0) {
		return ret;
	}
	atomic_ptr_set(&messages_active_app, (atomic_ptr_val_t)app);
	(void)zbus_obs_set_enable(&meshbus_desktop_messages_response_listener, true);
	messages_switch(app, MESSAGES_SCREEN_MENU);
	return 0;
}

static void messages_app_destroy(struct messages_app *app)
{
	if (app == NULL) {
		return;
	}

	if (app->host != NULL) {
		(void)zui_host_detach_router(app->host, ZUI_LAYER_FULLSCREEN);
	}
	(void)zbus_obs_set_enable(&meshbus_desktop_messages_response_listener, false);
	if ((struct messages_app *)atomic_ptr_get(&messages_active_app) == app) {
		atomic_ptr_set(&messages_active_app, (atomic_ptr_val_t)NULL);
	}
	messages_response_wait_idle();
	app->target_load_active = false;
	app->target_loading = false;
	{
		struct k_work_sync sync;

		(void)k_work_cancel_delayable_sync(&app->target_load_work, &sync);
	}
	{
		struct k_work_sync sync;

		(void)k_work_cancel_delayable_sync(&app->response_refresh_work, &sync);
	}
	if (app->router != NULL) {
		(void)zui_router_unregister_screen(app->router, MESSAGES_SCREEN_MENU);
		(void)zui_router_unregister_screen(app->router, MESSAGES_SCREEN_INBOX);
		(void)zui_router_unregister_screen(app->router, MESSAGES_SCREEN_DETAIL);
		(void)zui_router_unregister_screen(app->router, MESSAGES_SCREEN_COMPOSE);
		(void)zui_router_unregister_screen(app->router, MESSAGES_SCREEN_TARGET);
		(void)zui_router_unregister_screen(app->router, MESSAGES_SCREEN_CONTENT);
	}
	zui_screen_destroy(app->content_screen);
	zui_screen_destroy(app->target_screen);
	zui_screen_destroy(app->compose_screen);
	zui_screen_destroy(app->detail_screen);
	zui_screen_destroy(app->inbox_screen);
	zui_screen_destroy(app->menu_screen);
	zui_text_editor_destroy(app->content_editor);
	zui_form_destroy(app->compose);
	zui_sublist_destroy(app->targets);
	zui_sublist_destroy(app->inbox);
	zui_sublist_destroy(app->menu);
	zui_router_destroy(app->router);
}

static void messages_app_main(void *args)
{
	struct meshbus_desktop_app_args *app_args = args;
	struct messages_app *app;
	int ret;

	if (app_args == NULL || app_args->host == NULL) {
		return;
	}

	app = k_calloc(1U, sizeof(*app));
	if (app == NULL) {
		return;
	}

	ret = messages_app_create(app, app_args->host);
	if (ret != 0) {
		LOG_WRN("Failed to start static Messages app: %d", ret);
		messages_app_destroy(app);
		k_free(app);
		return;
	}

	(void)k_sem_take(&app->exit_sem, K_FOREVER);
	messages_app_destroy(app);
	k_free(app);
}

MESHBUS_DESKTOP_APP_DEFINE(MESHBUS_DESKTOP_APP_ID_MESSAGES,
			   MESHBUS_DESKTOP_APP_NAME_MESSAGES,
			   messages_app_main,
			   3072,
			   &A_message_14x14,
			   MESHBUS_DESKTOP_APP_MENU_INDEX_MESSAGES);
