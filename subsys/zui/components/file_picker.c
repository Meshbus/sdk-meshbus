/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include "internal.h"

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>

enum zui_file_picker_entry_type {
	ZUI_FILE_PICKER_ENTRY_LOADING,
	ZUI_FILE_PICKER_ENTRY_BACK,
	ZUI_FILE_PICKER_ENTRY_DIR,
	ZUI_FILE_PICKER_ENTRY_FILE,
};

struct zui_file_picker_internal_entry {
	char path[ZUI_COMPONENT_FILE_PICKER_PATH_SIZE];
	char name[ZUI_COMPONENT_FILE_PICKER_NAME_SIZE];
	enum zui_file_picker_entry_type type;
	const struct zui_icon *icon;
	const uint8_t *icon_data;
	uint16_t icon_width;
	uint16_t icon_height;
};

struct zui_file_picker {
	struct zui_file_picker_config config;
	struct zui_file_picker_internal_entry entries[ZUI_COMPONENT_FILE_PICKER_MAX_ENTRIES];
	size_t entry_count;
	size_t selected;
	size_t list_offset;
	bool loading;
	char path[ZUI_COMPONENT_FILE_PICKER_PATH_SIZE];
	char base_path[ZUI_COMPONENT_FILE_PICKER_PATH_SIZE];
	char focus_path[ZUI_COMPONENT_FILE_PICKER_PATH_SIZE];
	char selected_path[ZUI_COMPONENT_FILE_PICKER_PATH_SIZE];
	struct k_work_delayable load_work;
	struct zui_screen *screen;
};

static void zui_file_picker_load_work_handler(struct k_work *work);

static const char *zui_file_picker_name_from_path(const char *path)
{
	const char *name;

	if (path == NULL) {
		return "";
	}

	name = strrchr(path, '/');
	return name == NULL ? path : name + 1;
}

static bool zui_file_picker_path_is_abs(const char *path)
{
	return path != NULL && path[0] == '/';
}

static int zui_file_picker_normalize_abs(char *dst, size_t dst_len, const char *path)
{
	size_t out = 0U;
	const char *p;

	if (dst == NULL || dst_len == 0U) {
		return -EINVAL;
	}

	if (path == NULL || path[0] == '\0') {
		path = "/";
	}

	if (out + 1U >= dst_len) {
		return -ENAMETOOLONG;
	}
	dst[out++] = '/';

	p = path;
	while (*p != '\0') {
		const char *segment;
		size_t len;

		while (*p == '/') {
			p++;
		}
		segment = p;
		while (*p != '\0' && *p != '/') {
			p++;
		}
		len = (size_t)(p - segment);
		if (len == 0U || (len == 1U && segment[0] == '.')) {
			continue;
		}
		if (len == 2U && segment[0] == '.' && segment[1] == '.') {
			return -EINVAL;
		}
		if (out > 1U) {
			if (out + 1U >= dst_len) {
				return -ENAMETOOLONG;
			}
			dst[out++] = '/';
		}
		if (out + len >= dst_len) {
			return -ENAMETOOLONG;
		}
		memcpy(&dst[out], segment, len);
		out += len;
	}

	dst[out] = '\0';
	return 0;
}

static int zui_file_picker_join(char *dst, size_t dst_len, const char *dir, const char *name)
{
	char path[ZUI_COMPONENT_FILE_PICKER_PATH_SIZE];
	int rc;

	if (dst == NULL || dst_len == 0U || name == NULL) {
		return -EINVAL;
	}
	if (zui_file_picker_path_is_abs(name)) {
		return zui_file_picker_normalize_abs(dst, dst_len, name);
	}

	if (dir == NULL || dir[0] == '\0') {
		rc = snprintk(path, sizeof(path), "/%s", name);
	} else {
		rc = snprintk(path, sizeof(path), "%s/%s", dir, name);
	}
	if (rc < 0 || (size_t)rc >= sizeof(path)) {
		return -ENAMETOOLONG;
	}

	return zui_file_picker_normalize_abs(dst, dst_len, path);
}

static int zui_file_picker_parent_path(const char *path, char *dst, size_t dst_len)
{
	char *slash;
	int rc;

	if (dst == NULL || dst_len == 0U) {
		return -EINVAL;
	}

	rc = zui_file_picker_normalize_abs(dst, dst_len, path);
	if (rc != 0) {
		return rc;
	}
	slash = strrchr(dst, '/');
	if (slash == NULL) {
		dst[0] = '/';
		dst[1] = '\0';
	} else if (slash == dst) {
		dst[1] = '\0';
	} else {
		*slash = '\0';
	}

	return 0;
}

static bool zui_file_picker_path_within_base(const struct zui_file_picker *selector,
					     const char *path)
{
	size_t len;

	if (selector == NULL || path == NULL) {
		return false;
	}
	if (strcmp(selector->base_path, "/") == 0) {
		return true;
	}
	len = strlen(selector->base_path);
	return strcmp(path, selector->base_path) == 0 ||
	       (strncmp(path, selector->base_path, len) == 0 && path[len] == '/');
}

static bool zui_file_picker_is_root(const struct zui_file_picker *selector)
{
	if (selector == NULL) {
		return true;
	}
	if (strcmp(selector->path, selector->base_path) == 0) {
		return true;
	}

	return selector->path[0] == '\0' || strcmp(selector->path, "/") == 0;
}

static bool zui_file_picker_accepts(struct zui_file_picker *selector, const char *path, bool is_dir)
{
	const char *name;

	if (selector == NULL || path == NULL) {
		return false;
	}

	name = zui_file_picker_name_from_path(path);
	if (selector->config.hide_dot_files && name[0] == '.') {
		return false;
	}
	if (selector->config.skip_assets && is_dir && strcmp(name, "assets") == 0) {
		return false;
	}
	if (selector->config.extension != NULL && !is_dir) {
		const char *filter = selector->config.extension;
		const char *dot = strrchr(name, '.');
		bool matched = false;

		if (strchr(filter, '*') != NULL) {
			matched = true;
		}
		while (!matched && filter[0] != '\0') {
			const char *start;
			size_t len;

			while (filter[0] == ' ' || filter[0] == '\t' || filter[0] == ',' ||
			       filter[0] == ';') {
				filter++;
			}
			start = filter;
			while (filter[0] != '\0' && filter[0] != ',' && filter[0] != ';' &&
			       filter[0] != ' ' && filter[0] != '\t') {
				filter++;
			}
			len = (size_t)(filter - start);
			if (len == 0U || dot == NULL) {
				continue;
			}
			if (start[0] == '.') {
				matched = strlen(dot) == len && strncmp(dot, start, len) == 0;
			} else {
				matched = strlen(dot + 1) == len &&
					  strncmp(dot + 1, start, len) == 0;
			}
		}
		if (!matched) {
			return false;
		}
	}
	if (selector->config.filter != NULL &&
	    !selector->config.filter(path, selector->config.user_data)) {
		return false;
	}

	return true;
}

static int zui_file_picker_add_entry(struct zui_file_picker *selector,
				     enum zui_file_picker_entry_type type, const char *path,
				     const char *name, const struct zui_icon *icon,
				     const uint8_t *icon_data, uint16_t icon_width,
				     uint16_t icon_height)
{
	struct zui_file_picker_internal_entry *entry;
	char display_name[ZUI_COMPONENT_FILE_PICKER_NAME_SIZE];
	int rc;

	if (selector == NULL || selector->entry_count >= ARRAY_SIZE(selector->entries)) {
		return -ENOMEM;
	}

	entry = &selector->entries[selector->entry_count];
	memset(entry, 0, sizeof(*entry));
	if (path != NULL) {
		rc = zui_component_copy_text(entry->path, sizeof(entry->path), path);
		if (rc != 0) {
			return -ENAMETOOLONG;
		}
	}
	rc = zui_component_copy_text(display_name, sizeof(display_name),
				     name != NULL ? name : zui_file_picker_name_from_path(path));
	if (rc != 0) {
		return -ENAMETOOLONG;
	}
	if (selector->config.hide_extension && type == ZUI_FILE_PICKER_ENTRY_FILE) {
		char *dot = strrchr(display_name, '.');

		if (dot != NULL && dot != display_name) {
			*dot = '\0';
		}
	}
	rc = zui_component_copy_text(entry->name, sizeof(entry->name), display_name);
	if (rc != 0) {
		return -ENAMETOOLONG;
	}

	selector->entry_count++;
	entry->type = type;
	entry->icon = icon;
	entry->icon_data = icon_data;
	entry->icon_width = icon_width;
	entry->icon_height = icon_height;
	return 0;
}

static void zui_file_picker_select_focus(struct zui_file_picker *selector)
{
	if (selector == NULL || selector->focus_path[0] == '\0') {
		return;
	}

	for (size_t i = 0U; i < selector->entry_count; i++) {
		if (strcmp(selector->entries[i].path, selector->focus_path) == 0) {
			selector->selected = i;
			if (selector->entry_count > ZUI_COMPONENT_FILE_PICKER_VISIBLE_ITEMS) {
				selector->list_offset = selector->selected > 0U ? selector->selected - 1U
										: 0U;
				if (selector->list_offset + ZUI_COMPONENT_FILE_PICKER_VISIBLE_ITEMS >
				    selector->entry_count) {
					selector->list_offset =
						selector->entry_count -
						ZUI_COMPONENT_FILE_PICKER_VISIBLE_ITEMS;
				}
			}
			selector->focus_path[0] = '\0';
			return;
		}
	}
}

static int zui_file_picker_load(struct zui_file_picker *selector)
{
	struct zui_file_picker_entry raw[ZUI_COMPONENT_FILE_PICKER_MAX_ENTRIES];
	size_t raw_count = 0U;
	int rc;

	if (selector == NULL) {
		return -EINVAL;
	}

	selector->entry_count = 0U;
	selector->selected = 0U;
	selector->list_offset = 0U;

	if (!zui_file_picker_is_root(selector)) {
		rc = zui_file_picker_add_entry(
			selector, ZUI_FILE_PICKER_ENTRY_BACK, selector->path, ". .",
			zui_asset_pack_icon_by_id(zui_asset_pack_default(), ZUI_ASSET_ICON_NAV_BACK),
			NULL, 0U, 0U);
		if (rc != 0) {
			selector->loading = false;
			return rc;
		}
	}

	if (selector->config.load != NULL) {
		memset(raw, 0, sizeof(raw));
		rc = selector->config.load(selector, selector->path, raw, ARRAY_SIZE(raw),
					   &raw_count, selector->config.user_data);
		if (rc != 0) {
			selector->loading = false;
			return rc;
		}

		raw_count = MIN(raw_count, ARRAY_SIZE(raw));
		for (uint8_t pass = 0U; pass < 2U; pass++) {
			for (size_t i = 0U; i < raw_count; i++) {
				char full_path[ZUI_COMPONENT_FILE_PICKER_PATH_SIZE];
				const char *name;
				const struct zui_icon *icon = raw[i].icon;
				const uint8_t *icon_data = NULL;
				uint16_t icon_width = 0U;
				uint16_t icon_height = 0U;

				if (raw[i].is_dir != (pass == 0U)) {
					continue;
				}

				rc = zui_file_picker_join(full_path, sizeof(full_path),
							  selector->path, raw[i].path);
				if (rc != 0 ||
				    !zui_file_picker_path_within_base(selector, full_path)) {
					continue;
				}
				if (!zui_file_picker_accepts(selector, full_path, raw[i].is_dir)) {
					continue;
				}
				name = zui_file_picker_name_from_path(raw[i].path);
				if (selector->config.probe != NULL) {
					struct zui_file_picker_probe_result result = {0};
					char probe_name[ZUI_COMPONENT_FILE_PICKER_NAME_SIZE] = "";

					if (selector->config.probe(full_path, probe_name,
								   sizeof(probe_name), &result,
								   selector->config.user_data)) {
						if (probe_name[0] != '\0') {
							name = probe_name;
						}
						if (result.icon != NULL) {
							icon = result.icon;
						}
						icon_data = result.icon_data;
						icon_width = result.icon_width;
						icon_height = result.icon_height;
					} else {
						continue;
					}
				}
				rc = zui_file_picker_add_entry(
					selector,
					raw[i].is_dir ? ZUI_FILE_PICKER_ENTRY_DIR
						      : ZUI_FILE_PICKER_ENTRY_FILE,
					full_path, name, icon, icon_data, icon_width, icon_height);
				if (rc != 0) {
					selector->loading = false;
					return rc;
				}
			}
		}
#if defined(CONFIG_FILE_SYSTEM)
	} else if (IS_ENABLED(CONFIG_FILE_SYSTEM)) {
		struct fs_dir_t dir;
		struct fs_dirent dirent;

		fs_dir_t_init(&dir);
		rc = fs_opendir(&dir, selector->path);
		if (rc == 0) {
			while (fs_readdir(&dir, &dirent) == 0 && dirent.name[0] != '\0' &&
			       selector->entry_count < ARRAY_SIZE(selector->entries)) {
				char full_path[ZUI_COMPONENT_FILE_PICKER_PATH_SIZE];
				bool is_dir = dirent.type == FS_DIR_ENTRY_DIR;

				rc = zui_file_picker_join(full_path, sizeof(full_path),
							  selector->path, dirent.name);
				if (rc != 0 ||
				    !zui_file_picker_path_within_base(selector, full_path) ||
				    !zui_file_picker_accepts(selector, full_path, is_dir)) {
					continue;
				}
				rc = zui_file_picker_add_entry(
					selector,
					is_dir ? ZUI_FILE_PICKER_ENTRY_DIR
					       : ZUI_FILE_PICKER_ENTRY_FILE,
					full_path, dirent.name, NULL, NULL, 0U, 0U);
				if (rc != 0) {
					(void)fs_closedir(&dir);
					selector->loading = false;
					return rc;
				}
			}
			(void)fs_closedir(&dir);
		}
#endif
	}

	if (!zui_file_picker_is_root(selector) && selector->focus_path[0] == '\0' &&
	    selector->entry_count > 1U) {
		selector->selected = 1U;
	}
	selector->loading = false;
	zui_file_picker_select_focus(selector);
	return zui_component_request_redraw(selector->screen);
}

static void zui_file_picker_load_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct zui_file_picker *selector = CONTAINER_OF(dwork, struct zui_file_picker, load_work);

	(void)zui_file_picker_load(selector);
}

static void zui_file_picker_cancel_load(struct zui_file_picker *selector)
{
	struct k_work_sync sync;

	if (selector == NULL) {
		return;
	}

	(void)k_work_cancel_delayable_sync(&selector->load_work, &sync);
}

static int zui_file_picker_schedule_load(struct zui_file_picker *selector, bool async)
{
	if (selector == NULL) {
		return -EINVAL;
	}

	selector->loading = true;
	selector->entry_count = 1U;
	selector->entries[0] = (struct zui_file_picker_internal_entry){
		.type = ZUI_FILE_PICKER_ENTRY_LOADING,
		.name = "Loading",
		.icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
						  ZUI_ASSET_ICON_ACTIVITY_LOADING_10),
	};
	(void)zui_component_request_redraw(selector->screen);

	if (async && selector->config.work_q != NULL) {
		if (k_work_schedule_for_queue(selector->config.work_q, &selector->load_work,
					      K_NO_WAIT) < 0) {
			selector->loading = false;
			return -EIO;
		}

		return 0;
	}

	return zui_file_picker_load(selector);
}

static const char *zui_file_picker_display_name(const struct zui_file_picker *selector,
						const char *path, char *buf, size_t buf_size)
{
	const char *name;

	if (path == NULL) {
		return "";
	}

	name = zui_file_picker_name_from_path(path);
	if (!selector->config.hide_extension || buf == NULL || buf_size == 0U) {
		return name;
	}

	(void)snprintf(buf, buf_size, "%s", name);
	char *dot = strrchr(buf, '.');

	if (dot != NULL && dot != buf) {
		*dot = '\0';
	}

	return buf;
}

static void zui_file_picker_draw_frame(struct zui_draw_ctx *draw, size_t row, bool scrollbar)
{
	const int16_t y = (int16_t)(3U + row * 12U);
	uint16_t width = scrollbar ? 122U : 127U;

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_box(draw, &(struct zui_rect){.x = 0, .y = y, .width = width, .height = 12});
	zui_draw_set_color(draw, ZUI_COLOR_WHITE);
	zui_draw_dot(draw, (struct zui_point){.x = 0, .y = y});
	zui_draw_dot(draw, (struct zui_point){.x = 1, .y = y});
	zui_draw_dot(draw, (struct zui_point){.x = 0, .y = (int16_t)(y + 1)});
	zui_draw_dot(draw, (struct zui_point){.x = 0, .y = (int16_t)(y + 11)});
	zui_draw_dot(draw, (struct zui_point){.x = (int16_t)(width - 1U), .y = y});
	zui_draw_dot(draw, (struct zui_point){.x = (int16_t)(width - 1U), .y = (int16_t)(y + 11)});
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
}

static void zui_file_picker_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct zui_file_picker *selector = user_data;
	bool show_scrollbar;
	const struct zui_asset_pack *pack = zui_asset_pack_default();

	zui_draw_reset(draw);
	if (selector == NULL) {
		return;
	}

	show_scrollbar = selector->entry_count > ZUI_COMPONENT_FILE_PICKER_VISIBLE_ITEMS;
	for (size_t row = 0U;
	     row < ZUI_COMPONENT_FILE_PICKER_VISIBLE_ITEMS &&
	     selector->list_offset + row < selector->entry_count;
	     row++) {
		const size_t index = selector->list_offset + row;
		const struct zui_file_picker_internal_entry *entry = &selector->entries[index];
		const struct zui_icon *icon = entry->icon;
		char label[ZUI_COMPONENT_FILE_PICKER_NAME_SIZE];
		const char *name;

		if (entry->icon_data == NULL && icon == NULL) {
			icon = entry->type == ZUI_FILE_PICKER_ENTRY_DIR
				       ? zui_asset_pack_icon_by_id(pack, ZUI_ASSET_ICON_FILE_DIR)
				       : selector->config.file_icon;
		}
		if (entry->icon_data == NULL && icon == NULL &&
		    entry->type == ZUI_FILE_PICKER_ENTRY_FILE) {
			icon = zui_asset_pack_icon_by_id(pack, ZUI_ASSET_ICON_STATUS_UNKNOWN);
		}

		if (index == selector->selected) {
			zui_file_picker_draw_frame(draw, row, show_scrollbar);
		}

		zui_draw_set_color(draw,
				   index == selector->selected ? ZUI_COLOR_WHITE : ZUI_COLOR_BLACK);
		if (icon != NULL) {
			zui_draw_icon(draw,
				      (struct zui_point){.x = 2, .y = (int16_t)(4U + row * 12U)},
				      icon);
		} else if (entry->icon_data != NULL && entry->icon_width > 0U &&
			   entry->icon_height > 0U) {
			zui_draw_bitmap(draw,
					(struct zui_point){.x = 2,
							   .y = (int16_t)(4U + row * 12U)},
					entry->icon_width, entry->icon_height,
					ZUI_BITMAP_FORMAT_MONO, entry->icon_data);
		}
		zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
		name = entry->name[0] != '\0' ? entry->name
					      : zui_file_picker_display_name(selector, entry->path,
									     label, sizeof(label));
		zui_component_draw_fit_text(
			draw, (struct zui_point){.x = 15, .y = (int16_t)(12U + row * 12U)},
			show_scrollbar ? 104U : 110U, name);
		zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	}

	if (show_scrollbar) {
		zui_component_draw_scrollbar(draw, selector->selected, selector->entry_count);
	}
	if (selector->loading) {
		zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
		zui_draw_text_aligned(
			draw,
			(struct zui_point){.x = (int16_t)(zui_draw_width(draw) / 2U),
					   .y = (int16_t)(zui_draw_height(draw) / 2U)},
			ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER, "Loading");
	} else if (selector->entry_count == 0U ||
		   (selector->entry_count == 1U &&
		    selector->entries[0].type == ZUI_FILE_PICKER_ENTRY_BACK)) {
		zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
		zui_draw_text_aligned(
			draw,
			(struct zui_point){.x = (int16_t)(zui_draw_width(draw) / 2U),
					   .y = (int16_t)(zui_draw_height(draw) / 2U)},
			ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER, "<Empty>");
	}
}

static bool zui_file_picker_is_nav_code(enum zui_input_code code)
{
	return code == ZUI_INPUT_CODE_UP || code == ZUI_INPUT_CODE_LEFT ||
	       code == ZUI_INPUT_CODE_DOWN || code == ZUI_INPUT_CODE_RIGHT ||
	       code == ZUI_INPUT_CODE_HOME || code == ZUI_INPUT_CODE_MENU;
}

static int zui_file_picker_go_parent(struct zui_file_picker *selector)
{
	char current[ZUI_COMPONENT_FILE_PICKER_PATH_SIZE];
	char parent[ZUI_COMPONENT_FILE_PICKER_PATH_SIZE];
	int rc;

	if (selector == NULL || selector->loading || zui_file_picker_is_root(selector)) {
		return selector == NULL ? -EINVAL : -ENOTSUP;
	}

	rc = zui_component_copy_text(current, sizeof(current), selector->path);
	if (rc != 0) {
		return -ENAMETOOLONG;
	}
	rc = zui_file_picker_parent_path(selector->path, parent, sizeof(parent));
	if (rc != 0) {
		return rc;
	}
	if (!zui_file_picker_path_within_base(selector, parent)) {
		rc = zui_component_copy_text(parent, sizeof(parent), selector->base_path);
		if (rc != 0) {
			return -ENAMETOOLONG;
		}
	}
	rc = zui_component_copy_text(selector->focus_path, sizeof(selector->focus_path), current);
	if (rc != 0) {
		return -ENAMETOOLONG;
	}
	rc = zui_component_copy_text(selector->path, sizeof(selector->path), parent);
	if (rc != 0) {
		return -ENAMETOOLONG;
	}
	return zui_file_picker_schedule_load(selector, selector->config.work_q != NULL);
}

static bool zui_file_picker_input(const struct zui_input_event *event, void *user_data)
{
	struct zui_file_picker *selector = user_data;
	enum zui_move move;

	if (selector == NULL || event == NULL) {
		return false;
	}

	if (event->action == ZUI_INPUT_ACTION_RELEASE && zui_file_picker_is_nav_code(event->code)) {
		return true;
	}
	if (event->code == ZUI_INPUT_CODE_BACK && event->action == ZUI_INPUT_ACTION_CLICK) {
		return zui_file_picker_go_parent(selector) == 0;
	}
	if (zui_component_input_move(event, &move) == 0) {
		(void)zui_file_picker_move(selector, move);
		return true;
	}
	if (zui_component_is_select(event) || zui_component_is_long_select(event)) {
		return zui_file_picker_choose(selector, event) == 0;
	}

	return false;
}

static const struct zui_screen_ops zui_file_picker_screen_ops = {
	.draw = zui_file_picker_draw,
	.input = zui_file_picker_input,
};

struct zui_file_picker *zui_file_picker_create(const struct zui_file_picker_config *config)
{
	struct zui_file_picker *selector;

	if (config == NULL) {
		return NULL;
	}

	selector = zui_calloc(1U, sizeof(*selector));
	if (selector == NULL) {
		return NULL;
	}

	k_work_init_delayable(&selector->load_work, zui_file_picker_load_work_handler);
	if (zui_file_picker_update(selector, config) != 0) {
		zui_file_picker_destroy(selector);
		return NULL;
	}

	selector->screen = zui_screen_create(&zui_file_picker_screen_ops, selector);
	if (selector->screen == NULL) {
		zui_file_picker_destroy(selector);
		return NULL;
	}
	(void)zui_file_picker_open(selector, config->base_path);

	return selector;
}

void zui_file_picker_destroy(struct zui_file_picker *selector)
{
	if (selector == NULL) {
		return;
	}

	zui_file_picker_cancel_load(selector);
	zui_screen_destroy(selector->screen);
	zui_free(selector);
}

struct zui_screen *zui_file_picker_get_screen(struct zui_file_picker *selector)
{
	return selector == NULL ? NULL : selector->screen;
}

int zui_file_picker_update(struct zui_file_picker *selector,
			   const struct zui_file_picker_config *config)
{
	char base_path[ZUI_COMPONENT_FILE_PICKER_PATH_SIZE];
	int rc;

	if (selector == NULL || config == NULL) {
		return -EINVAL;
	}

	rc = zui_file_picker_normalize_abs(base_path, sizeof(base_path), config->base_path);
	if (rc != 0) {
		return rc;
	}

	zui_file_picker_cancel_load(selector);
	selector->loading = false;
	selector->config = *config;
	(void)zui_component_copy_text(selector->base_path, sizeof(selector->base_path),
				      base_path);
	return zui_component_request_redraw(selector->screen);
}

const char *zui_file_picker_path(const struct zui_file_picker *selector)
{
	return selector == NULL ? NULL : selector->path;
}

int zui_file_picker_open(struct zui_file_picker *selector, const char *path)
{
	const char *target;
	char normalized[ZUI_COMPONENT_FILE_PICKER_PATH_SIZE];
	int rc;

	if (selector == NULL) {
		return -EINVAL;
	}

	target = path != NULL ? path : selector->base_path;
	if (target == NULL || target[0] == '\0') {
		target = "/";
	}

	rc = zui_file_picker_normalize_abs(normalized, sizeof(normalized), target);
	if (rc != 0) {
		return rc;
	}
	if (!zui_file_picker_path_within_base(selector, normalized)) {
		return -EINVAL;
	}

	zui_file_picker_cancel_load(selector);
	if (strrchr(zui_file_picker_name_from_path(normalized), '.') != NULL) {
		rc = zui_component_copy_text(selector->focus_path, sizeof(selector->focus_path),
					     normalized);
		if (rc != 0) {
			return -ENAMETOOLONG;
		}
		rc = zui_file_picker_parent_path(normalized, selector->path, sizeof(selector->path));
		if (rc != 0) {
			return rc;
		}
		if (!zui_file_picker_path_within_base(selector, selector->path)) {
			rc = zui_component_copy_text(selector->path, sizeof(selector->path),
						     selector->base_path);
			if (rc != 0) {
				return -ENAMETOOLONG;
			}
		}
	} else {
		selector->focus_path[0] = '\0';
		rc = zui_component_copy_text(selector->path, sizeof(selector->path), normalized);
		if (rc != 0) {
			return -ENAMETOOLONG;
		}
	}

	return zui_file_picker_schedule_load(selector, selector->config.work_q != NULL);
}

int zui_file_picker_stop(struct zui_file_picker *selector)
{
	if (selector == NULL) {
		return -EINVAL;
	}

	zui_file_picker_cancel_load(selector);
	selector->loading = false;
	selector->entry_count = 0U;
	return zui_component_request_redraw(selector->screen);
}

int zui_file_picker_move(struct zui_file_picker *selector, enum zui_move move)
{
	int rc;

	if (selector == NULL) {
		return -EINVAL;
	}

	if (selector->loading) {
		return -EBUSY;
	}

	rc = zui_component_move_index_wrap(selector->entry_count, &selector->selected, move);
	if (rc == 0) {
		if (selector->entry_count <= ZUI_COMPONENT_FILE_PICKER_VISIBLE_ITEMS) {
			selector->list_offset = 0U;
		} else if (selector->selected < selector->list_offset) {
			selector->list_offset = selector->selected;
		} else if (selector->selected >=
			   selector->list_offset + ZUI_COMPONENT_FILE_PICKER_VISIBLE_ITEMS) {
			selector->list_offset =
				selector->selected - (ZUI_COMPONENT_FILE_PICKER_VISIBLE_ITEMS - 1U);
		}
		(void)zui_component_request_redraw(selector->screen);
	}

	return rc;
}

int zui_file_picker_choose(struct zui_file_picker *selector, const struct zui_input_event *event)
{
	const struct zui_file_picker_internal_entry *entry;
	int rc;

	if (selector == NULL || selector->entry_count == 0U) {
		return selector == NULL ? -EINVAL : -ENOENT;
	}
	if (selector->loading) {
		return -EBUSY;
	}

	entry = &selector->entries[selector->selected];
	if (entry->type == ZUI_FILE_PICKER_ENTRY_BACK) {
		return zui_file_picker_go_parent(selector);
	}
	if (zui_component_is_long_select(event) && entry->type != ZUI_FILE_PICKER_ENTRY_FILE) {
		return 0;
	}
	if (entry->type == ZUI_FILE_PICKER_ENTRY_DIR) {
		return zui_file_picker_open(selector, entry->path);
	}
	if (entry->type != ZUI_FILE_PICKER_ENTRY_FILE) {
		return -EAGAIN;
	}

	rc = zui_component_copy_text(selector->selected_path, sizeof(selector->selected_path),
				     entry->path);
	if (rc != 0) {
		return -ENAMETOOLONG;
	}
	if (zui_component_is_long_select(event) && selector->config.long_selected != NULL) {
		selector->config.long_selected(selector, entry->path, event, selector->config.user_data);
	} else if (!zui_component_is_long_select(event) && selector->config.selected != NULL) {
		selector->config.selected(selector, entry->path, event, selector->config.user_data);
	}

	return 0;
}

int zui_file_picker_set_work_q(struct zui_file_picker *selector, struct k_work_q *work_q)
{
	if (selector == NULL) {
		return -EINVAL;
	}

	zui_file_picker_cancel_load(selector);
	selector->config.work_q = work_q;
	return 0;
}
