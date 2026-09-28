/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "widget_common.h"

#if defined(CONFIG_MBS_GNSS)
#define POSITION_WIDGET_TICK_FIX_MS	 1000U
#define POSITION_WIDGET_TICK_NO_FIX_MS 1500U
#define POSITION_WIDGET_TICK_OFF_MS	 3000U
#define POSITION_WIDGET_DEGREE_BOX_W	 3
#define POSITION_WIDGET_DEGREE_BOX_H	 3
#define POSITION_WIDGET_DEGREE_BOX_DY	 (-8)
#define POSITION_WIDGET_LAT_MAX_NANODEG 90000000000LL
#define POSITION_WIDGET_LON_MAX_NANODEG 180000000000LL

struct position_widget_model {
	bool available;
	bool enabled;
	bool has_fix;
	char lon_dir_str[2];
	char lon_deg_str[4];
	char lon_min_str[8];
	char lat_dir_str[2];
	char lat_deg_str[4];
	char lat_min_str[8];
	char alt_str[16];
};

struct position_widget_snapshot {
	bool available;
	bool enabled;
	bool has_fix;
	enum mbs_gnss_state state;
	char lon_dir_str[2];
	char lon_deg_str[4];
	char lon_min_str[8];
	char lat_dir_str[2];
	char lat_deg_str[4];
	char lat_min_str[8];
	char alt_str[16];
};

struct position_widget_state {
	struct zui_screen *screen;
	struct position_widget_model model;
	struct position_widget_snapshot snapshot;
};

static struct position_widget_state position_widget;

static int64_t position_widget_clamp_coord_nanodeg(int64_t value_nanodeg,
							     bool latitude)
{
	int64_t max_nanodeg =
		latitude ? POSITION_WIDGET_LAT_MAX_NANODEG : POSITION_WIDGET_LON_MAX_NANODEG;

	if (value_nanodeg > max_nanodeg) {
		return max_nanodeg;
	}
	if (value_nanodeg < -max_nanodeg) {
		return -max_nanodeg;
	}

	return value_nanodeg;
}

static uint64_t position_widget_abs_i64_u64(int64_t value)
{
	if (value >= 0) {
		return (uint64_t)value;
	}

	return (uint64_t)(-(value + 1)) + 1U;
}

static void position_widget_snapshot_defaults(
	struct position_widget_snapshot *snapshot)
{
	if (snapshot == NULL) {
		return;
	}

	snapshot->available = false;
	snapshot->enabled = false;
	snapshot->has_fix = false;
	snapshot->state = MBS_GNSS_STATE_SLEEP;
	desktop_widget_strcpy(snapshot->lon_dir_str, sizeof(snapshot->lon_dir_str),
			      DESKTOP_TEXT_WIDGET_POSITION_PLACEHOLDER_DIR);
	desktop_widget_strcpy(snapshot->lon_deg_str, sizeof(snapshot->lon_deg_str),
			      DESKTOP_TEXT_WIDGET_POSITION_PLACEHOLDER_DEG);
	desktop_widget_strcpy(snapshot->lon_min_str, sizeof(snapshot->lon_min_str),
			      DESKTOP_TEXT_WIDGET_POSITION_PLACEHOLDER_MIN);
	desktop_widget_strcpy(snapshot->lat_dir_str, sizeof(snapshot->lat_dir_str),
			      DESKTOP_TEXT_WIDGET_POSITION_PLACEHOLDER_DIR);
	desktop_widget_strcpy(snapshot->lat_deg_str, sizeof(snapshot->lat_deg_str),
			      DESKTOP_TEXT_WIDGET_POSITION_PLACEHOLDER_DEG);
	desktop_widget_strcpy(snapshot->lat_min_str, sizeof(snapshot->lat_min_str),
			      DESKTOP_TEXT_WIDGET_POSITION_PLACEHOLDER_MIN);
	desktop_widget_strcpy(snapshot->alt_str, sizeof(snapshot->alt_str),
			      DESKTOP_TEXT_WIDGET_POSITION_PLACEHOLDER_ALT);
}

static void position_widget_format_coord(char *dir_out, size_t dir_out_size,
						   char *deg_out, size_t deg_out_size,
						   char *min_out, size_t min_out_size,
						   int64_t value_nanodeg, bool latitude)
{
	char dir;
	uint64_t abs_val;
	int64_t coord_nanodeg;
	uint64_t deg;
	uint64_t rem;
	uint64_t min_milli;
	uint32_t min_whole;
	uint32_t min_frac;

	if (dir_out == NULL || dir_out_size == 0U || deg_out == NULL || deg_out_size == 0U ||
	    min_out == NULL || min_out_size == 0U) {
		return;
	}

	coord_nanodeg = position_widget_clamp_coord_nanodeg(value_nanodeg, latitude);
	dir = latitude ? 'N' : 'E';
	if (coord_nanodeg < 0) {
		dir = latitude ? 'S' : 'W';
	}
	abs_val = position_widget_abs_i64_u64(coord_nanodeg);

	deg = abs_val / 1000000000ULL;
	rem = abs_val % 1000000000ULL;
	min_milli = ((rem * 60000ULL) + 500000000ULL) / 1000000000ULL;
	if (min_milli >= 60000ULL) {
		min_milli -= 60000ULL;
		deg++;
	}

	min_whole = (uint32_t)(min_milli / 1000ULL);
	min_frac = (uint32_t)(min_milli % 1000ULL);
	(void)snprintk(dir_out, dir_out_size, "%c", dir);
	(void)snprintk(deg_out, deg_out_size, "%llu", (unsigned long long)deg);
	(void)snprintk(min_out, min_out_size, "%02u.%03u'", (unsigned int)min_whole,
		       (unsigned int)min_frac);
}

static void position_widget_format_altitude(char *out, size_t out_size,
						      int32_t altitude_mm)
{
	int64_t alt_mm;
	bool neg;
	uint64_t abs_mm;
	uint64_t tenth;
	uint64_t int_part;
	uint32_t frac_part;

	if (out == NULL || out_size == 0U) {
		return;
	}

	alt_mm = altitude_mm;
	neg = alt_mm < 0;
	abs_mm = (uint64_t)(neg ? -alt_mm : alt_mm);
	tenth = (abs_mm + 50ULL) / 100ULL;
	int_part = tenth / 10ULL;
	frac_part = (uint32_t)(tenth % 10ULL);
	if (neg && tenth > 0ULL) {
		(void)snprintk(out, out_size, "-%llu.%u", (unsigned long long)int_part,
			       (unsigned int)frac_part);
	} else {
		(void)snprintk(out, out_size, "%llu.%u", (unsigned long long)int_part,
			       (unsigned int)frac_part);
	}
}

static uint32_t position_widget_tick_period_ms(
	const struct position_widget_snapshot *snapshot)
{
	if (snapshot == NULL || !snapshot->available || !snapshot->enabled) {
		return POSITION_WIDGET_TICK_OFF_MS;
	}
	if (snapshot->state == MBS_GNSS_STATE_ACQUIRING) {
		return POSITION_WIDGET_TICK_NO_FIX_MS;
	}
	if (snapshot->state == MBS_GNSS_STATE_SLEEP ||
	    snapshot->state == MBS_GNSS_STATE_ERROR) {
		return POSITION_WIDGET_TICK_OFF_MS;
	}
	if (snapshot->has_fix) {
		return POSITION_WIDGET_TICK_FIX_MS;
	}

	return POSITION_WIDGET_TICK_NO_FIX_MS;
}

static void position_widget_snapshot_read(
	struct position_widget_snapshot *snapshot)
{
	mbs_gnss_config cfg;
	struct navigation_data nav;

	if (snapshot == NULL) {
		return;
	}

	position_widget_snapshot_defaults(snapshot);
	if (mbs_gnss_config_get(&cfg) != 0) {
		return;
	}

	snapshot->available = true;
	snapshot->enabled = cfg.enabled;
	snapshot->state = mbs_gnss_state_get();
	if (!snapshot->enabled) {
		return;
	}
	if (mbs_gnss_position_get(&nav) != 0) {
		return;
	}

	snapshot->has_fix = true;
	position_widget_format_coord(snapshot->lon_dir_str,
					       sizeof(snapshot->lon_dir_str),
					       snapshot->lon_deg_str,
					       sizeof(snapshot->lon_deg_str),
					       snapshot->lon_min_str,
					       sizeof(snapshot->lon_min_str), nav.longitude,
					       false);
	position_widget_format_coord(snapshot->lat_dir_str,
					       sizeof(snapshot->lat_dir_str),
					       snapshot->lat_deg_str,
					       sizeof(snapshot->lat_deg_str),
					       snapshot->lat_min_str,
					       sizeof(snapshot->lat_min_str), nav.latitude,
					       true);
	position_widget_format_altitude(snapshot->alt_str, sizeof(snapshot->alt_str),
						  nav.altitude);
}

static bool position_widget_apply_snapshot(
	struct position_widget_model *model,
	const struct position_widget_snapshot *snapshot)
{
	bool changed = false;

	if (model == NULL || snapshot == NULL) {
		return false;
	}

	changed |= model->available != snapshot->available;
	changed |= model->enabled != snapshot->enabled;
	changed |= model->has_fix != snapshot->has_fix;
	changed |= strcmp(model->lon_dir_str, snapshot->lon_dir_str) != 0;
	changed |= strcmp(model->lon_deg_str, snapshot->lon_deg_str) != 0;
	changed |= strcmp(model->lon_min_str, snapshot->lon_min_str) != 0;
	changed |= strcmp(model->lat_dir_str, snapshot->lat_dir_str) != 0;
	changed |= strcmp(model->lat_deg_str, snapshot->lat_deg_str) != 0;
	changed |= strcmp(model->lat_min_str, snapshot->lat_min_str) != 0;
	changed |= strcmp(model->alt_str, snapshot->alt_str) != 0;
	if (!changed) {
		return false;
	}

	model->available = snapshot->available;
	model->enabled = snapshot->enabled;
	model->has_fix = snapshot->has_fix;
	desktop_widget_strcpy(model->lon_dir_str, sizeof(model->lon_dir_str),
			      snapshot->lon_dir_str);
	desktop_widget_strcpy(model->lon_deg_str, sizeof(model->lon_deg_str),
			      snapshot->lon_deg_str);
	desktop_widget_strcpy(model->lon_min_str, sizeof(model->lon_min_str),
			      snapshot->lon_min_str);
	desktop_widget_strcpy(model->lat_dir_str, sizeof(model->lat_dir_str),
			      snapshot->lat_dir_str);
	desktop_widget_strcpy(model->lat_deg_str, sizeof(model->lat_deg_str),
			      snapshot->lat_deg_str);
	desktop_widget_strcpy(model->lat_min_str, sizeof(model->lat_min_str),
			      snapshot->lat_min_str);
	desktop_widget_strcpy(model->alt_str, sizeof(model->alt_str), snapshot->alt_str);
	return true;
}

static void position_widget_model_defaults(struct position_widget_model *model)
{
	struct position_widget_snapshot snapshot;

	if (model == NULL) {
		return;
	}

	memset(model, 0, sizeof(*model));
	position_widget_snapshot_defaults(&snapshot);
	(void)position_widget_apply_snapshot(model, &snapshot);
}

static void position_widget_draw_deg_min(struct zui_draw_ctx *draw, int16_t x,
						   int16_t y, const char *deg_str,
						   const char *min_str)
{
	uint16_t deg_w;
	uint16_t deg_field_w;
	int16_t deg_x;
	int16_t sym_x;
	int16_t sy;
	static uint16_t degree_field_w_cache;

	if (deg_str == NULL) {
		deg_str = DESKTOP_TEXT_WIDGET_POSITION_PLACEHOLDER_DEG;
	}
	if (min_str == NULL) {
		min_str = DESKTOP_TEXT_WIDGET_POSITION_PLACEHOLDER_MIN;
	}

	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	if (degree_field_w_cache == 0U) {
		size_t max_digit_w = 0U;

		for (char d = '0'; d <= '9'; d++) {
			size_t w = zui_draw_glyph_width(draw, (uint32_t)d);
			if (w > max_digit_w) {
				max_digit_w = w;
			}
		}
		degree_field_w_cache = (uint16_t)(max_digit_w * 3U);
	}

	deg_field_w = degree_field_w_cache;
	deg_w = zui_draw_text_width(draw, deg_str);
	if (deg_w > deg_field_w) {
		deg_field_w = deg_w;
	}

	deg_x = x + (int16_t)deg_field_w - (int16_t)deg_w;
	zui_draw_text(draw, (struct zui_point){.x = deg_x, .y = y}, deg_str);
	sym_x = x + (int16_t)deg_field_w;
	sy = y + POSITION_WIDGET_DEGREE_BOX_DY;
	if (sy < 0) {
		sy = 0;
	}
	zui_draw_rect(draw, &(struct zui_rect){.x = sym_x + 1, .y = sy,
					       .width = POSITION_WIDGET_DEGREE_BOX_W,
					       .height = POSITION_WIDGET_DEGREE_BOX_H});
	zui_draw_text(draw, (struct zui_point){.x = sym_x + 1 + POSITION_WIDGET_DEGREE_BOX_W + 1,
					       .y = y},
		      min_str);
}

static void position_widget_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct position_widget_state *state = user_data;
	const struct position_widget_model *model =
		state != NULL ? &state->model : NULL;
	const char *lon_dir = model != NULL ? model->lon_dir_str :
					    DESKTOP_TEXT_WIDGET_POSITION_PLACEHOLDER_DIR;
	const char *lon_deg = model != NULL ? model->lon_deg_str :
					    DESKTOP_TEXT_WIDGET_POSITION_PLACEHOLDER_DEG;
	const char *lon_min = model != NULL ? model->lon_min_str :
					    DESKTOP_TEXT_WIDGET_POSITION_PLACEHOLDER_MIN;
	const char *lat_dir = model != NULL ? model->lat_dir_str :
					    DESKTOP_TEXT_WIDGET_POSITION_PLACEHOLDER_DIR;
	const char *lat_deg = model != NULL ? model->lat_deg_str :
					    DESKTOP_TEXT_WIDGET_POSITION_PLACEHOLDER_DEG;
	const char *lat_min = model != NULL ? model->lat_min_str :
					    DESKTOP_TEXT_WIDGET_POSITION_PLACEHOLDER_MIN;
	const char *alt = model != NULL ? model->alt_str :
					DESKTOP_TEXT_WIDGET_POSITION_PLACEHOLDER_ALT;

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	zui_draw_bitmap(draw, (struct zui_point){.x = 1,
						 .y = MBS_DESKTOP_DASHBOARD_HEADER_HEIGHT + 1},
			9, 5, ZUI_BITMAP_FORMAT_XBM, B_round_shadow_9x5);
	zui_draw_line(draw, (struct zui_point){.x = 11, .y = 13},
		      (struct zui_point){.x = 124, .y = 13});
	zui_draw_line(draw, (struct zui_point){.x = 1, .y = 19},
		      (struct zui_point){.x = 1, .y = 38});
	zui_draw_text(draw, (struct zui_point){.x = 4, .y = 24},
		      DESKTOP_TEXT_WIDGET_POSITION_LABEL_POSITION);
	zui_draw_text(draw, (struct zui_point){.x = 54, .y = 24}, lon_dir);
	position_widget_draw_deg_min(draw, 66, 24, lon_deg, lon_min);
	zui_draw_text(draw, (struct zui_point){.x = 54, .y = 36}, lat_dir);
	position_widget_draw_deg_min(draw, 66, 36, lat_deg, lat_min);

	zui_draw_line(draw, (struct zui_point){.x = 11, .y = 40},
		      (struct zui_point){.x = 124, .y = 40});
	zui_draw_line(draw, (struct zui_point){.x = 1, .y = 46},
		      (struct zui_point){.x = 1, .y = 63});
	zui_draw_bitmap(draw, (struct zui_point){.x = 1, .y = 40}, 9, 5,
			ZUI_BITMAP_FORMAT_XBM, B_round_shadow_9x5);
	zui_draw_text(draw, (struct zui_point){.x = 4, .y = 51},
		      DESKTOP_TEXT_WIDGET_POSITION_LABEL_ALTITUDE);
	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	zui_draw_text(draw, (struct zui_point){.x = 116, .y = 49},
		      DESKTOP_TEXT_WIDGET_POSITION_UNIT_METER);
	zui_draw_set_font(draw, ZUI_FONT_BIG_NUMBERS);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 112, .y = 50},
			      ZUI_ALIGN_RIGHT, ZUI_ALIGN_CENTER, alt);
}

static const struct zui_screen_ops position_widget_ops = {
	.draw = position_widget_draw,
};

static struct zui_screen *position_widget_screen_create(
	struct mbs_desktop_dashboard_widget *wctx)
{
	ARG_UNUSED(wctx);

	if (position_widget.screen == NULL) {
		position_widget_model_defaults(&position_widget.model);
		position_widget.screen =
			zui_screen_create(&position_widget_ops,
					  &position_widget);
	}

	return position_widget.screen;
}

static uint32_t position_widget_tick(struct mbs_desktop_dashboard_widget *wctx)
{
	uint32_t next_ms;

	ARG_UNUSED(wctx);

	position_widget_snapshot_read(&position_widget.snapshot);
	next_ms = position_widget_tick_period_ms(&position_widget.snapshot);
	if (position_widget_apply_snapshot(&position_widget.model,
						     &position_widget.snapshot) &&
	    position_widget.screen != NULL) {
		(void)zui_screen_request_redraw(position_widget.screen);
	}

	return next_ms;
}
#endif

#if defined(CONFIG_MBS_GNSS)
MBS_DESKTOP_DASHBOARD_WIDGETS_DEFINE(MBS_DESKTOP_DASHBOARD_WIDGET_ID_POSITION,
					 MBS_DESKTOP_DASHBOARD_WIDGET_TITLE_POSITION,
					 position_widget_screen_create,
					 position_widget_tick,
					 MBS_DESKTOP_APP_ID_GNSS);
#endif
