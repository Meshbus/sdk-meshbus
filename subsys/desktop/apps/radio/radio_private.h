/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "apps/app_common.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <radio/radio.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>
#include <zui/zui.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RADIO_SCREEN_MENU        1U
#define RADIO_SCREEN_PACKET      2U
#define RADIO_SCREEN_CAPTURE     3U
#define RADIO_SCREEN_CW          4U
#define RADIO_SCREEN_NOISE       5U
#define RADIO_SCREEN_SETTINGS    6U
#define RADIO_SCREEN_NUMBER      7U
#define RADIO_SCREEN_TEXT_EDITOR 8U
#define RADIO_SCREEN_HEX_EDITOR  9U
#define RADIO_SCREEN_RESET       10U

#define RADIO_MENU_ITEM_PACKET       1U
#define RADIO_MENU_ITEM_CW           2U
#define RADIO_MENU_ITEM_NOISE        3U
#define RADIO_MENU_ITEM_CALIBRATE_NF 4U
#define RADIO_MENU_ITEM_AGC_RESET    5U
#define RADIO_MENU_ITEM_SETTINGS     6U

#define RADIO_PACKET_ITEM_TEXT    1U
#define RADIO_PACKET_ITEM_HEX     2U
#define RADIO_PACKET_ITEM_CAPTURE 3U

#define RADIO_FORM_ENABLED          1U
#define RADIO_FORM_RX_ONLY          2U
#define RADIO_FORM_FREQUENCY        3U
#define RADIO_FORM_BANDWIDTH        4U
#define RADIO_FORM_DATA_RATE        5U
#define RADIO_FORM_CODING_RATE      6U
#define RADIO_FORM_PREAMBLE         7U
#define RADIO_FORM_TX_POWER         8U
#define RADIO_FORM_PACKET_CRC       9U
#define RADIO_FORM_RX_BOOSTED       10U
#define RADIO_FORM_DUTY_CYCLE       12U
#define RADIO_FORM_DUTY_RX_TIME     13U
#define RADIO_FORM_DUTY_SLEEP_TIME  14U
#define RADIO_FORM_APPLY            0xF0U
#define RADIO_FORM_RESET            0xF1U

#define RADIO_CW_FORM_TX_POWER  1U
#define RADIO_CW_FORM_FREQUENCY 2U
#define RADIO_CW_FORM_DURATION  3U
#define RADIO_CW_FORM_EXECUTE   4U

#define RADIO_FORM_ITEMS_MAX       18U
#define RADIO_VALUE_BUF_COUNT      18U
#define RADIO_VALUE_BUF_SIZE       24U
#define RADIO_NOISE_INTERVAL_MS    200U
#define RADIO_TICK_INTERVAL_MS     100U
#define RADIO_MENU_NF_CAL_THRESHOLD_DB 14
#define RADIO_NOISE_CAL_THRESHOLD_DB   14
#define RADIO_PACKET_CAPTURE_HEX_X 60
#define RADIO_PACKET_CAPTURE_HEX_Y 3
#define RADIO_PACKET_CAPTURE_HEX_W 62U
#define RADIO_PACKET_CAPTURE_HEX_H 47U
#define RADIO_PACKET_CAPTURE_HEX_SCROLL_X 126
#define RADIO_NOISE_ANALYZER_DBM_MIN CONFIG_MBS_DESKTOP_RADIO_NOISE_ANALYZER_DBM_MIN
#define RADIO_NOISE_ANALYZER_DBM_MAX CONFIG_MBS_DESKTOP_RADIO_NOISE_ANALYZER_DBM_MAX

#define RADIO_SEND_HEX_MAX_LEN 255U
#define RADIO_SEND_TEXT_MAX_LEN RADIO_SEND_HEX_MAX_LEN
#define RADIO_FREQUENCY_MIN_HZ 400000000U
#define RADIO_FREQUENCY_MAX_HZ 2500000000U
#define RADIO_PACKET_CAPTURE_DATA_MAX 255U
#define RADIO_NOISE_ANALYZER_HISTORY 126U
#define RADIO_TX_POWER_VALUE_COUNT 23U

BUILD_ASSERT(RADIO_NOISE_ANALYZER_DBM_MIN < RADIO_NOISE_ANALYZER_DBM_MAX,
             "noise analyzer dBm min must be less than max");

enum radio_number_field {
    RADIO_NUMBER_NONE,
    RADIO_NUMBER_SETTINGS_FREQUENCY,
    RADIO_NUMBER_SETTINGS_PREAMBLE,
    RADIO_NUMBER_SETTINGS_DUTY_RX_TIME,
    RADIO_NUMBER_SETTINGS_DUTY_SLEEP_TIME,
    RADIO_NUMBER_CW_FREQUENCY,
};

enum radio_deferred_action {
    RADIO_DEFERRED_ACTION_NONE,
    RADIO_DEFERRED_ACTION_AGC_RESET,
};

struct radio_settings {
    bool enabled;
    bool rx_only;
    bool rx_boosted;
    bool duty_cycle;
    uint32_t frequency_hz;
    uint8_t bandwidth_idx;
    uint8_t data_rate;
    uint8_t coding_rate;
    uint16_t preamble_length;
    uint8_t tx_power;
    bool packet_crc;
    uint32_t duty_cycle_rx_time;
    uint32_t duty_cycle_sleep_time;
};

struct radio_packet_capture_entry {
    uint32_t ts_ms;
    uint16_t raw_len;
    uint8_t len;
    int16_t rssi;
    int8_t snr;
    uint8_t data[RADIO_PACKET_CAPTURE_DATA_MAX];
};

struct radio_packet_capture_model {
    bool running;
    bool decode;
    uint8_t count;
    uint32_t last_gen_ms;
    uint32_t seq;
    uint8_t hex_scroll_line;
    uint8_t hex_total_lines;
    uint8_t hex_visible_lines;
    struct radio_packet_capture_entry entry;
};

struct radio_noise_analyzer_model {
    bool running;
    uint8_t count;
    uint8_t head;
    uint32_t last_gen_ms;
    uint32_t seq;
    int16_t current_dbm;
    int16_t min_dbm;
    int16_t max_dbm;
    int16_t noise_floor_dbm;
    int last_rc;
    bool rx_ready;
    bool channel_active;
    bool noise_floor_valid;
    bool calibration_requested;
    int16_t entries[RADIO_NOISE_ANALYZER_HISTORY];
};

struct radio_app {
    struct zui_host *host;
    struct zui_router *router;
    struct k_sem exit_sem;
    struct k_work_delayable tick_work;
    struct k_work action_work;
    struct k_work cw_done_work;

    struct zui_sublist *menu;
    struct zui_sublist *packet_menu;
    struct zui_form *settings_form;
    struct zui_form *cw_form;
    struct zui_number_editor *number_editor;
    struct zui_text_editor *text_editor;
    struct zui_hex_editor *hex_editor;
    struct zui_modal *reset_modal;

    struct zui_screen *menu_screen;
    struct zui_screen *packet_screen;
    struct zui_screen *capture_screen;
    struct zui_screen *cw_screen;
    struct zui_screen *noise_screen;
    struct zui_screen *settings_screen;
    struct zui_screen *number_screen;
    struct zui_screen *text_screen;
    struct zui_screen *hex_screen;
    struct zui_screen *reset_screen;

    struct zui_list_item menu_items[6];
    struct zui_list_item packet_items[3];
    struct zui_form_item form_items[RADIO_FORM_ITEMS_MAX];
    char value_bufs[RADIO_VALUE_BUF_COUNT][RADIO_VALUE_BUF_SIZE];
    size_t form_item_count;

    struct radio_settings applied;
    struct radio_settings editing;
    bool settings_reload;
    enum radio_number_field number_field;
    atomic_t deferred_action;

    uint8_t cw_tx_power;
    uint32_t cw_frequency_hz;
    uint16_t cw_duration_s;
    atomic_t cw_running;
    int cw_rc;
    uint32_t cw_toast_id;
    char cw_toast_text[48];

    atomic_t capture_enabled;
    atomic_t capture_dirty;
    struct k_spinlock capture_lock;
    struct radio_packet_capture_model capture_cache;
    struct radio_packet_capture_model capture_model;
    enum zui_input_code capture_pending_code;
    bool capture_press_pending;

    struct radio_noise_analyzer_model noise;

    struct mbs_radio_publish_event publish;
    char send_text[RADIO_SEND_TEXT_MAX_LEN + 1U];
    uint8_t send_hex[RADIO_SEND_HEX_MAX_LEN];
    char text_title[32];
    char hex_title[32];
    char toast_text[64];

    uint32_t current_screen;
    bool exit_requested;
};

extern const char *const radio_tx_power_values[RADIO_TX_POWER_VALUE_COUNT];
extern const struct zui_screen_ops radio_menu_ops;
extern const struct zui_screen_ops radio_packet_ops;
extern const struct zui_screen_ops radio_capture_ops;
extern const struct zui_screen_ops radio_cw_ops;
extern const struct zui_screen_ops radio_noise_ops;
extern const struct zui_screen_ops radio_settings_ops;
extern const struct zui_screen_ops radio_number_ops;
extern const struct zui_screen_ops radio_text_ops;
extern const struct zui_screen_ops radio_hex_ops;
extern const struct zui_screen_ops radio_reset_ops;

void radio_request_redraw(struct radio_app *app);
void radio_toast(struct radio_app *app, const char *title, const char *text,
                 const struct zui_icon *icon, uint32_t timeout_ms);
void radio_format_freq_mhz(char *buf, size_t size, uint32_t hz);
void radio_schedule_tick(struct radio_app *app);
void radio_switch(struct radio_app *app, uint32_t screen_id);
void radio_tick_work(struct k_work *work);
void radio_deferred_action_work(struct k_work *work);
void radio_submit_deferred_action(struct radio_app *app, enum radio_deferred_action action);
bool radio_require_enabled(struct radio_app *app);
bool radio_require_tx_allowed(struct radio_app *app);
int radio_value_buf(struct radio_app *app, size_t index, const char *fmt, ...);
void radio_form_add(struct radio_app *app, uint32_t id, const char *label,
                    const char *const *options, size_t option_count, size_t option_index);
void radio_form_add_value(struct radio_app *app, uint32_t id, const char *label,
                          const char *value);

void radio_settings_sanitize(struct radio_settings *settings);
void radio_settings_defaults(struct radio_settings *settings);
void radio_settings_load(struct radio_app *app);
void radio_settings_refresh(struct radio_app *app);
void radio_open_number(struct radio_app *app, enum radio_number_field field);
void radio_number_submitted(struct zui_number_editor *editor, int64_t value, void *user_data);
void radio_settings_changed(struct zui_form *form, uint32_t id, size_t option_index,
                            void *user_data);
void radio_settings_activated(struct zui_form *form, uint32_t id,
                              const struct zui_input_event *event, void *user_data);
void radio_reset_modal_result(struct zui_modal *modal, enum zui_modal_result result,
                              const struct zui_input_event *event, void *user_data);

void radio_cw_refresh(struct radio_app *app);
void radio_cw_dismiss_progress(struct radio_app *app);
void radio_cw_done_work(struct k_work *work);
void radio_cw_attach_app(struct radio_app *app);
void radio_cw_detach_app(struct radio_app *app);
void radio_cw_wait_idle(void);
void radio_cw_changed(struct zui_form *form, uint32_t id, size_t option_index,
                      void *user_data);
void radio_cw_activated(struct zui_form *form, uint32_t id,
                        const struct zui_input_event *event, void *user_data);

void radio_menu_selected(struct zui_sublist *list, uint32_t id, size_t index,
                         const struct zui_input_event *event, void *user_data);
void radio_packet_selected(struct zui_sublist *list, uint32_t id, size_t index,
                           const struct zui_input_event *event, void *user_data);
void radio_text_submitted(struct zui_text_editor *editor, const char *text,
                          void *user_data);
void radio_hex_submitted(struct zui_hex_editor *editor, const uint8_t *bytes,
                         size_t byte_count, void *user_data);

void radio_capture_set_enabled(bool enabled);
void radio_capture_attach_app(struct radio_app *app);
bool radio_capture_is_attached(struct radio_app *app);
void radio_capture_detach_app(struct radio_app *app);
void radio_capture_wait_idle(void);
void radio_capture_reset(struct radio_app *app);
bool radio_capture_drain(struct radio_app *app);
void radio_noise_reset(struct radio_noise_analyzer_model *model);
void radio_noise_collect(struct radio_app *app);

#ifdef __cplusplus
}
#endif
