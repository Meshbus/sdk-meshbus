/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ZUI asset API
 */

#ifndef ZEPHYR_INCLUDE_ZUI_ASSETS_H_
#define ZEPHYR_INCLUDE_ZUI_ASSETS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/zui/zui_types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zui_asset_pack;
struct zui_icon_anim;

typedef void (*zui_icon_anim_cb)(struct zui_icon_anim *anim, void *user_data);

enum zui_asset_icon_id {
	ZUI_ASSET_ICON_NONE = 0,
	ZUI_ASSET_ICON_NAV_BACK,
	ZUI_ASSET_ICON_FILE_DIR,
	ZUI_ASSET_ICON_FILE_DOCUMENT,
	ZUI_ASSET_ICON_ACTIVITY_LOADING_10,
	ZUI_ASSET_ICON_STATUS_UNKNOWN,
	ZUI_ASSET_ICON_BUTTON_SELECT,
	ZUI_ASSET_ICON_BUTTON_DOWN,
	ZUI_ASSET_ICON_BUTTON_LEFT,
	ZUI_ASSET_ICON_BUTTON_LEFT_SMALL,
	ZUI_ASSET_ICON_BUTTON_RIGHT,
	ZUI_ASSET_ICON_BUTTON_RIGHT_SMALL,
	ZUI_ASSET_ICON_BUTTON_UP,
	ZUI_ASSET_ICON_CHEVRON_DOWN_TINY,
	ZUI_ASSET_ICON_CHEVRON_UP_TINY,
	ZUI_ASSET_ICON_HASHMARK,
	ZUI_ASSET_ICON_MORE_PLACEHOLDER,
	ZUI_ASSET_ICON_REMOTE_ARROW_DOWN,
	ZUI_ASSET_ICON_REMOTE_ARROW_UP,
	ZUI_ASSET_ICON_ARROW_DOWN_SMALL,
	ZUI_ASSET_ICON_ARROW_DOWN,
	ZUI_ASSET_ICON_ARROW_UP_SMALL,
	ZUI_ASSET_ICON_ARROW_UP,
	ZUI_ASSET_ICON_KEY_BACKSPACE,
	ZUI_ASSET_ICON_KEY_BACKSPACE_SELECTED,
	ZUI_ASSET_ICON_KEY_SAVE,
	ZUI_ASSET_ICON_KEY_SAVE_BLOCKED,
	ZUI_ASSET_ICON_KEY_SAVE_BLOCKED_SELECTED,
	ZUI_ASSET_ICON_KEY_SAVE_SELECTED,
	ZUI_ASSET_ICON_KEY_SIGN,
	ZUI_ASSET_ICON_KEY_SIGN_SELECTED,
	ZUI_ASSET_ICON_PIN_ARROW_UP,
	ZUI_ASSET_ICON_PIN_DPAD_ATTENTION,
	ZUI_ASSET_ICON_PIN_BACK_ARROW,
	ZUI_ASSET_ICON_PIN_POINTER,
	ZUI_ASSET_ICON_PIN_STAR,
	ZUI_ASSET_ICON_COUNT,
};

const struct zui_asset_pack *zui_asset_pack_default(void);
size_t zui_asset_pack_icon_count(const struct zui_asset_pack *pack);
uint32_t zui_asset_pack_icon_id(const struct zui_asset_pack *pack, size_t index);
const char *zui_asset_pack_icon_name(const struct zui_asset_pack *pack, uint32_t icon_id);
const struct zui_icon *zui_asset_pack_icon_by_id(const struct zui_asset_pack *pack,
						 uint32_t icon_id);
uint16_t zui_icon_width(const struct zui_icon *icon);
uint16_t zui_icon_height(const struct zui_icon *icon);
uint8_t zui_icon_frame_count(const struct zui_icon *icon);
uint8_t zui_icon_frame_rate(const struct zui_icon *icon);
const uint8_t *zui_icon_frame_data(const struct zui_icon *icon, uint32_t frame);
size_t zui_icon_frame_size(const struct zui_icon *icon, uint32_t frame);
const uint8_t *zui_icon_data(const struct zui_icon *icon);

struct zui_icon_anim *zui_icon_anim_create(const struct zui_icon *icon);
void zui_icon_anim_destroy(struct zui_icon_anim *anim);
void zui_icon_anim_set_update_callback(struct zui_icon_anim *anim, zui_icon_anim_cb callback,
				       void *user_data);
void zui_icon_anim_set_frame_rate(struct zui_icon_anim *anim, uint8_t frame_rate);
void zui_icon_anim_start(struct zui_icon_anim *anim);
void zui_icon_anim_stop(struct zui_icon_anim *anim);
const struct zui_icon *zui_icon_anim_icon(const struct zui_icon_anim *anim);
uint16_t zui_icon_anim_width(const struct zui_icon_anim *anim);
uint16_t zui_icon_anim_height(const struct zui_icon_anim *anim);
uint32_t zui_icon_anim_frame(const struct zui_icon_anim *anim);
const uint8_t *zui_icon_anim_frame_data(const struct zui_icon_anim *anim);
bool zui_icon_anim_is_last_frame(const struct zui_icon_anim *anim);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ZUI_ASSETS_H_ */
