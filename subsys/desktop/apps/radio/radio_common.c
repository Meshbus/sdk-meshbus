/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "radio_private.h"

#include <stdarg.h>
#include <string.h>

#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "assets/assets_icons.h"
#include "text/desktop_text.h"

void radio_request_redraw(struct radio_app *app)
{
	if (app != NULL && app->host != NULL) {
		(void)zui_host_request_redraw(app->host);
	}
}

void radio_toast(struct radio_app *app, const char *title, const char *text,
			const struct zui_icon *icon, uint32_t timeout_ms)
{
	if (app == NULL || app->host == NULL) {
		return;
	}

	(void)zui_toast_show(app->host, &(struct zui_toast_config){
		.title = title,
		.text = text,
		.icon = icon,
		.timeout_ms = timeout_ms,
	});
}
void radio_format_freq_mhz(char *buf, size_t size, uint32_t hz)
{
	(void)snprintk(buf, size, "%u.%03u", (unsigned int)(hz / 1000000U),
		       (unsigned int)((hz % 1000000U) / 1000U));
}

void radio_schedule_tick(struct radio_app *app)
{
	if (app == NULL) {
		return;
	}

	if (app->current_screen == RADIO_SCREEN_CAPTURE || app->current_screen == RADIO_SCREEN_NOISE) {
		(void)k_work_reschedule(&app->tick_work, K_MSEC(RADIO_TICK_INTERVAL_MS));
	}
}

void radio_switch(struct radio_app *app, uint32_t screen_id)
{
	uint32_t previous_screen;

	if (app == NULL || app->router == NULL) {
		return;
	}

	previous_screen = app->current_screen;
	if (previous_screen == RADIO_SCREEN_CAPTURE && screen_id != RADIO_SCREEN_CAPTURE) {
		radio_capture_set_enabled(false);
		atomic_clear(&app->capture_enabled);
	}
	if ((previous_screen == RADIO_SCREEN_CAPTURE || previous_screen == RADIO_SCREEN_NOISE) &&
	    screen_id != RADIO_SCREEN_CAPTURE && screen_id != RADIO_SCREEN_NOISE) {
		(void)k_work_cancel_delayable(&app->tick_work);
	}
	app->current_screen = screen_id;
	(void)zui_router_switch(app->router, screen_id);
	radio_request_redraw(app);
	radio_schedule_tick(app);
}

void radio_tick_work(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct radio_app *app = CONTAINER_OF(dwork, struct radio_app, tick_work);
	bool redraw = false;

	if (app->current_screen == RADIO_SCREEN_CAPTURE) {
		bool running = app->capture_model.running;

		if (running != (atomic_get(&app->capture_enabled) != 0)) {
			atomic_set(&app->capture_enabled, running ? 1 : 0);
			if (running) {
				radio_capture_reset(app);
			}
			radio_capture_set_enabled(running);
			redraw = true;
		}
		redraw |= radio_capture_drain(app);
	} else if (app->current_screen == RADIO_SCREEN_NOISE) {
		uint32_t seq = app->noise.seq;

		radio_noise_collect(app);
		redraw = app->noise.seq != seq;
	}

	if (redraw) {
		radio_request_redraw(app);
	}
	radio_schedule_tick(app);
}

void radio_deferred_action_work(struct k_work *work)
{
	struct radio_app *app = CONTAINER_OF(work, struct radio_app, action_work);
	enum radio_deferred_action action =
		(enum radio_deferred_action)atomic_set(&app->deferred_action,
						       RADIO_DEFERRED_ACTION_NONE);

	switch (action) {
	case RADIO_DEFERRED_ACTION_AGC_RESET:
		mbs_radio_agc_reset();
		radio_toast(app, DESKTOP_TEXT_RADIO_TITLE, DESKTOP_TEXT_RADIO_AGC_RESET,
			    &I_done_24x24, 2000U);
		break;
	default:
		break;
	}
}

void radio_submit_deferred_action(struct radio_app *app, enum radio_deferred_action action)
{
	if (app == NULL || action == RADIO_DEFERRED_ACTION_NONE) {
		return;
	}

	atomic_set(&app->deferred_action, action);
	(void)k_work_submit(&app->action_work);
}

bool radio_require_enabled(struct radio_app *app)
{
	mbs_radio_config cfg;
	int rc;

	if (app == NULL) {
		return true;
	}

	rc = mbs_radio_config_get(&cfg);
	if (rc == 0 && !cfg.enabled) {
		radio_toast(app, DESKTOP_TEXT_RADIO_TITLE, DESKTOP_TEXT_RADIO_DISABLED,
			    &I_error_24x24, 1200U);
		return true;
	}

	return false;
}

bool radio_require_tx_allowed(struct radio_app *app)
{
	mbs_radio_config cfg;
	int rc;

	if (app == NULL) {
		return true;
	}

	rc = mbs_radio_config_get(&cfg);
	if (rc == 0 && cfg.receive_only) {
		radio_toast(app, DESKTOP_TEXT_RADIO_TITLE, DESKTOP_TEXT_RADIO_RX_ONLY_MODE,
			    &I_error_24x24, 1200U);
		return true;
	}

	return false;
}
int radio_value_buf(struct radio_app *app, size_t index, const char *fmt, ...)
{
	va_list ap;

	if (app == NULL || index >= ARRAY_SIZE(app->value_bufs)) {
		return -EINVAL;
	}

	va_start(ap, fmt);
	(void)vsnprintk(app->value_bufs[index], sizeof(app->value_bufs[index]), fmt, ap);
	va_end(ap);
	return 0;
}

void radio_form_add(struct radio_app *app, uint32_t id, const char *label,
			   const char *const *options, size_t option_count, size_t option_index)
{
	if (app == NULL || app->form_item_count >= ARRAY_SIZE(app->form_items)) {
		return;
	}

	app->form_items[app->form_item_count++] = (struct zui_form_item){
		.id = id,
		.label = label,
		.options = options,
		.option_count = option_count,
		.option_index = option_index,
	};
}

void radio_form_add_value(struct radio_app *app, uint32_t id, const char *label,
				 const char *value)
{
	if (app == NULL || app->form_item_count >= ARRAY_SIZE(app->form_items)) {
		return;
	}

	app->form_items[app->form_item_count++] = (struct zui_form_item){
		.id = id,
		.label = label,
		.value_text = value,
	};
}
