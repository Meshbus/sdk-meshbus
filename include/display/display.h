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

#ifndef ZEPHYR_INCLUDE_MBS_DISPLAY_H_
#define ZEPHYR_INCLUDE_MBS_DISPLAY_H_

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
typedef meshbus_DisplayConfig mbs_display_config;

/**
 * @brief Display state event published via ZBus.
 *
 * Published when display state changes (e.g., sleep).
 */
struct mbs_display_state_event {
	/** True if display is active (awake), false if sleeping */
	bool active;
};

/** @brief ZBus channel for display state events. */
ZBUS_CHAN_DECLARE(mbs_display_state_chan);

/**
 * @brief Get display configuration
 *
 * @param cfg Pointer to configuration structure to fill.
 * @return 0 on success, negative errno on failure.
 */
int mbs_display_config_get(mbs_display_config *cfg);

/**
 * @brief Set display configuration
 *
 * Validates and applies the configuration immediately.
 * Configuration is saved to flash after a delay.
 *
 * @param cfg Pointer to configuration to apply.
 * @return 0 on success, negative errno on failure.
 */
int mbs_display_config_set(const mbs_display_config *cfg);

/**
 * @brief Reset display configuration to defaults
 *
 * @return 0 on success, negative errno on failure.
 */
int mbs_display_config_reset(void);

/**
 * @brief Check if display is active (awake)
 *
 * @return true if display is active, false if sleeping.
 */
bool mbs_display_is_active(void);

/**
 * @brief Set display active state
 *
 * This function is typically called by the display driver when the display state changes.
 * It updates internal state and publishes a display state event via ZBus.
 *
 * @param active True if display is active (awake), false if sleeping.
 */
void mbs_display_active(bool active);

#define MBS_DISPLAY_DUMP_CHUNK_SIZE 256U

/** Caller-owned chunk of a frozen software frame, in physical coordinates. */
struct mbs_display_dump_chunk {
	uint32_t snapshot_id;
	uint32_t offset;
	uint32_t total_size;
	uint16_t width;
	uint16_t height;
	meshbus_DisplayDumpFormat format;
	meshbus_DisplayDumpOrientation orientation;
	bool inverted;
	uint16_t data_len;
	uint8_t data[MBS_DISPLAY_DUMP_CHUNK_SIZE];
};

/**
 * Capture or read a frozen display snapshot from thread context.
 * snapshot_id=0 with offset=0 captures the last complete submitted frame.
 * length=0 selects CHUNK_SIZE; otherwise length must be 1..CHUNK_SIZE.
 * Read subsequent chunks with the returned ID and byte offset. The single
 * shared snapshot is immutable until the next successful capture, including
 * across renderer teardown. A new capture invalidates the previous ID for all
 * clients. IDs are boot-local, and zero is never returned. Chunk retries are
 * allowed; offset must be below total_size. The final chunk may be shorter.
 * No panel I/O is performed. Blanking and brightness are not captured.
 * Returns -EINVAL for invalid arguments, -ENOENT for a stale ID, -ENODEV
 * without a submitted frame, -ENOSPC when the frame exceeds configured storage,
 * or -ENOTSUP without snapshot support. Output is untouched on failure.
 */
int mbs_display_dump_read(uint32_t snapshot_id, uint32_t offset, uint32_t length,
			      struct mbs_display_dump_chunk *chunk);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MBS_DISPLAY_H_ */
