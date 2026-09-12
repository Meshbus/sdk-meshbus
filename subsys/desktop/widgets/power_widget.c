/* SPDX-License-Identifier: Apache-2.0 */

#include "widget_common.h"

struct power_widget_model {
	bool charging;
	bool online;
	char soc_str[8];
	char voltage_str[12];
	char temp_str[12];
};

struct power_widget_snapshot {
	bool available;
	bool charging;
	bool online;
	char soc_str[8];
	char voltage_str[12];
	char temp_str[12];
};

struct power_widget_state {
	struct zui_screen *screen;
	struct power_widget_model model;
};

static struct power_widget_state power_widget;

static void power_widget_snapshot_defaults(struct power_widget_snapshot *snap)
{
	if (snap == NULL) {
		return;
	}

	snap->available = false;
	snap->charging = false;
	snap->online = false;
	desktop_widget_strcpy(snap->soc_str, sizeof(snap->soc_str),
			      DESKTOP_TEXT_WIDGET_POWER_PLACEHOLDER_SOC);
	desktop_widget_strcpy(snap->voltage_str, sizeof(snap->voltage_str),
			      DESKTOP_TEXT_WIDGET_POWER_PLACEHOLDER_VOLTAGE);
	desktop_widget_strcpy(snap->temp_str, sizeof(snap->temp_str),
			      DESKTOP_TEXT_WIDGET_POWER_TEMP_NC);
}

static void power_widget_temp_format(char *dst, size_t dst_size,
					       uint16_t temperature_dk)
{
	int32_t temp_c_x10;
	uint32_t temp_c_abs_x10;

	if (dst == NULL || dst_size == 0U) {
		return;
	}
	if (temperature_dk == 0U) {
		desktop_widget_strcpy(dst, dst_size, DESKTOP_TEXT_WIDGET_POWER_TEMP_NC);
		return;
	}

	temp_c_x10 = (int32_t)temperature_dk - 2731;
	temp_c_abs_x10 = (temp_c_x10 < 0) ? (uint32_t)(-temp_c_x10) : (uint32_t)temp_c_x10;
	(void)snprintk(dst, dst_size, DESKTOP_TEXT_POWER_VALUE_TEMPERATURE_FORMAT,
		       temp_c_x10 < 0 ? "-" : "", temp_c_abs_x10 / 10U,
		       temp_c_abs_x10 % 10U);
}

static void power_widget_snapshot_read(struct power_widget_snapshot *snap)
{
	uint16_t voltage_mv = 0U;
	uint16_t temperature_dk = 0U;
	uint8_t soc_percent = 0U;
	int ret;

	if (snap == NULL) {
		return;
	}

	power_widget_snapshot_defaults(snap);

#if defined(CONFIG_MBS_POWER)
	ret = mbs_power_fuel_gauge_get(&voltage_mv, &soc_percent, &temperature_dk);
	if (ret != 0) {
		return;
	}

	snap->available = true;
	snap->charging = mbs_power_is_charging();
	snap->online = mbs_power_is_online();
	(void)snprintk(snap->soc_str, sizeof(snap->soc_str), "%u%%",
		       (unsigned int)soc_percent);
	(void)snprintk(snap->voltage_str, sizeof(snap->voltage_str),
		       DESKTOP_TEXT_WIDGET_POWER_VOLTAGE_FORMAT,
		       (unsigned int)(voltage_mv / 1000U),
		       (unsigned int)((voltage_mv % 1000U) / 10U));
	power_widget_temp_format(snap->temp_str, sizeof(snap->temp_str),
					   temperature_dk);
#else
	ARG_UNUSED(voltage_mv);
	ARG_UNUSED(temperature_dk);
	ARG_UNUSED(soc_percent);
	ARG_UNUSED(ret);
#endif
}

static uint32_t power_widget_tick_period_ms(
	const struct power_widget_snapshot *snap)
{
	if (snap == NULL || !snap->available) {
		return POWER_WIDGET_TICK_OFF_MS;
	}
	if (snap->charging) {
		return POWER_WIDGET_TICK_ACTIVE_MS;
	}

	return POWER_WIDGET_TICK_IDLE_MS;
}

static bool power_widget_apply(struct power_widget_model *model,
					 const struct power_widget_snapshot *snap)
{
	bool changed = false;

	if (model == NULL || snap == NULL) {
		return false;
	}

	changed |= strcmp(model->soc_str, snap->soc_str) != 0;
	changed |= strcmp(model->voltage_str, snap->voltage_str) != 0;
	changed |= strcmp(model->temp_str, snap->temp_str) != 0;
	changed |= model->charging != snap->charging;
	changed |= model->online != snap->online;
	if (!changed) {
		return false;
	}

	desktop_widget_strcpy(model->soc_str, sizeof(model->soc_str), snap->soc_str);
	desktop_widget_strcpy(model->voltage_str, sizeof(model->voltage_str), snap->voltage_str);
	desktop_widget_strcpy(model->temp_str, sizeof(model->temp_str), snap->temp_str);
	model->charging = snap->charging;
	model->online = snap->online;
	return true;
}

static void power_widget_model_defaults(struct power_widget_model *model)
{
	if (model == NULL) {
		return;
	}

	model->charging = false;
	model->online = false;
	desktop_widget_strcpy(model->soc_str, sizeof(model->soc_str),
			      DESKTOP_TEXT_WIDGET_POWER_PLACEHOLDER_SOC);
	desktop_widget_strcpy(model->voltage_str, sizeof(model->voltage_str),
			      DESKTOP_TEXT_WIDGET_POWER_PLACEHOLDER_VOLTAGE);
	desktop_widget_strcpy(model->temp_str, sizeof(model->temp_str),
			      DESKTOP_TEXT_WIDGET_POWER_TEMP_NC);
}

static void power_widget_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct power_widget_state *state = user_data;
	const struct power_widget_model *model = state != NULL ? &state->model : NULL;
	const struct zui_icon *button_left =
		desktop_widget_common_icon(ZUI_ASSET_ICON_BUTTON_LEFT);
	const struct zui_icon *button_right =
		desktop_widget_common_icon(ZUI_ASSET_ICON_BUTTON_RIGHT);
	const struct zui_icon *button_select =
		desktop_widget_common_icon(ZUI_ASSET_ICON_BUTTON_SELECT);

	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);

	zui_draw_bitmap(draw, (struct zui_point){.x = 80, .y = 31}, 46, 28,
			ZUI_BITMAP_FORMAT_XBM, B_battery_46x28);
	if (model != NULL && model->charging) {
		zui_draw_icon(draw, (struct zui_point){.x = 66, .y = 48}, button_right);
	} else if (model != NULL && !model->online) {
		zui_draw_icon(draw, (struct zui_point){.x = 66, .y = 48}, button_left);
	} else {
		zui_draw_icon(draw, (struct zui_point){.x = 65, .y = 48}, button_select);
	}
	zui_draw_text(draw, (struct zui_point){.x = 94, .y = 49},
		      model != NULL ? model->soc_str : DESKTOP_TEXT_WIDGET_POWER_PLACEHOLDER_SOC);

	zui_draw_bitmap(draw, (struct zui_point){.x = 2, .y = 43}, 17, 17,
			ZUI_BITMAP_FORMAT_XBM, B_chip_17x17);
	zui_draw_icon(draw, (struct zui_point){.x = 26, .y = 48}, button_left);

	zui_draw_bitmap(draw, (struct zui_point){.x = 5, .y = 17}, 11, 11,
			ZUI_BITMAP_FORMAT_XBM, B_chip_11x11);
	if (model != NULL && model->online) {
		zui_draw_icon(draw, (struct zui_point){.x = 27, .y = 19}, button_right);
	}

	zui_draw_line(draw, (struct zui_point){.x = 21, .y = 51},
		      (struct zui_point){.x = 24, .y = 51});
	zui_draw_line(draw, (struct zui_point){.x = 32, .y = 51},
		      (struct zui_point){.x = 48, .y = 51});
	zui_draw_line(draw, (struct zui_point){.x = 63, .y = 51},
		      (struct zui_point){.x = 50, .y = 51});
	zui_draw_line(draw, (struct zui_point){.x = 77, .y = 51},
		      (struct zui_point){.x = 72, .y = 51});
	zui_draw_line(draw, (struct zui_point){.x = 18, .y = 22},
		      (struct zui_point){.x = 24, .y = 22});
	zui_draw_line(draw, (struct zui_point){.x = 48, .y = 22},
		      (struct zui_point){.x = 33, .y = 22});
	zui_draw_line(draw, (struct zui_point){.x = 49, .y = 25},
		      (struct zui_point){.x = 49, .y = 22});
	zui_draw_line(draw, (struct zui_point){.x = 49, .y = 51},
		      (struct zui_point){.x = 49, .y = 46});
	zui_draw_disc(draw, (struct zui_point){.x = 49, .y = 22}, 2);
	zui_draw_disc(draw, (struct zui_point){.x = 49, .y = 51}, 2);

	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	desktop_widget_frame(draw, 31, 26, 44, 20);
	zui_draw_icon(draw, (struct zui_point){.x = 33, .y = 28}, &I_voltage_16x16);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 72, .y = 36},
			      ZUI_ALIGN_RIGHT, ZUI_ALIGN_CENTER,
			      model != NULL ? model->voltage_str :
					      DESKTOP_TEXT_WIDGET_POWER_PLACEHOLDER_VOLTAGE);

	desktop_widget_frame(draw, 86, 14, 40, 11);
	zui_draw_line(draw, (struct zui_point){.x = 106, .y = 27},
		      (struct zui_point){.x = 106, .y = 29});
	zui_draw_text_aligned(draw, (struct zui_point){.x = 106, .y = 20},
			      ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER,
			      model != NULL ? model->temp_str : DESKTOP_TEXT_WIDGET_POWER_TEMP_NC);
}

static const struct zui_screen_ops power_widget_ops = {
	.draw = power_widget_draw,
};

static struct zui_screen *power_widget_screen_create(
	struct mbs_desktop_dashboard_widget *wctx)
{
	ARG_UNUSED(wctx);

	if (power_widget.screen == NULL) {
		power_widget_model_defaults(&power_widget.model);
		power_widget.screen =
			zui_screen_create(&power_widget_ops, &power_widget);
	}

	return power_widget.screen;
}

static uint32_t power_widget_tick(struct mbs_desktop_dashboard_widget *wctx)
{
	struct power_widget_snapshot snap;

	ARG_UNUSED(wctx);

	power_widget_snapshot_read(&snap);
	if (power_widget_apply(&power_widget.model, &snap) &&
	    power_widget.screen != NULL) {
		(void)zui_screen_request_redraw(power_widget.screen);
	}

	return power_widget_tick_period_ms(&snap);
}

MBS_DESKTOP_DASHBOARD_WIDGETS_DEFINE(MBS_DESKTOP_DASHBOARD_WIDGET_ID_POWER,
					 MBS_DESKTOP_DASHBOARD_WIDGET_TITLE_POWER,
					 power_widget_screen_create,
					 power_widget_tick,
					 MBS_DESKTOP_APP_ID_SYSTEM);
