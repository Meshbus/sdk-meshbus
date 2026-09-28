/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "desktop_private.h"

#include "apps/app_ids.h"
#include "assets/assets_icons.h"
#include "text/desktop_text.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zui/zui.h>

#ifndef MBS_DESKTOP_APP_REGISTRY_MAX
#define MBS_DESKTOP_APP_REGISTRY_MAX 16
#endif

LOG_MODULE_DECLARE(mbs_desktop, CONFIG_MBS_DESKTOP_LOG_LEVEL);

#if defined(CONFIG_MBS_DESKTOP_LAUNCHER)
#define DESKTOP_LAUNCHER_MENU_ID 0x100U
#endif

struct desktop_menu_sort_item {
	const struct mbs_desktop_app_desc *desc;
	mbs_desktop_app_handle_t handle;
};

void desktop_main_menu_deinit(struct zui_desktop *desktop);
static void desktop_main_menu_selected(struct zui_list *list, uint32_t id, size_t index,
				       const struct zui_input_event *event, void *user_data);

static int desktop_menu_item_cmp(const struct desktop_menu_sort_item *a,
				 const struct desktop_menu_sort_item *b)
{
	if (a->desc->menu_index != b->desc->menu_index) {
		return a->desc->menu_index < b->desc->menu_index ? -1 : 1;
	}

	return strcmp(a->desc->id != NULL ? a->desc->id : "",
		      b->desc->id != NULL ? b->desc->id : "");
}

static void desktop_menu_sort_items(struct desktop_menu_sort_item *items, size_t count)
{
	for (size_t i = 1U; i < count; i++) {
		struct desktop_menu_sort_item key = items[i];
		size_t j = i;

		while (j > 0U && desktop_menu_item_cmp(&key, &items[j - 1U]) < 0) {
			items[j] = items[j - 1U];
			j--;
		}
		items[j] = key;
	}
}

static void desktop_main_menu_icon_anim_updated(struct zui_icon_anim *anim, void *user_data)
{
	struct zui_desktop *desktop = user_data;

	ARG_UNUSED(anim);

	zui_desktop_request_redraw(desktop);
}

static struct zui_icon_anim *desktop_main_menu_icon_anim_create(struct zui_desktop *desktop,
								const struct zui_icon *icon)
{
	struct zui_icon_anim *anim;

	if (desktop == NULL || icon == NULL) {
		return NULL;
	}

	anim = zui_icon_anim_create(icon);
	if (anim == NULL) {
		return NULL;
	}

	zui_icon_anim_set_update_callback(anim, desktop_main_menu_icon_anim_updated, desktop);
	return anim;
}

static void desktop_main_menu_icon_anims_destroy(struct zui_desktop *desktop)
{
	if (desktop == NULL || desktop->main_menu_icon_anims == NULL) {
		return;
	}

	for (size_t i = 0U; i < desktop->main_menu_item_count; i++) {
		zui_icon_anim_destroy(desktop->main_menu_icon_anims[i]);
		desktop->main_menu_icon_anims[i] = NULL;
	}

	k_free(desktop->main_menu_icon_anims);
	desktop->main_menu_icon_anims = NULL;
}

static void desktop_main_menu_icon_anims_destroy_array(struct zui_icon_anim **anims,
						       size_t count)
{
	if (anims == NULL) {
		return;
	}

	for (size_t i = 0U; i < count; i++) {
		zui_icon_anim_destroy(anims[i]);
	}
	k_free(anims);
}

static const struct zui_icon *
desktop_main_menu_icon_for_desc(const struct mbs_desktop_app_desc *desc)
{
	const struct zui_asset_pack *assets = zui_asset_pack_default();
	uint32_t icon_id = ZUI_ASSET_ICON_FILE_DOCUMENT;

	if (desc != NULL && desc->menu_icon != NULL) {
		return desc->menu_icon;
	}

	return zui_asset_pack_icon_by_id(assets, icon_id);
}

static int desktop_main_menu_build(struct zui_desktop *desktop)
{
	struct desktop_menu_sort_item sorted[MBS_DESKTOP_APP_REGISTRY_MAX];
	size_t count;
	size_t launcher_count = 0U;
	size_t total_count;
	size_t item_count;

#if defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	launcher_count = 1U;
#endif
	count = mbs_desktop_app_registry_count();
	if (count > ARRAY_SIZE(sorted)) {
		count = ARRAY_SIZE(sorted);
	}

	for (size_t i = 0U; i < count; i++) {
		sorted[i].desc = mbs_desktop_app_registry_get(i);
		sorted[i].handle = mbs_desktop_app_registry_get_handle(i);
	}
	desktop_menu_sort_items(sorted, count);

	total_count = count + launcher_count;
	item_count = total_count == 0U ? 1U : total_count;
	desktop->main_menu_items = k_calloc(item_count, sizeof(*desktop->main_menu_items));
	if (desktop->main_menu_items == NULL) {
		return -ENOMEM;
	}
	desktop->main_menu_item_count = item_count;
	desktop->main_menu_icon_anims =
		k_calloc(item_count, sizeof(*desktop->main_menu_icon_anims));
	if (desktop->main_menu_icon_anims == NULL) {
		return -ENOMEM;
	}

	if (total_count == 0U) {
		desktop->main_menu_icon_anims[0] =
			desktop_main_menu_icon_anim_create(desktop, &I_error_24x24);
		desktop->main_menu_items[0] = (struct zui_list_item){
			.id = 0U,
			.label = DESKTOP_TEXT_COMMON_NO_DATA,
			.detail = NULL,
			.icon = &I_error_24x24,
			.icon_anim = desktop->main_menu_icon_anims[0],
		};
		return 0;
	}

	for (size_t i = 0U; i < count; i++) {
		const struct mbs_desktop_app_desc *desc = sorted[i].desc;
		const struct zui_icon *icon = desktop_main_menu_icon_for_desc(desc);

		desktop->main_menu_icon_anims[i] =
			desktop_main_menu_icon_anim_create(desktop, icon);

		desktop->main_menu_items[i] = (struct zui_list_item){
			.id = (uint32_t)sorted[i].handle,
			.label = desc != NULL && desc->display_name != NULL ?
				 desc->display_name : DESKTOP_TEXT_VIEW_MENU_INVALID_APP,
			.detail = NULL,
			.icon = icon,
			.icon_anim = desktop->main_menu_icon_anims[i],
		};
	}

#if defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	desktop->main_menu_icon_anims[count] =
		desktop_main_menu_icon_anim_create(desktop, &A_lanucher_14x14);
	desktop->main_menu_items[count] = (struct zui_list_item){
		.id = DESKTOP_LAUNCHER_MENU_ID,
		.label = DESKTOP_TEXT_LAUNCHER_TITLE,
		.detail = NULL,
		.icon = &A_lanucher_14x14,
		.icon_anim = desktop->main_menu_icon_anims[count],
	};
#endif

	return 0;
}

static int desktop_main_menu_refresh(struct zui_desktop *desktop)
{
	struct zui_list_item *old_items;
	struct zui_icon_anim **old_anims;
	size_t old_count;
	int ret;

	if ((desktop == NULL) || (desktop->main_menu == NULL)) {
		return -EINVAL;
	}

	old_items = desktop->main_menu_items;
	old_anims = desktop->main_menu_icon_anims;
	old_count = desktop->main_menu_item_count;
	desktop->main_menu_items = NULL;
	desktop->main_menu_icon_anims = NULL;
	desktop->main_menu_item_count = 0U;

	ret = desktop_main_menu_build(desktop);
	if (ret != 0) {
		k_free(desktop->main_menu_items);
		desktop_main_menu_icon_anims_destroy(desktop);
		desktop->main_menu_items = old_items;
		desktop->main_menu_icon_anims = old_anims;
		desktop->main_menu_item_count = old_count;
		return ret;
	}

	ret = zui_list_update(desktop->main_menu, &(struct zui_list_config){
		.items = desktop->main_menu_items,
		.item_count = desktop->main_menu_item_count,
		.selected = desktop_main_menu_selected,
		.user_data = desktop,
	});
	if (ret != 0) {
		k_free(desktop->main_menu_items);
		desktop_main_menu_icon_anims_destroy(desktop);
		desktop->main_menu_items = old_items;
		desktop->main_menu_icon_anims = old_anims;
		desktop->main_menu_item_count = old_count;
		return ret;
	}

	k_free(old_items);
	desktop_main_menu_icon_anims_destroy_array(old_anims, old_count);
	return 0;
}

static void desktop_main_menu_selected(struct zui_list *list, uint32_t id, size_t index,
				       const struct zui_input_event *event, void *user_data)
{
	struct zui_desktop *desktop = user_data;

	ARG_UNUSED(list);
	ARG_UNUSED(index);
	ARG_UNUSED(event);

	if (id == 0U) {
		return;
	}

#if defined(CONFIG_MBS_DESKTOP_LAUNCHER)
	if (id == DESKTOP_LAUNCHER_MENU_ID) {
		(void)zui_desktop_switch(desktop, MBS_DESKTOP_VIEW_LAUNCHER);
		return;
	}
#endif

	(void)desktop_open_app(desktop, (mbs_desktop_app_handle_t)id,
			       MBS_DESKTOP_VIEW_MAIN_MENU);
}

static void desktop_menu_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct zui_desktop *desktop = user_data;

	(void)zui_screen_draw(zui_list_get_screen(desktop->main_menu), draw);
}

static bool desktop_menu_input(const struct zui_input_event *event, void *user_data)
{
	struct zui_desktop *desktop = user_data;
	int ret;

	if (desktop == NULL || event == NULL) {
		return false;
	}
	if (desktop_shell_should_consume_edge_event(event)) {
		return true;
	}
	if (desktop_input_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		(void)zui_desktop_switch(desktop, MBS_DESKTOP_VIEW_DASHBOARD);
		return true;
	}

	ret = zui_screen_submit_input(zui_list_get_screen(desktop->main_menu), event);
	if (ret > 0) {
		zui_desktop_request_redraw(desktop);
		return true;
	}

	return false;
}

static void desktop_menu_enter(void *user_data)
{
	struct zui_desktop *desktop = user_data;

	LOG_DBG("Desktop MAIN_MENU enter");
	if (desktop_main_menu_refresh(desktop) != 0) {
		LOG_WRN("Desktop main menu refresh failed");
	}
	(void)zui_screen_enter(zui_list_get_screen(desktop->main_menu));
	zui_desktop_request_redraw(desktop);
}

static void desktop_menu_exit(void *user_data)
{
	struct zui_desktop *desktop = user_data;

	(void)zui_screen_exit(zui_list_get_screen(desktop->main_menu));
}

static const struct zui_screen_ops desktop_menu_ops = {
	.draw = desktop_menu_draw,
	.input = desktop_menu_input,
	.enter = desktop_menu_enter,
	.exit = desktop_menu_exit,
};

int desktop_main_menu_init(struct zui_desktop *desktop)
{
	int ret;

	if (desktop == NULL) {
		return -EINVAL;
	}

	ret = desktop_main_menu_build(desktop);
	if (ret != 0) {
		LOG_ERR("Failed to build Desktop main menu: %d", ret);
		desktop_main_menu_deinit(desktop);
		return ret;
	}

	desktop->main_menu = zui_list_create(&(struct zui_list_config){
		.items = desktop->main_menu_items,
		.item_count = desktop->main_menu_item_count,
		.selected = desktop_main_menu_selected,
		.user_data = desktop,
	});
	if (desktop->main_menu == NULL) {
		desktop_main_menu_deinit(desktop);
		return -ENOMEM;
	}

	desktop->main_menu_screen = zui_screen_create(&desktop_menu_ops, desktop);
	if (desktop->main_menu_screen == NULL) {
		desktop_main_menu_deinit(desktop);
		return -ENOMEM;
	}

	ret = zui_desktop_register_screen(desktop, MBS_DESKTOP_VIEW_MAIN_MENU,
					   desktop->main_menu_screen);
	if (ret != 0) {
		desktop_main_menu_deinit(desktop);
	}

	return ret;
}

void desktop_main_menu_deinit(struct zui_desktop *desktop)
{
	if (desktop == NULL) {
		return;
	}

	desktop_main_menu_icon_anims_destroy(desktop);
	if (desktop->router != NULL) {
		(void)zui_router_unregister_screen(desktop->router, MBS_DESKTOP_VIEW_MAIN_MENU);
	}
	if (desktop->main_menu_screen != NULL) {
		zui_screen_destroy(desktop->main_menu_screen);
		desktop->main_menu_screen = NULL;
	}
	if (desktop->main_menu != NULL) {
		zui_list_destroy(desktop->main_menu);
		desktop->main_menu = NULL;
	}
	k_free(desktop->main_menu_items);
	desktop->main_menu_items = NULL;
	desktop->main_menu_item_count = 0U;
}
