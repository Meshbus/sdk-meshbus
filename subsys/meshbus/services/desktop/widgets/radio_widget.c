/* SPDX-License-Identifier: Apache-2.0 */

#include "widget_common.h"

#if defined(CONFIG_MESHBUS_RADIO)
#define RADIO_WIDGET_TICK_ACTIVE_MS 500U
#define RADIO_WIDGET_TICK_IDLE_MS	1000U
#define RADIO_WIDGET_TICK_OFF_MS	3000U

struct radio_widget_model {
	bool radio_available;
	bool enabled;
	bool rx_only;
	uint8_t state;
	char freq_str[24];
	char bw_str[12];
	char lr_str[4];
	char txp_str[8];
	char sf_str[6];
	char cr_str[8];
	char status_str[6];
};

struct radio_widget_snapshot {
	bool radio_available;
	bool enabled;
	bool rx_only;
	enum meshbus_radio_state state;
	char freq_str[24];
	char bw_str[12];
	char lr_str[4];
	char txp_str[8];
	char sf_str[6];
	char cr_str[8];
	char status_str[6];
};

struct radio_widget_state {
	struct zui_screen *screen;
	struct radio_widget_model model;
	struct radio_widget_snapshot snapshot;
};

static struct radio_widget_state radio_widget;

static void radio_widget_format_freq(char *out, size_t out_size, uint64_t hz)
{
	uint64_t mhz_int;
	uint32_t mhz_frac;

	if (out == NULL || out_size == 0U) {
		return;
	}
	if (hz == 0ULL) {
		desktop_widget_strcpy(out, out_size,
				      DESKTOP_TEXT_WIDGET_RADIO_PLACEHOLDER_FREQUENCY);
		return;
	}

	mhz_int = hz / 1000000ULL;
	mhz_frac = (uint32_t)((hz % 1000000ULL) / 1000ULL);
	(void)snprintk(out, out_size, "%llu.%03u", (unsigned long long)mhz_int,
		       (unsigned int)mhz_frac);
}

static void radio_widget_format_bw(char *out, size_t out_size, uint32_t bw_hz)
{
	struct bw_map {
		uint32_t hz;
		uint8_t text_idx;
	};
	static const struct bw_map map[] = {
		{7800U, 0U},	 {10400U, 1U},  {15600U, 2U},	 {20800U, 3U},
		{31250U, 4U},	 {41700U, 5U},  {62500U, 6U},	 {125000U, 7U},
		{200000U, 8U},	 {250000U, 9U}, {400000U, 10U},	 {500000U, 11U},
		{800000U, 12U}, {1000000U, 13U}, {1600000U, 14U},
	};

	if (out == NULL || out_size == 0U) {
		return;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(map); i++) {
		if (map[i].hz == bw_hz) {
			desktop_widget_strcpy(out, out_size,
					      DESKTOP_TEXT_WIDGET_RADIO_BW_SHORT_VALUES
						      [map[i].text_idx]);
			return;
		}
	}

	if (bw_hz >= 1000U && (bw_hz % 1000U) == 0U) {
		(void)snprintk(out, out_size, DESKTOP_TEXT_WIDGET_RADIO_BW_KHZ_FORMAT,
			       (unsigned int)(bw_hz / 1000U));
		return;
	}

	(void)snprintk(out, out_size, "%u", (unsigned int)bw_hz);
}

static uint32_t radio_widget_tick_period_ms(
	const struct radio_widget_snapshot *snapshot)
{
	if (snapshot == NULL || !snapshot->radio_available || !snapshot->enabled) {
		return RADIO_WIDGET_TICK_OFF_MS;
	}
	if (snapshot->state == MESHBUS_RADIO_STATE_RECEIVE ||
	    snapshot->state == MESHBUS_RADIO_STATE_TRANSMIT) {
		return RADIO_WIDGET_TICK_ACTIVE_MS;
	}

	return RADIO_WIDGET_TICK_IDLE_MS;
}

static void radio_widget_snapshot_defaults(
	struct radio_widget_snapshot *snapshot)
{
	if (snapshot == NULL) {
		return;
	}

	snapshot->radio_available = false;
	snapshot->enabled = false;
	snapshot->rx_only = false;
	snapshot->state = MESHBUS_RADIO_STATE_IDLE;
	desktop_widget_strcpy(snapshot->freq_str, sizeof(snapshot->freq_str),
			      DESKTOP_TEXT_WIDGET_RADIO_PLACEHOLDER_FREQUENCY);
	desktop_widget_strcpy(snapshot->bw_str, sizeof(snapshot->bw_str),
			      DESKTOP_TEXT_WIDGET_RADIO_PLACEHOLDER_VALUE);
	desktop_widget_strcpy(snapshot->lr_str, sizeof(snapshot->lr_str),
			      DESKTOP_TEXT_WIDGET_RADIO_LR);
	desktop_widget_strcpy(snapshot->txp_str, sizeof(snapshot->txp_str),
			      DESKTOP_TEXT_WIDGET_RADIO_PLACEHOLDER_VALUE);
	desktop_widget_strcpy(snapshot->sf_str, sizeof(snapshot->sf_str),
			      DESKTOP_TEXT_WIDGET_RADIO_PLACEHOLDER_VALUE);
	desktop_widget_strcpy(snapshot->cr_str, sizeof(snapshot->cr_str),
			      DESKTOP_TEXT_WIDGET_RADIO_PLACEHOLDER_VALUE);
	desktop_widget_strcpy(snapshot->status_str, sizeof(snapshot->status_str),
			      DESKTOP_TEXT_WIDGET_RADIO_STATUS_OFF);
}

static void radio_widget_snapshot_read(struct radio_widget_snapshot *snapshot)
{
	meshbus_radio_config cfg;

	if (snapshot == NULL) {
		return;
	}

	radio_widget_snapshot_defaults(snapshot);
	if (meshbus_radio_config_get(&cfg) != 0) {
		return;
	}

	snapshot->radio_available = true;
	snapshot->enabled = cfg.enabled;
	snapshot->rx_only = cfg.receive_only;
	snapshot->state = meshbus_radio_state_get();
	radio_widget_format_freq(snapshot->freq_str, sizeof(snapshot->freq_str),
					   cfg.frequency);
	radio_widget_format_bw(snapshot->bw_str, sizeof(snapshot->bw_str),
					 cfg.bandwidth);
	desktop_widget_strcpy(snapshot->lr_str, sizeof(snapshot->lr_str),
			      DESKTOP_TEXT_WIDGET_RADIO_LR);
	(void)snprintk(snapshot->txp_str, sizeof(snapshot->txp_str),
		       DESKTOP_TEXT_WIDGET_RADIO_TX_POWER_FORMAT,
		       (int)cfg.tx_power);
	(void)snprintk(snapshot->sf_str, sizeof(snapshot->sf_str),
		       DESKTOP_TEXT_WIDGET_RADIO_SF_FORMAT,
		       (unsigned int)cfg.spread_factor);
	(void)snprintk(snapshot->cr_str, sizeof(snapshot->cr_str),
		       DESKTOP_TEXT_WIDGET_RADIO_CR_FORMAT,
		       (unsigned int)cfg.coding_rate);

	if (!snapshot->enabled) {
		desktop_widget_strcpy(snapshot->status_str, sizeof(snapshot->status_str),
				      DESKTOP_TEXT_WIDGET_RADIO_STATUS_OFF);
	} else if (snapshot->state == MESHBUS_RADIO_STATE_TRANSMIT) {
		desktop_widget_strcpy(snapshot->status_str, sizeof(snapshot->status_str),
				      DESKTOP_TEXT_WIDGET_RADIO_STATUS_TX);
	} else if (snapshot->state == MESHBUS_RADIO_STATE_RECEIVE) {
		desktop_widget_strcpy(snapshot->status_str, sizeof(snapshot->status_str),
				      DESKTOP_TEXT_WIDGET_RADIO_STATUS_RX);
	} else {
		desktop_widget_strcpy(snapshot->status_str, sizeof(snapshot->status_str),
				      DESKTOP_TEXT_WIDGET_RADIO_STATUS_IDLE);
	}
}

static bool radio_widget_apply_snapshot(
	struct radio_widget_model *model,
	const struct radio_widget_snapshot *snapshot)
{
	bool changed = false;

	if (model == NULL || snapshot == NULL) {
		return false;
	}

	changed |= model->radio_available != snapshot->radio_available;
	changed |= model->enabled != snapshot->enabled;
	changed |= model->rx_only != snapshot->rx_only;
	changed |= model->state != (uint8_t)snapshot->state;
	changed |= strcmp(model->freq_str, snapshot->freq_str) != 0;
	changed |= strcmp(model->bw_str, snapshot->bw_str) != 0;
	changed |= strcmp(model->lr_str, snapshot->lr_str) != 0;
	changed |= strcmp(model->txp_str, snapshot->txp_str) != 0;
	changed |= strcmp(model->sf_str, snapshot->sf_str) != 0;
	changed |= strcmp(model->cr_str, snapshot->cr_str) != 0;
	changed |= strcmp(model->status_str, snapshot->status_str) != 0;
	if (!changed) {
		return false;
	}

	model->radio_available = snapshot->radio_available;
	model->enabled = snapshot->enabled;
	model->rx_only = snapshot->rx_only;
	model->state = (uint8_t)snapshot->state;
	desktop_widget_strcpy(model->freq_str, sizeof(model->freq_str), snapshot->freq_str);
	desktop_widget_strcpy(model->bw_str, sizeof(model->bw_str), snapshot->bw_str);
	desktop_widget_strcpy(model->lr_str, sizeof(model->lr_str), snapshot->lr_str);
	desktop_widget_strcpy(model->txp_str, sizeof(model->txp_str), snapshot->txp_str);
	desktop_widget_strcpy(model->sf_str, sizeof(model->sf_str), snapshot->sf_str);
	desktop_widget_strcpy(model->cr_str, sizeof(model->cr_str), snapshot->cr_str);
	desktop_widget_strcpy(model->status_str, sizeof(model->status_str),
			      snapshot->status_str);
	return true;
}

static void radio_widget_model_defaults(struct radio_widget_model *model)
{
	struct radio_widget_snapshot snapshot;

	if (model == NULL) {
		return;
	}

	memset(model, 0, sizeof(*model));
	radio_widget_snapshot_defaults(&snapshot);
	(void)radio_widget_apply_snapshot(model, &snapshot);
}

static void radio_widget_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct radio_widget_state *state = user_data;
	const struct radio_widget_model *model =
		state != NULL ? &state->model : NULL;
	const uint8_t *icon = B_offline_32x32;

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	desktop_widget_frame(draw, 1, 13, 36, 47);
	desktop_widget_frame(draw, 40, 13, 86, 47);

	zui_draw_bitmap(draw, (struct zui_point){.x = 99, .y = 46}, 25, 11,
			ZUI_BITMAP_FORMAT_XBM, B_mhz_25x11);
	zui_draw_set_font(draw, ZUI_FONT_BIG_NUMBERS);
	zui_draw_text(draw, (struct zui_point){.x = 42, .y = 43},
		      model != NULL ? model->freq_str :
				      DESKTOP_TEXT_WIDGET_RADIO_PLACEHOLDER_FREQUENCY);
	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	zui_draw_round_rect(draw, &(struct zui_rect){.x = 42, .y = 15, .width = 18,
						     .height = 12}, 2);
	zui_draw_round_rect(draw, &(struct zui_rect){.x = 62, .y = 15, .width = 31,
						     .height = 12}, 2);
	zui_draw_round_rect(draw, &(struct zui_rect){.x = 96, .y = 15, .width = 28,
						     .height = 12}, 2);
	zui_draw_round_rect(draw, &(struct zui_rect){.x = 42, .y = 46, .width = 29,
						     .height = 12}, 2);
	zui_draw_round_rect(draw, &(struct zui_rect){.x = 73, .y = 46, .width = 24,
						     .height = 12}, 2);
	zui_draw_round_box(draw, &(struct zui_rect){.x = 1, .y = 48, .width = 36,
						    .height = 12}, 2);

	zui_draw_text_aligned(draw, (struct zui_point){.x = 51, .y = 21},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER,
			      model != NULL ? model->lr_str : DESKTOP_TEXT_WIDGET_RADIO_LR);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 77, .y = 21},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER,
			      model != NULL ? model->bw_str :
					      DESKTOP_TEXT_WIDGET_RADIO_PLACEHOLDER_VALUE);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 110, .y = 21},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER,
			      model != NULL ? model->txp_str :
					      DESKTOP_TEXT_WIDGET_RADIO_PLACEHOLDER_VALUE);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 56, .y = 52},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER,
			      model != NULL ? model->sf_str :
					      DESKTOP_TEXT_WIDGET_RADIO_PLACEHOLDER_VALUE);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 85, .y = 52},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER,
			      model != NULL ? model->cr_str :
					      DESKTOP_TEXT_WIDGET_RADIO_PLACEHOLDER_VALUE);

	if (model != NULL) {
		if (!model->radio_available || !model->enabled) {
			icon = B_offline_32x32;
		} else if (model->state == (uint8_t)MESHBUS_RADIO_STATE_TRANSMIT) {
			icon = B_boardcast_32x32;
		} else {
			icon = B_listening_32x32;
		}
	}

	zui_draw_set_color(draw, ZUI_COLOR_XOR);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 19, .y = 54},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER,
			      model != NULL ? model->status_str :
					      DESKTOP_TEXT_WIDGET_RADIO_STATUS_OFF);
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_bitmap(draw, (struct zui_point){.x = 3, .y = 15}, 32, 32,
			ZUI_BITMAP_FORMAT_XBM, icon);
}

static const struct zui_screen_ops radio_widget_ops = {
	.draw = radio_widget_draw,
};

static struct zui_screen *radio_widget_screen_create(
	struct meshbus_desktop_dashboard_widget *wctx)
{
	ARG_UNUSED(wctx);

	if (radio_widget.screen == NULL) {
		radio_widget_model_defaults(&radio_widget.model);
		radio_widget.screen =
			zui_screen_create(&radio_widget_ops, &radio_widget);
	}

	return radio_widget.screen;
}

static uint32_t radio_widget_tick(struct meshbus_desktop_dashboard_widget *wctx)
{
	uint32_t next_ms;

	ARG_UNUSED(wctx);

	radio_widget_snapshot_read(&radio_widget.snapshot);
	next_ms = radio_widget_tick_period_ms(&radio_widget.snapshot);
	if (radio_widget_apply_snapshot(&radio_widget.model,
						  &radio_widget.snapshot) &&
	    radio_widget.screen != NULL) {
		(void)zui_screen_request_redraw(radio_widget.screen);
	}

	return next_ms;
}
#endif

#if defined(CONFIG_MESHBUS_RADIO)
MESHBUS_DESKTOP_DASHBOARD_WIDGETS_DEFINE(MESHBUS_DESKTOP_DASHBOARD_WIDGET_ID_RADIO,
					 MESHBUS_DESKTOP_DASHBOARD_WIDGET_TITLE_RADIO,
					 radio_widget_screen_create,
					 radio_widget_tick,
					 MESHBUS_DESKTOP_APP_ID_RADIO);
#endif
