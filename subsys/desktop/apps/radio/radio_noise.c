/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "radio_private.h"

#include <string.h>

#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "text/desktop_text.h"

void radio_noise_reset(struct radio_noise_analyzer_model *model)
{
	bool running;

	if (model == NULL) {
		return;
	}

	running = model->running;
	memset(model, 0, sizeof(*model));
	model->running = running;
	model->last_gen_ms = k_uptime_get_32();
}

static void radio_noise_recompute_minmax(struct radio_noise_analyzer_model *model)
{
	uint8_t start;

	if (model == NULL || model->count == 0U) {
		return;
	}

	start = (uint8_t)((model->head + RADIO_NOISE_ANALYZER_HISTORY - model->count) %
			  RADIO_NOISE_ANALYZER_HISTORY);
	model->min_dbm = model->entries[start];
	model->max_dbm = model->entries[start];
	for (uint8_t i = 1U; i < model->count; i++) {
		uint8_t idx = (uint8_t)((start + i) % RADIO_NOISE_ANALYZER_HISTORY);

		model->min_dbm = MIN(model->min_dbm, model->entries[idx]);
		model->max_dbm = MAX(model->max_dbm, model->entries[idx]);
	}
}

void radio_noise_collect(struct radio_app *app)
{
	struct radio_noise_analyzer_model *model;
	enum mbs_radio_state state;
	bool channel_active;
	int16_t noise_floor;
	int16_t dbm = 0;
	int rc;
	uint32_t now_ms;

	if (app == NULL) {
		return;
	}

	model = &app->noise;
	now_ms = k_uptime_get_32();
	if (!model->running || (now_ms - model->last_gen_ms) < RADIO_NOISE_INTERVAL_MS) {
		return;
	}

	state = mbs_radio_state_get();
	channel_active = mbs_radio_channel_activity();
	noise_floor = mbs_radio_noise_floor();
	rc = mbs_radio_rssi_inst(&dbm);

	model->rx_ready = state == MBS_RADIO_STATE_RECEIVE;
	model->channel_active = channel_active;
	model->noise_floor_valid = noise_floor != 0;
	model->noise_floor_dbm = noise_floor;
	model->last_rc = rc;
	if (state == MBS_RADIO_STATE_RECEIVE && !model->noise_floor_valid &&
	    !model->calibration_requested) {
		mbs_radio_noise_calibrate(RADIO_NOISE_CAL_THRESHOLD_DB);
		model->calibration_requested = true;
	}
	if (model->noise_floor_valid) {
		model->calibration_requested = false;
	}
	if (rc != 0) {
		model->last_gen_ms = now_ms;
		return;
	}

	model->entries[model->head] = dbm;
	model->head = (uint8_t)((model->head + 1U) % RADIO_NOISE_ANALYZER_HISTORY);
	if (model->count < RADIO_NOISE_ANALYZER_HISTORY) {
		model->count++;
	}
	model->current_dbm = dbm;
	radio_noise_recompute_minmax(model);
	model->seq++;
	model->last_gen_ms = now_ms;
}
static uint8_t radio_noise_dbm_to_height(int16_t dbm)
{
	const int16_t min_dbm = RADIO_NOISE_ANALYZER_DBM_MIN;
	const int16_t max_dbm = RADIO_NOISE_ANALYZER_DBM_MAX;
	int32_t value = dbm;
	int32_t height;

	value = CLAMP(value, min_dbm, max_dbm);
	height = (value - min_dbm) * 28;
	height /= (max_dbm - min_dbm);

	return (uint8_t)CLAMP(height, 0, 28);
}

static void radio_noise_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct radio_app *app = user_data;
	struct radio_noise_analyzer_model *m = &app->noise;
	char line[32];
	int y_nf;

	zui_draw_clear(draw);
	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	zui_draw_text(draw, (struct zui_point){50, 62}, DESKTOP_TEXT_RADIO_NOISE);
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	zui_draw_text(draw, (struct zui_point){108, 18}, DESKTOP_TEXT_RADIO_DBM);
	zui_draw_line(draw, (struct zui_point){1, 1}, (struct zui_point){126, 1});
	zui_draw_line(draw, (struct zui_point){1, 47}, (struct zui_point){126, 47});
	zui_draw_line(draw, (struct zui_point){1, 49}, (struct zui_point){1, 48});
	zui_draw_line(draw, (struct zui_point){126, 49}, (struct zui_point){126, 48});
	for (uint8_t tick = 0U; tick <= 12U; tick++) {
		int16_t x = (int16_t)(1 + ((int)tick * 10));

		zui_draw_line(draw, (struct zui_point){x, 49}, (struct zui_point){x, 48});
	}
	if (m->count > 0U) {
		(void)snprintk(line, sizeof(line), DESKTOP_TEXT_RADIO_FORMAT_MIN_MAX,
			       (int)m->min_dbm, (int)m->max_dbm);
		zui_draw_text(draw, (struct zui_point){1, 10}, line);
		(void)snprintk(line, sizeof(line), "%d", (int)m->current_dbm);
		zui_draw_text_aligned(draw, (struct zui_point){127, 3}, ZUI_ALIGN_RIGHT,
				      ZUI_ALIGN_TOP, line);
	} else {
		zui_draw_text(draw, (struct zui_point){1, 10}, DESKTOP_TEXT_RADIO_MIN_MAX_DASH);
		zui_draw_text_aligned(draw, (struct zui_point){127, 3}, ZUI_ALIGN_RIGHT,
				      ZUI_ALIGN_TOP,
				      m->rx_ready ? DESKTOP_TEXT_RADIO_RX_UNKNOWN :
						    DESKTOP_TEXT_RADIO_NA);
	}
	(void)snprintk(line, sizeof(line),
		       m->noise_floor_valid ? DESKTOP_TEXT_RADIO_FORMAT_NF_STATUS :
					      DESKTOP_TEXT_RADIO_FORMAT_NF_DASH_STATUS,
		       (int)m->noise_floor_dbm,
		       m->channel_active ? DESKTOP_TEXT_RADIO_ACT : DESKTOP_TEXT_RADIO_QUIET);
	zui_draw_text(draw, (struct zui_point){1, 18}, line);

	if (m->count > 0U) {
		uint8_t visible = MIN(m->count, RADIO_NOISE_ANALYZER_HISTORY);
		uint8_t start =
			(uint8_t)((m->head + RADIO_NOISE_ANALYZER_HISTORY - visible) %
				  RADIO_NOISE_ANALYZER_HISTORY);

		for (uint8_t i = 0U; i < visible; i++) {
			uint8_t idx = (uint8_t)((start + i) % RADIO_NOISE_ANALYZER_HISTORY);
			uint8_t height = radio_noise_dbm_to_height(m->entries[idx]);
			int16_t x = (int16_t)(1 + i);
			int16_t y = (int16_t)(48 - height);

			if (height > 0U) {
				zui_draw_box(draw, &(struct zui_rect){.x = x,
								      .y = y,
								      .width = 1U,
								      .height = height});
			}
		}

		if (m->noise_floor_valid) {
			uint8_t nf_height = radio_noise_dbm_to_height(m->noise_floor_dbm);

			y_nf = 48 - (int)nf_height;
			y_nf = CLAMP(y_nf, 1, 47);
			zui_draw_line(draw, (struct zui_point){1, (int16_t)y_nf},
				      (struct zui_point){126, (int16_t)y_nf});
		}
	}
	zui_draw_button_hints(draw, &(struct zui_draw_button_hint){
		.left = DESKTOP_TEXT_COMMON_BUTTON_CLEAR,
		.right = m->running ? DESKTOP_TEXT_COMMON_BUTTON_STOP :
				      DESKTOP_TEXT_COMMON_BUTTON_START,
	});
}

static bool radio_noise_input(const struct zui_input_event *event, void *user_data)
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
	if (!desktop_app_input_is_click(event)) {
		return false;
	}
	if (event->code == ZUI_INPUT_CODE_RIGHT) {
		app->noise.running = !app->noise.running;
		app->noise.calibration_requested = false;
		radio_schedule_tick(app);
		radio_request_redraw(app);
		return true;
	}
	if (event->code == ZUI_INPUT_CODE_LEFT) {
		radio_noise_reset(&app->noise);
		radio_request_redraw(app);
		return true;
	}

	return false;
}
const struct zui_screen_ops radio_noise_ops = {
	.draw = radio_noise_draw,
	.input = radio_noise_input,
};
