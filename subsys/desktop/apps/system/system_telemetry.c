/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include "system_private.h"

#include "services/telemetry_cache.h"

LOG_MODULE_DECLARE(mbs_desktop_system_app, CONFIG_MBS_DESKTOP_LOG_LEVEL);

#if defined(CONFIG_MBS_TELEMETRY)

static const char *telemetry_channel_name(enum sensor_channel chan)
{
	switch (chan) {
	case SENSOR_CHAN_ACCEL_XYZ:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_ACCEL;
	case SENSOR_CHAN_GYRO_XYZ:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_GYRO;
	case SENSOR_CHAN_MAGN_XYZ:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_MAGN;
	case SENSOR_CHAN_DIE_TEMP:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_DIE_TEMP;
	case SENSOR_CHAN_AMBIENT_TEMP:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_AMBIENT;
	case SENSOR_CHAN_PRESS:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_PRESS;
	case SENSOR_CHAN_HUMIDITY:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_HUMIDITY;
	case SENSOR_CHAN_AMBIENT_LIGHT:
	case SENSOR_CHAN_LIGHT:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_LIGHT;
	case SENSOR_CHAN_VOLTAGE:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_VOLTAGE;
	case SENSOR_CHAN_CURRENT:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_CURRENT;
	case SENSOR_CHAN_GAUGE_STATE_OF_CHARGE:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_BATTERY;
	default:
		return DESKTOP_TEXT_WIDGET_TELEMETRY_LABEL_CHANNEL;
	}
}

static void telemetry_strcpy(char *dst, size_t dst_size, const char *src)
{
	if (dst == NULL || dst_size == 0U) {
		return;
	}
	if (src == NULL) {
		dst[0] = '\0';
		return;
	}

	(void)strncpy(dst, src, dst_size - 1U);
	dst[dst_size - 1U] = '\0';
}

static void telemetry_strappend(char *out, size_t out_size, const char *text)
{
	size_t off;

	if (out == NULL || out_size == 0U || text == NULL) {
		return;
	}

	off = strnlen(out, out_size);
	for (size_t i = 0U; text[i] != '\0' && off + 1U < out_size; i++) {
		out[off++] = text[i];
	}
	out[off] = '\0';
}

static void telemetry_build_reading_label(char *out, size_t out_size, const char *base,
					  uint8_t index, uint8_t count)
{
	uint8_t axis_index;

	if (out == NULL || out_size == 0U) {
		return;
	}

	telemetry_strcpy(out, out_size, base);
	if (count <= 1U) {
		return;
	}

	telemetry_strappend(out, out_size, " ");
	axis_index = MIN(index, (uint8_t)(DESKTOP_TEXT_WIDGET_TELEMETRY_AXIS_COUNT - 1U));
	telemetry_strappend(out, out_size,
			    DESKTOP_TEXT_WIDGET_TELEMETRY_AXIS_VALUES[axis_index]);
}

static void telemetry_format_sensor_value(char *out, size_t out_size,
					  const struct sensor_value *value)
{
	int64_t micro;
	uint64_t abs_micro;
	bool neg;

	if (out == NULL || out_size == 0U) {
		return;
	}
	if (value == NULL) {
		(void)snprintk(out, out_size, "%s", DESKTOP_TEXT_COMMON_NOT_AVAILABLE);
		return;
	}

	micro = sensor_value_to_micro(value);
	neg = micro < 0;
	abs_micro = (uint64_t)(neg ? -micro : micro);
	(void)snprintk(out, out_size, "%s%llu.%02llu", neg ? "-" : "",
		       (unsigned long long)(abs_micro / 1000000ULL),
		       (unsigned long long)((abs_micro % 1000000ULL) / 10000ULL));
}

static void telemetry_format_age(char *out, size_t out_size, uint32_t timestamp)
{
	uint32_t age_ms;

	if (out == NULL || out_size == 0U) {
		return;
	}
	if (timestamp == 0U) {
		(void)snprintk(out, out_size, "%s", DESKTOP_TEXT_COMMON_NOT_AVAILABLE);
		return;
	}

	age_ms = k_uptime_get_32() - timestamp;
	if (age_ms < 1000U) {
		(void)snprintk(out, out_size, "%u ms", (unsigned int)age_ms);
	} else if (age_ms < 60000U) {
		(void)snprintk(out, out_size, "%u s", (unsigned int)(age_ms / 1000U));
	} else {
		(void)snprintk(out, out_size, "%u m", (unsigned int)(age_ms / 60000U));
	}
}

static void telemetry_toast(struct system_app *app, const char *text,
			    const struct zui_icon *icon, uint32_t timeout_ms)
{
	if (app == NULL || app->ctx.host == NULL) {
		return;
	}

	(void)zui_toast_show(app->ctx.host, &(struct zui_toast_config){
		.title = DESKTOP_TEXT_TELEMETRY_TITLE,
		.text = text,
		.icon = icon,
		.timeout_ms = timeout_ms,
	});
}

static void telemetry_readings_destroy(struct system_app *app)
{
	if (app == NULL) {
		return;
	}

	if (app->ctx.router != NULL && app->telemetry_readings_screen != NULL) {
		(void)zui_router_unregister_screen(app->ctx.router,
						   SYSTEM_SCREEN_TELEMETRY_READINGS);
	}
	if (app->telemetry_readings_screen != NULL) {
		zui_screen_destroy(app->telemetry_readings_screen);
		app->telemetry_readings_screen = NULL;
	}
	if (app->telemetry_readings != NULL) {
		zui_sublist_destroy(app->telemetry_readings);
		app->telemetry_readings = NULL;
	}
}

static int telemetry_readings_ensure(struct system_app *app)
{
	int ret;

	if (app == NULL) {
		return -EINVAL;
	}
	if (app->telemetry_readings != NULL && app->telemetry_readings_screen != NULL) {
		return 0;
	}

	telemetry_readings_destroy(app);
	app->telemetry_readings = zui_sublist_create(&(struct zui_sublist_config){
		.title = DESKTOP_TEXT_TELEMETRY_READINGS,
		.items = app->telemetry_reading_items,
		.item_count = 0U,
		.user_data = app,
	});
	app->telemetry_readings_screen = zui_screen_create(&system_telemetry_readings_ops, app);
	if (app->telemetry_readings == NULL || app->telemetry_readings_screen == NULL) {
		telemetry_readings_destroy(app);
		return -ENOMEM;
	}

	ret = zui_router_register_screen(app->ctx.router, SYSTEM_SCREEN_TELEMETRY_READINGS,
					 app->telemetry_readings_screen);
	if (ret != 0) {
		telemetry_readings_destroy(app);
		return ret;
	}

	return 0;
}

static void telemetry_settings_destroy(struct system_app *app)
{
	if (app == NULL) {
		return;
	}

	if (app->ctx.router != NULL && app->telemetry_settings_screen != NULL) {
		(void)zui_router_unregister_screen(app->ctx.router,
						   SYSTEM_SCREEN_TELEMETRY_SETTINGS);
	}
	if (app->ctx.router != NULL && app->telemetry_number_screen != NULL) {
		(void)zui_router_unregister_screen(app->ctx.router,
						   SYSTEM_SCREEN_TELEMETRY_NUMBER);
	}
	if (app->ctx.router != NULL && app->telemetry_reset_screen != NULL) {
		(void)zui_router_unregister_screen(app->ctx.router,
						   SYSTEM_SCREEN_TELEMETRY_RESET);
	}
	if (app->telemetry_settings_screen != NULL) {
		zui_screen_destroy(app->telemetry_settings_screen);
		app->telemetry_settings_screen = NULL;
	}
	if (app->telemetry_number_screen != NULL) {
		zui_screen_destroy(app->telemetry_number_screen);
		app->telemetry_number_screen = NULL;
	}
	if (app->telemetry_reset_screen != NULL) {
		zui_screen_destroy(app->telemetry_reset_screen);
		app->telemetry_reset_screen = NULL;
	}
	if (app->telemetry_reset_modal != NULL) {
		zui_modal_destroy(app->telemetry_reset_modal);
		app->telemetry_reset_modal = NULL;
	}
	if (app->telemetry_number_editor != NULL) {
		zui_number_editor_destroy(app->telemetry_number_editor);
		app->telemetry_number_editor = NULL;
	}
	if (app->telemetry_settings_form != NULL) {
		zui_form_destroy(app->telemetry_settings_form);
		app->telemetry_settings_form = NULL;
	}
}

static int telemetry_settings_ensure(struct system_app *app)
{
	int ret;

	if (app == NULL) {
		return -EINVAL;
	}
	if (app->telemetry_settings_form != NULL &&
	    app->telemetry_number_editor != NULL &&
	    app->telemetry_reset_modal != NULL &&
	    app->telemetry_settings_screen != NULL &&
	    app->telemetry_number_screen != NULL &&
	    app->telemetry_reset_screen != NULL) {
		return 0;
	}

	telemetry_settings_destroy(app);
	app->telemetry_settings_form = zui_form_create(&(struct zui_form_config){
		.title = DESKTOP_TEXT_TELEMETRY_TITLE,
		.items = app->telemetry_form_items,
		.item_count = 0U,
		.user_data = app,
	});
	app->telemetry_number_editor =
		zui_number_editor_create(&(struct zui_number_editor_config){
			.title = DESKTOP_TEXT_TELEMETRY_HEADER_INTERVAL_MS,
			.min_value = CONFIG_MBS_TELEMETRY_MIN_SAMPLE_INTERVAL,
			.max_value = UINT32_MAX,
			.max_digits = SYSTEM_TELEMETRY_INTERVAL_MAX_DIGITS,
			.unsigned_only = true,
			.submitted = system_telemetry_number_submitted,
			.user_data = app,
		});
	app->telemetry_reset_modal = zui_modal_create(&(struct zui_modal_config){
		.title = DESKTOP_TEXT_COMMON_RESET,
		.text = DESKTOP_TEXT_COMMON_RESET_CONFIRM_TEXT,
		.left_button = DESKTOP_TEXT_COMMON_CANCEL,
		.right_button = DESKTOP_TEXT_COMMON_OK,
		.result = system_telemetry_reset_modal_result,
		.user_data = app,
	});
	app->telemetry_settings_screen = zui_screen_create(&system_telemetry_settings_ops, app);
	app->telemetry_number_screen = zui_screen_create(&system_telemetry_number_ops, app);
	app->telemetry_reset_screen = zui_screen_create(&system_telemetry_reset_ops, app);
	if (app->telemetry_settings_form == NULL ||
	    app->telemetry_number_editor == NULL ||
	    app->telemetry_reset_modal == NULL ||
	    app->telemetry_settings_screen == NULL ||
	    app->telemetry_number_screen == NULL ||
	    app->telemetry_reset_screen == NULL) {
		telemetry_settings_destroy(app);
		return -ENOMEM;
	}

	ret = zui_router_register_screen(app->ctx.router, SYSTEM_SCREEN_TELEMETRY_SETTINGS,
					 app->telemetry_settings_screen);
	if (ret != 0) {
		telemetry_settings_destroy(app);
		return ret;
	}
	ret = zui_router_register_screen(app->ctx.router, SYSTEM_SCREEN_TELEMETRY_NUMBER,
					 app->telemetry_number_screen);
	if (ret != 0) {
		telemetry_settings_destroy(app);
		return ret;
	}
	ret = zui_router_register_screen(app->ctx.router, SYSTEM_SCREEN_TELEMETRY_RESET,
					 app->telemetry_reset_screen);
	if (ret != 0) {
		telemetry_settings_destroy(app);
		return ret;
	}

	return 0;
}

void system_open_telemetry_menu(struct system_app *app)
{
	(void)zui_sublist_update(app->info_menu, &(struct zui_sublist_config){
		.title = DESKTOP_TEXT_TELEMETRY_TITLE,
		.items = app->telemetry_menu_items,
		.item_count = ARRAY_SIZE(app->telemetry_menu_items),
		.selected = system_telemetry_menu_selected,
		.user_data = app,
	});
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_INFO_MENU);
	telemetry_readings_destroy(app);
	telemetry_settings_destroy(app);
}

static void telemetry_trigger_sample(struct system_app *app)
{
	int rc = mbs_telemetry_sample_trigger();

	if (rc == 0) {
		telemetry_toast(app, DESKTOP_TEXT_TELEMETRY_SAMPLE_TRIGGERED,
				&I_done_24x24, 900U);
	} else if (rc == -ENODEV) {
		telemetry_toast(app, DESKTOP_TEXT_TELEMETRY_SAMPLE_DISABLED,
				&I_error_24x24, 1500U);
	} else {
		LOG_WRN("Telemetry sample trigger failed: %d", rc);
		telemetry_toast(app, DESKTOP_TEXT_TELEMETRY_SAMPLE_FAILED,
				&I_error_24x24, 1500U);
	}
}

static void telemetry_readings_append(struct system_app *app,
				      const struct mbs_telemetry_binding *binding,
				      const struct desktop_telemetry_cache_entry *entry,
				      uint8_t value_idx, uint8_t value_count)
{
	size_t index;
	const char *sensor_name;

	if (app == NULL || binding == NULL ||
	    app->telemetry_reading_item_count >= ARRAY_SIZE(app->telemetry_reading_items)) {
		return;
	}

	index = app->telemetry_reading_item_count;
	telemetry_build_reading_label(app->telemetry_reading_labels[index],
				      sizeof(app->telemetry_reading_labels[index]),
				      telemetry_channel_name(binding->chan), value_idx,
				      value_count);
	sensor_name = binding->sensor_name != NULL ? binding->sensor_name :
						     DESKTOP_TEXT_COMMON_NOT_AVAILABLE;
	telemetry_strcpy(app->telemetry_reading_sensors[index], sizeof(app->telemetry_reading_sensors[index]),
			 sensor_name);

	if (entry != NULL && value_idx < entry->value_count) {
		telemetry_format_sensor_value(app->telemetry_reading_details[index],
					      sizeof(app->telemetry_reading_details[index]),
					      &entry->values[value_idx]);
	} else {
		telemetry_strcpy(app->telemetry_reading_details[index], sizeof(app->telemetry_reading_details[index]),
				 DESKTOP_TEXT_TELEMETRY_NO_DATA);
	}

	app->telemetry_reading_chans[index] = binding->chan;
	app->telemetry_reading_value_indices[index] = value_idx;
	app->telemetry_reading_items[index] = (struct zui_list_item){
		.id = (uint32_t)(index + 1U),
		.label = app->telemetry_reading_labels[index],
		.detail = app->telemetry_reading_details[index],
	};
	app->telemetry_reading_item_count++;
}

static void telemetry_readings_refresh(struct system_app *app)
{
	size_t binding_count;

	if (app == NULL) {
		return;
	}

	binding_count = mbs_telemetry_bindings_count();
	app->telemetry_reading_item_count = 0U;
	for (size_t i = 0U; i < binding_count &&
			    app->telemetry_reading_item_count < ARRAY_SIZE(app->telemetry_reading_items);
	     i++) {
		struct mbs_telemetry_binding binding;
		struct desktop_telemetry_cache_entry entry;
		bool has_entry;
		uint8_t value_count;

		if (mbs_telemetry_binding_get(i, &binding) != 0) {
			continue;
		}

		has_entry = desktop_telemetry_cache_get(binding.chan, &entry);
		value_count = (uint8_t)MIN(mbs_telemetry_channel_value_count(binding.chan),
					   (size_t)MBS_TELEMETRY_MAX_VALUES);
		if (has_entry && entry.value_count > 0U) {
			value_count = MIN(entry.value_count, (uint8_t)MBS_TELEMETRY_MAX_VALUES);
		}
		value_count = MAX(value_count, 1U);

		for (uint8_t value_idx = 0U;
		     value_idx < value_count &&
		     app->telemetry_reading_item_count < ARRAY_SIZE(app->telemetry_reading_items);
		     value_idx++) {
			telemetry_readings_append(app, &binding, has_entry ? &entry : NULL,
						  value_idx, value_count);
		}
	}

	if (app->telemetry_reading_item_count == 0U) {
		(void)snprintk(app->telemetry_reading_labels[0], sizeof(app->telemetry_reading_labels[0]), "%s",
			       DESKTOP_TEXT_TELEMETRY_NO_READINGS);
		(void)snprintk(app->telemetry_reading_details[0], sizeof(app->telemetry_reading_details[0]), "%s",
			       DESKTOP_TEXT_COMMON_EMPTY);
		app->telemetry_reading_items[0] = (struct zui_list_item){
			.id = SYSTEM_TELEMETRY_READING_PLACEHOLDER,
			.label = app->telemetry_reading_labels[0],
			.detail = app->telemetry_reading_details[0],
		};
		app->telemetry_reading_item_count = 1U;
	}

	(void)zui_sublist_update(app->telemetry_readings, &(struct zui_sublist_config){
		.title = DESKTOP_TEXT_TELEMETRY_READINGS,
		.items = app->telemetry_reading_items,
		.item_count = app->telemetry_reading_item_count,
		.selected = NULL,
		.user_data = app,
	});
}

static void telemetry_open_readings(struct system_app *app)
{
	int rc;

	telemetry_settings_destroy(app);
	rc = telemetry_readings_ensure(app);
	if (rc != 0) {
		LOG_WRN("Telemetry readings open failed: %d", rc);
		telemetry_toast(app, DESKTOP_TEXT_TELEMETRY_OPEN_FAILED,
				&I_error_24x24, 1500U);
		return;
	}

	telemetry_readings_refresh(app);
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_TELEMETRY_READINGS);
}

static void telemetry_open_detail(struct system_app *app, size_t index)
{
	struct desktop_telemetry_cache_entry entry;
	char value_text[SYSTEM_TELEMETRY_VALUE_TEXT_MAX];
	char age_text[SYSTEM_TELEMETRY_AGE_TEXT_MAX];
	enum sensor_channel chan;
	uint8_t value_idx;
	bool has_entry;

	if (app == NULL || index >= app->telemetry_reading_item_count ||
	    app->telemetry_reading_items[index].id == SYSTEM_TELEMETRY_READING_PLACEHOLDER) {
		return;
	}

	chan = app->telemetry_reading_chans[index];
	value_idx = app->telemetry_reading_value_indices[index];
	has_entry = desktop_telemetry_cache_get(chan, &entry);
	if (has_entry && value_idx < entry.value_count) {
		telemetry_format_sensor_value(value_text, sizeof(value_text),
					      &entry.values[value_idx]);
		telemetry_format_age(age_text, sizeof(age_text), entry.timestamp);
	} else {
		(void)snprintk(value_text, sizeof(value_text), "%s",
			       DESKTOP_TEXT_TELEMETRY_NO_DATA);
		(void)snprintk(age_text, sizeof(age_text), "%s",
			       DESKTOP_TEXT_COMMON_NOT_AVAILABLE);
		entry.timestamp = 0U;
	}

	(void)snprintk(app->text, sizeof(app->text),
		       DESKTOP_TEXT_TELEMETRY_READING_DETAIL_FORMAT, app->telemetry_reading_sensors[index],
		       telemetry_channel_name(chan), (unsigned int)chan,
		       app->telemetry_reading_labels[index], value_text,
		       (unsigned int)entry.timestamp, age_text);
	(void)zui_text_view_update(app->text_view, &(struct zui_text_view_config){
		.title = DESKTOP_TEXT_TELEMETRY_READINGS,
		.text = app->text,
		.font = ZUI_FONT_SECONDARY,
		.mode = ZUI_TEXT_VIEW_MODE_TEXT,
	});
	app->text_back_screen = SYSTEM_SCREEN_TELEMETRY_READINGS;
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_TEXT);
}

static void telemetry_load_settings(struct system_app *app)
{
	mbs_telemetry_config cfg;

	if (app == NULL) {
		return;
	}
	if (mbs_telemetry_config_get(&cfg) != 0) {
		cfg.enabled = IS_ENABLED(CONFIG_MBS_TELEMETRY_DEFAULT_ENABLED);
		cfg.sample_interval = CONFIG_MBS_TELEMETRY_DEFAULT_SAMPLE_INTERVAL;
	}
	if (cfg.sample_interval < CONFIG_MBS_TELEMETRY_MIN_SAMPLE_INTERVAL) {
		cfg.sample_interval = CONFIG_MBS_TELEMETRY_MIN_SAMPLE_INTERVAL;
	}
	app->telemetry_applied = cfg;
	app->telemetry_editing = cfg;
}

static void telemetry_settings_refresh(struct system_app *app)
{
	if (app == NULL) {
		return;
	}

	if (app->telemetry_editing.sample_interval < CONFIG_MBS_TELEMETRY_MIN_SAMPLE_INTERVAL) {
		app->telemetry_editing.sample_interval = CONFIG_MBS_TELEMETRY_MIN_SAMPLE_INTERVAL;
	}

	(void)snprintk(app->telemetry_interval_text, sizeof(app->telemetry_interval_text), "%u",
		       (unsigned int)app->telemetry_editing.sample_interval);
	app->telemetry_form_items[0] = (struct zui_form_item){
		.id = SYSTEM_TELEMETRY_FORM_ENABLED,
		.label = DESKTOP_TEXT_TELEMETRY_SETTINGS_ENABLED,
		.options = DESKTOP_TEXT_COMMON_NO_YES_VALUES,
		.option_count = 2U,
		.option_index = app->telemetry_editing.enabled ? 1U : 0U,
	};
	app->telemetry_form_items[1] = (struct zui_form_item){
		.id = SYSTEM_TELEMETRY_FORM_INTERVAL,
		.label = DESKTOP_TEXT_TELEMETRY_SETTINGS_INTERVAL,
		.value_text = app->telemetry_interval_text,
		.value_align = ZUI_FORM_VALUE_ALIGN_RIGHT,
	};
	app->telemetry_form_items[2] = (struct zui_form_item){
		.id = SYSTEM_TELEMETRY_FORM_APPLY,
		.label = DESKTOP_TEXT_COMMON_ACTION_APPLY,
	};
	app->telemetry_form_items[3] = (struct zui_form_item){
		.id = SYSTEM_TELEMETRY_FORM_RESET,
		.label = DESKTOP_TEXT_COMMON_ACTION_RESET,
	};

	(void)zui_form_update(app->telemetry_settings_form, &(struct zui_form_config){
		.title = DESKTOP_TEXT_TELEMETRY_TITLE,
		.items = app->telemetry_form_items,
		.item_count = ARRAY_SIZE(app->telemetry_form_items),
		.changed = NULL,
		.activated = NULL,
		.user_data = app,
	});
}

static void telemetry_open_settings(struct system_app *app)
{
	int rc;

	telemetry_readings_destroy(app);
	rc = telemetry_settings_ensure(app);
	if (rc != 0) {
		LOG_WRN("Telemetry settings open failed: %d", rc);
		telemetry_toast(app, DESKTOP_TEXT_TELEMETRY_OPEN_FAILED,
				&I_error_24x24, 1500U);
		return;
	}

	telemetry_load_settings(app);
	telemetry_settings_refresh(app);
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_TELEMETRY_SETTINGS);
}

static void telemetry_apply_settings(struct system_app *app)
{
	int rc;

	if (app == NULL) {
		return;
	}

	rc = mbs_telemetry_config_set(&app->telemetry_editing);
	if (rc == 0) {
		app->telemetry_applied = app->telemetry_editing;
		telemetry_toast(app, DESKTOP_TEXT_COMMON_SETTINGS_APPLIED,
				&I_save_24x24, 900U);
		system_open_telemetry_menu(app);
	} else {
		LOG_WRN("Telemetry settings apply failed: %d", rc);
		telemetry_toast(app, DESKTOP_TEXT_COMMON_SETTINGS_INVALID,
				&I_error_24x24, 1500U);
	}
}

static void telemetry_reset_confirmed(struct system_app *app)
{
	int rc;

	if (app == NULL) {
		return;
	}

	rc = mbs_telemetry_config_reset();
	if (rc == 0) {
		telemetry_load_settings(app);
		telemetry_toast(app, DESKTOP_TEXT_COMMON_SETTINGS_RESET,
				&I_save_24x24, 900U);
		system_open_telemetry_menu(app);
	} else {
		LOG_WRN("Telemetry settings reset failed: %d", rc);
		telemetry_toast(app, DESKTOP_TEXT_COMMON_SETTINGS_INVALID,
				&I_error_24x24, 1500U);
		desktop_app_switch(&app->ctx, SYSTEM_SCREEN_TELEMETRY_SETTINGS);
	}
}

static void telemetry_open_number(struct system_app *app)
{
	if (app == NULL) {
		return;
	}

	(void)zui_number_editor_update(app->telemetry_number_editor,
				       &(struct zui_number_editor_config){
		.title = DESKTOP_TEXT_TELEMETRY_HEADER_INTERVAL_MS,
		.value = app->telemetry_editing.sample_interval,
		.min_value = CONFIG_MBS_TELEMETRY_MIN_SAMPLE_INTERVAL,
		.max_value = UINT32_MAX,
		.max_digits = SYSTEM_TELEMETRY_INTERVAL_MAX_DIGITS,
		.unsigned_only = true,
		.submitted = system_telemetry_number_submitted,
		.user_data = app,
	});
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_TELEMETRY_NUMBER);
}

static void telemetry_open_reset(struct system_app *app)
{
	if (app == NULL) {
		return;
	}

	(void)zui_modal_update(app->telemetry_reset_modal, &(struct zui_modal_config){
		.title = DESKTOP_TEXT_COMMON_RESET,
		.text = DESKTOP_TEXT_COMMON_RESET_CONFIRM_TEXT,
		.left_button = DESKTOP_TEXT_COMMON_CANCEL,
		.right_button = DESKTOP_TEXT_COMMON_OK,
		.result = system_telemetry_reset_modal_result,
		.user_data = app,
	});
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_TELEMETRY_RESET);
}

void system_telemetry_menu_selected(struct zui_sublist *list, uint32_t id, size_t index,
				    const struct zui_input_event *event, void *user_data)
{
	struct system_app *app = user_data;

	ARG_UNUSED(list);
	ARG_UNUSED(index);
	ARG_UNUSED(event);

	if (app == NULL) {
		return;
	}

	switch (id) {
	case SYSTEM_TELEMETRY_MENU_READINGS:
		telemetry_open_readings(app);
		break;
	case SYSTEM_TELEMETRY_MENU_TRIGGER:
		telemetry_trigger_sample(app);
		break;
	case SYSTEM_TELEMETRY_MENU_SETTINGS:
		telemetry_open_settings(app);
		break;
	default:
		break;
	}
}

static void telemetry_readings_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct system_app *app = user_data;

	(void)zui_screen_draw(zui_sublist_get_screen(app->telemetry_readings), draw);
}

static bool telemetry_readings_input(const struct zui_input_event *event, void *user_data)
{
	struct system_app *app = user_data;
	size_t selected;
	int ret;

	if (app == NULL || event == NULL) {
		return false;
	}
	if (desktop_app_input_should_consume_edge(event)) {
		return true;
	}
	if (desktop_app_input_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		system_open_telemetry_menu(app);
		return true;
	}
	if (desktop_app_input_is_click(event) && event->code == ZUI_INPUT_CODE_SELECT) {
		selected = zui_sublist_selected(app->telemetry_readings);
		telemetry_open_detail(app, selected);
		return true;
	}

	ret = zui_screen_submit_input(zui_sublist_get_screen(app->telemetry_readings), event);
	if (ret > 0) {
		desktop_app_request_redraw(&app->ctx);
		return true;
	}

	return false;
}

static void telemetry_settings_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct system_app *app = user_data;

	(void)zui_screen_draw(zui_form_get_screen(app->telemetry_settings_form), draw);
}

static bool telemetry_settings_input(const struct zui_input_event *event, void *user_data)
{
	struct system_app *app = user_data;
	int ret;

	if (app == NULL || event == NULL) {
		return false;
	}
	if (desktop_app_input_should_consume_edge(event)) {
		return true;
	}
	if (desktop_app_input_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		app->telemetry_editing = app->telemetry_applied;
		system_open_telemetry_menu(app);
		return true;
	}
	if (desktop_app_input_is_long_press(event) && event->code == ZUI_INPUT_CODE_SELECT) {
		telemetry_apply_settings(app);
		return true;
	}

	ret = zui_screen_submit_input(zui_form_get_screen(app->telemetry_settings_form), event);
	if (ret > 0) {
		app->telemetry_editing.enabled =
			zui_form_option(app->telemetry_settings_form, SYSTEM_TELEMETRY_FORM_ENABLED) != 0U;
		if (desktop_app_input_is_click(event) && event->code == ZUI_INPUT_CODE_SELECT) {
			switch (app->telemetry_form_items[zui_form_selected(app->telemetry_settings_form)].id) {
			case SYSTEM_TELEMETRY_FORM_INTERVAL:
				telemetry_open_number(app);
				break;
			case SYSTEM_TELEMETRY_FORM_APPLY:
				telemetry_apply_settings(app);
				break;
			case SYSTEM_TELEMETRY_FORM_RESET:
				telemetry_open_reset(app);
				break;
			default:
				break;
			}
		}
		desktop_app_request_redraw(&app->ctx);
		return true;
	}

	return false;
}

static void telemetry_number_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct system_app *app = user_data;

	(void)zui_screen_draw(zui_number_editor_get_screen(app->telemetry_number_editor), draw);
}

static bool telemetry_number_input(const struct zui_input_event *event, void *user_data)
{
	struct system_app *app = user_data;
	int ret;

	if (app == NULL || event == NULL) {
		return false;
	}
	if (desktop_app_input_should_consume_edge(event)) {
		return true;
	}
	if (desktop_app_input_is_long_press(event) && event->code == ZUI_INPUT_CODE_BACK) {
		desktop_app_switch(&app->ctx, SYSTEM_SCREEN_TELEMETRY_SETTINGS);
		return true;
	}

	ret = zui_screen_submit_input(zui_number_editor_get_screen(app->telemetry_number_editor), event);
	if (ret > 0) {
		desktop_app_request_redraw(&app->ctx);
		return true;
	}

	return false;
}

void system_telemetry_number_submitted(struct zui_number_editor *editor, int64_t value,
				       void *user_data)
{
	struct system_app *app = user_data;

	ARG_UNUSED(editor);

	if (app == NULL) {
		return;
	}

	value = CLAMP(value, (int64_t)CONFIG_MBS_TELEMETRY_MIN_SAMPLE_INTERVAL,
		      (int64_t)UINT32_MAX);
	app->telemetry_editing.sample_interval = (uint32_t)value;
	telemetry_settings_refresh(app);
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_TELEMETRY_SETTINGS);
}

static void telemetry_reset_result(struct system_app *app, enum zui_modal_result result)
{
	if (result == ZUI_MODAL_RESULT_RIGHT) {
		telemetry_reset_confirmed(app);
	} else {
		desktop_app_switch(&app->ctx, SYSTEM_SCREEN_TELEMETRY_SETTINGS);
	}
}

void system_telemetry_reset_modal_result(struct zui_modal *modal,
					 enum zui_modal_result result,
					 const struct zui_input_event *event, void *user_data)
{
	struct system_app *app = user_data;

	ARG_UNUSED(modal);
	ARG_UNUSED(event);

	if (app != NULL) {
		telemetry_reset_result(app, result);
	}
}

static void telemetry_reset_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct system_app *app = user_data;

	(void)zui_screen_draw(zui_modal_get_screen(app->telemetry_reset_modal), draw);
}

static bool telemetry_reset_input(const struct zui_input_event *event, void *user_data)
{
	struct system_app *app = user_data;
	int ret;

	if (app == NULL || event == NULL) {
		return false;
	}
	if (desktop_app_input_should_consume_edge(event)) {
		return true;
	}
	if (desktop_app_input_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		telemetry_reset_result(app, ZUI_MODAL_RESULT_LEFT);
		return true;
	}

	ret = zui_screen_submit_input(zui_modal_get_screen(app->telemetry_reset_modal), event);
	if (ret > 0) {
		desktop_app_request_redraw(&app->ctx);
		return true;
	}

	return false;
}

const struct zui_screen_ops system_telemetry_readings_ops = {
	.draw = telemetry_readings_draw,
	.input = telemetry_readings_input,
};

const struct zui_screen_ops system_telemetry_settings_ops = {
	.draw = telemetry_settings_draw,
	.input = telemetry_settings_input,
};

const struct zui_screen_ops system_telemetry_number_ops = {
	.draw = telemetry_number_draw,
	.input = telemetry_number_input,
};

const struct zui_screen_ops system_telemetry_reset_ops = {
	.draw = telemetry_reset_draw,
	.input = telemetry_reset_input,
};

#endif
