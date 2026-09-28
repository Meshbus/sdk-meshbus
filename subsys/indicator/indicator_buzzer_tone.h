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
INDICATOR_BUZZER_TONE_DEFINE(indicator_buzzer_startup_tone,
			     INDICATOR_BUZZER_TONE_NOTE(784U, 60U),
			     INDICATOR_BUZZER_TONE_REST(20U),
			     INDICATOR_BUZZER_TONE_NOTE(988U, 60U),
			     INDICATOR_BUZZER_TONE_REST(20U),
			     INDICATOR_BUZZER_TONE_NOTE(1175U, 100U),
			     INDICATOR_BUZZER_TONE_REST(40U),
			     INDICATOR_BUZZER_TONE_NOTE(1568U, 160U));
#endif

INDICATOR_BUZZER_TONE_DEFINE(indicator_buzzer_shutdown_tone,
			     INDICATOR_BUZZER_TONE_NOTE(1175U, 90U),
			     INDICATOR_BUZZER_TONE_REST(30U),
			     INDICATOR_BUZZER_TONE_NOTE(988U, 90U),
			     INDICATOR_BUZZER_TONE_REST(30U),
			     INDICATOR_BUZZER_TONE_NOTE(784U, 200U));

#if IS_ENABLED(CONFIG_MBS_INDICATOR_INPUT_FEEDBACK)
INDICATOR_BUZZER_TONE_DEFINE(indicator_buzzer_input_short_tone,
			     INDICATOR_BUZZER_TONE_NOTE(1175U, 10U));

INDICATOR_BUZZER_TONE_DEFINE(indicator_buzzer_input_long_tone,
			     INDICATOR_BUZZER_TONE_NOTE(1175U, 20U),
			     INDICATOR_BUZZER_TONE_REST(20U),
			     INDICATOR_BUZZER_TONE_NOTE(1175U, 20U));

INDICATOR_BUZZER_TONE_DEFINE(indicator_buzzer_input_ok_short_tone,
			     INDICATOR_BUZZER_TONE_NOTE(988U, 20U),
			     INDICATOR_BUZZER_TONE_NOTE(1175U, 30U));

INDICATOR_BUZZER_TONE_DEFINE(indicator_buzzer_input_ok_long_tone,
			     INDICATOR_BUZZER_TONE_NOTE(988U, 30U),
			     INDICATOR_BUZZER_TONE_REST(10U),
			     INDICATOR_BUZZER_TONE_NOTE(1175U, 30U),
			     INDICATOR_BUZZER_TONE_REST(10U),
			     INDICATOR_BUZZER_TONE_NOTE(1568U, 50U));

INDICATOR_BUZZER_TONE_DEFINE(indicator_buzzer_input_back_short_tone,
			     INDICATOR_BUZZER_TONE_NOTE(988U, 20U),
			     INDICATOR_BUZZER_TONE_NOTE(784U, 30U));

INDICATOR_BUZZER_TONE_DEFINE(indicator_buzzer_input_back_long_tone,
			     INDICATOR_BUZZER_TONE_NOTE(1175U, 30U),
			     INDICATOR_BUZZER_TONE_REST(10U),
			     INDICATOR_BUZZER_TONE_NOTE(988U, 30U),
			     INDICATOR_BUZZER_TONE_REST(10U),
			     INDICATOR_BUZZER_TONE_NOTE(784U, 50U));
#endif

#if IS_ENABLED(CONFIG_MBS_POWER_BACK_HOLD_SHUTDOWN) && IS_ENABLED(CONFIG_MBS_INDICATOR)
INDICATOR_BUZZER_TONE_DEFINE(indicator_buzzer_power_back_hold_release_tone,
			     INDICATOR_BUZZER_TONE_NOTE(988U, 40U),
			     INDICATOR_BUZZER_TONE_REST(20U),
			     INDICATOR_BUZZER_TONE_NOTE(784U, 70U));
#endif

#endif /* MBS_SERVICES_INDICATOR_BUZZER_TONE_H_ */
