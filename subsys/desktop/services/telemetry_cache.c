/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "telemetry_cache.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

struct desktop_telemetry_cache {
	uint32_t update_seq;
	size_t count;
	bool latest_valid;
	enum sensor_channel latest_chan;
	struct desktop_telemetry_cache_entry entries[DESKTOP_TELEMETRY_CACHE_COUNT];
};

static struct desktop_telemetry_cache g_telemetry_cache;
static K_MUTEX_DEFINE(g_telemetry_cache_mutex);

static int telemetry_cache_find_locked(enum sensor_channel chan)
{
	for (size_t i = 0U; i < g_telemetry_cache.count; i++) {
		if (g_telemetry_cache.entries[i].valid &&
		    g_telemetry_cache.entries[i].chan == chan) {
			return (int)i;
		}
	}

	return -ENOENT;
}

static int telemetry_cache_slot_locked(enum sensor_channel chan)
{
	int index = telemetry_cache_find_locked(chan);

	if (index >= 0) {
		return index;
	}
	if (g_telemetry_cache.count < ARRAY_SIZE(g_telemetry_cache.entries)) {
		index = (int)g_telemetry_cache.count;
		g_telemetry_cache.count++;
		return index;
	}

	return -ENOMEM;
}

uint32_t desktop_telemetry_cache_update_seq(void)
{
	uint32_t update_seq;

	k_mutex_lock(&g_telemetry_cache_mutex, K_FOREVER);
	update_seq = g_telemetry_cache.update_seq;
	k_mutex_unlock(&g_telemetry_cache_mutex);

	return update_seq;
}

bool desktop_telemetry_cache_latest(struct desktop_telemetry_cache_entry *out)
{
	bool found = false;
	int index;

	if (out == NULL) {
		return false;
	}

	k_mutex_lock(&g_telemetry_cache_mutex, K_FOREVER);
	if (g_telemetry_cache.latest_valid) {
		index = telemetry_cache_find_locked(g_telemetry_cache.latest_chan);
		if (index >= 0) {
			*out = g_telemetry_cache.entries[index];
			found = true;
		}
	}
	if (!found) {
		memset(out, 0, sizeof(*out));
	}
	k_mutex_unlock(&g_telemetry_cache_mutex);

	return found;
}

bool desktop_telemetry_cache_get(enum sensor_channel chan,
				 struct desktop_telemetry_cache_entry *out)
{
	bool found = false;
	int index;

	if (out == NULL) {
		return false;
	}

	k_mutex_lock(&g_telemetry_cache_mutex, K_FOREVER);
	index = telemetry_cache_find_locked(chan);
	if (index >= 0) {
		*out = g_telemetry_cache.entries[index];
		found = true;
	} else {
		memset(out, 0, sizeof(*out));
	}
	k_mutex_unlock(&g_telemetry_cache_mutex);

	return found;
}

static void telemetry_cache_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_telemetry_data_event *event;
	struct desktop_telemetry_cache_entry *entry;
	size_t value_count;
	int index;

	if (chan != &mbs_telemetry_data_chan) {
		return;
	}

	event = zbus_chan_const_msg(chan);
	if (event == NULL) {
		return;
	}

	value_count = MIN((size_t)event->value_count, (size_t)MBS_TELEMETRY_MAX_VALUES);

	k_mutex_lock(&g_telemetry_cache_mutex, K_FOREVER);
	index = telemetry_cache_slot_locked(event->chan);
	if (index >= 0) {
		entry = &g_telemetry_cache.entries[index];
		memset(entry, 0, sizeof(*entry));
		entry->valid = true;
		entry->timestamp = event->timestamp;
		entry->chan = event->chan;
		entry->value_count = (uint8_t)value_count;
		if (value_count > 0U) {
			memcpy(entry->values, event->values,
			       sizeof(struct sensor_value) * value_count);
		}
		g_telemetry_cache.latest_valid = true;
		g_telemetry_cache.latest_chan = event->chan;
		g_telemetry_cache.update_seq++;
	}
	k_mutex_unlock(&g_telemetry_cache_mutex);
}

ZBUS_LISTENER_DEFINE(desktop_telemetry_cache_listener, telemetry_cache_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_telemetry_data_chan, desktop_telemetry_cache_listener, 2);
