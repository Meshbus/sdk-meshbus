/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/devicetree.h>

#if defined(CONFIG_MESHBUS_GNSS) &&                                                    \
	(DT_HAS_CHOSEN(meshbus_compass) || DT_HAS_CHOSEN(meshbus_gnss))
#include "widget_common.h"

#include <zephyr/meshbus/gnss.h>

#define COMPASS_WIDGET_TICK_NORMAL_MS    100U
#define COMPASS_WIDGET_TICK_OFF_MS       3000U
#define COMPASS_WIDGET_ACQUIRE_RETRY_MS  1000U
#define COMPASS_WIDGET_TICK_STEP_DEG     5
#define COMPASS_WIDGET_LABEL_STEP_DEG    45
#define COMPASS_WIDGET_PX_PER_DEG_NUM    6
#define COMPASS_WIDGET_PX_PER_DEG_DEN    5
#define COMPASS_WIDGET_MARGIN_X          3

struct compass_widget_model {
	bool available;
	bool active;
	bool valid;
	uint8_t accuracy;
	uint8_t calibration_hint;
	uint8_t runtime_state;
	int32_t last_error;
	uint16_t heading_deg;
	char heading_str[4];
	char dir_str[6];
};

struct compass_widget_snapshot {
	bool available;
	bool active;
	bool valid;
	uint8_t accuracy;
	uint8_t calibration_hint;
	uint8_t runtime_state;
	int32_t last_error;
	uint16_t heading_deg;
	char heading_str[4];
	char dir_str[6];
};

struct compass_widget_state {
	struct zui_screen *screen;
	struct compass_widget_model model;
	struct compass_widget_snapshot snapshot;
	bool runtime_lease_held;
	bool runtime_release_pending;
	bool runtime_retry_armed;
	uint32_t runtime_retry_deadline_ms;
};

static struct compass_widget_state compass_widget;

static inline uint16_t compass_widget_norm_360(int32_t deg)
{
	deg %= 360;
	if (deg < 0) {
		deg += 360;
	}

	return (uint16_t)deg;
}

static inline int32_t compass_widget_wrap_180(int32_t deg)
{
	deg %= 360;
	if (deg > 180) {
		deg -= 360;
	} else if (deg < -180) {
		deg += 360;
	}

	return deg;
}

static inline int32_t compass_widget_deg_to_px(int32_t deg)
{
	return (deg * COMPASS_WIDGET_PX_PER_DEG_NUM) / COMPASS_WIDGET_PX_PER_DEG_DEN;
}

static inline int32_t compass_widget_floor_step(int32_t value, int32_t step)
{
	int32_t rem;

	if (step <= 0) {
		return value;
	}

	rem = value % step;
	if (rem < 0) {
		rem += step;
	}

	return value - rem;
}

static const char *compass_widget_label_for_deg(uint16_t deg)
{
	deg = compass_widget_norm_360(deg);

	switch (deg) {
	case 0:
		return DESKTOP_TEXT_WIDGET_COMPASS_DIRECTION_VALUES[0];
	case 45:
		return DESKTOP_TEXT_WIDGET_COMPASS_DIRECTION_VALUES[1];
	case 90:
		return DESKTOP_TEXT_WIDGET_COMPASS_DIRECTION_VALUES[2];
	case 135:
		return DESKTOP_TEXT_WIDGET_COMPASS_DIRECTION_VALUES[3];
	case 180:
		return DESKTOP_TEXT_WIDGET_COMPASS_DIRECTION_VALUES[4];
	case 225:
		return DESKTOP_TEXT_WIDGET_COMPASS_DIRECTION_VALUES[5];
	case 270:
		return DESKTOP_TEXT_WIDGET_COMPASS_DIRECTION_VALUES[6];
	case 315:
		return DESKTOP_TEXT_WIDGET_COMPASS_DIRECTION_VALUES[7];
	default:
		return NULL;
	}
}

static const char *compass_widget_accuracy_text(uint8_t accuracy, bool valid)
{
	if (!valid) {
		return DESKTOP_TEXT_WIDGET_COMPASS_ACCURACY_UNAVAILABLE;
	}

	switch ((enum meshbus_gnss_heading_accuracy)accuracy) {
	case MESHBUS_GNSS_HEADING_ACCURACY_LOW:
		return DESKTOP_TEXT_WIDGET_COMPASS_ACCURACY_LOW;
	case MESHBUS_GNSS_HEADING_ACCURACY_MEDIUM:
		return DESKTOP_TEXT_WIDGET_COMPASS_ACCURACY_MEDIUM;
	case MESHBUS_GNSS_HEADING_ACCURACY_HIGH:
		return DESKTOP_TEXT_WIDGET_COMPASS_ACCURACY_HIGH;
	case MESHBUS_GNSS_HEADING_ACCURACY_UNRELIABLE:
	default:
		return DESKTOP_TEXT_WIDGET_COMPASS_ACCURACY_UNRELIABLE;
	}
}

static const char *compass_widget_status_text(const struct compass_widget_model *model)
{
	if (model == NULL || !model->available || !model->active) {
		return DESKTOP_TEXT_WIDGET_COMPASS_STATUS_OFF;
	}
	if (model->last_error != 0 || model->runtime_state == MESHBUS_GNSS_HEADING_STATE_ERROR ||
	    model->runtime_state == MESHBUS_GNSS_HEADING_STATE_UNAVAILABLE) {
		return DESKTOP_TEXT_WIDGET_COMPASS_STATUS_ERROR;
	}
	if (!model->valid) {
		return DESKTOP_TEXT_WIDGET_COMPASS_STATUS_WAIT;
	}

	switch ((enum meshbus_gnss_heading_calibration_hint)model->calibration_hint) {
	case MESHBUS_GNSS_HEADING_CALIBRATION_HINT_FIGURE_EIGHT:
		return DESKTOP_TEXT_WIDGET_COMPASS_STATUS_CALIBRATE;
	case MESHBUS_GNSS_HEADING_CALIBRATION_HINT_KEEP_LEVEL:
		return DESKTOP_TEXT_WIDGET_COMPASS_STATUS_LEVEL;
	case MESHBUS_GNSS_HEADING_CALIBRATION_HINT_NONE:
	default:
		return DESKTOP_TEXT_WIDGET_COMPASS_STATUS_OK;
	}
}

static const char *compass_widget_direction_for_heading(uint16_t heading_deg)
{
	uint16_t sector = (uint16_t)((heading_deg + 22U) / 45U) % 8U;

	switch (sector) {
	case 0:
		return DESKTOP_TEXT_WIDGET_COMPASS_DIRECTION_VALUES[0];
	case 1:
		return DESKTOP_TEXT_WIDGET_COMPASS_DIRECTION_VALUES[1];
	case 2:
		return DESKTOP_TEXT_WIDGET_COMPASS_DIRECTION_VALUES[2];
	case 3:
		return DESKTOP_TEXT_WIDGET_COMPASS_DIRECTION_VALUES[3];
	case 4:
		return DESKTOP_TEXT_WIDGET_COMPASS_DIRECTION_VALUES[4];
	case 5:
		return DESKTOP_TEXT_WIDGET_COMPASS_DIRECTION_VALUES[5];
	case 6:
		return DESKTOP_TEXT_WIDGET_COMPASS_DIRECTION_VALUES[6];
	case 7:
	default:
		return DESKTOP_TEXT_WIDGET_COMPASS_DIRECTION_VALUES[7];
	}
}

static void compass_widget_format_heading(char *out, size_t out_size,
					  uint16_t heading_deg)
{
	if (out == NULL || out_size == 0U) {
		return;
	}

	(void)snprintk(out, out_size, "%u", (unsigned int)(heading_deg % 360U));
}

static uint16_t compass_widget_heading_from_millideg(int32_t heading_milli_deg)
{
	int32_t normalized = heading_milli_deg % 360000;

	if (normalized < 0) {
		normalized += 360000;
	}
	return (uint16_t)(((normalized + 500) / 1000) % 360);
}

static void compass_widget_snapshot_defaults(
	struct compass_widget_snapshot *snapshot)
{
	if (snapshot == NULL) {
		return;
	}

	snapshot->available = false;
	snapshot->active = false;
	snapshot->valid = false;
	snapshot->accuracy = MESHBUS_GNSS_HEADING_ACCURACY_UNRELIABLE;
	snapshot->calibration_hint = MESHBUS_GNSS_HEADING_CALIBRATION_HINT_NONE;
	snapshot->runtime_state = MESHBUS_GNSS_HEADING_STATE_UNAVAILABLE;
	snapshot->last_error = 0;
	snapshot->heading_deg = 0U;
	desktop_widget_strcpy(snapshot->heading_str, sizeof(snapshot->heading_str),
			      DESKTOP_TEXT_WIDGET_COMPASS_PLACEHOLDER_HEADING);
	desktop_widget_strcpy(snapshot->dir_str, sizeof(snapshot->dir_str),
			      DESKTOP_TEXT_WIDGET_COMPASS_PLACEHOLDER_DIRECTION);
}

static uint32_t compass_widget_tick_period_ms(
	const struct compass_widget_snapshot *snapshot,
	const struct meshbus_gnss_heading_runtime_status *status)
{
	if (snapshot == NULL || !snapshot->available || !snapshot->active) {
		return COMPASS_WIDGET_TICK_OFF_MS;
	}
	if (status != NULL && status->sample_interval_ms > 0U) {
		return status->sample_interval_ms;
	}

	return COMPASS_WIDGET_TICK_NORMAL_MS;
}

static void compass_widget_snapshot_read(
	struct compass_widget_snapshot *snapshot,
	struct meshbus_gnss_heading_runtime_status *status)
{
	struct meshbus_gnss_heading_snapshot compass_snapshot;

	if (snapshot == NULL) {
		return;
	}

	compass_widget_snapshot_defaults(snapshot);
	if (status == NULL || meshbus_gnss_heading_runtime_status_get(status) != 0 ||
	    meshbus_gnss_heading_snapshot_get(&compass_snapshot) != 0) {
		return;
	}

	snapshot->available = true;
	snapshot->active = status->active;
	snapshot->runtime_state = compass_snapshot.state;
	if (!snapshot->active) {
		return;
	}

	snapshot->last_error = compass_snapshot.last_error;
	snapshot->accuracy = compass_snapshot.accuracy;
	snapshot->calibration_hint = compass_snapshot.calibration_hint;
	snapshot->valid = compass_snapshot.valid && compass_snapshot.last_error == 0;
	if (!snapshot->valid) {
		return;
	}

	snapshot->heading_deg =
		compass_widget_heading_from_millideg(compass_snapshot.heading_milli_deg);
	compass_widget_format_heading(snapshot->heading_str,
						sizeof(snapshot->heading_str),
						snapshot->heading_deg);
	desktop_widget_strcpy(snapshot->dir_str, sizeof(snapshot->dir_str),
			      compass_widget_direction_for_heading(
				      snapshot->heading_deg));
}

static bool compass_widget_apply_snapshot(
	struct compass_widget_model *model,
	const struct compass_widget_snapshot *snapshot)
{
	bool changed = false;

	if (model == NULL || snapshot == NULL) {
		return false;
	}

	changed |= model->available != snapshot->available;
	changed |= model->active != snapshot->active;
	changed |= model->valid != snapshot->valid;
	changed |= model->accuracy != snapshot->accuracy;
	changed |= model->calibration_hint != snapshot->calibration_hint;
	changed |= model->runtime_state != snapshot->runtime_state;
	changed |= model->last_error != snapshot->last_error;
	changed |= model->heading_deg != snapshot->heading_deg;
	changed |= strcmp(model->heading_str, snapshot->heading_str) != 0;
	changed |= strcmp(model->dir_str, snapshot->dir_str) != 0;
	if (!changed) {
		return false;
	}

	model->available = snapshot->available;
	model->active = snapshot->active;
	model->valid = snapshot->valid;
	model->accuracy = snapshot->accuracy;
	model->calibration_hint = snapshot->calibration_hint;
	model->runtime_state = snapshot->runtime_state;
	model->last_error = snapshot->last_error;
	model->heading_deg = snapshot->heading_deg;
	desktop_widget_strcpy(model->heading_str, sizeof(model->heading_str),
			      snapshot->heading_str);
	desktop_widget_strcpy(model->dir_str, sizeof(model->dir_str), snapshot->dir_str);
	return true;
}

static void compass_widget_model_defaults(struct compass_widget_model *model)
{
	struct compass_widget_snapshot snapshot;

	if (model == NULL) {
		return;
	}

	memset(model, 0, sizeof(*model));
	compass_widget_snapshot_defaults(&snapshot);
	(void)compass_widget_apply_snapshot(model, &snapshot);
}

static bool compass_widget_needs_figure_eight(
	const struct compass_widget_model *model)
{
	return model != NULL && model->available && model->active &&
	       model->last_error == 0 &&
	       model->calibration_hint ==
		       MESHBUS_GNSS_HEADING_CALIBRATION_HINT_FIGURE_EIGHT;
}

static void compass_widget_draw_figure_eight_prompt(struct zui_draw_ctx *draw)
{
	desktop_widget_frame(draw, 1, 13, 125, 48);

	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 64, .y = 22},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER,
			      DESKTOP_TEXT_WIDGET_COMPASS_CALIBRATION_TITLE);

	zui_draw_set_font(draw, ZUI_FONT_BIG_NUMBERS);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 64, .y = 43},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_BOTTOM, "8");

	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 64, .y = 54},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER,
			      DESKTOP_TEXT_WIDGET_COMPASS_CALIBRATION_ACTION);
}

static void compass_widget_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct compass_widget_state *state = user_data;
	const struct compass_widget_model *model =
		state != NULL ? &state->model : NULL;
	const int16_t w = (int16_t)zui_draw_width(draw);
	const int16_t y0 = MESHBUS_DESKTOP_DASHBOARD_WIDGETS_START_Y + 2;
	const int16_t cx = w / 2;
	const int16_t labels_y = y0 + 10;
	const int16_t scale_y = y0 + 14;
	const int16_t visible_px = (w / 2) - COMPASS_WIDGET_MARGIN_X;
	const int16_t span_deg =
		(visible_px * COMPASS_WIDGET_PX_PER_DEG_DEN) / COMPASS_WIDGET_PX_PER_DEG_NUM;
	uint16_t heading = 0U;
	const char *heading_str = DESKTOP_TEXT_WIDGET_COMPASS_PLACEHOLDER_HEADING;
	const char *accuracy_str = DESKTOP_TEXT_WIDGET_COMPASS_ACCURACY_UNAVAILABLE;
	const char *dir_str = DESKTOP_TEXT_WIDGET_COMPASS_PLACEHOLDER_DIRECTION;
	const char *status_str = DESKTOP_TEXT_WIDGET_COMPASS_STATUS_OFF;
	int32_t start_deg;
	int32_t end_deg;

	if (model != NULL) {
		heading = model->heading_deg;
		heading_str = model->heading_str;
		accuracy_str = compass_widget_accuracy_text(model->accuracy, model->valid);
		dir_str = model->dir_str;
		status_str = compass_widget_status_text(model);
	}

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	if (compass_widget_needs_figure_eight(model)) {
		compass_widget_draw_figure_eight_prompt(draw);
		return;
	}

	start_deg = compass_widget_floor_step((int32_t)heading - span_deg,
							COMPASS_WIDGET_TICK_STEP_DEG);
	end_deg = (int32_t)heading + span_deg;
	for (int32_t deg = start_deg; deg <= end_deg; deg += COMPASS_WIDGET_TICK_STEP_DEG) {
		int32_t delta = compass_widget_wrap_180(deg - (int32_t)heading);
		int16_t x = cx + (int16_t)compass_widget_deg_to_px(delta);
		uint16_t ndeg = compass_widget_norm_360(deg);
		int16_t tick_h;

		if (x < COMPASS_WIDGET_MARGIN_X || x >= (w - COMPASS_WIDGET_MARGIN_X)) {
			continue;
		}
		if ((ndeg % COMPASS_WIDGET_LABEL_STEP_DEG) == 0U) {
			tick_h = 7;
		} else if ((ndeg % 15U) == 0U) {
			tick_h = 5;
		} else {
			tick_h = 3;
		}
		zui_draw_line(draw, (struct zui_point){.x = x, .y = scale_y},
			      (struct zui_point){.x = x, .y = scale_y + tick_h});
	}

	for (uint16_t base = 0U; base < 360U; base += COMPASS_WIDGET_LABEL_STEP_DEG) {
		const char *label = compass_widget_label_for_deg(base);
		int32_t delta = compass_widget_wrap_180((int32_t)base -
								  (int32_t)heading);
		int16_t x = cx + (int16_t)compass_widget_deg_to_px(delta);

		if (label == NULL ||
		    x < COMPASS_WIDGET_MARGIN_X ||
		    x >= (w - COMPASS_WIDGET_MARGIN_X)) {
			continue;
		}
		zui_draw_text_aligned(draw, (struct zui_point){.x = x, .y = labels_y},
				      ZUI_ALIGN_CENTER, ZUI_ALIGN_BOTTOM, label);
	}

	desktop_widget_frame(draw, 1, 13, 125, 25);
	zui_draw_icon(draw, (struct zui_point){.x = 61, .y = 40},
		      desktop_widget_common_icon(ZUI_ASSET_ICON_BUTTON_UP));
	zui_draw_set_font(draw, ZUI_FONT_BIG_NUMBERS);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 64, .y = 60},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_BOTTOM, heading_str);

	desktop_widget_frame(draw, 1, 40, 39, 21);
	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 20, .y = 46},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER, accuracy_str);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 20, .y = 55},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER,
			      DESKTOP_TEXT_WIDGET_COMPASS_ACCURACY_LABEL);

	desktop_widget_frame(draw, 87, 40, 39, 21);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 106, .y = 46},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER, status_str);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 106, .y = 55},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER, dir_str);
}

static void compass_widget_enter(void *user_data)
{
	struct compass_widget_state *state = user_data;

	if (state == NULL) {
		return;
	}
	if (state->runtime_release_pending) {
		if (meshbus_gnss_heading_release() != 0) {
			state->runtime_retry_deadline_ms =
				k_uptime_get_32() + COMPASS_WIDGET_ACQUIRE_RETRY_MS;
			state->runtime_retry_armed = true;
			return;
		}
		state->runtime_lease_held = false;
		state->runtime_release_pending = false;
	}
	if (state->runtime_lease_held) {
		return;
	}

	if (meshbus_gnss_heading_acquire() == 0) {
		state->runtime_lease_held = true;
		state->runtime_retry_armed = false;
	} else {
		/* Retry only from the active widget's tick path, never from draw. */
		state->runtime_retry_deadline_ms =
			k_uptime_get_32() + COMPASS_WIDGET_ACQUIRE_RETRY_MS;
		state->runtime_retry_armed = true;
	}
}

static void compass_widget_exit(void *user_data)
{
	struct compass_widget_state *state = user_data;

	if (state != NULL && state->runtime_lease_held) {
		if (meshbus_gnss_heading_release() == 0) {
			state->runtime_lease_held = false;
			state->runtime_release_pending = false;
		} else {
			/* Preserve ownership so the next page entry can retry teardown. */
			state->runtime_release_pending = true;
		}
	}
	if (state != NULL) {
		state->runtime_retry_armed = false;
	}
}

static const struct zui_screen_ops compass_widget_ops = {
	.draw = compass_widget_draw,
	.enter = compass_widget_enter,
	.exit = compass_widget_exit,
};

static struct zui_screen *compass_widget_screen_create(
	struct meshbus_desktop_dashboard_widget *wctx)
{
	ARG_UNUSED(wctx);

	if (compass_widget.screen == NULL) {
		compass_widget_model_defaults(&compass_widget.model);
		compass_widget.screen =
			zui_screen_create(&compass_widget_ops,
					  &compass_widget);
	}

	return compass_widget.screen;
}

static uint32_t compass_widget_tick(struct meshbus_desktop_dashboard_widget *wctx)
{
	struct meshbus_gnss_heading_runtime_status status = {0};
	uint32_t next_ms;
	uint32_t now_ms;

	ARG_UNUSED(wctx);

	now_ms = k_uptime_get_32();
	if (compass_widget.runtime_retry_armed &&
	    (int32_t)(now_ms - compass_widget.runtime_retry_deadline_ms) >= 0) {
		compass_widget_enter(&compass_widget);
	}

	compass_widget_snapshot_read(&compass_widget.snapshot, &status);
	next_ms = compass_widget_tick_period_ms(&compass_widget.snapshot, &status);
	if (compass_widget.runtime_retry_armed) {
		uint32_t retry_ms = compass_widget.runtime_retry_deadline_ms - now_ms;

		next_ms = MIN(next_ms, retry_ms);
	}
	if (compass_widget_apply_snapshot(&compass_widget.model,
					  &compass_widget.snapshot) &&
	    compass_widget.screen != NULL) {
		(void)zui_screen_request_redraw(compass_widget.screen);
	}

	return next_ms;
}

MESHBUS_DESKTOP_DASHBOARD_WIDGETS_DEFINE(MESHBUS_DESKTOP_DASHBOARD_WIDGET_ID_COMPASS,
					 MESHBUS_DESKTOP_DASHBOARD_WIDGET_TITLE_COMPASS,
					 compass_widget_screen_create,
					 compass_widget_tick,
					 NULL);
#endif
