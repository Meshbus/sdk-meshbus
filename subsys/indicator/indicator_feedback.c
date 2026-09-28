/* SPDX-FileCopyrightText: 2026 FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <indicator/indicator.h>
#include "indicator_feedback.h"
#include "indicator_light.h"
#include "indicator_audio.h"

struct mbs_indicator_pattern {
	uint16_t on_ms;
	uint16_t off_ms;
	uint8_t count;
	uint8_t priority;
	bool message;
};
static const struct mbs_indicator_pattern patterns[] = {
	[MBS_INDICATOR_FEEDBACK_ACCEPTED] = {80, 0, 1, 3, true},
	[MBS_INDICATOR_FEEDBACK_RECEIVED] = {80, 120, 2, 3, true},
	[MBS_INDICATOR_FEEDBACK_RECEIVED_CHANNEL] = {80, 120, 2, 3, true},
	[MBS_INDICATOR_FEEDBACK_STARTUP] = {400, 0, 1, 3, false},
	[MBS_INDICATOR_FEEDBACK_ACK] = {400, 0, 1, 3, true},
	[MBS_INDICATOR_FEEDBACK_UNCONFIRMED] = {400, 200, 2, 4, true},
	[MBS_INDICATOR_FEEDBACK_MESSAGE_FAILED] = {80, 120, 3, 4, true},
	[MBS_INDICATOR_FEEDBACK_SYSTEM_SUCCESS] = {400, 0, 1, 3, false},
	[MBS_INDICATOR_FEEDBACK_SYSTEM_FAILED] = {80, 120, 3, 4, false},
	[MBS_INDICATOR_FEEDBACK_LOW_BATTERY] = {1000, 0, 1, 2, false},
	[MBS_INDICATOR_FEEDBACK_FAULT] = {200, 200, 3, 5, false},
};
BUILD_ASSERT(ARRAY_SIZE(patterns) == MBS_INDICATOR_FEEDBACK_COUNT);
static struct k_spinlock feedback_lock;
static int64_t pending[MBS_INDICATOR_FEEDBACK_COUNT];
static int64_t last_event[MBS_INDICATOR_FEEDBACK_COUNT];
static int active = -1;
static int64_t active_until;
static bool enabled;
static bool messages_enabled;
static bool system_enabled;
static bool startup_fault;
static bool radio_fault;
static bool low_battery;
static int64_t next_fault;
static int64_t next_battery;
static void feedback_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(feedback_work, feedback_work_handler);

static bool feedback_allowed(unsigned int event)
{
	return enabled && (patterns[event].message ? messages_enabled : system_enabled);
}

static void feedback_enqueue(unsigned int event, int64_t now)
{
	/* Store time + 1 so uptime zero remains a valid event timestamp. */
	if (!feedback_allowed(event) ||
	    (last_event[event] && now + 1 - last_event[event] < 1000)) {
		return;
	}
	last_event[event] = now + 1;
	if (!pending[event]) {
		pending[event] = now + 1;
	}
}

static int feedback_submit(enum mbs_indicator_feedback event, bool audio)
{
	if ((unsigned int)event >= MBS_INDICATOR_FEEDBACK_LOW_BATTERY) {
		return -EINVAL;
	}
	k_spinlock_key_t key = k_spin_lock(&feedback_lock);
	feedback_enqueue(event, k_uptime_get());
	k_spin_unlock(&feedback_lock, key);
	if (audio) {
		mbs_indicator_audio_submit(event);
	}
	(void)k_work_reschedule(&feedback_work, K_NO_WAIT);
	return 0;
}

int mbs_indicator_feedback_submit(enum mbs_indicator_feedback event)
{
	return feedback_submit(event, true);
}

void mbs_indicator_feedback_configure(bool on, bool messages, bool system)
{
	k_spinlock_key_t key = k_spin_lock(&feedback_lock);
	enabled = on;
	messages_enabled = messages;
	system_enabled = system;
	for (unsigned int i = 0; i < ARRAY_SIZE(patterns); i++) {
		if (!feedback_allowed(i)) {
			pending[i] = 0;
			last_event[i] = 0;
		}
	}
	k_spin_unlock(&feedback_lock, key);
	(void)k_work_reschedule(&feedback_work, K_NO_WAIT);
}

void mbs_indicator_feedback_cancel(void)
{
	k_spinlock_key_t key = k_spin_lock(&feedback_lock);
	memset(pending, 0, sizeof(pending));
	active = -1;
	k_spin_unlock(&feedback_lock, key);
}

void mbs_indicator_startup_complete(bool ready)
{
	k_spinlock_key_t key = k_spin_lock(&feedback_lock);
	startup_fault = !ready;
	k_spin_unlock(&feedback_lock, key);
	if (ready) {
		(void)mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_STARTUP);
	} else {
		(void)k_work_reschedule(&feedback_work, K_NO_WAIT);
	}
}

static void feedback_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	int selected = -1;
	bool stop = false;
	int64_t now = k_uptime_get();
	int64_t wake = INT64_MAX;
	k_spinlock_key_t key = k_spin_lock(&feedback_lock);

	if (enabled && system_enabled) {
		if (startup_fault || radio_fault) {
			if (now >= next_fault) {
				feedback_enqueue(MBS_INDICATOR_FEEDBACK_FAULT, now);
				next_fault = now + 10000;
			}
			wake = MIN(wake, next_fault);
		} else {
			pending[MBS_INDICATOR_FEEDBACK_FAULT] = 0;
			next_fault = 0;
		}
		if (low_battery) {
			if (now >= next_battery) {
				feedback_enqueue(MBS_INDICATOR_FEEDBACK_LOW_BATTERY, now);
				next_battery = now + 60000;
			}
			wake = MIN(wake, next_battery);
		} else {
			pending[MBS_INDICATOR_FEEDBACK_LOW_BATTERY] = 0;
			next_battery = 0;
		}
	}
	if (active >= 0 && (!feedback_allowed(active) || now >= active_until ||
	    (active == MBS_INDICATOR_FEEDBACK_LOW_BATTERY && !low_battery) ||
	    (active == MBS_INDICATOR_FEEDBACK_FAULT && !startup_fault && !radio_fault))) {
		active = -1;
		stop = true;
	}
	for (unsigned int i = 0; i < ARRAY_SIZE(patterns); i++) {
		if (pending[i] && (now + 1 - pending[i] >= 3000 || !feedback_allowed(i))) {
			pending[i] = 0;
		}
		if (!pending[i]) {
			continue;
		}
		if (selected < 0 || patterns[i].priority > patterns[selected].priority ||
		    (patterns[i].priority == patterns[selected].priority &&
		     pending[i] < pending[selected])) {
			selected = i;
		}
	}
	if (selected >= 0 && (active < 0 ||
	    patterns[selected].priority > patterns[active].priority)) {
		active = selected;
		pending[selected] = 0;
		active_until = now + (patterns[selected].on_ms + patterns[selected].off_ms) *
			patterns[selected].count;
	} else {
		selected = -1;
	}
	if (active >= 0) {
		wake = MIN(wake, active_until);
	}
	bool audio_low = low_battery;
	bool audio_fault = startup_fault || radio_fault;
	k_spin_unlock(&feedback_lock, key);
	mbs_indicator_audio_conditions(audio_low, audio_fault);

	/* Only this worker drives semantic playback. No state lock spans driver I/O. */
	if (selected >= 0) {
		const struct mbs_indicator_pattern *p = &patterns[selected];
		(void)indicator_light_play(p->on_ms, p->off_ms, p->count);
	} else if (stop) {
		indicator_light_stop();
	}
	if (wake != INT64_MAX) {
		/* Keep any earlier wake scheduled by an arriving event. */
		(void)k_work_schedule(&feedback_work, K_MSEC(MAX(1, wake - k_uptime_get())));
	}
}

#if defined(CONFIG_MBS_POWER)
#include <power/power.h>
static bool battery_latched;
static void feedback_power_cb(const struct zbus_channel *chan)
{
	const struct mbs_power_fuel_gauge_data_event *event = zbus_chan_const_msg(chan);
	if (event->soc_percent > 100 || !event->voltage_mv) {
		return;
	}
	k_spinlock_key_t key = k_spin_lock(&feedback_lock);
	if (event->soc_percent <= 15) {
		battery_latched = true;
	} else if (event->soc_percent >= 20) {
		battery_latched = false;
	}
	low_battery = battery_latched && !event->charging;
	k_spin_unlock(&feedback_lock, key);
	(void)k_work_reschedule(&feedback_work, K_NO_WAIT);
}
ZBUS_LISTENER_DEFINE(mbs_indicator_power_listener, feedback_power_cb);
ZBUS_CHAN_ADD_OBS(mbs_power_fuel_gauge_data_chan, mbs_indicator_power_listener, 2);
#endif

#if defined(CONFIG_MBS_MESSAGE)
#include <message/message.h>
static void feedback_message_cb(const struct zbus_channel *chan)
{
	if (chan == &mbs_message_response_chan) {
		const struct mbs_message_response_event *event = zbus_chan_const_msg(chan);
		(void)feedback_submit(
			event->type == meshbus_MessageContent_MessageType_RECEIVE_CHANNEL ?
			MBS_INDICATOR_FEEDBACK_RECEIVED_CHANNEL : MBS_INDICATOR_FEEDBACK_RECEIVED,
			IS_ENABLED(CONFIG_MBS_INDICATOR_MESSAGE_FEEDBACK));
		return;
	}
	const struct mbs_message_send_result_event *event = zbus_chan_const_msg(chan);
	switch (event->result) {
	case MBS_MESSAGE_SEND_ACCEPTED:
		(void)mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_ACCEPTED);
		break;
	case MBS_MESSAGE_SEND_CONFIRMED:
		(void)mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_ACK);
		break;
	case MBS_MESSAGE_SEND_UNCONFIRMED:
		(void)mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_UNCONFIRMED);
		break;
	case MBS_MESSAGE_SEND_FAILED:
		(void)mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_MESSAGE_FAILED);
		break;
	}
}
ZBUS_LISTENER_DEFINE(mbs_indicator_message_result_listener, feedback_message_cb);
ZBUS_CHAN_ADD_OBS(mbs_message_send_result_chan, mbs_indicator_message_result_listener, 2);
ZBUS_CHAN_ADD_OBS(mbs_message_response_chan, mbs_indicator_message_result_listener, 3);
#endif

#if defined(CONFIG_MBS_BLUETOOTH)
#include <bluetooth/bluetooth.h>
static void feedback_pairing_cb(const struct zbus_channel *chan)
{
	const struct mbs_bluetooth_pairing_result_event *event = zbus_chan_const_msg(chan);
	if (event->result == MBS_BLUETOOTH_PAIRING_SUCCESS) {
		(void)mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_SYSTEM_SUCCESS);
	} else if (event->result == MBS_BLUETOOTH_PAIRING_FAILED) {
		(void)mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_SYSTEM_FAILED);
	}
}
ZBUS_LISTENER_DEFINE(mbs_indicator_pairing_listener, feedback_pairing_cb);
ZBUS_CHAN_ADD_OBS(mbs_bluetooth_pairing_result_chan, mbs_indicator_pairing_listener, 2);
#endif

#if defined(CONFIG_MBS_RADIO)
#include <radio/radio.h>
static bool radio_enabled;
static bool radio_unhealthy;
static void feedback_radio_cb(const struct zbus_channel *chan)
{
	k_spinlock_key_t key = k_spin_lock(&feedback_lock);
	if (chan == &mbs_radio_state_chan) {
		const struct mbs_radio_state_event *event = zbus_chan_const_msg(chan);
		radio_enabled = event->enabled;
	} else {
		const struct mbs_radio_health_event *event = zbus_chan_const_msg(chan);
		radio_unhealthy = event->rx_failed || event->tx_failed;
	}
	radio_fault = radio_enabled && radio_unhealthy;
	k_spin_unlock(&feedback_lock, key);
	(void)k_work_reschedule(&feedback_work, K_NO_WAIT);
}
ZBUS_LISTENER_DEFINE(mbs_indicator_radio_listener, feedback_radio_cb);
ZBUS_CHAN_ADD_OBS(mbs_radio_state_chan, mbs_indicator_radio_listener, 2);
ZBUS_CHAN_ADD_OBS(mbs_radio_health_chan, mbs_indicator_radio_listener, 2);
#endif
