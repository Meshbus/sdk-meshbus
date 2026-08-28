/*
 * Copyright (c) 2025 FoBE Projects
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Meshbus Indicator API
 *
 * This module provides simple LED and buzzer control with persisted configuration.
 */

#ifndef ZEPHYR_INCLUDE_MESHBUS_INDICATOR_H_
#define ZEPHYR_INCLUDE_MESHBUS_INDICATOR_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/zbus/zbus.h>

#include "meshbus/indicator.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Indicator light-play request channel. */
ZBUS_CHAN_DECLARE(meshbus_indicator_light_play_chan);

/** @brief Indicator buzzer-play request channel. */
ZBUS_CHAN_DECLARE(meshbus_indicator_buzzer_play_chan);

/**
 * @brief Indicator runtime configuration (maps to meshbus_IndicatorConfig).
 */
typedef meshbus_IndicatorConfig meshbus_indicator_config;

/** @brief Indicator light feedback routing policy. */
typedef meshbus_IndicatorConfig_LightFeedback meshbus_indicator_light_feedback;

/** @brief Indicator buzzer feedback routing policy. */
typedef meshbus_IndicatorConfig_BuzzerFeedback meshbus_indicator_buzzer_feedback;

/**
 * @brief Buzzer request source type.
 *
 * Used for filtering based on the buzzer feedback configuration.
 */
enum indicator_buzzer_source {
	INDICATOR_SOURCE_SYSTEM = 0,   /**< System tones (button press, startup) */
	INDICATOR_SOURCE_DIRECT_MSG,   /**< Direct message alerts */
	INDICATOR_SOURCE_CHANNEL_MSG,  /**< Channel message alerts */
};

/**
 * @brief Buzzer note definition.
 *
 * Single note with frequency and duration.
 */
struct indicator_buzzer_note {
	uint16_t freq_hz;     /**< Frequency in Hz (0 = silence/rest) */
	uint16_t duration_ms; /**< Duration in milliseconds */
};

/**
 * @brief Check if LED driver is available.
 *
 * @return true if LED hardware is present and ready.
 */
bool meshbus_indicator_light_is_ready(void);

/**
 * @brief Check if buzzer driver is available.
 *
 * @return true if buzzer hardware is present and ready.
 */
bool meshbus_indicator_buzzer_is_ready(void);

/**
 * @brief Buzzer melody definition
 *
 * Array of notes forming a melody.
 *
 * @warning The notes array pointer is stored and accessed asynchronously
 * during playback. Caller must ensure the notes data remains valid until
 * playback completes or indicator_buzzer_stop() is called. Do NOT use
 * stack-allocated notes arrays.
 */
struct indicator_buzzer_melody {
	const struct indicator_buzzer_note *notes; /**< Pointer to note array */
	uint8_t length;                            /**< Number of notes */
};

/** @brief Request a light blinking pattern over @ref meshbus_indicator_light_play_chan. */
struct meshbus_indicator_light_play_event {
	uint32_t on_duration_ms;  /**< On duration in milliseconds */
	uint32_t off_duration_ms; /**< Off duration in milliseconds */
	uint8_t count;            /**< Number of blink cycles (0 = infinite) */
};

/** @brief Request a one-note buzzer tone over @ref meshbus_indicator_buzzer_play_chan. */
struct meshbus_indicator_buzzer_play_event {
	enum indicator_buzzer_source source; /**< Source of the buzzer request */
	uint16_t freq_hz;                    /**< Frequency in Hz */
	uint16_t duration_ms;                /**< Duration in milliseconds */
};

/**
 * @brief Get current indicator configuration.
 *
 * @param[out] cfg Configuration buffer.
 * @return 0 on success, -EINVAL if cfg is NULL.
 */
int meshbus_indicator_config_get(meshbus_indicator_config *cfg);

/**
 * @brief Set indicator configuration.
 *
 * Updates configuration and persists via Settings.
 * Changes take effect immediately (e.g., enabling/disabling heartbeat).
 *
 * @param cfg New configuration.
 * @return 0 on success, -EINVAL on validation failure.
 */
int meshbus_indicator_config_set(const meshbus_indicator_config *cfg);

/**
 * @brief Reset indicator configuration to defaults and delete persisted settings.
 *
 * @return 0 on success, negative errno on failure.
 */
int meshbus_indicator_config_reset(void);

/**
 * @brief Set idle light color.
 *
 * Sets the color used for idle/heartbeat indication.
 * Only takes effect if light mode is set to IDLE or HEARTBEAT.
 *
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return 0 on success, -EINVAL on invalid parameters.
 */
int meshbus_indicator_light_idle_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Set idle light blinking pattern.
 *
 * Sets the on/off durations used for idle/heartbeat indication.
 * Only takes effect if light mode is set to IDLE or HEARTBEAT.
 *
 * @param on_duration_ms On duration in milliseconds
 * @param off_duration_ms Off duration in milliseconds
 * @return 0 on success, -EINVAL on invalid parameters.
 */
int meshbus_indicator_light_idle(uint32_t on_duration_ms, uint32_t off_duration_ms);

/**
 * @brief Set light color immediately.
 *
 * Overrides any ongoing light patterns.
 *
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return 0 on success, -EINVAL on invalid parameters.
 */
int meshbus_indicator_light_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Play a light blinking pattern.
 *
 * Overrides any ongoing light patterns.
 *
 * @param on_duration_ms On duration in milliseconds
 * @param off_duration_ms Off duration in milliseconds
 * @param count Number of blink cycles (0 = infinite)
 * @return 0 on success, -EINVAL on invalid parameters.
 */
int meshbus_indicator_light_play(uint32_t on_duration_ms, uint32_t off_duration_ms, uint8_t count);

/**
 * @brief Stop any ongoing light patterns.
 *
 * Turns off the light.
 *
 * @return 0 on success, negative errno on failure.
 */
int meshbus_indicator_light_stop(void);

/**
 * @brief Play a melody from an RTTTL (Ring Tone Text Transfer Language) string.
 *
 * RTTTL format: "name:d=N,o=N,b=N:notes"
 * - name: Melody name (ignored)
 * - d: Default duration (1,2,4,8,16,32)
 * - o: Default octave (4-7)
 * - b: BPM tempo
 * - notes: Comma-separated notes (e.g., "e6,d#6,b5")
 *
 * Example: "Nokia:d=4,o=5,b=112:e6,d#6,b5,a5"
 *
 * @param rtttl_string Null-terminated RTTTL string.
 * @return 0 on success, -EINVAL on parse error, -EACCES if buzzer disabled.
 */
int meshbus_indicator_buzzer_rtttl(const char *rtttl_string);

/**
 * @brief Play a melody.
 *
 * @param source Source of the buzzer request.
 * @param melody Pointer to melody definition.
 * @return 0 on success, -EINVAL on invalid parameters, -EACCES if buzzer disabled.
 */
int meshbus_indicator_buzzer_play(enum indicator_buzzer_source source,
				  const struct indicator_buzzer_melody *melody);

/**
 * @brief Stop any ongoing buzzer playback.
 */
void meshbus_indicator_buzzer_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MESHBUS_INDICATOR_H_ */
