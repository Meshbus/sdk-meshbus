/* SPDX-License-Identifier: Apache-2.0 */

#include "system_private.h"

#if defined(CONFIG_MESHBUS_DISPLAY)
static const uint8_t system_display_brightness_values[] = {
	5, 10, 15, 20, 25, 30, 35, 40, 45, 50,
	55, 60, 65, 70, 75, 80, 85, 90, 95, 100,
};

static const uint16_t system_display_timeout_values[] = {
	0, 5, 10, 20, 30, 60, 120, 180, 300,
};

static char system_display_brightness_labels[ARRAY_SIZE(system_display_brightness_values)][8];
static const char *system_display_brightness_options[ARRAY_SIZE(system_display_brightness_values)];
static char system_display_timeout_labels[ARRAY_SIZE(system_display_timeout_values)][8];
static const char *system_display_timeout_options[ARRAY_SIZE(system_display_timeout_values)];
static bool system_display_options_ready;

static void system_display_format_timeout(char *buf, size_t size, uint32_t timeout_s)
{
	if (timeout_s == 0U) {
		(void)snprintk(buf, size, "%s", DESKTOP_TEXT_COMMON_OFF);
		return;
	}

	(void)snprintk(buf, size, DESKTOP_TEXT_POWER_VALUE_SECONDS_FORMAT,
		       (unsigned int)timeout_s);
}

void system_display_prepare_options(void)
{
	if (system_display_options_ready) {
		return;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(system_display_brightness_values); i++) {
		(void)snprintk(system_display_brightness_labels[i],
			       sizeof(system_display_brightness_labels[i]), "%u%%",
			       (unsigned int)system_display_brightness_values[i]);
		system_display_brightness_options[i] = system_display_brightness_labels[i];
	}

	for (size_t i = 0U; i < ARRAY_SIZE(system_display_timeout_values); i++) {
		system_display_format_timeout(system_display_timeout_labels[i],
					      sizeof(system_display_timeout_labels[i]),
					      system_display_timeout_values[i]);
		system_display_timeout_options[i] = system_display_timeout_labels[i];
	}

	system_display_options_ready = true;
}

static size_t system_find_closest_u8(const uint8_t *values, size_t count, uint8_t value)
{
	size_t best_idx = 0U;
	uint8_t best_diff = UINT8_MAX;

	for (size_t i = 0U; i < count; i++) {
		uint8_t diff = values[i] > value ? values[i] - value : value - values[i];

		if (diff < best_diff) {
			best_idx = i;
			best_diff = diff;
			if (diff == 0U) {
				break;
			}
		}
	}

	return best_idx;
}

static size_t system_find_closest_u16(const uint16_t *values, size_t count, uint32_t value)
{
	size_t best_idx = 0U;
	uint32_t best_diff = UINT32_MAX;

	for (size_t i = 0U; i < count; i++) {
		uint32_t current = values[i];
		uint32_t diff = current > value ? current - value : value - current;

		if (diff < best_diff) {
			best_idx = i;
			best_diff = diff;
			if (diff == 0U) {
				break;
			}
		}
	}

	return best_idx;
}

static void system_display_sanitize(meshbus_display_config *cfg)
{
	if (cfg == NULL) {
		return;
	}

	cfg->brightness = system_display_brightness_values[system_find_closest_u8(
		system_display_brightness_values, ARRAY_SIZE(system_display_brightness_values),
		(uint8_t)cfg->brightness)];
	cfg->sleep_timeout = system_display_timeout_values[system_find_closest_u16(
		system_display_timeout_values, ARRAY_SIZE(system_display_timeout_values),
		cfg->sleep_timeout)];
}

static void system_display_default_config(meshbus_display_config *cfg)
{
	if (cfg == NULL) {
		return;
	}

	cfg->brightness = CONFIG_MESHBUS_DISPLAY_DEFAULT_BRIGHTNESS;
	cfg->sleep_timeout = CONFIG_MESHBUS_DISPLAY_DEFAULT_SLEEP_TIMEOUT;
	cfg->invert = IS_ENABLED(CONFIG_MESHBUS_DISPLAY_DEFAULT_INVERT);
	system_display_sanitize(cfg);
}

static void system_display_form_add(struct system_app *app, uint32_t id, const char *label,
				    const char *const *options, size_t option_count,
				    size_t option_index)
{
	if (app == NULL || app->display_form_item_count >= ARRAY_SIZE(app->display_form_items)) {
		return;
	}

	app->display_form_items[app->display_form_item_count++] = (struct zui_form_item){
		.id = id,
		.label = label,
		.options = options,
		.option_count = option_count,
		.option_index = option_index,
	};
}

static void system_display_form_add_action(struct system_app *app, uint32_t id,
					   const char *label)
{
	if (app == NULL || app->display_form_item_count >= ARRAY_SIZE(app->display_form_items)) {
		return;
	}

	app->display_form_items[app->display_form_item_count++] = (struct zui_form_item){
		.id = id,
		.label = label,
	};
}

static void system_display_show_apply_toast(struct system_app *app, bool success)
{
	if (app == NULL) {
		return;
	}

	(void)zui_toast_show(app->ctx.host, &(struct zui_toast_config){
		.title = DESKTOP_TEXT_DISPLAY_TITLE,
		.text = success ? DESKTOP_TEXT_COMMON_SETTINGS_APPLIED :
				  DESKTOP_TEXT_COMMON_SETTINGS_INVALID,
		.icon = success ? &I_save_24x24 : &I_error_24x24,
		.timeout_ms = success ? 900U : 1500U,
	});
}

static void system_display_show_reset_toast(struct system_app *app, bool success)
{
	if (app == NULL) {
		return;
	}

	(void)zui_toast_show(app->ctx.host, &(struct zui_toast_config){
		.title = DESKTOP_TEXT_DISPLAY_TITLE,
		.text = success ? DESKTOP_TEXT_COMMON_SETTINGS_RESET :
				  DESKTOP_TEXT_COMMON_SETTINGS_INVALID,
		.icon = success ? &I_save_24x24 : &I_error_24x24,
		.timeout_ms = success ? 900U : 1500U,
	});
}
#endif

#if defined(CONFIG_MESHBUS_DISPLAY)
void system_display_form_changed(struct zui_form *form, uint32_t id,
					size_t option_index, void *user_data)
{
	struct system_app *app = user_data;

	ARG_UNUSED(form);

	if (app == NULL) {
		return;
	}

	switch (id) {
	case SYSTEM_DISPLAY_FORM_BRIGHTNESS:
		app->display_editing.brightness =
			system_display_brightness_values[option_index];
		break;
	case SYSTEM_DISPLAY_FORM_SLEEP_TIMEOUT:
		app->display_editing.sleep_timeout =
			system_display_timeout_values[option_index];
		break;
	case SYSTEM_DISPLAY_FORM_INVERT:
		app->display_editing.invert = option_index != 0U;
		break;
	default:
		break;
	}
}

static void system_display_apply(struct system_app *app)
{
	int rc;

	if (app == NULL) {
		return;
	}

	app->display_editing.brightness =
		system_display_brightness_values[zui_form_option(
			app->display_form, SYSTEM_DISPLAY_FORM_BRIGHTNESS)];
	app->display_editing.sleep_timeout =
		system_display_timeout_values[zui_form_option(
			app->display_form, SYSTEM_DISPLAY_FORM_SLEEP_TIMEOUT)];
	app->display_editing.invert =
		zui_form_option(app->display_form, SYSTEM_DISPLAY_FORM_INVERT) != 0U;
	system_display_sanitize(&app->display_editing);
	rc = meshbus_display_config_set(&app->display_editing);
	if (rc == 0) {
		app->display_applied = app->display_editing;
	}

	system_display_show_apply_toast(app, rc == 0);
	if (rc == 0) {
		system_open_menu(app);
	}
}

static void system_display_reset_confirmed(struct system_app *app)
{
	int rc;

	if (app == NULL) {
		return;
	}

	rc = meshbus_display_config_reset();
	if (rc == 0) {
		if (meshbus_display_config_get(&app->display_applied) != 0) {
			system_display_default_config(&app->display_applied);
		}
		system_display_sanitize(&app->display_applied);
		app->display_editing = app->display_applied;
	}

	system_display_show_reset_toast(app, rc == 0);
	if (rc == 0) {
		system_open_menu(app);
	} else {
		desktop_app_switch(&app->ctx, SYSTEM_SCREEN_DISPLAY_FORM);
	}
}

void system_display_reset_modal_result(struct zui_modal *modal,
					      enum zui_modal_result result,
					      const struct zui_input_event *event,
					      void *user_data)
{
	struct system_app *app = user_data;

	ARG_UNUSED(modal);
	ARG_UNUSED(event);

	if (app == NULL) {
		return;
	}

	if (result == ZUI_MODAL_RESULT_RIGHT) {
		system_display_reset_confirmed(app);
	} else {
		desktop_app_switch(&app->ctx, SYSTEM_SCREEN_DISPLAY_FORM);
	}
}

static void system_display_open_reset_modal(struct system_app *app)
{
	if (app == NULL) {
		return;
	}

	(void)zui_modal_update(app->display_reset_modal, &(struct zui_modal_config){
		.title = DESKTOP_TEXT_COMMON_RESET,
		.text = DESKTOP_TEXT_COMMON_RESET_CONFIRM_TEXT,
		.left_button = DESKTOP_TEXT_COMMON_CANCEL,
		.right_button = DESKTOP_TEXT_COMMON_OK,
		.result = system_display_reset_modal_result,
		.user_data = app,
	});
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_DISPLAY_RESET);
}

void system_display_form_activated(struct zui_form *form, uint32_t id,
					  const struct zui_input_event *event,
					  void *user_data)
{
	struct system_app *app = user_data;

	ARG_UNUSED(form);
	ARG_UNUSED(event);

	if (app == NULL) {
		return;
	}

	if (id == SYSTEM_DISPLAY_FORM_APPLY) {
		system_display_apply(app);
	} else if (id == SYSTEM_DISPLAY_FORM_RESET) {
		system_display_open_reset_modal(app);
	}
}

void system_open_display_form(struct system_app *app)
{
	if (app == NULL) {
		return;
	}

	if (meshbus_display_config_get(&app->display_applied) != 0) {
		system_display_default_config(&app->display_applied);
	}
	system_display_sanitize(&app->display_applied);
	app->display_editing = app->display_applied;

	app->display_form_item_count = 0U;
	system_display_form_add(
		app, SYSTEM_DISPLAY_FORM_BRIGHTNESS, DESKTOP_TEXT_DISPLAY_SETTINGS_BRIGHTNESS,
		system_display_brightness_options, ARRAY_SIZE(system_display_brightness_options),
		system_find_closest_u8(system_display_brightness_values,
				       ARRAY_SIZE(system_display_brightness_values),
				       (uint8_t)app->display_editing.brightness));
	system_display_form_add(
		app, SYSTEM_DISPLAY_FORM_SLEEP_TIMEOUT,
		DESKTOP_TEXT_DISPLAY_SETTINGS_SLEEP_TIMEOUT, system_display_timeout_options,
		ARRAY_SIZE(system_display_timeout_options),
		system_find_closest_u16(system_display_timeout_values,
					ARRAY_SIZE(system_display_timeout_values),
					app->display_editing.sleep_timeout));
	system_display_form_add(app, SYSTEM_DISPLAY_FORM_INVERT,
				DESKTOP_TEXT_DISPLAY_SETTINGS_INVERT,
				DESKTOP_TEXT_COMMON_BOOL_VALUES, 2U,
				app->display_editing.invert ? 1U : 0U);
	system_display_form_add_action(app, SYSTEM_DISPLAY_FORM_APPLY,
				       DESKTOP_TEXT_COMMON_ACTION_APPLY);
	system_display_form_add_action(app, SYSTEM_DISPLAY_FORM_RESET,
				       DESKTOP_TEXT_COMMON_ACTION_RESET);

	(void)zui_form_update(app->display_form, &(struct zui_form_config){
		.title = DESKTOP_TEXT_DISPLAY_TITLE,
		.items = app->display_form_items,
		.item_count = app->display_form_item_count,
		.changed = system_display_form_changed,
		.activated = system_display_form_activated,
		.user_data = app,
	});
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_DISPLAY_FORM);
}
#endif

#if defined(CONFIG_MESHBUS_DISPLAY)
static void system_display_form_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct system_app *app = user_data;

	(void)zui_screen_draw(zui_form_get_screen(app->display_form), draw);
}

static bool system_display_form_input(const struct zui_input_event *event, void *user_data)
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
		system_open_menu(app);
		return true;
	}
	if (desktop_app_input_is_long_press(event) && event->code == ZUI_INPUT_CODE_SELECT) {
		system_display_apply(app);
		return true;
	}

	ret = zui_screen_submit_input(zui_form_get_screen(app->display_form), event);
	if (ret > 0) {
		desktop_app_request_redraw(&app->ctx);
		return true;
	}

	return false;
}

static void system_display_reset_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct system_app *app = user_data;

	(void)zui_screen_draw(zui_modal_get_screen(app->display_reset_modal), draw);
}

static bool system_display_reset_input(const struct zui_input_event *event, void *user_data)
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
		system_display_reset_modal_result(app->display_reset_modal,
						  ZUI_MODAL_RESULT_LEFT, event, app);
		return true;
	}

	ret = zui_screen_submit_input(zui_modal_get_screen(app->display_reset_modal), event);
	if (ret > 0) {
		desktop_app_request_redraw(&app->ctx);
		return true;
	}

	return false;
}
#endif

#if defined(CONFIG_MESHBUS_DISPLAY)
const struct zui_screen_ops system_display_form_ops = {
	.draw = system_display_form_draw,
	.input = system_display_form_input,
};

const struct zui_screen_ops system_display_reset_ops = {
	.draw = system_display_reset_draw,
	.input = system_display_reset_input,
};
#endif
