// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zui/zui.h>

static const uint8_t frame0[] = {
	0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81,
};
static const uint8_t frame1[] = {
	0x18, 0x24, 0x42, 0x81, 0x81, 0x42, 0x24, 0x18,
};
static const uint8_t frame2[] = {
	0xff, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xff,
};

static const uint8_t *anim_frames[] = {
	frame0,
	frame1,
	frame2,
};

static const struct zui_icon test_anim_icon = {
	.width = 8,
	.height = 8,
	.frame_count = 3,
	.frame_rate = 100,
	.frames = anim_frames,
};

static void anim_update_cb(struct zui_icon_anim *anim, void *user_data)
{
	uint32_t *count = user_data;

	ARG_UNUSED(anim);
	(*count)++;
}

ZTEST(zui_assets, test_default_asset_pack_lookup)
{
	const struct zui_asset_pack *pack = zui_asset_pack_default();
	const struct zui_icon *back;
	const struct zui_icon *loading;

	zassert_not_null(pack);
	zassert_equal(zui_asset_pack_icon_count(pack), ZUI_ASSET_ICON_COUNT - 1);
	zassert_equal(zui_asset_pack_icon_count(NULL), 0);
	zassert_equal(zui_asset_pack_icon_id(NULL, 0), ZUI_ASSET_ICON_NONE);
	zassert_equal(zui_asset_pack_icon_id(pack, zui_asset_pack_icon_count(pack)),
		      ZUI_ASSET_ICON_NONE);

	back = zui_asset_pack_icon_by_id(pack, ZUI_ASSET_ICON_NAV_BACK);
	zassert_not_null(back);
	zassert_equal(zui_icon_width(back), 10);
	zassert_equal(zui_icon_height(back), 10);
	zassert_equal(zui_icon_frame_count(back), 1);
	zassert_equal(zui_icon_frame_rate(back), 0);
	zassert_not_null(zui_icon_data(back));
	zassert_not_null(zui_icon_frame_data(back, 0));
	zassert_is_null(zui_icon_frame_data(back, 1));

	loading = zui_asset_pack_icon_by_id(pack, ZUI_ASSET_ICON_ACTIVITY_LOADING_10);
	zassert_not_null(loading);
	zassert_equal(zui_icon_width(loading), 10);
	zassert_equal(zui_icon_height(loading), 10);
	zassert_equal(zui_icon_frame_count(loading), 1);
	zassert_equal(zui_icon_frame_rate(loading), 0);

	zassert_equal(zui_asset_pack_icon_id(pack, 0), ZUI_ASSET_ICON_NAV_BACK);
#if IS_ENABLED(CONFIG_ZUI_ASSET_NAMES)
	zassert_str_equal(zui_asset_pack_icon_name(pack, ZUI_ASSET_ICON_NAV_BACK), "nav-back");
#else
	zassert_is_null(zui_asset_pack_icon_name(pack, ZUI_ASSET_ICON_NAV_BACK));
#endif
	zassert_is_null(zui_asset_pack_icon_name(pack, 0xFFFFFFFFU));
	zassert_is_null(zui_asset_pack_icon_by_id(pack, 0xFFFFFFFFU));
}

ZTEST(zui_assets, test_icon_accessors_handle_invalid_inputs)
{
	zassert_equal(zui_icon_width(NULL), 0);
	zassert_equal(zui_icon_height(NULL), 0);
	zassert_equal(zui_icon_frame_count(NULL), 0);
	zassert_equal(zui_icon_frame_rate(NULL), 0);
	zassert_is_null(zui_icon_data(NULL));
	zassert_is_null(zui_icon_frame_data(NULL, 0));
}

ZTEST(zui_assets, test_icon_animation_lifecycle)
{
	struct zui_icon_anim *anim;
	uint32_t updates = 0;

	zassert_is_null(zui_icon_anim_create(NULL));
	zassert_equal(zui_icon_anim_width(NULL), 0);
	zassert_equal(zui_icon_anim_height(NULL), 0);
	zassert_equal(zui_icon_anim_frame(NULL), 0);
	zassert_is_null(zui_icon_anim_icon(NULL));
	zassert_is_null(zui_icon_anim_frame_data(NULL));
	zassert_false(zui_icon_anim_is_last_frame(NULL));

	anim = zui_icon_anim_create(&test_anim_icon);
	zassert_not_null(anim);
	zassert_equal(zui_icon_anim_icon(anim), &test_anim_icon);
	zassert_equal(zui_icon_anim_width(anim), 8);
	zassert_equal(zui_icon_anim_height(anim), 8);
	zassert_equal(zui_icon_anim_frame(anim), 0);
	zassert_equal(zui_icon_anim_frame_data(anim), frame0);
	zassert_false(zui_icon_anim_is_last_frame(anim));

	zui_icon_anim_set_update_callback(anim, anim_update_cb, &updates);
	zui_icon_anim_set_frame_rate(anim, 200);
	zui_icon_anim_start(anim);
	k_msleep(20);
	zui_icon_anim_stop(anim);

	zassert_true(updates > 0);
	zassert_equal(zui_icon_anim_frame(anim), 0);
	zassert_equal(zui_icon_anim_frame_data(anim), frame0);

	zui_icon_anim_destroy(anim);
	zui_icon_anim_destroy(NULL);
	zui_icon_anim_start(NULL);
	zui_icon_anim_stop(NULL);
	zui_icon_anim_set_update_callback(NULL, NULL, NULL);
	zui_icon_anim_set_frame_rate(NULL, 10);
}

ZTEST_SUITE(zui_assets, NULL, NULL, NULL, NULL, NULL);
