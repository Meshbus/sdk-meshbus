/* SPDX-License-Identifier: Apache-2.0 */

#include "radio_private.h"

#include <string.h>

#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#include "assets/assets_icons.h"
#include "text/desktop_text.h"

static atomic_ptr_t radio_cw_app_ptr;
static atomic_t radio_cw_cb_inflight;
static K_SEM_DEFINE(radio_cw_cb_idle_sem, 0, 1);

static void radio_cw_listener_done(void)
{
	if (atomic_dec(&radio_cw_cb_inflight) == 1) {
		k_sem_give(&radio_cw_cb_idle_sem);
	}
}

static void radio_cw_listener_cb(const struct zbus_channel *chan)
{
	struct radio_app *app;
	const struct meshbus_radio_cw_done_event *event;

	atomic_inc(&radio_cw_cb_inflight);
	app = (struct radio_app *)atomic_ptr_get(&radio_cw_app_ptr);
	if (app == NULL || chan != &meshbus_radio_cw_done_chan ||
	    atomic_get(&app->cw_running) == 0) {
		goto out;
	}

	event = zbus_chan_const_msg(chan);
	if (event == NULL) {
		goto out;
	}

	app->cw_rc = event->status;
	atomic_clear(&app->cw_running);
	(void)k_work_submit(&app->cw_done_work);

out:
	radio_cw_listener_done();
}

ZBUS_LISTENER_DEFINE_WITH_ENABLE(radio_cw_listener, radio_cw_listener_cb, false);
ZBUS_CHAN_ADD_OBS(meshbus_radio_cw_done_chan, radio_cw_listener, 1);

void radio_cw_attach_app(struct radio_app *app)
{
	atomic_ptr_set(&radio_cw_app_ptr, (atomic_ptr_val_t)app);
	(void)zbus_obs_set_enable(&radio_cw_listener, true);
}

void radio_cw_detach_app(struct radio_app *app)
{
	(void)zbus_obs_set_enable(&radio_cw_listener, false);
	if ((struct radio_app *)atomic_ptr_get(&radio_cw_app_ptr) == app) {
		atomic_ptr_set(&radio_cw_app_ptr, (atomic_ptr_val_t)NULL);
	}
}

void radio_cw_wait_idle(void)
{
	while (k_sem_take(&radio_cw_cb_idle_sem, K_NO_WAIT) == 0) {
	}
	while (atomic_get(&radio_cw_cb_inflight) > 0) {
		(void)k_sem_take(&radio_cw_cb_idle_sem, K_FOREVER);
	}
}

static const char *const radio_duration_values[] = {
	"1", "2", "3", "4", "5", "6", "7", "8", "9", "10",
};

void radio_cw_refresh(struct radio_app *app)
{
	if (app == NULL) {
		return;
	}

	app->form_item_count = 0U;
	(void)radio_value_buf(app, 0U, "%u", (unsigned int)app->cw_tx_power);
	radio_form_add(app, RADIO_CW_FORM_TX_POWER, DESKTOP_TEXT_RADIO_CW_TX_POWER,
		       radio_tx_power_values, ARRAY_SIZE(radio_tx_power_values), app->cw_tx_power);
	radio_format_freq_mhz(app->value_bufs[1], sizeof(app->value_bufs[1]), app->cw_frequency_hz);
	radio_form_add_value(app, RADIO_CW_FORM_FREQUENCY, DESKTOP_TEXT_RADIO_SETTINGS_FREQUENCY,
			     app->value_bufs[1]);
	(void)radio_value_buf(app, 2U, "%u", (unsigned int)app->cw_duration_s);
	radio_form_add(app, RADIO_CW_FORM_DURATION, DESKTOP_TEXT_RADIO_CW_DURATION,
		       radio_duration_values, ARRAY_SIZE(radio_duration_values),
		       app->cw_duration_s - 1U);
	radio_form_add_value(app, RADIO_CW_FORM_EXECUTE, DESKTOP_TEXT_RADIO_CW_EXECUTE,
			     DESKTOP_TEXT_COMMON_EMPTY);
	(void)zui_form_update(app->cw_form, &(struct zui_form_config){
		.title = DESKTOP_TEXT_RADIO_CONTINUOUS_WAVE,
		.items = app->form_items,
		.item_count = app->form_item_count,
		.changed = radio_cw_changed,
		.activated = radio_cw_activated,
		.user_data = app,
	});
}
void radio_cw_dismiss_progress(struct radio_app *app)
{
	if (app == NULL || app->cw_toast_id == 0U) {
		return;
	}

	(void)zui_toast_dismiss(app->host, app->cw_toast_id);
	app->cw_toast_id = 0U;
}

void radio_cw_done_work(struct k_work *work)
{
	struct radio_app *app = CONTAINER_OF(work, struct radio_app, cw_done_work);

	atomic_clear(&app->cw_running);
	radio_cw_dismiss_progress(app);
	if (app->cw_rc == 0) {
		radio_toast(app, DESKTOP_TEXT_RADIO_CONTINUOUS_WAVE, DESKTOP_TEXT_RADIO_FINISHED,
			    &I_done_24x24, 2000U);
	} else {
		(void)snprintk(app->cw_toast_text, sizeof(app->cw_toast_text),
			       DESKTOP_TEXT_RADIO_FORMAT_ERROR_CODE, app->cw_rc);
		radio_toast(app, DESKTOP_TEXT_RADIO_CONTINUOUS_WAVE, app->cw_toast_text,
			    &I_error_24x24, 2000U);
	}
	radio_cw_refresh(app);
	radio_request_redraw(app);
}

static int radio_cw_start(struct radio_app *app)
{
	if (app == NULL) {
		return -EINVAL;
	}
	if (atomic_get(&app->cw_running) != 0) {
		return -EBUSY;
	}

	app->cw_tx_power = MIN(app->cw_tx_power, 22U);
	app->cw_frequency_hz = CLAMP(app->cw_frequency_hz, RADIO_FREQUENCY_MIN_HZ,
				     RADIO_FREQUENCY_MAX_HZ);
	app->cw_duration_s = CLAMP(app->cw_duration_s, 1U, UINT16_MAX);
	app->cw_rc = 0;
	struct meshbus_radio_cw_request_event event = {
		.frequency_hz = app->cw_frequency_hz,
		.duration_s = app->cw_duration_s,
		.tx_power_dbm = (int8_t)app->cw_tx_power,
	};
	atomic_set(&app->cw_running, 1);
	int rc = zbus_chan_pub(&meshbus_radio_cw_chan, &event, K_NO_WAIT);
	if (rc != 0) {
		atomic_clear(&app->cw_running);
	}
	return rc;
}

static void radio_cw_execute(struct radio_app *app)
{
	int rc;

	if (app == NULL) {
		return;
	}

	radio_cw_dismiss_progress(app);
	app->cw_toast_id = zui_toast_show(app->host, &(struct zui_toast_config){
		.title = DESKTOP_TEXT_RADIO_CONTINUOUS_WAVE,
		.text = DESKTOP_TEXT_RADIO_EXECUTING,
		.icon = &I_loading_24x24,
		.timeout_ms = 120000U,
	});
	rc = radio_cw_start(app);
	if (rc != 0) {
		radio_cw_dismiss_progress(app);
		(void)snprintk(app->cw_toast_text, sizeof(app->cw_toast_text),
			       DESKTOP_TEXT_RADIO_FORMAT_ERROR_CODE, rc);
		radio_toast(app, DESKTOP_TEXT_RADIO_CONTINUOUS_WAVE, app->cw_toast_text,
			    &I_error_24x24, 2000U);
	}
}
void radio_cw_changed(struct zui_form *form, uint32_t id, size_t option_index,
			     void *user_data)
{
	struct radio_app *app = user_data;

	ARG_UNUSED(form);

	if (app == NULL) {
		return;
	}

	if (id == RADIO_CW_FORM_TX_POWER) {
		app->cw_tx_power = (uint8_t)option_index;
	} else if (id == RADIO_CW_FORM_DURATION) {
		app->cw_duration_s = (uint16_t)(option_index + 1U);
	}
	radio_cw_refresh(app);
	radio_request_redraw(app);
}

void radio_cw_activated(struct zui_form *form, uint32_t id,
			       const struct zui_input_event *event, void *user_data)
{
	struct radio_app *app = user_data;

	ARG_UNUSED(form);
	ARG_UNUSED(event);

	if (app == NULL) {
		return;
	}

	if (id == RADIO_CW_FORM_FREQUENCY) {
		radio_open_number(app, RADIO_NUMBER_CW_FREQUENCY);
	} else if (id == RADIO_CW_FORM_EXECUTE) {
		radio_cw_execute(app);
	}
}
static void radio_cw_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct radio_app *app = user_data;

	(void)zui_screen_draw(zui_form_get_screen(app->cw_form), draw);
}

static bool radio_cw_input(const struct zui_input_event *event, void *user_data)
{
	struct radio_app *app = user_data;

	if (app == NULL || event == NULL) {
		return false;
	}
	if (radio_should_consume_edge(event)) {
		return true;
	}
	if (radio_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		radio_switch(app, RADIO_SCREEN_MENU);
		return true;
	}
	if (zui_screen_submit_input(zui_form_get_screen(app->cw_form), event) > 0) {
		radio_request_redraw(app);
		return true;
	}

	return false;
}

static void radio_cw_enter(void *user_data)
{
	struct radio_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_enter(zui_form_get_screen(app->cw_form));
	}
}

static void radio_cw_exit(void *user_data)
{
	struct radio_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_exit(zui_form_get_screen(app->cw_form));
	}
}
const struct zui_screen_ops radio_cw_ops = {
	.draw = radio_cw_draw,
	.input = radio_cw_input,
	.enter = radio_cw_enter,
	.exit = radio_cw_exit,
};
