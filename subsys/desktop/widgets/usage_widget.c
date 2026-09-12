/* SPDX-License-Identifier: Apache-2.0 */

#include "widget_common.h"

struct usage_widget_state {
	bool sys_valid;
	float sys_ratio;
	float zui_ratio;
	char sys_text[24];
	char zui_text[24];
};

static struct usage_widget_state usage_widget;

static bool usage_widget_format_heap(char *text, size_t text_size, float *ratio,
				     size_t used, size_t free)
{
	size_t total = used + free;

	if (total == 0U) {
		*ratio = 0.0f;
		(void)snprintk(text, text_size, "%s",
			       DESKTOP_TEXT_WIDGET_USAGE_NOT_AVAILABLE);
		return false;
	}

	*ratio = desktop_widget_usage_ratio(used, total);
	(void)snprintk(text, text_size, DESKTOP_TEXT_WIDGET_USAGE_VALUE_FORMAT,
		       (unsigned int)(*ratio * 100.0f + 0.5f),
		       (unsigned int)(used / 1024U),
		       (unsigned int)((used % 1024U) * 10U / 1024U));
	return true;
}

static void usage_widget_refresh(struct usage_widget_state *state)
{
	struct sys_memory_stats sys_stats;
	struct zui_runtime_stats zui_stats;
	bool ok;

	if (state == NULL) {
		return;
	}

	ok = desktop_widget_system_heap_stats_get(&sys_stats);
	if (ok) {
		state->sys_valid = usage_widget_format_heap(
			state->sys_text, sizeof(state->sys_text), &state->sys_ratio,
			sys_stats.allocated_bytes, sys_stats.free_bytes);
	} else {
		state->sys_valid = false;
		state->sys_ratio = 0.0f;
		(void)snprintk(state->sys_text, sizeof(state->sys_text), "%s",
			       DESKTOP_TEXT_WIDGET_USAGE_NOT_AVAILABLE);
	}

	ok = zui_get_runtime_stats(&zui_stats) == 0 && zui_stats.heap_stats_available;
	if (ok) {
		(void)usage_widget_format_heap(state->zui_text, sizeof(state->zui_text),
					       &state->zui_ratio,
					       zui_stats.heap_allocated_bytes,
					       zui_stats.heap_free_bytes);
	} else {
		state->zui_ratio = 0.0f;
		(void)snprintk(state->zui_text, sizeof(state->zui_text), "%s",
			       DESKTOP_TEXT_WIDGET_USAGE_NOT_AVAILABLE);
	}
}

static void usage_widget_draw(struct zui_draw_ctx *draw, void *user_data)
{
	const struct usage_widget_state *state = user_data;
	const char *sys_text = DESKTOP_TEXT_WIDGET_USAGE_NOT_AVAILABLE;
	const char *zui_text = DESKTOP_TEXT_WIDGET_USAGE_NOT_AVAILABLE;
	float sys_ratio = 0.0f;
	float zui_ratio = 0.0f;

	if (state != NULL) {
		sys_text = state->sys_text[0] != '\0' ? state->sys_text :
							    DESKTOP_TEXT_WIDGET_USAGE_NOT_AVAILABLE;
		zui_text = state->zui_text[0] != '\0' ? state->zui_text :
							    DESKTOP_TEXT_WIDGET_USAGE_NOT_AVAILABLE;
		sys_ratio = state->sys_ratio;
		zui_ratio = state->zui_ratio;
	}

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);

	zui_draw_line(draw, (struct zui_point){.x = 3, .y = 13},
		      (struct zui_point){.x = 107, .y = 13});
	zui_draw_line(draw, (struct zui_point){.x = 2, .y = 14},
		      (struct zui_point){.x = 1, .y = 15});
	zui_draw_dot(draw, (struct zui_point){.x = 1, .y = 16});
	zui_draw_dot(draw, (struct zui_point){.x = 1, .y = 18});
	zui_draw_dot(draw, (struct zui_point){.x = 1, .y = 20});
	zui_draw_line(draw, (struct zui_point){.x = 1, .y = 22},
		      (struct zui_point){.x = 1, .y = 63});
	zui_draw_line(draw, (struct zui_point){.x = 126, .y = 32},
		      (struct zui_point){.x = 126, .y = 63});
	zui_draw_bitmap(draw, (struct zui_point){.x = 109, .y = 13}, 17, 17,
			ZUI_BITMAP_FORMAT_XBM, B_chip_17x17);
	desktop_widget_frame(draw, 109, 13, 17, 17);
	for (int16_t x = 3; x < 106; x += 2) {
		zui_draw_dot(draw, (struct zui_point){.x = x, .y = 30});
	}
	zui_draw_text(draw, (struct zui_point){.x = 4, .y = 26},
		      DESKTOP_TEXT_WIDGET_USAGE_TITLE_RAM_USAGE);
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	zui_draw_text(draw, (struct zui_point){.x = 4, .y = 43},
		      DESKTOP_TEXT_WIDGET_USAGE_LABEL_SYSTEM);
	zui_draw_text(draw, (struct zui_point){.x = 4, .y = 56},
		      DESKTOP_TEXT_WIDGET_USAGE_LABEL_GRAPH);

	desktop_widget_progress(draw, &(struct zui_rect){.x = 40, .y = 35, .width = 84,
							 .height = 11},
					sys_ratio, sys_text);

	desktop_widget_progress(draw, &(struct zui_rect){.x = 40, .y = 48, .width = 84,
							 .height = 11},
					zui_ratio, zui_text);
}

static const struct zui_screen_ops usage_widget_ops = {
	.draw = usage_widget_draw,
};

static struct zui_screen *usage_widget_screen_create(
	struct mbs_desktop_dashboard_widget *wctx)
{
	static struct zui_screen *screen;

	ARG_UNUSED(wctx);

	if (screen == NULL) {
		usage_widget_refresh(&usage_widget);
		screen = zui_screen_create(&usage_widget_ops, &usage_widget);
	}

	return screen;
}

static uint32_t usage_widget_tick(struct mbs_desktop_dashboard_widget *wctx)
{
	ARG_UNUSED(wctx);

	usage_widget_refresh(&usage_widget);
	return DASHBOARD_WIDGET_TICK_MS;
}

MBS_DESKTOP_DASHBOARD_WIDGETS_DEFINE(MBS_DESKTOP_DASHBOARD_WIDGET_ID_USAGE,
					 MBS_DESKTOP_DASHBOARD_WIDGET_TITLE_USAGE,
					 usage_widget_screen_create,
					 usage_widget_tick,
					 MBS_DESKTOP_APP_ID_SYSTEM);
