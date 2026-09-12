/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/pwm.h>

#include "indicator_buzzer.h"

#if IS_ENABLED(CONFIG_MBS_INDICATOR_BUZZER) && IS_ENABLED(MBS_INDICATOR_BUZZER_DEVICE_EXIST)

LOG_MODULE_REGISTER(mbs_indicator_buzzer, CONFIG_MBS_INDICATOR_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* Devices                                                                    */
/* -------------------------------------------------------------------------- */
#define BUZZER_PWM_CHANNEL 0
#define BUZZER_NODE        DT_CHOSEN(meshbus_indicator_buzzer)
static const struct device *const buzzer_pwm = DEVICE_DT_GET(BUZZER_NODE);

/* -------------------------------------------------------------------------- */
/* Defaults And State                                                         */
/* -------------------------------------------------------------------------- */
struct buzzer_play_state {
	struct indicator_buzzer_melody melody; /**< Current melody */
	uint8_t current_note;                  /**< Current note index */
	bool active;                           /**< Playback is active */
};
static struct {
	struct buzzer_play_state play;
	bool ready;
} buzzer_state;
static K_MUTEX_DEFINE(buzzer_mutex);
static K_SEM_DEFINE(buzzer_done_sem, 0, 1);
static struct k_work_delayable buzzer_work;
/** Static buffer for RTTTL notes */
static struct indicator_buzzer_note rtttl_notes[CONFIG_MBS_INDICATOR_BUZZER_RTTTL_MAX_NOTES];
static struct indicator_buzzer_melody rtttl_melody;
/**
 * Note frequencies lookup table (octave 4 as base)
 * Index: 0=C, 1=C#, 2=D, 3=D#, 4=E, 5=F, 6=F#, 7=G, 8=G#, 9=A, 10=A#, 11=B
 */
static const uint16_t note_freq_o4[] = {262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494};

/* -------------------------------------------------------------------------- */
/* Validation And Helpers                                                     */
/* -------------------------------------------------------------------------- */
static int rtttl_get_note_index(char note)
{
	switch (note) {
	case 'c':
		return 0;
	case 'd':
		return 2;
	case 'e':
		return 4;
	case 'f':
		return 5;
	case 'g':
		return 7;
	case 'a':
		return 9;
	case 'b':
		return 11;
	case 'p':
		return -1; /* Pause */
	default:
		return -2; /* Invalid */
	}
}

static uint16_t rtttl_calc_freq(int note_idx, int octave)
{
	if (note_idx < 0) {
		return 0; /* Pause */
	}

	uint16_t freq = note_freq_o4[note_idx];

	/* Adjust for octave (base is octave 4) */
	if (octave < 4) {
		freq >>= (4 - octave);
	} else if (octave > 4) {
		freq <<= (octave - 4);
	}

	return freq;
}

static const char *rtttl_skip_ws(const char *p)
{
	while (*p == ' ' || *p == ',') {
		p++;
	}
	return p;
}

static int rtttl_parse_num(const char **pp)
{
	const char *p = *pp;
	int num = 0;

	while (*p >= '0' && *p <= '9') {
		num = num * 10 + (*p - '0');
		p++;
	}

	*pp = p;
	return num;
}

static void buzzer_hw_set_freq(uint16_t freq_hz)
{
	if (freq_hz == 0) {
		/* Turn off buzzer */
		pwm_set(buzzer_pwm, BUZZER_PWM_CHANNEL, 0, 0, 0);
		LOG_DBG("Buzzer off");
	} else {
		/* Calculate period in microseconds */
		uint32_t period_us = USEC_PER_SEC / freq_hz;
		/* 50% duty cycle */
		uint32_t pulse_us = period_us / 2;

		int rc = pwm_set(buzzer_pwm, BUZZER_PWM_CHANNEL, PWM_USEC(period_us),
				 PWM_USEC(pulse_us), 0);
		if (rc != 0) {
			LOG_ERR("Failed to set buzzer PWM: %d", rc);
			return;
		}

		LOG_DBG("Buzzer: %u Hz", freq_hz);
	}
}

static int buzzer_hw_init(void)
{
	if (!device_is_ready(buzzer_pwm)) {
		LOG_ERR("Buzzer PWM device not ready");
		return -ENODEV;
	}

	/* Ensure buzzer is off initially */
	pwm_set(buzzer_pwm, BUZZER_PWM_CHANNEL, 0, 0, 0);

	LOG_DBG("Buzzer initialized (PWM ch%d)", BUZZER_PWM_CHANNEL);

	return 0;
}

static void buzzer_update_output(void)
{
	if (!buzzer_state.play.active) {
		buzzer_hw_set_freq(0);
		return;
	}

	if (buzzer_state.play.melody.notes != NULL &&
	    buzzer_state.play.current_note < buzzer_state.play.melody.length) {
		buzzer_hw_set_freq(
			buzzer_state.play.melody.notes[buzzer_state.play.current_note].freq_hz);
	} else {
		buzzer_hw_set_freq(0);
	}
}

static void buzzer_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&buzzer_mutex, K_FOREVER);

	if (!buzzer_state.play.active) {
		/* If playback was stopped, stop() already turned output off while power was held.
		 * Avoid touching PWM after the shared power-domain may have been released.
		 */
		k_mutex_unlock(&buzzer_mutex);
		return;
	}

	struct buzzer_play_state *play = &buzzer_state.play;

	/* Move to next note */
	play->current_note++;

	if (play->current_note >= play->melody.length) {
		/* Melody complete */
		play->active = false;
		buzzer_update_output();
		k_mutex_unlock(&buzzer_mutex);
		/* Signal completion */
		k_sem_give(&buzzer_done_sem);
		return;
	}

	/* Play current note */
	buzzer_update_output();

	uint16_t duration = play->melody.notes[play->current_note].duration_ms;
	k_mutex_unlock(&buzzer_mutex);

	if (duration > 0) {
		k_work_reschedule(&buzzer_work, K_MSEC(duration));
	}
}

static void buzzer_start_play(void)
{
	struct buzzer_play_state *play = &buzzer_state.play;

	play->current_note = 0;

	buzzer_update_output();

	if (play->melody.notes != NULL && play->melody.length > 0) {
		uint16_t duration = play->melody.notes[0].duration_ms;
		if (duration > 0) {
			k_work_reschedule(&buzzer_work, K_MSEC(duration));
		}
	}
}

bool indicator_buzzer_is_ready(void)
{
	return buzzer_state.ready;
}

int indicator_buzzer_play(const struct indicator_buzzer_melody *melody)
{
	if (melody == NULL || melody->notes == NULL || melody->length == 0) {
		return -EINVAL;
	}

	if (!buzzer_state.ready) {
		return -ENODEV;
	}

	k_mutex_lock(&buzzer_mutex, K_FOREVER);

	/* Reset completion semaphore */
	k_sem_reset(&buzzer_done_sem);

	/* Setup state - replaces any current request */
	buzzer_state.play.melody = *melody;
	buzzer_state.play.active = true;

	buzzer_start_play();

	k_mutex_unlock(&buzzer_mutex);

	LOG_DBG("Play started: notes=%d", melody->length);

	return 0;
}

void indicator_buzzer_stop(void)
{
	k_mutex_lock(&buzzer_mutex, K_FOREVER);

	buzzer_state.play.active = false;
	buzzer_update_output();

	k_mutex_unlock(&buzzer_mutex);

	/* Signal completion in case someone is waiting */
	k_sem_give(&buzzer_done_sem);

	LOG_DBG("Play stopped");
}

int indicator_buzzer_play_sync(const struct indicator_buzzer_melody *melody, k_timeout_t timeout)
{
	k_timepoint_t end;
	int rc = 0;

	if (melody == NULL || melody->notes == NULL || melody->length == 0) {
		return -EINVAL;
	}

	if (!buzzer_state.ready) {
		return -ENODEV;
	}

	end = sys_timepoint_calc(timeout);

	k_mutex_lock(&buzzer_mutex, K_FOREVER);

	(void)k_work_cancel_delayable(&buzzer_work);
	k_sem_reset(&buzzer_done_sem);

	buzzer_state.play.melody = *melody;
	buzzer_state.play.active = true;

	for (uint8_t note = 0U; note < melody->length; note++) {
		if (sys_timepoint_expired(end)) {
			rc = -ETIMEDOUT;
			break;
		}

		buzzer_state.play.current_note = note;
		buzzer_update_output();

		if (melody->notes[note].duration_ms > 0U) {
			k_sleep(K_MSEC(melody->notes[note].duration_ms));
		}
	}

	buzzer_state.play.active = false;
	buzzer_update_output();

	k_mutex_unlock(&buzzer_mutex);
	k_sem_give(&buzzer_done_sem);

	return rc;
}

int indicator_buzzer_play_rtttl(const char *rtttl_string)
{
	if (rtttl_string == NULL || *rtttl_string == '\0') {
		return -EINVAL;
	}

	if (!buzzer_state.ready) {
		return -ENODEV;
	}

	const char *p = rtttl_string;

	/* Skip name section (everything before first ':') */
	while (*p && *p != ':') {
		p++;
	}
	if (*p != ':') {
		LOG_ERR("RTTTL: missing name separator");
		return -EINVAL;
	}
	p++; /* Skip ':' */

	/* Parse defaults section: d=N,o=N,b=N */
	int default_duration = 4;
	int default_octave = 6;
	int bpm = 63;

	p = rtttl_skip_ws(p);

	while (*p && *p != ':') {
		char key = *p++;

		if (*p == '=') {
			p++;
		}

		int val = rtttl_parse_num(&p);

		switch (key) {
		case 'd':
			default_duration = val;
			break;
		case 'o':
			default_octave = val;
			break;
		case 'b':
			bpm = val;
			break;
		default:
			break;
		}

		p = rtttl_skip_ws(p);
	}

	if (*p != ':') {
		LOG_ERR("RTTTL: missing defaults separator");
		return -EINVAL;
	}

	if (default_duration <= 0) {
		LOG_ERR("RTTTL: invalid default duration: %d", default_duration);
		return -EINVAL;
	}
	if (bpm <= 0) {
		LOG_ERR("RTTTL: invalid bpm: %d", bpm);
		return -EINVAL;
	}
	if (default_octave < 1) {
		LOG_WRN("RTTTL: default octave too low: %d, clamping to 1", default_octave);
		default_octave = 1;
	} else if (default_octave > 9) {
		LOG_WRN("RTTTL: default octave too high: %d, clamping to 9", default_octave);
		default_octave = 9;
	}

	p++; /* Skip ':' */

	/* Calculate whole note duration in ms */
	uint32_t whole_note_ms = (60000U * 4U) / (uint32_t)bpm;

	/* Parse notes */
	uint8_t note_count = 0;

	p = rtttl_skip_ws(p);

	while (*p && note_count < CONFIG_MBS_INDICATOR_BUZZER_RTTTL_MAX_NOTES) {
		int duration = 0;
		int note_idx = -2;
		bool dotted = false;
		int octave = default_octave;

		/* Parse optional duration prefix */
		duration = rtttl_parse_num(&p);
		if (duration == 0) {
			duration = default_duration;
		}

		/* Parse note name */
		if (*p) {
			char c = *p++;

			/* Convert to lowercase */
			if (c >= 'A' && c <= 'Z') {
				c = c - 'A' + 'a';
			}
			note_idx = rtttl_get_note_index(c);
		}

		if (note_idx == -2) {
			/* Invalid note, skip to next */
			p = rtttl_skip_ws(p);
			continue;
		}

		/* Check for sharp */
		if (*p == '#') {
			p++;
			if (note_idx >= 0) {
				note_idx++;
				if (note_idx > 11) {
					note_idx = 0;
					octave++;
				}
			}
		}

		/* Check for dotted note (first occurrence) */
		if (*p == '.') {
			dotted = true;
			p++;
		}

		/* Parse optional octave */
		if (*p >= '0' && *p <= '9') {
			octave = rtttl_parse_num(&p);
		}
		if (octave < 1) {
			octave = 1;
		} else if (octave > 9) {
			octave = 9;
		}

		/* Check for dotted note (after octave) */
		if (*p == '.') {
			dotted = true;
			p++;
		}

		/* Calculate duration in ms */
		uint32_t dur_ms = whole_note_ms / (uint32_t)duration;
		if (dotted) {
			dur_ms = dur_ms + dur_ms / 2;
		}

		/* Calculate frequency */
		uint16_t freq = rtttl_calc_freq(note_idx, octave);

		/* Store note */
		rtttl_notes[note_count].freq_hz = freq;
		rtttl_notes[note_count].duration_ms = (uint16_t)MIN(dur_ms, UINT16_MAX);
		note_count++;

		p = rtttl_skip_ws(p);
	}

	if (note_count == 0) {
		LOG_ERR("RTTTL: no valid notes");
		return -EINVAL;
	}

	/* Setup melody and play */
	rtttl_melody.notes = rtttl_notes;
	rtttl_melody.length = note_count;

	LOG_DBG("RTTTL parsed: %d notes, bpm=%d, d=%d, o=%d", note_count, bpm, default_duration,
		default_octave);

	return indicator_buzzer_play(&rtttl_melody);
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */
int mbs_indicator_buzzer_init(void)
{
	k_work_init_delayable(&buzzer_work, buzzer_work_handler);

	k_mutex_lock(&buzzer_mutex, K_FOREVER);
	int rc = buzzer_hw_init();
	k_mutex_unlock(&buzzer_mutex);
	if (rc == 0) {
		buzzer_state.ready = true;
		LOG_INF("Indicator buzzer is ready");
	} else if (rc == -ENODEV) {
		LOG_WRN("Indicator no buzzer hardware");
	} else {
		LOG_ERR("Indicator buzzer initialize failed: %d", rc);
	}

	return 0; /* Don't fail system init */
}

#elif IS_ENABLED(CONFIG_MBS_INDICATOR_BUZZER)

bool indicator_buzzer_is_ready(void)
{
	return false;
}

int mbs_indicator_buzzer_init(void)
{
	return 0;
}

int indicator_buzzer_play(const struct indicator_buzzer_melody *melody)
{
	ARG_UNUSED(melody);
	return -ENODEV;
}

int indicator_buzzer_play_rtttl(const char *rtttl_string)
{
	ARG_UNUSED(rtttl_string);
	return -ENODEV;
}

void indicator_buzzer_stop(void)
{
}

int indicator_buzzer_play_sync(const struct indicator_buzzer_melody *melody, k_timeout_t timeout)
{
	ARG_UNUSED(melody);
	ARG_UNUSED(timeout);
	return -ENODEV;
}

#endif /* CONFIG_MBS_INDICATOR_BUZZER */
