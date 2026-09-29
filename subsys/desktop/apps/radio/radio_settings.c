/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "radio_private.h"

#include <stdarg.h>
#include <string.h>

#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "assets/assets_icons.h"
#include "text/desktop_text.h"

static const uint32_t radio_bw_hz_values[] = {
	7800U,  10400U, 15600U, 20800U, 31250U,
	41700U, 62500U, 125000U, 250000U, 500000U,
};
static const char *const radio_data_rate_values[] = {
	"5", "6", "7", "8", "9", "10", "11", "12",
};
static const char *const radio_coding_rate_values[] = {
	"5", "6", "7", "8",
};
const char *const radio_tx_power_values[RADIO_TX_POWER_VALUE_COUNT] = {
	"0",  "1",  "2",  "3",  "4",  "5",  "6",  "7",
	"8",  "9",  "10", "11", "12", "13", "14", "15",
	"16", "17", "18", "19", "20", "21", "22",
};

static uint32_t radio_bw_idx_to_hz(uint8_t idx)
{
	if (idx >= ARRAY_SIZE(radio_bw_hz_values)) {
		idx = 0U;
	}

	return radio_bw_hz_values[idx];
}

static uint8_t radio_bw_hz_to_idx(uint32_t bw_hz)
{
	uint8_t best = 0U;
	uint32_t best_diff = UINT32_MAX;

	for (uint8_t i = 0U; i < ARRAY_SIZE(radio_bw_hz_values); i++) {
		uint32_t v = radio_bw_hz_values[i];
		uint32_t diff = (v > bw_hz) ? (v - bw_hz) : (bw_hz - v);

		if (diff < best_diff) {
			best = i;
			best_diff = diff;
		}
	}

	return best;
}

void radio_settings_sanitize(struct radio_settings *settings)
{
	if (settings == NULL) {
		return;
	}

	settings->frequency_hz = CLAMP(settings->frequency_hz, RADIO_FREQUENCY_MIN_HZ,
				       RADIO_FREQUENCY_MAX_HZ);
	if (settings->bandwidth_idx >= ARRAY_SIZE(radio_bw_hz_values)) {
		settings->bandwidth_idx = 0U;
	}
	settings->data_rate = CLAMP(settings->data_rate, 5U, 12U);
	settings->coding_rate = CLAMP(settings->coding_rate, 5U, 8U);
	settings->preamble_length = CLAMP(settings->preamble_length, 6U, UINT16_MAX);
	settings->tx_power = MIN(settings->tx_power, 22U);
	settings->duty_cycle_rx_time = CLAMP(settings->duty_cycle_rx_time, 1U, 262143U);
	settings->duty_cycle_sleep_time = CLAMP(settings->duty_cycle_sleep_time, 1U, 262143U);
}

void radio_settings_defaults(struct radio_settings *settings)
{
	memset(settings, 0, sizeof(*settings));
	settings->enabled = true;
	settings->frequency_hz = 915125000U;
	settings->bandwidth_idx = 7U;
	settings->data_rate = 7U;
	settings->coding_rate = 5U;
	settings->preamble_length = 8U;
	settings->tx_power = 14U;
	settings->packet_crc = true;
	settings->duty_cycle_rx_time = 1U;
	settings->duty_cycle_sleep_time = 1U;
	radio_settings_sanitize(settings);
}

static int radio_settings_from_meshbus(struct radio_settings *out,
				       const mbs_radio_config *cfg)
{
	uint64_t hz64;

	if (out == NULL || cfg == NULL) {
		return -EINVAL;
	}

	memset(out, 0, sizeof(*out));
	out->enabled = cfg->enabled;
	out->rx_only = cfg->receive_only;
	out->rx_boosted = cfg->rx_boosted;
	out->duty_cycle = cfg->duty_cycle;
	hz64 = cfg->frequency;
	out->frequency_hz = (hz64 > UINT32_MAX) ? UINT32_MAX : (uint32_t)hz64;
	out->bandwidth_idx = radio_bw_hz_to_idx(cfg->bandwidth);
	out->data_rate = cfg->spread_factor;
	out->coding_rate = cfg->coding_rate;
	out->preamble_length = (cfg->preamble_length > UINT16_MAX) ?
				       UINT16_MAX :
				       (uint16_t)cfg->preamble_length;
	out->tx_power = (cfg->tx_power < 0) ? 0U : (uint8_t)cfg->tx_power;
	out->packet_crc = cfg->crc;
	out->duty_cycle_rx_time = cfg->duty_cycle_rx_time;
	out->duty_cycle_sleep_time = cfg->duty_cycle_sleep_time;
	radio_settings_sanitize(out);
	return 0;
}

static int radio_settings_to_meshbus(mbs_radio_config *out, const mbs_radio_config *base,
				     const struct radio_settings *in)
{
	struct radio_settings s;

	if (out == NULL || base == NULL || in == NULL) {
		return -EINVAL;
	}

	*out = *base;
	s = *in;
	radio_settings_sanitize(&s);
	out->enabled = s.enabled;
	out->receive_only = s.rx_only;
	out->rx_boosted = s.rx_boosted;
	out->duty_cycle = s.duty_cycle;
	out->frequency = (uint64_t)s.frequency_hz;
	out->bandwidth = radio_bw_idx_to_hz(s.bandwidth_idx);
	out->spread_factor = s.data_rate;
	out->coding_rate = s.coding_rate;
	out->preamble_length = s.preamble_length;
	out->tx_power = (int8_t)s.tx_power;
	out->crc = s.packet_crc;
	out->duty_cycle_rx_time = s.duty_cycle_rx_time;
	out->duty_cycle_sleep_time = s.duty_cycle_sleep_time;
	return 0;
}
void radio_settings_load(struct radio_app *app)
{
	mbs_radio_config cfg;

	if (app == NULL) {
		return;
	}

	if (mbs_radio_config_get(&cfg) == 0) {
		(void)radio_settings_from_meshbus(&app->applied, &cfg);
	}
	radio_settings_sanitize(&app->applied);
	app->editing = app->applied;
	radio_settings_sanitize(&app->editing);
	app->settings_reload = false;
}
void radio_settings_refresh(struct radio_app *app)
{
	if (app == NULL) {
		return;
	}

	radio_settings_sanitize(&app->editing);
	app->form_item_count = 0U;
	radio_form_add(app, RADIO_FORM_ENABLED, DESKTOP_TEXT_RADIO_SETTINGS_ENABLED,
		       DESKTOP_TEXT_COMMON_NO_YES_VALUES, 2U, app->editing.enabled ? 1U : 0U);
	radio_form_add(app, RADIO_FORM_RX_ONLY, DESKTOP_TEXT_RADIO_SETTINGS_RX_ONLY,
		       DESKTOP_TEXT_COMMON_NO_YES_VALUES, 2U, app->editing.rx_only ? 1U : 0U);
	radio_format_freq_mhz(app->value_bufs[0], sizeof(app->value_bufs[0]),
			      app->editing.frequency_hz);
	radio_form_add_value(app, RADIO_FORM_FREQUENCY, DESKTOP_TEXT_RADIO_SETTINGS_FREQUENCY,
			     app->value_bufs[0]);
	radio_form_add(app, RADIO_FORM_BANDWIDTH, DESKTOP_TEXT_RADIO_SETTINGS_BANDWIDTH,
		       DESKTOP_TEXT_RADIO_BW_VALUES, DESKTOP_TEXT_RADIO_BW_COUNT,
		       app->editing.bandwidth_idx);
	(void)radio_value_buf(app, 1U, "%u", (unsigned int)app->editing.data_rate);
	radio_form_add(app, RADIO_FORM_DATA_RATE, DESKTOP_TEXT_RADIO_SETTINGS_DATA_RATE,
		       radio_data_rate_values, ARRAY_SIZE(radio_data_rate_values),
		       (size_t)(app->editing.data_rate - 5U));
	(void)radio_value_buf(app, 2U, "%u", (unsigned int)app->editing.coding_rate);
	radio_form_add(app, RADIO_FORM_CODING_RATE, DESKTOP_TEXT_RADIO_SETTINGS_CODING_RATE,
		       radio_coding_rate_values, ARRAY_SIZE(radio_coding_rate_values),
		       (size_t)(app->editing.coding_rate - 5U));
	(void)radio_value_buf(app, 3U, "%u", (unsigned int)app->editing.preamble_length);
	radio_form_add_value(app, RADIO_FORM_PREAMBLE, DESKTOP_TEXT_RADIO_SETTINGS_PREAMBLE,
			     app->value_bufs[3]);
	(void)radio_value_buf(app, 4U, "%u", (unsigned int)app->editing.tx_power);
	radio_form_add(app, RADIO_FORM_TX_POWER, DESKTOP_TEXT_RADIO_SETTINGS_TX_POWER,
		       radio_tx_power_values, ARRAY_SIZE(radio_tx_power_values),
		       app->editing.tx_power);
	radio_form_add(app, RADIO_FORM_PACKET_CRC, DESKTOP_TEXT_RADIO_SETTINGS_PACKET_CRC,
		       DESKTOP_TEXT_COMMON_BOOL_VALUES, 2U, app->editing.packet_crc ? 1U : 0U);
	radio_form_add(app, RADIO_FORM_RX_BOOSTED, DESKTOP_TEXT_RADIO_SETTINGS_RX_BOOSTED,
		       DESKTOP_TEXT_COMMON_BOOL_VALUES, 2U, app->editing.rx_boosted ? 1U : 0U);
	radio_form_add(app, RADIO_FORM_DUTY_CYCLE, DESKTOP_TEXT_RADIO_SETTINGS_DUTY_CYCLE,
		       DESKTOP_TEXT_COMMON_BOOL_VALUES, 2U, app->editing.duty_cycle ? 1U : 0U);
	(void)radio_value_buf(app, 5U, DESKTOP_TEXT_RADIO_FORMAT_U_MS,
			      (unsigned int)app->editing.duty_cycle_rx_time);
	radio_form_add_value(app, RADIO_FORM_DUTY_RX_TIME,
			     DESKTOP_TEXT_RADIO_SETTINGS_DUTY_RX_TIME, app->value_bufs[5]);
	(void)radio_value_buf(app, 6U, DESKTOP_TEXT_RADIO_FORMAT_U_MS,
			      (unsigned int)app->editing.duty_cycle_sleep_time);
	radio_form_add_value(app, RADIO_FORM_DUTY_SLEEP_TIME,
			     DESKTOP_TEXT_RADIO_SETTINGS_DUTY_SLEEP_TIME, app->value_bufs[6]);
	radio_form_add_value(app, RADIO_FORM_APPLY, DESKTOP_TEXT_COMMON_ACTION_APPLY,
			     DESKTOP_TEXT_COMMON_EMPTY);
	radio_form_add_value(app, RADIO_FORM_RESET, DESKTOP_TEXT_COMMON_ACTION_RESET,
			     DESKTOP_TEXT_COMMON_EMPTY);
	(void)zui_form_update(app->settings_form, &(struct zui_form_config){
		.title = DESKTOP_TEXT_RADIO_TITLE,
		.items = app->form_items,
		.item_count = app->form_item_count,
		.changed = radio_settings_changed,
		.activated = radio_settings_activated,
		.user_data = app,
	});
}
static void radio_apply_settings(struct radio_app *app)
{
	mbs_radio_config base;
	mbs_radio_config cfg;
	int rc;

	if (app == NULL) {
		return;
	}

	rc = mbs_radio_config_get(&base);
	if (rc == 0) {
		(void)radio_settings_to_meshbus(&cfg, &base, &app->editing);
		rc = mbs_radio_config_set(&cfg);
	}
	if (rc == 0) {
		app->applied = app->editing;
		radio_settings_sanitize(&app->applied);
		radio_toast(app, DESKTOP_TEXT_RADIO_TITLE, DESKTOP_TEXT_COMMON_SETTINGS_APPLIED,
			    &I_save_24x24, 900U);
		radio_switch(app, RADIO_SCREEN_MENU);
		return;
	}

	radio_toast(app, DESKTOP_TEXT_RADIO_TITLE, DESKTOP_TEXT_COMMON_SETTINGS_INVALID,
		    &I_error_24x24, 1500U);
}

static void radio_reset_confirmed(struct radio_app *app)
{
	int rc;

	if (app == NULL) {
		return;
	}

	rc = mbs_radio_config_reset();
	if (rc == 0) {
		radio_settings_load(app);
		radio_toast(app, DESKTOP_TEXT_RADIO_TITLE, DESKTOP_TEXT_COMMON_SETTINGS_RESET,
			    &I_save_24x24, 900U);
		radio_switch(app, RADIO_SCREEN_MENU);
		return;
	}

	radio_toast(app, DESKTOP_TEXT_RADIO_TITLE, DESKTOP_TEXT_COMMON_SETTINGS_INVALID,
		    &I_error_24x24, 1500U);
	radio_switch(app, RADIO_SCREEN_SETTINGS);
}

static void radio_open_reset_modal(struct radio_app *app)
{
	if (app == NULL) {
		return;
	}

	(void)zui_modal_update(app->reset_modal, &(struct zui_modal_config){
		.title = DESKTOP_TEXT_COMMON_RESET,
		.text = DESKTOP_TEXT_COMMON_RESET_CONFIRM_TEXT,
		.left_button = DESKTOP_TEXT_COMMON_CANCEL,
		.right_button = DESKTOP_TEXT_COMMON_OK,
		.result = radio_reset_modal_result,
		.user_data = app,
	});
	radio_switch(app, RADIO_SCREEN_RESET);
}

void radio_open_number(struct radio_app *app, enum radio_number_field field)
{
	const char *title = DESKTOP_TEXT_RADIO_HEADER_FREQUENCY_HZ;
	int64_t value = 0;
	int64_t min_value = 0;
	int64_t max_value = 0;
	size_t max_digits = 10U;

	if (app == NULL) {
		return;
	}

	app->number_field = field;
	switch (field) {
	case RADIO_NUMBER_SETTINGS_FREQUENCY:
		title = DESKTOP_TEXT_RADIO_HEADER_FREQUENCY_HZ;
		value = app->editing.frequency_hz;
		min_value = RADIO_FREQUENCY_MIN_HZ;
		max_value = RADIO_FREQUENCY_MAX_HZ;
		break;
	case RADIO_NUMBER_SETTINGS_PREAMBLE:
		title = DESKTOP_TEXT_RADIO_HEADER_PREAMBLE;
		value = app->editing.preamble_length;
		min_value = 6;
		max_value = UINT16_MAX;
		break;
	case RADIO_NUMBER_SETTINGS_DUTY_RX_TIME:
		title = DESKTOP_TEXT_RADIO_HEADER_DUTY_RX_MS;
		value = app->editing.duty_cycle_rx_time;
		min_value = 1;
		max_value = 262143;
		break;
	case RADIO_NUMBER_SETTINGS_DUTY_SLEEP_TIME:
		title = DESKTOP_TEXT_RADIO_HEADER_DUTY_SLEEP_MS;
		value = app->editing.duty_cycle_sleep_time;
		min_value = 1;
		max_value = 262143;
		break;
	case RADIO_NUMBER_CW_FREQUENCY:
		title = DESKTOP_TEXT_RADIO_HEADER_FREQUENCY_HZ;
		value = app->cw_frequency_hz;
		min_value = RADIO_FREQUENCY_MIN_HZ;
		max_value = RADIO_FREQUENCY_MAX_HZ;
		break;
	default:
		return;
	}

	(void)zui_number_editor_update(app->number_editor, &(struct zui_number_editor_config){
		.title = title,
		.value = value,
		.min_value = min_value,
		.max_value = max_value,
		.max_digits = max_digits,
		.unsigned_only = true,
		.submitted = radio_number_submitted,
		.user_data = app,
	});
	radio_switch(app, RADIO_SCREEN_NUMBER);
}

void radio_number_submitted(struct zui_number_editor *editor, int64_t value, void *user_data)
{
	struct radio_app *app = user_data;

	ARG_UNUSED(editor);
	if (app == NULL) {
		return;
	}

	switch (app->number_field) {
	case RADIO_NUMBER_SETTINGS_FREQUENCY:
		app->editing.frequency_hz = (uint32_t)value;
		radio_settings_refresh(app);
		radio_switch(app, RADIO_SCREEN_SETTINGS);
		break;
	case RADIO_NUMBER_SETTINGS_PREAMBLE:
		app->editing.preamble_length = (uint16_t)value;
		radio_settings_refresh(app);
		radio_switch(app, RADIO_SCREEN_SETTINGS);
		break;
	case RADIO_NUMBER_SETTINGS_DUTY_RX_TIME:
		app->editing.duty_cycle_rx_time = (uint32_t)value;
		radio_settings_refresh(app);
		radio_switch(app, RADIO_SCREEN_SETTINGS);
		break;
	case RADIO_NUMBER_SETTINGS_DUTY_SLEEP_TIME:
		app->editing.duty_cycle_sleep_time = (uint32_t)value;
		radio_settings_refresh(app);
		radio_switch(app, RADIO_SCREEN_SETTINGS);
		break;
	case RADIO_NUMBER_CW_FREQUENCY:
		app->cw_frequency_hz = (uint32_t)value;
		radio_cw_refresh(app);
		radio_switch(app, RADIO_SCREEN_CW);
		break;
	default:
		break;
	}
}
void radio_settings_changed(struct zui_form *form, uint32_t id, size_t option_index,
				   void *user_data)
{
	struct radio_app *app = user_data;

	ARG_UNUSED(form);

	if (app == NULL) {
		return;
	}

	switch (id) {
	case RADIO_FORM_ENABLED:
		app->editing.enabled = option_index != 0U;
		break;
	case RADIO_FORM_RX_ONLY:
		app->editing.rx_only = option_index != 0U;
		break;
	case RADIO_FORM_BANDWIDTH:
		app->editing.bandwidth_idx = (uint8_t)option_index;
		break;
	case RADIO_FORM_DATA_RATE:
		app->editing.data_rate = (uint8_t)(5U + option_index);
		break;
	case RADIO_FORM_CODING_RATE:
		app->editing.coding_rate = (uint8_t)(5U + option_index);
		break;
	case RADIO_FORM_TX_POWER:
		app->editing.tx_power = (uint8_t)option_index;
		break;
	case RADIO_FORM_PACKET_CRC:
		app->editing.packet_crc = option_index != 0U;
		break;
	case RADIO_FORM_RX_BOOSTED:
		app->editing.rx_boosted = option_index != 0U;
		break;
	case RADIO_FORM_DUTY_CYCLE:
		app->editing.duty_cycle = option_index != 0U;
		break;
	default:
		break;
	}
	radio_settings_refresh(app);
	radio_request_redraw(app);
}

void radio_settings_activated(struct zui_form *form, uint32_t id,
				     const struct zui_input_event *event, void *user_data)
{
	struct radio_app *app = user_data;

	ARG_UNUSED(form);
	ARG_UNUSED(event);

	switch (id) {
	case RADIO_FORM_FREQUENCY:
		radio_open_number(app, RADIO_NUMBER_SETTINGS_FREQUENCY);
		break;
	case RADIO_FORM_PREAMBLE:
		radio_open_number(app, RADIO_NUMBER_SETTINGS_PREAMBLE);
		break;
	case RADIO_FORM_DUTY_RX_TIME:
		radio_open_number(app, RADIO_NUMBER_SETTINGS_DUTY_RX_TIME);
		break;
	case RADIO_FORM_DUTY_SLEEP_TIME:
		radio_open_number(app, RADIO_NUMBER_SETTINGS_DUTY_SLEEP_TIME);
		break;
	case RADIO_FORM_APPLY:
		radio_apply_settings(app);
		break;
	case RADIO_FORM_RESET:
		radio_open_reset_modal(app);
		break;
	default:
		break;
	}
}
static void radio_form_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct radio_app *app = user_data;

	(void)zui_screen_draw(zui_form_get_screen(app->settings_form), draw);
}

static bool radio_form_input(const struct zui_input_event *event, void *user_data)
{
	struct radio_app *app = user_data;

	if (app == NULL || event == NULL) {
		return false;
	}
	if (desktop_app_input_should_consume_edge(event)) {
		return true;
	}
	if (desktop_app_input_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		app->editing = app->applied;
		radio_switch(app, RADIO_SCREEN_MENU);
		return true;
	}
	if (desktop_app_input_is_long_press(event) && event->code == ZUI_INPUT_CODE_SELECT) {
		radio_apply_settings(app);
		return true;
	}
	if (zui_screen_submit_input(zui_form_get_screen(app->settings_form), event) > 0) {
		radio_request_redraw(app);
		return true;
	}

	return false;
}

static void radio_form_enter(void *user_data)
{
	struct radio_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_enter(zui_form_get_screen(app->settings_form));
	}
}

static void radio_form_exit(void *user_data)
{
	struct radio_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_exit(zui_form_get_screen(app->settings_form));
	}
}
static void radio_number_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct radio_app *app = user_data;

	(void)zui_screen_draw(zui_number_editor_get_screen(app->number_editor), draw);
}

static bool radio_number_input(const struct zui_input_event *event, void *user_data)
{
	struct radio_app *app = user_data;

	if (app == NULL || event == NULL) {
		return false;
	}
	if (desktop_app_input_should_consume_edge(event)) {
		return true;
	}
	if (desktop_app_input_is_long_press(event) && event->code == ZUI_INPUT_CODE_BACK) {
		radio_switch(app, app->number_field == RADIO_NUMBER_CW_FREQUENCY ?
					  RADIO_SCREEN_CW :
					  RADIO_SCREEN_SETTINGS);
		return true;
	}
	if (zui_screen_submit_input(zui_number_editor_get_screen(app->number_editor), event) > 0) {
		radio_request_redraw(app);
		return true;
	}

	return false;
}

static void radio_number_enter(void *user_data)
{
	struct radio_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_enter(zui_number_editor_get_screen(app->number_editor));
	}
}

static void radio_number_exit(void *user_data)
{
	struct radio_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_exit(zui_number_editor_get_screen(app->number_editor));
	}
}

void radio_reset_modal_result(struct zui_modal *modal, enum zui_modal_result result,
			      const struct zui_input_event *event, void *user_data)
{
	struct radio_app *app = user_data;

	ARG_UNUSED(modal);
	ARG_UNUSED(event);

	if (app == NULL) {
		return;
	}

	if (result == ZUI_MODAL_RESULT_RIGHT) {
		radio_reset_confirmed(app);
	} else {
		radio_switch(app, RADIO_SCREEN_SETTINGS);
	}
}

static void radio_reset_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct radio_app *app = user_data;

	(void)zui_screen_draw(zui_modal_get_screen(app->reset_modal), draw);
}

static bool radio_reset_input(const struct zui_input_event *event, void *user_data)
{
	struct radio_app *app = user_data;

	if (app == NULL || event == NULL) {
		return false;
	}
	if (desktop_app_input_should_consume_edge(event)) {
		return true;
	}
	if (desktop_app_input_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		radio_reset_modal_result(app->reset_modal, ZUI_MODAL_RESULT_LEFT, event, app);
		return true;
	}
	if (zui_screen_submit_input(zui_modal_get_screen(app->reset_modal), event) > 0) {
		radio_request_redraw(app);
		return true;
	}

	return false;
}

const struct zui_screen_ops radio_settings_ops = {
	.draw = radio_form_draw,
	.input = radio_form_input,
	.enter = radio_form_enter,
	.exit = radio_form_exit,
};
const struct zui_screen_ops radio_number_ops = {
	.draw = radio_number_draw,
	.input = radio_number_input,
	.enter = radio_number_enter,
	.exit = radio_number_exit,
};
const struct zui_screen_ops radio_reset_ops = {
	.draw = radio_reset_draw,
	.input = radio_reset_input,
};
