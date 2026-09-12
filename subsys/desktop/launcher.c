/* SPDX-License-Identifier: Apache-2.0 */

#include "desktop_private.h"

#include "assets/assets_icons.h"
#include "text/desktop_text.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <fs/fs.h>
#include <llext/llext.h>
#include <zephyr/sys/util.h>
#include <zui/zui.h>

LOG_MODULE_DECLARE(mbs_desktop, CONFIG_MBS_DESKTOP_LOG_LEVEL);

struct desktop_launcher {
	struct zui_desktop *desktop;
	struct zui_form *action_menu;
	struct zui_modal *delete_modal;
	struct zui_text_view *properties_view;
	char entry_names[CONFIG_ZUI_FILE_PICKER_MAX_ENTRIES][CONFIG_ZUI_FILE_PICKER_NAME_SIZE];
	uint8_t icon_data[CONFIG_MBS_DESKTOP_LAUNCHER_MAX_APPS]
			 [MBS_LLEXT_APP_ICON_DATA_MAX_LEN];
	char selected_path[MBS_LLEXT_PATH_MAX_LEN + 1];
	char selected_name[CONFIG_ZUI_FILE_PICKER_NAME_SIZE];
	char properties_text[640];
	size_t icon_count;
	char toast_text[48];
	uint8_t mode;
};

static struct desktop_launcher launcher;

void desktop_launcher_deinit(struct zui_desktop *desktop);

enum launcher_mode {
	LAUNCHER_MODE_PICKER,
	LAUNCHER_MODE_MENU,
	LAUNCHER_MODE_DELETE,
	LAUNCHER_MODE_PROPERTIES,
};

enum launcher_action {
	LAUNCHER_ACTION_DELETE = 1,
	LAUNCHER_ACTION_PROPERTIES,
};

static const struct zui_form_item launcher_action_items[] = {
	{
		.id = LAUNCHER_ACTION_DELETE,
		.label = DESKTOP_TEXT_LAUNCHER_ACTION_DELETE,
	},
	{
		.id = LAUNCHER_ACTION_PROPERTIES,
		.label = DESKTOP_TEXT_LAUNCHER_ACTION_PROPERTIES,
	},
};

static bool launcher_has_app_suffix(const char *name)
{
	size_t name_len;
	size_t suffix_len = strlen(MBS_LLEXT_APP_SUFFIX);

	if (name == NULL) {
		return false;
	}

	name_len = strlen(name);
	return (name_len > suffix_len) &&
	       (strcmp(&name[name_len - suffix_len], MBS_LLEXT_APP_SUFFIX) == 0);
}

static bool launcher_entry_visible(const struct mbs_fs_entry *entry)
{
	if (entry == NULL || entry->name[0] == '\0') {
		return false;
	}
	if (entry->type == MBS_FS_ENTRY_DIR) {
		return true;
	}
	if (entry->type != MBS_FS_ENTRY_FILE) {
		return false;
	}

	return launcher_has_app_suffix(entry->name);
}

static const char *launcher_basename(const char *path)
{
	const char *slash;

	if (path == NULL) {
		return "";
	}

	slash = strrchr(path, '/');
	return slash == NULL ? path : slash + 1;
}

static bool launcher_path_in_app_root(const char *path)
{
	size_t root_len = strlen(CONFIG_MBS_LLEXT_APP_DEFAULT_PATH);

	if (path == NULL) {
		return false;
	}

	return strcmp(path, CONFIG_MBS_LLEXT_APP_DEFAULT_PATH) == 0 ||
	       (strncmp(path, CONFIG_MBS_LLEXT_APP_DEFAULT_PATH, root_len) == 0 &&
		path[root_len] == '/');
}

static void launcher_set_mode(struct desktop_launcher *app, enum launcher_mode mode)
{
	if (app == NULL) {
		return;
	}

	app->mode = (uint8_t)mode;
	if (app->desktop != NULL) {
		zui_desktop_request_redraw(app->desktop);
	}
}

static void launcher_show_error(struct desktop_launcher *app, const char *format, int rc)
{
	if (app == NULL || app->desktop == NULL || app->desktop->host == NULL) {
		return;
	}

	(void)snprintk(app->toast_text, sizeof(app->toast_text), format, rc);
	(void)zui_toast_show(app->desktop->host, &(struct zui_toast_config){
		.title = DESKTOP_TEXT_LAUNCHER_TITLE,
		.text = app->toast_text,
		.icon = &I_error_24x24,
		.timeout_ms = 1500U,
	});
}

static void launcher_show_message(struct desktop_launcher *app, const char *text)
{
	if (app == NULL || app->desktop == NULL || app->desktop->host == NULL) {
		return;
	}

	(void)zui_toast_show(app->desktop->host, &(struct zui_toast_config){
		.title = DESKTOP_TEXT_LAUNCHER_TITLE,
		.text = text,
		.icon = &I_done_24x24,
		.timeout_ms = 1000U,
	});
}

struct launcher_load_ctx {
	struct desktop_launcher *app;
	struct zui_file_picker_entry *entries;
	size_t limit;
	size_t out;
};

static int launcher_load_entry_cb(const struct mbs_fs_entry *entry, void *user_data)
{
	struct launcher_load_ctx *ctx = user_data;
	struct desktop_launcher *app;

	if (ctx == NULL || ctx->app == NULL || entry == NULL) {
		return -EINVAL;
	}
	if (!launcher_entry_visible(entry)) {
		return 0;
	}

	app = ctx->app;
	if (ctx->out >= ctx->limit) {
		return 0;
	}
	if (strnlen(entry->name, sizeof(app->entry_names[ctx->out])) >=
	    sizeof(app->entry_names[ctx->out])) {
		return 0;
	}

	strcpy(app->entry_names[ctx->out], entry->name);
	ctx->entries[ctx->out] = (struct zui_file_picker_entry){
		.path = app->entry_names[ctx->out],
		.is_dir = entry->type == MBS_FS_ENTRY_DIR,
	};
	ctx->out++;
	return 0;
}

static int launcher_load(struct zui_file_picker *picker, const char *path,
			 struct zui_file_picker_entry *entries, size_t capacity,
			 size_t *count, void *user_data)
{
	struct desktop_launcher *app = user_data;
	struct launcher_load_ctx ctx;
	size_t limit;
	int rc;

	ARG_UNUSED(picker);

	if (app == NULL || path == NULL || entries == NULL || count == NULL) {
		return -EINVAL;
	}

	app->icon_count = 0U;
	memset(app->entry_names, 0, sizeof(app->entry_names));
	memset(app->icon_data, 0, sizeof(app->icon_data));
	*count = 0U;

	if (strcmp(path, CONFIG_MBS_LLEXT_APP_DEFAULT_PATH) == 0) {
		(void)mbs_fs_ensure_product_dirs();
	}

	limit = capacity;
	if ((strcmp(path, CONFIG_MBS_LLEXT_APP_DEFAULT_PATH) != 0) && limit > 0U) {
		limit--;
	}

	ctx = (struct launcher_load_ctx){
		.app = app,
		.entries = entries,
		.limit = limit,
	};
	rc = mbs_fs_list(path, 0U, SIZE_MAX, launcher_load_entry_cb, &ctx, NULL, NULL);
	if (rc != 0) {
		return rc;
	}

	*count = ctx.out;
	return 0;
}

static bool launcher_probe(const char *path, char *name, size_t name_size,
			   struct zui_file_picker_probe_result *result, void *user_data)
{
	struct desktop_launcher *app = user_data;
	struct mbs_llext_app_info info;
	struct mbs_fs_entry entry;
	int rc;

	if (path == NULL || app == NULL) {
		return false;
	}

	rc = mbs_fs_stat(path, &entry);
	if (rc == 0 && entry.type == MBS_FS_ENTRY_DIR) {
		return true;
	}
	if (!launcher_has_app_suffix(path)) {
		return false;
	}

	rc = mbs_llext_app_probe(path, &info);
	if (rc != 0) {
		LOG_WRN("Skipping app '%s': probe failed (%d)", path, rc);
		return false;
	}

	if (name != NULL && name_size > 0U) {
		const char *display = info.name[0] != '\0' ? info.name : info.id;

		(void)snprintk(name, name_size, "%s", display);
	}

	if (result != NULL && info.icon_data_size == MBS_LLEXT_APP_ICON_DATA_SIZE &&
	    app->icon_count < ARRAY_SIZE(app->icon_data)) {
		uint8_t *slot = app->icon_data[app->icon_count++];

		memset(slot, 0, MBS_LLEXT_APP_ICON_DATA_SIZE);
		memcpy(slot, info.icon_data, MBS_LLEXT_APP_ICON_DATA_SIZE);
		result->icon_data = slot;
		result->icon_width = MBS_LLEXT_APP_ICON_WIDTH;
		result->icon_height = MBS_LLEXT_APP_ICON_HEIGHT;
	}

	return true;
}

void desktop_launcher_show_cleanup_error(struct zui_desktop *desktop, int error)
{
	if (launcher.desktop == desktop) {
		launcher_show_error(&launcher, DESKTOP_TEXT_LAUNCHER_CLEANUP_FAILED_FORMAT, error);
	}
}

static void launcher_selected(struct zui_file_picker *picker, const char *path,
			      const struct zui_input_event *event, void *user_data)
{
	struct desktop_launcher *app = user_data;
	int rc;

	ARG_UNUSED(picker);
	ARG_UNUSED(event);

	if (app == NULL || path == NULL) {
		return;
	}

	rc = desktop_mba_start(app->desktop, path);
	if (rc != 0) {
		LOG_WRN("MBA start failed: path='%s' rc=%d", path, rc);
		launcher_show_error(app, DESKTOP_TEXT_LAUNCHER_START_FAILED_FORMAT, rc);
	}
}

static void launcher_refresh_selected_dir(struct desktop_launcher *app, const char *focus_path)
{
	if (app == NULL || app->desktop == NULL || app->desktop->launcher_picker == NULL) {
		return;
	}

	(void)zui_file_picker_open(app->desktop->launcher_picker,
				   focus_path != NULL ? focus_path : app->selected_path);
}

static void launcher_selected_long(struct zui_file_picker *picker, const char *path,
				   const struct zui_input_event *event, void *user_data)
{
	struct desktop_launcher *app = user_data;
	const char *name;

	ARG_UNUSED(picker);
	ARG_UNUSED(event);

	if (app == NULL || path == NULL || !launcher_path_in_app_root(path) ||
	    !launcher_has_app_suffix(path)) {
		return;
	}

	name = launcher_basename(path);
	if (snprintk(app->selected_path, sizeof(app->selected_path), "%s", path) >=
	    sizeof(app->selected_path)) {
		launcher_show_error(app, DESKTOP_TEXT_LAUNCHER_PROPERTIES_FAILED_FORMAT,
				    -ENAMETOOLONG);
		return;
	}
	if (snprintk(app->selected_name, sizeof(app->selected_name), "%s", name) >=
	    sizeof(app->selected_name)) {
		launcher_show_error(app, DESKTOP_TEXT_LAUNCHER_PROPERTIES_FAILED_FORMAT,
				    -ENAMETOOLONG);
		return;
	}

	(void)zui_form_select(app->action_menu, 0U);
	launcher_set_mode(app, LAUNCHER_MODE_MENU);
}

static void launcher_open_delete(struct desktop_launcher *app)
{
	if (app == NULL || app->delete_modal == NULL) {
		return;
	}

	(void)zui_modal_update(app->delete_modal, &(struct zui_modal_config){
		.title = DESKTOP_TEXT_LAUNCHER_DELETE_TITLE,
		.text = app->selected_name,
		.left_button = DESKTOP_TEXT_COMMON_CANCEL,
		.right_button = DESKTOP_TEXT_LAUNCHER_ACTION_DELETE,
		.result = NULL,
		.user_data = app,
	});
	launcher_set_mode(app, LAUNCHER_MODE_DELETE);
}

static void launcher_confirm_delete(struct desktop_launcher *app)
{
	int rc;

	if (app == NULL) {
		return;
	}

	rc = mbs_fs_delete(app->selected_path);
	if (rc != 0) {
		launcher_show_error(app, DESKTOP_TEXT_LAUNCHER_DELETE_FAILED_FORMAT, rc);
		launcher_set_mode(app, LAUNCHER_MODE_MENU);
		return;
	}

	launcher_show_message(app, DESKTOP_TEXT_LAUNCHER_DELETED);
	launcher_set_mode(app, LAUNCHER_MODE_PICKER);
	launcher_refresh_selected_dir(app, NULL);
}

static void launcher_open_properties(struct desktop_launcher *app)
{
	struct mbs_llext_app_info info;
	struct mbs_fs_entry entry;
	const char *display;
	int rc;

	if (app == NULL || app->properties_view == NULL) {
		return;
	}

	rc = mbs_fs_stat(app->selected_path, &entry);
	if (rc == 0) {
		rc = mbs_llext_app_probe(app->selected_path, &info);
	}
	if (rc != 0) {
		launcher_show_error(app, DESKTOP_TEXT_LAUNCHER_PROPERTIES_FAILED_FORMAT, rc);
		launcher_set_mode(app, LAUNCHER_MODE_MENU);
		return;
	}

	display = info.name[0] != '\0' ? info.name : info.id;
	(void)snprintk(app->properties_text, sizeof(app->properties_text),
		       "Name: %s\n"
		       "File: %s\n"
		       "Path: %s\n"
		       "Version: %s\n"
		       "Size: %zu\n"
		       "Stack: %u\n"
		       "Heap: %u\n"
		       "Target: %s\n"
		       "Built with EDK: %s\n"
		       "Last error: %d",
		       display, app->selected_name, app->selected_path, info.version, entry.size,
		       (unsigned int)info.stack_size, (unsigned int)info.heap_size, info.target,
		       info.edk_version, (int)info.last_error);
	(void)zui_text_view_update(app->properties_view, &(struct zui_text_view_config){
		.title = DESKTOP_TEXT_LAUNCHER_ACTION_PROPERTIES,
		.text = app->properties_text,
		.font = ZUI_FONT_SECONDARY,
		.mode = ZUI_TEXT_VIEW_MODE_TEXT,
	});
	launcher_set_mode(app, LAUNCHER_MODE_PROPERTIES);
}

static void launcher_action_activated(struct zui_form *form, uint32_t id,
				      const struct zui_input_event *event, void *user_data)
{
	struct desktop_launcher *app = user_data;

	ARG_UNUSED(form);
	ARG_UNUSED(event);

	switch (id) {
	case LAUNCHER_ACTION_DELETE:
		launcher_open_delete(app);
		break;
	case LAUNCHER_ACTION_PROPERTIES:
		launcher_open_properties(app);
		break;
	default:
		break;
	}
}

static bool launcher_at_root(const struct zui_desktop *desktop)
{
	const char *path;

	if (desktop == NULL || desktop->launcher_picker == NULL) {
		return true;
	}

	path = zui_file_picker_path(desktop->launcher_picker);
	return path == NULL || strcmp(path, CONFIG_MBS_LLEXT_APP_DEFAULT_PATH) == 0;
}

static void launcher_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct zui_desktop *desktop = user_data;
	struct desktop_launcher *app = &launcher;
	struct zui_screen *screen = NULL;

	if (desktop == NULL) {
		return;
	}

	switch (app->mode) {
	case LAUNCHER_MODE_MENU:
		screen = zui_form_get_screen(app->action_menu);
		break;
	case LAUNCHER_MODE_DELETE:
		screen = zui_modal_get_screen(app->delete_modal);
		break;
	case LAUNCHER_MODE_PROPERTIES:
		screen = zui_text_view_get_screen(app->properties_view);
		break;
	case LAUNCHER_MODE_PICKER:
	default:
		if (desktop->launcher_picker != NULL) {
			screen = zui_file_picker_get_screen(desktop->launcher_picker);
		}
		break;
	}

	if (screen != NULL) {
		(void)zui_screen_draw(screen, draw);
	}
}

static bool launcher_input(const struct zui_input_event *event, void *user_data)
{
	struct zui_desktop *desktop = user_data;
	struct desktop_launcher *app = &launcher;
	struct zui_screen *screen = NULL;
	int rc;

	if (desktop == NULL || event == NULL || desktop->launcher_picker == NULL) {
		return false;
	}
	if (desktop_shell_should_consume_edge_event(event)) {
		return true;
	}

	if (app->mode != LAUNCHER_MODE_PICKER) {
		if (desktop_input_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
			launcher_set_mode(app, LAUNCHER_MODE_PICKER);
			return true;
		}

		switch (app->mode) {
		case LAUNCHER_MODE_MENU:
			screen = zui_form_get_screen(app->action_menu);
			break;
		case LAUNCHER_MODE_DELETE:
			if (desktop_input_is_click(event)) {
				if (event->code == ZUI_INPUT_CODE_RIGHT) {
					launcher_confirm_delete(app);
					return true;
				}
				if (event->code == ZUI_INPUT_CODE_LEFT ||
				    event->code == ZUI_INPUT_CODE_SELECT) {
					launcher_set_mode(app, LAUNCHER_MODE_MENU);
					return true;
				}
			}
			screen = zui_modal_get_screen(app->delete_modal);
			break;
		case LAUNCHER_MODE_PROPERTIES:
			screen = zui_text_view_get_screen(app->properties_view);
			break;
		default:
			break;
		}

		rc = zui_screen_submit_input(screen, event);
		if (rc > 0) {
			zui_desktop_request_redraw(desktop);
			return true;
		}
		return false;
	}

	if (desktop_input_is_click(event) && event->code == ZUI_INPUT_CODE_BACK &&
	    launcher_at_root(desktop)) {
		(void)zui_desktop_switch(desktop, MBS_DESKTOP_VIEW_MAIN_MENU);
		return true;
	}

	rc = zui_screen_submit_input(zui_file_picker_get_screen(desktop->launcher_picker), event);
	if (rc > 0) {
		zui_desktop_request_redraw(desktop);
		return true;
	}

	return false;
}

static void launcher_enter(void *user_data)
{
	struct zui_desktop *desktop = user_data;
	const char *path;

	LOG_DBG("Desktop LAUNCHER enter");
	if (desktop == NULL || desktop->launcher_picker == NULL) {
		return;
	}

	launcher_set_mode(&launcher, LAUNCHER_MODE_PICKER);
	path = zui_file_picker_path(desktop->launcher_picker);
	if (path == NULL || path[0] == '\0') {
		path = CONFIG_MBS_LLEXT_APP_DEFAULT_PATH;
	}

	(void)zui_file_picker_open(desktop->launcher_picker, path);
	(void)zui_screen_enter(zui_file_picker_get_screen(desktop->launcher_picker));
	zui_desktop_request_redraw(desktop);
}

static void launcher_exit(void *user_data)
{
	struct zui_desktop *desktop = user_data;

	if (desktop == NULL || desktop->launcher_picker == NULL) {
		return;
	}

	(void)zui_screen_exit(zui_file_picker_get_screen(desktop->launcher_picker));
}

static const struct zui_screen_ops launcher_ops = {
	.draw = launcher_draw,
	.input = launcher_input,
	.enter = launcher_enter,
	.exit = launcher_exit,
};

int desktop_launcher_init(struct zui_desktop *desktop)
{
	int rc;

	if (desktop == NULL) {
		return -EINVAL;
	}

	memset(&launcher, 0, sizeof(launcher));
	launcher.desktop = desktop;
	launcher.mode = LAUNCHER_MODE_PICKER;

	desktop->launcher_picker = zui_file_picker_create(&(struct zui_file_picker_config){
		.title = DESKTOP_TEXT_LAUNCHER_TITLE,
		.base_path = CONFIG_MBS_LLEXT_APP_DEFAULT_PATH,
		.extension = MBS_LLEXT_APP_SUFFIX,
		.hide_dot_files = true,
		.file_icon = &I_llext_10px,
		.load = launcher_load,
		.probe = launcher_probe,
		.selected = launcher_selected,
		.long_selected = launcher_selected_long,
		.user_data = &launcher,
	});
	if (desktop->launcher_picker == NULL) {
		return -ENOMEM;
	}

	launcher.action_menu = zui_form_create(&(struct zui_form_config){
		.title = DESKTOP_TEXT_LAUNCHER_TITLE,
		.items = launcher_action_items,
		.item_count = ARRAY_SIZE(launcher_action_items),
		.activated = launcher_action_activated,
		.user_data = &launcher,
	});
	launcher.delete_modal = zui_modal_create(&(struct zui_modal_config){
		.title = DESKTOP_TEXT_LAUNCHER_DELETE_TITLE,
		.left_button = DESKTOP_TEXT_COMMON_CANCEL,
		.right_button = DESKTOP_TEXT_LAUNCHER_ACTION_DELETE,
		.user_data = &launcher,
	});
	launcher.properties_view = zui_text_view_create(&(struct zui_text_view_config){
		.title = DESKTOP_TEXT_LAUNCHER_ACTION_PROPERTIES,
		.text = launcher.properties_text,
		.font = ZUI_FONT_SECONDARY,
		.mode = ZUI_TEXT_VIEW_MODE_TEXT,
	});
	if (launcher.action_menu == NULL || launcher.delete_modal == NULL ||
	    launcher.properties_view == NULL) {
		desktop_launcher_deinit(desktop);
		return -ENOMEM;
	}

	desktop->launcher_screen = zui_screen_create(&launcher_ops, desktop);
	if (desktop->launcher_screen == NULL) {
		desktop_launcher_deinit(desktop);
		return -ENOMEM;
	}

	rc = zui_desktop_register_screen(desktop, MBS_DESKTOP_VIEW_LAUNCHER,
					 desktop->launcher_screen);
	if (rc != 0) {
		desktop_launcher_deinit(desktop);
	}

	return rc;
}

void desktop_launcher_deinit(struct zui_desktop *desktop)
{
	if (desktop == NULL) {
		return;
	}

	if (desktop->router != NULL) {
		(void)zui_router_unregister_screen(desktop->router, MBS_DESKTOP_VIEW_LAUNCHER);
	}
	if (desktop->launcher_screen != NULL) {
		zui_screen_destroy(desktop->launcher_screen);
		desktop->launcher_screen = NULL;
	}
	if (desktop->launcher_picker != NULL) {
		zui_file_picker_destroy(desktop->launcher_picker);
		desktop->launcher_picker = NULL;
	}
	zui_form_destroy(launcher.action_menu);
	launcher.action_menu = NULL;
	zui_modal_destroy(launcher.delete_modal);
	launcher.delete_modal = NULL;
	zui_text_view_destroy(launcher.properties_view);
	launcher.properties_view = NULL;
	memset(&launcher, 0, sizeof(launcher));
}
