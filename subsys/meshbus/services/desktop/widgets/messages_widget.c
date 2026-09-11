/* SPDX-License-Identifier: Apache-2.0 */

#include "widget_common.h"

#include <zephyr/logging/log.h>
#include <zephyr/meshbus/time.h>

LOG_MODULE_DECLARE(meshbus_desktop, CONFIG_MESHBUS_DESKTOP_LOG_LEVEL);

#define MESSAGES_WIDGET_TICK_MS	    600U
#define MESSAGES_WIDGET_SOURCE_MAX  40U
#define MESSAGES_WIDGET_TIME_MAX    8U
#define MESSAGES_WIDGET_PAYLOAD_MAX (CONFIG_MESHBUS_MESSAGE_TX_MAX_LEN + 1U)
#define MESSAGES_WIDGET_VISIBLE_LINES 3U
#define MESSAGES_WIDGET_SCROLL_PAUSE_TICKS 5U
#define MESSAGES_WIDGET_SCROLL_LINE_TICKS  2U
#define MESSAGES_WIDGET_SCROLL_STEP_PX     3U
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
	const char *kind;
	char source[MESSAGES_WIDGET_SOURCE_MAX];
	char time[MESSAGES_WIDGET_TIME_MAX];
	char payload[MESSAGES_WIDGET_PAYLOAD_MAX];
	char lines[MESSAGES_WIDGET_VISIBLE_LINES][MESSAGES_WIDGET_PAYLOAD_MAX];
	uint16_t name_x;
	uint16_t name_width;
	uint16_t name_offset;
	uint8_t visible_lines;
	uint8_t line_step;
	bool more_above;
	bool more_below;
};

struct messages_widget_row {
	uint64_t entry_id;
	uint64_t timestamp_ms;
	bool unread;
	bool timestamp_realtime;
	const char *kind;
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
	bool newer_pending;
	char position_text[24];
};

static struct messages_widget_state messages_widget;

/* Reject incomplete sequences so copied names and line breaks remain UTF-8 aligned. */
static size_t messages_widget_utf8_length(const char *text)
{
	uint8_t first = (uint8_t)text[0];
	size_t length;

	if (first < 0x80U) {
		return first == 0U ? 0U : 1U;
	}
	if (first < 0xc2U || first > 0xf4U) {
		return 0U;
	}
	length = first < 0xe0U ? 2U : (first < 0xf0U ? 3U : 4U);
	for (size_t i = 1U; i < length; i++) {
		if (((uint8_t)text[i] & 0xc0U) != 0x80U) {
			return 0U;
		}
	}
	if ((first == 0xe0U && (uint8_t)text[1] < 0xa0U) ||
	    (first == 0xedU && (uint8_t)text[1] >= 0xa0U) ||
	    (first == 0xf0U && (uint8_t)text[1] < 0x90U) ||
	    (first == 0xf4U && (uint8_t)text[1] >= 0x90U)) {
		return 0U;
	}
	return length;
}

static void messages_widget_copy_text(char *dst, const char *src, size_t size)
{
	size_t used = 0U;

	if (size == 0U) {
		return;
	}
	while (src[used] != '\0') {
		size_t length = messages_widget_utf8_length(src + used);

		if (length == 0U || used + length >= size) {
			break;
		}
		memmove(dst + used, src + used, length);
		used += length;
	}
	dst[used] = '\0';
}

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
					 const meshbus_message_content *message,
					 const char **kind)
{
	if (buf == NULL || buf_size == 0U || message == NULL || kind == NULL) {
		return;
	}

	if (messages_widget_is_channel(message->type)) {
		*kind = DESKTOP_TEXT_WIDGET_MESSAGES_CHANNEL;
		messages_widget_resolve_channel_name(buf, buf_size, message);
	} else if (messages_widget_is_flood_node(message)) {
		*kind = DESKTOP_TEXT_WIDGET_MESSAGES_FLOOD;
		messages_widget_resolve_node_name(buf, buf_size, message);
	} else {
		*kind = DESKTOP_TEXT_WIDGET_MESSAGES_DIRECT;
		messages_widget_resolve_node_name(buf, buf_size, message);
	}
	messages_widget_copy_text(buf, buf, buf_size);
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
	messages_widget_copy_text(buf, buf, buf_size);
}

static void messages_widget_model_defaults(struct messages_widget_model *model)
{
	if (model == NULL) {
		return;
	}

	memset(model, 0, sizeof(*model));
	messages_widget_copy_text(model->source, DESKTOP_TEXT_MESSAGES_TITLE, sizeof(model->source));
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
	messages_widget_build_source(row->source, sizeof(row->source), &entry->message,
				     &row->kind);
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
	       model->timestamp_realtime == row->timestamp_realtime && model->kind == row->kind &&
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

	reset_scroll = reset_scroll || model->entry_id != row->entry_id;
	scroll_ticks = reset_scroll ? 0U : model->scroll_ticks;
	memset(model, 0, sizeof(*model));
	model->entry_id = row->entry_id;
	model->timestamp_ms = row->timestamp_ms;
	model->cache_seq = cache_seq;
	model->scroll_ticks = scroll_ticks;
	model->has_message = true;
	model->unread = row->unread;
	model->timestamp_realtime = row->timestamp_realtime;
	model->kind = row->kind;
	desktop_widget_strcpy(model->source, sizeof(model->source), row->source);
	desktop_widget_strcpy(model->time, sizeof(model->time), row->time);
	desktop_widget_strcpy(model->payload, sizeof(model->payload), row->payload);
	return true;
}

/* Keep a browsed entry stable when insertion shifts its cache position. */
static void messages_widget_reconcile_selection(struct messages_widget_state *state,
						uint64_t entry_id, bool follow_latest)
{
	if (state->received_count == 0U) {
		state->selected_position = 0U;
		state->latest_entry_id = 0U;
		state->newer_pending = false;
		return;
	}

	if (state->latest_entry_id != 0U &&
	    state->rows[0].entry_id > state->latest_entry_id && !follow_latest) {
		state->newer_pending = true;
	}
	state->latest_entry_id = state->rows[0].entry_id;
	if (follow_latest) {
		state->selected_position = 0U;
	} else {
		for (uint32_t i = 0U; i < state->received_count; i++) {
			if (state->rows[i].entry_id == entry_id) {
				state->selected_position = i;
				break;
			}
		}
		/* A message evicted from the bounded cache falls back to its nearest row. */
		state->selected_position = MIN(state->selected_position,
					       state->received_count - 1U);
	}
	if (state->selected_position == 0U) {
		state->newer_pending = false;
	}
}

static bool messages_widget_refresh(struct messages_widget_state *state)
{
	uint32_t count;
	uint32_t cache_seq;
	bool changed;
	uint64_t selected_id;
	bool follow_latest;

	if (state == NULL) {
		return false;
	}

	selected_id = state->model.entry_id;
	follow_latest = !state->model.has_message || state->selected_position == 0U;
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

	messages_widget_reconcile_selection(state, selected_id, follow_latest);
	if (state->received_count == 0U) {
		return messages_widget_apply_row(&state->model, NULL, cache_seq, false);
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
					    cache_seq, false);
	return changed;
}

static uint16_t messages_widget_scroll_position(uint16_t distance, uint32_t ticks,
					       uint16_t step, uint16_t step_ticks)
{
	uint32_t steps;
	uint32_t phase;

	if (distance == 0U) {
		return 0U;
	}
	steps = DIV_ROUND_UP(distance, step);
	phase = ticks % (2U * MESSAGES_WIDGET_SCROLL_PAUSE_TICKS +
			(steps - 1U) * step_ticks);
	if (phase < MESSAGES_WIDGET_SCROLL_PAUSE_TICKS) {
		return 0U;
	}
	return MIN(((phase - MESSAGES_WIDGET_SCROLL_PAUSE_TICKS) / step_ticks + 1U) *
		   step, distance);
}

static const char *messages_widget_next_payload_line(struct zui_draw_ctx *draw,
						     const char *src, uint16_t max_width,
						     char *line, size_t line_size)
{
	size_t fit = 0U;
	size_t last_break = 0U;
	size_t consumed;

	if (draw == NULL || src == NULL || line == NULL || line_size == 0U) {
		return src;
	}
	line[0] = '\0';
	while (src[fit] != '\0' && src[fit] != '\n' && src[fit] != '\r') {
		size_t length = MAX(1U, messages_widget_utf8_length(src + fit));

		if (fit + length >= line_size) {
			break;
		}
		memcpy(line + fit, src + fit, length);
		if (src[fit] == '\t') {
			line[fit] = ' ';
		}
		line[fit + length] = '\0';
		if (zui_draw_text_width(draw, line) > max_width && fit != 0U) {
			break;
		}
		fit += length;
		if (line[fit - 1U] == ' ') {
			last_break = fit;
		}
	}

	if (src[fit] != '\0' && src[fit] != '\n' && src[fit] != '\r' &&
	    src[fit] != ' ' && src[fit] != '\t' && last_break > 0U) {
		fit = last_break;
	}
	consumed = fit;
	while (fit > 0U && line[fit - 1U] == ' ') {
		fit--;
	}
	line[fit] = '\0';
	if (consumed == 0U && src[0] != '\0' && src[0] != '\n' && src[0] != '\r') {
		/* Even a buffer too small for one glyph must not stall the caller. */
		consumed = MAX(1U, messages_widget_utf8_length(src));
	}
	src += consumed;
	while (*src == ' ' || *src == '\t') {
		src++;
	}
	if (*src == '\r') {
		src++;
	}
	if (*src == '\n') {
		src++;
	}
	return src;
}

static void messages_widget_prepare_view(struct messages_widget_state *state)
{
	struct zui_desktop *desktop = zui_desktop_get_instance();
	struct messages_widget_model *model = &state->model;
	struct zui_draw_ctx *draw = desktop != NULL ? desktop->draw : NULL;
	char line[MESSAGES_WIDGET_PAYLOAD_MAX];
	const char *cursor;
	uint16_t line_count = 0U;
	uint16_t first_line;
	uint16_t name_width;

	if (draw == NULL) {
		return;
	}

	/* Dashboard poll and input run on the render thread, before drawing. */
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	model->name_x = model->has_message ? 10U + zui_draw_text_width(draw, model->kind) : 4U;
	model->name_width = model->has_message ?
		MAX(1, 121 - (int)zui_draw_text_width(draw, model->time) - model->name_x) : 120U;
	name_width = zui_draw_text_width(draw, model->source);
	model->name_offset = messages_widget_scroll_position(
		name_width > model->name_width ? name_width - model->name_width : 0U,
		model->scroll_ticks, MESSAGES_WIDGET_SCROLL_STEP_PX, 1U);
	(void)snprintk(state->position_text, sizeof(state->position_text),
		       DESKTOP_TEXT_WIDGET_MESSAGES_POSITION_FORMAT,
		       (unsigned int)(state->selected_position + 1U),
		       (unsigned int)state->received_count);

	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	model->line_step = zui_draw_font_height(draw) > 9U ? 12U : 9U;
	model->visible_lines = model->line_step > 9U ? 2U : MESSAGES_WIDGET_VISIBLE_LINES;
	memset(model->lines, 0, sizeof(model->lines));
	cursor = model->payload;
	while (*cursor != '\0') {
		cursor = messages_widget_next_payload_line(draw, cursor,
			MESSAGES_WIDGET_PAYLOAD_RIGHT - 4U, line, sizeof(line));
		line_count++;
	}
	first_line = messages_widget_scroll_position(
		line_count > model->visible_lines ? line_count - model->visible_lines : 0U,
		model->scroll_ticks, 1U, MESSAGES_WIDGET_SCROLL_LINE_TICKS);
	model->more_above = first_line > 0U;
	model->more_below = first_line + model->visible_lines < line_count;
	cursor = model->payload;
	for (uint16_t i = 0U; *cursor != '\0' && i < first_line + model->visible_lines; i++) {
		cursor = messages_widget_next_payload_line(draw, cursor,
			MESSAGES_WIDGET_PAYLOAD_RIGHT - 4U, line, sizeof(line));
		if (i >= first_line) {
			messages_widget_copy_text(model->lines[i - first_line], line, sizeof(model->lines[0]));
		}
	}
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
}

static void messages_widget_draw_source(struct zui_draw_ctx *draw,
					const struct messages_widget_model *model)
{
	if (model->has_message) {
		zui_draw_rect(draw, &(struct zui_rect){.x = 3, .y = 14,
			.width = model->name_x - 6U, .height = 11});
		zui_draw_text(draw, (struct zui_point){.x = 5, .y = 23}, model->kind);
	}
	zui_draw_set_clip(draw, &(struct zui_rect){.x = model->name_x, .y = 15,
		.width = model->name_width, .height = 10});
	zui_draw_text(draw, (struct zui_point){.x = model->name_x - model->name_offset,
		.y = 23}, model->source);
	zui_draw_clear_clip(draw);
}

static void messages_widget_draw_payload(struct zui_draw_ctx *draw,
					 const struct messages_widget_model *model)
{
	zui_draw_set_clip(draw, &(struct zui_rect){.x = 3, .y = 27, .width = 115, .height = 29});
	for (uint8_t i = 0U; i < model->visible_lines; i++) {
		zui_draw_text(draw, (struct zui_point){.x = 4, .y = 26 + (i + 1U) * model->line_step},
			      model->lines[i]);
	}
	zui_draw_clear_clip(draw);
	/* Slim continuation marks describe body scrolling, separately from message navigation. */
	if (model->more_above) {
		zui_draw_line(draw, (struct zui_point){.x = 122, .y = 28},
			      (struct zui_point){.x = 122, .y = 31});
	}
	if (model->more_below) {
		zui_draw_line(draw, (struct zui_point){.x = 122, .y = 49},
			      (struct zui_point){.x = 122, .y = 52});
	}
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
	if (model->has_message) {
		zui_draw_text_aligned(draw, (struct zui_point){.x = 124, .y = 23},
			ZUI_ALIGN_RIGHT, ZUI_ALIGN_BOTTOM, model->time);
	}

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	if (!model->has_message) {
		zui_draw_icon(draw, (struct zui_point){.x = 60, .y = 31}, &I_message_8x8);
		zui_draw_text_aligned(draw, (struct zui_point){.x = 64, .y = 51},
			ZUI_ALIGN_CENTER, ZUI_ALIGN_BOTTOM, DESKTOP_TEXT_WIDGET_MESSAGES_EMPTY);
	} else {
		messages_widget_draw_payload(draw, model);
		zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
		if (model->unread) {
			zui_draw_text(draw, (struct zui_point){.x = 4, .y = 62},
				DESKTOP_TEXT_WIDGET_MESSAGES_UNREAD);
		}
		if (state->newer_pending) {
			zui_draw_text(draw, (struct zui_point){.x = 28, .y = 62},
				DESKTOP_TEXT_WIDGET_MESSAGES_NEWER);
		}
		zui_draw_text_aligned(draw, (struct zui_point){.x = 109, .y = 62},
			ZUI_ALIGN_RIGHT, ZUI_ALIGN_BOTTOM, state->position_text);
		if (state->selected_position > 0U) {
			zui_draw_icon(draw, (struct zui_point){.x = 112, .y = 57},
				desktop_widget_common_icon(ZUI_ASSET_ICON_ARROW_UP_SMALL));
		}
		if (state->selected_position + 1U < state->received_count) {
			zui_draw_icon(draw, (struct zui_point){.x = 119, .y = 57},
				desktop_widget_common_icon(ZUI_ASSET_ICON_ARROW_DOWN_SMALL));
		}
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

	if (state->selected_position == 0U) {
		state->newer_pending = false;
	}
	(void)messages_widget_apply_row(&state->model, &state->rows[state->selected_position],
					state->row_cache_seq, true);
	messages_widget_prepare_view(state);
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
	messages_widget_prepare_view(state);
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
		messages_widget_prepare_view(&messages_widget);
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
	messages_widget_prepare_view(&messages_widget);
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
