/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MBS_SERVICES_INDICATOR_BUZZER_TONE_H_
#define MBS_SERVICES_INDICATOR_BUZZER_TONE_H_

#include <zephyr/sys/util.h>
#include <indicator/indicator.h>

#define INDICATOR_BUZZER_TONE_NOTE(freq, duration)                                                 \
	{                                                                                          \
		.freq_hz = (freq),                                                                 \
		.duration_ms = (duration),                                                         \
	}
#define INDICATOR_BUZZER_TONE_REST(duration) INDICATOR_BUZZER_TONE_NOTE(0U, duration)
#define INDICATOR_BUZZER_TONE_DEFINE(name, ...)                                                    \
	static const struct indicator_buzzer_note name##_notes[] __maybe_unused = {__VA_ARGS__};   \
	static const struct indicator_buzzer_melody name __maybe_unused = {                        \
		.notes = name##_notes,                                                             \
		.length = ARRAY_SIZE(name##_notes),                                                \
	}

#if IS_ENABLED(CONFIG_MBS_INDICATOR_BUZZER)
INDICATOR_BUZZER_TONE_DEFINE(indicator_buzzer_startup_tone, INDICATOR_BUZZER_TONE_NOTE(1000U, 45U),
			     INDICATOR_BUZZER_TONE_REST(30U),
			     INDICATOR_BUZZER_TONE_NOTE(1250U, 45U),
			     INDICATOR_BUZZER_TONE_REST(30U),
			     INDICATOR_BUZZER_TONE_NOTE(1500U, 45U));
#endif

INDICATOR_BUZZER_TONE_DEFINE(indicator_buzzer_shutdown_tone, INDICATOR_BUZZER_TONE_NOTE(784U, 150U),
			     INDICATOR_BUZZER_TONE_REST(50U),
			     INDICATOR_BUZZER_TONE_NOTE(659U, 150U),
			     INDICATOR_BUZZER_TONE_REST(50U),
			     INDICATOR_BUZZER_TONE_NOTE(523U, 300U));

#if IS_ENABLED(CONFIG_MBS_INDICATOR_MESSAGE_FEEDBACK)
#define INDICATOR_BUZZER_MESSAGE_REFERENCE_FREQ_HZ 1568U
#define INDICATOR_BUZZER_MESSAGE_BASE_FREQ_HZ      4000U
#define INDICATOR_BUZZER_MESSAGE_FREQ_HZ(freq)                                            \
	((uint16_t)(((uint32_t)(freq) *                                                      \
		      INDICATOR_BUZZER_MESSAGE_BASE_FREQ_HZ +                                   \
		      (INDICATOR_BUZZER_MESSAGE_REFERENCE_FREQ_HZ / 2U)) /                      \
		     INDICATOR_BUZZER_MESSAGE_REFERENCE_FREQ_HZ))

INDICATOR_BUZZER_TONE_DEFINE(indicator_buzzer_message_feedback_tone,
			     INDICATOR_BUZZER_TONE_NOTE(
				     INDICATOR_BUZZER_MESSAGE_FREQ_HZ(1568U), 75U),
			     INDICATOR_BUZZER_TONE_NOTE(
				     INDICATOR_BUZZER_MESSAGE_FREQ_HZ(1568U), 75U),
			     INDICATOR_BUZZER_TONE_NOTE(
				     INDICATOR_BUZZER_MESSAGE_FREQ_HZ(1048U), 75U),
			     INDICATOR_BUZZER_TONE_REST(75U),
			     INDICATOR_BUZZER_TONE_NOTE(
				     INDICATOR_BUZZER_MESSAGE_FREQ_HZ(1320U), 75U));
#endif

#if IS_ENABLED(CONFIG_MBS_INDICATOR_INPUT_FEEDBACK)
#define INDICATOR_BUZZER_INPUT_BASE_FREQ_HZ 1500U
#define INDICATOR_BUZZER_INPUT_STEP_HZ      500U
#define INDICATOR_BUZZER_INPUT_HALF_STEP_HZ 350U

INDICATOR_BUZZER_TONE_DEFINE(indicator_buzzer_input_short_tone,
			     INDICATOR_BUZZER_TONE_NOTE(INDICATOR_BUZZER_INPUT_BASE_FREQ_HZ, 15U));

INDICATOR_BUZZER_TONE_DEFINE(indicator_buzzer_input_long_tone,
			     INDICATOR_BUZZER_TONE_NOTE(INDICATOR_BUZZER_INPUT_BASE_FREQ_HZ, 15U),
			     INDICATOR_BUZZER_TONE_REST(15U),
			     INDICATOR_BUZZER_TONE_NOTE(INDICATOR_BUZZER_INPUT_BASE_FREQ_HZ, 15U));

INDICATOR_BUZZER_TONE_DEFINE(indicator_buzzer_input_ok_short_tone,
			     INDICATOR_BUZZER_TONE_NOTE(INDICATOR_BUZZER_INPUT_BASE_FREQ_HZ, 15U),
			     INDICATOR_BUZZER_TONE_NOTE(INDICATOR_BUZZER_INPUT_BASE_FREQ_HZ +
								INDICATOR_BUZZER_INPUT_STEP_HZ,
							15U));

INDICATOR_BUZZER_TONE_DEFINE(indicator_buzzer_input_ok_long_tone,
			     INDICATOR_BUZZER_TONE_NOTE(INDICATOR_BUZZER_INPUT_BASE_FREQ_HZ, 45U),
			     INDICATOR_BUZZER_TONE_REST(15U),
			     INDICATOR_BUZZER_TONE_NOTE(INDICATOR_BUZZER_INPUT_BASE_FREQ_HZ +
								INDICATOR_BUZZER_INPUT_HALF_STEP_HZ,
							45U),
			     INDICATOR_BUZZER_TONE_REST(15U),
			     INDICATOR_BUZZER_TONE_NOTE(INDICATOR_BUZZER_INPUT_BASE_FREQ_HZ +
								INDICATOR_BUZZER_INPUT_STEP_HZ,
							45U));

INDICATOR_BUZZER_TONE_DEFINE(indicator_buzzer_input_back_short_tone,
			     INDICATOR_BUZZER_TONE_NOTE(INDICATOR_BUZZER_INPUT_BASE_FREQ_HZ, 15U),
			     INDICATOR_BUZZER_TONE_REST(15U),
			     INDICATOR_BUZZER_TONE_NOTE(INDICATOR_BUZZER_INPUT_BASE_FREQ_HZ -
								INDICATOR_BUZZER_INPUT_STEP_HZ,
							15U));

INDICATOR_BUZZER_TONE_DEFINE(indicator_buzzer_input_back_long_tone,
			     INDICATOR_BUZZER_TONE_NOTE(INDICATOR_BUZZER_INPUT_BASE_FREQ_HZ, 45U),
			     INDICATOR_BUZZER_TONE_REST(15U),
			     INDICATOR_BUZZER_TONE_NOTE(INDICATOR_BUZZER_INPUT_BASE_FREQ_HZ -
								INDICATOR_BUZZER_INPUT_HALF_STEP_HZ,
							45U),
			     INDICATOR_BUZZER_TONE_REST(15U),
			     INDICATOR_BUZZER_TONE_NOTE(INDICATOR_BUZZER_INPUT_BASE_FREQ_HZ -
								INDICATOR_BUZZER_INPUT_STEP_HZ,
							45U));
#endif

#if IS_ENABLED(CONFIG_MBS_POWER_BACK_HOLD_SHUTDOWN) && IS_ENABLED(CONFIG_MBS_INDICATOR)
INDICATOR_BUZZER_TONE_DEFINE(indicator_buzzer_power_back_hold_release_tone,
			     INDICATOR_BUZZER_TONE_NOTE(1500U, 45U),
			     INDICATOR_BUZZER_TONE_REST(30U),
			     INDICATOR_BUZZER_TONE_NOTE(1250U, 45U),
			     INDICATOR_BUZZER_TONE_REST(30U),
			     INDICATOR_BUZZER_TONE_NOTE(1000U, 45U));
#endif

#endif /* MBS_SERVICES_INDICATOR_BUZZER_TONE_H_ */
