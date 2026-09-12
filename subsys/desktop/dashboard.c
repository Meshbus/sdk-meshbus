/* SPDX-License-Identifier: Apache-2.0 */

#include "desktop_private.h"

#include "assets/assets_icons.h"
#include "assets/assets_xbms.h"
#include "services/messages_cache.h"
#include "text/desktop_text.h"

#include <errno.h>
#include <time.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zui/zui.h>
#if defined(CONFIG_MBS_CLOCK)
#include <clock/clock.h>
#endif
#if defined(CONFIG_MBS_BLUETOOTH)
#include <bluetooth/bluetooth.h>
#endif
#if defined(CONFIG_MBS_GNSS)
#include <gnss/gnss.h>
#endif
#if defined(CONFIG_MBS_INDICATOR)
#include <indicator/indicator.h>
#endif
#if defined(CONFIG_MBS_POWER)
#include <power/power.h>
#endif
#if defined(CONFIG_MBS_RADIO)
#include <radio/radio.h>
#endif

LOG_MODULE_DECLARE(mbs_desktop, CONFIG_MBS_DESKTOP_LOG_LEVEL);

#define MBS_DESKTOP_DASHBOARD_TICK_MIN_MS 20U

#define DASHBOARD_HEADER_STATUS_PERIOD_MS 1000U
#define DASHBOARD_STORAGE_MOUNT_POINT     "/extra"

#define DASHBOARD_TAB_AUTOHIDE_DELAY_MS 2000U
#define DASHBOARD_TAB_ANIM_PERIOD_MS    MBS_DESKTOP_DASHBOARD_TAB_ANIM_PERIOD_MS
#define DASHBOARD_TAB_ANIM_STEP_PX      1U

#define DASHBOARD_HEADER_BATTERY_X		    2
#define DASHBOARD_HEADER_BATTERY_Y		    2
#define DASHBOARD_HEADER_BATTERY_ICON_W	    14
#define DASHBOARD_HEADER_BATTERY_ICON_H	    8
#define DASHBOARD_HEADER_BATTERY_FILL_X	    2
#define DASHBOARD_HEADER_BATTERY_FILL_Y	    2
#define DASHBOARD_HEADER_BATTERY_FILL_W	    9
#define DASHBOARD_HEADER_BATTERY_FILL_H	    4
#define DASHBOARD_HEADER_BATTERY_ANIM_MS 1400U
#define DASHBOARD_HEADER_STATUS_START_X \
	(DASHBOARD_HEADER_BATTERY_X + DASHBOARD_HEADER_BATTERY_ICON_W + 4)
#define DASHBOARD_HEADER_STATUS_Y        2
#define DASHBOARD_HEADER_STATUS_SLOT_W   11
#define DASHBOARD_HEADER_STATUS_STEP_X   13

static void desktop_dashboard_header_time_refresh(struct zui_desktop *desktop)
{
	uint8_t hh = 0U;
	uint8_t mm = 0U;
	bool have_time = false;

	if (desktop == NULL) {
		return;
	}

#if defined(CONFIG_MBS_CLOCK) && \
	(IS_ENABLED(CONFIG_POSIX_API) || IS_ENABLED(CONFIG_NEWLIB_LIBC) || \
	 IS_ENABLED(CONFIG_PICOLIBC))
	{
		time_t now = time(NULL);
		struct tm local_time;

		if (now > 0 && mbs_clock_localtime(now, &local_time) == 0) {
			hh = (uint8_t)local_time.tm_hour;
			mm = (uint8_t)local_time.tm_min;
			have_time = true;
		}
	}
#endif
	if (!have_time) {
		uint32_t total_minutes = (uint32_t)(k_uptime_get() / (60 * 1000));

		hh = (uint8_t)((total_minutes / 60U) % 24U);
		mm = (uint8_t)(total_minutes % 60U);
	}

	desktop->dashboard_header_time[0] = (char)('0' + (hh / 10U));
	desktop->dashboard_header_time[1] = (char)('0' + (hh % 10U));
	desktop->dashboard_header_time[2] = ':';
	desktop->dashboard_header_time[3] = (char)('0' + (mm / 10U));
	desktop->dashboard_header_time[4] = (char)('0' + (mm % 10U));
	desktop->dashboard_header_time[5] = '\0';
}

static void desktop_dashboard_header_refresh(struct zui_desktop *desktop)
{
	if (desktop == NULL) {
		return;
	}

	desktop_dashboard_header_time_refresh(desktop);

	desktop->dashboard_header_battery_valid = false;
	desktop->dashboard_header_battery_charging = false;
	desktop->dashboard_header_battery_online = false;
	desktop->dashboard_header_battery_soc = 0U;
#if defined(CONFIG_MBS_POWER)
	{
		uint16_t voltage_mv;
		uint16_t temperature_dk;
		uint8_t soc_percent;
		int rc;

		rc = mbs_power_fuel_gauge_get(&voltage_mv, &soc_percent, &temperature_dk);
		ARG_UNUSED(voltage_mv);
		ARG_UNUSED(temperature_dk);
		if (rc == 0) {
			desktop->dashboard_header_battery_valid = true;
			desktop->dashboard_header_battery_soc = MIN(soc_percent, 100U);
		}
		desktop->dashboard_header_battery_charging = mbs_power_is_charging();
		desktop->dashboard_header_battery_online = mbs_power_is_online();
	}
#endif

	desktop->dashboard_header_bluetooth_enabled = false;
#if defined(CONFIG_MBS_BLUETOOTH)
	{
		mbs_bluetooth_config cfg;

		if (mbs_bluetooth_config_get(&cfg) == 0 && cfg.enabled) {
			desktop->dashboard_header_bluetooth_enabled = true;
		}
	}
#endif

	desktop->dashboard_header_buzzer_enabled = false;
#if defined(CONFIG_MBS_INDICATOR)
	{
		mbs_indicator_config cfg;

		if (mbs_indicator_config_get(&cfg) == 0 && cfg.buzzer_enabled) {
			desktop->dashboard_header_buzzer_enabled = true;
		}
	}
#endif

	desktop->dashboard_header_messages_unread =
		desktop_messages_cache_unread_received_count() > 0U;

	desktop->dashboard_header_gnss_enabled = false;
#if defined(CONFIG_MBS_GNSS)
	{
		mbs_gnss_config cfg;

		if (mbs_gnss_config_get(&cfg) == 0 && cfg.enabled) {
			desktop->dashboard_header_gnss_enabled = true;
		}
	}
#endif

	desktop->dashboard_header_radio_enabled = false;
#if defined(CONFIG_MBS_RADIO)
	{
		mbs_radio_config cfg;

		if (mbs_radio_config_get(&cfg) == 0 && cfg.enabled) {
			desktop->dashboard_header_radio_enabled = true;
		}
	}
#endif

	desktop->dashboard_header_storage_mounted = false;
#if defined(CONFIG_FILE_SYSTEM)
	{
		struct fs_statvfs stat;

		desktop->dashboard_header_storage_mounted =
			fs_statvfs(DASHBOARD_STORAGE_MOUNT_POINT, &stat) == 0;
	}
#endif
}

static bool desktop_dashboard_header_poll(struct zui_desktop *desktop)
{
	bool power_dirty;
	uint32_t now_ms;

	if (desktop == NULL) {
		return false;
	}

	power_dirty = atomic_cas(&desktop->dashboard_header_power_dirty, 1, 0);
	now_ms = k_uptime_get_32();
	if (!power_dirty && desktop->dashboard_header_status_armed &&
	    (int32_t)(now_ms - desktop->dashboard_header_status_deadline_ms) < 0) {
		return false;
	}

	desktop_dashboard_header_refresh(desktop);
	desktop->dashboard_header_status_deadline_ms = now_ms + DASHBOARD_HEADER_STATUS_PERIOD_MS;
	desktop->dashboard_header_status_armed = true;
	return true;
}

static void desktop_draw_header_time(struct zui_draw_ctx *draw,
				     const struct zui_desktop *desktop)
{
	const char *time_str = "--:--";

	if (desktop != NULL && desktop->dashboard_header_time[0] != '\0') {
		time_str = desktop->dashboard_header_time;
	}
	zui_draw_text_aligned(draw, (struct zui_point){.x = (int16_t)zui_draw_width(draw) - 2,
						       .y = 10},
			      ZUI_ALIGN_RIGHT, ZUI_ALIGN_BOTTOM, time_str);
}

static void desktop_draw_header_battery(struct zui_draw_ctx *draw, const struct zui_desktop *desktop)
{
	uint8_t fill_percent;
	uint8_t fill_w = 0U;
	bool charging;
	bool valid;

	zui_draw_icon(draw, (struct zui_point){.x = DASHBOARD_HEADER_BATTERY_X,
					       .y = DASHBOARD_HEADER_BATTERY_Y},
		      &I_battery_14x8);

	if (desktop == NULL) {
		return;
	}

	valid = desktop->dashboard_header_battery_valid;
	charging = desktop->dashboard_header_battery_charging ||
		   desktop->dashboard_header_battery_online;
	if (!valid && !charging) {
		return;
	}

	fill_percent = valid ? desktop->dashboard_header_battery_soc : 0U;
	if (charging && fill_percent < 100U) {
		uint32_t phase = k_uptime_get_32() % DASHBOARD_HEADER_BATTERY_ANIM_MS;
		uint32_t span = 100U - fill_percent;

		fill_percent +=
			(uint8_t)MIN(span, (phase * (span + 1U)) /
					   DASHBOARD_HEADER_BATTERY_ANIM_MS);
	}

	fill_w = (uint8_t)((fill_percent * DASHBOARD_HEADER_BATTERY_FILL_W + 99U) / 100U);
	fill_w = MIN(fill_w, DASHBOARD_HEADER_BATTERY_FILL_W);
	if (fill_w > 0U) {
		zui_draw_box(draw,
			     &(struct zui_rect){
				     .x = DASHBOARD_HEADER_BATTERY_X +
					  DASHBOARD_HEADER_BATTERY_FILL_X,
				     .y = DASHBOARD_HEADER_BATTERY_Y +
					  DASHBOARD_HEADER_BATTERY_FILL_Y,
				     .width = fill_w,
				     .height = DASHBOARD_HEADER_BATTERY_FILL_H});
	}
}

static int16_t desktop_header_slot_icon_x(int16_t slot_x, uint8_t icon_w)
{
	return slot_x + (int16_t)((DASHBOARD_HEADER_STATUS_SLOT_W - icon_w) / 2U);
}

static void desktop_draw_header_slot_icon(struct zui_draw_ctx *draw, int16_t slot_x,
					  const struct zui_icon *icon, uint8_t icon_w)
{
	zui_draw_icon(draw, (struct zui_point){.x = desktop_header_slot_icon_x(slot_x, icon_w),
					       .y = DASHBOARD_HEADER_STATUS_Y},
		      icon);
}

static bool desktop_draw_header_buzzer(struct zui_draw_ctx *draw,
				       const struct zui_desktop *desktop, int16_t slot_x)
{
	const struct zui_icon *icon = &I_mute_7x8;

	if (desktop != NULL && desktop->dashboard_header_buzzer_enabled) {
		icon = &I_sound_7x8;
	}

	desktop_draw_header_slot_icon(draw, slot_x, icon, 7U);
	return true;
}

static bool desktop_draw_header_messages(struct zui_draw_ctx *draw,
					 const struct zui_desktop *desktop, int16_t slot_x)
{
	if (desktop == NULL || !desktop->dashboard_header_messages_unread) {
		return false;
	}

	desktop_draw_header_slot_icon(draw, slot_x, &I_message_8x8, 8U);
	return true;
}

static bool desktop_draw_header_gnss(struct zui_draw_ctx *draw, const struct zui_desktop *desktop,
				     int16_t slot_x)
{
	if (desktop == NULL || !desktop->dashboard_header_gnss_enabled) {
		return false;
	}

	desktop_draw_header_slot_icon(draw, slot_x, &I_gnss_8x8, 8U);
	return true;
}

static bool desktop_draw_header_bluetooth(struct zui_draw_ctx *draw,
					  const struct zui_desktop *desktop, int16_t slot_x)
{
	if (desktop == NULL || !desktop->dashboard_header_bluetooth_enabled) {
		return false;
	}

	desktop_draw_header_slot_icon(draw, slot_x, &I_bluetooth_7x8, 7U);
	return true;
}

static bool desktop_draw_header_radio(struct zui_draw_ctx *draw, const struct zui_desktop *desktop,
				      int16_t slot_x)
{
	if (desktop == NULL || !desktop->dashboard_header_radio_enabled) {
		return false;
	}

	desktop_draw_header_slot_icon(draw, slot_x, &I_radio_7x8, 7U);
	return true;
}

static bool desktop_draw_header_storage(struct zui_draw_ctx *draw,
					const struct zui_desktop *desktop, int16_t slot_x)
{
	if (desktop == NULL || !desktop->dashboard_header_storage_mounted) {
		return false;
	}

	desktop_draw_header_slot_icon(draw, slot_x, &I_store_8x8, 8U);
	return true;
}

static void desktop_draw_header(struct zui_draw_ctx *draw, const struct zui_desktop *desktop)
{
	int16_t status_x = DASHBOARD_HEADER_STATUS_START_X;

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_box(draw, &(struct zui_rect){.x = 0,
					      .y = 0,
					      .width = zui_draw_width(draw),
					      .height = MBS_DESKTOP_DASHBOARD_HEADER_HEIGHT});
	zui_draw_set_color(draw, ZUI_COLOR_WHITE);
	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	desktop_draw_header_battery(draw, desktop);
	if (desktop_draw_header_buzzer(draw, desktop, status_x)) {
		status_x += DASHBOARD_HEADER_STATUS_STEP_X;
	}
	if (desktop_draw_header_messages(draw, desktop, status_x)) {
		status_x += DASHBOARD_HEADER_STATUS_STEP_X;
	}
	if (desktop_draw_header_gnss(draw, desktop, status_x)) {
		status_x += DASHBOARD_HEADER_STATUS_STEP_X;
	}
	if (desktop_draw_header_bluetooth(draw, desktop, status_x)) {
		status_x += DASHBOARD_HEADER_STATUS_STEP_X;
	}
	if (desktop_draw_header_radio(draw, desktop, status_x)) {
		status_x += DASHBOARD_HEADER_STATUS_STEP_X;
	}
	(void)desktop_draw_header_storage(draw, desktop, status_x);
	desktop_draw_header_time(draw, desktop);
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
}

static void desktop_draw_dashboard_tab(struct zui_draw_ctx *draw, const char *title, size_t count,
				       size_t selected, uint8_t anim_offset)
{
	const uint16_t width = zui_draw_width(draw);
	const int16_t y0 = (int16_t)zui_draw_height(draw) -
			   MBS_DESKTOP_DASHBOARD_TAB_HEIGHT + (int16_t)anim_offset;
	const struct zui_asset_pack *assets = zui_asset_pack_default();
	const struct zui_icon *left_icon =
		zui_asset_pack_icon_by_id(assets, ZUI_ASSET_ICON_BUTTON_LEFT);
	const struct zui_icon *right_icon =
		zui_asset_pack_icon_by_id(assets, ZUI_ASSET_ICON_BUTTON_RIGHT);
	char label[40];
	uint16_t title_w;
	uint16_t left_icon_w = 4U;
	uint16_t right_icon_w = 4U;
	int16_t frame_w;
	int16_t frame_left;
	int16_t frame_right;
	int16_t left_arrow_x;
	int16_t right_arrow_x;
	int16_t title_left;
	int16_t title_right;
	int16_t title_x;
	int16_t title_y;
	int16_t top_y;
	int16_t corner_y;
	int16_t side_y1;
	int16_t side_y2;

	(void)snprintk(label, sizeof(label), "%s", title != NULL ? title : DESKTOP_TEXT_COMMON_EMPTY);
	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	title_w = zui_draw_text_width(draw, label);

	frame_w = (int16_t)title_w;
	frame_w += (int16_t)left_icon_w + (int16_t)right_icon_w;
	frame_w += MBS_DESKTOP_DASHBOARD_TAB_FRAME_LEFT_PAD +
		   MBS_DESKTOP_DASHBOARD_TAB_FRAME_RIGHT_PAD;
	frame_w += 2 * MBS_DESKTOP_DASHBOARD_TAB_TITLE_GAP_PX;
	frame_w += 1;

	frame_w = (int16_t)MAX((uint16_t)MBS_DESKTOP_DASHBOARD_TAB_FRAME_MIN_W,
			       (uint16_t)frame_w);
	frame_w = MIN(frame_w, (int16_t)width - 4);
	frame_left = ((int16_t)width - frame_w) / 2;
	frame_right = frame_left + frame_w - 1;
	left_arrow_x = frame_left + MBS_DESKTOP_DASHBOARD_TAB_FRAME_LEFT_PAD;
	right_arrow_x = frame_right - MBS_DESKTOP_DASHBOARD_TAB_FRAME_RIGHT_PAD -
			(int16_t)right_icon_w;
	title_left = left_arrow_x + (int16_t)left_icon_w +
		     MBS_DESKTOP_DASHBOARD_TAB_TITLE_GAP_PX;
	title_right = right_arrow_x - MBS_DESKTOP_DASHBOARD_TAB_TITLE_GAP_PX - 1;
	if (title_right < title_left) {
		title_right = title_left;
	}
	title_x = title_left + (title_right - title_left + 1 - (int16_t)title_w) / 2;
	if (title_x < title_left) {
		title_x = title_left;
	}
	title_y = y0 + 10;
	top_y = y0;
	corner_y = y0 + 1;
	side_y1 = y0 + 2;
	side_y2 = y0 + MBS_DESKTOP_DASHBOARD_TAB_HEIGHT - 1;

	zui_draw_set_color(draw, ZUI_COLOR_WHITE);
	zui_draw_round_box(draw, &(struct zui_rect){.x = frame_left,
						    .y = y0,
						    .width = (uint16_t)frame_w,
						    .height = MBS_DESKTOP_DASHBOARD_TAB_HEIGHT + 2U},
			   4);
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_box(draw, &(struct zui_rect){.x = frame_left,
					      .y = side_y1,
					      .width = 2,
					      .height = (uint16_t)(side_y2 - side_y1 + 1)});
	zui_draw_line(draw, (struct zui_point){.x = frame_left + 1, .y = corner_y},
		      (struct zui_point){.x = frame_left + 2, .y = corner_y});
	zui_draw_line(draw, (struct zui_point){.x = frame_right, .y = side_y1},
		      (struct zui_point){.x = frame_right, .y = side_y2});
	zui_draw_dot(draw, (struct zui_point){.x = frame_right - 1, .y = corner_y});
	zui_draw_line(draw, (struct zui_point){.x = frame_left + 3, .y = top_y},
		      (struct zui_point){.x = frame_right - 2, .y = top_y});
	zui_draw_icon(draw, (struct zui_point){.x = left_arrow_x, .y = y0 + 3}, left_icon);
	zui_draw_icon(draw, (struct zui_point){.x = right_arrow_x, .y = y0 + 3}, right_icon);
	zui_draw_text(draw, (struct zui_point){.x = title_x, .y = title_y}, label);

	ARG_UNUSED(count);
	ARG_UNUSED(selected);
}

static void desktop_dashboard_tab_anim_kick(struct zui_desktop *desktop)
{
	if (desktop == NULL) {
		return;
	}

	desktop->dashboard_tab_anim_deadline_ms = k_uptime_get_32();
	desktop->dashboard_tab_anim_armed = true;
	k_sem_give(&desktop->redraw_sem);
}

static void desktop_dashboard_tab_autohide_arm(struct zui_desktop *desktop)
{
	if (desktop == NULL) {
		return;
	}

	desktop->dashboard_tab_autohide_deadline_ms =
		k_uptime_get_32() + DASHBOARD_TAB_AUTOHIDE_DELAY_MS;
	desktop->dashboard_tab_autohide_armed = true;
}

static void desktop_dashboard_tab_wake(struct zui_desktop *desktop)
{
	if (desktop == NULL) {
		return;
	}

	desktop->dashboard_tab_anim_target = 0U;
	if (desktop->dashboard_tab_anim_offset != desktop->dashboard_tab_anim_target) {
		desktop_dashboard_tab_anim_kick(desktop);
	}
	desktop_dashboard_tab_autohide_arm(desktop);
}

static void desktop_dashboard_tab_init(struct zui_desktop *desktop)
{
	if (desktop == NULL || desktop->dashboard_tab_initialized) {
		return;
	}

	desktop->dashboard_tab_anim_offset = MBS_DESKTOP_DASHBOARD_TAB_HIDDEN_OFFSET;
	desktop->dashboard_tab_anim_target = 0U;
	desktop->dashboard_tab_initialized = true;
	desktop_dashboard_tab_anim_kick(desktop);
	desktop_dashboard_tab_autohide_arm(desktop);
}

void desktop_dashboard_tab_poll(struct zui_desktop *desktop)
{
	uint32_t now_ms;
	uint8_t cur;
	uint8_t tgt;

	if (desktop == NULL || desktop->router == NULL ||
	    zui_router_current(desktop->router) != MBS_DESKTOP_VIEW_DASHBOARD) {
		return;
	}

	now_ms = k_uptime_get_32();
	if (desktop->dashboard_tab_autohide_armed &&
	    (int32_t)(now_ms - desktop->dashboard_tab_autohide_deadline_ms) >= 0) {
		desktop->dashboard_tab_autohide_armed = false;
		desktop->dashboard_tab_anim_target = MBS_DESKTOP_DASHBOARD_TAB_HIDDEN_OFFSET;
		if (desktop->dashboard_tab_anim_offset != desktop->dashboard_tab_anim_target) {
			desktop_dashboard_tab_anim_kick(desktop);
		}
	}

	if (!desktop->dashboard_tab_anim_armed ||
	    (int32_t)(now_ms - desktop->dashboard_tab_anim_deadline_ms) < 0) {
		return;
	}

	cur = desktop->dashboard_tab_anim_offset;
	tgt = desktop->dashboard_tab_anim_target;
	if (cur < tgt) {
		desktop->dashboard_tab_anim_offset =
			(uint8_t)MIN((uint32_t)tgt, (uint32_t)cur + DASHBOARD_TAB_ANIM_STEP_PX);
	} else if (cur > tgt) {
		desktop->dashboard_tab_anim_offset =
			(uint8_t)MAX((int32_t)tgt, (int32_t)cur - DASHBOARD_TAB_ANIM_STEP_PX);
	}

	desktop->dashboard_tab_anim_armed = (desktop->dashboard_tab_anim_offset != tgt);
	if (desktop->dashboard_tab_anim_armed) {
		desktop->dashboard_tab_anim_deadline_ms = now_ms + DASHBOARD_TAB_ANIM_PERIOD_MS;
	}
	zui_desktop_request_redraw(desktop);
}

uint32_t desktop_dashboard_draw_timeout_ms(struct zui_desktop *desktop)
{
	uint32_t delay_ms = MBS_DESKTOP_DRAW_PERIOD_MS;
	int32_t tick_remaining_ms;

	if (desktop != NULL && desktop->dashboard_tab_anim_armed) {
		delay_ms = MIN(delay_ms, DASHBOARD_TAB_ANIM_PERIOD_MS);
	}
	if (desktop != NULL && desktop->dashboard_tick_armed) {
		tick_remaining_ms = (int32_t)(desktop->dashboard_tick_deadline_ms -
					      k_uptime_get_32());
		if (tick_remaining_ms <= 0) {
			return 0U;
		}
		delay_ms = MIN(delay_ms, (uint32_t)tick_remaining_ms);
	}

	return delay_ms;
}

static const char *desktop_dashboard_active_title(struct zui_desktop *desktop)
{
	struct mbs_desktop_dashboard_widget *wctx;
	const struct mbs_desktop_dashboard_widget_desc *desc;
	size_t count;

	count = mbs_desktop_dashboard_widgets_count();
	if (count == 0U) {
		return DESKTOP_TEXT_COMMON_NO_DATA;
	}
	if (desktop->dashboard_active_idx >= count) {
		desktop->dashboard_active_idx = 0U;
	}

	wctx = mbs_desktop_dashboard_widgets_get(desktop->dashboard_active_idx);
	desc = wctx != NULL ? wctx->desc : NULL;
	return desc != NULL && desc->title != NULL ? desc->title : DESKTOP_TEXT_COMMON_NO_DATA;
}

static struct mbs_desktop_dashboard_widget *
desktop_dashboard_active_widget(struct zui_desktop *desktop)
{
	size_t count;

	if (desktop == NULL) {
		return NULL;
	}

	count = mbs_desktop_dashboard_widgets_count();
	if (count == 0U) {
		desktop->dashboard_active_idx = 0U;
		return NULL;
	}
	if (desktop->dashboard_active_idx >= count) {
		desktop->dashboard_active_idx = 0U;
	}

	return mbs_desktop_dashboard_widgets_get(desktop->dashboard_active_idx);
}

static struct zui_screen *desktop_dashboard_create_active_screen(struct zui_desktop *desktop)
{
	struct mbs_desktop_dashboard_widget *wctx;
	const struct mbs_desktop_dashboard_widget_desc *desc;

	wctx = desktop_dashboard_active_widget(desktop);
	desc = wctx != NULL ? wctx->desc : NULL;
	if (desc == NULL || desc->screen_create == NULL) {
		return NULL;
	}

	return desc->screen_create(wctx);
}

static void desktop_dashboard_sync_active_screen(struct zui_desktop *desktop);

static bool desktop_dashboard_submit_active_input(struct zui_desktop *desktop,
						  const struct zui_input_event *event)
{
	int ret;

	if (desktop == NULL || event == NULL) {
		return false;
	}

	desktop_dashboard_sync_active_screen(desktop);
	if (desktop->dashboard_active_screen == NULL) {
		return false;
	}

	ret = zui_screen_submit_input(desktop->dashboard_active_screen, event);
	if (ret > 0) {
		zui_desktop_request_redraw(desktop);
		return true;
	}

	return false;
}

static void desktop_dashboard_sync_active_screen(struct zui_desktop *desktop)
{
	struct zui_screen *screen;
	bool home_active;

	if (desktop == NULL) {
		return;
	}

	screen = desktop_dashboard_create_active_screen(desktop);
	if (screen == desktop->dashboard_active_screen) {
		return;
	}

	home_active = desktop->router != NULL &&
		      zui_router_current(desktop->router) == MBS_DESKTOP_VIEW_DASHBOARD;
	if (home_active && desktop->dashboard_active_screen != NULL) {
		(void)zui_screen_exit(desktop->dashboard_active_screen);
	}

	desktop->dashboard_active_screen = screen;
	desktop->dashboard_tick_armed = false;

	if (home_active && desktop->dashboard_active_screen != NULL) {
		(void)zui_screen_enter(desktop->dashboard_active_screen);
	}
}

void desktop_dashboard_poll(struct zui_desktop *desktop)
{
	struct mbs_desktop_dashboard_widget *wctx;
	const struct mbs_desktop_dashboard_widget_desc *desc;
	uint32_t now_ms;
	uint32_t delay_ms;
	bool header_changed;

	if (desktop == NULL || desktop->router == NULL ||
	    !zui_host_layer_is_enabled(desktop->host, ZUI_LAYER_DESKTOP) ||
	    zui_router_current(desktop->router) != MBS_DESKTOP_VIEW_DASHBOARD ||
	    desktop->dashboard_paused_for_sleep) {
		return;
	}

	header_changed = desktop_dashboard_header_poll(desktop);
	desktop_dashboard_sync_active_screen(desktop);
	wctx = desktop_dashboard_active_widget(desktop);
	desc = wctx != NULL ? wctx->desc : NULL;
	if (desc == NULL || desc->tick == NULL) {
		if (header_changed) {
			zui_desktop_request_redraw(desktop);
		}
		return;
	}

	now_ms = k_uptime_get_32();
	if (desktop->dashboard_tick_armed &&
	    (int32_t)(now_ms - desktop->dashboard_tick_deadline_ms) < 0) {
		if (header_changed) {
			zui_desktop_request_redraw(desktop);
		}
		return;
	}

	delay_ms = desc->tick(wctx);
	if (delay_ms < MBS_DESKTOP_DASHBOARD_TICK_MIN_MS) {
		delay_ms = MBS_DESKTOP_DASHBOARD_TICK_MIN_MS;
	}

	desktop->dashboard_tick_deadline_ms = now_ms + delay_ms;
	desktop->dashboard_tick_armed = true;
	if (desktop->dashboard_active_screen != NULL) {
		(void)zui_screen_request_redraw(desktop->dashboard_active_screen);
	}
	if (header_changed) {
		zui_desktop_request_redraw(desktop);
	}
}

static void desktop_home_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct zui_desktop *desktop = user_data;
	size_t count;
	const char *title;

	zui_draw_reset(draw);
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	desktop_draw_header(draw, desktop);

	count = mbs_desktop_dashboard_widgets_count();
	title = desktop_dashboard_active_title(desktop);

	zui_draw_set_color(draw, ZUI_COLOR_WHITE);
	zui_draw_box(draw, &(struct zui_rect){.x = 0,
					      .y = MBS_DESKTOP_DASHBOARD_HEADER_HEIGHT,
					      .width = zui_draw_width(draw),
					      .height = MBS_DESKTOP_DASHBOARD_WIDGETS_HEIGHT});
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_bitmap(draw, (struct zui_point){.x = 0,
						 .y = MBS_DESKTOP_DASHBOARD_HEADER_HEIGHT},
			2, 2, ZUI_BITMAP_FORMAT_XBM, B_frame_left_shadow_2x2);
	zui_draw_bitmap(draw, (struct zui_point){.x = (int16_t)zui_draw_width(draw) - 2,
						 .y = MBS_DESKTOP_DASHBOARD_HEADER_HEIGHT},
			2, 2, ZUI_BITMAP_FORMAT_XBM, B_frame_right_shadow_2x2);

	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	if (count == 0U) {
		zui_draw_text_aligned(draw,
				      (struct zui_point){.x = (int16_t)zui_draw_width(draw) / 2,
							 .y = 38},
				      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER,
				      DESKTOP_TEXT_COMMON_NO_DATA);
	} else {
		desktop_dashboard_sync_active_screen(desktop);
		if (desktop->dashboard_active_screen != NULL) {
			(void)zui_screen_draw(desktop->dashboard_active_screen, draw);
		}
	}

	desktop_draw_dashboard_tab(draw, title, count, desktop->dashboard_active_idx,
				   desktop->dashboard_tab_anim_offset);
}

static bool desktop_home_input(const struct zui_input_event *event, void *user_data)
{
	struct zui_desktop *desktop = user_data;
	size_t count;

	if (event == NULL || desktop == NULL) {
		return false;
	}

	if (desktop_input_is_long(event) && event->code == ZUI_INPUT_CODE_BACK) {
		(void)zui_desktop_switch(desktop, MBS_DESKTOP_VIEW_POWER_MENU);
		return true;
	}

	if (!desktop_input_is_click(event)) {
		return desktop_dashboard_submit_active_input(desktop, event);
	}

	count = mbs_desktop_dashboard_widgets_count();
	if (event->code == ZUI_INPUT_CODE_BACK) {
		(void)zui_desktop_switch(desktop, MBS_DESKTOP_VIEW_MAIN_MENU);
		return true;
	}
	if (event->code == ZUI_INPUT_CODE_LEFT && count > 0U) {
		desktop->dashboard_active_idx =
			desktop->dashboard_active_idx > 0U ? desktop->dashboard_active_idx - 1U :
							     (uint32_t)count - 1U;
		desktop_dashboard_sync_active_screen(desktop);
		desktop_dashboard_tab_wake(desktop);
		zui_desktop_request_redraw(desktop);
		return true;
	}
	if (event->code == ZUI_INPUT_CODE_RIGHT && count > 0U) {
		desktop->dashboard_active_idx = (desktop->dashboard_active_idx + 1U) % count;
		desktop_dashboard_sync_active_screen(desktop);
		desktop_dashboard_tab_wake(desktop);
		zui_desktop_request_redraw(desktop);
		return true;
	}
	if (event->code == ZUI_INPUT_CODE_SELECT && count > 0U) {
		struct mbs_desktop_dashboard_widget *wctx;
		const struct mbs_desktop_dashboard_widget_desc *desc;

		wctx = mbs_desktop_dashboard_widgets_get(desktop->dashboard_active_idx);
		desc = wctx != NULL ? wctx->desc : NULL;
		if (desc != NULL && desc->app_id != NULL) {
			mbs_desktop_app_handle_t handle =
				mbs_desktop_app_registry_handle_from_id(desc->app_id);

			if (handle != MBS_DESKTOP_APP_HANDLE_INVALID) {
				return desktop_open_app(desktop, handle,
							MBS_DESKTOP_VIEW_DASHBOARD);
			}
		}
		return true;
	}

	return desktop_dashboard_submit_active_input(desktop, event);
}

static void desktop_home_enter(void *user_data)
{
	struct zui_desktop *desktop = user_data;

	LOG_DBG("Desktop HOME enter");
	desktop->dashboard_paused_for_sleep = false;
	desktop_dashboard_header_refresh(desktop);
	atomic_set(&desktop->dashboard_header_power_dirty, 0);
	desktop->dashboard_header_status_deadline_ms =
		k_uptime_get_32() + DASHBOARD_HEADER_STATUS_PERIOD_MS;
	desktop->dashboard_header_status_armed = true;
	desktop_dashboard_tab_init(desktop);
	desktop_dashboard_tab_wake(desktop);
	desktop_dashboard_sync_active_screen(desktop);
	if (desktop->dashboard_active_screen != NULL) {
		(void)zui_screen_enter(desktop->dashboard_active_screen);
	}
	zui_desktop_request_redraw(desktop);
}

static void desktop_home_exit(void *user_data)
{
	struct zui_desktop *desktop = user_data;

	if (desktop == NULL) {
		return;
	}

	if (desktop->dashboard_active_screen != NULL) {
		(void)zui_screen_exit(desktop->dashboard_active_screen);
	}
	desktop->dashboard_tick_armed = false;
	desktop->dashboard_header_status_armed = false;
	desktop->dashboard_tab_autohide_armed = false;
	desktop->dashboard_tab_anim_armed = false;
}

static bool desktop_home_event(const struct zui_screen_event *event, void *user_data)
{
	struct zui_desktop *desktop = user_data;

	if (event == NULL || desktop == NULL) {
		return false;
	}

	if (event->type == ZUI_SCREEN_EVENT_BACK) {
		(void)zui_desktop_switch(desktop, MBS_DESKTOP_VIEW_MAIN_MENU);
		return true;
	}

	return false;
}

static const struct zui_screen_ops desktop_home_ops = {
	.draw = desktop_home_draw,
	.input = desktop_home_input,
	.enter = desktop_home_enter,
	.exit = desktop_home_exit,
	.event = desktop_home_event,
};

int desktop_dashboard_init(struct zui_desktop *desktop)
{
	if (desktop == NULL) {
		return -EINVAL;
	}

	desktop->home_screen = zui_screen_create(&desktop_home_ops, desktop);
	if (desktop->home_screen == NULL) {
		LOG_ERR("Failed to allocate Desktop dashboard screen");
		return -ENOMEM;
	}

	return zui_desktop_register_screen(desktop, MBS_DESKTOP_VIEW_DASHBOARD,
					   desktop->home_screen);
}

void desktop_dashboard_deinit(struct zui_desktop *desktop)
{
	if (desktop == NULL) {
		return;
	}

	desktop->dashboard_tick_armed = false;
	desktop->dashboard_header_status_armed = false;
	desktop->dashboard_tab_autohide_armed = false;
	desktop->dashboard_tab_anim_armed = false;
	desktop->dashboard_active_screen = NULL;

	if (desktop->router != NULL) {
		(void)zui_router_unregister_screen(desktop->router, MBS_DESKTOP_VIEW_DASHBOARD);
	}
	if (desktop->home_screen != NULL) {
		zui_screen_destroy(desktop->home_screen);
		desktop->home_screen = NULL;
	}
}
