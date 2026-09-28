/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <gnss/gnss.h>
#include <zui/zui.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GNSS_APP_SCREEN_MENU       1U
#define GNSS_APP_SCREEN_STATUS     2U
#define GNSS_APP_SCREEN_SATELLITES 3U
#define GNSS_APP_SCREEN_DETAIL     4U
#define GNSS_APP_SCREEN_SETTINGS   5U
#define GNSS_APP_SCREEN_NUMBER     6U
#define GNSS_APP_SCREEN_RESET      7U

#define GNSS_MENU_ITEM_STATUS      1U
#define GNSS_MENU_ITEM_SATELLITES  2U
#define GNSS_MENU_ITEM_ACQUISITION 3U
#define GNSS_MENU_ITEM_SETTINGS    4U

#define GNSS_SAT_ITEM_RELOAD       UINT32_MAX
#define GNSS_SAT_ITEM_PLACEHOLDER  (UINT32_MAX - 1U)

#define GNSS_TEXT_MAX              512U
#define GNSS_DETAIL_TEXT_MAX       160U
#define GNSS_DETAIL_HEADER_MAX     24U
#define GNSS_FORM_ITEMS_MAX        18U
#define GNSS_VALUE_BUF_COUNT       4U
#define GNSS_VALUE_BUF_SIZE        16U

#if defined(CONFIG_MBS_GNSS_SATELLITE_CACHE_SIZE) && \
    (CONFIG_MBS_GNSS_SATELLITE_CACHE_SIZE > 0)
#define GNSS_SATELLITES_MAX CONFIG_MBS_GNSS_SATELLITE_CACHE_SIZE
#else
#define GNSS_SATELLITES_MAX 32U
#endif

#if defined(CONFIG_MBS_GNSS_MIN_UPDATE_INTERVAL)
#define GNSS_UPDATE_INTERVAL_MIN CONFIG_MBS_GNSS_MIN_UPDATE_INTERVAL
#else
#define GNSS_UPDATE_INTERVAL_MIN 1000U
#endif

#if defined(CONFIG_MBS_GNSS_MAX_UPDATE_INTERVAL)
#define GNSS_UPDATE_INTERVAL_MAX CONFIG_MBS_GNSS_MAX_UPDATE_INTERVAL
#else
#define GNSS_UPDATE_INTERVAL_MAX 86400000U
#endif

#if defined(CONFIG_MBS_GNSS_MIN_ACTIVE_TIME)
#define GNSS_MIN_ACTIVE_TIME_MIN CONFIG_MBS_GNSS_MIN_ACTIVE_TIME
#else
#define GNSS_MIN_ACTIVE_TIME_MIN 1000U
#endif

#if defined(CONFIG_MBS_GNSS_MAX_ACTIVE_TIME)
#define GNSS_MIN_ACTIVE_TIME_MAX CONFIG_MBS_GNSS_MAX_ACTIVE_TIME
#else
#define GNSS_MIN_ACTIVE_TIME_MAX 86400000U
#endif

enum gnss_form_id {
    GNSS_FORM_ENABLED = 1,
    GNSS_FORM_UPDATE_INTERVAL,
    GNSS_FORM_MIN_ACTIVE_TIME,
    GNSS_FORM_NAV_MODE,
    GNSS_FORM_FIX_RATE,
    GNSS_FORM_SYSTEM_MODE,
    GNSS_FORM_SYSTEM_GPS,
    GNSS_FORM_SYSTEM_GLONASS,
    GNSS_FORM_SYSTEM_GALILEO,
    GNSS_FORM_SYSTEM_BEIDOU,
    GNSS_FORM_SYSTEM_QZSS,
    GNSS_FORM_SYSTEM_IRNSS,
    GNSS_FORM_SYSTEM_SBAS,
    GNSS_FORM_SYSTEM_IMES,
    GNSS_FORM_ELECTRONIC_COMPASS,
    GNSS_FORM_TIME_SYNC,
    GNSS_FORM_APPLY = 0xF0,
    GNSS_FORM_RESET = 0xF1,
};

enum gnss_number_field {
    GNSS_NUMBER_UPDATE_INTERVAL,
    GNSS_NUMBER_MIN_ACTIVE_TIME,
};

enum gnss_system_mode {
    GNSS_SYSTEM_MODE_ALL,
    GNSS_SYSTEM_MODE_CUSTOM,
};

struct gnss_settings_state {
    bool enabled;
    uint32_t update_interval_ms;
    uint32_t min_active_time_ms;
    uint8_t nav_mode_idx;
    uint8_t fix_rate_idx;
    uint8_t system_mode_idx;
    bool system_gps;
    bool system_glonass;
    bool system_galileo;
    bool system_beidou;
    bool system_qzss;
    bool system_irnss;
    bool system_sbas;
    bool system_imes;
    bool electronic_compass;
    bool time_sync;
};

struct gnss_satellite_item {
    uint8_t prn;
    uint8_t system;
    uint8_t snr;
    uint8_t elevation;
    uint16_t azimuth;
    bool tracked;
    bool corrected;
};

struct gnss_status_scratch {
    mbs_gnss_config cfg;
    struct gnss_info info;
    struct navigation_data nav;
    struct gnss_time time;
    char hdop[16];
    char tracked[16];
    char visible[16];
    char latitude[32];
    char longitude[32];
    char altitude[24];
    char speed[24];
    char bearing[24];
    char utc[40];
};

struct gnss_app {
    struct zui_host *host;
    struct zui_router *router;
    struct k_sem exit_sem;

    struct zui_sublist *menu;
    struct zui_sublist *satellites;
    struct zui_form *settings_form;
    struct zui_text_view *status_view;
    struct zui_number_editor *number_editor;
    struct zui_modal *reset_modal;

    struct zui_screen *menu_screen;
    struct zui_screen *status_screen;
    struct zui_screen *satellites_screen;
    struct zui_screen *detail_screen;
    struct zui_screen *settings_screen;
    struct zui_screen *number_screen;
    struct zui_screen *reset_screen;

    struct zui_list_item menu_items[4];
    struct zui_list_item sat_items[GNSS_SATELLITES_MAX + 1U];
    struct zui_form_item form_items[GNSS_FORM_ITEMS_MAX];
    char value_bufs[GNSS_VALUE_BUF_COUNT][GNSS_VALUE_BUF_SIZE];
    char sat_labels[GNSS_SATELLITES_MAX][20];
    size_t form_item_count;
    size_t sat_item_count;
    enum gnss_number_field number_field;

    struct gnss_settings_state applied;
    struct gnss_settings_state editing;
    struct gnss_status_scratch status_scratch;
    struct gnss_satellite sat_reload_scratch[GNSS_SATELLITES_MAX];
    struct gnss_satellite_item sat_data[GNSS_SATELLITES_MAX];
    uint16_t sat_count;
    uint16_t sat_selected_idx;
    int sat_last_rc;
    char status_text[GNSS_TEXT_MAX];
    char detail_header[GNSS_DETAIL_HEADER_MAX];
    char detail_text[GNSS_DETAIL_TEXT_MAX];
};

extern const struct zui_screen_ops gnss_menu_ops;
extern const struct zui_screen_ops gnss_status_ops;
extern const struct zui_screen_ops gnss_satellites_ops;
extern const struct zui_screen_ops gnss_detail_ops;
extern const struct zui_screen_ops gnss_settings_ops;
extern const struct zui_screen_ops gnss_number_ops;
extern const struct zui_screen_ops gnss_reset_ops;

void gnss_request_redraw(struct gnss_app *app);
bool gnss_is_click(const struct zui_input_event *event);
bool gnss_is_long(const struct zui_input_event *event);
bool gnss_should_consume_edge(const struct zui_input_event *event);
void gnss_switch(struct gnss_app *app, uint32_t screen_id);
void gnss_exit(struct gnss_app *app);
void gnss_toast(struct gnss_app *app, const char *text, const struct zui_icon *icon,
                uint32_t timeout_ms);
bool gnss_back_to_menu_input(const struct zui_input_event *event, struct gnss_app *app,
                             struct zui_screen *screen);

void gnss_open_status(struct gnss_app *app);

int gnss_satellites_reload(struct gnss_app *app);
void gnss_satellites_update_list(struct gnss_app *app);
void gnss_prepare_detail(struct gnss_app *app);
void gnss_open_satellites(struct gnss_app *app);
void gnss_satellites_selected(struct zui_sublist *list, uint32_t id, size_t index,
                              const struct zui_input_event *event, void *user_data);

void gnss_load_settings(struct gnss_app *app);
void gnss_form_refresh(struct gnss_app *app);
void gnss_open_settings(struct gnss_app *app);
void gnss_form_changed(struct zui_form *form, uint32_t id, size_t option_index,
                       void *user_data);
void gnss_form_activated(struct zui_form *form, uint32_t id,
                         const struct zui_input_event *event, void *user_data);
void gnss_number_submitted(struct zui_number_editor *editor, int64_t value,
                           void *user_data);
void gnss_reset_modal_result(struct zui_modal *modal, enum zui_modal_result result,
                             const struct zui_input_event *event, void *user_data);

void gnss_menu_selected(struct zui_sublist *list, uint32_t id, size_t index,
                        const struct zui_input_event *event, void *user_data);

#ifdef __cplusplus
}
#endif
