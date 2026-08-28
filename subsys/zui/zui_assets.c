/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/zui/assets.h>

#include "zui_mem.h"

#if IS_ENABLED(CONFIG_ZUI_COMMON_ICONS)
#include "zui_common_icons.h"
#endif

struct zui_asset_entry {
	uint32_t id;
#if IS_ENABLED(CONFIG_ZUI_ASSET_NAMES)
	const char *name;
#endif
	const struct zui_icon *icon;
};

struct zui_asset_pack {
	const struct zui_asset_entry *icons;
	size_t icon_count;
};

struct zui_icon_anim {
	const struct zui_icon *icon;
	uint32_t frame;
	uint8_t frame_rate;
	bool running;
	struct k_mutex lock;
	struct k_work_delayable work;
	zui_icon_anim_cb callback;
	void *user_data;
};

#if IS_ENABLED(CONFIG_ZUI_COMMON_ICONS)
#if IS_ENABLED(CONFIG_ZUI_ASSET_NAMES)
#define ZUI_ASSET_ENTRY(_id, _name, _icon)                                                         \
	{                                                                                          \
		.id = (_id),                                                                       \
		.name = (_name),                                                                   \
		.icon = &(_icon),                                                                  \
	}
#else
#define ZUI_ASSET_ENTRY(_id, _name, _icon)                                                         \
	{                                                                                          \
		.id = (_id),                                                                       \
		.icon = &(_icon),                                                                  \
	}
#endif

static const struct zui_asset_entry zui_default_icons[] = {
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_NAV_BACK, "nav-back", I_back_10px),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_FILE_DIR, "file-dir", I_dir_10px),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_FILE_DOCUMENT, "file-document", I_file_10px),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_ACTIVITY_LOADING_10, "activity-loading-10", I_loading_10px),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_STATUS_UNKNOWN, "status-unknown", I_unknown_10px),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_BUTTON_SELECT, "button-select", I_button_center_7x7),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_BUTTON_DOWN, "button-down", I_button_down_7x4),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_BUTTON_LEFT, "button-left", I_button_left_4x7),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_BUTTON_LEFT_SMALL, "button-left-small",
			I_button_left_small_3x5),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_BUTTON_RIGHT, "button-right", I_button_right_4x7),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_BUTTON_RIGHT_SMALL, "button-right-small",
			I_button_right_small_3x5),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_BUTTON_UP, "button-up", I_button_up_7x4),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_CHEVRON_DOWN_TINY, "chevron-down-tiny", I_arrow_nano_down),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_CHEVRON_UP_TINY, "chevron-up-tiny", I_arrow_nano_up),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_HASHMARK, "hashmark", I_hashmark_7x7),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_MORE_PLACEHOLDER, "more-placeholder",
			I_more_data_placeholder_5x7),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_REMOTE_ARROW_DOWN, "remote-arrow-down",
			I_infrared_arrow_down_4x8),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_REMOTE_ARROW_UP, "remote-arrow-up", I_infrared_arrow_up_4x8),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_ARROW_DOWN_SMALL, "arrow-down-small",
			I_small_arrow_down_3x5),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_ARROW_DOWN, "arrow-down", I_small_arrow_down_4x7),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_ARROW_UP_SMALL, "arrow-up-small", I_small_arrow_up_3x5),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_ARROW_UP, "arrow-up", I_small_arrow_up_4x7),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_KEY_BACKSPACE, "key-backspace", I_key_backspace_16x9),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_KEY_BACKSPACE_SELECTED, "key-backspace-selected",
			I_key_backspace_selected_16x9),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_KEY_SAVE, "key-save", I_key_save_24x11),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_KEY_SAVE_BLOCKED, "key-save-blocked",
			I_key_save_blocked_24x11),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_KEY_SAVE_BLOCKED_SELECTED, "key-save-blocked-selected",
			I_key_save_blocked_selected_24x11),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_KEY_SAVE_SELECTED, "key-save-selected",
			I_key_save_selected_24x11),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_KEY_SIGN, "key-sign", I_key_sign_21x11),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_KEY_SIGN_SELECTED, "key-sign-selected",
			I_key_sign_selected_21x11),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_PIN_ARROW_UP, "pin-arrow-up", I_pin_arrow_up_7x9),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_PIN_DPAD_ATTENTION, "pin-dpad-attention",
			I_pin_attention_dpad_29x29),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_PIN_BACK_ARROW, "pin-back-arrow", I_pin_back_arrow_10x8),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_PIN_POINTER, "pin-pointer", I_pin_pointer_5x3),
	ZUI_ASSET_ENTRY(ZUI_ASSET_ICON_PIN_STAR, "pin-star", I_pin_star_7x7),
};
#endif

static const struct zui_asset_pack zui_default_pack = {
#if IS_ENABLED(CONFIG_ZUI_COMMON_ICONS)
	.icons = zui_default_icons,
	.icon_count = ARRAY_SIZE(zui_default_icons),
#else
	.icons = NULL,
	.icon_count = 0,
#endif
};

static const struct zui_asset_entry *zui_asset_pack_find_icon(const struct zui_asset_pack *pack,
							      uint32_t icon_id)
{
	if (pack == NULL || icon_id == ZUI_ASSET_ICON_NONE) {
		return NULL;
	}

	for (size_t i = 0U; i < pack->icon_count; i++) {
		if (pack->icons[i].id == icon_id) {
			return &pack->icons[i];
		}
	}

	return NULL;
}

static uint32_t zui_icon_anim_period_ms(const struct zui_icon *icon, uint8_t frame_rate_override)
{
	uint8_t frame_rate =
		frame_rate_override != 0U ? frame_rate_override : zui_icon_frame_rate(icon);

	if (frame_rate == 0U) {
		frame_rate = 12U;
	}

	return MAX(1U, 1000U / frame_rate);
}

static void zui_icon_anim_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct zui_icon_anim *anim = CONTAINER_OF(dwork, struct zui_icon_anim, work);
	zui_icon_anim_cb callback = NULL;
	void *user_data = NULL;
	uint32_t period_ms = 0U;

	if (anim == NULL) {
		return;
	}

	k_mutex_lock(&anim->lock, K_FOREVER);

	if (!anim->running || zui_icon_frame_count(anim->icon) <= 1U) {
		k_mutex_unlock(&anim->lock);
		return;
	}

	anim->frame = (anim->frame + 1U) % zui_icon_frame_count(anim->icon);
	period_ms = zui_icon_anim_period_ms(anim->icon, anim->frame_rate);

	if (anim->callback != NULL) {
		callback = anim->callback;
		user_data = anim->user_data;
	}

	k_mutex_unlock(&anim->lock);

	if (callback != NULL) {
		callback(anim, user_data);
	}

	k_mutex_lock(&anim->lock, K_FOREVER);
	if (anim->running) {
		(void)k_work_schedule(&anim->work, K_MSEC(period_ms));
	}
	k_mutex_unlock(&anim->lock);
}

const struct zui_asset_pack *zui_asset_pack_default(void)
{
	return &zui_default_pack;
}

size_t zui_asset_pack_icon_count(const struct zui_asset_pack *pack)
{
	return pack == NULL ? 0U : pack->icon_count;
}

uint32_t zui_asset_pack_icon_id(const struct zui_asset_pack *pack, size_t index)
{
	if (pack == NULL || index >= pack->icon_count) {
		return ZUI_ASSET_ICON_NONE;
	}

	return pack->icons[index].id;
}

const char *zui_asset_pack_icon_name(const struct zui_asset_pack *pack, uint32_t icon_id)
{
#if IS_ENABLED(CONFIG_ZUI_ASSET_NAMES)
	const struct zui_asset_entry *entry = zui_asset_pack_find_icon(pack, icon_id);

	return entry == NULL ? NULL : entry->name;
#else
	ARG_UNUSED(pack);
	ARG_UNUSED(icon_id);

	return NULL;
#endif
}

const struct zui_icon *zui_asset_pack_icon_by_id(const struct zui_asset_pack *pack,
						 uint32_t icon_id)
{
	const struct zui_asset_entry *entry = zui_asset_pack_find_icon(pack, icon_id);

	return entry == NULL ? NULL : entry->icon;
}

uint16_t zui_icon_width(const struct zui_icon *icon)
{
	return icon == NULL ? 0U : icon->width;
}

uint16_t zui_icon_height(const struct zui_icon *icon)
{
	return icon == NULL ? 0U : icon->height;
}

uint8_t zui_icon_frame_count(const struct zui_icon *icon)
{
	return icon == NULL ? 0U : icon->frame_count;
}

uint8_t zui_icon_frame_rate(const struct zui_icon *icon)
{
	return icon == NULL ? 0U : icon->frame_rate;
}

const uint8_t *zui_icon_frame_data(const struct zui_icon *icon, uint32_t frame)
{
	if (icon == NULL || icon->frames == NULL || icon->frame_count == 0U ||
	    frame >= icon->frame_count) {
		return NULL;
	}

	return icon->frames[frame];
}

size_t zui_icon_frame_size(const struct zui_icon *icon, uint32_t frame)
{
	if (icon == NULL || icon->frame_sizes == NULL || icon->frame_count == 0U ||
	    frame >= icon->frame_count) {
		return 0U;
	}

	return icon->frame_sizes[frame];
}

const uint8_t *zui_icon_data(const struct zui_icon *icon)
{
	return zui_icon_frame_data(icon, 0U);
}

struct zui_icon_anim *zui_icon_anim_create(const struct zui_icon *icon)
{
	struct zui_icon_anim *anim;

	if (icon == NULL || icon->frames == NULL || icon->frame_count == 0U) {
		return NULL;
	}

	anim = zui_calloc(1U, sizeof(*anim));
	if (anim == NULL) {
		return NULL;
	}

	anim->icon = icon;
	k_mutex_init(&anim->lock);
	k_work_init_delayable(&anim->work, zui_icon_anim_work_handler);

	return anim;
}

void zui_icon_anim_destroy(struct zui_icon_anim *anim)
{
	if (anim == NULL) {
		return;
	}

	zui_icon_anim_stop(anim);
	k_mutex_lock(&anim->lock, K_FOREVER);
	anim->callback = NULL;
	anim->user_data = NULL;
	k_mutex_unlock(&anim->lock);
	zui_free(anim);
}

void zui_icon_anim_set_update_callback(struct zui_icon_anim *anim, zui_icon_anim_cb callback,
				       void *user_data)
{
	if (anim == NULL) {
		return;
	}

	k_mutex_lock(&anim->lock, K_FOREVER);
	anim->callback = callback;
	anim->user_data = user_data;
	k_mutex_unlock(&anim->lock);
}

void zui_icon_anim_set_frame_rate(struct zui_icon_anim *anim, uint8_t frame_rate)
{
	if (anim == NULL) {
		return;
	}

	k_mutex_lock(&anim->lock, K_FOREVER);
	anim->frame_rate = frame_rate;
	k_mutex_unlock(&anim->lock);
}

void zui_icon_anim_start(struct zui_icon_anim *anim)
{
	uint32_t period_ms;

	if (anim == NULL) {
		return;
	}

	k_mutex_lock(&anim->lock, K_FOREVER);
	if (anim->running || zui_icon_frame_count(anim->icon) <= 1U) {
		k_mutex_unlock(&anim->lock);
		return;
	}

	anim->running = true;
	period_ms = zui_icon_anim_period_ms(anim->icon, anim->frame_rate);
	k_mutex_unlock(&anim->lock);

	(void)k_work_schedule(&anim->work, K_MSEC(period_ms));
}

void zui_icon_anim_stop(struct zui_icon_anim *anim)
{
	struct k_work_sync sync;
	bool was_running;

	if (anim == NULL) {
		return;
	}

	k_mutex_lock(&anim->lock, K_FOREVER);
	was_running = anim->running;
	anim->running = false;
	anim->frame = 0U;
	k_mutex_unlock(&anim->lock);

	if (was_running) {
		(void)k_work_cancel_delayable_sync(&anim->work, &sync);
	}
}

const struct zui_icon *zui_icon_anim_icon(const struct zui_icon_anim *anim)
{
	return anim == NULL ? NULL : anim->icon;
}

uint16_t zui_icon_anim_width(const struct zui_icon_anim *anim)
{
	return anim == NULL ? 0U : zui_icon_width(anim->icon);
}

uint16_t zui_icon_anim_height(const struct zui_icon_anim *anim)
{
	return anim == NULL ? 0U : zui_icon_height(anim->icon);
}

uint32_t zui_icon_anim_frame(const struct zui_icon_anim *anim)
{
	uint32_t frame;

	if (anim == NULL) {
		return 0U;
	}

	k_mutex_lock((struct k_mutex *)&anim->lock, K_FOREVER);
	frame = anim->frame;
	k_mutex_unlock((struct k_mutex *)&anim->lock);

	return frame;
}

const uint8_t *zui_icon_anim_frame_data(const struct zui_icon_anim *anim)
{
	const uint8_t *data;

	if (anim == NULL) {
		return NULL;
	}

	k_mutex_lock((struct k_mutex *)&anim->lock, K_FOREVER);
	data = zui_icon_frame_data(anim->icon, anim->frame);
	k_mutex_unlock((struct k_mutex *)&anim->lock);

	return data;
}

bool zui_icon_anim_is_last_frame(const struct zui_icon_anim *anim)
{
	bool last;

	if (anim == NULL) {
		return false;
	}

	k_mutex_lock((struct k_mutex *)&anim->lock, K_FOREVER);
	last = zui_icon_frame_count(anim->icon) > 0U &&
	       anim->frame == (uint32_t)zui_icon_frame_count(anim->icon) - 1U;
	k_mutex_unlock((struct k_mutex *)&anim->lock);

	return last;
}
