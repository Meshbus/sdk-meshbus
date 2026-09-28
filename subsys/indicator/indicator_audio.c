/* SPDX-FileCopyrightText: 2026 FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <string.h>
#include "indicator_audio.h"
#include "indicator_buzzer.h"
#include "indicator_buzzer_tone.h"

#if defined(CONFIG_MBS_INDICATOR_BUZZER)
/* Immutable notes stay alive until the PWM player releases its owned token. */
INDICATOR_BUZZER_TONE_DEFINE(audio_direct, {2400, 80}, {0, 100}, {3000, 80});
INDICATOR_BUZZER_TONE_DEFINE(audio_channel, {2400, 80});
INDICATOR_BUZZER_TONE_DEFINE(audio_success, {2000, 80}, {0, 60}, {3000, 100});
INDICATOR_BUZZER_TONE_DEFINE(audio_pair_failed, {3000, 100}, {0, 60}, {1600, 120});
INDICATOR_BUZZER_TONE_DEFINE(audio_failed,
	{2200, 80}, {0, 100}, {2200, 80}, {0, 100}, {2200, 80});
INDICATOR_BUZZER_TONE_DEFINE(audio_unconfirmed, {1400, 220}, {0, 160}, {1400, 220});
INDICATOR_BUZZER_TONE_DEFINE(audio_battery, {1800, 150}, {0, 120}, {1200, 200});
INDICATOR_BUZZER_TONE_DEFINE(audio_fault,
	{1800, 150}, {0, 100}, {1800, 150}, {0, 100}, {1200, 250});

struct audio_pattern {
	const struct indicator_buzzer_melody *melody;
	enum indicator_buzzer_source source;
	uint8_t priority;
};
static const struct audio_pattern audio_patterns[MBS_INDICATOR_FEEDBACK_COUNT] = {
	[MBS_INDICATOR_FEEDBACK_RECEIVED] = {&audio_direct, INDICATOR_SOURCE_DIRECT_MSG, 3},
	[MBS_INDICATOR_FEEDBACK_RECEIVED_CHANNEL] = {&audio_channel, INDICATOR_SOURCE_CHANNEL_MSG, 3},
	[MBS_INDICATOR_FEEDBACK_ACK] = {&audio_success, INDICATOR_SOURCE_SYSTEM, 3},
	[MBS_INDICATOR_FEEDBACK_MESSAGE_FAILED] = {&audio_failed, INDICATOR_SOURCE_SYSTEM, 4},
	[MBS_INDICATOR_FEEDBACK_UNCONFIRMED] = {&audio_unconfirmed, INDICATOR_SOURCE_SYSTEM, 4},
	[MBS_INDICATOR_FEEDBACK_SYSTEM_SUCCESS] = {&audio_success, INDICATOR_SOURCE_SYSTEM, 3},
	[MBS_INDICATOR_FEEDBACK_SYSTEM_FAILED] = {&audio_pair_failed, INDICATOR_SOURCE_SYSTEM, 4},
	[MBS_INDICATOR_FEEDBACK_STARTUP] = {&indicator_buzzer_startup_tone, INDICATOR_SOURCE_SYSTEM, 3},
	[MBS_INDICATOR_FEEDBACK_LOW_BATTERY] = {&audio_battery, INDICATOR_SOURCE_SYSTEM, 2},
	[MBS_INDICATOR_FEEDBACK_FAULT] = {&audio_fault, INDICATOR_SOURCE_SYSTEM, 5},
};
static struct k_spinlock audio_lock;
static bool allowed[MBS_INDICATOR_FEEDBACK_COUNT];
static int64_t pending[MBS_INDICATOR_FEEDBACK_COUNT];
static int64_t last[MBS_INDICATOR_FEEDBACK_COUNT];
static bool low_condition;
static bool fault_condition;
/* Preserve cooldown through flapping conditions and preference changes. */
static int64_t next_battery;
static int64_t next_fault;
static int active = -1;
static int64_t completion_check_at;
static uint32_t playback_token; /* Only audio_work owns the driver token. */
static void audio_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(audio_work, audio_work_handler);

static void enqueue(unsigned int event, int64_t now)
{
	if (!allowed[event] || (last[event] && now + 1 - last[event] < 1000)) {
		return;
	}
	last[event] = now + 1;
	if (!pending[event]) {
		pending[event] = now + 1;
	}
}

void mbs_indicator_audio_configure(const mbs_indicator_config *cfg)
{
	k_spinlock_key_t key = k_spin_lock(&audio_lock);
	for (unsigned int i = 0; i < ARRAY_SIZE(allowed); i++) {
		bool on = cfg && cfg->buzzer_enabled && cfg->has_buzzer_feedback &&
			audio_patterns[i].melody;
		if (on) {
			switch (audio_patterns[i].source) {
			case INDICATOR_SOURCE_DIRECT_MSG:
				on = cfg->buzzer_feedback.direct_message_enabled;
				break;
			case INDICATOR_SOURCE_CHANNEL_MSG:
				on = cfg->buzzer_feedback.channel_message_enabled;
				break;
			default:
				on = cfg->buzzer_feedback.system_enabled;
				break;
			}
		}
		allowed[i] = on;
		if (!on) {
			pending[i] = 0;
			last[i] = 0;
		}
	}
	k_spin_unlock(&audio_lock, key);
	(void)k_work_reschedule(&audio_work, K_NO_WAIT);
}

void mbs_indicator_audio_submit(enum mbs_indicator_feedback event)
{
	if ((unsigned int)event >= MBS_INDICATOR_FEEDBACK_LOW_BATTERY) {
		return;
	}
	k_spinlock_key_t key = k_spin_lock(&audio_lock);
	enqueue(event, k_uptime_get());
	k_spin_unlock(&audio_lock, key);
	(void)k_work_reschedule(&audio_work, K_NO_WAIT);
}

void mbs_indicator_audio_conditions(bool low, bool fault)
{
	k_spinlock_key_t key = k_spin_lock(&audio_lock);
	bool changed = low != low_condition || fault != fault_condition;
	low_condition = low;
	fault_condition = fault;
	k_spin_unlock(&audio_lock, key);
	if (changed) {
		(void)k_work_reschedule(&audio_work, K_NO_WAIT);
	}
}

void mbs_indicator_audio_cancel(void)
{
	k_spinlock_key_t key = k_spin_lock(&audio_lock);
	memset(pending, 0, sizeof(pending));
	active = -1;
	k_spin_unlock(&audio_lock, key);
}

bool mbs_indicator_audio_busy(void)
{
	k_spinlock_key_t key = k_spin_lock(&audio_lock);
	bool busy = active >= 0;
	for (unsigned int i = 0; i < ARRAY_SIZE(pending); i++) {
		busy |= pending[i] != 0;
	}
	k_spin_unlock(&audio_lock, key);
	return busy;
}

static void audio_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	int selected = -1;
	bool stop = false;
	int64_t now = k_uptime_get();
	int64_t wake = INT64_MAX;
	/* Query the owned player without holding the scheduler spinlock. */
	bool playing = indicator_buzzer_playing(playback_token);
	k_spinlock_key_t key = k_spin_lock(&audio_lock);
	const unsigned int reminders[] = {MBS_INDICATOR_FEEDBACK_LOW_BATTERY,
		MBS_INDICATOR_FEEDBACK_FAULT};
	for (unsigned int i = 0; i < ARRAY_SIZE(reminders); i++) {
		unsigned int event = reminders[i];
		bool condition = i ? fault_condition : low_condition;
		int64_t *next = i ? &next_fault : &next_battery;
		if (condition && allowed[event]) {
			if (now >= *next) {
				enqueue(event, now);
				*next = now + 600000;
			}
			wake = MIN(wake, *next);
		} else {
			pending[event] = 0;
		}
	}
	if (active >= 0 && (!allowed[active] ||
	    (active == MBS_INDICATOR_FEEDBACK_LOW_BATTERY && !low_condition) ||
	    (active == MBS_INDICATOR_FEEDBACK_FAULT && !fault_condition))) {
		active = -1;
		stop = true;
	}
	/* Nominal duration is only a wake estimate: note transitions may be late. */
	if (active >= 0 && !playing) {
		active = -1;
		playback_token = 0;
	}
	for (unsigned int i = 0; i < ARRAY_SIZE(pending); i++) {
		if (pending[i] && (!allowed[i] || now + 1 - pending[i] >= 3000)) {
			pending[i] = 0;
		}
		if (pending[i] && (selected < 0 ||
		    audio_patterns[i].priority > audio_patterns[selected].priority ||
		    (audio_patterns[i].priority == audio_patterns[selected].priority &&
		     pending[i] < pending[selected]))) {
			selected = i;
		}
	}
	if (selected >= 0 && (active < 0 ||
	    audio_patterns[selected].priority > audio_patterns[active].priority)) {
		active = selected;
		pending[selected] = 0;
		completion_check_at = now;
		const struct indicator_buzzer_melody *melody = audio_patterns[selected].melody;
		for (size_t i = 0; i < melody->length; i++) {
			completion_check_at += melody->notes[i].duration_ms;
		}
	} else {
		selected = -1;
	}
	if (active >= 0) {
		/* A delayed player keeps ownership until its last note completes. */
		wake = MIN(wake, MAX(completion_check_at, now + 20));
	}
	k_spin_unlock(&audio_lock, key);

	if (selected >= 0) {
		/* Public routing rechecks preferences before touching PWM. */
		(void)mbs_indicator_buzzer_play_owned(audio_patterns[selected].source,
			audio_patterns[selected].melody, &playback_token);
	} else if (stop) {
		indicator_buzzer_stop_owned(playback_token);
		playback_token = 0;
	}
	if (wake != INT64_MAX) {
		(void)k_work_schedule(&audio_work, K_MSEC(MAX(1, wake - k_uptime_get())));
	}
}
#else
void mbs_indicator_audio_configure(const mbs_indicator_config *cfg) { ARG_UNUSED(cfg); }
void mbs_indicator_audio_submit(enum mbs_indicator_feedback event) { ARG_UNUSED(event); }
void mbs_indicator_audio_conditions(bool low, bool fault) { ARG_UNUSED(low); ARG_UNUSED(fault); }
bool mbs_indicator_audio_busy(void) { return false; }
void mbs_indicator_audio_cancel(void) {}
#endif
