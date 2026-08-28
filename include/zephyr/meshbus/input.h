/*
 * Copyright (c) 2025 FoBE Projects
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Meshbus Input API
 *
 * Meshbus input publishes input events via ZBus.
 */

#ifndef ZEPHYR_INCLUDE_MESHBUS_INPUT_H_
#define ZEPHYR_INCLUDE_MESHBUS_INPUT_H_

#include <stdint.h>

#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name Input event ACTION codes.
 * @anchor INPUT_ACT_CODES
 * @{
 */
#define INPUT_ACT_KEY_SHORT  0
#define INPUT_ACT_KEY_LONG   1
#define INPUT_ACT_SCROLL_CW  2
#define INPUT_ACT_SCROLL_CCW 3

/**
 * @brief Raw input event structure.
 */
struct meshbus_input_event {
	/** Event type (see @ref INPUT_EV_CODES). */
	uint8_t type;
	/**
	 * Event code (see @ref INPUT_KEY_CODES, @ref INPUT_BTN_CODES,
	 * @ref INPUT_ABS_CODES, @ref INPUT_REL_CODES, @ref INPUT_MSC_CODES).
	 */
	uint16_t code;
	/** Event value. */
	int32_t value;
};

/**
 * @brief Processed key event structure.
 */
struct meshbus_input_act_event {
	/** Event type (see @ref INPUT_EV_CODES). */
	uint8_t type;
	/**
	 * Event code (see @ref INPUT_KEY_CODES, @ref INPUT_BTN_CODES,
	 * @ref INPUT_ABS_CODES, @ref INPUT_REL_CODES, @ref INPUT_MSC_CODES).
	 */
	uint16_t code;
	/** Input action (see @ref INPUT_ACT_CODES). */
	uint8_t action;
};

/**
 * @brief Publish a raw input event.
 *
 * @param type Input event type.
 * @param code Input event code.
 * @param value Input event value.
 *
 * @return 0 on success, or a negative errno value on failure.
 */
int meshbus_input_key_event_publish(uint8_t type, uint16_t code, int32_t value);

/**
 * @brief Publish a processed input action event.
 *
 * @param type Input event type.
 * @param code Input event code.
 * @param action Input action (see @ref INPUT_ACT_CODES).
 *
 * @return 0 on success, or a negative errno value on failure.
 */
int meshbus_input_action_event_publish(uint8_t type, uint16_t code, uint8_t action);

/**
 * @brief Raw input events (directly forwarded from the input callback).
 *
 * Compatibility note:
 * The raw channel symbol is implemented as `meshbus_input_key_chan`.
 * Keep `meshbus_input_raw_event_chan` as a preprocessor alias so modules
 * can use the clearer name without requiring a repo-wide rename.
 */
ZBUS_CHAN_DECLARE(meshbus_input_key_chan);
#define meshbus_input_raw_event_chan meshbus_input_key_chan

/** @brief Processed key events (after duration/gesture detection). */
ZBUS_CHAN_DECLARE(meshbus_input_action_chan);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MESHBUS_INPUT_H_ */
