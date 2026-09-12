/* SPDX-License-Identifier: Apache-2.0 */

#include "desktop_private.h"

#include "assets/assets_icons.h"
#include "text/desktop_text.h"

#if defined(CONFIG_ZBUS) && defined(CONFIG_MBS_BLUETOOTH)
#include <stdint.h>

#include <zephyr/kernel.h>
#include <bluetooth/bluetooth.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/zui/zui.h>

#define DESKTOP_BLUETOOTH_RESULT_DELAY_MS 250U
#define DESKTOP_BLUETOOTH_RESULT_TIMEOUT_MS 1200U
#define DESKTOP_BLUETOOTH_PAIRING_TIMEOUT_MS UINT32_MAX

enum desktop_bluetooth_notify_result {
	DESKTOP_BLUETOOTH_NOTIFY_RESULT_NONE = 0,
	DESKTOP_BLUETOOTH_NOTIFY_RESULT_CONNECTED,
	DESKTOP_BLUETOOTH_NOTIFY_RESULT_DISCONNECTED,
};

static atomic_t desktop_bt_pairing_toast_id;
static atomic_t desktop_bt_pending_result;
static char desktop_bt_pairing_text[24];

static struct zui_host *desktop_bluetooth_notify_host(void)
{
	struct zui_desktop *desktop = zui_desktop_get_instance();

	return desktop != NULL ? desktop->host : NULL;
}

static void desktop_bluetooth_notify_dismiss_pairing(struct zui_host *host)
{
	uint32_t id = (uint32_t)atomic_get(&desktop_bt_pairing_toast_id);

	if (id == 0U) {
		return;
	}

	atomic_set(&desktop_bt_pairing_toast_id, 0);
	if (host != NULL) {
		(void)zui_toast_dismiss(host, id);
	}
}

static void desktop_bluetooth_notify_show_result(struct zui_host *host,
						 enum desktop_bluetooth_notify_result result)
{
	const char *text;
	const struct zui_icon *icon;

	if (host == NULL) {
		return;
	}

	switch (result) {
	case DESKTOP_BLUETOOTH_NOTIFY_RESULT_CONNECTED:
		text = DESKTOP_TEXT_NOTIFY_BLUETOOTH_CONNECTED;
		icon = &I_done_24x24;
		break;
	case DESKTOP_BLUETOOTH_NOTIFY_RESULT_DISCONNECTED:
		text = DESKTOP_TEXT_NOTIFY_BLUETOOTH_DISCONNECTED;
		icon = &I_error_24x24;
		break;
	default:
		return;
	}

	(void)zui_toast_show(host, &(struct zui_toast_config){
		.title = DESKTOP_TEXT_BLUETOOTH_TITLE,
		.text = text,
		.icon = icon,
		.timeout_ms = DESKTOP_BLUETOOTH_RESULT_TIMEOUT_MS,
	});
}

static void desktop_bluetooth_notify_result_work_handler(struct k_work *work)
{
	struct zui_host *host = desktop_bluetooth_notify_host();
	enum desktop_bluetooth_notify_result result;

	ARG_UNUSED(work);

	result = (enum desktop_bluetooth_notify_result)atomic_get(&desktop_bt_pending_result);
	atomic_set(&desktop_bt_pending_result, DESKTOP_BLUETOOTH_NOTIFY_RESULT_NONE);
	desktop_bluetooth_notify_show_result(host, result);
}

K_WORK_DELAYABLE_DEFINE(desktop_bluetooth_notify_result_work,
			desktop_bluetooth_notify_result_work_handler);

static void desktop_bluetooth_notify_pairing_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_bluetooth_pairing_event *evt;
	struct zui_host *host;
	uint32_t current_id;
	uint32_t id;

	if (chan != &mbs_bluetooth_pairing_chan) {
		return;
	}

	evt = zbus_chan_const_msg(chan);
	if (evt == NULL) {
		return;
	}

	host = desktop_bluetooth_notify_host();
	if (host == NULL) {
		return;
	}

	atomic_set(&desktop_bt_pending_result, DESKTOP_BLUETOOTH_NOTIFY_RESULT_NONE);
	(void)k_work_cancel_delayable(&desktop_bluetooth_notify_result_work);
	(void)snprintk(desktop_bt_pairing_text, sizeof(desktop_bt_pairing_text),
		       DESKTOP_TEXT_NOTIFY_BLUETOOTH_PAIRING_CODE_FORMAT, evt->passkey);

	current_id = (uint32_t)atomic_get(&desktop_bt_pairing_toast_id);
	if (current_id != 0U && zui_toast_is_visible(host, current_id)) {
		(void)zui_host_request_redraw(host);
		return;
	}

	id = zui_toast_show(host, &(struct zui_toast_config){
		.title = DESKTOP_TEXT_NOTIFY_BLUETOOTH_PAIRING_HEADER,
		.text = desktop_bt_pairing_text,
		.icon = &I_loading_24x24,
		.timeout_ms = DESKTOP_BLUETOOTH_PAIRING_TIMEOUT_MS,
	});
	atomic_set(&desktop_bt_pairing_toast_id, (atomic_val_t)id);
}

static void desktop_bluetooth_notify_state_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_bluetooth_state_event *evt;
	struct zui_host *host;
	enum desktop_bluetooth_notify_result result;
	bool had_pairing_toast;

	if (chan != &mbs_bluetooth_state_chan) {
		return;
	}

	evt = zbus_chan_const_msg(chan);
	if (evt == NULL) {
		return;
	}

	switch (evt->state) {
	case MBS_BLUETOOTH_STATE_CONNECTED:
		result = DESKTOP_BLUETOOTH_NOTIFY_RESULT_CONNECTED;
		break;
	case MBS_BLUETOOTH_STATE_DISCONNECTED:
		result = DESKTOP_BLUETOOTH_NOTIFY_RESULT_DISCONNECTED;
		break;
	case MBS_BLUETOOTH_STATE_DISABLED:
		result = DESKTOP_BLUETOOTH_NOTIFY_RESULT_NONE;
		break;
	default:
		return;
	}

	host = desktop_bluetooth_notify_host();
	had_pairing_toast = atomic_get(&desktop_bt_pairing_toast_id) != 0;
	desktop_bluetooth_notify_dismiss_pairing(host);

	if (result == DESKTOP_BLUETOOTH_NOTIFY_RESULT_NONE) {
		atomic_set(&desktop_bt_pending_result, DESKTOP_BLUETOOTH_NOTIFY_RESULT_NONE);
		(void)k_work_cancel_delayable(&desktop_bluetooth_notify_result_work);
		return;
	}

	if (had_pairing_toast) {
		atomic_set(&desktop_bt_pending_result, result);
		(void)k_work_reschedule(&desktop_bluetooth_notify_result_work,
					 K_MSEC(DESKTOP_BLUETOOTH_RESULT_DELAY_MS));
	} else {
		desktop_bluetooth_notify_show_result(host, result);
	}
}

ZBUS_LISTENER_DEFINE(mbs_desktop_notify_pairing_listener,
		     desktop_bluetooth_notify_pairing_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_bluetooth_pairing_chan, mbs_desktop_notify_pairing_listener, 2);

ZBUS_LISTENER_DEFINE(mbs_desktop_notify_state_listener,
		     desktop_bluetooth_notify_state_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_bluetooth_state_chan, mbs_desktop_notify_state_listener, 2);
#endif
