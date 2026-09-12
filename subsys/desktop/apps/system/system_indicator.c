/* SPDX-License-Identifier: Apache-2.0 */

#include "system_private.h"

#if defined(CONFIG_MBS_INDICATOR)
static void system_indicator_default_config(mbs_indicator_config *cfg)
{
	if (cfg == NULL) {
		return;
	}

	*cfg = (mbs_indicator_config)meshbus_IndicatorConfig_init_zero;
	cfg->light_enabled = true;
	cfg->buzzer_enabled = true;
	cfg->has_light_feedback = true;
	cfg->light_feedback.heartbeat_enabled = true;
	cfg->has_buzzer_feedback = true;
	cfg->buzzer_feedback.direct_message_enabled = true;
	cfg->buzzer_feedback.channel_message_enabled = true;
	cfg->buzzer_feedback.system_enabled = true;
}

static void system_indicator_sanitize_config(mbs_indicator_config *cfg)
{
	if (cfg == NULL) {
		return;
	}

	if (!cfg->has_light_feedback) {
		cfg->has_light_feedback = true;
		cfg->light_feedback.heartbeat_enabled = true;
	}
	if (!cfg->has_buzzer_feedback) {
		cfg->has_buzzer_feedback = true;
		cfg->buzzer_feedback.direct_message_enabled = true;
		cfg->buzzer_feedback.channel_message_enabled = true;
		cfg->buzzer_feedback.system_enabled = true;
	}
}

static void system_indicator_form_add(struct system_app *app, uint32_t id, const char *label,
				      bool value)
{
	if (app == NULL || app->indicator_form_item_count >= ARRAY_SIZE(app->indicator_form_items)) {
		return;
	}

	app->indicator_form_items[app->indicator_form_item_count++] = (struct zui_form_item){
		.id = id,
		.label = label,
		.options = DESKTOP_TEXT_COMMON_NO_YES_VALUES,
		.option_count = ARRAY_SIZE(DESKTOP_TEXT_COMMON_NO_YES_VALUES),
		.option_index = system_bool_idx(value),
	};
}

static void system_indicator_form_add_action(struct system_app *app, uint32_t id,
					     const char *label)
{
	if (app == NULL || app->indicator_form_item_count >= ARRAY_SIZE(app->indicator_form_items)) {
		return;
	}

	app->indicator_form_items[app->indicator_form_item_count++] = (struct zui_form_item){
		.id = id,
		.label = label,
	};
}

static void system_indicator_show_apply_toast(struct system_app *app, bool success)
{
	if (app == NULL) {
		return;
	}

	(void)zui_toast_show(app->ctx.host, &(struct zui_toast_config){
		.title = DESKTOP_TEXT_INDICATOR_TITLE,
		.text = success ? DESKTOP_TEXT_COMMON_SETTINGS_APPLIED :
				  DESKTOP_TEXT_COMMON_SETTINGS_INVALID,
		.icon = success ? &I_save_24x24 : &I_error_24x24,
		.timeout_ms = success ? 900U : 1500U,
	});
}

static void system_indicator_show_reset_toast(struct system_app *app, bool success)
{
	if (app == NULL) {
		return;
	}

	(void)zui_toast_show(app->ctx.host, &(struct zui_toast_config){
		.title = DESKTOP_TEXT_INDICATOR_TITLE,
		.text = success ? DESKTOP_TEXT_COMMON_SETTINGS_RESET :
				  DESKTOP_TEXT_COMMON_SETTINGS_INVALID,
		.icon = success ? &I_save_24x24 : &I_error_24x24,
		.timeout_ms = success ? 900U : 1500U,
	});
}
#endif

#if defined(CONFIG_MBS_INDICATOR)
void system_indicator_form_changed(struct zui_form *form, uint32_t id,
					  size_t option_index, void *user_data)
{
	struct system_app *app = user_data;
	bool value = system_bool_from_idx(option_index);

	ARG_UNUSED(form);

	if (app == NULL) {
		return;
	}

	switch (id) {
	case SYSTEM_INDICATOR_FORM_LIGHT:
		app->indicator_editing.light_enabled = value;
		break;
	case SYSTEM_INDICATOR_FORM_HEARTBEAT:
		app->indicator_editing.has_light_feedback = true;
		app->indicator_editing.light_feedback.heartbeat_enabled = value;
		break;
	case SYSTEM_INDICATOR_FORM_BUZZER:
		app->indicator_editing.buzzer_enabled = value;
		break;
	case SYSTEM_INDICATOR_FORM_DM:
		app->indicator_editing.has_buzzer_feedback = true;
		app->indicator_editing.buzzer_feedback.direct_message_enabled = value;
		break;
	case SYSTEM_INDICATOR_FORM_CHANNEL:
		app->indicator_editing.has_buzzer_feedback = true;
		app->indicator_editing.buzzer_feedback.channel_message_enabled = value;
		break;
	case SYSTEM_INDICATOR_FORM_SYSTEM:
		app->indicator_editing.has_buzzer_feedback = true;
		app->indicator_editing.buzzer_feedback.system_enabled = value;
		break;
	default:
		break;
	}
}

static void system_indicator_apply(struct system_app *app)
{
	int rc;

	if (app == NULL) {
		return;
	}

	app->indicator_editing.light_enabled =
		system_bool_from_idx(zui_form_option(app->indicator_form,
						     SYSTEM_INDICATOR_FORM_LIGHT));
	app->indicator_editing.has_light_feedback = true;
	app->indicator_editing.light_feedback.heartbeat_enabled =
		system_bool_from_idx(zui_form_option(app->indicator_form,
						     SYSTEM_INDICATOR_FORM_HEARTBEAT));
	app->indicator_editing.buzzer_enabled =
		system_bool_from_idx(zui_form_option(app->indicator_form,
						     SYSTEM_INDICATOR_FORM_BUZZER));
	app->indicator_editing.has_buzzer_feedback = true;
	app->indicator_editing.buzzer_feedback.direct_message_enabled =
		system_bool_from_idx(zui_form_option(app->indicator_form,
						     SYSTEM_INDICATOR_FORM_DM));
	app->indicator_editing.buzzer_feedback.channel_message_enabled =
		system_bool_from_idx(zui_form_option(app->indicator_form,
						     SYSTEM_INDICATOR_FORM_CHANNEL));
	app->indicator_editing.buzzer_feedback.system_enabled =
		system_bool_from_idx(zui_form_option(app->indicator_form,
						     SYSTEM_INDICATOR_FORM_SYSTEM));

	rc = mbs_indicator_config_set(&app->indicator_editing);
	if (rc == 0) {
		app->indicator_applied = app->indicator_editing;
	}

	system_indicator_show_apply_toast(app, rc == 0);
	if (rc == 0) {
		system_open_menu(app);
	}
}

static void system_indicator_reset_confirmed(struct system_app *app)
{
	int rc;

	if (app == NULL) {
		return;
	}

	rc = mbs_indicator_config_reset();
	if (rc == 0) {
		if (mbs_indicator_config_get(&app->indicator_applied) != 0) {
			system_indicator_default_config(&app->indicator_applied);
		}
		system_indicator_sanitize_config(&app->indicator_applied);
		app->indicator_editing = app->indicator_applied;
	}

	system_indicator_show_reset_toast(app, rc == 0);
	if (rc == 0) {
		system_open_menu(app);
	} else {
		desktop_app_switch(&app->ctx, SYSTEM_SCREEN_INDICATOR_FORM);
	}
}

void system_indicator_reset_modal_result(struct zui_modal *modal,
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
		system_indicator_reset_confirmed(app);
	} else {
		desktop_app_switch(&app->ctx, SYSTEM_SCREEN_INDICATOR_FORM);
	}
}

static void system_indicator_open_reset_modal(struct system_app *app)
{
	if (app == NULL) {
		return;
	}

	(void)zui_modal_update(app->indicator_reset_modal, &(struct zui_modal_config){
		.title = DESKTOP_TEXT_COMMON_RESET,
		.text = DESKTOP_TEXT_COMMON_RESET_CONFIRM_TEXT,
		.left_button = DESKTOP_TEXT_COMMON_CANCEL,
		.right_button = DESKTOP_TEXT_COMMON_OK,
		.result = system_indicator_reset_modal_result,
		.user_data = app,
	});
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_INDICATOR_RESET);
}

void system_indicator_form_activated(struct zui_form *form, uint32_t id,
					    const struct zui_input_event *event, void *user_data)
{
	struct system_app *app = user_data;

	ARG_UNUSED(form);
	ARG_UNUSED(event);

	if (app == NULL) {
		return;
	}

	if (id == SYSTEM_INDICATOR_FORM_APPLY) {
		system_indicator_apply(app);
	} else if (id == SYSTEM_INDICATOR_FORM_RESET) {
		system_indicator_open_reset_modal(app);
	}
}

void system_open_indicator_form(struct system_app *app)
{
	if (app == NULL) {
		return;
	}

	if (mbs_indicator_config_get(&app->indicator_applied) != 0) {
		system_indicator_default_config(&app->indicator_applied);
	}
	system_indicator_sanitize_config(&app->indicator_applied);
	app->indicator_editing = app->indicator_applied;

	app->indicator_form_item_count = 0U;
	system_indicator_form_add(app, SYSTEM_INDICATOR_FORM_LIGHT,
				  DESKTOP_TEXT_INDICATOR_LIGHT,
				  app->indicator_editing.light_enabled);
	system_indicator_form_add(app, SYSTEM_INDICATOR_FORM_HEARTBEAT,
				  DESKTOP_TEXT_INDICATOR_HEARTBEAT,
				  app->indicator_editing.light_feedback.heartbeat_enabled);
	system_indicator_form_add(app, SYSTEM_INDICATOR_FORM_BUZZER,
				  DESKTOP_TEXT_INDICATOR_BUZZER,
				  app->indicator_editing.buzzer_enabled);
	system_indicator_form_add(app, SYSTEM_INDICATOR_FORM_DM, DESKTOP_TEXT_INDICATOR_DM,
				  app->indicator_editing.buzzer_feedback.direct_message_enabled);
	system_indicator_form_add(app, SYSTEM_INDICATOR_FORM_CHANNEL,
				  DESKTOP_TEXT_INDICATOR_CHANNEL,
				  app->indicator_editing.buzzer_feedback.channel_message_enabled);
	system_indicator_form_add(app, SYSTEM_INDICATOR_FORM_SYSTEM,
				  DESKTOP_TEXT_INDICATOR_SYSTEM,
				  app->indicator_editing.buzzer_feedback.system_enabled);
	system_indicator_form_add_action(app, SYSTEM_INDICATOR_FORM_APPLY,
					 DESKTOP_TEXT_COMMON_ACTION_APPLY);
	system_indicator_form_add_action(app, SYSTEM_INDICATOR_FORM_RESET,
					 DESKTOP_TEXT_COMMON_ACTION_RESET);

	(void)zui_form_update(app->indicator_form, &(struct zui_form_config){
		.title = DESKTOP_TEXT_INDICATOR_TITLE,
		.items = app->indicator_form_items,
		.item_count = app->indicator_form_item_count,
		.changed = system_indicator_form_changed,
		.activated = system_indicator_form_activated,
		.user_data = app,
	});
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_INDICATOR_FORM);
}
#endif

#if defined(CONFIG_MBS_INDICATOR)
static void system_indicator_form_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct system_app *app = user_data;

	(void)zui_screen_draw(zui_form_get_screen(app->indicator_form), draw);
}

static bool system_indicator_form_input(const struct zui_input_event *event, void *user_data)
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
		system_indicator_apply(app);
		return true;
	}

	ret = zui_screen_submit_input(zui_form_get_screen(app->indicator_form), event);
	if (ret > 0) {
		desktop_app_request_redraw(&app->ctx);
		return true;
	}

	return false;
}

static void system_indicator_reset_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct system_app *app = user_data;

	(void)zui_screen_draw(zui_modal_get_screen(app->indicator_reset_modal), draw);
}

static bool system_indicator_reset_input(const struct zui_input_event *event, void *user_data)
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
		system_indicator_reset_modal_result(app->indicator_reset_modal,
						    ZUI_MODAL_RESULT_LEFT, event, app);
		return true;
	}

	ret = zui_screen_submit_input(zui_modal_get_screen(app->indicator_reset_modal), event);
	if (ret > 0) {
		desktop_app_request_redraw(&app->ctx);
		return true;
	}

	return false;
}
#endif

#if defined(CONFIG_MBS_INDICATOR)
const struct zui_screen_ops system_indicator_form_ops = {
	.draw = system_indicator_form_draw,
	.input = system_indicator_form_input,
};

const struct zui_screen_ops system_indicator_reset_ops = {
	.draw = system_indicator_reset_draw,
	.input = system_indicator_reset_input,
};
#endif
