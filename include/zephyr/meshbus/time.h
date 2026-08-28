/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Meshbus time helper API
 */

#ifndef ZEPHYR_INCLUDE_MESHBUS_TIME_H_
#define ZEPHYR_INCLUDE_MESHBUS_TIME_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MESHBUS_TIME_VALID_UNIX_TIMESTAMP_S 1704067200ULL

/**
 * @brief Return whether SYS_CLOCK_REALTIME currently looks synchronized.
 *
 * The threshold is intentionally source-agnostic. Any producer that updates
 * SYS_CLOCK_REALTIME (BLE, mesh, RTC, SNTP, or shell) can make business
 * timestamps use real Unix time.
 *
 * @return true when realtime is at or after 2024-01-01T00:00:00Z.
 */
bool meshbus_time_realtime_is_valid(void);

/**
 * @brief Get a business timestamp in seconds.
 *
 * Returns synchronized Unix seconds when SYS_CLOCK_REALTIME is valid, otherwise
 * falls back to monotonic uptime seconds. The value is never zero on success.
 *
 * @param[out] out_timestamp_s Timestamp in seconds.
 * @return 0 on success, -EINVAL if out_timestamp_s is NULL.
 */
int meshbus_time_timestamp_s_get(uint32_t *out_timestamp_s);

/**
 * @brief Get a business timestamp in milliseconds.
 *
 * Returns synchronized Unix milliseconds when SYS_CLOCK_REALTIME is valid,
 * otherwise falls back to monotonic uptime milliseconds. The value is never zero
 * on success.
 *
 * @param[out] out_timestamp_ms Timestamp in milliseconds.
 * @return 0 on success, -EINVAL if out_timestamp_ms is NULL.
 */
int meshbus_time_timestamp_ms_get(uint64_t *out_timestamp_ms);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MESHBUS_TIME_H_ */
