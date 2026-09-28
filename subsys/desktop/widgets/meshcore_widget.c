/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "widget_common.h"

#include <meshcore/meshcore.h>

struct meshcore_widget_model {
	char node_id[MESHCORE_WIDGET_NODE_ID_STR_MAX];
	char role[MESHCORE_WIDGET_ROLE_STR_MAX];
};

struct meshcore_widget_state {
	struct zui_screen *screen;
	struct meshcore_widget_model model;
};

static struct meshcore_widget_state meshcore_widget;

static const char *meshcore_widget_role(uint32_t role)
{
	switch (role) {
	case MBS_MESHCORE_ROLE_CHAT:
		return DESKTOP_TEXT_WIDGET_MESHCORE_ROLE_CLIENT;
	case MBS_MESHCORE_ROLE_REPEATER:
		return DESKTOP_TEXT_WIDGET_MESHCORE_ROLE_REPEATER;
	case MBS_MESHCORE_ROLE_ROOM:
		return DESKTOP_TEXT_WIDGET_MESHCORE_ROLE_ROOM;
	case MBS_MESHCORE_ROLE_SENSOR:
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

	desktop_widget_strcpy(model->node_id, sizeof(model->node_id),
			      DESKTOP_TEXT_COMMON_UNKNOWN);
	desktop_widget_strcpy(model->role, sizeof(model->role), DESKTOP_TEXT_COMMON_UNKNOWN);
}

static void meshcore_widget_snapshot(struct meshcore_widget_model *model)
{
	mbs_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	size_t prefix_len;

	if (model == NULL) {
		return;
	}

	meshcore_widget_defaults(model);
	if (mbs_meshcore_config_get(&cfg) == 0) {
		desktop_widget_strcpy(model->role, sizeof(model->role),
				      meshcore_widget_role(mbs_meshcore_active_role_get()));
		prefix_len = MIN((size_t)cfg.public_key.size,
				 (size_t)MESHCORE_WIDGET_NODE_PREFIX_BYTES);
		desktop_widget_hex_prefix_format(model->node_id, sizeof(model->node_id),
						 cfg.public_key.bytes, prefix_len);
	}
}

static void meshcore_widget_draw_frame(struct zui_draw_ctx *draw)
{
	/* Info-style outer decoration, without its separate icon box. */
	zui_draw_line(draw, (struct zui_point){.x = 3, .y = 13},
		      (struct zui_point){.x = 124, .y = 13});
	zui_draw_line(draw, (struct zui_point){.x = 2, .y = 14},
		      (struct zui_point){.x = 1, .y = 15});
	zui_draw_dot(draw, (struct zui_point){.x = 1, .y = 16});
	zui_draw_dot(draw, (struct zui_point){.x = 1, .y = 18});
	zui_draw_dot(draw, (struct zui_point){.x = 1, .y = 20});
	zui_draw_line(draw, (struct zui_point){.x = 1, .y = 22},
		      (struct zui_point){.x = 1, .y = 63});
	zui_draw_line(draw, (struct zui_point){.x = 125, .y = 14},
		      (struct zui_point){.x = 126, .y = 15});
	zui_draw_line(draw, (struct zui_point){.x = 126, .y = 16},
		      (struct zui_point){.x = 126, .y = 63});
	zui_draw_line(draw, (struct zui_point){.x = 3, .y = 63},
		      (struct zui_point){.x = 8, .y = 63});
	zui_draw_dot(draw, (struct zui_point){.x = 10, .y = 63});
	zui_draw_dot(draw, (struct zui_point){.x = 12, .y = 63});
	zui_draw_dot(draw, (struct zui_point){.x = 14, .y = 63});
}

static void meshcore_widget_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct meshcore_widget_state *state = user_data;
	const struct meshcore_widget_model *model = state != NULL ? &state->model : NULL;

	if (model == NULL) {
		return;
	}

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	meshcore_widget_draw_frame(draw);
	/* Matching cards leave two blank columns before the outer right edge. */
	desktop_widget_frame(draw, 67, 17, 56, 18);
	desktop_widget_frame(draw, 67, 38, 56, 18);

	zui_draw_bitmap(draw, (struct zui_point){.x = 7, .y = 25}, 52, 24,
			ZUI_BITMAP_FORMAT_XBM, B_meshcore_52x24);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 95, .y = 26},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER, model->role);

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 95, .y = 47},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER, model->node_id);
}

static const struct zui_screen_ops meshcore_widget_ops = {
	.draw = meshcore_widget_draw,
};

static struct zui_screen *meshcore_widget_screen_create(
	struct mbs_desktop_dashboard_widget *wctx)
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

static uint32_t meshcore_widget_tick(struct mbs_desktop_dashboard_widget *wctx)
{
	ARG_UNUSED(wctx);

	meshcore_widget_snapshot(&meshcore_widget.model);
	if (meshcore_widget.screen != NULL) {
		(void)zui_screen_request_redraw(meshcore_widget.screen);
	}

	return MESHCORE_WIDGET_TICK_MS;
}

MBS_DESKTOP_DASHBOARD_WIDGETS_DEFINE(MBS_DESKTOP_DASHBOARD_WIDGET_ID_MESHCORE,
					 MBS_DESKTOP_DASHBOARD_WIDGET_TITLE_MESHCORE,
					 meshcore_widget_screen_create,
					 meshcore_widget_tick,
					 MBS_DESKTOP_APP_ID_MESHCORE);
