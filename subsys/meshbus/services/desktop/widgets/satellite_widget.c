/* SPDX-License-Identifier: Apache-2.0 */

#include "widget_common.h"

#if defined(CONFIG_MESHBUS_GNSS)
#define SATELLITE_WIDGET_TICK_FIXING_MS 500U
#define SATELLITE_WIDGET_TICK_FIX_MS    1000U
#define SATELLITE_WIDGET_TICK_OFF_MS    3000U
#define SATELLITE_WIDGET_MAX_SNR_BARS   32U
#define SATELLITE_WIDGET_SNR_MAX	     50U
#define SATELLITE_WIDGET_BARS_BASE_Y    42
#define SATELLITE_WIDGET_BARS_TOP_Y     26
#define SATELLITE_WIDGET_BARS_MAX_H \
	(SATELLITE_WIDGET_BARS_BASE_Y - SATELLITE_WIDGET_BARS_TOP_Y)

struct satellite_widget_model {
	bool gnss_available;
	bool enabled;
	bool has_fix;
	char fix_str[8];
	char hdop_str[10];
	char sats_str[12];
	char tz_str[4];
	char utc_str[20];
	uint8_t sats_snr[SATELLITE_WIDGET_MAX_SNR_BARS];
};

struct satellite_widget_snapshot {
	bool gnss_available;
	bool enabled;
	bool has_fix;
	enum meshbus_gnss_state state;
	enum gnss_fix_status fix_status;
	char fix_str[8];
	char hdop_str[10];
	char sats_str[12];
	char tz_str[4];
	char utc_str[20];
	uint8_t sats_snr[SATELLITE_WIDGET_MAX_SNR_BARS];
};

struct satellite_widget_state {
	struct zui_screen *screen;
	struct satellite_widget_model model;
	struct satellite_widget_snapshot snapshot;
};

static struct satellite_widget_state satellite_widget;

static const char *satellite_widget_state_text(bool gnss_available, bool enabled,
							 enum meshbus_gnss_state state)
{
	if (!gnss_available || !enabled) {
		return DESKTOP_TEXT_WIDGET_SATELLITE_STATUS_OFF;
	}

	switch (state) {
	case MESHBUS_GNSS_STATE_SLEEP:
		return DESKTOP_TEXT_WIDGET_SATELLITE_STATUS_SLEEP;
	case MESHBUS_GNSS_STATE_ACQUIRING:
		return DESKTOP_TEXT_WIDGET_SATELLITE_STATUS_ACQ;
	case MESHBUS_GNSS_STATE_TRACK:
		return DESKTOP_TEXT_WIDGET_SATELLITE_STATUS_TRACK;
	case MESHBUS_GNSS_STATE_ERROR:
		return DESKTOP_TEXT_WIDGET_SATELLITE_STATUS_ERROR;
	default:
		return DESKTOP_TEXT_WIDGET_SATELLITE_STATUS_ACQ;
	}
}

static void satellite_widget_format_hdop(char *out, size_t out_size, uint32_t hdop)
{
	if (out == NULL || out_size == 0U) {
		return;
	}
	if (hdop == 0U) {
		desktop_widget_strcpy(out, out_size, DESKTOP_TEXT_WIDGET_SATELLITE_PLACEHOLDER_HDOP);
		return;
	}

	(void)snprintk(out, out_size, "%u.%u", (unsigned int)(hdop / 1000U),
		       (unsigned int)((hdop % 1000U) / 100U));
}

static void satellite_widget_format_utc(char *out, size_t out_size,
						  const struct gnss_time *utc)
{
	uint32_t year;
	uint32_t month;
	uint32_t day;
	uint32_t hour;
	uint32_t minute;
	uint32_t second;

	if (out == NULL || out_size == 0U) {
		return;
	}
	if (utc == NULL || utc->month == 0U || utc->month_day == 0U) {
		desktop_widget_strcpy(out, out_size,
				      DESKTOP_TEXT_WIDGET_SATELLITE_PLACEHOLDER_UTC);
		return;
	}

	year = 2000U + ((uint32_t)utc->century_year % 100U);
	month = ((uint32_t)utc->month) % 100U;
	day = ((uint32_t)utc->month_day) % 100U;
	hour = ((uint32_t)utc->hour) % 100U;
	minute = ((uint32_t)utc->minute) % 100U;
	second = ((uint32_t)utc->millisecond / 1000U) % 100U;
	(void)snprintk(out, out_size, "%04u-%02u-%02u %02u:%02u:%02u", (unsigned int)year,
		       (unsigned int)month, (unsigned int)day, (unsigned int)hour,
		       (unsigned int)minute, (unsigned int)second);
}

static void satellite_widget_snapshot_defaults(
	struct satellite_widget_snapshot *snapshot)
{
	if (snapshot == NULL) {
		return;
	}

	snapshot->gnss_available = false;
	snapshot->enabled = false;
	snapshot->has_fix = false;
	snapshot->state = MESHBUS_GNSS_STATE_SLEEP;
	snapshot->fix_status = GNSS_FIX_STATUS_NO_FIX;
	desktop_widget_strcpy(snapshot->fix_str, sizeof(snapshot->fix_str),
			      DESKTOP_TEXT_WIDGET_SATELLITE_STATUS_OFF);
	desktop_widget_strcpy(snapshot->hdop_str, sizeof(snapshot->hdop_str),
			      DESKTOP_TEXT_WIDGET_SATELLITE_PLACEHOLDER_HDOP);
	desktop_widget_strcpy(snapshot->sats_str, sizeof(snapshot->sats_str),
			      DESKTOP_TEXT_WIDGET_SATELLITE_PLACEHOLDER_SATS);
	desktop_widget_strcpy(snapshot->tz_str, sizeof(snapshot->tz_str),
			      DESKTOP_TEXT_WIDGET_SATELLITE_TZ_LUT);
	desktop_widget_strcpy(snapshot->utc_str, sizeof(snapshot->utc_str),
			      DESKTOP_TEXT_WIDGET_SATELLITE_PLACEHOLDER_UTC);
	memset(snapshot->sats_snr, 0, sizeof(snapshot->sats_snr));
}

#if defined(CONFIG_GNSS_SATELLITES) && defined(CONFIG_MESHBUS_GNSS_SATELLITE_CACHE_SIZE)
static uint16_t satellite_widget_snapshot_read_snr(
	struct satellite_widget_snapshot *snapshot)
{
	struct gnss_satellite sats[CONFIG_MESHBUS_GNSS_SATELLITE_CACHE_SIZE];
	uint16_t sat_count = 0U;

	if (snapshot == NULL) {
		return 0U;
	}
	if (meshbus_gnss_satellites_get(sats, ARRAY_SIZE(sats), &sat_count) != 0 ||
	    sat_count == 0U) {
		return 0U;
	}

	for (uint16_t i = 0U; i < sat_count && i < SATELLITE_WIDGET_MAX_SNR_BARS; i++) {
		snapshot->sats_snr[i] = sats[i].snr;
	}

	return sat_count;
}
#else
static uint16_t satellite_widget_snapshot_read_snr(
	struct satellite_widget_snapshot *snapshot)
{
	ARG_UNUSED(snapshot);
	return 0U;
}
#endif

static void satellite_widget_snapshot_read(
	struct satellite_widget_snapshot *snapshot)
{
	meshbus_gnss_config cfg;
	struct gnss_info info;
	struct gnss_time utc;
	uint16_t visible_cnt = 0U;
	uint16_t tracked_cnt = 0U;

	if (snapshot == NULL) {
		return;
	}

	satellite_widget_snapshot_defaults(snapshot);
	if (meshbus_gnss_config_get(&cfg) != 0) {
		return;
	}

	snapshot->gnss_available = true;
	snapshot->enabled = cfg.enabled;
	snapshot->state = meshbus_gnss_state_get();
	desktop_widget_strcpy(snapshot->fix_str, sizeof(snapshot->fix_str),
			      satellite_widget_state_text(snapshot->gnss_available,
								    snapshot->enabled,
								    snapshot->state));
	if (!snapshot->enabled) {
		return;
	}

	visible_cnt = satellite_widget_snapshot_read_snr(snapshot);
	if (meshbus_gnss_info_get(&info) != 0) {
		snapshot->has_fix = false;
		(void)snprintk(snapshot->sats_str, sizeof(snapshot->sats_str), "%u/%u",
			       (unsigned int)tracked_cnt, (unsigned int)visible_cnt);
		return;
	}

	snapshot->has_fix = true;
	snapshot->fix_status = info.fix_status;
	tracked_cnt = info.satellites_cnt;
	if (visible_cnt < tracked_cnt) {
		visible_cnt = tracked_cnt;
	}
	satellite_widget_format_hdop(snapshot->hdop_str, sizeof(snapshot->hdop_str),
					       info.hdop);
	(void)snprintk(snapshot->sats_str, sizeof(snapshot->sats_str), "%u/%u",
		       (unsigned int)tracked_cnt, (unsigned int)visible_cnt);
	if (meshbus_gnss_time_get(&utc) == 0) {
		satellite_widget_format_utc(snapshot->utc_str, sizeof(snapshot->utc_str),
						      &utc);
	}
}

static bool satellite_widget_apply_snapshot(
	struct satellite_widget_model *model,
	const struct satellite_widget_snapshot *snapshot)
{
	bool changed = false;

	if (model == NULL || snapshot == NULL) {
		return false;
	}

	changed |= model->gnss_available != snapshot->gnss_available;
	changed |= model->enabled != snapshot->enabled;
	changed |= model->has_fix != snapshot->has_fix;
	changed |= strcmp(model->fix_str, snapshot->fix_str) != 0;
	changed |= strcmp(model->hdop_str, snapshot->hdop_str) != 0;
	changed |= strcmp(model->sats_str, snapshot->sats_str) != 0;
	changed |= strcmp(model->tz_str, snapshot->tz_str) != 0;
	changed |= strcmp(model->utc_str, snapshot->utc_str) != 0;
	changed |= memcmp(model->sats_snr, snapshot->sats_snr, sizeof(model->sats_snr)) != 0;
	if (!changed) {
		return false;
	}

	model->gnss_available = snapshot->gnss_available;
	model->enabled = snapshot->enabled;
	model->has_fix = snapshot->has_fix;
	desktop_widget_strcpy(model->fix_str, sizeof(model->fix_str), snapshot->fix_str);
	desktop_widget_strcpy(model->hdop_str, sizeof(model->hdop_str), snapshot->hdop_str);
	desktop_widget_strcpy(model->sats_str, sizeof(model->sats_str), snapshot->sats_str);
	desktop_widget_strcpy(model->tz_str, sizeof(model->tz_str), snapshot->tz_str);
	desktop_widget_strcpy(model->utc_str, sizeof(model->utc_str), snapshot->utc_str);
	memcpy(model->sats_snr, snapshot->sats_snr, sizeof(model->sats_snr));
	return true;
}

static uint32_t satellite_widget_tick_period_ms(
	const struct satellite_widget_snapshot *snapshot)
{
	if (snapshot == NULL || !snapshot->gnss_available || !snapshot->enabled) {
		return SATELLITE_WIDGET_TICK_OFF_MS;
	}
	if (snapshot->state == MESHBUS_GNSS_STATE_ACQUIRING) {
		return SATELLITE_WIDGET_TICK_FIXING_MS;
	}
	if (snapshot->state == MESHBUS_GNSS_STATE_ERROR) {
		return SATELLITE_WIDGET_TICK_OFF_MS;
	}

	return SATELLITE_WIDGET_TICK_FIX_MS;
}

static void satellite_widget_model_defaults(
	struct satellite_widget_model *model)
{
	struct satellite_widget_snapshot snapshot;

	if (model == NULL) {
		return;
	}

	memset(model, 0, sizeof(*model));
	satellite_widget_snapshot_defaults(&snapshot);
	(void)satellite_widget_apply_snapshot(model, &snapshot);
}

static void satellite_widget_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct satellite_widget_state *state = user_data;
	const struct satellite_widget_model *model =
		state != NULL ? &state->model : NULL;
	const char *fix_str = model != NULL ? model->fix_str :
					     DESKTOP_TEXT_WIDGET_SATELLITE_STATUS_OFF;
	const char *hdop_str = model != NULL ? model->hdop_str :
					      DESKTOP_TEXT_WIDGET_SATELLITE_PLACEHOLDER_HDOP;
	const char *sats_str = model != NULL ? model->sats_str :
					      DESKTOP_TEXT_WIDGET_SATELLITE_PLACEHOLDER_SATS;
	const char *tz_str = model != NULL ? model->tz_str :
					    DESKTOP_TEXT_WIDGET_SATELLITE_TZ_LUT;
	const char *utc_str = model != NULL ? model->utc_str :
					     DESKTOP_TEXT_WIDGET_SATELLITE_PLACEHOLDER_UTC;
	const uint8_t *sats_snr = model != NULL ? model->sats_snr : NULL;

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);

	desktop_widget_frame(draw, 2, 14, 53, 14);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 5, .y = 21},
			      ZUI_ALIGN_LEFT, ZUI_ALIGN_CENTER, fix_str);
	zui_draw_icon(draw, (struct zui_point){.x = 45, .y = 17}, &I_radio_7x8);

	desktop_widget_frame(draw, 2, 30, 53, 14);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 5, .y = 37},
			      ZUI_ALIGN_LEFT, ZUI_ALIGN_CENTER,
			      DESKTOP_TEXT_WIDGET_SATELLITE_LABEL_HDOP);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 52, .y = 37},
			      ZUI_ALIGN_RIGHT, ZUI_ALIGN_CENTER, hdop_str);

	desktop_widget_frame(draw, 59, 14, 66, 30);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 62, .y = 21},
			      ZUI_ALIGN_LEFT, ZUI_ALIGN_CENTER,
			      DESKTOP_TEXT_WIDGET_SATELLITE_LABEL_SATS);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 122, .y = 21},
			      ZUI_ALIGN_RIGHT, ZUI_ALIGN_CENTER, sats_str);
	if (sats_snr != NULL) {
		for (uint8_t i = 0U; i < SATELLITE_WIDGET_MAX_SNR_BARS; i++) {
			uint32_t snr = MIN((uint32_t)sats_snr[i], SATELLITE_WIDGET_SNR_MAX);
			int16_t h = (int16_t)DIV_ROUND_CLOSEST(
				snr * (uint32_t)SATELLITE_WIDGET_BARS_MAX_H,
				(uint32_t)SATELLITE_WIDGET_SNR_MAX);
			int16_t top_y;
			int16_t dx;

			if (h <= 0) {
				continue;
			}
			top_y = SATELLITE_WIDGET_BARS_BASE_Y - h;
			if (top_y < SATELLITE_WIDGET_BARS_TOP_Y) {
				top_y = SATELLITE_WIDGET_BARS_TOP_Y;
			}
			dx = 60 + (int16_t)i * 2;
			zui_draw_line(draw, (struct zui_point){.x = dx,
							       .y = SATELLITE_WIDGET_BARS_BASE_Y},
				      (struct zui_point){.x = dx, .y = top_y});
			zui_draw_line(draw, (struct zui_point){.x = dx + 1,
							       .y = SATELLITE_WIDGET_BARS_BASE_Y},
				      (struct zui_point){.x = dx + 1, .y = top_y});
		}
	}

	desktop_widget_frame(draw, 2, 46, 123, 15);
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 5, .y = 54},
			      ZUI_ALIGN_LEFT, ZUI_ALIGN_CENTER, tz_str);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 122, .y = 54},
			      ZUI_ALIGN_RIGHT, ZUI_ALIGN_CENTER, utc_str);
}

static const struct zui_screen_ops satellite_widget_ops = {
	.draw = satellite_widget_draw,
};

static struct zui_screen *satellite_widget_screen_create(
	struct meshbus_desktop_dashboard_widget *wctx)
{
	ARG_UNUSED(wctx);

	if (satellite_widget.screen == NULL) {
		satellite_widget_model_defaults(&satellite_widget.model);
		satellite_widget.screen =
			zui_screen_create(&satellite_widget_ops,
					  &satellite_widget);
	}

	return satellite_widget.screen;
}

static uint32_t satellite_widget_tick(struct meshbus_desktop_dashboard_widget *wctx)
{
	uint32_t next_ms;

	ARG_UNUSED(wctx);

	satellite_widget_snapshot_read(&satellite_widget.snapshot);
	next_ms = satellite_widget_tick_period_ms(&satellite_widget.snapshot);
	if (satellite_widget_apply_snapshot(&satellite_widget.model,
						      &satellite_widget.snapshot) &&
	    satellite_widget.screen != NULL) {
		(void)zui_screen_request_redraw(satellite_widget.screen);
	}

	return next_ms;
}
#endif

#if defined(CONFIG_MESHBUS_GNSS)
MESHBUS_DESKTOP_DASHBOARD_WIDGETS_DEFINE(MESHBUS_DESKTOP_DASHBOARD_WIDGET_ID_SATELLITE,
					 MESHBUS_DESKTOP_DASHBOARD_WIDGET_TITLE_SATELLITE,
					 satellite_widget_screen_create,
					 satellite_widget_tick,
					 MESHBUS_DESKTOP_APP_ID_GNSS);
#endif
