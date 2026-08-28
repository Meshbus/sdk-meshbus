/* SPDX-License-Identifier: Apache-2.0 */

#include "widget_common.h"

#include <zephyr/logging/log.h>

#include "services/telemetry_cache.h"

LOG_MODULE_DECLARE(meshbus_desktop, CONFIG_MESHBUS_DESKTOP_LOG_LEVEL);

#define TELEMETRY_WIDGET_TICK_ACTIVE_MS 1000U
#define TELEMETRY_WIDGET_TICK_IDLE_MS   3000U
#define TELEMETRY_WIDGET_VALUE_MAX      18U
#define TELEMETRY_WIDGET_LABEL_MAX      14U
#define TELEMETRY_WIDGET_AGE_MAX        12U
#define TELEMETRY_WIDGET_COUNT_MAX      8U
#define TELEMETRY_WIDGET_ROW_MAX        16U
#define TELEMETRY_WIDGET_VISIBLE_LINES  4U
#define TELEMETRY_WIDGET_LABEL_W        60U
#define TELEMETRY_WIDGET_FRAME_LEFT     1
#define TELEMETRY_WIDGET_FRAME_RIGHT    126
#define TELEMETRY_WIDGET_FRAME_TOP      13
#define TELEMETRY_WIDGET_FRAME_BOTTOM   63
#define TELEMETRY_WIDGET_HEADER_BOTTOM  25

struct telemetry_widget_row {
	char label[TELEMETRY_WIDGET_LABEL_MAX];
	char value[TELEMETRY_WIDGET_VALUE_MAX];
};

struct telemetry_widget_model {
	bool available;
	bool enabled;
	bool has_data;
	uint32_t update_seq;
	uint32_t row_count;
	uint32_t total_count;
	char latest_age[TELEMETRY_WIDGET_AGE_MAX];
	char total[TELEMETRY_WIDGET_COUNT_MAX];
	struct telemetry_widget_row rows[TELEMETRY_WIDGET_ROW_MAX];
};

struct telemetry_widget_state {
	struct zui_screen *screen;
	struct telemetry_widget_model model;
	uint32_t selected_position;
};

static struct telemetry_widget_state telemetry_widget;

static const char *telemetry_widget_channel_name(enum sensor_channel chan)
{
	switch (chan) {
	case SENSOR_CHAN_ACCEL_XYZ:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_ACCEL;
	case SENSOR_CHAN_GYRO_XYZ:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_GYRO;
	case SENSOR_CHAN_MAGN_XYZ:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_MAGN;
	case SENSOR_CHAN_DIE_TEMP:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_DIE_TEMP;
	case SENSOR_CHAN_AMBIENT_TEMP:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_AMBIENT;
	case SENSOR_CHAN_PRESS:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_PRESS;
	case SENSOR_CHAN_HUMIDITY:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_HUMIDITY;
	case SENSOR_CHAN_AMBIENT_LIGHT:
	case SENSOR_CHAN_LIGHT:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_LIGHT;
	case SENSOR_CHAN_VOLTAGE:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_VOLTAGE;
	case SENSOR_CHAN_CURRENT:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_CURRENT;
	case SENSOR_CHAN_GAUGE_STATE_OF_CHARGE:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_BATTERY;
	default:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_CHANNEL;
	}
}

static void telemetry_widget_strappend(char *out, size_t out_size, const char *text)
{
	size_t off;

	if (out == NULL || out_size == 0U || text == NULL) {
		return;
	}

	off = strnlen(out, out_size);
	for (size_t i = 0U; text[i] != '\0' && off + 1U < out_size; i++) {
		out[off++] = text[i];
	}
	out[off] = '\0';
}

static void telemetry_widget_build_label(char *out, size_t out_size, const char *base,
					 uint8_t index, uint8_t count)
{
	uint8_t axis_index;

	if (out == NULL || out_size == 0U) {
		return;
	}

	desktop_widget_strcpy(out, out_size, base);
	if (count <= 1U) {
		return;
	}

	telemetry_widget_strappend(out, out_size, " ");
	axis_index = MIN(index, (uint8_t)(DESKTOP_TEXT_WIDGET_TELEMETRY_AXIS_COUNT - 1U));
	telemetry_widget_strappend(out, out_size,
				   DESKTOP_TEXT_WIDGET_TELEMETRY_AXIS_VALUES[axis_index]);
}

static void telemetry_widget_format_sensor_value(char *out, size_t out_size,
						 const struct sensor_value *value)
{
	int64_t micro;
	uint64_t abs_micro;
	bool neg;

	if (out == NULL || out_size == 0U) {
		return;
	}
	if (value == NULL) {
		desktop_widget_strcpy(out, out_size, DESKTOP_TEXT_COMMON_NOT_AVAILABLE);
		return;
	}

	micro = sensor_value_to_micro(value);
	neg = micro < 0;
	abs_micro = (uint64_t)(neg ? -micro : micro);
	(void)snprintk(out, out_size, "%s%llu.%02llu", neg ? "-" : "",
		       (unsigned long long)(abs_micro / 1000000ULL),
		       (unsigned long long)((abs_micro % 1000000ULL) / 10000ULL));
}

static void telemetry_widget_format_age(char *out, size_t out_size, uint32_t timestamp)
{
	uint32_t age_ms;

	if (out == NULL || out_size == 0U) {
		return;
	}
	if (timestamp == 0U) {
		desktop_widget_strcpy(out, out_size, DESKTOP_TEXT_COMMON_NOT_AVAILABLE);
		return;
	}

	age_ms = k_uptime_get_32() - timestamp;
	if (age_ms < 1000U) {
		(void)snprintk(out, out_size, DESKTOP_TEXT_WIDGET_TELEMETRY_AGE_FORMAT, 0U);
	} else {
		(void)snprintk(out, out_size, DESKTOP_TEXT_WIDGET_TELEMETRY_AGE_FORMAT,
			       (unsigned int)(age_ms / 1000U));
	}
}

static void telemetry_widget_model_defaults(struct telemetry_widget_model *model)
{
	if (model == NULL) {
		return;
	}

	memset(model, 0, sizeof(*model));
	desktop_widget_strcpy(model->latest_age, sizeof(model->latest_age),
			      DESKTOP_TEXT_COMMON_NOT_AVAILABLE);
	(void)snprintk(model->total, sizeof(model->total),
		       DESKTOP_TEXT_WIDGET_TELEMETRY_COUNT_FORMAT, 0U);
}

static void telemetry_widget_append_row(struct telemetry_widget_model *model, const char *label,
					const char *value)
{
	struct telemetry_widget_row *row;

	if (model == NULL || model->row_count >= ARRAY_SIZE(model->rows)) {
		return;
	}

	row = &model->rows[model->row_count++];
	desktop_widget_strcpy(row->label, sizeof(row->label), label);
	desktop_widget_strcpy(row->value, sizeof(row->value), value);
}

static void telemetry_widget_snapshot_read(struct telemetry_widget_model *snapshot)
{
	meshbus_telemetry_config cfg;
	struct desktop_telemetry_cache_entry latest;
	bool has_latest;
	size_t binding_count;

	if (snapshot == NULL) {
		return;
	}

	telemetry_widget_model_defaults(snapshot);
	if (meshbus_telemetry_config_get(&cfg) != 0) {
		return;
	}

	snapshot->available = true;
	snapshot->enabled = cfg.enabled;
	snapshot->update_seq = desktop_telemetry_cache_update_seq();

	has_latest = desktop_telemetry_cache_latest(&latest);
	if (has_latest) {
		snapshot->has_data = true;
		telemetry_widget_format_age(snapshot->latest_age, sizeof(snapshot->latest_age),
					    latest.timestamp);
	}

	binding_count = meshbus_telemetry_bindings_count();
	for (size_t i = 0U; i < binding_count; i++) {
		struct meshbus_telemetry_binding binding;
		struct desktop_telemetry_cache_entry entry;
		const char *base;
		bool has_entry;
		uint8_t value_count;

		if (meshbus_telemetry_binding_get(i, &binding) != 0) {
			continue;
		}

		base = telemetry_widget_channel_name(binding.chan);
		has_entry = desktop_telemetry_cache_get(binding.chan, &entry);
		value_count = (uint8_t)MIN(meshbus_telemetry_channel_value_count(binding.chan),
					   (size_t)MESHBUS_TELEMETRY_MAX_VALUES);
		if (has_entry && entry.value_count > 0U) {
			value_count = MIN(entry.value_count, (uint8_t)MESHBUS_TELEMETRY_MAX_VALUES);
		}
		value_count = MAX(value_count, 1U);
		snapshot->total_count += value_count;

		for (uint8_t value_idx = 0U; value_idx < value_count; value_idx++) {
			char label[TELEMETRY_WIDGET_LABEL_MAX];
			char value[TELEMETRY_WIDGET_VALUE_MAX];

			telemetry_widget_build_label(label, sizeof(label), base, value_idx,
						     value_count);
			if (has_entry && value_idx < entry.value_count) {
				telemetry_widget_format_sensor_value(value, sizeof(value),
								     &entry.values[value_idx]);
			} else {
				desktop_widget_strcpy(value, sizeof(value),
						      DESKTOP_TEXT_WIDGET_TELEMETRY_NO_DATA);
			}
			telemetry_widget_append_row(snapshot, label, value);
		}
	}

	(void)snprintk(snapshot->total, sizeof(snapshot->total),
		       DESKTOP_TEXT_WIDGET_TELEMETRY_COUNT_FORMAT,
		       (unsigned int)snapshot->total_count);
}

static bool telemetry_widget_model_apply(struct telemetry_widget_model *model,
					 const struct telemetry_widget_model *snapshot)
{
	if (model == NULL || snapshot == NULL) {
		return false;
	}
	if (memcmp(model, snapshot, sizeof(*model)) == 0) {
		return false;
	}

	*model = *snapshot;
	return true;
}

static void telemetry_widget_draw_header_background(struct zui_draw_ctx *draw)
{
	zui_draw_line(draw, (struct zui_point){.x = TELEMETRY_WIDGET_FRAME_LEFT + 2,
					       .y = TELEMETRY_WIDGET_FRAME_TOP},
		      (struct zui_point){.x = TELEMETRY_WIDGET_FRAME_RIGHT - 2,
					 .y = TELEMETRY_WIDGET_FRAME_TOP});
	zui_draw_line(draw, (struct zui_point){.x = TELEMETRY_WIDGET_FRAME_LEFT + 1,
					       .y = TELEMETRY_WIDGET_FRAME_TOP + 1},
		      (struct zui_point){.x = TELEMETRY_WIDGET_FRAME_RIGHT - 1,
					 .y = TELEMETRY_WIDGET_FRAME_TOP + 1});
	zui_draw_box(draw, &(struct zui_rect){.x = TELEMETRY_WIDGET_FRAME_LEFT,
					      .y = TELEMETRY_WIDGET_FRAME_TOP + 2,
					      .width = TELEMETRY_WIDGET_FRAME_RIGHT -
						       TELEMETRY_WIDGET_FRAME_LEFT + 1,
					      .height = TELEMETRY_WIDGET_HEADER_BOTTOM -
							TELEMETRY_WIDGET_FRAME_TOP - 1});
}

static void telemetry_widget_draw_frame(struct zui_draw_ctx *draw)
{
	zui_draw_line(draw, (struct zui_point){.x = TELEMETRY_WIDGET_FRAME_LEFT + 2,
					       .y = TELEMETRY_WIDGET_FRAME_TOP},
		      (struct zui_point){.x = TELEMETRY_WIDGET_FRAME_RIGHT - 2,
					 .y = TELEMETRY_WIDGET_FRAME_TOP});
	zui_draw_line(draw, (struct zui_point){.x = TELEMETRY_WIDGET_FRAME_LEFT + 1,
					       .y = TELEMETRY_WIDGET_FRAME_TOP + 1},
		      (struct zui_point){.x = TELEMETRY_WIDGET_FRAME_LEFT,
					 .y = TELEMETRY_WIDGET_FRAME_TOP + 2});
	zui_draw_line(draw, (struct zui_point){.x = TELEMETRY_WIDGET_FRAME_RIGHT - 1,
					       .y = TELEMETRY_WIDGET_FRAME_TOP + 1},
		      (struct zui_point){.x = TELEMETRY_WIDGET_FRAME_RIGHT,
					 .y = TELEMETRY_WIDGET_FRAME_TOP + 2});
	zui_draw_line(draw, (struct zui_point){.x = TELEMETRY_WIDGET_FRAME_LEFT,
					       .y = TELEMETRY_WIDGET_FRAME_TOP + 3},
		      (struct zui_point){.x = TELEMETRY_WIDGET_FRAME_LEFT,
					 .y = TELEMETRY_WIDGET_FRAME_BOTTOM});
	zui_draw_line(draw, (struct zui_point){.x = TELEMETRY_WIDGET_FRAME_RIGHT,
					       .y = TELEMETRY_WIDGET_FRAME_TOP + 3},
		      (struct zui_point){.x = TELEMETRY_WIDGET_FRAME_RIGHT,
					 .y = TELEMETRY_WIDGET_FRAME_BOTTOM});
}

static void telemetry_widget_draw_text_clipped(struct zui_draw_ctx *draw, struct zui_point pos,
					       uint16_t width, const char *text)
{
	if (zui_draw_text_width(draw, text) <= width) {
		zui_draw_text(draw, pos, text);
		return;
	}

	zui_draw_set_clip(draw, &(struct zui_rect){.x = pos.x, .y = pos.y - 8,
						   .width = width, .height = 10});
	zui_draw_text(draw, pos, text);
	zui_draw_clear_clip(draw);
}

static uint32_t telemetry_widget_first_visible(const struct telemetry_widget_state *state)
{
	uint32_t row_count;

	if (state == NULL) {
		return 0U;
	}

	row_count = state->model.row_count;
	if (row_count <= TELEMETRY_WIDGET_VISIBLE_LINES ||
	    state->selected_position + 1U <= TELEMETRY_WIDGET_VISIBLE_LINES) {
		return 0U;
	}

	return state->selected_position + 1U - TELEMETRY_WIDGET_VISIBLE_LINES;
}

static void telemetry_widget_draw(struct zui_draw_ctx *draw, void *user_data)
{
	const struct telemetry_widget_state *state = user_data;
	const struct telemetry_widget_model *model;
	uint32_t first_row;

	model = state != NULL ? &state->model : NULL;
	if (model == NULL) {
		return;
	}

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	telemetry_widget_draw_header_background(draw);
	telemetry_widget_draw_frame(draw);

	zui_draw_set_color(draw, ZUI_COLOR_XOR);
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	telemetry_widget_draw_text_clipped(draw, (struct zui_point){.x = 4, .y = 23}, 88U,
					   model->latest_age);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 124, .y = 23},
			      ZUI_ALIGN_RIGHT, ZUI_ALIGN_BOTTOM, model->total);

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	if (state != NULL && state->selected_position > 0U) {
		zui_draw_icon(draw, (struct zui_point){.x = 118, .y = 28},
			      desktop_widget_common_icon(ZUI_ASSET_ICON_PIN_POINTER));
	}
	if (state != NULL &&
	    state->selected_position + 1U < model->row_count) {
		zui_draw_icon(draw, (struct zui_point){.x = 118, .y = 58},
			      desktop_widget_common_icon(ZUI_ASSET_ICON_ARROW_DOWN_SMALL));
	}

	if (model->row_count == 0U) {
		zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
		zui_draw_text(draw, (struct zui_point){.x = 4, .y = 45},
			      DESKTOP_TEXT_WIDGET_TELEMETRY_NO_DATA);
		return;
	}

	first_row = telemetry_widget_first_visible(state);
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	for (uint32_t i = 0U; i < TELEMETRY_WIDGET_VISIBLE_LINES; i++) {
		uint32_t row_index = first_row + i;
		const struct telemetry_widget_row *row;
		int16_t y = (int16_t)(35 + (i * 9));
		int16_t label_x = 9;
		uint16_t label_w = TELEMETRY_WIDGET_LABEL_W;

		if (row_index >= model->row_count) {
			break;
		}

		row = &model->rows[row_index];
		if (state != NULL && row_index == state->selected_position) {
			zui_draw_text(draw, (struct zui_point){.x = 4, .y = y},
				      DESKTOP_TEXT_COMMON_ROUTE);
			label_x = 12;
			label_w -= 4U;
		}

		telemetry_widget_draw_text_clipped(draw, (struct zui_point){.x = label_x, .y = y},
						   label_w, row->label);
		zui_draw_text_aligned(draw, (struct zui_point){.x = 114, .y = y},
				      ZUI_ALIGN_RIGHT, ZUI_ALIGN_BOTTOM, row->value);
	}
}

static bool telemetry_widget_select_position(struct telemetry_widget_state *state, bool newer)
{
	if (state == NULL || state->model.row_count <= 1U) {
		return false;
	}

	if (newer) {
		if (state->selected_position == 0U) {
			return false;
		}
		state->selected_position--;
	} else {
		if (state->selected_position + 1U >= state->model.row_count) {
			return false;
		}
		state->selected_position++;
	}

	if (state->screen != NULL) {
		(void)zui_screen_request_redraw(state->screen);
	}
	return true;
}

static bool telemetry_widget_input(const struct zui_input_event *event, void *user_data)
{
	struct telemetry_widget_state *state = user_data;

	if (event == NULL || state == NULL) {
		return false;
	}

	if (event->code == ZUI_INPUT_CODE_SELECT &&
	    event->action == ZUI_INPUT_ACTION_LONG_PRESS) {
		int rc = meshbus_telemetry_sample_trigger();

		if (rc == 0) {
			LOG_INF("Telemetry widget sample triggered");
		} else {
			LOG_WRN("Telemetry widget sample trigger failed: %d", rc);
		}
		return true;
	}

	if (event->action != ZUI_INPUT_ACTION_CLICK ||
	    (event->code != ZUI_INPUT_CODE_UP && event->code != ZUI_INPUT_CODE_DOWN)) {
		return false;
	}

	return telemetry_widget_select_position(state, event->code == ZUI_INPUT_CODE_UP);
}

static const struct zui_screen_ops telemetry_widget_ops = {
	.draw = telemetry_widget_draw,
	.input = telemetry_widget_input,
};

static struct zui_screen *telemetry_widget_screen_create(
	struct meshbus_desktop_dashboard_widget *wctx)
{
	struct telemetry_widget_model snapshot;

	ARG_UNUSED(wctx);

	if (telemetry_widget.screen == NULL) {
		telemetry_widget_snapshot_read(&snapshot);
		(void)telemetry_widget_model_apply(&telemetry_widget.model, &snapshot);
		telemetry_widget.screen =
			zui_screen_create(&telemetry_widget_ops, &telemetry_widget);
	}

	return telemetry_widget.screen;
}

static uint32_t telemetry_widget_tick(struct meshbus_desktop_dashboard_widget *wctx)
{
	struct telemetry_widget_model snapshot;

	ARG_UNUSED(wctx);

	telemetry_widget_snapshot_read(&snapshot);
	if (telemetry_widget_model_apply(&telemetry_widget.model, &snapshot) &&
	    telemetry_widget.screen != NULL) {
		if (telemetry_widget.selected_position >= telemetry_widget.model.row_count) {
			telemetry_widget.selected_position =
				telemetry_widget.model.row_count > 0U ?
					telemetry_widget.model.row_count - 1U :
					0U;
		}
		(void)zui_screen_request_redraw(telemetry_widget.screen);
	}

	if (!telemetry_widget.model.available || !telemetry_widget.model.enabled) {
		return TELEMETRY_WIDGET_TICK_IDLE_MS;
	}

	return TELEMETRY_WIDGET_TICK_ACTIVE_MS;
}

MESHBUS_DESKTOP_DASHBOARD_WIDGETS_DEFINE(MESHBUS_DESKTOP_DASHBOARD_WIDGET_ID_TELEMETRY,
					 MESHBUS_DESKTOP_DASHBOARD_WIDGET_TITLE_TELEMETRY,
					 telemetry_widget_screen_create,
					 telemetry_widget_tick,
					 MESHBUS_DESKTOP_APP_ID_SYSTEM);
