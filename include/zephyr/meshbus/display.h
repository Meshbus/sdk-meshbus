/*
 * Copyright (c) 2025 FoBE Projects
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Meshbus Display API
 *
 *  This module provides display management helpers and display state.
 */

#ifndef ZEPHYR_INCLUDE_MESHBUS_DISPLAY_H_
#define ZEPHYR_INCLUDE_MESHBUS_DISPLAY_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/iterable_sections.h>
#include <zephyr/zbus/zbus.h>

#include "meshbus/display.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Display runtime configuration (maps to meshbus_DisplayConfig).
 */
typedef meshbus_DisplayConfig meshbus_display_config;

/**
 * @brief Display state event published via ZBus.
 *
 * Published when display state changes (e.g., sleep).
 */
struct meshbus_display_state_event {
	/** True if display is active (awake), false if sleeping */
	bool active;
};

/** @brief ZBus channel for display state events. */
ZBUS_CHAN_DECLARE(meshbus_display_state_chan);

/**
 * @brief Get display configuration
 *
 * @param cfg Pointer to configuration structure to fill.
 * @return 0 on success, negative errno on failure.
 */
int meshbus_display_config_get(meshbus_display_config *cfg);

/**
 * @brief Set display configuration
 *
 * Validates and applies the configuration immediately.
 * Configuration is saved to flash after a delay.
 *
 * @param cfg Pointer to configuration to apply.
 * @return 0 on success, negative errno on failure.
 */
int meshbus_display_config_set(const meshbus_display_config *cfg);

/**
 * @brief Reset display configuration to defaults
 *
 * @return 0 on success, negative errno on failure.
 */
int meshbus_display_config_reset(void);

/**
 * @brief Check if display is active (awake)
 *
 * @return true if display is active, false if sleeping.
 */
bool meshbus_display_is_active(void);

/**
 * @brief Set display active state
 *
 * This function is typically called by the display driver when the display state changes.
 * It updates internal state and publishes a display state event via ZBus.
 *
 * @param active True if display is active (awake), false if sleeping.
 */
void meshbus_display_active(bool active);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MESHBUS_DISPLAY_H_ */
