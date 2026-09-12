/*
 * Copyright (c) 2025 FoBE Projects
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Meshbus Telemetry API
 *
 * Meshbus telemetry periodically samples a fixed devicetree map of common Zephyr sensor channels
 * and publishes readings via ZBus. Settings control the sampling policy only; they never replace
 * channel providers. Driver-private channels at and above @ref SENSOR_CHAN_ALL are not supported.
 */

#ifndef ZEPHYR_INCLUDE_MBS_TELEMETRY_H_
#define ZEPHYR_INCLUDE_MBS_TELEMETRY_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/zbus/zbus.h>

#include "meshbus/telemetry.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Telemetry runtime configuration (maps to meshbus_TelemetryConfig).
 */
typedef meshbus_TelemetryConfig mbs_telemetry_config;

/**
 * @brief Maximum number of sensor values returned by a telemetry channel.
 *
 * Most channels are scalar. Multi-axis channels (e.g. *_XYZ) may return 3 values,
 * and some quaternion-like channels may return 4.
 */
#define MBS_TELEMETRY_MAX_VALUES 4U

/**
 * @brief Telemetry data event published on ZBus.
 *
 * One event is published per devicetree binding (sensor, channel). The channel
 * rejects events for unbound or driver-private channels and events whose value
 * count does not match the channel shape.
 */
struct mbs_telemetry_data_event {
	/** System uptime in milliseconds when data was collected. */
	uint32_t timestamp;
	/** Common Zephyr sensor channel in the range [0, SENSOR_CHAN_ALL). */
	enum sensor_channel chan;
	/** Number of sensor values; must equal mbs_telemetry_channel_value_count(chan). */
	uint8_t value_count;
	/** Sensor values (val1 + val2 * 10^-6). */
	struct sensor_value values[MBS_TELEMETRY_MAX_VALUES];
};

/**
 * @brief Static telemetry binding information.
 *
 * A binding maps one common Zephyr sensor channel in the range
 * [0, SENSOR_CHAN_ALL) to one sensor device.
 *
 * Note: @ref sensor_name points to the device name string owned by the Zephyr device model.
 */
struct mbs_telemetry_binding {
	/** Sensor device name. */
	const char *sensor_name;
	/** Common Zephyr sensor channel in the range [0, SENSOR_CHAN_ALL). */
	enum sensor_channel chan;
};

/**
 * @brief Service-owned ZBus channel used to publish telemetry data events.
 *
 * Consumers may subscribe to or read this channel. Meshbus Telemetry is its
 * sole producer; the LLEXT bridge exposes it as subscribe-only.
 */
ZBUS_CHAN_DECLARE(mbs_telemetry_data_chan);

/**
 * @brief Get number of values expected for a given common sensor channel.
 *
 * For multi-axis channels (e.g. *_XYZ), this returns 3. For scalar channels, this returns 1.
 */
size_t mbs_telemetry_channel_value_count(enum sensor_channel chan);

/**
 * @brief Get current telemetry configuration.
 *
 * @param[out] cfg Configuration buffer.
 * @return 0 on success, -EINVAL if cfg is NULL.
 */
int mbs_telemetry_config_get(mbs_telemetry_config *cfg);

/**
 * @brief Set a new telemetry configuration.
 *
 * Validates, applies, and persists via Settings.
 *
 * @param cfg New configuration.
 * @return 0 on success, -EINVAL on validation failure.
 */
int mbs_telemetry_config_set(const mbs_telemetry_config *cfg);

/**
 * @brief Reset telemetry configuration to defaults.
 *
 * @return 0 on success, negative errno on failure.
 */
int mbs_telemetry_config_reset(void);

/**
 * @brief Trigger an immediate telemetry sample.
 *
 * Forces an immediate sample collection from all configured sensors,
 * bypassing the normal sample interval timing.
 *
 * @return 0 on success, -ENODEV if telemetry is disabled.
 */
int mbs_telemetry_sample_trigger(void);

/**
 * @brief Read a telemetry channel value (or values).
 *
 * For multi-axis channels (e.g. *_XYZ), @p val must point to an array with
 * at least mbs_telemetry_channel_value_count(chan) elements. For single
 * value channels, a single struct sensor_value is sufficient.
 *
 * @param[in] chan Common Zephyr sensor channel in the range [0, SENSOR_CHAN_ALL).
 * @param[out] val Pointer to a sensor_value (or array of values).
 * @return 0 on success, negative error code on failure.
 */
int mbs_telemetry_channel_get(enum sensor_channel chan, struct sensor_value *val);

/**
 * @brief Get number of devicetree telemetry bindings (sensor, channel pairs).
 *
 * @return Number of (sensor, channel) bindings configured via devicetree.
 */
size_t mbs_telemetry_bindings_count(void);

/**
 * @brief Get a devicetree telemetry binding by index.
 *
 * @param index Binding index in range [0, mbs_telemetry_bindings_count()).
 * @param[out] out Binding info.
 *
 * @return 0 on success, -EINVAL if out is NULL, -ENOENT if index is out of range.
 */
int mbs_telemetry_binding_get(size_t index, struct mbs_telemetry_binding *out);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MBS_TELEMETRY_H_ */
