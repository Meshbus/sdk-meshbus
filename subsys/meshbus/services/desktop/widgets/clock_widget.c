/* SPDX-License-Identifier: Apache-2.0 */

#include "widget_common.h"

struct clock_widget_model {
	bool available;
	char hh_str[3];
	char mm_str[3];
	char ss_str[3];
	char day_str[4];
	char date_str[8];
	char meridiem_str[2];
	char sync_str[7];
};

struct clock_widget_state {
	struct zui_screen *screen;
	struct clock_widget_model model;
};

static struct clock_widget_state clock_widget;

static void clock_widget_format_u8_2digits(char out[3], uint8_t value)
{
	if (out == NULL) {
		return;
	}

	out[0] = (char)('0' + ((value / 10U) % 10U));
	out[1] = (char)('0' + (value % 10U));
	out[2] = '\0';
}

static void clock_widget_model_unavailable(struct clock_widget_model *model)
{
	if (model == NULL) {
		return;
	}

	model->available = false;
	desktop_widget_strcpy(model->hh_str, sizeof(model->hh_str),
			      DESKTOP_TEXT_WIDGET_CLOCK_PLACEHOLDER_HMS);
	desktop_widget_strcpy(model->mm_str, sizeof(model->mm_str),
			      DESKTOP_TEXT_WIDGET_CLOCK_PLACEHOLDER_HMS);
	desktop_widget_strcpy(model->ss_str, sizeof(model->ss_str),
			      DESKTOP_TEXT_WIDGET_CLOCK_PLACEHOLDER_HMS);
	desktop_widget_strcpy(model->day_str, sizeof(model->day_str),
			      DESKTOP_TEXT_WIDGET_CLOCK_PLACEHOLDER_DAY);
	desktop_widget_strcpy(model->date_str, sizeof(model->date_str),
			      DESKTOP_TEXT_WIDGET_CLOCK_PLACEHOLDER_DATE);
	desktop_widget_strcpy(model->meridiem_str, sizeof(model->meridiem_str),
			      DESKTOP_TEXT_COMMON_EMPTY);
	desktop_widget_strcpy(model->sync_str, sizeof(model->sync_str),
			      DESKTOP_TEXT_WIDGET_CLOCK_UNSYNC);
}

static void clock_widget_snapshot(struct clock_widget_model *model)
{
#if defined(CONFIG_MESHBUS_CLOCK)
	meshbus_clock_config cfg;
	struct timespec ts;
	struct tm tm_now;
	bool use_12h;
	uint8_t hour;
	int rc;
#endif

	if (model == NULL) {
		return;
	}

	clock_widget_model_unavailable(model);

#if defined(CONFIG_MESHBUS_CLOCK)
	rc = meshbus_clock_config_get(&cfg);
	if (rc != 0) {
		return;
	}

	rc = sys_clock_gettime(SYS_CLOCK_REALTIME, &ts);
	if (rc != 0 || ts.tv_sec <= 0) {
		return;
	}

	desktop_widget_strcpy(model->sync_str, sizeof(model->sync_str),
			      ((int64_t)ts.tv_sec > CLOCK_WIDGET_SYNC_FALLBACK_UNIX) ?
			      DESKTOP_TEXT_WIDGET_CLOCK_SYNC : DESKTOP_TEXT_WIDGET_CLOCK_UNSYNC);

	if (cfg.time_format == meshbus_ClockConfig_ClockTimeFormat_TIME_FORMAT_12H) {
		use_12h = true;
	} else if (cfg.time_format == meshbus_ClockConfig_ClockTimeFormat_TIME_FORMAT_24H) {
		use_12h = false;
	} else {
		return;
	}

	rc = meshbus_clock_localtime(ts.tv_sec, &tm_now);
	if (rc != 0) {
		return;
	}

	model->available = true;
	if (use_12h) {
		hour = (uint8_t)(tm_now.tm_hour % 12);
		if (hour == 0U) {
			hour = 12U;
		}
		clock_widget_format_u8_2digits(model->hh_str, hour);
		desktop_widget_strcpy(model->meridiem_str, sizeof(model->meridiem_str),
				      tm_now.tm_hour >= 12 ?
				      DESKTOP_TEXT_WIDGET_CLOCK_MERIDIEM_PM :
				      DESKTOP_TEXT_WIDGET_CLOCK_MERIDIEM_AM);
	} else {
		clock_widget_format_u8_2digits(model->hh_str, (uint8_t)tm_now.tm_hour);
		desktop_widget_strcpy(model->meridiem_str, sizeof(model->meridiem_str),
				      DESKTOP_TEXT_COMMON_EMPTY);
	}
	clock_widget_format_u8_2digits(model->mm_str, (uint8_t)tm_now.tm_min);
	clock_widget_format_u8_2digits(model->ss_str, (uint8_t)tm_now.tm_sec);

	if (tm_now.tm_wday >= 0 && tm_now.tm_wday < 7) {
		desktop_widget_strcpy(model->day_str, sizeof(model->day_str),
				      DESKTOP_TEXT_WIDGET_CLOCK_DAY_VALUES[tm_now.tm_wday]);
	} else {
		desktop_widget_strcpy(model->day_str, sizeof(model->day_str),
				      DESKTOP_TEXT_WIDGET_CLOCK_PLACEHOLDER_DAY);
	}
	(void)snprintk(model->date_str, sizeof(model->date_str), "%u-%02u",
		       (unsigned int)(tm_now.tm_mon + 1), (unsigned int)tm_now.tm_mday);
#endif
}

static void clock_widget_draw_digit(struct zui_draw_ctx *draw, int16_t x, int16_t y,
					      const char *digits, size_t index)
{
	char digit_buf[2] = {0};

	digit_buf[0] = (digits != NULL && digits[index] != '\0') ? digits[index] : '-';
	zui_draw_text(draw, (struct zui_point){.x = x, .y = y}, digit_buf);
}

static void clock_widget_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct clock_widget_state *state = user_data;
	const struct clock_widget_model *model = state != NULL ? &state->model : NULL;
	struct clock_widget_model fallback;

	if (model == NULL) {
		clock_widget_model_unavailable(&fallback);
		model = &fallback;
	}

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);

	zui_draw_set_font_data(draw, F_segment_46);
	clock_widget_draw_digit(draw, 7, 61, model->hh_str, 0U);
	clock_widget_draw_digit(draw, 29, 61, model->hh_str, 1U);
	clock_widget_draw_digit(draw, 57, 61, model->mm_str, 0U);
	clock_widget_draw_digit(draw, 79, 61, model->mm_str, 1U);

	zui_draw_set_font_data(draw, F_segment_24);
	clock_widget_draw_digit(draw, 102, 61, model->ss_str, 0U);
	clock_widget_draw_digit(draw, 114, 61, model->ss_str, 1U);

	zui_draw_disc(draw, (struct zui_point){.x = 56, .y = 40}, 2);
	zui_draw_disc(draw, (struct zui_point){.x = 55, .y = 53}, 2);

	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	zui_draw_text(draw, (struct zui_point){.x = 4, .y = 38}, model->meridiem_str);

	zui_draw_line(draw, (struct zui_point){.x = 0, .y = 26},
		      (struct zui_point){.x = 127, .y = 26});
	zui_draw_line(draw, (struct zui_point){.x = 95, .y = 12},
		      (struct zui_point){.x = 95, .y = 26});
	zui_draw_line(draw, (struct zui_point){.x = 58, .y = 12},
		      (struct zui_point){.x = 58, .y = 26});
	zui_draw_text_aligned(draw, (struct zui_point){.x = 77, .y = 19},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER, model->day_str);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 112, .y = 19},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER, model->date_str);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 4, .y = 19},
			      ZUI_ALIGN_LEFT, ZUI_ALIGN_CENTER, model->sync_str);
}

static const struct zui_screen_ops clock_widget_ops = {
	.draw = clock_widget_draw,
};

static struct zui_screen *clock_widget_screen_create(
	struct meshbus_desktop_dashboard_widget *wctx)
{
	ARG_UNUSED(wctx);

	if (clock_widget.screen == NULL) {
		clock_widget_model_unavailable(&clock_widget.model);
		clock_widget.screen =
			zui_screen_create(&clock_widget_ops, &clock_widget);
	}

	return clock_widget.screen;
}

static uint32_t clock_widget_tick(struct meshbus_desktop_dashboard_widget *wctx)
{
	ARG_UNUSED(wctx);

	clock_widget_snapshot(&clock_widget.model);
	if (clock_widget.screen != NULL) {
		(void)zui_screen_request_redraw(clock_widget.screen);
	}

	return clock_widget.model.available ? CLOCK_WIDGET_TICK_READY_MS :
							CLOCK_WIDGET_TICK_UNAVAILABLE_MS;
}

MESHBUS_DESKTOP_DASHBOARD_WIDGETS_DEFINE(MESHBUS_DESKTOP_DASHBOARD_WIDGET_ID_CLOCK,
					 MESHBUS_DESKTOP_DASHBOARD_WIDGET_TITLE_CLOCK,
					 clock_widget_screen_create,
					 clock_widget_tick,
					 MESHBUS_DESKTOP_APP_ID_SYSTEM);
