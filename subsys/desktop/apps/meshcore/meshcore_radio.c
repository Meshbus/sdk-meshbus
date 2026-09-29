/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "meshcore_private.h"

#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_MBS_RADIO)
#include <radio/radio.h>
#endif

#include "text/desktop_text.h"

static const struct meshcore_radio_preset meshcore_radio_presets[] = {
	{920250000U, 62500U, 8U, 5U},   {915800000U, 250000U, 10U, 5U},
	{916575000U, 62500U, 7U, 8U},   {923125000U, 62500U, 8U, 8U},
	{923125000U, 62500U, 8U, 5U},   {869618000U, 62500U, 8U, 8U},
	{869432000U, 62500U, 7U, 5U},   {433650000U, 250000U, 11U, 5U},
	{917375000U, 250000U, 11U, 5U}, {917375000U, 62500U, 7U, 5U},
	{433375000U, 62500U, 9U, 6U},   {869618000U, 62500U, 7U, 6U},
	{869618000U, 62500U, 8U, 8U},   {910525000U, 62500U, 7U, 5U},
};

BUILD_ASSERT(ARRAY_SIZE(meshcore_radio_presets) == DESKTOP_TEXT_MESHCORE_RADIO_PRESET_COUNT,
	     "meshcore preset count mismatch");

void meshcore_build_radio_preset_list(struct meshcore_app *app)
{
	if (app == NULL) {
		return;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(app->preset_items); i++) {
		app->preset_items[i] = (struct zui_list_item){
			.id = (uint32_t)i,
			.label = DESKTOP_TEXT_MESHCORE_RADIO_PRESET_NAME_VALUES[i],
		};
	}
	(void)zui_sublist_update(app->radio_preset,
				 &(struct zui_sublist_config){
					 .title = DESKTOP_TEXT_MESHCORE_RADIO_PRESET_TITLE,
					 .items = app->preset_items,
					 .item_count = ARRAY_SIZE(app->preset_items),
					 .selected = meshcore_radio_selected,
					 .user_data = app,
				 });
	if (app->radio_preset_selected_idx != MESHCORE_RADIO_PRESET_CUSTOM &&
	    app->radio_preset_selected_idx < ARRAY_SIZE(app->preset_items)) {
		(void)zui_sublist_select(app->radio_preset, app->radio_preset_selected_idx);
	}
}

uint8_t meshcore_radio_preset_match(void)
{
#if defined(CONFIG_MBS_RADIO)
	mbs_radio_config cfg;

	if (mbs_radio_config_get(&cfg) != 0) {
		return MESHCORE_RADIO_PRESET_CUSTOM;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(meshcore_radio_presets); i++) {
		const struct meshcore_radio_preset *preset = &meshcore_radio_presets[i];

		if (cfg.frequency == (uint64_t)preset->frequency_hz &&
		    cfg.bandwidth == preset->bandwidth_hz &&
		    cfg.spread_factor == preset->spread_factor &&
		    cfg.coding_rate == preset->coding_rate) {
			return (uint8_t)i;
		}
	}
#endif
	return MESHCORE_RADIO_PRESET_CUSTOM;
}
void meshcore_apply_radio_preset(struct meshcore_app *app)
{
#if defined(CONFIG_MBS_RADIO)
	mbs_radio_config cfg;
	const struct meshcore_radio_preset *preset;
	int rc;

	if (app == NULL || app->radio_preset_pending_idx >= ARRAY_SIZE(meshcore_radio_presets)) {
		return;
	}

	rc = mbs_radio_config_get(&cfg);
	if (rc != 0) {
		meshcore_apply_toast(app, false, NULL);
		meshcore_switch(app, MESHCORE_SCREEN_RADIO_PRESET);
		return;
	}

	preset = &meshcore_radio_presets[app->radio_preset_pending_idx];
	cfg.frequency = (uint64_t)preset->frequency_hz;
	cfg.bandwidth = preset->bandwidth_hz;
	cfg.spread_factor = preset->spread_factor;
	cfg.coding_rate = preset->coding_rate;
	rc = mbs_radio_config_set(&cfg);
	if (rc != 0) {
		meshcore_apply_toast(app, false, NULL);
		meshcore_switch(app, MESHCORE_SCREEN_RADIO_PRESET);
		return;
	}

	app->radio_preset_selected_idx = app->radio_preset_pending_idx;
	meshcore_apply_toast(app, true, NULL);
	meshcore_switch(app, MESHCORE_SCREEN_MENU);
#else
	ARG_UNUSED(app);
#endif
}
void meshcore_radio_selected(struct zui_sublist *list, uint32_t id, size_t index,
			     const struct zui_input_event *event, void *user_data)
{
	struct meshcore_app *app = user_data;
	const struct meshcore_radio_preset *preset;

	ARG_UNUSED(list);
	ARG_UNUSED(index);
	ARG_UNUSED(event);

	if (app == NULL || id >= ARRAY_SIZE(meshcore_radio_presets)) {
		return;
	}

	app->radio_preset_pending_idx = (uint8_t)id;
	preset = &meshcore_radio_presets[id];
	(void)snprintk(app->modal_text, sizeof(app->modal_text),
		       DESKTOP_TEXT_MESHCORE_RADIO_PRESET_DETAIL_FORMAT,
		       DESKTOP_TEXT_MESHCORE_RADIO_PRESET_NAME_VALUES[id],
		       (unsigned int)(preset->frequency_hz / 1000000U),
		       (unsigned int)((preset->frequency_hz % 1000000U) / 1000U),
		       (unsigned int)(preset->bandwidth_hz / 1000U),
		       (unsigned int)(preset->bandwidth_hz % 1000U),
		       (unsigned int)preset->spread_factor, (unsigned int)preset->coding_rate);
	meshcore_show_modal(app, MESHCORE_MODAL_RADIO_PRESET,
			    DESKTOP_TEXT_MESHCORE_RADIO_PRESET_CONFIRM_TITLE, app->modal_text,
			    DESKTOP_TEXT_COMMON_CANCEL, DESKTOP_TEXT_COMMON_APPLY);
}

static void meshcore_radio_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_draw(zui_sublist_get_screen(app->radio_preset), draw);
	}
}

static bool meshcore_radio_input(const struct zui_input_event *event, void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app == NULL) {
		return false;
	}
	if (desktop_app_input_should_consume_edge(event)) {
		return true;
	}
	if (desktop_app_input_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		meshcore_switch(app, MESHCORE_SCREEN_MENU);
		return true;
	}
	return meshcore_forward_input(zui_sublist_get_screen(app->radio_preset), event);
}

static void meshcore_radio_enter(void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		meshcore_forward_enter(zui_sublist_get_screen(app->radio_preset));
	}
}

static void meshcore_radio_exit(void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		meshcore_forward_exit(zui_sublist_get_screen(app->radio_preset));
	}
}

const struct zui_screen_ops meshcore_radio_ops = {
	.draw = meshcore_radio_draw,
	.input = meshcore_radio_input,
	.enter = meshcore_radio_enter,
	.exit = meshcore_radio_exit,
};
