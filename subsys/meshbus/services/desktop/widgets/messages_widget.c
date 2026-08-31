/* SPDX-License-Identifier: Apache-2.0 */

#include "widget_common.h"

#include <zephyr/logging/log.h>
#include <zephyr/meshbus/time.h>

LOG_MODULE_DECLARE(meshbus_desktop, CONFIG_MESHBUS_DESKTOP_LOG_LEVEL);

#define MESSAGES_WIDGET_TICK_MS	    600U
#define MESSAGES_WIDGET_SOURCE_MAX  40U
#define MESSAGES_WIDGET_TIME_MAX    8U
#define MESSAGES_WIDGET_PAYLOAD_MAX (CONFIG_MESHBUS_MESSAGE_TX_MAX_LEN + 1U)
#define MESSAGES_WIDGET_VISIBLE_LINES 4U
#define MESSAGES_WIDGET_SCROLL_DIV    2U
#define MESSAGES_WIDGET_SOURCE_W      98U
#define MESSAGES_WIDGET_PAYLOAD_RIGHT 118U
#define MESSAGES_WIDGET_FRAME_LEFT    1
#define MESSAGES_WIDGET_FRAME_RIGHT   126
#define MESSAGES_WIDGET_FRAME_TOP     13
#define MESSAGES_WIDGET_FRAME_BOTTOM  63
#define MESSAGES_WIDGET_HEADER_BOTTOM 25

struct messages_widget_model {
	uint64_t entry_id;
	uint64_t timestamp_ms;
	uint32_t cache_seq;
	uint32_t scroll_ticks;
	bool has_message;
	bool unread;
	bool timestamp_realtime;
	char source[MESSAGES_WIDGET_SOURCE_MAX];
	char time[MESSAGES_WIDGET_TIME_MAX];
	char payload[MESSAGES_WIDGET_PAYLOAD_MAX];
};

struct messages_widget_row {
	uint64_t entry_id;
	uint64_t timestamp_ms;
	bool unread;
	bool timestamp_realtime;
	char source[MESSAGES_WIDGET_SOURCE_MAX];
	char time[MESSAGES_WIDGET_TIME_MAX];
	char payload[MESSAGES_WIDGET_PAYLOAD_MAX];
};

struct messages_widget_state {
	struct zui_screen *screen;
	struct messages_widget_model model;
	struct messages_widget_row rows[CONFIG_MESHBUS_DESKTOP_MESSAGE_CACHE_COUNT];
	uint64_t latest_entry_id;
	uint32_t selected_position;
	uint32_t received_count;
	uint32_t row_cache_seq;
};

static struct messages_widget_state messages_widget;

static bool messages_widget_is_channel(meshbus_message_type type)
{
	return type == meshbus_MessageContent_MessageType_SEND_CHANNEL ||
	       type == meshbus_MessageContent_MessageType_RECEIVE_CHANNEL;
}

static bool messages_widget_is_flood_node(const meshbus_message_content *message)
{
	return message != NULL && message->type == meshbus_MessageContent_MessageType_RECEIVE_NODE &&
	       message->route == meshbus_MessageContent_MessageRoute_ROUTE_FLOOD;
}

static void messages_widget_hex_prefix(char *buf, size_t buf_size, const uint8_t *bytes,
				       size_t len)
{
	if (buf == NULL || buf_size == 0U) {
		return;
	}
	if (bytes == NULL || len == 0U) {
		desktop_widget_strcpy(buf, buf_size, DESKTOP_TEXT_COMMON_UNKNOWN);
		return;
	}

	desktop_widget_hex_prefix_format(buf, buf_size, bytes, len);
}

static void messages_widget_resolve_node_name(char *buf, size_t buf_size,
					      const meshbus_message_content *message)
{
	meshbus_contact contact = meshbus_Contact_init_zero;

	if (buf == NULL || buf_size == 0U || message == NULL) {
		return;
	}

	if (meshbus_contact_find_by_prefix(message->target.bytes, &contact) == 0) {
		if (contact.alias[0] != '\0') {
			desktop_widget_strcpy(buf, buf_size, contact.alias);
			return;
		}
		if (contact.name[0] != '\0') {
			desktop_widget_strcpy(buf, buf_size, contact.name);
			return;
		}
	}
	if (message->sender_name[0] != '\0') {
		desktop_widget_strcpy(buf, buf_size, message->sender_name);
		return;
	}

	messages_widget_hex_prefix(buf, buf_size, message->target.bytes, message->target.size);
}

static void messages_widget_resolve_channel_name(char *buf, size_t buf_size,
						 const meshbus_message_content *message)
{
	uint8_t store_size = meshbus_channel_store_size();
	size_t target_len;

	if (buf == NULL || buf_size == 0U || message == NULL) {
		return;
	}

	target_len = MIN((size_t)message->target.size, (size_t)sizeof(message->target.bytes));
	if (target_len > 0U) {
		for (uint8_t idx = 0U; idx < store_size; idx++) {
			meshbus_channel channel = meshbus_Channel_init_zero;

			if (meshbus_channel_get(idx, &channel) != 0 ||
			    channel.secret.size < target_len ||
			    memcmp(channel.secret.bytes, message->target.bytes, target_len) != 0) {
				continue;
			}
			if (channel.name[0] != '\0') {
				desktop_widget_strcpy(buf, buf_size, channel.name);
				return;
			}
		}
	}

	messages_widget_hex_prefix(buf, buf_size, message->target.bytes, message->target.size);
}

static void messages_widget_build_source(char *buf, size_t buf_size,
					 const meshbus_message_content *message)
{
	char name[MESSAGES_WIDGET_SOURCE_MAX];
	const char *kind;
	size_t off;

	if (buf == NULL || buf_size == 0U || message == NULL) {
		return;
	}

	if (messages_widget_is_channel(message->type)) {
		kind = DESKTOP_TEXT_WIDGET_MESSAGES_CHANNEL;
		messages_widget_resolve_channel_name(name, sizeof(name), message);
	} else if (messages_widget_is_flood_node(message)) {
		kind = DESKTOP_TEXT_WIDGET_MESSAGES_FLOOD;
		messages_widget_resolve_node_name(name, sizeof(name), message);
	} else {
		kind = DESKTOP_TEXT_WIDGET_MESSAGES_DIRECT;
		messages_widget_resolve_node_name(name, sizeof(name), message);
	}

	desktop_widget_strcpy(buf, buf_size, kind);
	off = strnlen(buf, buf_size);
	if (off + 1U < buf_size) {
		buf[off++] = '@';
		buf[off] = '\0';
	}
	for (size_t i = 0U; name[i] != '\0' && off + 1U < buf_size; i++) {
		buf[off++] = name[i];
	}
	buf[off] = '\0';
}

static uint64_t messages_widget_now_ms(bool realtime)
{
	uint64_t now_ms;

	if (realtime && meshbus_time_realtime_is_valid()) {
		if (meshbus_time_timestamp_ms_get(&now_ms) == 0 && now_ms > 0U) {
			return now_ms;
		}
	}

	now_ms = (uint64_t)k_uptime_get();
	return now_ms > 0U ? now_ms : 1U;
}

static bool messages_widget_format_clock(char *buf, size_t buf_size, uint64_t timestamp_ms)
{
#if defined(CONFIG_MESHBUS_CLOCK)
	uint64_t timestamp_seconds;
	time_t timestamp_s;
	struct tm local_time;

	if (buf == NULL || buf_size == 0U || timestamp_ms == 0U) {
		return false;
	}

	timestamp_seconds = timestamp_ms / 1000U;
	timestamp_s = (time_t)timestamp_seconds;
	if ((uint64_t)timestamp_s != timestamp_seconds) {
		return false;
	}
	if (meshbus_clock_localtime(timestamp_s, &local_time) != 0) {
		return false;
	}

	(void)snprintk(buf, buf_size, "%02u:%02u", (unsigned int)local_time.tm_hour,
		       (unsigned int)local_time.tm_min);
	return true;
#else
	ARG_UNUSED(buf);
	ARG_UNUSED(buf_size);
	ARG_UNUSED(timestamp_ms);
	return false;
#endif
}

static void messages_widget_format_age(char *buf, size_t buf_size, uint64_t timestamp_ms,
				       bool timestamp_realtime)
{
	uint64_t now_ms;
	uint64_t age_ms;

	if (buf == NULL || buf_size == 0U) {
		return;
	}

	now_ms = messages_widget_now_ms(timestamp_realtime);
	age_ms = now_ms >= timestamp_ms ? now_ms - timestamp_ms : 0U;
	if (age_ms < 60U * 1000U) {
		desktop_widget_strcpy(buf, buf_size, DESKTOP_TEXT_MESSAGES_TIME_AGO_NOW);
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

static void messages_widget_format_time(char *time_text, size_t time_text_size,
					bool has_message, uint64_t timestamp_ms,
					bool timestamp_realtime)
{
	if (time_text == NULL || time_text_size == 0U) {
		return;
	}

	if (!has_message || timestamp_ms == 0U) {
		desktop_widget_strcpy(time_text, time_text_size, DESKTOP_TEXT_NODES_DASH);
	} else if (timestamp_realtime && meshbus_time_realtime_is_valid() &&
		   messages_widget_format_clock(time_text, time_text_size, timestamp_ms)) {
		/* Clock text already formatted. */
	} else {
		messages_widget_format_age(time_text, time_text_size, timestamp_ms,
					   timestamp_realtime);
	}
}

static void messages_widget_payload_to_string(char *buf, size_t buf_size,
					      const meshbus_message_content *message)
{
	size_t len;

	if (buf == NULL || buf_size == 0U) {
		return;
	}
	if (message == NULL || message->payload.size == 0U) {
		desktop_widget_strcpy(buf, buf_size, DESKTOP_TEXT_COMMON_EMPTY);
		return;
	}

	len = MIN((size_t)message->payload.size, (size_t)sizeof(message->payload.bytes));
	len = MIN(len, buf_size - 1U);
	memcpy(buf, message->payload.bytes, len);
	buf[len] = '\0';
}

static void messages_widget_model_defaults(struct messages_widget_model *model)
{
	if (model == NULL) {
		return;
	}

	memset(model, 0, sizeof(*model));
	desktop_widget_strcpy(model->source, sizeof(model->source),
			      DESKTOP_TEXT_WIDGET_MESSAGES_EMPTY);
	desktop_widget_strcpy(model->time, sizeof(model->time), DESKTOP_TEXT_COMMON_UNKNOWN);
	desktop_widget_strcpy(model->payload, sizeof(model->payload),
			      DESKTOP_TEXT_WIDGET_MESSAGES_NO_MORE_MESSAGES);
}

static void messages_widget_row_from_entry(struct messages_widget_row *row,
					   const struct desktop_messages_cache_entry *entry)
{
	if (row == NULL) {
		return;
	}

	memset(row, 0, sizeof(*row));
	if (entry == NULL) {
		return;
	}

	row->entry_id = entry->entry_id;
	row->timestamp_ms = entry->message.timestamp;
	row->unread = entry->unread;
	row->timestamp_realtime = entry->timestamp_realtime;
	messages_widget_build_source(row->source, sizeof(row->source), &entry->message);
	messages_widget_payload_to_string(row->payload, sizeof(row->payload), &entry->message);
	messages_widget_format_time(row->time, sizeof(row->time), true, row->timestamp_ms,
				    row->timestamp_realtime);
}

static bool messages_widget_model_equal_row(const struct messages_widget_model *model,
					    const struct messages_widget_row *row,
					    uint32_t cache_seq)
{
	if (model == NULL || row == NULL || !model->has_message) {
		return false;
	}

	return model->cache_seq == cache_seq &&
	       model->entry_id == row->entry_id &&
	       model->timestamp_ms == row->timestamp_ms && model->unread == row->unread &&
	       model->timestamp_realtime == row->timestamp_realtime &&
	       strcmp(model->source, row->source) == 0 &&
	       strcmp(model->time, row->time) == 0 &&
	       strcmp(model->payload, row->payload) == 0;
}

static bool messages_widget_apply_row(struct messages_widget_model *model,
				      const struct messages_widget_row *row,
				      uint32_t cache_seq, bool reset_scroll)
{
	uint32_t scroll_ticks;

	if (model == NULL) {
		return false;
	}

	if (row == NULL) {
		bool changed = model->has_message;

		messages_widget_model_defaults(model);
		model->cache_seq = cache_seq;
		return changed;
	}

	if (messages_widget_model_equal_row(model, row, cache_seq)) {
		if (reset_scroll && model->scroll_ticks != 0U) {
			model->scroll_ticks = 0U;
			return true;
		}
		return false;
	}

	scroll_ticks = reset_scroll ? 0U : model->scroll_ticks;
	memset(model, 0, sizeof(*model));
	model->entry_id = row->entry_id;
	model->timestamp_ms = row->timestamp_ms;
	model->cache_seq = cache_seq;
	model->scroll_ticks = scroll_ticks;
	model->has_message = true;
	model->unread = row->unread;
	model->timestamp_realtime = row->timestamp_realtime;
	desktop_widget_strcpy(model->source, sizeof(model->source), row->source);
	desktop_widget_strcpy(model->time, sizeof(model->time), row->time);
	desktop_widget_strcpy(model->payload, sizeof(model->payload), row->payload);
	return true;
}

static bool messages_widget_refresh(struct messages_widget_state *state)
{
	uint32_t count;
	uint32_t cache_seq;
	bool changed;
	bool reset_scroll = false;

	if (state == NULL) {
		return false;
	}

	cache_seq = desktop_messages_cache_update_seq();
	count = desktop_messages_cache_received_count();
	if (count != state->received_count || cache_seq != state->row_cache_seq) {
		LOG_DBG("messages widget: cache count=%u seq=%u",
			(unsigned int)count, (unsigned int)cache_seq);
		count = MIN(count, (uint32_t)ARRAY_SIZE(state->rows));
		state->received_count = 0U;
		for (uint32_t pos = 0U; pos < count; pos++) {
			struct desktop_messages_cache_entry entry;

			if (!desktop_messages_cache_copy_received(pos, &entry)) {
				break;
			}
			messages_widget_row_from_entry(&state->rows[state->received_count], &entry);
			state->received_count++;
		}
		state->row_cache_seq = cache_seq;
	}

	if (state->received_count == 0U) {
		state->selected_position = 0U;
		state->latest_entry_id = 0U;
		return messages_widget_apply_row(&state->model, NULL, cache_seq, false);
	}

	if (state->latest_entry_id != state->rows[0].entry_id) {
		LOG_DBG("messages widget: latest cache entry changed");
		state->latest_entry_id = state->rows[0].entry_id;
		state->selected_position = 0U;
		reset_scroll = true;
	}

	for (uint32_t i = 0U; i < state->received_count; i++) {
		char time_text[MESSAGES_WIDGET_TIME_MAX];

		messages_widget_format_time(time_text, sizeof(time_text), true,
					    state->rows[i].timestamp_ms,
					    state->rows[i].timestamp_realtime);
		if (strcmp(state->rows[i].time, time_text) != 0) {
			desktop_widget_strcpy(state->rows[i].time, sizeof(state->rows[i].time),
					      time_text);
		}
	}

	if (state->selected_position >= state->received_count) {
		state->selected_position = state->received_count - 1U;
	}

	changed = messages_widget_apply_row(&state->model,
					    &state->rows[state->selected_position],
					    cache_seq, reset_scroll);
	return changed;
}

static void messages_widget_draw_source(struct zui_draw_ctx *draw,
					const struct messages_widget_model *model)
{
	if (draw == NULL || model == NULL) {
		return;
	}

	if (zui_draw_text_width(draw, model->source) <= MESSAGES_WIDGET_SOURCE_W) {
		zui_draw_text(draw, (struct zui_point){.x = 4, .y = 23}, model->source);
		return;
	}

	zui_draw_set_clip(draw, &(struct zui_rect){.x = 4, .y = 15,
						   .width = MESSAGES_WIDGET_SOURCE_W,
						   .height = 10});
	zui_draw_text_line_scrolled(draw, (struct zui_point){.x = 4, .y = 23},
				    MESSAGES_WIDGET_SOURCE_W, model->source,
				    (size_t)((model->scroll_ticks / MESSAGES_WIDGET_SCROLL_DIV) %
					     strlen(model->source)),
				    false);
	zui_draw_clear_clip(draw);
}

static const char *messages_widget_next_payload_line(struct zui_draw_ctx *draw,
						     const char *src,
						     uint16_t max_width,
						     char *line,
						     size_t line_size)
{
	size_t len = 0U;
	size_t fit = 0U;

	if (line != NULL && line_size > 0U) {
		line[0] = '\0';
	}
	if (draw == NULL || src == NULL || line == NULL || line_size == 0U) {
		return src;
	}

	while (src[len] != '\0' && src[len] != '\n' && len + 1U < line_size) {
		line[len] = src[len];
		line[len + 1U] = '\0';
		if (zui_draw_text_width(draw, line) > max_width) {
			break;
		}
		len++;
		fit = len;
	}

	if (fit == 0U && src[0] != '\0' && src[0] != '\n') {
		fit = 1U;
		line[0] = src[0];
		line[1] = '\0';
	} else {
		line[fit] = '\0';
	}

	src += fit;
	if (*src == '\n') {
		src++;
	}
	return src;
}

static bool messages_widget_payload_line_at(struct zui_draw_ctx *draw, const char *payload,
					    uint16_t max_width, uint8_t target,
					    char *line, size_t line_size)
{
	const char *cursor = payload;

	if (line != NULL && line_size > 0U) {
		line[0] = '\0';
	}
	if (draw == NULL || payload == NULL || line == NULL || line_size == 0U) {
		return false;
	}

	for (uint8_t idx = 0U; *cursor != '\0'; idx++) {
		cursor = messages_widget_next_payload_line(draw, cursor, max_width, line,
							   line_size);
		if (idx == target) {
			return true;
		}
	}

	return target == 0U;
}

static void messages_widget_draw_payload(struct zui_draw_ctx *draw,
					 const struct messages_widget_model *model)
{
	char line[MESSAGES_WIDGET_PAYLOAD_MAX];
	char row[MESSAGES_WIDGET_PAYLOAD_MAX + 3U];
	const char *cursor;
	uint16_t content_width;
	uint8_t line_count = 0U;
	uint8_t first_line = 0U;
	bool scrolling;

	if (draw == NULL || model == NULL || model->payload[0] == '\0') {
		return;
	}

	content_width = MESSAGES_WIDGET_PAYLOAD_RIGHT - 4U -
			zui_draw_text_width(draw, "> ");
	cursor = model->payload;
	while (*cursor != '\0') {
		cursor = messages_widget_next_payload_line(draw, cursor, content_width, line,
							   sizeof(line));
		line_count++;
	}
	line_count = MAX(line_count, 1U);
	scrolling = line_count > 3U;

	if (scrolling) {
		line_count++;
		first_line = (uint8_t)((model->scroll_ticks / MESSAGES_WIDGET_SCROLL_DIV) %
				       line_count);
	}

	zui_draw_set_clip(draw, &(struct zui_rect){.x = 2, .y = 27, .width = 116, .height = 34});
	for (uint8_t idx = 0U; idx < MESSAGES_WIDGET_VISIBLE_LINES; idx++) {
		uint8_t target_line;
		int16_t y = (int16_t)(36 + (idx * 10));
		bool found;

		if (scrolling) {
			target_line = (uint8_t)((first_line + idx) % line_count);
		} else {
			if (idx >= line_count) {
				break;
			}
			target_line = idx;
		}

		if (scrolling && target_line == line_count - 1U) {
			line[0] = '\0';
			found = true;
		} else {
			found = messages_widget_payload_line_at(draw, model->payload, content_width,
								target_line, line, sizeof(line));
		}

		if (!found) {
			break;
		}
		if (line[0] == '\0') {
			continue;
		}

		if (idx == 0U) {
			(void)snprintk(row, sizeof(row),
				       DESKTOP_TEXT_WIDGET_MESSAGES_SELECTED_LINE_FORMAT, line);
			zui_draw_text(draw, (struct zui_point){.x = 4, .y = y}, row);
		} else {
			zui_draw_text(draw, (struct zui_point){.x = 10, .y = y}, line);
		}
	}
	zui_draw_clear_clip(draw);
}

static void messages_widget_draw_header_background(struct zui_draw_ctx *draw)
{
	zui_draw_line(draw, (struct zui_point){.x = MESSAGES_WIDGET_FRAME_LEFT + 2,
					       .y = MESSAGES_WIDGET_FRAME_TOP},
		      (struct zui_point){.x = MESSAGES_WIDGET_FRAME_RIGHT - 2,
					 .y = MESSAGES_WIDGET_FRAME_TOP});
	zui_draw_line(draw, (struct zui_point){.x = MESSAGES_WIDGET_FRAME_LEFT + 1,
					       .y = MESSAGES_WIDGET_FRAME_TOP + 1},
		      (struct zui_point){.x = MESSAGES_WIDGET_FRAME_RIGHT - 1,
					 .y = MESSAGES_WIDGET_FRAME_TOP + 1});
	zui_draw_box(draw, &(struct zui_rect){.x = MESSAGES_WIDGET_FRAME_LEFT,
					      .y = MESSAGES_WIDGET_FRAME_TOP + 2,
					      .width = MESSAGES_WIDGET_FRAME_RIGHT -
						       MESSAGES_WIDGET_FRAME_LEFT + 1,
					      .height = MESSAGES_WIDGET_HEADER_BOTTOM -
							MESSAGES_WIDGET_FRAME_TOP - 1});
}

static void messages_widget_draw_frame(struct zui_draw_ctx *draw)
{
	zui_draw_line(draw, (struct zui_point){.x = MESSAGES_WIDGET_FRAME_LEFT + 2,
					       .y = MESSAGES_WIDGET_FRAME_TOP},
		      (struct zui_point){.x = MESSAGES_WIDGET_FRAME_RIGHT - 2,
					 .y = MESSAGES_WIDGET_FRAME_TOP});
	zui_draw_line(draw, (struct zui_point){.x = MESSAGES_WIDGET_FRAME_LEFT + 1,
					       .y = MESSAGES_WIDGET_FRAME_TOP + 1},
		      (struct zui_point){.x = MESSAGES_WIDGET_FRAME_LEFT,
					 .y = MESSAGES_WIDGET_FRAME_TOP + 2});
	zui_draw_line(draw, (struct zui_point){.x = MESSAGES_WIDGET_FRAME_RIGHT - 1,
					       .y = MESSAGES_WIDGET_FRAME_TOP + 1},
		      (struct zui_point){.x = MESSAGES_WIDGET_FRAME_RIGHT,
					 .y = MESSAGES_WIDGET_FRAME_TOP + 2});
	zui_draw_line(draw, (struct zui_point){.x = MESSAGES_WIDGET_FRAME_LEFT,
					       .y = MESSAGES_WIDGET_FRAME_TOP + 3},
		      (struct zui_point){.x = MESSAGES_WIDGET_FRAME_LEFT,
					 .y = MESSAGES_WIDGET_FRAME_BOTTOM});
	zui_draw_line(draw, (struct zui_point){.x = MESSAGES_WIDGET_FRAME_RIGHT,
					       .y = MESSAGES_WIDGET_FRAME_TOP + 3},
		      (struct zui_point){.x = MESSAGES_WIDGET_FRAME_RIGHT,
					 .y = MESSAGES_WIDGET_FRAME_BOTTOM});
}

static void messages_widget_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct messages_widget_state *state = user_data;
	const struct messages_widget_model *model = state != NULL ? &state->model : NULL;

	if (model == NULL) {
		return;
	}

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	messages_widget_draw_header_background(draw);
	messages_widget_draw_frame(draw);

	zui_draw_set_color(draw, ZUI_COLOR_XOR);
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	messages_widget_draw_source(draw, model);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 124, .y = 23},
			      ZUI_ALIGN_RIGHT, ZUI_ALIGN_BOTTOM, model->time);

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	if (model->has_message && state != NULL && state->selected_position > 0U) {
		zui_draw_icon(draw, (struct zui_point){.x = 118, .y = 28},
			      desktop_widget_common_icon(ZUI_ASSET_ICON_PIN_POINTER));
	}
	if (model->has_message && state != NULL &&
	    state->selected_position + 1U < state->received_count) {
		zui_draw_icon(draw, (struct zui_point){.x = 118, .y = 58},
			      desktop_widget_common_icon(ZUI_ASSET_ICON_ARROW_DOWN_SMALL));
	}

	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	messages_widget_draw_payload(draw, model);
	if (model->unread) {
		zui_draw_icon(draw, (struct zui_point){.x = 117, .y = 41},
			      desktop_widget_common_icon(ZUI_ASSET_ICON_BUTTON_SELECT));
	}
}

static bool messages_widget_select_position(struct messages_widget_state *state,
					    bool newer)
{
	if (state == NULL) {
		return false;
	}

	if (state->received_count <= 1U) {
		return false;
	}

	if (newer) {
		if (state->selected_position == 0U) {
			return false;
		}
		state->selected_position--;
	} else {
		if (state->selected_position + 1U >= state->received_count) {
			return false;
		}
		state->selected_position++;
	}

	(void)messages_widget_apply_row(&state->model, &state->rows[state->selected_position],
					state->row_cache_seq, true);
	if (state->screen != NULL) {
		(void)zui_screen_request_redraw(state->screen);
	}
	return true;
}

static void messages_widget_toggle_read_state(struct messages_widget_state *state)
{
	bool unread;

	if (state == NULL || !state->model.has_message) {
		return;
	}

	unread = !state->model.unread;
	if (unread) {
		desktop_messages_cache_mark_unread(state->model.entry_id);
		LOG_DBG("messages widget: desktop_messages_cache_mark_unread() -> 0");
	} else {
		desktop_messages_cache_mark_read(state->model.entry_id);
		LOG_DBG("messages widget: desktop_messages_cache_mark_read() -> 0");
	}

	state->model.unread = unread;
	if (state->selected_position < state->received_count) {
		state->rows[state->selected_position].unread = unread;
	}
}

static bool messages_widget_input(const struct zui_input_event *event, void *user_data)
{
	struct messages_widget_state *state = user_data;

	if (event == NULL || state == NULL) {
		return false;
	}

	if (event->action == ZUI_INPUT_ACTION_CLICK &&
	    (event->code == ZUI_INPUT_CODE_UP || event->code == ZUI_INPUT_CODE_DOWN)) {
		return messages_widget_select_position(state, event->code == ZUI_INPUT_CODE_UP);
	}

	if (event->code != ZUI_INPUT_CODE_SELECT ||
	    event->action != ZUI_INPUT_ACTION_LONG_PRESS || !state->model.has_message) {
		return false;
	}

	messages_widget_toggle_read_state(state);
	if (state->screen != NULL) {
		(void)zui_screen_request_redraw(state->screen);
	}
	return true;
}

static const struct zui_screen_ops messages_widget_ops = {
	.draw = messages_widget_draw,
	.input = messages_widget_input,
};

static struct zui_screen *messages_widget_screen_create(
	struct meshbus_desktop_dashboard_widget *wctx)
{
	ARG_UNUSED(wctx);

	if (messages_widget.screen == NULL) {
		messages_widget_model_defaults(&messages_widget.model);
		messages_widget.screen =
			zui_screen_create(&messages_widget_ops, &messages_widget);
	}

	return messages_widget.screen;
}

static uint32_t messages_widget_tick(struct meshbus_desktop_dashboard_widget *wctx)
{
	bool changed;

	ARG_UNUSED(wctx);

	messages_widget.model.scroll_ticks++;
	changed = messages_widget_refresh(&messages_widget);
	if (messages_widget.model.has_message || strlen(messages_widget.model.source) > 16U) {
		changed = true;
	}

	if (changed && messages_widget.screen != NULL) {
		(void)zui_screen_request_redraw(messages_widget.screen);
	}

	return MESSAGES_WIDGET_TICK_MS;
}

MESHBUS_DESKTOP_DASHBOARD_WIDGETS_DEFINE(MESHBUS_DESKTOP_DASHBOARD_WIDGET_ID_MESSAGES,
					 MESHBUS_DESKTOP_DASHBOARD_WIDGET_TITLE_MESSAGES,
					 messages_widget_screen_create,
					 messages_widget_tick,
					 MESHBUS_DESKTOP_APP_ID_MESSAGES);
