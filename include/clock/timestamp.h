/* SPDX-License-Identifier: Apache-2.0 */

/* Copyright (c) 2026 FoBE Studio */

/**
 * @file
 * @brief Meshbus business timestamps and realtime validity
 */

#ifndef MESHBUS_INCLUDE_CLOCK_TIMESTAMP_H_
#define MESHBUS_INCLUDE_CLOCK_TIMESTAMP_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MBS_CLOCK_VALID_UNIX_TIMESTAMP_S 1704067200ULL

/* These operations require only CONFIG_MBS, even when CONFIG_MBS_CLOCK=n. */

/**
 * @brief Return whether SYS_CLOCK_REALTIME currently looks synchronized.
 *
 * The threshold is intentionally source-agnostic. Any producer that updates
 * SYS_CLOCK_REALTIME (BLE, mesh, RTC, SNTP, or shell) can make business
 * timestamps use real Unix time.
 *
 * @return true when realtime is at or after 2024-01-01T00:00:00Z and its
 *         seconds fit in uint32_t, with a valid nanosecond component.
 */
bool mbs_clock_realtime_is_valid(void);

/**
 * @brief Get a business timestamp in seconds.
 *
 * Returns synchronized Unix seconds when SYS_CLOCK_REALTIME is valid, otherwise
 * falls back to monotonic uptime seconds. The value is never zero on success.
 *
 * @param[out] out_timestamp_s Timestamp in seconds.
 * @return 0 on success, -EINVAL if out_timestamp_s is NULL.
 */
int mbs_clock_timestamp_s_get(uint32_t *out_timestamp_s);

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
int mbs_clock_timestamp_ms_get(uint64_t *out_timestamp_ms);


#ifdef __cplusplus
}
#endif

#endif /* MESHBUS_INCLUDE_CLOCK_TIMESTAMP_H_ */
