/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Common sensor API extensions for Compass heading providers
 */

#ifndef MESHBUS_INCLUDE_DRIVERS_SENSOR_COMPASS_H_
#define MESHBUS_INCLUDE_DRIVERS_SENSOR_COMPASS_H_

#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Additional channel implemented by a Compass heading provider. */
enum compass_sensor_channel {
	/** Final heading in degrees in the range [0, 360). */
	SENSOR_CHAN_COMPASS_HEADING = SENSOR_CHAN_PRIV_START,
};

/** Common and optional attributes implemented by Compass providers. */
enum compass_sensor_attribute {
	/** Magnetic declination in degrees. */
	SENSOR_ATTR_COMPASS_DECLINATION = SENSOR_ATTR_PRIV_START,
	/** Read-only calibration movement hint. */
	SENSOR_ATTR_COMPASS_CAL_HINT,
	/** Read-only provider quality value. */
	SENSOR_ATTR_COMPASS_ACCURACY,
	/** Board/assembly yaw mounting offset in degrees. */
	SENSOR_ATTR_COMPASS_MOUNT_OFFSET,
	/** Optional provider-specific fusion alpha in range [0.0, 1.0]. */
	SENSOR_ATTR_COMPASS_FUSION_ALPHA,
	/** Optional active magnetometer hard-iron bias. */
	SENSOR_ATTR_COMPASS_MAG_BIAS,
	/** Optional active magnetometer soft-iron scale. */
	SENSOR_ATTR_COMPASS_MAG_SCALE,
	/** Optional estimated magnetometer hard-iron bias. */
	SENSOR_ATTR_COMPASS_MAG_BIAS_EST,
	/** Optional estimated magnetometer soft-iron scale. */
	SENSOR_ATTR_COMPASS_MAG_SCALE_EST,
	/** Reset live calibration state. */
	SENSOR_ATTR_COMPASS_CAL_RESET,
	/** Read-only bit mask of enum compass_sensor_capability. */
	SENSOR_ATTR_COMPASS_CAPABILITIES,
	/**
	 * Volatile interactive sampling-frequency override in Hz.
	 *
	 * A positive value requests an interactive profile. Zero restores the
	 * exact provider state captured by the first successful override.
	 */
	SENSOR_ATTR_COMPASS_RATE_OVERRIDE,
};

/** Common calibration hints returned by SENSOR_ATTR_COMPASS_CAL_HINT. */
enum compass_sensor_cal_hint {
	COMPASS_CAL_HINT_NONE = 0,
	COMPASS_CAL_HINT_FIGURE_EIGHT = 1,
	COMPASS_CAL_HINT_KEEP_LEVEL = 2,
};

/** Common quality values returned by SENSOR_ATTR_COMPASS_ACCURACY. */
enum compass_sensor_accuracy {
	COMPASS_ACCURACY_UNRELIABLE = 0,
	COMPASS_ACCURACY_LOW = 1,
	COMPASS_ACCURACY_MEDIUM = 2,
	COMPASS_ACCURACY_HIGH = 3,
};

/** Provider capability bits returned by SENSOR_ATTR_COMPASS_CAPABILITIES. */
enum compass_sensor_capability {
	COMPASS_CAP_TILT_COMPENSATED = BIT(0),
	COMPASS_CAP_DECLINATION = BIT(1),
	COMPASS_CAP_MOUNT_OFFSET = BIT(2),
	COMPASS_CAP_CALIBRATION = BIT(3),
	COMPASS_CAP_CALIBRATION_HINT = BIT(4),
	COMPASS_CAP_ACCURACY = BIT(5),
	COMPASS_CAP_RATE_OVERRIDE = BIT(6),
	COMPASS_CAP_RAW_CALIBRATION = BIT(7),
};

#ifdef __cplusplus
}
#endif

#endif /* MESHBUS_INCLUDE_DRIVERS_SENSOR_COMPASS_H_ */
