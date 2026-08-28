/* SPDX-License-Identifier: Apache-2.0 */

#include "gnss_private.h"

#include <string.h>

#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "text/desktop_text.h"

static const char *gnss_state_str(enum meshbus_gnss_state state)
{
	switch (state) {
	case MESHBUS_GNSS_STATE_SLEEP:
		return DESKTOP_TEXT_GNSS_STATE_SLEEP;
	case MESHBUS_GNSS_STATE_ACQUIRING:
		return DESKTOP_TEXT_GNSS_STATE_ACQUIRING;
	case MESHBUS_GNSS_STATE_TRACK:
		return DESKTOP_TEXT_GNSS_STATE_TRACK;
	case MESHBUS_GNSS_STATE_ERROR:
		return DESKTOP_TEXT_GNSS_STATE_ERROR;
	default:
		return DESKTOP_TEXT_COMMON_UNKNOWN;
	}
}

static const char *gnss_fix_str(enum gnss_fix_status status)
{
	switch (status) {
	case GNSS_FIX_STATUS_NO_FIX:
		return DESKTOP_TEXT_GNSS_FIX_NO_FIX;
	case GNSS_FIX_STATUS_GNSS_FIX:
		return DESKTOP_TEXT_GNSS_FIX_GNSS_FIX;
	case GNSS_FIX_STATUS_DGNSS_FIX:
		return DESKTOP_TEXT_GNSS_FIX_DGNSS_FIX;
	case GNSS_FIX_STATUS_ESTIMATED_FIX:
		return DESKTOP_TEXT_GNSS_FIX_ESTIMATED_FIX;
	default:
		return DESKTOP_TEXT_COMMON_UNKNOWN;
	}
}

static const char *gnss_quality_str(enum gnss_fix_quality quality)
{
	switch (quality) {
	case GNSS_FIX_QUALITY_INVALID:
		return DESKTOP_TEXT_GNSS_QUALITY_INVALID;
	case GNSS_FIX_QUALITY_GNSS_SPS:
		return DESKTOP_TEXT_GNSS_QUALITY_GNSS_SPS;
	case GNSS_FIX_QUALITY_DGNSS:
		return DESKTOP_TEXT_GNSS_QUALITY_DGNSS;
	case GNSS_FIX_QUALITY_GNSS_PPS:
		return DESKTOP_TEXT_GNSS_QUALITY_GNSS_PPS;
	case GNSS_FIX_QUALITY_RTK:
		return DESKTOP_TEXT_GNSS_QUALITY_RTK;
	case GNSS_FIX_QUALITY_FLOAT_RTK:
		return DESKTOP_TEXT_GNSS_QUALITY_FLOAT_RTK;
	case GNSS_FIX_QUALITY_ESTIMATED:
		return DESKTOP_TEXT_GNSS_QUALITY_ESTIMATED;
	default:
		return DESKTOP_TEXT_COMMON_UNKNOWN;
	}
}
static void gnss_format_coord(char *buf, size_t size, int64_t coord_nanodeg)
{
	bool negative = coord_nanodeg < 0;
	uint64_t abs_value = (uint64_t)(negative ? -coord_nanodeg : coord_nanodeg);

	(void)snprintk(buf, size, "%s%llu.%06llu %s", negative ? "-" : "",
		       (unsigned long long)(abs_value / 1000000000ULL),
		       (unsigned long long)((abs_value % 1000000000ULL) / 1000ULL),
		       DESKTOP_TEXT_GNSS_UNIT_DEG);
}

static void gnss_format_milli(char *buf, size_t size, int32_t value, const char *unit)
{
	int64_t value64 = value;
	bool negative = value64 < 0;
	uint32_t abs_value;

	if (negative) {
		value64 = -value64;
	}
	abs_value = (uint32_t)value64;
	(void)snprintk(buf, size, "%s%u.%03u %s", negative ? "-" : "",
		       (unsigned int)(abs_value / 1000U), (unsigned int)(abs_value % 1000U),
		       unit);
}

static void gnss_status_refresh(struct gnss_app *app, bool preserve_scroll)
{
	struct gnss_status_scratch *scratch = &app->status_scratch;
	const char *enabled = DESKTOP_TEXT_GNSS_NOT_AVAILABLE;
	const char *fix_status = DESKTOP_TEXT_GNSS_NOT_AVAILABLE;
	const char *quality = DESKTOP_TEXT_GNSS_NOT_AVAILABLE;
	size_t scroll =
		preserve_scroll ? zui_text_view_scroll(app->status_view) : 0U;

	memset(scratch, 0, sizeof(*scratch));
	(void)snprintk(scratch->hdop, sizeof(scratch->hdop), "%s",
		       DESKTOP_TEXT_GNSS_NOT_AVAILABLE);
	(void)snprintk(scratch->tracked, sizeof(scratch->tracked), "%s",
		       DESKTOP_TEXT_GNSS_NOT_AVAILABLE);
	(void)snprintk(scratch->latitude, sizeof(scratch->latitude), "%s",
		       DESKTOP_TEXT_GNSS_NOT_AVAILABLE);
	(void)snprintk(scratch->longitude, sizeof(scratch->longitude), "%s",
		       DESKTOP_TEXT_GNSS_NOT_AVAILABLE);
	(void)snprintk(scratch->altitude, sizeof(scratch->altitude), "%s",
		       DESKTOP_TEXT_GNSS_NOT_AVAILABLE);
	(void)snprintk(scratch->speed, sizeof(scratch->speed), "%s",
		       DESKTOP_TEXT_GNSS_NOT_AVAILABLE);
	(void)snprintk(scratch->bearing, sizeof(scratch->bearing), "%s",
		       DESKTOP_TEXT_GNSS_NOT_AVAILABLE);
	(void)snprintk(scratch->utc, sizeof(scratch->utc), "%s",
		       DESKTOP_TEXT_GNSS_NOT_AVAILABLE);

	(void)snprintk(scratch->visible, sizeof(scratch->visible), "%u",
		       (unsigned int)meshbus_gnss_satellites_count());
	if (meshbus_gnss_config_get(&scratch->cfg) == 0) {
		enabled = scratch->cfg.enabled ? DESKTOP_TEXT_COMMON_YES :
						  DESKTOP_TEXT_COMMON_NO;
	}
	if (meshbus_gnss_info_get(&scratch->info) == 0) {
		fix_status = gnss_fix_str(scratch->info.fix_status);
		quality = gnss_quality_str(scratch->info.fix_quality);
		(void)snprintk(scratch->hdop, sizeof(scratch->hdop), "%u.%03u",
			       (unsigned int)(scratch->info.hdop / 1000U),
			       (unsigned int)(scratch->info.hdop % 1000U));
		(void)snprintk(scratch->tracked, sizeof(scratch->tracked), "%u",
			       (unsigned int)scratch->info.satellites_cnt);
	}
	if (meshbus_gnss_position_get(&scratch->nav) == 0) {
		gnss_format_coord(scratch->latitude, sizeof(scratch->latitude),
				  scratch->nav.latitude);
		gnss_format_coord(scratch->longitude, sizeof(scratch->longitude),
				  scratch->nav.longitude);
		gnss_format_milli(scratch->altitude, sizeof(scratch->altitude),
				  scratch->nav.altitude, DESKTOP_TEXT_GNSS_UNIT_M);
		gnss_format_milli(scratch->speed, sizeof(scratch->speed),
				  (int32_t)scratch->nav.speed, DESKTOP_TEXT_GNSS_UNIT_MPS);
		gnss_format_milli(scratch->bearing, sizeof(scratch->bearing),
				  (int32_t)scratch->nav.bearing, DESKTOP_TEXT_GNSS_UNIT_DEG);
	}
	if (meshbus_gnss_time_get(&scratch->time) == 0) {
		(void)snprintk(scratch->utc, sizeof(scratch->utc),
			       "%04u-%02u-%02u %02u:%02u:%02u.%03u",
			       2000U + (unsigned int)scratch->time.century_year,
			       (unsigned int)scratch->time.month,
			       (unsigned int)scratch->time.month_day,
			       (unsigned int)scratch->time.hour,
			       (unsigned int)scratch->time.minute,
			       (unsigned int)(scratch->time.millisecond / 1000U),
			       (unsigned int)(scratch->time.millisecond % 1000U));
	}

	(void)snprintk(app->status_text, sizeof(app->status_text),
		       DESKTOP_TEXT_GNSS_STATUS_DETAIL_FORMAT,
		       enabled, gnss_state_str(meshbus_gnss_state_get()), fix_status, quality,
		       scratch->hdop, scratch->tracked, scratch->visible, scratch->latitude,
		       scratch->longitude, scratch->altitude, scratch->speed, scratch->bearing,
		       scratch->utc);
	(void)zui_text_view_update(app->status_view, &(struct zui_text_view_config){
		.title = DESKTOP_TEXT_GNSS_STATUS_TITLE,
		.text = app->status_text,
		.font = ZUI_FONT_SECONDARY,
		.mode = ZUI_TEXT_VIEW_MODE_TEXT,
	});
	if (preserve_scroll) {
		(void)zui_text_view_set_scroll(app->status_view, scroll);
	}
}

void gnss_open_status(struct gnss_app *app)
{
	gnss_status_refresh(app, false);
	gnss_switch(app, GNSS_APP_SCREEN_STATUS);
}
static void gnss_status_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct gnss_app *app = user_data;

	(void)zui_screen_draw(zui_text_view_get_screen(app->status_view), draw);
}

static bool gnss_status_input(const struct zui_input_event *event, void *user_data)
{
	struct gnss_app *app = user_data;

	if (app == NULL || event == NULL) {
		return false;
	}
	if (gnss_is_long(event) && event->code == ZUI_INPUT_CODE_SELECT) {
		gnss_status_refresh(app, true);
		gnss_request_redraw(app);
		return true;
	}

	return gnss_back_to_menu_input(
		event, app, zui_text_view_get_screen(app->status_view));
}
const struct zui_screen_ops gnss_status_ops = {
	.draw = gnss_status_draw,
	.input = gnss_status_input,
};
