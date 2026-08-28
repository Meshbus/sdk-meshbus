/* SPDX-License-Identifier: Apache-2.0 */

#include "gnss_private.h"

#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "assets/assets_icons.h"
#include "services/gnss/gnss_internal.h"
#include "text/desktop_text.h"

LOG_MODULE_DECLARE(meshbus_desktop_gnss, CONFIG_MESHBUS_DESKTOP_LOG_LEVEL);

static const uint8_t gnss_fix_rate_values[] = {1U, 2U, 4U, 5U, 10U};

static uint8_t gnss_fix_rate_to_idx(uint8_t fix_rate_hz)
{
	for (uint8_t i = 0U; i < ARRAY_SIZE(gnss_fix_rate_values); i++) {
		if (gnss_fix_rate_values[i] == fix_rate_hz) {
			return i;
		}
	}

	return 0U;
}

static uint8_t gnss_fix_rate_from_idx(uint8_t idx)
{
	if (idx >= ARRAY_SIZE(gnss_fix_rate_values)) {
		idx = 0U;
	}

	return gnss_fix_rate_values[idx];
}

static void gnss_systems_from_mask(struct gnss_settings_state *settings, uint32_t mask)
{
	settings->system_gps = (mask & BIT(0)) != 0U;
	settings->system_glonass = (mask & BIT(1)) != 0U;
	settings->system_galileo = (mask & BIT(2)) != 0U;
	settings->system_beidou = (mask & BIT(3)) != 0U;
	settings->system_qzss = (mask & BIT(4)) != 0U;
	settings->system_irnss = (mask & BIT(5)) != 0U;
	settings->system_sbas = (mask & BIT(6)) != 0U;
	settings->system_imes = (mask & BIT(7)) != 0U;
}

static uint32_t gnss_system_mask_from_settings(const struct gnss_settings_state *settings)
{
	uint32_t mask = 0U;

	if (settings->system_gps) {
		mask |= BIT(0);
	}
	if (settings->system_glonass) {
		mask |= BIT(1);
	}
	if (settings->system_galileo) {
		mask |= BIT(2);
	}
	if (settings->system_beidou) {
		mask |= BIT(3);
	}
	if (settings->system_qzss) {
		mask |= BIT(4);
	}
	if (settings->system_irnss) {
		mask |= BIT(5);
	}
	if (settings->system_sbas) {
		mask |= BIT(6);
	}
	if (settings->system_imes) {
		mask |= BIT(7);
	}

	return mask == 0U ? BIT(0) : mask;
}

static void gnss_settings_sanitize(struct gnss_settings_state *settings)
{
	if (settings->nav_mode_idx > GNSS_NAVIGATION_MODE_HIGH_DYNAMICS) {
		settings->nav_mode_idx = 0U;
	}
	if (settings->fix_rate_idx >= ARRAY_SIZE(gnss_fix_rate_values)) {
		settings->fix_rate_idx = 0U;
	}
	if (settings->system_mode_idx > GNSS_SYSTEM_MODE_CUSTOM) {
		settings->system_mode_idx = GNSS_SYSTEM_MODE_ALL;
	}
	settings->update_interval_ms =
		CLAMP(settings->update_interval_ms, GNSS_UPDATE_INTERVAL_MIN,
		      GNSS_UPDATE_INTERVAL_MAX);
	settings->min_active_time_ms =
		CLAMP(settings->min_active_time_ms, GNSS_MIN_ACTIVE_TIME_MIN,
		      GNSS_MIN_ACTIVE_TIME_MAX);

	if (settings->system_mode_idx == GNSS_SYSTEM_MODE_ALL) {
		settings->system_gps = true;
		settings->system_glonass = true;
		settings->system_galileo = true;
		settings->system_beidou = true;
		settings->system_qzss = true;
		settings->system_irnss = true;
		settings->system_sbas = true;
		settings->system_imes = true;
	} else if (!settings->system_gps && !settings->system_glonass &&
		   !settings->system_galileo && !settings->system_beidou &&
		   !settings->system_qzss && !settings->system_irnss && !settings->system_sbas &&
		   !settings->system_imes) {
		settings->system_gps = true;
	}
}

static void gnss_settings_from_config(struct gnss_settings_state *out,
				      const meshbus_gnss_config *cfg)
{
	out->enabled = cfg->enabled;
	out->update_interval_ms = cfg->update_interval;
	out->min_active_time_ms = cfg->min_active_time;
	out->nav_mode_idx = (uint8_t)cfg->nav_mode;
	out->fix_rate_idx = gnss_fix_rate_to_idx(cfg->fix_rate);
	out->electronic_compass = cfg->electronic_compass;
	out->time_sync = cfg->time_sync;
	out->system_mode_idx =
		(cfg->system_mask == 0xFFU) ? GNSS_SYSTEM_MODE_ALL : GNSS_SYSTEM_MODE_CUSTOM;
	gnss_systems_from_mask(out, cfg->system_mask);
	gnss_settings_sanitize(out);
}

static void gnss_config_from_settings(meshbus_gnss_config *out, const meshbus_gnss_config *base,
				      const struct gnss_settings_state *in)
{
	struct gnss_settings_state sanitized = *in;

	*out = *base;
	gnss_settings_sanitize(&sanitized);
	out->enabled = sanitized.enabled;
	out->update_interval = sanitized.update_interval_ms;
	out->min_active_time = sanitized.min_active_time_ms;
	out->nav_mode = (meshbus_GnssConfig_GnssNavMode)sanitized.nav_mode_idx;
	out->fix_rate = gnss_fix_rate_from_idx(sanitized.fix_rate_idx);
	out->has_electronic_compass = true;
	out->electronic_compass = sanitized.electronic_compass;
	out->time_sync = sanitized.time_sync;
	out->system_mask = sanitized.system_mode_idx == GNSS_SYSTEM_MODE_ALL ?
				   0xFFU :
				   gnss_system_mask_from_settings(&sanitized);
}

static void gnss_default_settings(struct gnss_settings_state *settings)
{
	meshbus_gnss_config cfg = {
		.enabled = IS_ENABLED(CONFIG_MESHBUS_GNSS_DEFAULT_ENABLED),
		.update_interval = CONFIG_MESHBUS_GNSS_DEFAULT_UPDATE_INTERVAL,
		.nav_mode = CONFIG_MESHBUS_GNSS_DEFAULT_NAV_MODE,
		.fix_rate = CONFIG_MESHBUS_GNSS_DEFAULT_FIX_RATE,
		.system_mask = CONFIG_MESHBUS_GNSS_DEFAULT_SYSTEM_MASK,
		.min_active_time = CONFIG_MESHBUS_GNSS_DEFAULT_MIN_ACTIVE_TIME,
		.time_sync = IS_ENABLED(CONFIG_MESHBUS_GNSS_DEFAULT_TIME_SYNC),
		.has_electronic_compass = true,
		.electronic_compass = true,
	};

	gnss_settings_from_config(settings, &cfg);
}

void gnss_load_settings(struct gnss_app *app)
{
	meshbus_gnss_config cfg;

	if (meshbus_gnss_config_get(&cfg) == 0) {
		gnss_settings_from_config(&app->applied, &cfg);
	} else {
		gnss_default_settings(&app->applied);
	}
	gnss_settings_sanitize(&app->applied);
	app->editing = app->applied;
}

void gnss_form_add(struct gnss_app *app, uint32_t id, const char *label,
			  const char *const *options, size_t option_count, size_t option_index)
{
	if (app->form_item_count >= ARRAY_SIZE(app->form_items)) {
		return;
	}

	app->form_items[app->form_item_count++] = (struct zui_form_item){
		.id = id,
		.label = label,
		.options = options,
		.option_count = option_count,
		.option_index = option_index,
	};
}

void gnss_form_add_value(struct gnss_app *app, uint32_t id, const char *label,
				const char *value)
{
	if (app->form_item_count >= ARRAY_SIZE(app->form_items)) {
		return;
	}

	app->form_items[app->form_item_count++] = (struct zui_form_item){
		.id = id,
		.label = label,
		.value_text = value,
	};
}

void gnss_form_refresh(struct gnss_app *app);

void gnss_form_changed(struct zui_form *form, uint32_t id, size_t option_index,
			      void *user_data)
{
	struct gnss_app *app = user_data;

	ARG_UNUSED(form);

	switch (id) {
	case GNSS_FORM_ENABLED:
		app->editing.enabled = option_index != 0U;
		break;
	case GNSS_FORM_NAV_MODE:
		app->editing.nav_mode_idx = option_index;
		break;
	case GNSS_FORM_FIX_RATE:
		app->editing.fix_rate_idx = option_index;
		break;
	case GNSS_FORM_SYSTEM_MODE:
		app->editing.system_mode_idx = option_index;
		gnss_settings_sanitize(&app->editing);
		gnss_form_refresh(app);
		break;
	case GNSS_FORM_SYSTEM_GPS:
		app->editing.system_gps = option_index != 0U;
		break;
	case GNSS_FORM_SYSTEM_GLONASS:
		app->editing.system_glonass = option_index != 0U;
		break;
	case GNSS_FORM_SYSTEM_GALILEO:
		app->editing.system_galileo = option_index != 0U;
		break;
	case GNSS_FORM_SYSTEM_BEIDOU:
		app->editing.system_beidou = option_index != 0U;
		break;
	case GNSS_FORM_SYSTEM_QZSS:
		app->editing.system_qzss = option_index != 0U;
		break;
	case GNSS_FORM_SYSTEM_IRNSS:
		app->editing.system_irnss = option_index != 0U;
		break;
	case GNSS_FORM_SYSTEM_SBAS:
		app->editing.system_sbas = option_index != 0U;
		break;
	case GNSS_FORM_SYSTEM_IMES:
		app->editing.system_imes = option_index != 0U;
		break;
	case GNSS_FORM_TIME_SYNC:
		app->editing.time_sync = option_index != 0U;
		break;
	case GNSS_FORM_ELECTRONIC_COMPASS:
		app->editing.electronic_compass = option_index != 0U;
		break;
	default:
		break;
	}
}

void gnss_form_refresh(struct gnss_app *app)
{
	gnss_settings_sanitize(&app->editing);
	(void)snprintk(app->value_bufs[0], sizeof(app->value_bufs[0]), "%u",
		       (unsigned int)app->editing.update_interval_ms);
	(void)snprintk(app->value_bufs[1], sizeof(app->value_bufs[1]), "%u",
		       (unsigned int)app->editing.min_active_time_ms);

	app->form_item_count = 0U;
	gnss_form_add(app, GNSS_FORM_ENABLED, DESKTOP_TEXT_GNSS_SETTINGS_ENABLED,
		      DESKTOP_TEXT_COMMON_NO_YES_VALUES, 2U, app->editing.enabled ? 1U : 0U);
	gnss_form_add_value(app, GNSS_FORM_UPDATE_INTERVAL,
			    DESKTOP_TEXT_GNSS_SETTINGS_UPDATE_INTERVAL, app->value_bufs[0]);
	gnss_form_add_value(app, GNSS_FORM_MIN_ACTIVE_TIME,
			    DESKTOP_TEXT_GNSS_SETTINGS_MIN_ACTIVE, app->value_bufs[1]);
	gnss_form_add(app, GNSS_FORM_NAV_MODE, DESKTOP_TEXT_GNSS_SETTINGS_NAV_MODE,
		      DESKTOP_TEXT_GNSS_NAV_MODE_VALUES, DESKTOP_TEXT_GNSS_NAV_MODE_COUNT,
		      app->editing.nav_mode_idx);
	gnss_form_add(app, GNSS_FORM_FIX_RATE, DESKTOP_TEXT_GNSS_SETTINGS_FIX_RATE,
		      DESKTOP_TEXT_GNSS_FIX_RATE_VALUES, DESKTOP_TEXT_GNSS_FIX_RATE_COUNT,
		      app->editing.fix_rate_idx);
	gnss_form_add(app, GNSS_FORM_SYSTEM_MODE, DESKTOP_TEXT_GNSS_SETTINGS_SYSTEM,
		      DESKTOP_TEXT_GNSS_SYSTEM_MODE_VALUES, DESKTOP_TEXT_GNSS_SYSTEM_MODE_COUNT,
		      app->editing.system_mode_idx);
	if (app->editing.system_mode_idx == GNSS_SYSTEM_MODE_CUSTOM) {
		gnss_form_add(app, GNSS_FORM_SYSTEM_GPS, DESKTOP_TEXT_GNSS_SETTINGS_GPS,
			      DESKTOP_TEXT_COMMON_BOOL_VALUES, 2U,
			      app->editing.system_gps ? 1U : 0U);
		gnss_form_add(app, GNSS_FORM_SYSTEM_GLONASS, DESKTOP_TEXT_GNSS_SETTINGS_GLONASS,
			      DESKTOP_TEXT_COMMON_BOOL_VALUES, 2U,
			      app->editing.system_glonass ? 1U : 0U);
		gnss_form_add(app, GNSS_FORM_SYSTEM_GALILEO, DESKTOP_TEXT_GNSS_SETTINGS_GALILEO,
			      DESKTOP_TEXT_COMMON_BOOL_VALUES, 2U,
			      app->editing.system_galileo ? 1U : 0U);
		gnss_form_add(app, GNSS_FORM_SYSTEM_BEIDOU, DESKTOP_TEXT_GNSS_SETTINGS_BEIDOU,
			      DESKTOP_TEXT_COMMON_BOOL_VALUES, 2U,
			      app->editing.system_beidou ? 1U : 0U);
		gnss_form_add(app, GNSS_FORM_SYSTEM_QZSS, DESKTOP_TEXT_GNSS_SETTINGS_QZSS,
			      DESKTOP_TEXT_COMMON_BOOL_VALUES, 2U,
			      app->editing.system_qzss ? 1U : 0U);
		gnss_form_add(app, GNSS_FORM_SYSTEM_IRNSS, DESKTOP_TEXT_GNSS_SETTINGS_IRNSS,
			      DESKTOP_TEXT_COMMON_BOOL_VALUES, 2U,
			      app->editing.system_irnss ? 1U : 0U);
		gnss_form_add(app, GNSS_FORM_SYSTEM_SBAS, DESKTOP_TEXT_GNSS_SETTINGS_SBAS,
			      DESKTOP_TEXT_COMMON_BOOL_VALUES, 2U,
			      app->editing.system_sbas ? 1U : 0U);
		gnss_form_add(app, GNSS_FORM_SYSTEM_IMES, DESKTOP_TEXT_GNSS_SETTINGS_IMES,
			      DESKTOP_TEXT_COMMON_BOOL_VALUES, 2U,
			      app->editing.system_imes ? 1U : 0U);
	}
	gnss_form_add(app, GNSS_FORM_ELECTRONIC_COMPASS,
		      DESKTOP_TEXT_GNSS_SETTINGS_ELECTRONIC_COMPASS,
		      DESKTOP_TEXT_COMMON_BOOL_VALUES, 2U,
		      app->editing.electronic_compass ? 1U : 0U);
	gnss_form_add(app, GNSS_FORM_TIME_SYNC, DESKTOP_TEXT_GNSS_SETTINGS_TIME_SYNC,
		      DESKTOP_TEXT_COMMON_BOOL_VALUES, 2U, app->editing.time_sync ? 1U : 0U);
	gnss_form_add_value(app, GNSS_FORM_APPLY, DESKTOP_TEXT_COMMON_ACTION_APPLY, NULL);
	gnss_form_add_value(app, GNSS_FORM_RESET, DESKTOP_TEXT_COMMON_ACTION_RESET, NULL);

	(void)zui_form_update(app->settings_form, &(struct zui_form_config){
		.title = DESKTOP_TEXT_GNSS_TITLE,
		.items = app->form_items,
		.item_count = app->form_item_count,
		.changed = gnss_form_changed,
		.activated = gnss_form_activated,
		.user_data = app,
	});
}

void gnss_open_number(struct gnss_app *app, enum gnss_number_field field)
{
	const char *title = DESKTOP_TEXT_GNSS_HEADER_UPDATE_INTERVAL_MS;
	uint32_t min = GNSS_UPDATE_INTERVAL_MIN;
	uint32_t max = GNSS_UPDATE_INTERVAL_MAX;
	uint32_t value = app->editing.update_interval_ms;

	app->number_field = field;
	if (field == GNSS_NUMBER_MIN_ACTIVE_TIME) {
		title = DESKTOP_TEXT_GNSS_HEADER_MIN_ACTIVE_MS;
		min = GNSS_MIN_ACTIVE_TIME_MIN;
		max = GNSS_MIN_ACTIVE_TIME_MAX;
		value = app->editing.min_active_time_ms;
	}

	(void)zui_number_editor_update(app->number_editor, &(struct zui_number_editor_config){
		.title = title,
		.value = value,
		.min_value = min,
		.max_value = max,
		.max_digits = 8U,
		.unsigned_only = true,
		.submitted = gnss_number_submitted,
		.user_data = app,
	});
	gnss_switch(app, GNSS_APP_SCREEN_NUMBER);
}

void gnss_apply_settings(struct gnss_app *app)
{
	meshbus_gnss_config base;
	meshbus_gnss_config next;
	int rc;

	rc = meshbus_gnss_config_get(&base);
	if (rc == 0) {
		gnss_config_from_settings(&next, &base, &app->editing);
		rc = meshbus_gnss_config_set_full(&next);
	}
	if (rc == 0) {
		app->applied = app->editing;
		gnss_settings_sanitize(&app->applied);
		gnss_toast(app, DESKTOP_TEXT_COMMON_SETTINGS_APPLIED, &I_save_24x24, 900U);
		gnss_switch(app, GNSS_APP_SCREEN_MENU);
	} else {
		LOG_WRN("GNSS settings apply failed: %d", rc);
		gnss_toast(app, DESKTOP_TEXT_COMMON_SETTINGS_INVALID, &I_error_24x24, 1500U);
	}
}

static void gnss_reset_confirmed(struct gnss_app *app)
{
	int rc;

	if (app == NULL) {
		return;
	}

	rc = meshbus_gnss_config_reset();
	if (rc == 0) {
		gnss_load_settings(app);
		gnss_toast(app, DESKTOP_TEXT_COMMON_SETTINGS_RESET, &I_save_24x24, 900U);
		gnss_switch(app, GNSS_APP_SCREEN_MENU);
	} else {
		LOG_WRN("GNSS settings reset failed: %d", rc);
		gnss_toast(app, DESKTOP_TEXT_COMMON_SETTINGS_INVALID, &I_error_24x24, 1500U);
		gnss_switch(app, GNSS_APP_SCREEN_SETTINGS);
	}
}

static void gnss_open_reset_modal(struct gnss_app *app)
{
	if (app == NULL) {
		return;
	}

	(void)zui_modal_update(app->reset_modal, &(struct zui_modal_config){
		.title = DESKTOP_TEXT_COMMON_RESET,
		.text = DESKTOP_TEXT_COMMON_RESET_CONFIRM_TEXT,
		.left_button = DESKTOP_TEXT_COMMON_CANCEL,
		.right_button = DESKTOP_TEXT_COMMON_OK,
		.result = gnss_reset_modal_result,
		.user_data = app,
	});
	gnss_switch(app, GNSS_APP_SCREEN_RESET);
}

void gnss_form_activated(struct zui_form *form, uint32_t id,
				const struct zui_input_event *event, void *user_data)
{
	struct gnss_app *app = user_data;

	ARG_UNUSED(form);
	ARG_UNUSED(event);

	switch (id) {
	case GNSS_FORM_UPDATE_INTERVAL:
		gnss_open_number(app, GNSS_NUMBER_UPDATE_INTERVAL);
		break;
	case GNSS_FORM_MIN_ACTIVE_TIME:
		gnss_open_number(app, GNSS_NUMBER_MIN_ACTIVE_TIME);
		break;
	case GNSS_FORM_APPLY:
		gnss_apply_settings(app);
		break;
	case GNSS_FORM_RESET:
		gnss_open_reset_modal(app);
		break;
	default:
		break;
	}
}
void gnss_open_settings(struct gnss_app *app)
{
	gnss_load_settings(app);
	gnss_form_refresh(app);
	(void)zui_form_update(app->settings_form, &(struct zui_form_config){
		.title = DESKTOP_TEXT_GNSS_TITLE,
		.items = app->form_items,
		.item_count = app->form_item_count,
		.changed = gnss_form_changed,
		.activated = gnss_form_activated,
		.user_data = app,
	});
	gnss_switch(app, GNSS_APP_SCREEN_SETTINGS);
}
static void gnss_settings_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct gnss_app *app = user_data;

	(void)zui_screen_draw(zui_form_get_screen(app->settings_form), draw);
}

static bool gnss_settings_input(const struct zui_input_event *event, void *user_data)
{
	struct gnss_app *app = user_data;
	int ret;

	if (app == NULL || event == NULL) {
		return false;
	}
	if (gnss_should_consume_edge(event)) {
		return true;
	}
	if (gnss_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		app->editing = app->applied;
		gnss_switch(app, GNSS_APP_SCREEN_MENU);
		return true;
	}
	if (gnss_is_long(event) && event->code == ZUI_INPUT_CODE_SELECT) {
		gnss_apply_settings(app);
		return true;
	}
	ret = zui_screen_submit_input(zui_form_get_screen(app->settings_form), event);
	if (ret > 0) {
		gnss_request_redraw(app);
		return true;
	}

	return false;
}

void gnss_number_submitted(struct zui_number_editor *editor, int64_t value,
				  void *user_data)
{
	struct gnss_app *app = user_data;

	ARG_UNUSED(editor);

	if (app == NULL) {
		return;
	}
	if (app->number_field == GNSS_NUMBER_UPDATE_INTERVAL) {
		app->editing.update_interval_ms =
			CLAMP((uint32_t)value, GNSS_UPDATE_INTERVAL_MIN, GNSS_UPDATE_INTERVAL_MAX);
	} else {
		app->editing.min_active_time_ms =
			CLAMP((uint32_t)value, GNSS_MIN_ACTIVE_TIME_MIN, GNSS_MIN_ACTIVE_TIME_MAX);
	}
	gnss_form_refresh(app);
	gnss_switch(app, GNSS_APP_SCREEN_SETTINGS);
}

static void gnss_number_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct gnss_app *app = user_data;

	(void)zui_screen_draw(zui_number_editor_get_screen(app->number_editor), draw);
}

static bool gnss_number_input(const struct zui_input_event *event, void *user_data)
{
	struct gnss_app *app = user_data;
	int ret;

	if (app == NULL || event == NULL) {
		return false;
	}
	if (gnss_should_consume_edge(event)) {
		return true;
	}
	if (gnss_is_long(event) && event->code == ZUI_INPUT_CODE_BACK) {
		gnss_switch(app, GNSS_APP_SCREEN_SETTINGS);
		return true;
	}
	ret = zui_screen_submit_input(zui_number_editor_get_screen(app->number_editor), event);
	if (ret > 0) {
		gnss_request_redraw(app);
		return true;
	}

	return false;
}

void gnss_reset_modal_result(struct zui_modal *modal, enum zui_modal_result result,
			     const struct zui_input_event *event, void *user_data)
{
	struct gnss_app *app = user_data;

	ARG_UNUSED(modal);
	ARG_UNUSED(event);

	if (app == NULL) {
		return;
	}

	if (result == ZUI_MODAL_RESULT_RIGHT) {
		gnss_reset_confirmed(app);
	} else {
		gnss_switch(app, GNSS_APP_SCREEN_SETTINGS);
	}
}

static void gnss_reset_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct gnss_app *app = user_data;

	(void)zui_screen_draw(zui_modal_get_screen(app->reset_modal), draw);
}

static bool gnss_reset_input(const struct zui_input_event *event, void *user_data)
{
	struct gnss_app *app = user_data;
	int ret;

	if (app == NULL || event == NULL) {
		return false;
	}
	if (gnss_should_consume_edge(event)) {
		return true;
	}
	if (gnss_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		gnss_reset_modal_result(app->reset_modal, ZUI_MODAL_RESULT_LEFT, event, app);
		return true;
	}
	ret = zui_screen_submit_input(zui_modal_get_screen(app->reset_modal), event);
	if (ret > 0) {
		gnss_request_redraw(app);
		return true;
	}

	return false;
}

const struct zui_screen_ops gnss_settings_ops = {
	.draw = gnss_settings_draw,
	.input = gnss_settings_input,
};
const struct zui_screen_ops gnss_number_ops = {
	.draw = gnss_number_draw,
	.input = gnss_number_input,
};
const struct zui_screen_ops gnss_reset_ops = {
	.draw = gnss_reset_draw,
	.input = gnss_reset_input,
};
