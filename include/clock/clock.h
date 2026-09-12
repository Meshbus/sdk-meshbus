/*
 * Copyright (c) 2025 FoBE Projects
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Meshbus Clock API
 *
 * Local civil-time configuration and realtime clock setting require
 * CONFIG_MBS_CLOCK. Basic timestamps in <clock/timestamp.h> require
 * only CONFIG_MBS.
 */

#ifndef ZEPHYR_INCLUDE_MBS_CLOCK_H_
#define ZEPHYR_INCLUDE_MBS_CLOCK_H_

#include <clock/timestamp.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "meshbus/clock.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Clock runtime configuration (maps to meshbus_ClockConfig).
 */
typedef meshbus_ClockConfig mbs_clock_config;

#define MBS_CLOCK_MAX_UTC_OFFSET_MINUTES 840
#define MBS_CLOCK_MIN_UTC_OFFSET_MINUTES (-720)

/**
 * @brief Get current clock configuration.
 *
 * @param[out] cfg Configuration buffer.
 * @return 0 on success, -EINVAL if cfg is NULL.
 */
int mbs_clock_config_get(mbs_clock_config *cfg);

/**
 * @brief Set a new clock configuration.
 *
 * Validates, applies, and persists via Settings.
 *
 * @param cfg New configuration.
 * @return 0 on success, -EINVAL on validation failure.
 */
int mbs_clock_config_set(const mbs_clock_config *cfg);

/**
 * @brief Reset clock configuration to defaults.
 *
 * @return 0 on success, negative errno on failure.
 */
int mbs_clock_config_reset(void);

/**
 * @brief Convert a UTC Unix timestamp to configured local civil time.
 *
 * Applies the current ClockConfig UTC offset and converts the result with
 * @c gmtime_r. The returned @c tm does not depend on the C library process
 * timezone.
 *
 * @param utc_time UTC Unix timestamp in seconds.
 * @param[out] local_time Converted local civil time.
 * @return 0 on success, -EINVAL if local_time is NULL, or -ERANGE if the
 *         adjusted timestamp cannot be represented.
 */
int mbs_clock_localtime(time_t utc_time, struct tm *local_time);

/**
 * @brief Set the device real-time clock from a UTC Unix timestamp.
 *
 * This updates the runtime SYS_CLOCK_REALTIME offset only. It does not persist
 * the current time and does not change ClockConfig fields.
 *
 * @param unix_time_ms UTC Unix epoch timestamp in milliseconds.
 * @param[out] applied_unix_time_ms Optional applied timestamp read back after setting.
 * @return 0 on success, -EINVAL for invalid timestamps, negative errno on failure.
 */
int mbs_clock_time_set_unix_ms(uint64_t unix_time_ms, uint64_t *applied_unix_time_ms);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MBS_CLOCK_H_ */
