/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/app_version.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#if defined(CONFIG_MBS_BLUETOOTH)
#include <bluetooth/bluetooth.h>
#endif
#if defined(CONFIG_MBS_CLOCK)
#include <clock/clock.h>
#endif
#include <desktop/desktop.h>
#if defined(CONFIG_MBS_DISPLAY)
#include <display/display.h>
#endif
#if defined(CONFIG_MBS_INDICATOR)
#include <indicator/indicator.h>
#endif
#if defined(CONFIG_MBS_POWER)
#include <power/power.h>
#endif
#if defined(CONFIG_MBS_TELEMETRY)
#include <telemetry/telemetry.h>
#endif
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/mem_stats.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/sys/util.h>
#include <zephyr/version.h>
#include <zui/zui.h>

#if defined(CONFIG_BT)
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#endif

#include "apps/app_common.h"
#include "apps/app_ids.h"
#include "assets/assets_icons.h"
#include "text/desktop_text.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYSTEM_SCREEN_MENU 1U
#define SYSTEM_SCREEN_TEXT 2U
#define SYSTEM_SCREEN_DISPLAY_FORM 3U
#define SYSTEM_SCREEN_DISPLAY_RESET 4U
#define SYSTEM_SCREEN_INDICATOR_FORM 5U
#define SYSTEM_SCREEN_INDICATOR_RESET 6U
#define SYSTEM_SCREEN_BLUETOOTH_FORM 7U
#define SYSTEM_SCREEN_BLUETOOTH_NUMBER 9U
#define SYSTEM_SCREEN_BLUETOOTH_RESET 10U
#define SYSTEM_SCREEN_CLOCK_FORM 11U
#define SYSTEM_SCREEN_CLOCK_RESET 12U
#define SYSTEM_SCREEN_POWER_FORM 13U
#define SYSTEM_SCREEN_POWER_RESET 15U
#define SYSTEM_SCREEN_INFO_MENU 16U
#define SYSTEM_SCREEN_TELEMETRY_READINGS 17U
#define SYSTEM_SCREEN_TELEMETRY_SETTINGS 18U
#define SYSTEM_SCREEN_TELEMETRY_NUMBER 19U
#define SYSTEM_SCREEN_TELEMETRY_RESET 20U
#define SYSTEM_TEXT_MAX    4096U
#define SYSTEM_MENU_ITEMS_MAX 7U
#define SYSTEM_INFO_MENU_ITEMS_MAX 5U
#define SYSTEM_DISPLAY_FORM_ITEMS_MAX 5U
#define SYSTEM_INDICATOR_FORM_ITEMS_MAX 8U
#define SYSTEM_BLUETOOTH_FORM_ITEMS_MAX 6U
#define SYSTEM_BLUETOOTH_VALUE_BUF_COUNT 1U
#define SYSTEM_BLUETOOTH_VALUE_BUF_SIZE 16U
#define SYSTEM_CLOCK_FORM_ITEMS_MAX 4U
#define SYSTEM_POWER_FORM_ITEMS_MAX 5U
#define SYSTEM_TELEMETRY_READING_ITEMS_MAX     16U
#define SYSTEM_TELEMETRY_READING_LABEL_MAX     24U
#define SYSTEM_TELEMETRY_READING_DETAIL_MAX    40U
#define SYSTEM_TELEMETRY_READING_SENSOR_MAX    24U
#define SYSTEM_TELEMETRY_VALUE_TEXT_MAX        96U
#define SYSTEM_TELEMETRY_AGE_TEXT_MAX          16U
#define SYSTEM_TELEMETRY_FORM_ITEMS_MAX        4U
#define SYSTEM_TELEMETRY_INTERVAL_TEXT_MAX     16U
#define SYSTEM_TELEMETRY_INTERVAL_MAX_DIGITS   10U
#define SYSTEM_TELEMETRY_READING_PLACEHOLDER   UINT32_MAX

#define SYSTEM_BLUETOOTH_FIXED_PASSKEY_MIN        0U
#define SYSTEM_BLUETOOTH_FIXED_PASSKEY_MAX        999999U
#define SYSTEM_BLUETOOTH_FIXED_PASSKEY_MAX_DIGITS 6U

#define SYSTEM_CLOCK_UTC_STEP_MIN 15
#define SYSTEM_CLOCK_UTC_VALUE_COUNT                                                         \
	((MBS_CLOCK_MAX_UTC_OFFSET_MINUTES - MBS_CLOCK_MIN_UTC_OFFSET_MINUTES) /     \
	 SYSTEM_CLOCK_UTC_STEP_MIN + 1)

enum system_menu_id {
	SYSTEM_MENU_INFORMATION = 1U,
	SYSTEM_MENU_DISPLAY,
	SYSTEM_MENU_BLUETOOTH,
	SYSTEM_MENU_CLOCK,
	SYSTEM_MENU_POWER,
	SYSTEM_MENU_TELEMETRY,
	SYSTEM_MENU_INDICATOR,
};

enum system_info_menu_id {
	SYSTEM_INFO_IDENTITY = 1U,
	SYSTEM_INFO_HARDWARE,
	SYSTEM_INFO_FIRMWARE,
	SYSTEM_INFO_RUNTIME,
	SYSTEM_INFO_DEVICES,
};

enum system_display_form_id {
	SYSTEM_DISPLAY_FORM_BRIGHTNESS = 1U,
	SYSTEM_DISPLAY_FORM_SLEEP_TIMEOUT,
	SYSTEM_DISPLAY_FORM_INVERT,
	SYSTEM_DISPLAY_FORM_APPLY = 0xF0U,
	SYSTEM_DISPLAY_FORM_RESET = 0xF1U,
};

enum system_indicator_form_id {
	SYSTEM_INDICATOR_FORM_LIGHT = 1U,
	SYSTEM_INDICATOR_FORM_HEARTBEAT,
	SYSTEM_INDICATOR_FORM_BUZZER,
	SYSTEM_INDICATOR_FORM_DM,
	SYSTEM_INDICATOR_FORM_CHANNEL,
	SYSTEM_INDICATOR_FORM_SYSTEM,
	SYSTEM_INDICATOR_FORM_APPLY = 0xF0U,
	SYSTEM_INDICATOR_FORM_RESET = 0xF1U,
};

enum system_bluetooth_form_id {
	SYSTEM_BLUETOOTH_FORM_ENABLED = 1U,
	SYSTEM_BLUETOOTH_FORM_MESHCORE_COMPANION,
	SYSTEM_BLUETOOTH_FORM_PASSKEY_MODE,
	SYSTEM_BLUETOOTH_FORM_FIXED_PASSKEY,
	SYSTEM_BLUETOOTH_FORM_APPLY = 0xF0U,
	SYSTEM_BLUETOOTH_FORM_RESET = 0xF1U,
};

enum system_clock_form_id {
	SYSTEM_CLOCK_FORM_FORMAT = 1U,
	SYSTEM_CLOCK_FORM_UTC_OFFSET,
	SYSTEM_CLOCK_FORM_APPLY = 0xF0U,
	SYSTEM_CLOCK_FORM_RESET = 0xF1U,
};

enum system_power_form_id {
	SYSTEM_POWER_FORM_LOW_VOLTAGE = 1U,
	SYSTEM_POWER_FORM_LOSING_POWER,
	SYSTEM_POWER_FORM_NO_CONNECTION,
	SYSTEM_POWER_FORM_APPLY = 0xF0U,
	SYSTEM_POWER_FORM_RESET = 0xF1U,
};

enum system_telemetry_menu_id {
	SYSTEM_TELEMETRY_MENU_READINGS = 1U,
	SYSTEM_TELEMETRY_MENU_TRIGGER,
	SYSTEM_TELEMETRY_MENU_SETTINGS,
};

enum system_telemetry_form_id {
	SYSTEM_TELEMETRY_FORM_ENABLED = 1U,
	SYSTEM_TELEMETRY_FORM_INTERVAL,
	SYSTEM_TELEMETRY_FORM_APPLY = 0xF0U,
	SYSTEM_TELEMETRY_FORM_RESET = 0xF1U,
};

#if defined(CONFIG_MBS_DESKTOP_DEVICE_NAME)
#define SYSTEM_DEVICE_NAME CONFIG_MBS_DESKTOP_DEVICE_NAME
#else
#define SYSTEM_DEVICE_NAME DESKTOP_TEXT_COMMON_UNKNOWN
#endif

#if defined(CONFIG_MBS_DESKTOP_MODEL_NAME)
#define SYSTEM_MODEL_NAME CONFIG_MBS_DESKTOP_MODEL_NAME
#else
#define SYSTEM_MODEL_NAME DESKTOP_TEXT_COMMON_UNKNOWN
#endif

#if defined(CONFIG_MBS_DESKTOP_MANUFACTURER_NAME)
#define SYSTEM_MANUFACTURER_NAME CONFIG_MBS_DESKTOP_MANUFACTURER_NAME
#else
#define SYSTEM_MANUFACTURER_NAME DESKTOP_TEXT_COMMON_UNKNOWN
#endif

#if defined(CONFIG_SOC)
#define SYSTEM_SOC_NAME CONFIG_SOC
#else
#define SYSTEM_SOC_NAME DESKTOP_TEXT_COMMON_UNKNOWN
#endif

#if defined(CONFIG_FLASH_SIZE)
#define SYSTEM_FLASH_SIZE_KB CONFIG_FLASH_SIZE
#else
#define SYSTEM_FLASH_SIZE_KB 0
#endif

#if defined(CONFIG_SRAM_SIZE)
#define SYSTEM_RAM_SIZE_KB CONFIG_SRAM_SIZE
#else
#define SYSTEM_RAM_SIZE_KB 0
#endif

struct system_app {
	struct desktop_app_context ctx;
	struct zui_sublist *menu;
	struct zui_sublist *info_menu;
	struct zui_text_view *text_view;
#if defined(CONFIG_MBS_DISPLAY)
	struct zui_form *display_form;
	struct zui_modal *display_reset_modal;
#endif
#if defined(CONFIG_MBS_BLUETOOTH)
	struct zui_form *bluetooth_form;
	struct zui_number_editor *bluetooth_number_editor;
	struct zui_modal *bluetooth_reset_modal;
#endif
#if defined(CONFIG_MBS_CLOCK)
	struct zui_form *clock_form;
	struct zui_modal *clock_reset_modal;
#endif
#if defined(CONFIG_MBS_POWER)
	struct zui_form *power_form;
	struct zui_modal *power_reset_modal;
#endif
#if defined(CONFIG_MBS_TELEMETRY)
	struct zui_sublist *telemetry_readings;
	struct zui_form *telemetry_settings_form;
	struct zui_number_editor *telemetry_number_editor;
	struct zui_modal *telemetry_reset_modal;
#endif
#if defined(CONFIG_MBS_INDICATOR)
	struct zui_form *indicator_form;
	struct zui_modal *indicator_reset_modal;
#endif
	struct zui_screen *menu_screen;
	struct zui_screen *info_menu_screen;
	struct zui_screen *text_screen;
#if defined(CONFIG_MBS_DISPLAY)
	struct zui_screen *display_form_screen;
	struct zui_screen *display_reset_screen;
#endif
#if defined(CONFIG_MBS_BLUETOOTH)
	struct zui_screen *bluetooth_form_screen;
	struct zui_screen *bluetooth_number_screen;
	struct zui_screen *bluetooth_reset_screen;
#endif
#if defined(CONFIG_MBS_CLOCK)
	struct zui_screen *clock_form_screen;
	struct zui_screen *clock_reset_screen;
#endif
#if defined(CONFIG_MBS_POWER)
	struct zui_screen *power_form_screen;
	struct zui_screen *power_reset_screen;
#endif
#if defined(CONFIG_MBS_TELEMETRY)
	struct zui_screen *telemetry_readings_screen;
	struct zui_screen *telemetry_settings_screen;
	struct zui_screen *telemetry_number_screen;
	struct zui_screen *telemetry_reset_screen;
#endif
#if defined(CONFIG_MBS_INDICATOR)
	struct zui_screen *indicator_form_screen;
	struct zui_screen *indicator_reset_screen;
#endif
	struct zui_list_item menu_items[SYSTEM_MENU_ITEMS_MAX];
	struct zui_list_item info_menu_items[SYSTEM_INFO_MENU_ITEMS_MAX];
	uint32_t text_back_screen;
#if defined(CONFIG_MBS_DISPLAY)
	struct zui_form_item display_form_items[SYSTEM_DISPLAY_FORM_ITEMS_MAX];
	size_t display_form_item_count;
	mbs_display_config display_applied;
	mbs_display_config display_editing;
#endif
#if defined(CONFIG_MBS_BLUETOOTH)
	struct zui_form_item bluetooth_form_items[SYSTEM_BLUETOOTH_FORM_ITEMS_MAX];
	size_t bluetooth_form_item_count;
	char bluetooth_value_bufs[SYSTEM_BLUETOOTH_VALUE_BUF_COUNT]
				 [SYSTEM_BLUETOOTH_VALUE_BUF_SIZE];
	mbs_bluetooth_config bluetooth_applied;
	mbs_bluetooth_config bluetooth_editing;
#endif
#if defined(CONFIG_MBS_CLOCK)
	struct zui_form_item clock_form_items[SYSTEM_CLOCK_FORM_ITEMS_MAX];
	size_t clock_form_item_count;
	mbs_clock_config clock_applied;
	mbs_clock_config clock_editing;
#endif
#if defined(CONFIG_MBS_POWER)
	struct zui_form_item power_form_items[SYSTEM_POWER_FORM_ITEMS_MAX];
	size_t power_form_item_count;
	mbs_power_config power_applied;
	mbs_power_config power_editing;
#endif
#if defined(CONFIG_MBS_TELEMETRY)
	struct zui_list_item telemetry_menu_items[3];
	struct zui_list_item telemetry_reading_items[SYSTEM_TELEMETRY_READING_ITEMS_MAX];
	struct zui_form_item telemetry_form_items[SYSTEM_TELEMETRY_FORM_ITEMS_MAX];
	char telemetry_reading_labels[SYSTEM_TELEMETRY_READING_ITEMS_MAX]
				      [SYSTEM_TELEMETRY_READING_LABEL_MAX];
	char telemetry_reading_details[SYSTEM_TELEMETRY_READING_ITEMS_MAX]
				       [SYSTEM_TELEMETRY_READING_DETAIL_MAX];
	char telemetry_reading_sensors[SYSTEM_TELEMETRY_READING_ITEMS_MAX]
				       [SYSTEM_TELEMETRY_READING_SENSOR_MAX];
	enum sensor_channel telemetry_reading_chans[SYSTEM_TELEMETRY_READING_ITEMS_MAX];
	uint8_t telemetry_reading_value_indices[SYSTEM_TELEMETRY_READING_ITEMS_MAX];
	size_t telemetry_reading_item_count;
	char telemetry_interval_text[SYSTEM_TELEMETRY_INTERVAL_TEXT_MAX];
	mbs_telemetry_config telemetry_applied;
	mbs_telemetry_config telemetry_editing;
#endif
#if defined(CONFIG_MBS_INDICATOR)
	struct zui_form_item indicator_form_items[SYSTEM_INDICATOR_FORM_ITEMS_MAX];
	size_t indicator_form_item_count;
	mbs_indicator_config indicator_applied;
	mbs_indicator_config indicator_editing;
#endif
	char text[SYSTEM_TEXT_MAX];
};

void system_open_menu(struct system_app *app);
void system_open_info_menu(struct system_app *app);
void system_text_update(struct system_app *app, const char *title);
#if defined(CONFIG_BT)
void system_bluetooth_addr(char *out, size_t out_size);
#endif
size_t system_bool_idx(bool value);
bool system_bool_from_idx(size_t option_index);

void system_menu_selected(struct zui_sublist *list, uint32_t id, size_t index,
			  const struct zui_input_event *event, void *user_data);
void system_info_menu_selected(struct zui_sublist *list, uint32_t id, size_t index,
			       const struct zui_input_event *event, void *user_data);
extern const struct zui_screen_ops system_menu_ops;
extern const struct zui_screen_ops system_info_menu_ops;
extern const struct zui_screen_ops system_text_ops;

#if defined(CONFIG_MBS_DISPLAY)
void system_display_prepare_options(void);
void system_open_display_form(struct system_app *app);
void system_display_form_changed(struct zui_form *form, uint32_t id, size_t option_index,
				 void *user_data);
void system_display_form_activated(struct zui_form *form, uint32_t id,
				   const struct zui_input_event *event, void *user_data);
void system_display_reset_modal_result(struct zui_modal *modal,
				       enum zui_modal_result result,
				       const struct zui_input_event *event, void *user_data);
extern const struct zui_screen_ops system_display_form_ops;
extern const struct zui_screen_ops system_display_reset_ops;
#endif

#if defined(CONFIG_MBS_BLUETOOTH)
void system_open_bluetooth_form(struct system_app *app);
void system_bluetooth_form_changed(struct zui_form *form, uint32_t id,
				   size_t option_index, void *user_data);
void system_bluetooth_form_activated(struct zui_form *form, uint32_t id,
				     const struct zui_input_event *event, void *user_data);
void system_bluetooth_reset_modal_result(struct zui_modal *modal,
					 enum zui_modal_result result,
					 const struct zui_input_event *event,
					 void *user_data);
extern const struct zui_screen_ops system_bluetooth_form_ops;
extern const struct zui_screen_ops system_bluetooth_number_ops;
extern const struct zui_screen_ops system_bluetooth_reset_ops;
#endif

#if defined(CONFIG_MBS_CLOCK)
void system_clock_prepare_options(void);
void system_open_clock_form(struct system_app *app);
void system_clock_form_changed(struct zui_form *form, uint32_t id, size_t option_index,
			       void *user_data);
void system_clock_form_activated(struct zui_form *form, uint32_t id,
				 const struct zui_input_event *event, void *user_data);
void system_clock_reset_modal_result(struct zui_modal *modal,
				     enum zui_modal_result result,
				     const struct zui_input_event *event, void *user_data);
extern const struct zui_screen_ops system_clock_form_ops;
extern const struct zui_screen_ops system_clock_reset_ops;
#endif

#if defined(CONFIG_MBS_POWER)
void system_power_prepare_options(void);
void system_open_power_form(struct system_app *app);
void system_power_form_changed(struct zui_form *form, uint32_t id, size_t option_index,
			       void *user_data);
void system_power_form_activated(struct zui_form *form, uint32_t id,
				 const struct zui_input_event *event, void *user_data);
void system_power_reset_modal_result(struct zui_modal *modal,
				     enum zui_modal_result result,
				     const struct zui_input_event *event, void *user_data);
extern const struct zui_screen_ops system_power_form_ops;
extern const struct zui_screen_ops system_power_reset_ops;
#endif

#if defined(CONFIG_MBS_TELEMETRY)
void system_open_telemetry_menu(struct system_app *app);
void system_telemetry_menu_selected(struct zui_sublist *list, uint32_t id, size_t index,
				    const struct zui_input_event *event, void *user_data);
void system_telemetry_number_submitted(struct zui_number_editor *editor, int64_t value,
				       void *user_data);
void system_telemetry_reset_modal_result(struct zui_modal *modal,
					 enum zui_modal_result result,
					 const struct zui_input_event *event, void *user_data);
extern const struct zui_screen_ops system_telemetry_readings_ops;
extern const struct zui_screen_ops system_telemetry_settings_ops;
extern const struct zui_screen_ops system_telemetry_number_ops;
extern const struct zui_screen_ops system_telemetry_reset_ops;
#endif

#if defined(CONFIG_MBS_INDICATOR)
void system_open_indicator_form(struct system_app *app);
void system_indicator_form_changed(struct zui_form *form, uint32_t id,
				   size_t option_index, void *user_data);
void system_indicator_form_activated(struct zui_form *form, uint32_t id,
				     const struct zui_input_event *event, void *user_data);
void system_indicator_reset_modal_result(struct zui_modal *modal,
					 enum zui_modal_result result,
					 const struct zui_input_event *event,
					 void *user_data);
extern const struct zui_screen_ops system_indicator_form_ops;
extern const struct zui_screen_ops system_indicator_reset_ops;
#endif

#ifdef __cplusplus
}
#endif
