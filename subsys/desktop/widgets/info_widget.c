/* SPDX-License-Identifier: Apache-2.0 */

#include "widget_common.h"

struct info_widget_model {
	char device_name[INFO_WIDGET_DEVICE_NAME_MAX_LEN];
	char version_str[INFO_WIDGET_VERSION_MAX_LEN];
	char serial_lines[2][(INFO_WIDGET_SERIAL_MAX_LEN + 1) / 2];
};

struct info_widget_state {
	struct zui_screen *screen;
	struct info_widget_model model;
	uint8_t device_id_buf[INFO_WIDGET_DEVICE_ID_BUF_LEN];
};

static struct info_widget_state info_widget;

static void info_widget_format_device_id(char *out, size_t out_size, uint8_t *id,
						   size_t id_size)
{
	static const char hex[] = "0123456789ABCDEF";
	ssize_t id_len;
	size_t out_len = 0U;

	if (out == NULL || out_size == 0U || id == NULL || id_size == 0U) {
		return;
	}

	id_len = hwinfo_get_device_id(id, id_size);
	if (id_len <= 0) {
		desktop_widget_strcpy(out, out_size, DESKTOP_TEXT_WIDGET_INFO_SERIAL_UNKNOWN);
		return;
	}

	for (size_t i = 0U; i < (size_t)id_len && (out_len + 2U) < out_size; i++) {
		out[out_len++] = hex[id[i] >> 4];
		out[out_len++] = hex[id[i] & 0x0FU];
	}
	out[out_len] = '\0';
}

static void info_widget_snapshot(struct info_widget_state *state)
{
	char serial[INFO_WIDGET_SERIAL_MAX_LEN];
	size_t split;

	if (state == NULL) {
		return;
	}

	desktop_widget_strcpy(state->model.device_name, sizeof(state->model.device_name),
			      CONFIG_MBS_DESKTOP_DEVICE_NAME);
	desktop_widget_strcpy(state->model.version_str, sizeof(state->model.version_str),
			      APP_VERSION_STRING);
	info_widget_format_device_id(serial, sizeof(serial), state->device_id_buf,
				    sizeof(state->device_id_buf));
	if (strlen(serial) < sizeof(state->model.serial_lines[0])) {
		desktop_widget_strcpy(state->model.serial_lines[0],
				      sizeof(state->model.serial_lines[0]), serial);
		state->model.serial_lines[1][0] = '\0';
		return;
	}
	/* Two balanced lines, at most 16 hex digits each (96 px in the fixed font). */
	split = (strlen(serial) + 1U) / 2U;
	memcpy(state->model.serial_lines[0], serial, split);
	state->model.serial_lines[0][split] = '\0';
	desktop_widget_strcpy(state->model.serial_lines[1],
			      sizeof(state->model.serial_lines[1]), serial + split);
}

static void info_widget_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct info_widget_state *state = user_data;
	const struct info_widget_model *m = state != NULL ? &state->model : NULL;
	bool serial_multiline = m != NULL && m->serial_lines[1][0] != '\0';
	uint16_t version_y = serial_multiline ? 39 : 42;
	uint16_t serial_y = serial_multiline ? 54 : 57;

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
	desktop_widget_frame(draw, 109, 13, 17, 17);
	zui_draw_text(draw, (struct zui_point){.x = 116, .y = 26},
		      DESKTOP_TEXT_WIDGET_INFO_ICON_INFO);
	zui_draw_line(draw, (struct zui_point){.x = 2, .y = 30},
		      (struct zui_point){.x = 107, .y = 30});
	zui_draw_text(draw, (struct zui_point){.x = 4, .y = 26},
		      m != NULL ? m->device_name : DESKTOP_TEXT_WIDGET_INFO_PLACEHOLDER_DEVICE);
	zui_draw_line(draw, (struct zui_point){.x = 2, .y = serial_multiline ? 40 : 45},
		      (struct zui_point){.x = 125, .y = serial_multiline ? 40 : 45});
	zui_draw_line(draw, (struct zui_point){.x = 2, .y = 60},
		      (struct zui_point){.x = 125, .y = 60});
	zui_draw_line(draw, (struct zui_point){.x = 3, .y = 63},
		      (struct zui_point){.x = 8, .y = 63});
	zui_draw_dot(draw, (struct zui_point){.x = 10, .y = 63});
	zui_draw_dot(draw, (struct zui_point){.x = 12, .y = 63});
	zui_draw_dot(draw, (struct zui_point){.x = 14, .y = 63});

	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	zui_draw_text(draw, (struct zui_point){.x = 4, .y = version_y},
		      DESKTOP_TEXT_WIDGET_INFO_LABEL_VERSION);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 124, .y = version_y},
			      ZUI_ALIGN_RIGHT, ZUI_ALIGN_BOTTOM,
			      m != NULL ? m->version_str : DESKTOP_TEXT_WIDGET_INFO_PLACEHOLDER_VERSION);
	zui_draw_text(draw, (struct zui_point){.x = 4, .y = serial_y},
		      DESKTOP_TEXT_WIDGET_INFO_LABEL_SN);

	/* Identity is ASCII: 16 fixed-width digits fit between x=28 and x=124. */
	zui_draw_set_font(draw, ZUI_FONT_KEYBOARD);
	zui_draw_text_aligned(draw,
			      (struct zui_point){.x = 124, .y = serial_multiline ? 49 : serial_y},
			      ZUI_ALIGN_RIGHT, ZUI_ALIGN_BOTTOM,
			      m != NULL ? m->serial_lines[0] : DESKTOP_TEXT_WIDGET_INFO_SERIAL_UNKNOWN);
	if (serial_multiline) {
		zui_draw_text_aligned(draw, (struct zui_point){.x = 124, .y = 59},
				      ZUI_ALIGN_RIGHT, ZUI_ALIGN_BOTTOM, m->serial_lines[1]);
	}
}

static const struct zui_screen_ops info_widget_ops = {
	.draw = info_widget_draw,
};

static struct zui_screen *info_widget_screen_create(
	struct mbs_desktop_dashboard_widget *wctx)
{
	ARG_UNUSED(wctx);

	if (info_widget.screen == NULL) {
		info_widget_snapshot(&info_widget);
		info_widget.screen =
			zui_screen_create(&info_widget_ops, &info_widget);
	}

	return info_widget.screen;
}

static uint32_t info_widget_tick(struct mbs_desktop_dashboard_widget *wctx)
{
	uint64_t now_ms;
	uint32_t next_ms;

	ARG_UNUSED(wctx);

	info_widget_snapshot(&info_widget);
	if (info_widget.screen != NULL) {
		(void)zui_screen_request_redraw(info_widget.screen);
	}

	now_ms = (uint64_t)k_uptime_get();
	next_ms = INFO_WIDGET_MINUTE_MS - (uint32_t)(now_ms % INFO_WIDGET_MINUTE_MS);
	if (next_ms < INFO_WIDGET_MIN_REFRESH_MS) {
		next_ms = INFO_WIDGET_MIN_REFRESH_MS;
	}

	return next_ms;
}

MBS_DESKTOP_DASHBOARD_WIDGETS_DEFINE(MBS_DESKTOP_DASHBOARD_WIDGET_ID_INFO,
					 MBS_DESKTOP_DASHBOARD_WIDGET_TITLE_INFO,
					 info_widget_screen_create,
					 info_widget_tick,
					 MBS_DESKTOP_APP_ID_SYSTEM);
