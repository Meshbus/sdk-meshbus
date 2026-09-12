/* SPDX-License-Identifier: Apache-2.0 */

#include "system_private.h"

#if defined(CONFIG_MBS_CLOCK)
static char system_clock_utc_labels[SYSTEM_CLOCK_UTC_VALUE_COUNT][7];
static const char *system_clock_utc_options[SYSTEM_CLOCK_UTC_VALUE_COUNT];
static bool system_clock_options_ready;

static void system_clock_format_utc(char *buf, size_t size, int32_t minutes)
{
	int32_t abs_minutes = minutes;
	char sign = '+';

	if (minutes == 0) {
		(void)snprintk(buf, size, "00:00");
		return;
	}

	if (abs_minutes < 0) {
		sign = '-';
		abs_minutes = -abs_minutes;
	}

	(void)snprintk(buf, size, DESKTOP_TEXT_CLOCK_UTC_OFFSET_FORMAT, sign,
		       (int)(abs_minutes / 60), (int)(abs_minutes % 60));
}

void system_clock_prepare_options(void)
{
	if (system_clock_options_ready) {
		return;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(system_clock_utc_options); i++) {
		int32_t offset = MBS_CLOCK_MIN_UTC_OFFSET_MINUTES +
				 ((int32_t)i * SYSTEM_CLOCK_UTC_STEP_MIN);

		system_clock_format_utc(system_clock_utc_labels[i],
					sizeof(system_clock_utc_labels[i]), offset);
		system_clock_utc_options[i] = system_clock_utc_labels[i];
	}
	system_clock_options_ready = true;
}

static size_t system_clock_format_to_idx(meshbus_ClockConfig_ClockTimeFormat format)
{
	return format == meshbus_ClockConfig_ClockTimeFormat_TIME_FORMAT_12H ? 0U : 1U;
}

static meshbus_ClockConfig_ClockTimeFormat system_clock_format_from_idx(size_t idx)
{
	return (idx % 2U) == 0U ?
		meshbus_ClockConfig_ClockTimeFormat_TIME_FORMAT_12H :
		meshbus_ClockConfig_ClockTimeFormat_TIME_FORMAT_24H;
}

static void system_clock_sanitize(mbs_clock_config *cfg)
{
	int32_t shifted;

	if (cfg == NULL) {
		return;
	}

	if (cfg->time_format != meshbus_ClockConfig_ClockTimeFormat_TIME_FORMAT_12H &&
	    cfg->time_format != meshbus_ClockConfig_ClockTimeFormat_TIME_FORMAT_24H) {
		cfg->time_format = meshbus_ClockConfig_ClockTimeFormat_TIME_FORMAT_24H;
	}

	cfg->utc_offset_minutes = CLAMP(cfg->utc_offset_minutes,
				       MBS_CLOCK_MIN_UTC_OFFSET_MINUTES,
				       MBS_CLOCK_MAX_UTC_OFFSET_MINUTES);
	shifted = cfg->utc_offset_minutes - MBS_CLOCK_MIN_UTC_OFFSET_MINUTES;
	shifted = (shifted / SYSTEM_CLOCK_UTC_STEP_MIN) * SYSTEM_CLOCK_UTC_STEP_MIN;
	cfg->utc_offset_minutes = MBS_CLOCK_MIN_UTC_OFFSET_MINUTES + shifted;
}

static void system_clock_default_config(mbs_clock_config *cfg)
{
	if (cfg == NULL) {
		return;
	}

	cfg->time_format =
		(meshbus_ClockConfig_ClockTimeFormat)CONFIG_MBS_CLOCK_DEFAULT_TIME_FORMAT;
	cfg->utc_offset_minutes = CONFIG_MBS_CLOCK_DEFAULT_UTC_OFFSET_MINUTES;
	system_clock_sanitize(cfg);
}

static size_t system_clock_utc_to_idx(int32_t minutes)
{
	minutes = CLAMP(minutes, MBS_CLOCK_MIN_UTC_OFFSET_MINUTES,
		       MBS_CLOCK_MAX_UTC_OFFSET_MINUTES);
	return (size_t)((minutes - MBS_CLOCK_MIN_UTC_OFFSET_MINUTES) /
			SYSTEM_CLOCK_UTC_STEP_MIN);
}

static int32_t system_clock_utc_from_idx(size_t idx)
{
	if (idx >= SYSTEM_CLOCK_UTC_VALUE_COUNT) {
		idx = SYSTEM_CLOCK_UTC_VALUE_COUNT - 1U;
	}

	return MBS_CLOCK_MIN_UTC_OFFSET_MINUTES +
	       ((int32_t)idx * SYSTEM_CLOCK_UTC_STEP_MIN);
}

static void system_clock_form_add(struct system_app *app, uint32_t id, const char *label,
				  const char *const *options, size_t option_count,
				  size_t option_index)
{
	if (app == NULL || app->clock_form_item_count >= ARRAY_SIZE(app->clock_form_items)) {
		return;
	}

	app->clock_form_items[app->clock_form_item_count++] = (struct zui_form_item){
		.id = id,
		.label = label,
		.options = options,
		.option_count = option_count,
		.option_index = option_index,
	};
}

static void system_clock_form_add_action(struct system_app *app, uint32_t id,
					 const char *label)
{
	if (app == NULL || app->clock_form_item_count >= ARRAY_SIZE(app->clock_form_items)) {
		return;
	}

	app->clock_form_items[app->clock_form_item_count++] = (struct zui_form_item){
		.id = id,
		.label = label,
	};
}

static void system_clock_show_apply_toast(struct system_app *app, bool success)
{
	if (app == NULL) {
		return;
	}

	(void)zui_toast_show(app->ctx.host, &(struct zui_toast_config){
		.title = DESKTOP_TEXT_CLOCK_TITLE,
		.text = success ? DESKTOP_TEXT_COMMON_SETTINGS_APPLIED :
				  DESKTOP_TEXT_COMMON_SETTINGS_INVALID,
		.icon = success ? &I_save_24x24 : &I_error_24x24,
		.timeout_ms = success ? 900U : 1500U,
	});
}

static void system_clock_show_reset_toast(struct system_app *app, bool success)
{
	if (app == NULL) {
		return;
	}

	(void)zui_toast_show(app->ctx.host, &(struct zui_toast_config){
		.title = DESKTOP_TEXT_CLOCK_TITLE,
		.text = success ? DESKTOP_TEXT_COMMON_SETTINGS_RESET :
				  DESKTOP_TEXT_COMMON_SETTINGS_INVALID,
		.icon = success ? &I_save_24x24 : &I_error_24x24,
		.timeout_ms = success ? 900U : 1500U,
	});
}
#endif

#if defined(CONFIG_MBS_CLOCK)
void system_clock_form_changed(struct zui_form *form, uint32_t id,
				      size_t option_index, void *user_data)
{
	struct system_app *app = user_data;

	ARG_UNUSED(form);

	if (app == NULL) {
		return;
	}

	switch (id) {
	case SYSTEM_CLOCK_FORM_FORMAT:
		app->clock_editing.time_format = system_clock_format_from_idx(option_index);
		break;
	case SYSTEM_CLOCK_FORM_UTC_OFFSET:
		app->clock_editing.utc_offset_minutes = system_clock_utc_from_idx(option_index);
		break;
	default:
		break;
	}
}

static void system_clock_apply(struct system_app *app)
{
	int rc;

	if (app == NULL) {
		return;
	}

	app->clock_editing.time_format =
		system_clock_format_from_idx(zui_form_option(app->clock_form,
							     SYSTEM_CLOCK_FORM_FORMAT));
	app->clock_editing.utc_offset_minutes =
		system_clock_utc_from_idx(zui_form_option(app->clock_form,
							  SYSTEM_CLOCK_FORM_UTC_OFFSET));
	system_clock_sanitize(&app->clock_editing);
	rc = mbs_clock_config_set(&app->clock_editing);
	if (rc == 0) {
		app->clock_applied = app->clock_editing;
	}

	system_clock_show_apply_toast(app, rc == 0);
	if (rc == 0) {
		system_open_menu(app);
	}
}

static void system_clock_reset_confirmed(struct system_app *app)
{
	int rc;

	if (app == NULL) {
		return;
	}

	rc = mbs_clock_config_reset();
	if (rc == 0) {
		if (mbs_clock_config_get(&app->clock_applied) != 0) {
			system_clock_default_config(&app->clock_applied);
		}
		system_clock_sanitize(&app->clock_applied);
		app->clock_editing = app->clock_applied;
	}

	system_clock_show_reset_toast(app, rc == 0);
	if (rc == 0) {
		system_open_menu(app);
	} else {
		desktop_app_switch(&app->ctx, SYSTEM_SCREEN_CLOCK_FORM);
	}
}

void system_clock_reset_modal_result(struct zui_modal *modal,
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
		system_clock_reset_confirmed(app);
	} else {
		desktop_app_switch(&app->ctx, SYSTEM_SCREEN_CLOCK_FORM);
	}
}

static void system_clock_open_reset_modal(struct system_app *app)
{
	if (app == NULL) {
		return;
	}

	(void)zui_modal_update(app->clock_reset_modal, &(struct zui_modal_config){
		.title = DESKTOP_TEXT_COMMON_RESET,
		.text = DESKTOP_TEXT_COMMON_RESET_CONFIRM_TEXT,
		.left_button = DESKTOP_TEXT_COMMON_CANCEL,
		.right_button = DESKTOP_TEXT_COMMON_OK,
		.result = system_clock_reset_modal_result,
		.user_data = app,
	});
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_CLOCK_RESET);
}

void system_clock_form_activated(struct zui_form *form, uint32_t id,
					const struct zui_input_event *event,
					void *user_data)
{
	struct system_app *app = user_data;

	ARG_UNUSED(form);
	ARG_UNUSED(event);

	if (app == NULL) {
		return;
	}

	if (id == SYSTEM_CLOCK_FORM_APPLY) {
		system_clock_apply(app);
	} else if (id == SYSTEM_CLOCK_FORM_RESET) {
		system_clock_open_reset_modal(app);
	}
}

void system_open_clock_form(struct system_app *app)
{
	if (app == NULL) {
		return;
	}

	if (mbs_clock_config_get(&app->clock_applied) != 0) {
		system_clock_default_config(&app->clock_applied);
	}
	system_clock_sanitize(&app->clock_applied);
	app->clock_editing = app->clock_applied;

	app->clock_form_item_count = 0U;
	system_clock_form_add(app, SYSTEM_CLOCK_FORM_FORMAT,
			      DESKTOP_TEXT_CLOCK_SETTINGS_FORMAT,
			      DESKTOP_TEXT_CLOCK_FORMAT_VALUES, DESKTOP_TEXT_CLOCK_FORMAT_COUNT,
			      system_clock_format_to_idx(app->clock_editing.time_format));
	system_clock_form_add(app, SYSTEM_CLOCK_FORM_UTC_OFFSET,
			      DESKTOP_TEXT_CLOCK_SETTINGS_UTC_OFFSET,
			      system_clock_utc_options, ARRAY_SIZE(system_clock_utc_options),
			      system_clock_utc_to_idx(app->clock_editing.utc_offset_minutes));
	system_clock_form_add_action(app, SYSTEM_CLOCK_FORM_APPLY,
				     DESKTOP_TEXT_COMMON_ACTION_APPLY);
	system_clock_form_add_action(app, SYSTEM_CLOCK_FORM_RESET,
				     DESKTOP_TEXT_COMMON_ACTION_RESET);

	(void)zui_form_update(app->clock_form, &(struct zui_form_config){
		.title = DESKTOP_TEXT_CLOCK_TITLE,
		.items = app->clock_form_items,
		.item_count = app->clock_form_item_count,
		.changed = system_clock_form_changed,
		.activated = system_clock_form_activated,
		.user_data = app,
	});
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_CLOCK_FORM);
}
#endif

#if defined(CONFIG_MBS_CLOCK)
static void system_clock_form_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct system_app *app = user_data;

	(void)zui_screen_draw(zui_form_get_screen(app->clock_form), draw);
}

static bool system_clock_form_input(const struct zui_input_event *event, void *user_data)
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
		system_clock_apply(app);
		return true;
	}

	ret = zui_screen_submit_input(zui_form_get_screen(app->clock_form), event);
	if (ret > 0) {
		desktop_app_request_redraw(&app->ctx);
		return true;
	}

	return false;
}

static void system_clock_reset_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct system_app *app = user_data;

	(void)zui_screen_draw(zui_modal_get_screen(app->clock_reset_modal), draw);
}

static bool system_clock_reset_input(const struct zui_input_event *event, void *user_data)
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
		system_clock_reset_modal_result(app->clock_reset_modal,
						ZUI_MODAL_RESULT_LEFT, event, app);
		return true;
	}

	ret = zui_screen_submit_input(zui_modal_get_screen(app->clock_reset_modal), event);
	if (ret > 0) {
		desktop_app_request_redraw(&app->ctx);
		return true;
	}

	return false;
}
#endif

#if defined(CONFIG_MBS_CLOCK)
const struct zui_screen_ops system_clock_form_ops = {
	.draw = system_clock_form_draw,
	.input = system_clock_form_input,
};

const struct zui_screen_ops system_clock_reset_ops = {
	.draw = system_clock_reset_draw,
	.input = system_clock_reset_input,
};
#endif
