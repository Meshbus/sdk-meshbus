/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "desktop_private.h"

#include "assets/assets_icons.h"

#if defined(CONFIG_ZBUS) && defined(CONFIG_MBS_MESSAGE)
#include <string.h>

#include <zephyr/kernel.h>
#include <message/message.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>
#include <zui/zui.h>

#define DESKTOP_MESSAGE_TOAST_TIMEOUT_MS       2500U
#define DESKTOP_MESSAGE_TOAST_RETRY_MS         100U
#define DESKTOP_MESSAGE_TOAST_PENDING_TTL_MS   5000U
#define DESKTOP_MESSAGE_TOAST_TITLE_SIZE       32U
#define DESKTOP_MESSAGE_TOAST_TEXT_SIZE        96U

struct desktop_message_toast_pending {
	char title[DESKTOP_MESSAGE_TOAST_TITLE_SIZE];
	char text[DESKTOP_MESSAGE_TOAST_TEXT_SIZE];
	int64_t expires_at_ms;
	uint32_t generation;
	bool pending;
};

static struct k_spinlock desktop_message_toast_lock;
static struct desktop_message_toast_pending desktop_message_toast_pending;

static size_t desktop_message_toast_utf8_trim(const char *text, size_t len)
{
	size_t start;
	uint8_t lead;
	size_t char_len;

	if (!text || len == 0U) {
		return 0U;
	}

	start = len - 1U;
	while (start > 0U && ((uint8_t)text[start] & 0xc0U) == 0x80U) {
		start--;
	}

	lead = (uint8_t)text[start];
	if ((lead & 0x80U) == 0U) {
		char_len = 1U;
	} else if ((lead & 0xe0U) == 0xc0U) {
		char_len = 2U;
	} else if ((lead & 0xf0U) == 0xe0U) {
		char_len = 3U;
	} else if ((lead & 0xf8U) == 0xf0U) {
		char_len = 4U;
	} else {
		return start;
	}

	return start + char_len <= len ? len : start;
}

static void message_toast_copy_title(
	char title[DESKTOP_MESSAGE_TOAST_TITLE_SIZE],
	const struct mbs_message_response_event *event)
{
	size_t len = strnlen(event->sender_name, sizeof(event->sender_name));

	len = MIN(len, (size_t)DESKTOP_MESSAGE_TOAST_TITLE_SIZE - 1U);
	len = desktop_message_toast_utf8_trim(event->sender_name, len);
	memcpy(title, event->sender_name, len);
	title[len] = '\0';
}

static void message_toast_copy_text(
	char text[DESKTOP_MESSAGE_TOAST_TEXT_SIZE],
	const struct mbs_message_response_event *event)
{
	size_t len = MIN((size_t)event->payload_len,
			 (size_t)DESKTOP_MESSAGE_TOAST_TEXT_SIZE - 1U);

	len = desktop_message_toast_utf8_trim((const char *)event->payload,
					      len);
	memcpy(text, event->payload, len);
	text[len] = '\0';
}

static bool
message_toast_is_received(const struct mbs_message_response_event *event)
{
	if (!event || event->payload_len == 0U) {
		return false;
	}

	return event->type == meshbus_MessageContent_MessageType_RECEIVE_NODE ||
	       event->type ==
		       meshbus_MessageContent_MessageType_RECEIVE_CHANNEL;
}

static struct zui_host *desktop_message_toast_host(void)
{
	struct zui_desktop *desktop = zui_desktop_get_instance();

	return desktop ? desktop->host : NULL;
}

static void desktop_message_toast_work_handler(struct k_work *work)
{
	struct desktop_message_toast_pending snapshot;
	struct zui_host *host;
	k_spinlock_key_t key;
	uint32_t toast_id;
	bool reschedule = false;

	ARG_UNUSED(work);

	key = k_spin_lock(&desktop_message_toast_lock);
	snapshot = desktop_message_toast_pending;
	k_spin_unlock(&desktop_message_toast_lock, key);

	if (!snapshot.pending) {
		return;
	}

	host = desktop_message_toast_host();
	toast_id = !host ? 0U :
		zui_toast_show(host, &(struct zui_toast_config){
			.title = snapshot.title,
			.text = snapshot.text,
			.icon = &A_message_14x14,
			.timeout_ms = DESKTOP_MESSAGE_TOAST_TIMEOUT_MS,
		});

	key = k_spin_lock(&desktop_message_toast_lock);
	if (desktop_message_toast_pending.pending &&
	    desktop_message_toast_pending.generation == snapshot.generation) {
		if (toast_id != 0U ||
		    k_uptime_get() >=
			    desktop_message_toast_pending.expires_at_ms) {
			desktop_message_toast_pending.pending = false;
		} else {
			reschedule = true;
		}
	} else if (desktop_message_toast_pending.pending) {
		reschedule = true;
	}
	k_spin_unlock(&desktop_message_toast_lock, key);

	if (reschedule) {
		(void)k_work_reschedule(k_work_delayable_from_work(work),
			K_MSEC(DESKTOP_MESSAGE_TOAST_RETRY_MS));
	}
}

K_WORK_DELAYABLE_DEFINE(desktop_message_toast_work,
			desktop_message_toast_work_handler);

static void desktop_message_toast_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_message_response_event *event;
	struct desktop_message_toast_pending pending = {0};
	k_spinlock_key_t key;

	if (chan != &mbs_message_response_chan) {
		return;
	}

	event = zbus_chan_const_msg(chan);
	if (!message_toast_is_received(event)) {
		return;
	}

	message_toast_copy_title(pending.title, event);
	message_toast_copy_text(pending.text, event);
	pending.expires_at_ms =
		k_uptime_get() + DESKTOP_MESSAGE_TOAST_PENDING_TTL_MS;
	pending.pending = true;

	key = k_spin_lock(&desktop_message_toast_lock);
	pending.generation = desktop_message_toast_pending.generation + 1U;
	if (pending.generation == 0U) {
		pending.generation = 1U;
	}
	desktop_message_toast_pending = pending;
	k_spin_unlock(&desktop_message_toast_lock, key);

	(void)k_work_reschedule(&desktop_message_toast_work, K_NO_WAIT);
}

ZBUS_LISTENER_DEFINE(mbs_desktop_notify_message_listener,
		     desktop_message_toast_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_message_response_chan,
		  mbs_desktop_notify_message_listener, 2);
#endif
