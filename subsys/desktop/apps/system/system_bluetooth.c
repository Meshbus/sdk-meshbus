/* SPDX-License-Identifier: Apache-2.0 */

#include "system_private.h"

#if defined(CONFIG_MBS_BLUETOOTH)
static void system_bluetooth_sanitize(mbs_bluetooth_config *cfg)
{
	if (cfg == NULL) {
		return;
	}

	if (!IS_ENABLED(CONFIG_MBS_MESHCORE_COMPANION_BLUETOOTH)) {
		cfg->meshcore_companion_enabled = false;
	}
	if (cfg->passkey_mode > MBS_BLUETOOTH_PASSKEY_MODE_FIXED) {
		cfg->passkey_mode = MBS_BLUETOOTH_PASSKEY_MODE_RANDOM;
	}
	if (cfg->fixed_passkey > SYSTEM_BLUETOOTH_FIXED_PASSKEY_MAX) {
		cfg->fixed_passkey = SYSTEM_BLUETOOTH_FIXED_PASSKEY_MAX;
	}
}

static void system_bluetooth_default_config(mbs_bluetooth_config *cfg)
{
	if (cfg == NULL) {
		return;
	}

	cfg->enabled = IS_ENABLED(CONFIG_MBS_BLUETOOTH_DEFAULT_ENABLED);
	cfg->meshcore_companion_enabled =
		IS_ENABLED(CONFIG_MBS_MESHCORE_COMPANION_BLUETOOTH);
	cfg->passkey_mode =
		(mbs_bluetooth_passkey_mode)CONFIG_MBS_BLUETOOTH_DEFAULT_PASSKEY_MODE;
	cfg->fixed_passkey = CONFIG_MBS_BLUETOOTH_DEFAULT_FIXED_PASSKEY;
	system_bluetooth_sanitize(cfg);
}

static const char *system_bluetooth_fixed_passkey_text(struct system_app *app)
{
	if (app == NULL ||
	    app->bluetooth_editing.passkey_mode == MBS_BLUETOOTH_PASSKEY_MODE_RANDOM) {
		return DESKTOP_TEXT_COMMON_OFF;
	}

	(void)snprintk(app->bluetooth_value_bufs[0], sizeof(app->bluetooth_value_bufs[0]),
		       "%06u", app->bluetooth_editing.fixed_passkey);
	return app->bluetooth_value_bufs[0];
}

static void system_bluetooth_form_add(struct system_app *app, uint32_t id,
				      const char *label, const char *const *options,
				      size_t option_count, size_t option_index)
{
	if (app == NULL ||
	    app->bluetooth_form_item_count >= ARRAY_SIZE(app->bluetooth_form_items)) {
		return;
	}

	app->bluetooth_form_items[app->bluetooth_form_item_count++] = (struct zui_form_item){
		.id = id,
		.label = label,
		.options = options,
		.option_count = option_count,
		.option_index = option_index,
	};
}

static void system_bluetooth_form_add_value(struct system_app *app, uint32_t id,
					    const char *label, const char *value)
{
	if (app == NULL ||
	    app->bluetooth_form_item_count >= ARRAY_SIZE(app->bluetooth_form_items)) {
		return;
	}

	app->bluetooth_form_items[app->bluetooth_form_item_count++] = (struct zui_form_item){
		.id = id,
		.label = label,
		.value_text = value,
	};
}

static void system_bluetooth_show_apply_toast(struct system_app *app, bool success)
{
	if (app == NULL) {
		return;
	}

	(void)zui_toast_show(app->ctx.host, &(struct zui_toast_config){
		.title = DESKTOP_TEXT_BLUETOOTH_TITLE,
		.text = success ? DESKTOP_TEXT_COMMON_SETTINGS_APPLIED :
				  DESKTOP_TEXT_COMMON_SETTINGS_INVALID,
		.icon = success ? &I_save_24x24 : &I_error_24x24,
		.timeout_ms = success ? 900U : 1500U,
	});
}

static void system_bluetooth_show_reset_toast(struct system_app *app, bool success)
{
	if (app == NULL) {
		return;
	}

	(void)zui_toast_show(app->ctx.host, &(struct zui_toast_config){
		.title = DESKTOP_TEXT_BLUETOOTH_TITLE,
		.text = success ? DESKTOP_TEXT_COMMON_SETTINGS_RESET :
				  DESKTOP_TEXT_COMMON_SETTINGS_INVALID,
		.icon = success ? &I_save_24x24 : &I_error_24x24,
		.timeout_ms = success ? 900U : 1500U,
	});
}
#endif

#if defined(CONFIG_MBS_BLUETOOTH)
void system_bluetooth_form_changed(struct zui_form *form, uint32_t id,
				   size_t option_index, void *user_data)
{
	struct system_app *app = user_data;

	if (app == NULL || form == NULL) {
		return;
	}

	switch (id) {
	case SYSTEM_BLUETOOTH_FORM_ENABLED:
		app->bluetooth_editing.enabled = system_bool_from_idx(option_index);
		break;
	case SYSTEM_BLUETOOTH_FORM_MESHCORE_COMPANION:
		app->bluetooth_editing.meshcore_companion_enabled =
			system_bool_from_idx(option_index);
		break;
	case SYSTEM_BLUETOOTH_FORM_PASSKEY_MODE:
		app->bluetooth_editing.passkey_mode =
			(mbs_bluetooth_passkey_mode)option_index;
		(void)zui_form_set_value_text(
			form, SYSTEM_BLUETOOTH_FORM_FIXED_PASSKEY,
			system_bluetooth_fixed_passkey_text(app));
		break;
	default:
		break;
	}
}

static void system_bluetooth_apply(struct system_app *app)
{
	int rc;

	if (app == NULL) {
		return;
	}

	app->bluetooth_editing.enabled =
		system_bool_from_idx(zui_form_option(app->bluetooth_form,
						     SYSTEM_BLUETOOTH_FORM_ENABLED));
	app->bluetooth_editing.meshcore_companion_enabled =
		system_bool_from_idx(zui_form_option(app->bluetooth_form,
						     SYSTEM_BLUETOOTH_FORM_MESHCORE_COMPANION));
	app->bluetooth_editing.passkey_mode = (mbs_bluetooth_passkey_mode)zui_form_option(
		app->bluetooth_form, SYSTEM_BLUETOOTH_FORM_PASSKEY_MODE);
	system_bluetooth_sanitize(&app->bluetooth_editing);
	rc = mbs_bluetooth_config_set(&app->bluetooth_editing);
	if (rc == 0) {
		app->bluetooth_applied = app->bluetooth_editing;
	}

	system_bluetooth_show_apply_toast(app, rc == 0);
	if (rc == 0) {
		system_open_menu(app);
	}
}

static void system_bluetooth_fixed_passkey_submitted(struct zui_number_editor *editor,
						     int64_t value, void *user_data)
{
	struct system_app *app = user_data;

	ARG_UNUSED(editor);

	if (app == NULL) {
		return;
	}

	value = CLAMP(value, (int64_t)SYSTEM_BLUETOOTH_FIXED_PASSKEY_MIN,
		      (int64_t)SYSTEM_BLUETOOTH_FIXED_PASSKEY_MAX);
	app->bluetooth_editing.fixed_passkey = (uint32_t)value;
	(void)zui_form_set_value_text(app->bluetooth_form,
				      SYSTEM_BLUETOOTH_FORM_FIXED_PASSKEY,
				      system_bluetooth_fixed_passkey_text(app));
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_BLUETOOTH_FORM);
}

static void system_bluetooth_open_fixed_passkey_editor(struct system_app *app)
{
	if (app == NULL || app->bluetooth_number_editor == NULL) {
		return;
	}

	(void)zui_number_editor_update(app->bluetooth_number_editor,
				       &(struct zui_number_editor_config){
					       .title =
						       DESKTOP_TEXT_BLUETOOTH_SETTINGS_FIXED_PASSKEY,
					       .value = app->bluetooth_editing.fixed_passkey,
					       .min_value = SYSTEM_BLUETOOTH_FIXED_PASSKEY_MIN,
					       .max_value = SYSTEM_BLUETOOTH_FIXED_PASSKEY_MAX,
					       .max_digits =
						       SYSTEM_BLUETOOTH_FIXED_PASSKEY_MAX_DIGITS,
					       .unsigned_only = true,
					       .keep_leading_zeros = true,
					       .submitted =
						       system_bluetooth_fixed_passkey_submitted,
					       .user_data = app,
				       });
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_BLUETOOTH_NUMBER);
}

static void system_bluetooth_reset_confirmed(struct system_app *app)
{
	int rc;

	if (app == NULL) {
		return;
	}

	rc = mbs_bluetooth_config_reset();
	if (rc == 0) {
		if (mbs_bluetooth_config_get(&app->bluetooth_applied) != 0) {
			system_bluetooth_default_config(&app->bluetooth_applied);
		}
		system_bluetooth_sanitize(&app->bluetooth_applied);
		app->bluetooth_editing = app->bluetooth_applied;
	}

	system_bluetooth_show_reset_toast(app, rc == 0);
	if (rc == 0) {
		system_open_menu(app);
	} else {
		desktop_app_switch(&app->ctx, SYSTEM_SCREEN_BLUETOOTH_FORM);
	}
}

void system_bluetooth_reset_modal_result(struct zui_modal *modal,
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
		system_bluetooth_reset_confirmed(app);
	} else {
		desktop_app_switch(&app->ctx, SYSTEM_SCREEN_BLUETOOTH_FORM);
	}
}

static void system_bluetooth_open_reset_modal(struct system_app *app)
{
	if (app == NULL) {
		return;
	}

	(void)zui_modal_update(app->bluetooth_reset_modal, &(struct zui_modal_config){
		.title = DESKTOP_TEXT_COMMON_RESET,
		.text = DESKTOP_TEXT_COMMON_RESET_CONFIRM_TEXT,
		.left_button = DESKTOP_TEXT_COMMON_CANCEL,
		.right_button = DESKTOP_TEXT_COMMON_OK,
		.result = system_bluetooth_reset_modal_result,
		.user_data = app,
	});
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_BLUETOOTH_RESET);
}

void system_bluetooth_form_activated(struct zui_form *form, uint32_t id,
				     const struct zui_input_event *event,
				     void *user_data)
{
	struct system_app *app = user_data;

	ARG_UNUSED(form);
	ARG_UNUSED(event);

	if (app == NULL) {
		return;
	}

	if (id == SYSTEM_BLUETOOTH_FORM_APPLY) {
		system_bluetooth_apply(app);
	} else if (id == SYSTEM_BLUETOOTH_FORM_RESET) {
		system_bluetooth_open_reset_modal(app);
	} else if (id == SYSTEM_BLUETOOTH_FORM_FIXED_PASSKEY &&
		   app->bluetooth_editing.passkey_mode ==
			   MBS_BLUETOOTH_PASSKEY_MODE_FIXED) {
		system_bluetooth_open_fixed_passkey_editor(app);
	}
}

void system_open_bluetooth_form(struct system_app *app)
{
	if (app == NULL) {
		return;
	}

	if (mbs_bluetooth_config_get(&app->bluetooth_applied) != 0) {
		system_bluetooth_default_config(&app->bluetooth_applied);
	}
	system_bluetooth_sanitize(&app->bluetooth_applied);
	app->bluetooth_editing = app->bluetooth_applied;

	app->bluetooth_form_item_count = 0U;
	system_bluetooth_form_add(app, SYSTEM_BLUETOOTH_FORM_ENABLED,
				  DESKTOP_TEXT_BLUETOOTH_SETTINGS_ENABLED,
				  DESKTOP_TEXT_COMMON_NO_YES_VALUES,
				  ARRAY_SIZE(DESKTOP_TEXT_COMMON_NO_YES_VALUES),
				  system_bool_idx(app->bluetooth_editing.enabled));
	system_bluetooth_form_add(app, SYSTEM_BLUETOOTH_FORM_MESHCORE_COMPANION,
				  DESKTOP_TEXT_BLUETOOTH_SETTINGS_MESHCORE_COMPANION,
				  DESKTOP_TEXT_COMMON_BOOL_VALUES,
				  ARRAY_SIZE(DESKTOP_TEXT_COMMON_BOOL_VALUES),
				  system_bool_idx(
					  app->bluetooth_editing.meshcore_companion_enabled));
	system_bluetooth_form_add(app, SYSTEM_BLUETOOTH_FORM_PASSKEY_MODE,
				  DESKTOP_TEXT_BLUETOOTH_SETTINGS_PASSKEY_MODE,
				  DESKTOP_TEXT_BLUETOOTH_PASSKEY_MODE_VALUES,
				  DESKTOP_TEXT_BLUETOOTH_PASSKEY_MODE_COUNT,
				  (size_t)app->bluetooth_editing.passkey_mode);
	system_bluetooth_form_add_value(app, SYSTEM_BLUETOOTH_FORM_FIXED_PASSKEY,
					DESKTOP_TEXT_BLUETOOTH_SETTINGS_FIXED_PASSKEY,
					system_bluetooth_fixed_passkey_text(app));
	system_bluetooth_form_add_value(app, SYSTEM_BLUETOOTH_FORM_APPLY,
					DESKTOP_TEXT_COMMON_ACTION_APPLY, NULL);
	system_bluetooth_form_add_value(app, SYSTEM_BLUETOOTH_FORM_RESET,
					DESKTOP_TEXT_COMMON_ACTION_RESET, NULL);

	(void)zui_form_update(app->bluetooth_form, &(struct zui_form_config){
		.title = DESKTOP_TEXT_BLUETOOTH_TITLE,
		.items = app->bluetooth_form_items,
		.item_count = app->bluetooth_form_item_count,
		.changed = system_bluetooth_form_changed,
		.activated = system_bluetooth_form_activated,
		.user_data = app,
	});
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_BLUETOOTH_FORM);
}
#endif

#if defined(CONFIG_MBS_BLUETOOTH)
static void system_bluetooth_form_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct system_app *app = user_data;

	(void)zui_screen_draw(zui_form_get_screen(app->bluetooth_form), draw);
}

static bool system_bluetooth_form_input(const struct zui_input_event *event, void *user_data)
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
		system_bluetooth_apply(app);
		return true;
	}

	ret = zui_screen_submit_input(zui_form_get_screen(app->bluetooth_form), event);
	if (ret > 0) {
		desktop_app_request_redraw(&app->ctx);
		return true;
	}

	return false;
}

static void system_bluetooth_number_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct system_app *app = user_data;

	(void)zui_screen_draw(zui_number_editor_get_screen(app->bluetooth_number_editor), draw);
}

static bool system_bluetooth_number_input(const struct zui_input_event *event, void *user_data)
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
		desktop_app_switch(&app->ctx, SYSTEM_SCREEN_BLUETOOTH_FORM);
		return true;
	}

	ret = zui_screen_submit_input(zui_number_editor_get_screen(app->bluetooth_number_editor),
				      event);
	if (ret > 0) {
		desktop_app_request_redraw(&app->ctx);
		return true;
	}

	return false;
}

static void system_bluetooth_reset_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct system_app *app = user_data;

	(void)zui_screen_draw(zui_modal_get_screen(app->bluetooth_reset_modal), draw);
}

static bool system_bluetooth_reset_input(const struct zui_input_event *event, void *user_data)
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
		system_bluetooth_reset_modal_result(app->bluetooth_reset_modal,
						    ZUI_MODAL_RESULT_LEFT, event, app);
		return true;
	}

	ret = zui_screen_submit_input(zui_modal_get_screen(app->bluetooth_reset_modal), event);
	if (ret > 0) {
		desktop_app_request_redraw(&app->ctx);
		return true;
	}

	return false;
}
#endif

#if defined(CONFIG_MBS_BLUETOOTH)
const struct zui_screen_ops system_bluetooth_form_ops = {
	.draw = system_bluetooth_form_draw,
	.input = system_bluetooth_form_input,
};

const struct zui_screen_ops system_bluetooth_number_ops = {
	.draw = system_bluetooth_number_draw,
	.input = system_bluetooth_number_input,
};

const struct zui_screen_ops system_bluetooth_reset_ops = {
	.draw = system_bluetooth_reset_draw,
	.input = system_bluetooth_reset_input,
};
#endif
