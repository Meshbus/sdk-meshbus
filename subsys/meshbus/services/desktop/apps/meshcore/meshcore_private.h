/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/meshbus/channel.h>
#include <zephyr/meshbus/meshcore.h>
#include <zephyr/zui/zui.h>

#include "meshcore_qr.h"
#include "text/desktop_text.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MESHCORE_SCREEN_MENU             1U
#define MESHCORE_SCREEN_SETTINGS         2U
#define MESHCORE_SCREEN_RADIO_PRESET     3U
#define MESHCORE_SCREEN_CHANNELS         4U
#define MESHCORE_SCREEN_CHANNEL_SETTINGS 5U
#define MESHCORE_SCREEN_TEXT             6U
#define MESHCORE_SCREEN_NUMBER           7U
#define MESHCORE_SCREEN_DETAIL           8U
#define MESHCORE_SCREEN_MODAL            9U

#define MESHCORE_MENU_ADVERT   1U
#define MESHCORE_MENU_RADIO    2U
#define MESHCORE_MENU_CHANNELS 3U
#define MESHCORE_MENU_SETTINGS 4U

#define MESHCORE_FORM_NAME               1U
#define MESHCORE_FORM_ROLE               2U
#define MESHCORE_FORM_PUBLIC_KEY         3U
#define MESHCORE_FORM_MULTI_ACKS         4U
#define MESHCORE_FORM_ADVERT_POSITION    5U
#define MESHCORE_FORM_PATH_HASH_SIZE     6U
#define MESHCORE_FORM_CLIENT_REPEAT      7U
#define MESHCORE_FORM_ADD_CONTACT_OVERWRITE 8U
#define MESHCORE_FORM_ADD_CONTACT_CHAT      9U
#define MESHCORE_FORM_ADD_CONTACT_REPEATER  10U
#define MESHCORE_FORM_ADD_CONTACT_ROOM      11U
#define MESHCORE_FORM_DISABLE_FWD        12U
#define MESHCORE_FORM_LOOP_DETECT        13U
#define MESHCORE_FORM_FLOOD_MAX          14U
#define MESHCORE_FORM_ADVERT_INTERVAL    15U
#define MESHCORE_FORM_FLOOD_ADVERT       16U
#define MESHCORE_FORM_CONTACT_ADD_POLICY    17U
#define MESHCORE_FORM_FORWARDING         18U
#define MESHCORE_FORM_TELEMETRY_MODE     19U
#define MESHCORE_FORM_ADD_CONTACT_SENSOR    20U
#define MESHCORE_FORM_ADD_CONTACT_HOPS      21U
#define MESHCORE_FORM_TELEMETRY_BASE     22U
#define MESHCORE_FORM_TELEMETRY_LOCAT    23U
#define MESHCORE_FORM_TELEMETRY_ENV      24U
#define MESHCORE_FORM_TX_DELAY_FACTOR    25U
#define MESHCORE_FORM_DIRECT_TX_DELAY    26U
#define MESHCORE_FORM_RESET              0xE0U
#define MESHCORE_FORM_APPLY              0xF0U

#define MESHCORE_CHANNEL_FORM_NAME   1U
#define MESHCORE_CHANNEL_FORM_SECRET 2U
#define MESHCORE_CHANNEL_FORM_APPLY  0xF0U
#define MESHCORE_CHANNEL_FORM_RESET  0xF1U

#define MESHCORE_CHANNEL_ITEM_EMPTY UINT32_MAX

#define MESHCORE_NAME_MAX            32U
#define MESHCORE_TEXT_MAX            512U
#define MESHCORE_FORM_ITEMS_MAX      24U
#define MESHCORE_VALUE_BUF_COUNT     24U
#define MESHCORE_VALUE_BUF_SIZE      20U
#define MESHCORE_CHANNEL_ITEMS_MAX   CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS
#define MESHCORE_CHANNEL_LABEL_MAX   32U
#define MESHCORE_RADIO_PRESET_CUSTOM 0xFFU
#define MESHCORE_CONTACT_ADD_HOPS_LIMIT_MAX 63U

enum meshcore_number_field {
	MESHCORE_NUMBER_NONE,
	MESHCORE_NUMBER_MULTI_ACKS,
	MESHCORE_NUMBER_FLOOD_MAX,
	MESHCORE_NUMBER_ADVERT_INTERVAL,
	MESHCORE_NUMBER_FLOOD_ADVERT_INTERVAL,
	MESHCORE_NUMBER_CONTACT_ADD_HOPS_LIMIT,
	MESHCORE_NUMBER_TX_DELAY_FACTOR_X100,
	MESHCORE_NUMBER_DIRECT_TX_DELAY_FACTOR_X100,
};

enum meshcore_text_mode {
	MESHCORE_TEXT_NONE,
	MESHCORE_TEXT_NODE_NAME,
	MESHCORE_TEXT_CHANNEL_NAME,
};

enum meshcore_modal_kind {
	MESHCORE_MODAL_NONE,
	MESHCORE_MODAL_ADVERT,
	MESHCORE_MODAL_RESET,
	MESHCORE_MODAL_CHANNEL_RESET,
	MESHCORE_MODAL_RADIO_PRESET,
};

enum meshcore_detail_kind {
	MESHCORE_DETAIL_TEXT,
	MESHCORE_DETAIL_CHANNEL_SECRET,
};

enum meshcore_settings_page {
	MESHCORE_SETTINGS_PAGE_ROOT,
	MESHCORE_SETTINGS_PAGE_CONTACT_ADD_POLICY,
	MESHCORE_SETTINGS_PAGE_FORWARDING,
	MESHCORE_SETTINGS_PAGE_TELEMETRY_MODE,
};

struct meshcore_radio_preset {
	uint32_t frequency_hz;
	uint32_t bandwidth_hz;
	uint8_t spread_factor;
	uint8_t coding_rate;
};

struct meshcore_app {
	struct k_work settings_work;
	atomic_t settings_busy;
	bool settings_reset;
	meshbus_meshcore_config settings_candidate;
	struct zui_host *host;
	struct zui_router *router;
	struct k_sem exit_sem;

	struct zui_sublist *menu;
	struct zui_sublist *radio_preset;
	struct zui_sublist *channels;
	struct zui_form *settings_form;
	struct zui_form *channel_form;
	struct zui_text_editor *text_editor;
	struct zui_number_editor *number_editor;
	struct zui_text_view *detail_view;
	struct zui_modal *modal;

	struct zui_screen *menu_screen;
	struct zui_screen *settings_screen;
	struct zui_screen *radio_screen;
	struct zui_screen *channels_screen;
	struct zui_screen *channel_settings_screen;
	struct zui_screen *text_screen;
	struct zui_screen *number_screen;
	struct zui_screen *detail_screen;
	struct zui_screen *modal_screen;

	struct zui_list_item menu_items[4];
	struct zui_list_item preset_items[DESKTOP_TEXT_MESHCORE_RADIO_PRESET_COUNT];
	struct zui_list_item channel_items[MESHCORE_CHANNEL_ITEMS_MAX];
	struct zui_form_item form_items[MESHCORE_FORM_ITEMS_MAX];
	struct zui_form_item channel_form_items[4];
	char value_bufs[MESHCORE_VALUE_BUF_COUNT][MESHCORE_VALUE_BUF_SIZE];
	char channel_labels[MESHCORE_CHANNEL_ITEMS_MAX][MESHCORE_CHANNEL_LABEL_MAX];
	char channel_right[MESHCORE_CHANNEL_ITEMS_MAX][10];
	char text_buf[MESHCORE_NAME_MAX + 1U];
	char detail_text[MESHCORE_TEXT_MAX];
	char modal_text[160];
	struct meshcore_qr_code detail_qr;

	meshbus_meshcore_config applied;
	meshbus_meshcore_config editing;
	meshbus_channel channel_applied;
	meshbus_channel channel_editing;

	uint32_t current_screen;
	uint32_t return_screen;
	size_t form_item_count;
	size_t channel_item_count;
	size_t channel_form_item_count;
	size_t settings_root_selected;
	size_t selected_channel_slot;
	uint8_t selected_channel_idx;
	uint8_t radio_preset_selected_idx;
	uint8_t radio_preset_pending_idx;
	enum meshcore_settings_page settings_page;
	enum meshcore_number_field number_field;
	enum meshcore_text_mode text_mode;
	enum meshcore_modal_kind modal_kind;
	enum meshcore_detail_kind detail_kind;
	bool detail_qr_visible;
	bool detail_qr_valid;
};

extern const struct zui_screen_ops meshcore_menu_ops;
extern const struct zui_screen_ops meshcore_settings_ops;
extern const struct zui_screen_ops meshcore_radio_ops;
extern const struct zui_screen_ops meshcore_channels_ops;
extern const struct zui_screen_ops meshcore_channel_form_ops;
extern const struct zui_screen_ops meshcore_text_ops;
extern const struct zui_screen_ops meshcore_number_ops;
extern const struct zui_screen_ops meshcore_detail_ops;
extern const struct zui_screen_ops meshcore_modal_ops;

bool meshcore_is_click(const struct zui_input_event *event);
bool meshcore_is_long(const struct zui_input_event *event);
bool meshcore_should_consume_edge(const struct zui_input_event *event);
void meshcore_request_redraw(struct meshcore_app *app);
void meshcore_switch(struct meshcore_app *app, uint32_t screen_id);
void meshcore_exit(struct meshcore_app *app);
void meshcore_toast(struct meshcore_app *app, const char *text, const struct zui_icon *icon,
		    uint32_t timeout_ms);
void meshcore_apply_toast(struct meshcore_app *app, bool success, const char *success_text);
void meshcore_settings_sanitize(meshbus_meshcore_config *cfg);
void meshcore_channel_sanitize(meshbus_channel *channel);
char *meshcore_value_buf(struct meshcore_app *app, size_t *idx);
void meshcore_hex_short(const uint8_t *bytes, size_t size, char *buf, size_t buf_size,
			size_t max_bytes);
void meshcore_hex_payload(char *out, size_t out_size, const uint8_t *bytes, size_t size);
void meshcore_show_detail(struct meshcore_app *app, const char *title, const uint8_t *bytes,
			  size_t size);
void meshcore_show_channel_secret_detail(struct meshcore_app *app, const uint8_t *bytes,
					 size_t size);
void meshcore_show_modal(struct meshcore_app *app, enum meshcore_modal_kind kind, const char *title,
			 const char *text, const char *left, const char *right);
void meshcore_open_number(struct meshcore_app *app, enum meshcore_number_field field,
			  const char *title, int64_t value, int64_t min_value, int64_t max_value);
void meshcore_open_name_editor(struct meshcore_app *app, enum meshcore_text_mode mode,
			       const char *title, const char *text, uint32_t return_screen);
bool meshcore_forward_input(struct zui_screen *screen, const struct zui_input_event *event);
void meshcore_forward_enter(struct zui_screen *screen);
void meshcore_forward_exit(struct zui_screen *screen);

void meshcore_prepare_menu(struct meshcore_app *app);
void meshcore_menu_selected(struct zui_sublist *list, uint32_t id, size_t index,
			    const struct zui_input_event *event, void *user_data);

void meshcore_reload_node(struct meshcore_app *app);
void meshcore_build_settings_form(struct meshcore_app *app);
void meshcore_sync_settings_from_form(struct meshcore_app *app);
void meshcore_apply_settings(struct meshcore_app *app);
void meshcore_settings_work(struct k_work *work);
void meshcore_reset_settings(struct meshcore_app *app);
void meshcore_settings_activated(struct zui_form *form, uint32_t id,
				 const struct zui_input_event *event, void *user_data);
void meshcore_number_submitted(struct zui_number_editor *editor, int64_t value, void *user_data);

void meshcore_build_radio_preset_list(struct meshcore_app *app);
uint8_t meshcore_radio_preset_match(void);
void meshcore_apply_radio_preset(struct meshcore_app *app);
void meshcore_radio_selected(struct zui_sublist *list, uint32_t id, size_t index,
			     const struct zui_input_event *event, void *user_data);

void meshcore_build_channels_list(struct meshcore_app *app);
void meshcore_build_channel_form(struct meshcore_app *app);
void meshcore_apply_channel(struct meshcore_app *app);
void meshcore_reset_channel(struct meshcore_app *app);
void meshcore_channel_selected(struct zui_sublist *list, uint32_t id, size_t index,
			       const struct zui_input_event *event, void *user_data);
void meshcore_channel_form_activated(struct zui_form *form, uint32_t id,
				     const struct zui_input_event *event, void *user_data);

void meshcore_text_submitted(struct zui_text_editor *editor, const char *text, void *user_data);
void meshcore_modal_result(struct zui_modal *modal, enum zui_modal_result result,
			   const struct zui_input_event *event, void *user_data);

#ifdef __cplusplus
}
#endif
