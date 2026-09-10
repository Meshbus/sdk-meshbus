/* SPDX-License-Identifier: Apache-2.0 */

#include "widget_common.h"

#include <zephyr/logging/log.h>
#include <zephyr/meshbus/contact.h>
#include <zephyr/meshbus/meshcore.h>
#include <zephyr/meshbus/time.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_DECLARE(meshbus_desktop, CONFIG_MESHBUS_DESKTOP_LOG_LEVEL);

#define NODES_WIDGET_LATEST_LABEL_MAX 40U
#define NODES_WIDGET_AGE_STR_MAX      8U
#define NODES_WIDGET_COUNT_STR_MAX    20U
#define NODES_WIDGET_NAME_RIGHT       96
#define NODES_WIDGET_SCROLL_PAUSE_TICKS 5U
#define NODES_WIDGET_SCROLL_STEP_PX    3U
#define NODES_WIDGET_BODY_X	       1
#define NODES_WIDGET_BODY_RIGHT       126
#define NODES_WIDGET_BODY_Y	       13
#define NODES_WIDGET_BODY_W	       126U
#define NODES_WIDGET_BODY_BOTTOM      63
#define NODES_WIDGET_HEADER_BOTTOM    25
#define NODES_WIDGET_COUNT_Y	       37
#define NODES_WIDGET_COUNT_ICON_Y     29
#define NODES_WIDGET_BROADCAST_Y      49
#define NODES_WIDGET_BROADCAST_ICON_Y 42
#define NODES_WIDGET_CONTACT_NAME_MAX    32U
#define NODES_WIDGET_PROGRESS_X       3
#define NODES_WIDGET_PROGRESS_Y       53
#define NODES_WIDGET_PROGRESS_W       122U
#define NODES_WIDGET_PROGRESS_H       11U
#define NODES_WIDGET_PUBLIC_KEY_PREFIX_SIZE 3U
#define NODES_WIDGET_REFRESH_INTERVAL_MS    3000U
#define NODES_WIDGET_TICK_INTERVAL_MS       600U
#define NODES_WIDGET_REFRESH_TICKS \
	((NODES_WIDGET_REFRESH_INTERVAL_MS + NODES_WIDGET_TICK_INTERVAL_MS - 1U) / \
	 NODES_WIDGET_TICK_INTERVAL_MS)

struct nodes_widget_model {
	uint32_t age_ticks;
	uint32_t latest_label_scroll;
	uint32_t local_advert_ticks;
	uint8_t store_count;
	uint8_t store_size;
	uint32_t base_now_s;
	uint32_t latest_last_seen_timestamp;
	meshbus_contact_role latest_role;
	bool has_contact;
	bool local_advert_seen;
	char latest_label[NODES_WIDGET_LATEST_LABEL_MAX];
	char latest_age[NODES_WIDGET_AGE_STR_MAX];
	char advert_age[NODES_WIDGET_COUNT_STR_MAX];
	char count_text[4];
	char progress_text[NODES_WIDGET_COUNT_STR_MAX];
	float progress_ratio;
};

struct nodes_widget_snapshot {
	uint8_t store_count;
	uint8_t store_size;
	uint32_t base_now_s;
	uint32_t latest_last_seen_timestamp;
	meshbus_contact_role latest_role;
	bool has_contact;
	char latest_label[NODES_WIDGET_LATEST_LABEL_MAX];
};

struct nodes_widget_state {
	struct zui_screen *screen;
	struct nodes_widget_model model;
	uint8_t scan_ticks;
	uint8_t store_size;
	bool store_size_loaded;
};

static struct nodes_widget_state nodes_widget;
static atomic_t nodes_widget_dirty = ATOMIC_INIT(1);

static uint32_t nodes_widget_now_s(void)
{
	uint32_t now_s;

	if (meshbus_time_timestamp_s_get(&now_s) != 0) {
		return 1U;
	}

	return now_s;
}

static bool nodes_widget_time_valid(uint32_t now_s)
{
	return now_s >= MESHBUS_TIME_VALID_UNIX_TIMESTAMP_S;
}

static uint32_t nodes_widget_ticks_ms(uint32_t ticks)
{
	uint64_t ms = (uint64_t)ticks * NODES_WIDGET_TICK_INTERVAL_MS;

	return ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms;
}

static void nodes_widget_format_time_ago(char *buf, size_t buf_size, uint32_t age_ms)
{
	if (buf == NULL || buf_size == 0U) {
		return;
	}

	if (age_ms < 60U * 1000U) {
		desktop_widget_strcpy(buf, buf_size, DESKTOP_TEXT_NODES_TIME_AGO_NOW);
		return;
	}
	if (age_ms < 3600U * 1000U) {
		(void)snprintk(buf, buf_size, DESKTOP_TEXT_NODES_TIME_AGO_MIN_FORMAT,
			       (unsigned int)(age_ms / (60U * 1000U)));
		return;
	}
	if (age_ms < 24U * 3600U * 1000U) {
		(void)snprintk(buf, buf_size, DESKTOP_TEXT_NODES_TIME_AGO_HOUR_FORMAT,
			       (unsigned int)(age_ms / (3600U * 1000U)));
		return;
	}
	if (age_ms > 7U * 24U * 3600U * 1000U) {
		desktop_widget_strcpy(buf, buf_size, DESKTOP_TEXT_NODES_TIME_AGO_OVER_7D);
		return;
	}

	(void)snprintk(buf, buf_size, DESKTOP_TEXT_NODES_TIME_AGO_DAY_FORMAT,
		       (unsigned int)(age_ms / (24U * 3600U * 1000U)));
}

static void nodes_widget_format_contact_age(const struct nodes_widget_model *model,
					 char *buf, size_t buf_size)
{
	uint64_t age_ms;

	if (buf == NULL || buf_size == 0U) {
		return;
	}

	if (model == NULL || !model->has_contact || model->latest_last_seen_timestamp == 0U ||
	    !nodes_widget_time_valid(model->base_now_s) ||
	    model->base_now_s < model->latest_last_seen_timestamp) {
		desktop_widget_strcpy(buf, buf_size, DESKTOP_TEXT_NODES_DASH);
		return;
	}

	age_ms = (uint64_t)(model->base_now_s - model->latest_last_seen_timestamp) * 1000U;
	age_ms += nodes_widget_ticks_ms(model->age_ticks);
	if (age_ms > UINT32_MAX) {
		age_ms = UINT32_MAX;
	}
	nodes_widget_format_time_ago(buf, buf_size, (uint32_t)age_ms);
}

static void nodes_widget_format_advert_age(const struct nodes_widget_model *model,
					   char *buf, size_t buf_size)
{
	char age[NODES_WIDGET_AGE_STR_MAX];

	if (buf == NULL || buf_size == 0U) {
		return;
	}

	if (model == NULL || !model->local_advert_seen) {
		desktop_widget_strcpy(buf, buf_size, DESKTOP_TEXT_NODES_DASH);
		return;
	}

	nodes_widget_format_time_ago(age, sizeof(age),
				     nodes_widget_ticks_ms(model->local_advert_ticks));
	(void)snprintk(buf, buf_size, DESKTOP_TEXT_WIDGET_NODES_ADVERT_REQUEST_FORMAT, age);
}

static void nodes_widget_format_prefix_hex(const uint8_t *bytes, size_t len,
					   char *out, size_t out_size)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t off = 0U;

	if (out == NULL || out_size == 0U) {
		return;
	}

	out[0] = '\0';
	if (bytes == NULL) {
		return;
	}

	for (size_t i = 0U; i < len && (off + 2U) < out_size; i++) {
		out[off++] = hex[(bytes[i] >> 4) & 0x0F];
		out[off++] = hex[bytes[i] & 0x0F];
	}
	out[off] = '\0';
}

static const char *nodes_widget_role_token(meshbus_contact_role role)
{
	switch (role) {
	case MESHBUS_CONTACT_ROLE_CHAT:
		return DESKTOP_TEXT_WIDGET_NODES_CLI;
	case MESHBUS_CONTACT_ROLE_REPEATER:
		return DESKTOP_TEXT_WIDGET_NODES_RPT;
	case MESHBUS_CONTACT_ROLE_ROOM:
		return DESKTOP_TEXT_WIDGET_NODES_ROOM;
	case MESHBUS_CONTACT_ROLE_SENSOR:
		return DESKTOP_TEXT_WIDGET_NODES_SENSOR;
	default:
		return DESKTOP_TEXT_WIDGET_NODES_ROLE_UNKNOWN;
	}
}

#if defined(CONFIG_MESHBUS_CONTACT)
static uint32_t nodes_widget_latest_sort_key(const meshbus_contact *contact)
{
	if (contact == NULL) {
		return 0U;
	}
	return contact->last_seen_timestamp;
}

static void nodes_widget_format_latest_label(const meshbus_contact *contact,
					     char *out, size_t out_size)
{
	char name[NODES_WIDGET_CONTACT_NAME_MAX];
	size_t prefix_len;

	if (out == NULL || out_size == 0U) {
		return;
	}

	out[0] = '\0';
	if (contact == NULL) {
		desktop_widget_strcpy(out, out_size, DESKTOP_TEXT_WIDGET_NODES_EMPTY);
		return;
	}

	if (contact->name[0] != '\0') {
		desktop_widget_strcpy(name, sizeof(name), contact->name);
	} else {
		prefix_len = MIN((size_t)contact->public_key.size,
				 (size_t)NODES_WIDGET_PUBLIC_KEY_PREFIX_SIZE);
		nodes_widget_format_prefix_hex(contact->public_key.bytes, prefix_len, name,
					       sizeof(name));
		if (name[0] == '\0') {
			desktop_widget_strcpy(name, sizeof(name),
					      DESKTOP_TEXT_WIDGET_NODES_UNKNOWN_NAME);
		}
	}

	desktop_widget_strcpy(out, out_size, name);
}

static uint8_t nodes_widget_store_size_get(struct nodes_widget_state *state)
{
	if (state == NULL) {
		return 0U;
	}

	if (!state->store_size_loaded) {
		state->store_size = meshbus_contact_store_size();
		state->store_size_loaded = true;
		LOG_DBG("nodes widget: meshbus_contact_store_size() -> %u",
			(unsigned int)state->store_size);
	}

	return state->store_size;
}

static void nodes_widget_snapshot_read(struct nodes_widget_state *state,
				       struct nodes_widget_snapshot *snapshot)
{
	meshbus_contact latest = meshbus_Contact_init_zero;
	int64_t start_ms;
	uint32_t latest_key = 0U;
	uint8_t latest_slot = 0U;
	uint8_t found_count = 0U;

	if (state == NULL || snapshot == NULL) {
		return;
	}

	start_ms = k_uptime_get();
	memset(snapshot, 0, sizeof(*snapshot));
	desktop_widget_strcpy(snapshot->latest_label, sizeof(snapshot->latest_label),
			      DESKTOP_TEXT_WIDGET_NODES_EMPTY);
	snapshot->base_now_s = nodes_widget_now_s();
	snapshot->store_count = meshbus_contact_store_count();
	snapshot->store_size = nodes_widget_store_size_get(state);

	if (snapshot->store_count == 0U || snapshot->store_size == 0U) {
		LOG_DBG("nodes widget: snapshot count=%u size=%u found=0 elapsed=%lldms",
			(unsigned int)snapshot->store_count,
			(unsigned int)snapshot->store_size,
			(long long)(k_uptime_get() - start_ms));
		return;
	}

	for (uint8_t slot = 0U; slot < snapshot->store_size && found_count < snapshot->store_count;
	     slot++) {
		meshbus_contact contact = meshbus_Contact_init_zero;
		uint32_t key;

		if (meshbus_contact_get(slot, &contact) != 0) {
			continue;
		}

		found_count++;
		key = nodes_widget_latest_sort_key(&contact);
		if (!snapshot->has_contact || key > latest_key ||
		    (key == latest_key && slot >= latest_slot)) {
			latest = contact;
			latest_key = key;
			latest_slot = slot;
			snapshot->has_contact = true;
		}
	}

	if (snapshot->has_contact) {
		nodes_widget_format_latest_label(&latest, snapshot->latest_label,
						 sizeof(snapshot->latest_label));
		snapshot->latest_last_seen_timestamp = latest.last_seen_timestamp;
		snapshot->latest_role = latest.role;
	}
	LOG_DBG("nodes widget: snapshot count=%u size=%u found=%u elapsed=%lldms",
		(unsigned int)snapshot->store_count, (unsigned int)snapshot->store_size,
		(unsigned int)found_count, (long long)(k_uptime_get() - start_ms));
}
#else
static void nodes_widget_snapshot_read(struct nodes_widget_state *state,
				       struct nodes_widget_snapshot *snapshot)
{
	ARG_UNUSED(state);

	if (snapshot == NULL) {
		return;
	}

	memset(snapshot, 0, sizeof(*snapshot));
	desktop_widget_strcpy(snapshot->latest_label, sizeof(snapshot->latest_label),
			      DESKTOP_TEXT_WIDGET_NODES_EMPTY);
	snapshot->base_now_s = nodes_widget_now_s();
}
#endif

static void nodes_widget_prepare_view(struct nodes_widget_model *model)
{
	const char *capacity_format = DESKTOP_TEXT_WIDGET_NODES_CAPACITY_FORMAT;

	nodes_widget_format_contact_age(model, model->latest_age, sizeof(model->latest_age));
	nodes_widget_format_advert_age(model, model->advert_age, sizeof(model->advert_age));
	(void)snprintk(model->count_text, sizeof(model->count_text), "%u",
		       (unsigned int)model->store_count);
	(void)snprintk(model->progress_text, sizeof(model->progress_text), capacity_format,
		       (unsigned int)model->store_count, (unsigned int)model->store_size);
	model->progress_ratio = model->store_size > 0U ?
		desktop_widget_usage_ratio(model->store_count, model->store_size) : 0.0f;
}

static bool nodes_widget_apply_snapshot(struct nodes_widget_model *model,
					const struct nodes_widget_snapshot *snapshot)
{
	bool changed;
	bool label_changed;

	if (model == NULL || snapshot == NULL) {
		return false;
	}

	label_changed = model->latest_role != snapshot->latest_role ||
			strncmp(model->latest_label, snapshot->latest_label,
				sizeof(model->latest_label)) != 0;
	changed = model->store_count != snapshot->store_count ||
		  model->store_size != snapshot->store_size ||
		  model->latest_last_seen_timestamp != snapshot->latest_last_seen_timestamp ||
		  model->has_contact != snapshot->has_contact || label_changed;

	model->store_count = snapshot->store_count;
	model->store_size = snapshot->store_size;
	model->base_now_s = snapshot->base_now_s;
	model->latest_last_seen_timestamp = snapshot->latest_last_seen_timestamp;
	model->latest_role = snapshot->latest_role;
	model->has_contact = snapshot->has_contact;
	desktop_widget_strcpy(model->latest_label, sizeof(model->latest_label),
			      snapshot->latest_label);
	model->age_ticks = 0U;
	if (label_changed) {
		model->latest_label_scroll = 0U;
	}
	nodes_widget_prepare_view(model);

	return changed;
}

static void nodes_widget_model_defaults(struct nodes_widget_model *model)
{
	if (model == NULL) {
		return;
	}

	memset(model, 0, sizeof(*model));
	model->base_now_s = nodes_widget_now_s();
	desktop_widget_strcpy(model->latest_label, sizeof(model->latest_label),
			      DESKTOP_TEXT_WIDGET_NODES_EMPTY);
	nodes_widget_prepare_view(model);
}

static bool nodes_widget_refresh_summary(struct nodes_widget_state *state)
{
	struct nodes_widget_snapshot snapshot;

	if (state == NULL) {
		return false;
	}

	nodes_widget_snapshot_read(state, &snapshot);
	state->scan_ticks = 0U;
	return nodes_widget_apply_snapshot(&state->model, &snapshot);
}

static uint16_t nodes_widget_scroll_offset(uint16_t text_width, uint16_t width,
					  uint32_t ticks)
{
	uint16_t distance;
	uint32_t steps;
	uint32_t phase;

	if (text_width <= width) {
		return 0;
	}
	distance = text_width - width;
	steps = DIV_ROUND_UP(distance, NODES_WIDGET_SCROLL_STEP_PX);
	phase = ticks % (2U * NODES_WIDGET_SCROLL_PAUSE_TICKS + steps);
	if (phase < NODES_WIDGET_SCROLL_PAUSE_TICKS) {
		return 0;
	}
	return MIN((phase - NODES_WIDGET_SCROLL_PAUSE_TICKS + 1U) *
			   NODES_WIDGET_SCROLL_STEP_PX, distance);
}

static void nodes_widget_draw_scrolled_text(struct zui_draw_ctx *draw, struct zui_point pos,
					   uint16_t width, const char *text, uint32_t scroll)
{
	uint16_t text_width;

	if (draw == NULL || text == NULL) {
		return;
	}

	text_width = zui_draw_text_width(draw, text);
	if (text_width <= width) {
		zui_draw_text(draw, pos, text);
		return;
	}

	zui_draw_set_clip(draw, &(struct zui_rect){.x = pos.x, .y = pos.y - 8,
						   .width = width, .height = 10});
	pos.x -= nodes_widget_scroll_offset(text_width, width, scroll);
	zui_draw_text(draw, pos, text);
	zui_draw_clear_clip(draw);
}

static void nodes_widget_draw_header_background(struct zui_draw_ctx *draw)
{
	zui_draw_line(draw, (struct zui_point){.x = NODES_WIDGET_BODY_X + 2,
					       .y = NODES_WIDGET_BODY_Y},
		      (struct zui_point){.x = NODES_WIDGET_BODY_RIGHT - 2,
					 .y = NODES_WIDGET_BODY_Y});
	zui_draw_line(draw, (struct zui_point){.x = NODES_WIDGET_BODY_X + 1,
					       .y = NODES_WIDGET_BODY_Y + 1},
		      (struct zui_point){.x = NODES_WIDGET_BODY_RIGHT - 1,
					 .y = NODES_WIDGET_BODY_Y + 1});
	zui_draw_box(draw, &(struct zui_rect){.x = NODES_WIDGET_BODY_X,
					      .y = NODES_WIDGET_BODY_Y + 2,
					      .width = NODES_WIDGET_BODY_W,
					      .height = NODES_WIDGET_HEADER_BOTTOM -
							NODES_WIDGET_BODY_Y - 1});
}

static void nodes_widget_draw_frame(struct zui_draw_ctx *draw)
{
	zui_draw_line(draw, (struct zui_point){.x = NODES_WIDGET_BODY_X + 2,
					       .y = NODES_WIDGET_BODY_Y},
		      (struct zui_point){.x = NODES_WIDGET_BODY_RIGHT - 2,
					 .y = NODES_WIDGET_BODY_Y});
	zui_draw_line(draw, (struct zui_point){.x = NODES_WIDGET_BODY_X + 1,
					       .y = NODES_WIDGET_BODY_Y + 1},
		      (struct zui_point){.x = NODES_WIDGET_BODY_X,
					 .y = NODES_WIDGET_BODY_Y + 2});
	zui_draw_line(draw, (struct zui_point){.x = NODES_WIDGET_BODY_RIGHT - 1,
					       .y = NODES_WIDGET_BODY_Y + 1},
		      (struct zui_point){.x = NODES_WIDGET_BODY_RIGHT,
					 .y = NODES_WIDGET_BODY_Y + 2});
	zui_draw_line(draw, (struct zui_point){.x = NODES_WIDGET_BODY_X,
					       .y = NODES_WIDGET_BODY_Y + 3},
		      (struct zui_point){.x = NODES_WIDGET_BODY_X,
					 .y = NODES_WIDGET_BODY_BOTTOM});
	zui_draw_line(draw, (struct zui_point){.x = NODES_WIDGET_BODY_RIGHT,
					       .y = NODES_WIDGET_BODY_Y + 3},
		      (struct zui_point){.x = NODES_WIDGET_BODY_RIGHT,
					 .y = NODES_WIDGET_BODY_BOTTOM});
}

static void nodes_widget_draw_progress(struct zui_draw_ctx *draw, float ratio, const char *text)
{
	struct zui_rect rect = {.x = NODES_WIDGET_PROGRESS_X,
			       .y = NODES_WIDGET_PROGRESS_Y,
			       .width = NODES_WIDGET_PROGRESS_W,
			       .height = NODES_WIDGET_PROGRESS_H};

	desktop_widget_progress(draw, &rect, ratio, text);
}

static void nodes_widget_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct nodes_widget_state *state = user_data;
	const struct nodes_widget_model *model = state != NULL ? &state->model : NULL;
	int16_t name_x = 3;

	if (model == NULL) {
		return;
	}

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	nodes_widget_draw_header_background(draw);
	nodes_widget_draw_frame(draw);

	zui_draw_set_color(draw, ZUI_COLOR_XOR);
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	if (model->has_contact) {
		const char *role = nodes_widget_role_token(model->latest_role);
		uint16_t badge_width = zui_draw_text_width(draw, role) + 4U;

		zui_draw_rect(draw, &(struct zui_rect){.x = 3, .y = 14,
						    .width = badge_width, .height = 11});
		zui_draw_text(draw, (struct zui_point){.x = 5, .y = 23}, role);
		name_x += badge_width + 3;
	}
	nodes_widget_draw_scrolled_text(draw, (struct zui_point){.x = name_x, .y = 23},
					NODES_WIDGET_NAME_RIGHT - name_x, model->latest_label,
					model->latest_label_scroll);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 124, .y = 23},
			      ZUI_ALIGN_RIGHT, ZUI_ALIGN_BOTTOM, model->latest_age);

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	zui_draw_text(draw, (struct zui_point){.x = 16, .y = NODES_WIDGET_COUNT_Y},
		      DESKTOP_TEXT_NODES_TITLE);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 124, .y = NODES_WIDGET_COUNT_Y},
			      ZUI_ALIGN_RIGHT, ZUI_ALIGN_BOTTOM, model->count_text);
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	zui_draw_text(draw, (struct zui_point){.x = 16, .y = NODES_WIDGET_BROADCAST_Y},
		      DESKTOP_TEXT_WIDGET_NODES_ADVERT);
	zui_draw_icon(draw, (struct zui_point){.x = 5, .y = NODES_WIDGET_BROADCAST_ICON_Y},
		      desktop_widget_common_icon(ZUI_ASSET_ICON_BUTTON_SELECT));
	zui_draw_icon(draw, (struct zui_point){.x = 4, .y = NODES_WIDGET_COUNT_ICON_Y},
		      &I_store_8x8);

	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 124, .y = NODES_WIDGET_BROADCAST_Y},
			      ZUI_ALIGN_RIGHT, ZUI_ALIGN_BOTTOM, model->advert_age);

	nodes_widget_draw_progress(draw, model->progress_ratio, model->progress_text);
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
}

static bool nodes_widget_input(const struct zui_input_event *event, void *user_data)
{
	struct nodes_widget_state *state = user_data;

	if (event == NULL || state == NULL) {
		return false;
	}

	if (event->code != ZUI_INPUT_CODE_SELECT ||
	    event->action != ZUI_INPUT_ACTION_LONG_PRESS) {
		return false;
	}

#if defined(CONFIG_MESHBUS_CONTACT)
	int rc = meshbus_meshcore_advert_request(true);

	LOG_DBG("nodes widget: meshbus_meshcore_advert_request(flood=1) -> %d", rc);
	if (rc == 0) {
		state->model.local_advert_seen = true;
		state->model.local_advert_ticks = 0U;
		nodes_widget_prepare_view(&state->model);
		if (state->screen != NULL) {
			(void)zui_screen_request_redraw(state->screen);
		}
	}
#endif

	return true;
}

static const struct zui_screen_ops nodes_widget_ops = {
	.draw = nodes_widget_draw,
	.input = nodes_widget_input,
};

static struct zui_screen *nodes_widget_screen_create(
	struct meshbus_desktop_dashboard_widget *wctx)
{
	ARG_UNUSED(wctx);

	if (nodes_widget.screen == NULL) {
		nodes_widget_model_defaults(&nodes_widget.model);
		(void)nodes_widget_refresh_summary(&nodes_widget);
		(void)atomic_set(&nodes_widget_dirty, 0);
		nodes_widget.screen =
			zui_screen_create(&nodes_widget_ops, &nodes_widget);
	}

	return nodes_widget.screen;
}

static uint32_t nodes_widget_tick(struct meshbus_desktop_dashboard_widget *wctx)
{
	bool dirty;
	bool should_scan = false;

	ARG_UNUSED(wctx);

	nodes_widget.model.age_ticks++;
	nodes_widget.model.latest_label_scroll++;
	if (nodes_widget.model.local_advert_seen &&
	    nodes_widget.model.local_advert_ticks < UINT32_MAX) {
		nodes_widget.model.local_advert_ticks++;
	}
	if (nodes_widget.scan_ticks < UINT8_MAX) {
		nodes_widget.scan_ticks++;
	}

	dirty = atomic_set(&nodes_widget_dirty, 0) != 0;
	if (dirty || nodes_widget.scan_ticks >= NODES_WIDGET_REFRESH_TICKS) {
		should_scan = true;
	}

	if (should_scan) {
		(void)nodes_widget_refresh_summary(&nodes_widget);
	}
	nodes_widget_prepare_view(&nodes_widget.model);

	if (nodes_widget.screen != NULL) {
		(void)zui_screen_request_redraw(nodes_widget.screen);
	}

	return NODES_WIDGET_TICK_INTERVAL_MS;
}

#if defined(CONFIG_ZBUS)
static void nodes_widget_contact_store_listener_cb(const struct zbus_channel *chan)
{
	if (chan != &meshbus_contact_store_change_chan) {
		return;
	}

	(void)atomic_set(&nodes_widget_dirty, 1);
}

ZBUS_LISTENER_DEFINE(meshbus_desktop_nodes_widget_contact_store_listener,
		     nodes_widget_contact_store_listener_cb);
ZBUS_CHAN_ADD_OBS(meshbus_contact_store_change_chan,
		  meshbus_desktop_nodes_widget_contact_store_listener, 2);
#endif

MESHBUS_DESKTOP_DASHBOARD_WIDGETS_DEFINE(MESHBUS_DESKTOP_DASHBOARD_WIDGET_ID_NODES,
					 MESHBUS_DESKTOP_DASHBOARD_WIDGET_TITLE_NODES,
					 nodes_widget_screen_create,
					 nodes_widget_tick,
					 MESHBUS_DESKTOP_APP_ID_NODES);
