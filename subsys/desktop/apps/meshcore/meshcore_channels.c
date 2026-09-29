/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "meshcore_private.h"

#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "text/desktop_text.h"

static void meshcore_channel_label(char *buf, size_t buf_size, const mbs_channel *channel)
{
	if (buf == NULL || buf_size == 0U) {
		return;
	}

	if (channel != NULL && channel->name[0] != '\0') {
		(void)snprintk(buf, buf_size, "%s", channel->name);
		return;
	}

	meshcore_hex_short(channel != NULL ? channel->secret.bytes : NULL,
			   channel != NULL ? channel->secret.size : 0U, buf, buf_size, 3U);
}

void meshcore_build_channels_list(struct meshcore_app *app)
{
	size_t count = 0U;
	uint8_t store_size;

	if (app == NULL) {
		return;
	}

	store_size = mbs_channel_store_size();
	for (uint8_t idx = 0U; idx < store_size && count < ARRAY_SIZE(app->channel_items); idx++) {
		mbs_channel channel = meshbus_Channel_init_zero;

		if (mbs_channel_get(idx, &channel) != 0) {
			continue;
		}

		meshcore_channel_sanitize(&channel);
		meshcore_channel_label(app->channel_labels[count],
				       sizeof(app->channel_labels[count]), &channel);
		meshcore_hex_short(channel.secret.bytes, channel.secret.size,
				   app->channel_right[count], sizeof(app->channel_right[count]),
				   3U);
		app->channel_items[count] = (struct zui_list_item){
			.id = idx,
			.label = app->channel_labels[count],
			.detail = app->channel_right[count],
		};
		count++;
	}

	if (count == 0U) {
		app->channel_items[0] = (struct zui_list_item){
			.id = MESHCORE_CHANNEL_ITEM_EMPTY,
			.label = DESKTOP_TEXT_MESHCORE_CHANNELS_EMPTY,
		};
		count = 1U;
	}

	app->channel_item_count = count;
	(void)zui_sublist_update(app->channels,
				 &(struct zui_sublist_config){
					 .title = DESKTOP_TEXT_MESHCORE_CHANNELS_TITLE,
					 .items = app->channel_items,
					 .item_count = app->channel_item_count,
					 .selected = meshcore_channel_selected,
					 .user_data = app,
				 });
}

static bool meshcore_load_selected_channel(struct meshcore_app *app)
{
	mbs_channel channel = meshbus_Channel_init_zero;
	struct zui_list_item item = {0};
	size_t selected;

	if (app == NULL || app->channels == NULL) {
		return false;
	}

	selected = zui_sublist_selected(app->channels);
	if (selected >= app->channel_item_count) {
		return false;
	}
	item = app->channel_items[selected];
	if (item.id == MESHCORE_CHANNEL_ITEM_EMPTY || item.id > UINT8_MAX) {
		return false;
	}
	if (mbs_channel_get((uint8_t)item.id, &channel) != 0) {
		return false;
	}

	meshcore_channel_sanitize(&channel);
	app->selected_channel_slot = selected;
	app->selected_channel_idx = (uint8_t)item.id;
	app->channel_applied = channel;
	app->channel_editing = channel;
	return true;
}

void meshcore_build_channel_form(struct meshcore_app *app)
{
	char *buf;
	size_t value_idx = 0U;

	if (app == NULL) {
		return;
	}

	app->channel_form_item_count = 0U;
	app->channel_form_items[app->channel_form_item_count++] = (struct zui_form_item){
		.id = MESHCORE_CHANNEL_FORM_NAME,
		.label = DESKTOP_TEXT_MESHCORE_CHANNEL_SETTINGS_NAME,
		.value_text = app->channel_editing.name[0] != '\0' ? app->channel_editing.name
								   : DESKTOP_TEXT_COMMON_NONE,
	};
	buf = meshcore_value_buf(app, &value_idx);
	meshcore_hex_short(app->channel_editing.secret.bytes, app->channel_editing.secret.size, buf,
			   MESHCORE_VALUE_BUF_SIZE, 4U);
	app->channel_form_items[app->channel_form_item_count++] = (struct zui_form_item){
		.id = MESHCORE_CHANNEL_FORM_SECRET,
		.label = DESKTOP_TEXT_MESHCORE_CHANNEL_SETTINGS_SECRET,
		.value_text = buf,
	};
	app->channel_form_items[app->channel_form_item_count++] = (struct zui_form_item){
		.id = MESHCORE_CHANNEL_FORM_APPLY,
		.label = DESKTOP_TEXT_COMMON_ACTION_APPLY,
	};
	app->channel_form_items[app->channel_form_item_count++] = (struct zui_form_item){
		.id = MESHCORE_CHANNEL_FORM_RESET,
		.label = DESKTOP_TEXT_COMMON_ACTION_RESET,
	};

	(void)zui_form_update(app->channel_form,
			      &(struct zui_form_config){
				      .title = DESKTOP_TEXT_MESHCORE_CHANNELS_TITLE,
				      .items = app->channel_form_items,
				      .item_count = app->channel_form_item_count,
				      .activated = meshcore_channel_form_activated,
				      .user_data = app,
			      });
}
void meshcore_apply_channel(struct meshcore_app *app)
{
	mbs_channel channel;
	int rc;

	if (app == NULL || app->selected_channel_idx == UINT8_MAX) {
		return;
	}

	channel = app->channel_editing;
	meshcore_channel_sanitize(&channel);
	rc = mbs_channel_set(app->selected_channel_idx, channel.secret.bytes,
				 channel.secret.size, channel.name);
	if (rc != 0) {
		meshcore_apply_toast(app, false, NULL);
		meshcore_build_channel_form(app);
		return;
	}

	app->channel_applied = channel;
	app->channel_editing = channel;
	meshcore_apply_toast(app, true, NULL);
	meshcore_build_channels_list(app);
	meshcore_switch(app, MESHCORE_SCREEN_CHANNELS);
}

void meshcore_reset_channel(struct meshcore_app *app)
{
	int rc;

	if (app == NULL || app->selected_channel_idx == UINT8_MAX) {
		return;
	}

	rc = mbs_channel_reset(app->selected_channel_idx);
	if (rc != 0) {
		meshcore_apply_toast(app, false, NULL);
		meshcore_build_channel_form(app);
		return;
	}

	meshcore_build_channels_list(app);
	meshcore_apply_toast(app, true, NULL);
	meshcore_switch(app, MESHCORE_SCREEN_CHANNELS);
}

void meshcore_channel_selected(struct zui_sublist *list, uint32_t id, size_t index,
			       const struct zui_input_event *event, void *user_data)
{
	struct meshcore_app *app = user_data;

	ARG_UNUSED(list);
	ARG_UNUSED(index);
	ARG_UNUSED(event);

	if (app == NULL || id == MESHCORE_CHANNEL_ITEM_EMPTY) {
		return;
	}

	if (!meshcore_load_selected_channel(app)) {
		meshcore_apply_toast(app, false, NULL);
		return;
	}

	meshcore_build_channel_form(app);
	meshcore_switch(app, MESHCORE_SCREEN_CHANNEL_SETTINGS);
}
void meshcore_channel_form_activated(struct zui_form *form, uint32_t id,
				     const struct zui_input_event *event, void *user_data)
{
	struct meshcore_app *app = user_data;

	ARG_UNUSED(form);
	ARG_UNUSED(event);

	if (app == NULL) {
		return;
	}

	switch (id) {
	case MESHCORE_CHANNEL_FORM_NAME:
		meshcore_open_name_editor(app, MESHCORE_TEXT_CHANNEL_NAME,
					  DESKTOP_TEXT_MESHCORE_CHANNEL_SETTINGS_NAME,
					  app->channel_editing.name,
					  MESHCORE_SCREEN_CHANNEL_SETTINGS);
		break;
	case MESHCORE_CHANNEL_FORM_SECRET:
		meshcore_show_channel_secret_detail(app, app->channel_editing.secret.bytes,
						    app->channel_editing.secret.size);
		break;
	case MESHCORE_CHANNEL_FORM_APPLY:
		meshcore_apply_channel(app);
		break;
	case MESHCORE_CHANNEL_FORM_RESET:
		meshcore_show_modal(app, MESHCORE_MODAL_CHANNEL_RESET, DESKTOP_TEXT_COMMON_RESET,
				    DESKTOP_TEXT_COMMON_RESET_CONFIRM_TEXT,
				    DESKTOP_TEXT_COMMON_CANCEL, DESKTOP_TEXT_COMMON_OK);
		break;
	default:
		break;
	}
}
static void meshcore_channel_form_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_draw(zui_form_get_screen(app->channel_form), draw);
	}
}

static bool meshcore_channel_form_input(const struct zui_input_event *event, void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app == NULL) {
		return false;
	}
	if (desktop_app_input_should_consume_edge(event)) {
		return true;
	}
	if (desktop_app_input_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		app->channel_editing = app->channel_applied;
		meshcore_switch(app, MESHCORE_SCREEN_CHANNELS);
		return true;
	}
	if (desktop_app_input_is_long_press(event) && event->code == ZUI_INPUT_CODE_SELECT) {
		meshcore_apply_channel(app);
		return true;
	}
	if (meshcore_forward_input(zui_form_get_screen(app->channel_form), event)) {
		return true;
	}
	return false;
}

static void meshcore_channel_form_enter(void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		meshcore_forward_enter(zui_form_get_screen(app->channel_form));
	}
}

static void meshcore_channel_form_exit(void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		meshcore_forward_exit(zui_form_get_screen(app->channel_form));
	}
}

static void meshcore_channels_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_draw(zui_sublist_get_screen(app->channels), draw);
	}
}

static bool meshcore_channels_input(const struct zui_input_event *event, void *user_data)
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
	return meshcore_forward_input(zui_sublist_get_screen(app->channels), event);
}

static void meshcore_channels_enter(void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		meshcore_forward_enter(zui_sublist_get_screen(app->channels));
	}
}

static void meshcore_channels_exit(void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		meshcore_forward_exit(zui_sublist_get_screen(app->channels));
	}
}

const struct zui_screen_ops meshcore_channels_ops = {
	.draw = meshcore_channels_draw,
	.input = meshcore_channels_input,
	.enter = meshcore_channels_enter,
	.exit = meshcore_channels_exit,
};

const struct zui_screen_ops meshcore_channel_form_ops = {
	.draw = meshcore_channel_form_draw,
	.input = meshcore_channel_form_input,
	.enter = meshcore_channel_form_enter,
	.exit = meshcore_channel_form_exit,
};
