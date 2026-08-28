/* SPDX-License-Identifier: Apache-2.0 */

#include "system_private.h"

#if defined(CONFIG_MESHBUS_POWER)
static const uint16_t system_power_timeout_values[] = {
	0, 5, 10, 20, 30, 60, 120, 180, 300,
};

static char system_power_timeout_labels[ARRAY_SIZE(system_power_timeout_values)][8];
static const char *system_power_timeout_options[ARRAY_SIZE(system_power_timeout_values)];
static bool system_power_options_ready;

static void system_power_format_timeout(char *buf, size_t size, uint32_t timeout_s)
{
	if (timeout_s == 0U) {
		(void)snprintk(buf, size, "%s", DESKTOP_TEXT_COMMON_OFF);
		return;
	}

	(void)snprintk(buf, size, DESKTOP_TEXT_POWER_VALUE_SECONDS_FORMAT,
		       (unsigned int)timeout_s);
}

void system_power_prepare_options(void)
{
	if (system_power_options_ready) {
		return;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(system_power_timeout_values); i++) {
		system_power_format_timeout(system_power_timeout_labels[i],
					    sizeof(system_power_timeout_labels[i]),
					    system_power_timeout_values[i]);
		system_power_timeout_options[i] = system_power_timeout_labels[i];
	}

	system_power_options_ready = true;
}

static size_t system_power_find_closest_timeout(uint32_t value)
{
	size_t best_idx = 0U;
	uint32_t best_diff = UINT32_MAX;

	for (size_t i = 0U; i < ARRAY_SIZE(system_power_timeout_values); i++) {
		uint32_t current = system_power_timeout_values[i];
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

static void system_power_sanitize(meshbus_power_config *cfg)
{
	if (cfg == NULL) {
		return;
	}

	cfg->low_voltage_shutdown_timeout =
		system_power_timeout_values[system_power_find_closest_timeout(
			cfg->low_voltage_shutdown_timeout)];
	cfg->losing_power_shutdown_timeout =
		system_power_timeout_values[system_power_find_closest_timeout(
			cfg->losing_power_shutdown_timeout)];
	cfg->no_connection_shutdown_timeout =
		system_power_timeout_values[system_power_find_closest_timeout(
			cfg->no_connection_shutdown_timeout)];
}

static void system_power_default_config(meshbus_power_config *cfg)
{
	if (cfg == NULL) {
		return;
	}

	*cfg = (meshbus_power_config){0};
	system_power_sanitize(cfg);
}

static void system_power_form_add(struct system_app *app, uint32_t id, const char *label,
				  const char *const *options, size_t option_count,
				  size_t option_index)
{
	if (app == NULL || app->power_form_item_count >= ARRAY_SIZE(app->power_form_items)) {
		return;
	}

	app->power_form_items[app->power_form_item_count++] = (struct zui_form_item){
		.id = id,
		.label = label,
		.options = options,
		.option_count = option_count,
		.option_index = option_index,
	};
}

static void system_power_form_add_action(struct system_app *app, uint32_t id,
					 const char *label)
{
	if (app == NULL || app->power_form_item_count >= ARRAY_SIZE(app->power_form_items)) {
		return;
	}

	app->power_form_items[app->power_form_item_count++] = (struct zui_form_item){
		.id = id,
		.label = label,
	};
}

static void system_power_show_apply_toast(struct system_app *app, bool success)
{
	if (app == NULL) {
		return;
	}

	(void)zui_toast_show(app->ctx.host, &(struct zui_toast_config){
		.title = DESKTOP_TEXT_POWER_TITLE,
		.text = success ? DESKTOP_TEXT_COMMON_SETTINGS_APPLIED :
				  DESKTOP_TEXT_COMMON_SETTINGS_INVALID,
		.icon = success ? &I_save_24x24 : &I_error_24x24,
		.timeout_ms = success ? 900U : 1500U,
	});
}

static void system_power_show_reset_toast(struct system_app *app, bool success)
{
	if (app == NULL) {
		return;
	}

	(void)zui_toast_show(app->ctx.host, &(struct zui_toast_config){
		.title = DESKTOP_TEXT_POWER_TITLE,
		.text = success ? DESKTOP_TEXT_COMMON_SETTINGS_RESET :
				  DESKTOP_TEXT_COMMON_SETTINGS_INVALID,
		.icon = success ? &I_save_24x24 : &I_error_24x24,
		.timeout_ms = success ? 900U : 1500U,
	});
}

#endif

#if defined(CONFIG_MESHBUS_POWER)
void system_power_form_changed(struct zui_form *form, uint32_t id,
				      size_t option_index, void *user_data)
{
	struct system_app *app = user_data;

	ARG_UNUSED(form);

	if (app == NULL) {
		return;
	}

	switch (id) {
	case SYSTEM_POWER_FORM_LOW_VOLTAGE:
		app->power_editing.low_voltage_shutdown_timeout =
			system_power_timeout_values[option_index];
		break;
	case SYSTEM_POWER_FORM_LOSING_POWER:
		app->power_editing.losing_power_shutdown_timeout =
			system_power_timeout_values[option_index];
		break;
	case SYSTEM_POWER_FORM_NO_CONNECTION:
		app->power_editing.no_connection_shutdown_timeout =
			system_power_timeout_values[option_index];
		break;
	default:
		break;
	}
}

static void system_power_apply(struct system_app *app)
{
	int rc;

	if (app == NULL) {
		return;
	}

	app->power_editing.low_voltage_shutdown_timeout = system_power_timeout_values
		[zui_form_option(app->power_form, SYSTEM_POWER_FORM_LOW_VOLTAGE)];
	app->power_editing.losing_power_shutdown_timeout = system_power_timeout_values
		[zui_form_option(app->power_form, SYSTEM_POWER_FORM_LOSING_POWER)];
	app->power_editing.no_connection_shutdown_timeout = system_power_timeout_values
		[zui_form_option(app->power_form, SYSTEM_POWER_FORM_NO_CONNECTION)];
	system_power_sanitize(&app->power_editing);
	rc = meshbus_power_config_set(&app->power_editing);
	if (rc == 0) {
		app->power_applied = app->power_editing;
	}

	system_power_show_apply_toast(app, rc == 0);
	if (rc == 0) {
		system_open_menu(app);
	}
}

static void system_power_reset_confirmed(struct system_app *app)
{
	int rc;

	if (app == NULL) {
		return;
	}

	rc = meshbus_power_config_reset();
	if (rc == 0) {
		if (meshbus_power_config_get(&app->power_applied) != 0) {
			system_power_default_config(&app->power_applied);
		}
		system_power_sanitize(&app->power_applied);
		app->power_editing = app->power_applied;
	}

	system_power_show_reset_toast(app, rc == 0);
	if (rc == 0) {
		system_open_menu(app);
	} else {
		desktop_app_switch(&app->ctx, SYSTEM_SCREEN_POWER_FORM);
	}
}

void system_power_reset_modal_result(struct zui_modal *modal,
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
		system_power_reset_confirmed(app);
	} else {
		desktop_app_switch(&app->ctx, SYSTEM_SCREEN_POWER_FORM);
	}
}

static void system_power_open_reset_modal(struct system_app *app)
{
	if (app == NULL) {
		return;
	}

	(void)zui_modal_update(app->power_reset_modal, &(struct zui_modal_config){
		.title = DESKTOP_TEXT_COMMON_RESET,
		.text = DESKTOP_TEXT_COMMON_RESET_CONFIRM_TEXT,
		.left_button = DESKTOP_TEXT_COMMON_CANCEL,
		.right_button = DESKTOP_TEXT_COMMON_OK,
		.result = system_power_reset_modal_result,
		.user_data = app,
	});
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_POWER_RESET);
}

void system_power_form_activated(struct zui_form *form, uint32_t id,
					const struct zui_input_event *event,
					void *user_data)
{
	struct system_app *app = user_data;

	ARG_UNUSED(form);
	ARG_UNUSED(event);

	if (app == NULL) {
		return;
	}

	if (id == SYSTEM_POWER_FORM_APPLY) {
		system_power_apply(app);
	} else if (id == SYSTEM_POWER_FORM_RESET) {
		system_power_open_reset_modal(app);
	}
}

void system_open_power_form(struct system_app *app)
{
	if (app == NULL) {
		return;
	}

	if (meshbus_power_config_get(&app->power_applied) != 0) {
		system_power_default_config(&app->power_applied);
	}
	system_power_sanitize(&app->power_applied);
	app->power_editing = app->power_applied;

	app->power_form_item_count = 0U;
	system_power_form_add(app, SYSTEM_POWER_FORM_LOW_VOLTAGE,
			      DESKTOP_TEXT_POWER_SETTINGS_LOW_VOLTAGE_TIMEOUT,
			      system_power_timeout_options, ARRAY_SIZE(system_power_timeout_options),
			      system_power_find_closest_timeout(
				      app->power_editing.low_voltage_shutdown_timeout));
	system_power_form_add(app, SYSTEM_POWER_FORM_LOSING_POWER,
			      DESKTOP_TEXT_POWER_SETTINGS_LOSING_POWER_TIMEOUT,
			      system_power_timeout_options, ARRAY_SIZE(system_power_timeout_options),
			      system_power_find_closest_timeout(
				      app->power_editing.losing_power_shutdown_timeout));
	system_power_form_add(app, SYSTEM_POWER_FORM_NO_CONNECTION,
			      DESKTOP_TEXT_POWER_SETTINGS_NO_CONNECTION_TIMEOUT,
			      system_power_timeout_options, ARRAY_SIZE(system_power_timeout_options),
			      system_power_find_closest_timeout(
				      app->power_editing.no_connection_shutdown_timeout));
	system_power_form_add_action(app, SYSTEM_POWER_FORM_APPLY,
				     DESKTOP_TEXT_COMMON_ACTION_APPLY);
	system_power_form_add_action(app, SYSTEM_POWER_FORM_RESET,
				     DESKTOP_TEXT_COMMON_ACTION_RESET);

	(void)zui_form_update(app->power_form, &(struct zui_form_config){
		.title = DESKTOP_TEXT_POWER_TITLE,
		.items = app->power_form_items,
		.item_count = app->power_form_item_count,
		.changed = system_power_form_changed,
		.activated = system_power_form_activated,
		.user_data = app,
	});
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_POWER_FORM);
}
#endif

#if defined(CONFIG_MESHBUS_POWER)
static void system_power_form_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct system_app *app = user_data;

	(void)zui_screen_draw(zui_form_get_screen(app->power_form), draw);
}

static bool system_power_form_input(const struct zui_input_event *event, void *user_data)
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
		system_power_apply(app);
		return true;
	}

	ret = zui_screen_submit_input(zui_form_get_screen(app->power_form), event);
	if (ret > 0) {
		desktop_app_request_redraw(&app->ctx);
		return true;
	}

	return false;
}

static void system_power_reset_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct system_app *app = user_data;

	(void)zui_screen_draw(zui_modal_get_screen(app->power_reset_modal), draw);
}

static bool system_power_reset_input(const struct zui_input_event *event, void *user_data)
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
		system_power_reset_modal_result(app->power_reset_modal,
						ZUI_MODAL_RESULT_LEFT, event, app);
		return true;
	}

	ret = zui_screen_submit_input(zui_modal_get_screen(app->power_reset_modal), event);
	if (ret > 0) {
		desktop_app_request_redraw(&app->ctx);
		return true;
	}

	return false;
}
#endif

#if defined(CONFIG_MESHBUS_POWER)
const struct zui_screen_ops system_power_form_ops = {
	.draw = system_power_form_draw,
	.input = system_power_form_input,
};

const struct zui_screen_ops system_power_reset_ops = {
	.draw = system_power_reset_draw,
	.input = system_power_reset_input,
};
#endif
