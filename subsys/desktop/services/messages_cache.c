/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <zephyr/kernel.h>
#include <message/message.h>
#include <clock/timestamp.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#include "messages_cache.h"

BUILD_ASSERT(CONFIG_MBS_DESKTOP_MESSAGE_CACHE_COUNT > 0U,
	     "CONFIG_MBS_DESKTOP_MESSAGE_CACHE_COUNT must be > 0");
BUILD_ASSERT(CONFIG_MBS_DESKTOP_MESSAGE_CACHE_COUNT <= CONFIG_MBS_MESSAGE_MAX_STORE_COUNT,
	     "Desktop message cache cannot exceed the Meshbus message store");

struct desktop_messages_cache {
	uint32_t count;
	uint32_t update_seq;
	uint64_t last_timestamp_ms;
	uint64_t last_entry_id;
	struct desktop_messages_cache_entry entries[CONFIG_MBS_DESKTOP_MESSAGE_CACHE_COUNT];
};

static struct desktop_messages_cache g_messages_cache;
static K_MUTEX_DEFINE(g_messages_cache_mutex);

static bool messages_cache_is_received(const mbs_message_content *message)
{
	if (message == NULL) {
		return false;
	}

	return message->type == meshbus_MessageContent_MessageType_RECEIVE_NODE ||
	       message->type == meshbus_MessageContent_MessageType_RECEIVE_CHANNEL;
}

static uint64_t messages_cache_timestamp_reserve_locked(bool *realtime_out)
{
	uint64_t timestamp_ms;
	bool realtime = mbs_clock_realtime_is_valid();

	if (mbs_clock_timestamp_ms_get(&timestamp_ms) != 0 || timestamp_ms == 0U) {
		timestamp_ms = (uint64_t)k_uptime_get();
		if (timestamp_ms == 0U) {
			timestamp_ms = 1U;
		}
		realtime = false;
	}

	if (timestamp_ms <= g_messages_cache.last_timestamp_ms) {
		timestamp_ms = g_messages_cache.last_timestamp_ms + 1U;
	}
	g_messages_cache.last_timestamp_ms = timestamp_ms;

	if (realtime_out != NULL) {
		*realtime_out = realtime;
	}

	return timestamp_ms;
}

static bool messages_cache_find_entry_locked(uint64_t entry_id, uint32_t *index_out)
{
	if (entry_id == 0U) {
		return false;
	}

	for (uint32_t i = 0U; i < g_messages_cache.count; i++) {
		if (g_messages_cache.entries[i].entry_id == entry_id) {
			*index_out = i;
			return true;
		}
	}

	return false;
}

static bool messages_cache_from_response_event(
	const struct mbs_message_response_event *event, mbs_message_content *message,
	bool *timestamp_realtime_out)
{
	if (event == NULL || message == NULL) {
		return false;
	}
	if (event->type != meshbus_MessageContent_MessageType_RECEIVE_NODE &&
	    event->type != meshbus_MessageContent_MessageType_RECEIVE_CHANNEL) {
		return false;
	}
	if (event->route != meshbus_MessageContent_MessageRoute_ROUTE_UNSPECIFIED &&
	    event->route != meshbus_MessageContent_MessageRoute_ROUTE_FLOOD &&
	    event->route != meshbus_MessageContent_MessageRoute_ROUTE_DIRECT) {
		return false;
	}
	if (event->payload_len == 0U || event->payload_len > CONFIG_MBS_MESSAGE_TX_MAX_LEN) {
		return false;
	}
	if (event->sender_name[0] == '\0') {
		return false;
	}
	if (memchr(event->payload, 0, event->payload_len) != NULL) {
		return false;
	}

	*message = (mbs_message_content)meshbus_MessageContent_init_zero;
	message->type = event->type;
	message->route = event->route;
	message->target.size = MBS_MESSAGE_TARGET_PREFIX_BYTES;
	memcpy(message->target.bytes, event->target, sizeof(event->target));
	message->payload.size = event->payload_len;
	memcpy(message->payload.bytes, event->payload, event->payload_len);
	(void)snprintk(message->sender_name, sizeof(message->sender_name), "%s",
		       event->sender_name);
	message->timestamp = messages_cache_timestamp_reserve_locked(timestamp_realtime_out);
	message->sender_timestamp = event->sender_timestamp;
	message->has_rx_snr = event->has_rx_snr;
	if (event->has_rx_snr) {
		message->rx_snr = event->rx_snr;
	}

	return true;
}

static void messages_cache_append_response_locked(
	const struct mbs_message_response_event *event)
{
	mbs_message_content message = meshbus_MessageContent_init_zero;
	bool timestamp_realtime = false;

	if (!messages_cache_from_response_event(event, &message, &timestamp_realtime)) {
		return;
	}
	/* Never reuse an ID while a UI snapshot may still refer to it. */
	if (g_messages_cache.last_entry_id == UINT64_MAX) {
		return;
	}

	if (g_messages_cache.count == ARRAY_SIZE(g_messages_cache.entries)) {
		memmove(&g_messages_cache.entries[0], &g_messages_cache.entries[1],
			sizeof(g_messages_cache.entries[0]) *
				(ARRAY_SIZE(g_messages_cache.entries) - 1U));
		g_messages_cache.count--;
	}

	g_messages_cache.entries[g_messages_cache.count].entry_id = ++g_messages_cache.last_entry_id;
	g_messages_cache.entries[g_messages_cache.count].message = message;
	g_messages_cache.entries[g_messages_cache.count].unread = true;
	g_messages_cache.entries[g_messages_cache.count].timestamp_realtime = timestamp_realtime;
	g_messages_cache.count++;
	g_messages_cache.update_seq++;
}

static bool messages_cache_get_by_filter_locked(
	uint32_t newest_position, bool unread_only,
	const struct desktop_messages_cache_entry **entry_out, uint32_t *position_out)
{
	uint32_t match_pos = 0U;

	for (uint32_t i = g_messages_cache.count; i > 0U; i--) {
		const struct desktop_messages_cache_entry *entry = &g_messages_cache.entries[i - 1U];

		if (!messages_cache_is_received(&entry->message)) {
			continue;
		}
		if (unread_only && !entry->unread) {
			continue;
		}
		if (match_pos == newest_position) {
			if (entry_out != NULL) {
				*entry_out = entry;
			}
			if (position_out != NULL) {
				*position_out = match_pos;
			}
			return true;
		}
		match_pos++;
	}

	return false;
}

uint32_t desktop_messages_cache_received_count(void)
{
	uint32_t count = 0U;

	k_mutex_lock(&g_messages_cache_mutex, K_FOREVER);
	for (uint32_t i = 0U; i < g_messages_cache.count; i++) {
		count += messages_cache_is_received(&g_messages_cache.entries[i].message) ? 1U : 0U;
	}
	k_mutex_unlock(&g_messages_cache_mutex);

	return count;
}

uint32_t desktop_messages_cache_unread_received_count(void)
{
	uint32_t count = 0U;

	k_mutex_lock(&g_messages_cache_mutex, K_FOREVER);
	for (uint32_t i = 0U; i < g_messages_cache.count; i++) {
		if (!g_messages_cache.entries[i].unread) {
			continue;
		}
		count += messages_cache_is_received(&g_messages_cache.entries[i].message) ? 1U : 0U;
	}
	k_mutex_unlock(&g_messages_cache_mutex);

	return count;
}

bool desktop_messages_cache_copy_received(uint32_t newest_position,
					  struct desktop_messages_cache_entry *entry_out)
{
	const struct desktop_messages_cache_entry *entry = NULL;
	bool found;

	if (entry_out == NULL) {
		return false;
	}

	k_mutex_lock(&g_messages_cache_mutex, K_FOREVER);
	found = messages_cache_get_by_filter_locked(newest_position, false, &entry, NULL);
	if (found && entry != NULL) {
		*entry_out = *entry;
	} else {
		memset(entry_out, 0, sizeof(*entry_out));
	}
	k_mutex_unlock(&g_messages_cache_mutex);

	return found;
}

bool desktop_messages_cache_copy_unread_received(
	uint32_t newest_position, struct desktop_messages_cache_entry *entry_out)
{
	const struct desktop_messages_cache_entry *entry = NULL;
	bool found;

	if (entry_out == NULL) {
		return false;
	}

	k_mutex_lock(&g_messages_cache_mutex, K_FOREVER);
	found = messages_cache_get_by_filter_locked(newest_position, true, &entry, NULL);
	if (found && entry != NULL) {
		*entry_out = *entry;
	} else {
		memset(entry_out, 0, sizeof(*entry_out));
	}
	k_mutex_unlock(&g_messages_cache_mutex);

	return found;
}

uint32_t desktop_messages_cache_update_seq(void)
{
	uint32_t update_seq;

	k_mutex_lock(&g_messages_cache_mutex, K_FOREVER);
	update_seq = g_messages_cache.update_seq;
	k_mutex_unlock(&g_messages_cache_mutex);

	return update_seq;
}

void desktop_messages_cache_mark_read(uint64_t entry_id)
{
	uint32_t index = 0U;

	k_mutex_lock(&g_messages_cache_mutex, K_FOREVER);
	if (messages_cache_find_entry_locked(entry_id, &index)) {
		if (g_messages_cache.entries[index].unread) {
			g_messages_cache.entries[index].unread = false;
			g_messages_cache.update_seq++;
		}
	}
	k_mutex_unlock(&g_messages_cache_mutex);
}

void desktop_messages_cache_mark_unread(uint64_t entry_id)
{
	uint32_t index = 0U;

	k_mutex_lock(&g_messages_cache_mutex, K_FOREVER);
	if (messages_cache_find_entry_locked(entry_id, &index)) {
		if (!g_messages_cache.entries[index].unread) {
			g_messages_cache.entries[index].unread = true;
			g_messages_cache.update_seq++;
		}
	}
	k_mutex_unlock(&g_messages_cache_mutex);
}

static void messages_response_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_message_response_event *msg;

	if (chan != &mbs_message_response_chan) {
		return;
	}

	msg = zbus_chan_const_msg(chan);
	if (msg == NULL) {
		return;
	}

	k_mutex_lock(&g_messages_cache_mutex, K_FOREVER);
	messages_cache_append_response_locked(msg);
	k_mutex_unlock(&g_messages_cache_mutex);
}

ZBUS_LISTENER_DEFINE(desktop_messages_cache_response_listener, messages_response_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_message_response_chan, desktop_messages_cache_response_listener, 2);
