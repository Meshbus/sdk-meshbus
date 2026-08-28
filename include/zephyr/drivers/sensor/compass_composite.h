/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Extended sensor API for compass composite sensor driver
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_COMPASS_COMPOSITE_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_COMPASS_COMPOSITE_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor/compass.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Copy the unique physical source devices used by a composite.
 *
 * Accel and gyro sources that resolve to the same device are returned once.
 * Passing @p sources as NULL with zero capacity queries the required count.
 *
 * @param dev Compass composite device.
 * @param[out] sources Destination array, or NULL for a count query.
 * @param capacity Number of entries available in @p sources.
 * @return Unique source count on success, or negative errno on failure.
 */
int compass_composite_sources_get(const struct device *dev, const struct device **sources,
				  size_t capacity);

/**
 * @brief Override accelerometer and gyroscope source sampling frequency.
 *
 * The first successful override saves both source frequencies so a later
 * compass_composite_imu_rate_restore() can restore the exact runtime state.
 * Accel and gyro updates are applied transactionally and rolled back when one
 * source rejects the requested rate.
 *
 * @param dev Compass composite device.
 * @param frequency_hz Requested whole-Hz accel and gyro frequency.
 * @return 0 on success, -ENOTSUP when a source has no runtime frequency API,
 *         -EBUSY when a different override is already active, or another
 *         negative errno from a source driver.
 */
int compass_composite_imu_rate_override(const struct device *dev, uint32_t frequency_hz);

/**
 * @brief Restore accel and gyro frequencies saved by the active override.
 *
 * @param dev Compass composite device.
 * @return 0 on success, or a negative errno from a source driver. Calling this
 *         function without an active override is a successful no-op.
 */
int compass_composite_imu_rate_restore(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_COMPASS_COMPOSITE_H_ */
