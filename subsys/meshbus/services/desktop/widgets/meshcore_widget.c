/* SPDX-License-Identifier: Apache-2.0 */

#include "widget_common.h"

#include <zephyr/meshbus/meshcore.h>

struct meshcore_widget_model {
	char region[MESHCORE_WIDGET_REGION_STR_MAX];
	char channel[MESHCORE_WIDGET_CHANNEL_STR_MAX];
	char node_id[MESHCORE_WIDGET_NODE_ID_STR_MAX];
	char role[MESHCORE_WIDGET_ROLE_STR_MAX];
};

struct meshcore_widget_state {
	struct zui_screen *screen;
	struct meshcore_widget_model model;
};

static struct meshcore_widget_state meshcore_widget;

#if defined(CONFIG_MESHBUS_RADIO)
struct meshcore_widget_radio_preset {
	uint32_t frequency_hz;
	uint32_t bandwidth_hz;
	uint8_t spread_factor;
	uint8_t coding_rate;
};

static const struct meshcore_widget_radio_preset meshcore_widget_radio_presets[] = {
	{920250000U, 62500U, 8U, 5U},
	{915800000U, 250000U, 10U, 5U},
	{916575000U, 62500U, 7U, 8U},
	{923125000U, 62500U, 8U, 8U},
	{923125000U, 62500U, 8U, 5U},
	{869618000U, 62500U, 8U, 8U},
	{869432000U, 62500U, 7U, 5U},
	{433650000U, 250000U, 11U, 5U},
	{917375000U, 250000U, 11U, 5U},
	{917375000U, 62500U, 7U, 5U},
	{433375000U, 62500U, 9U, 6U},
	{869618000U, 62500U, 7U, 6U},
	{869618000U, 62500U, 8U, 8U},
	{910525000U, 62500U, 7U, 5U},
};

BUILD_ASSERT(ARRAY_SIZE(meshcore_widget_radio_presets) ==
	     DESKTOP_TEXT_MESHCORE_RADIO_PRESET_COUNT,
	     "meshcore preset count mismatch");

static uint8_t meshcore_widget_radio_preset_match(const meshbus_radio_config *cfg)
{
	if (cfg == NULL) {
		return MESHCORE_RADIO_PRESET_INDEX_CUSTOM;
	}

	for (uint8_t i = 0U; i < (uint8_t)ARRAY_SIZE(meshcore_widget_radio_presets); i++) {
		const struct meshcore_widget_radio_preset *preset =
			&meshcore_widget_radio_presets[i];

		if (cfg->frequency == (uint64_t)preset->frequency_hz &&
		    cfg->bandwidth == preset->bandwidth_hz &&
		    cfg->spread_factor == preset->spread_factor &&
		    cfg->coding_rate == preset->coding_rate) {
			return i;
		}
	}

	return MESHCORE_RADIO_PRESET_INDEX_CUSTOM;
}

static const char *meshcore_widget_preset_short(uint8_t preset_idx)
{
	if (preset_idx >= DESKTOP_TEXT_MESHCORE_RADIO_PRESET_COUNT) {
		return DESKTOP_TEXT_MESHCORE_CUSTOM;
	}

	return DESKTOP_TEXT_MESHCORE_RADIO_PRESET_SHORT_VALUES[preset_idx];
}
#endif

static const char *meshcore_widget_role(uint32_t role)
{
	switch (role) {
	case MESHBUS_MESHCORE_ROLE_CHAT:
		return DESKTOP_TEXT_WIDGET_MESHCORE_ROLE_CLIENT;
	case MESHBUS_MESHCORE_ROLE_REPEATER:
		return DESKTOP_TEXT_WIDGET_MESHCORE_ROLE_REPEATER;
	case MESHBUS_MESHCORE_ROLE_ROOM:
		return DESKTOP_TEXT_WIDGET_MESHCORE_ROLE_ROOM;
	case MESHBUS_MESHCORE_ROLE_SENSOR:
		return DESKTOP_TEXT_WIDGET_MESHCORE_ROLE_SENSOR;
	default:
		return DESKTOP_TEXT_COMMON_UNKNOWN;
	}
}

static void meshcore_widget_defaults(struct meshcore_widget_model *model)
{
	if (model == NULL) {
		return;
	}

	desktop_widget_strcpy(model->region, sizeof(model->region),
			      DESKTOP_TEXT_COMMON_UNKNOWN);
	desktop_widget_strcpy(model->channel, sizeof(model->channel),
			      DESKTOP_TEXT_WIDGET_MESHCORE_SYNC_WORD_PRIVATE);
	desktop_widget_strcpy(model->node_id, sizeof(model->node_id),
			      DESKTOP_TEXT_COMMON_UNKNOWN);
	desktop_widget_strcpy(model->role, sizeof(model->role), DESKTOP_TEXT_COMMON_UNKNOWN);
}

static void meshcore_widget_snapshot(struct meshcore_widget_model *model)
{
	meshbus_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	size_t prefix_len;

	if (model == NULL) {
		return;
	}

	meshcore_widget_defaults(model);
	if (meshbus_meshcore_config_get(&cfg) == 0) {
		desktop_widget_strcpy(model->role, sizeof(model->role),
				      meshcore_widget_role(meshbus_meshcore_firmware_role_get()));
		prefix_len = MIN((size_t)cfg.public_key.size,
				 (size_t)MESHCORE_WIDGET_NODE_PREFIX_BYTES);
		desktop_widget_hex_prefix_format(model->node_id, sizeof(model->node_id),
						 cfg.public_key.bytes, prefix_len);
	}

#if defined(CONFIG_MESHBUS_RADIO)
	{
		meshbus_radio_config radio_cfg;

		if (meshbus_radio_config_get(&radio_cfg) == 0) {
			uint8_t preset_idx =
				meshcore_widget_radio_preset_match(&radio_cfg);

			desktop_widget_strcpy(model->region, sizeof(model->region),
					      meshcore_widget_preset_short(preset_idx));
		}
	}
#endif

}

static void meshcore_widget_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct meshcore_widget_state *state = user_data;
	const struct meshcore_widget_model *model =
		state != NULL ? &state->model : NULL;
	struct meshcore_widget_model fallback;

	if (model == NULL) {
		meshcore_widget_defaults(&fallback);
		model = &fallback;
	}

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	zui_draw_bitmap(draw, (struct zui_point){.x = 7, .y = 14}, 52, 24,
			ZUI_BITMAP_FORMAT_XBM, B_meshcore_52x24);
	zui_draw_round_box(draw, &(struct zui_rect){.x = 5, .y = 38, .width = 56,
						    .height = 12}, 5);
	desktop_widget_frame(draw, 1, 13, 64, 39);
	desktop_widget_frame(draw, 67, 13, 39, 18);
	desktop_widget_frame(draw, 108, 13, 18, 18);
	desktop_widget_frame(draw, 67, 34, 59, 18);

	zui_draw_text_aligned(draw, (struct zui_point){.x = 86, .y = 22},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER, model->region);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 117, .y = 22},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER, model->channel);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 96, .y = 43},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER, model->node_id);

	zui_draw_set_color(draw, ZUI_COLOR_XOR);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 32, .y = 44},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER, model->role);
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);

	zui_draw_set_color(draw, ZUI_COLOR_XOR);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 6, .y = 60},
			      ZUI_ALIGN_LEFT, ZUI_ALIGN_CENTER,
			      DESKTOP_TEXT_WIDGET_MESHCORE_VERSION);
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
}

static const struct zui_screen_ops meshcore_widget_ops = {
	.draw = meshcore_widget_draw,
};

static struct zui_screen *meshcore_widget_screen_create(
	struct meshbus_desktop_dashboard_widget *wctx)
{
	ARG_UNUSED(wctx);

	if (meshcore_widget.screen == NULL) {
		meshcore_widget_defaults(&meshcore_widget.model);
		meshcore_widget.screen =
			zui_screen_create(&meshcore_widget_ops,
					  &meshcore_widget);
	}

	return meshcore_widget.screen;
}

static uint32_t meshcore_widget_tick(struct meshbus_desktop_dashboard_widget *wctx)
{
	ARG_UNUSED(wctx);

	meshcore_widget_snapshot(&meshcore_widget.model);
	if (meshcore_widget.screen != NULL) {
		(void)zui_screen_request_redraw(meshcore_widget.screen);
	}

	return MESHCORE_WIDGET_TICK_MS;
}

MESHBUS_DESKTOP_DASHBOARD_WIDGETS_DEFINE(MESHBUS_DESKTOP_DASHBOARD_WIDGET_ID_MESHCORE,
					 MESHBUS_DESKTOP_DASHBOARD_WIDGET_TITLE_MESHCORE,
					 meshcore_widget_screen_create,
					 meshcore_widget_tick,
					 MESHBUS_DESKTOP_APP_ID_MESHCORE);
