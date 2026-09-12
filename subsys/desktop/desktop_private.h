/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include <desktop/desktop.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/zui/input.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zui_host;
struct zui_router;
struct zui_screen;
struct zui_draw_ctx;
struct zui_list;
struct zui_actions;
struct zui_file_picker;
struct zui_list_item;
struct zui_icon_anim;

struct zui_desktop_dashboard;
struct zui_desktop_menu;
struct zui_desktop_power_menu;
struct mbs_desktop_app_desc;

#if IS_ENABLED(CONFIG_MBS_DESKTOP_PERF_LOG)
struct zui_desktop_perf_stats {
	uint32_t interval_start_ms;
	uint32_t loops;
	uint32_t poll_calls;
	uint32_t poll_dispatches;
	uint32_t draw_calls;
	uint32_t max_loop_us;
	uint32_t max_poll_us;
	uint32_t max_draw_us;
	uint32_t max_present_us;
	uint64_t loop_us_total;
	uint64_t poll_us_total;
	uint64_t draw_us_total;
	uint64_t present_us_total;
};
#endif

#ifndef CONFIG_ZUI_DISPLAY_WIDTH
#define MBS_DESKTOP_DISPLAY_WIDTH 128
#else
#define MBS_DESKTOP_DISPLAY_WIDTH CONFIG_ZUI_DISPLAY_WIDTH
#endif

#ifndef CONFIG_ZUI_DISPLAY_HEIGHT
#define MBS_DESKTOP_DISPLAY_HEIGHT 64
#else
#define MBS_DESKTOP_DISPLAY_HEIGHT CONFIG_ZUI_DISPLAY_HEIGHT
#endif

#define MBS_DESKTOP_DASHBOARD_TAB_HEIGHT 11
#define MBS_DESKTOP_DASHBOARD_HEADER_HEIGHT 12

/* Dashboard tab (bottom selector bar) layout */
#define MBS_DESKTOP_DASHBOARD_TAB_VISIBLE_HEIGHT_PX 2
#define MBS_DESKTOP_DASHBOARD_TAB_HIDDEN_OFFSET \
	(MBS_DESKTOP_DASHBOARD_TAB_HEIGHT - MBS_DESKTOP_DASHBOARD_TAB_VISIBLE_HEIGHT_PX)

#define MBS_DESKTOP_DASHBOARD_TAB_FRAME_MIN_W    64
#define MBS_DESKTOP_DASHBOARD_TAB_FRAME_MARGIN_X 2

#define MBS_DESKTOP_DASHBOARD_TAB_FRAME_LEFT_PAD  3
#define MBS_DESKTOP_DASHBOARD_TAB_FRAME_RIGHT_PAD 1
#define MBS_DESKTOP_DASHBOARD_TAB_TITLE_GAP_PX    6
#define MBS_DESKTOP_DASHBOARD_WIDGETS_HEIGHT \
	(MBS_DESKTOP_DISPLAY_HEIGHT - MBS_DESKTOP_DASHBOARD_HEADER_HEIGHT)
#define MBS_DESKTOP_DASHBOARD_WIDGETS_CONTENT_HEIGHT \
	(MBS_DESKTOP_DISPLAY_HEIGHT - MBS_DESKTOP_DASHBOARD_HEADER_HEIGHT - \
	 MBS_DESKTOP_DASHBOARD_TAB_HEIGHT)
#define MBS_DESKTOP_DASHBOARD_WIDGETS_START_X 0
#define MBS_DESKTOP_DASHBOARD_WIDGETS_START_Y MBS_DESKTOP_DASHBOARD_HEADER_HEIGHT
#define MBS_DESKTOP_DRAW_PERIOD_MS	     250U
#define MBS_DESKTOP_DASHBOARD_TAB_ANIM_PERIOD_MS 16U

/*
 * Internal desktop context.
 *
 * This header is intentionally not installed; it is shared between desktop
 * core, registry and scene implementations.
 */
struct zui_desktop {
	struct zui_host *host;
	struct zui_router *router;
	struct zui_screen *home_screen;
	struct zui_screen *main_menu_screen;
	struct zui_screen *launcher_screen;
	struct zui_screen *power_menu_screen;
	struct zui_list *main_menu;
	struct zui_file_picker *launcher_picker;
	struct zui_actions *power_menu;
	struct zui_draw_ctx *draw;
	struct k_sem redraw_sem;
	atomic_t app_exit_pending;
	struct k_mutex input_mutex;
	struct k_msgq input_msgq;

	uint32_t draw_count;
	uint32_t input_count;
	char last_input_line[32];
	uint32_t dashboard_active_idx;
	uint32_t dashboard_tick_deadline_ms;
	uint32_t dashboard_header_status_deadline_ms;
	atomic_t dashboard_header_power_dirty;
	uint32_t dashboard_tab_autohide_deadline_ms;
	uint32_t dashboard_tab_anim_deadline_ms;
	uint32_t power_action_deadline_ms;
	uint32_t power_action_release_deadline_ms;
	uint32_t app_return_screen_id;
	struct zui_screen *dashboard_active_screen;
	struct zui_list_item *main_menu_items;
	struct zui_icon_anim **main_menu_icon_anims;
	size_t main_menu_item_count;
	struct zui_input_event input_events[CONFIG_ZUI_INPUT_QUEUE_SIZE];

	char dashboard_header_time[6];
	uint8_t dashboard_header_battery_soc;
	uint8_t dashboard_tab_anim_offset;
	uint8_t dashboard_tab_anim_target;
	bool dashboard_header_status_armed;
	bool dashboard_header_battery_valid;
	bool dashboard_header_battery_charging;
	bool dashboard_header_battery_online;
	bool dashboard_header_buzzer_enabled;
	bool dashboard_header_messages_unread;
	bool dashboard_header_gnss_enabled;
	bool dashboard_header_bluetooth_enabled;
	bool dashboard_header_radio_enabled;
	bool dashboard_header_storage_mounted;
	bool dashboard_tick_armed;
	bool dashboard_tab_autohide_armed;
	bool dashboard_tab_anim_armed;
	bool dashboard_tab_initialized;
	bool power_action_pending;
	bool ui_suspended_for_sleep;
	bool dashboard_paused_for_sleep;
	uint8_t power_action_item;

	mbs_desktop_app_handle_t active_app_handle;
	const struct mbs_desktop_app_desc *active_app;

#if IS_ENABLED(CONFIG_MBS_DESKTOP_PERF_LOG)
	struct zui_desktop_perf_stats perf;
#endif

	k_thread_stack_t *app_shared_stack;
	size_t app_shared_stack_size;
};

struct zui_desktop *zui_desktop_get_instance(void);
void zui_desktop_request_redraw(struct zui_desktop *desktop);
int zui_desktop_submit_input(struct zui_desktop *desktop, const struct zui_input_event *event);
void zui_desktop_request_app_exit(struct zui_desktop *desktop);
int zui_desktop_switch(struct zui_desktop *desktop, uint32_t screen_id);
int zui_desktop_register_screen(struct zui_desktop *desktop, uint32_t id,
				struct zui_screen *screen);

bool desktop_input_is_click(const struct zui_input_event *event);
bool desktop_input_is_long(const struct zui_input_event *event);
bool desktop_shell_should_consume_edge_event(const struct zui_input_event *event);

struct desktop_input_drop_counts {
	uint32_t press;
	uint32_t action;
	uint32_t release;
};

uint32_t desktop_input_pressed_mask_get(void);
void desktop_input_drop_counts_get(struct desktop_input_drop_counts *counts);
bool desktop_open_app(struct zui_desktop *desktop, mbs_desktop_app_handle_t handle,
		      uint32_t return_screen_id);
/* The app runtime owns each MBA Session, including failed reclamation.
 * A new launch retries retained resources before loading the selected path.
 */
int desktop_mba_start(struct zui_desktop *desktop, const char *path);
/* Joins a returning thread before reclaiming its session and restoring Desktop.
 * Join failure keeps the exit pending; MBA cleanup failure restores Desktop,
 * reports the error and retains the session until another explicit launch.
 */
int desktop_app_complete_exit(struct zui_desktop *desktop, k_timeout_t timeout);

int desktop_dashboard_init(struct zui_desktop *desktop);
void desktop_dashboard_deinit(struct zui_desktop *desktop);
void desktop_dashboard_poll(struct zui_desktop *desktop);
void desktop_dashboard_tab_poll(struct zui_desktop *desktop);
uint32_t desktop_dashboard_draw_timeout_ms(struct zui_desktop *desktop);

int desktop_main_menu_init(struct zui_desktop *desktop);
void desktop_main_menu_deinit(struct zui_desktop *desktop);

int desktop_launcher_init(struct zui_desktop *desktop);
void desktop_launcher_deinit(struct zui_desktop *desktop);
void desktop_launcher_show_cleanup_error(struct zui_desktop *desktop, int error);

int desktop_power_menu_init(struct zui_desktop *desktop);
void desktop_power_menu_deinit(struct zui_desktop *desktop);
void desktop_power_menu_poll(struct zui_desktop *desktop);

#ifdef __cplusplus
}
#endif
