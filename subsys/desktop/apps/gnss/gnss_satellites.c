/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "gnss_private.h"

#include <errno.h>
#include <string.h>

#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "assets/assets_icons.h"
#include "text/desktop_text.h"

static const int16_t gnss_sin_15deg_x1000[] = {
	0,    259,  500,  707,  866,  966,  1000, 966,  866,  707,  500,  259, 0,
	-259, -500, -707, -866, -966, -1000, -966, -866, -707, -500, -259, 0,
};
static const int16_t gnss_cos_15deg_x1000[] = {
	1000, 966,  866,  707,  500,  259, 0,   -259, -500, -707, -866, -966, -1000,
	-966, -866, -707, -500, -259, 0,   259, 500,  707,  866,  966,  1000,
};

static const char *gnss_system_short(uint8_t system)
{
	switch (system) {
	case GNSS_SYSTEM_GPS:
		return DESKTOP_TEXT_GNSS_SYSTEM_GPS_SHORT;
	case GNSS_SYSTEM_GLONASS:
		return DESKTOP_TEXT_GNSS_SYSTEM_GLONASS_SHORT;
	case GNSS_SYSTEM_GALILEO:
		return DESKTOP_TEXT_GNSS_SYSTEM_GALILEO_SHORT;
	case GNSS_SYSTEM_BEIDOU:
		return DESKTOP_TEXT_GNSS_SYSTEM_BEIDOU_SHORT;
	case GNSS_SYSTEM_QZSS:
		return DESKTOP_TEXT_GNSS_SYSTEM_QZSS_SHORT;
	case GNSS_SYSTEM_IRNSS:
		return DESKTOP_TEXT_GNSS_SYSTEM_IRNSS_SHORT;
	case GNSS_SYSTEM_SBAS:
		return DESKTOP_TEXT_GNSS_SYSTEM_SBAS_SHORT;
	case GNSS_SYSTEM_IMES:
		return DESKTOP_TEXT_GNSS_SYSTEM_IMES_SHORT;
	default:
		return DESKTOP_TEXT_GNSS_SYSTEM_UNKNOWN_SHORT;
	}
}

static const char *gnss_system_full(uint8_t system)
{
	switch (system) {
	case GNSS_SYSTEM_GPS:
		return DESKTOP_TEXT_GNSS_SYSTEM_GPS_FULL;
	case GNSS_SYSTEM_GLONASS:
		return DESKTOP_TEXT_GNSS_SYSTEM_GLONASS_FULL;
	case GNSS_SYSTEM_GALILEO:
		return DESKTOP_TEXT_GNSS_SYSTEM_GALILEO_FULL;
	case GNSS_SYSTEM_BEIDOU:
		return DESKTOP_TEXT_GNSS_SYSTEM_BEIDOU_FULL;
	case GNSS_SYSTEM_QZSS:
		return DESKTOP_TEXT_GNSS_SYSTEM_QZSS_FULL;
	case GNSS_SYSTEM_IRNSS:
		return DESKTOP_TEXT_GNSS_SYSTEM_IRNSS_FULL;
	case GNSS_SYSTEM_SBAS:
		return DESKTOP_TEXT_GNSS_SYSTEM_SBAS_FULL;
	case GNSS_SYSTEM_IMES:
		return DESKTOP_TEXT_GNSS_SYSTEM_IMES_FULL;
	default:
		return DESKTOP_TEXT_GNSS_SYSTEM_UNKNOWN_FULL;
	}
}
int gnss_satellites_reload(struct gnss_app *app)
{
	struct gnss_satellite *sats = app->sat_reload_scratch;
	uint16_t count = 0U;
	int rc;

	app->sat_count = 0U;
	rc = mbs_gnss_satellites_get(sats, ARRAY_SIZE(app->sat_reload_scratch), &count);
	app->sat_last_rc = rc;
	if (rc == 0 && count > 0U) {
		for (uint16_t i = 0U; i < count; i++) {
			app->sat_data[i].prn = sats[i].prn;
			app->sat_data[i].system = (uint8_t)sats[i].system;
			app->sat_data[i].snr = sats[i].snr;
			app->sat_data[i].elevation = sats[i].elevation;
			app->sat_data[i].azimuth = sats[i].azimuth;
			app->sat_data[i].tracked = sats[i].is_tracked != 0U;
			app->sat_data[i].corrected = sats[i].is_corrected != 0U;
		}
		app->sat_count = count;
		if (app->sat_selected_idx >= app->sat_count) {
			app->sat_selected_idx = 0U;
		}
		return 0;
	}

	app->sat_selected_idx = 0U;
	return rc;
}

void gnss_satellites_update_list(struct gnss_app *app)
{
	size_t count = 0U;

	if (app->sat_count == 0U) {
		app->sat_items[count++] = (struct zui_list_item){
			.id = GNSS_SAT_ITEM_PLACEHOLDER,
			.label = app->sat_last_rc == 0 || app->sat_last_rc == -ENODATA ?
					 DESKTOP_TEXT_GNSS_NO_SATELLITES_PAREN :
					 DESKTOP_TEXT_GNSS_RELOAD_FAILED,
		};
	} else {
		for (uint16_t i = 0U; i < app->sat_count; i++) {
			const struct gnss_satellite_item *item = &app->sat_data[i];

			(void)snprintk(app->sat_labels[i], sizeof(app->sat_labels[i]), "%s[%u]",
				       gnss_system_short(item->system), (unsigned int)item->prn);
			app->sat_items[count++] = (struct zui_list_item){
				.id = i,
				.label = app->sat_labels[i],
				.detail = item->tracked ? DESKTOP_TEXT_GNSS_TRACKED :
							   DESKTOP_TEXT_GNSS_VISIBLE,
			};
		}
	}

	app->sat_items[count++] = (struct zui_list_item){
		.id = GNSS_SAT_ITEM_RELOAD,
		.label = DESKTOP_TEXT_GNSS_ACTION_RELOAD,
	};
	app->sat_item_count = count;
	(void)zui_sublist_update(app->satellites, &(struct zui_sublist_config){
		.title = DESKTOP_TEXT_GNSS_SATELLITES,
		.items = app->sat_items,
		.item_count = app->sat_item_count,
		.selected = gnss_satellites_selected,
		.user_data = app,
	});
}

void gnss_prepare_detail(struct gnss_app *app)
{
	const struct gnss_satellite_item *item;

	if (app->sat_selected_idx >= app->sat_count) {
		(void)snprintk(app->detail_header, sizeof(app->detail_header), "%s",
			       DESKTOP_TEXT_GNSS_SATELLITE);
		(void)snprintk(app->detail_text, sizeof(app->detail_text),
			       "%s", DESKTOP_TEXT_GNSS_NO_SATELLITE_SELECTED);
		return;
	}

	item = &app->sat_data[app->sat_selected_idx];
	(void)snprintk(app->detail_header, sizeof(app->detail_header), "%s[%u]",
		       gnss_system_full(item->system), (unsigned int)item->prn);
	(void)snprintk(app->detail_text, sizeof(app->detail_text),
		       DESKTOP_TEXT_GNSS_SATELLITE_DETAIL_FORMAT,
		       (unsigned int)item->snr, (unsigned int)item->elevation,
		       (unsigned int)item->azimuth,
		       item->tracked ? DESKTOP_TEXT_COMMON_YES : DESKTOP_TEXT_COMMON_NO,
		       item->corrected ? DESKTOP_TEXT_COMMON_YES : DESKTOP_TEXT_COMMON_NO);
}
void gnss_open_satellites(struct gnss_app *app)
{
	(void)gnss_satellites_reload(app);
	gnss_satellites_update_list(app);
	gnss_switch(app, GNSS_APP_SCREEN_SATELLITES);
}
void gnss_satellites_selected(struct zui_sublist *list, uint32_t id, size_t index,
				     const struct zui_input_event *event, void *user_data)
{
	struct gnss_app *app = user_data;
	int rc;

	ARG_UNUSED(list);
	ARG_UNUSED(event);

	if (id == GNSS_SAT_ITEM_RELOAD) {
		rc = gnss_satellites_reload(app);
		gnss_satellites_update_list(app);
		if (rc == 0) {
			gnss_toast(app, DESKTOP_TEXT_GNSS_SATELLITES_RELOAD, &I_done_24x24, 900U);
		} else if (rc == -ENODATA) {
			gnss_toast(app, DESKTOP_TEXT_GNSS_NO_SATELLITES, &I_done_24x24, 900U);
		} else {
			gnss_toast(app, DESKTOP_TEXT_GNSS_RELOAD_FAILED, &I_error_24x24, 1500U);
		}
		return;
	}
	if (id == GNSS_SAT_ITEM_PLACEHOLDER || index >= app->sat_count) {
		return;
	}

	app->sat_selected_idx = (uint16_t)index;
	gnss_prepare_detail(app);
	gnss_switch(app, GNSS_APP_SCREEN_DETAIL);
}
static void gnss_satellites_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct gnss_app *app = user_data;

	(void)zui_screen_draw(zui_sublist_get_screen(app->satellites), draw);
}

static bool gnss_satellites_input(const struct zui_input_event *event, void *user_data)
{
	struct gnss_app *app = user_data;

	return app != NULL && event != NULL &&
	       gnss_back_to_menu_input(event, app, zui_sublist_get_screen(app->satellites));
}

static void gnss_detail_text_line(struct zui_draw_ctx *draw, const char *text, size_t line_idx)
{
	char line[40];
	const char *end;
	size_t len;

	if (text == NULL || text[0] == '\0') {
		return;
	}
	for (size_t i = 0U; i < line_idx; i++) {
		text = strchr(text, '\n');
		if (text == NULL) {
			return;
		}
		text++;
	}
	end = strchr(text, '\n');
	len = end == NULL ? strlen(text) : (size_t)(end - text);
	len = MIN(len, sizeof(line) - 1U);
	memcpy(line, text, len);
	line[len] = '\0';
	zui_draw_text(draw, (struct zui_point){.x = 62, .y = (int16_t)(25 + line_idx * 8U)},
		      line);
}

static void gnss_azimuth_to_unit(uint16_t azimuth_deg, int32_t *sin_x1000, int32_t *cos_x1000)
{
	uint16_t az = azimuth_deg % 360U;
	uint16_t idx = az / 15U;
	uint16_t rem = az % 15U;

	*sin_x1000 = (gnss_sin_15deg_x1000[idx] * (15 - rem) +
		      gnss_sin_15deg_x1000[idx + 1U] * rem) /
		     15;
	*cos_x1000 = (gnss_cos_15deg_x1000[idx] * (15 - rem) +
		      gnss_cos_15deg_x1000[idx + 1U] * rem) /
		     15;
}

static void gnss_detail_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct gnss_app *app = user_data;
	bool has_sat = app != NULL && app->sat_selected_idx < app->sat_count;

	zui_draw_reset(draw);
	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	zui_draw_round_rect(draw, &(struct zui_rect){.x = 0, .y = 13, .width = 128, .height = 64},
			    5U);
	zui_draw_text(draw, (struct zui_point){.x = 2, .y = 10}, app->detail_header);

	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	zui_draw_circle(draw, (struct zui_point){.x = 28, .y = 39}, 16U);
	zui_draw_circle(draw, (struct zui_point){.x = 28, .y = 39}, 10U);
	zui_draw_circle(draw, (struct zui_point){.x = 28, .y = 39}, 5U);
	zui_draw_line(draw, (struct zui_point){.x = 12, .y = 39},
		      (struct zui_point){.x = 44, .y = 39});
	zui_draw_line(draw, (struct zui_point){.x = 28, .y = 23},
		      (struct zui_point){.x = 28, .y = 55});
	zui_draw_text_aligned(draw, (struct zui_point){.x = 28, .y = 22}, ZUI_ALIGN_CENTER,
			      ZUI_ALIGN_BOTTOM, DESKTOP_TEXT_GNSS_COMPASS_NORTH);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 46, .y = 39}, ZUI_ALIGN_LEFT,
			      ZUI_ALIGN_CENTER, DESKTOP_TEXT_GNSS_COMPASS_EAST);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 28, .y = 56}, ZUI_ALIGN_CENTER,
			      ZUI_ALIGN_TOP, DESKTOP_TEXT_GNSS_COMPASS_SOUTH);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 11, .y = 39}, ZUI_ALIGN_RIGHT,
			      ZUI_ALIGN_CENTER, DESKTOP_TEXT_GNSS_COMPASS_WEST);

	if (has_sat) {
		const struct gnss_satellite_item *item = &app->sat_data[app->sat_selected_idx];
		uint32_t el = MIN((uint32_t)item->elevation, 90U);
		uint32_t radius = (16U * (90U - el)) / 90U;
		int32_t sin_x1000;
		int32_t cos_x1000;
		int32_t px;
		int32_t py;

		gnss_azimuth_to_unit(item->azimuth, &sin_x1000, &cos_x1000);
		px = 28 + ((int32_t)radius * sin_x1000) / 1000;
		py = 39 - ((int32_t)radius * cos_x1000) / 1000;
		zui_draw_disc(draw, (struct zui_point){.x = CLAMP(px, 1, 126),
						       .y = CLAMP(py, 15, 63)},
			      2U);
	}

	for (size_t i = 0U; i < 5U; i++) {
		gnss_detail_text_line(draw, app->detail_text, i);
	}
}

static bool gnss_detail_input(const struct zui_input_event *event, void *user_data)
{
	struct gnss_app *app = user_data;

	if (app == NULL || event == NULL) {
		return false;
	}
	if (gnss_should_consume_edge(event)) {
		return true;
	}
	if (gnss_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		gnss_switch(app, GNSS_APP_SCREEN_SATELLITES);
		return true;
	}

	return false;
}
const struct zui_screen_ops gnss_satellites_ops = {
	.draw = gnss_satellites_draw,
	.input = gnss_satellites_input,
};
const struct zui_screen_ops gnss_detail_ops = {
	.draw = gnss_detail_draw,
	.input = gnss_detail_input,
};
