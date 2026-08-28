/*
 * Copyright (c) 2025 FoBE Projects
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Meshbus GNSS API
 *
 * Meshbus GNSS provides a simple configuration interface and publishes GNSS updates via ZBus.
 */

#ifndef ZEPHYR_INCLUDE_MESHBUS_GNSS_H_
#define ZEPHYR_INCLUDE_MESHBUS_GNSS_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/gnss.h>
#ifdef CONFIG_MESHBUS_GNSS_STATS
#include <zephyr/stats/stats.h>
#endif
#include <zephyr/zbus/zbus.h>

#include "meshbus/gnss.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief GNSS runtime configuration (maps to meshbus_GnssConfig).
 */
typedef meshbus_GnssConfig meshbus_gnss_config;

/**
 * @brief GNSS runtime state.
 */
enum meshbus_gnss_state {
	MESHBUS_GNSS_STATE_SLEEP = 0,     /**< GNSS is in sleep */
	MESHBUS_GNSS_STATE_ACQUIRING = 1, /**< GNSS is in acquisition */
	MESHBUS_GNSS_STATE_TRACK = 2,     /**< GNSS is tracking */
	MESHBUS_GNSS_STATE_ERROR = 3,     /**< GNSS hardware/config state is degraded */
};

/** Effective source used by the page-scoped Compass widget heading runtime. */
enum meshbus_gnss_heading_source {
	MESHBUS_GNSS_HEADING_SOURCE_UNSPECIFIED = 0,
	MESHBUS_GNSS_HEADING_SOURCE_ELECTRONIC = 1,
	MESHBUS_GNSS_HEADING_SOURCE_COURSE = 2,
};

/** Current page-scoped heading runtime state. */
enum meshbus_gnss_heading_state {
	MESHBUS_GNSS_HEADING_STATE_UNAVAILABLE = 0,
	MESHBUS_GNSS_HEADING_STATE_IDLE = 1,
	MESHBUS_GNSS_HEADING_STATE_STARTING = 2,
	MESHBUS_GNSS_HEADING_STATE_READY = 3,
	MESHBUS_GNSS_HEADING_STATE_WAITING_FOR_FIX = 4,
	MESHBUS_GNSS_HEADING_STATE_WAITING_FOR_MOTION = 5,
	MESHBUS_GNSS_HEADING_STATE_STALE = 6,
	MESHBUS_GNSS_HEADING_STATE_SOURCE_DISABLED = 7,
	MESHBUS_GNSS_HEADING_STATE_ERROR = 8,
};

/** Source-specific heading quality. */
enum meshbus_gnss_heading_accuracy {
	MESHBUS_GNSS_HEADING_ACCURACY_UNRELIABLE = 0,
	MESHBUS_GNSS_HEADING_ACCURACY_LOW = 1,
	MESHBUS_GNSS_HEADING_ACCURACY_MEDIUM = 2,
	MESHBUS_GNSS_HEADING_ACCURACY_HIGH = 3,
};

/** Optional electronic-Compass calibration movement hint. */
enum meshbus_gnss_heading_calibration_hint {
	MESHBUS_GNSS_HEADING_CALIBRATION_HINT_NONE = 0,
	MESHBUS_GNSS_HEADING_CALIBRATION_HINT_FIGURE_EIGHT = 1,
	MESHBUS_GNSS_HEADING_CALIBRATION_HINT_KEEP_LEVEL = 2,
};

/** Immutable heading snapshot copied by meshbus_gnss_heading_snapshot_get(). */
struct meshbus_gnss_heading_snapshot {
	/** Monotonic sequence incremented for every completed update. */
	uint32_t sequence;
	/** Source acquisition/fix uptime in milliseconds. */
	uint32_t source_timestamp_ms;
	/** Heading in milli-degrees, normalized to [0, 360000). */
	int32_t heading_milli_deg;
	/** Last provider error, or zero for non-error waiting states. */
	int32_t last_error;
	/** Effective source from enum meshbus_gnss_heading_source. */
	uint8_t source;
	/** Runtime state from enum meshbus_gnss_heading_state. */
	uint8_t state;
	/** Quality from enum meshbus_gnss_heading_accuracy. */
	uint8_t accuracy;
	/** Calibration guidance from enum meshbus_gnss_heading_calibration_hint. */
	uint8_t calibration_hint;
	/** True only when heading_milli_deg is currently usable. */
	bool valid;
};

/** Volatile Compass-widget heading lifecycle status. */
struct meshbus_gnss_heading_runtime_status {
	/** Common provider capability mask from enum compass_sensor_capability. */
	uint32_t capabilities;
	/** Active update/poll interval in milliseconds. */
	uint32_t sample_interval_ms;
	/** Number of Widget/runtime clients holding the source active. */
	uint16_t active_client_count;
	/** True when DTS configures an electronic Compass provider. */
	bool electronic_available;
	/** True while at least one active client lease is held. */
	bool active;
};

/**
 * @brief GNSS data event published on ZBus.
 *
 * Published to meshbus_gnss_data_chan when a valid position is acquired.
 * Subscribers can use ZBUS_MSG_SUBSCRIBER to receive copies.
 */
struct meshbus_gnss_data_event {
	/** Navigation data (latitude, longitude, altitude, speed, bearing) */
	struct navigation_data nav_data;
	/** GNSS info (satellites, HDOP, fix status/quality) */
	struct gnss_info info;
	/** UTC time when data was acquired */
	struct gnss_time utc;
	/** True if data is valid (at least one fix was obtained during sampling) */
	bool valid;
};

/** @brief ZBus channel used to publish GNSS data events. */
ZBUS_CHAN_DECLARE(meshbus_gnss_data_chan);

#ifdef CONFIG_MESHBUS_GNSS_STATS
/**
 * @brief GNSS statistics.
 *
 * Time-to-first-fix values are reported in milliseconds.
 */
STATS_SECT_START(meshbus_gnss_stats)
STATS_SECT_ENTRY32(ttff_last)
STATS_SECT_ENTRY32(ttff_min)
STATS_SECT_ENTRY32(ttff_max)
STATS_SECT_END;
STATS_NAME_START(meshbus_gnss_stats)
STATS_NAME(meshbus_gnss_stats, ttff_last)
STATS_NAME(meshbus_gnss_stats, ttff_min)
STATS_NAME(meshbus_gnss_stats, ttff_max)
STATS_NAME_END(meshbus_gnss_stats);
extern STATS_SECT_DECL(meshbus_gnss_stats) meshbus_gnss_stats;
#endif

/**
 * @brief Get current GNSS configuration.
 *
 * @param[out] cfg Configuration buffer.
 * @return 0 on success, -EINVAL if cfg is NULL.
 */
int meshbus_gnss_config_get(meshbus_gnss_config *cfg);

/**
 * @brief Set a new GNSS configuration.
 *
 * Validates, applies to hardware, and persists to NVS.
 *
 * This exported ABI-v1 entry updates the original GNSS fields through
 * @c time_sync and preserves the current electronic-Compass preference. This
 * prevents extensions built before that preference existed from exposing
 * uninitialized tail padding as configuration. Local protobuf/UI control paths
 * use a firmware-private full setter when changing the preference.
 *
 * @param cfg New configuration.
 * @return 0 on success, -EINVAL on validation failure, -ENODEV if not ready,
 *         or -ESHUTDOWN after terminal power quiescing has started.
 */
int meshbus_gnss_config_set(const meshbus_gnss_config *cfg);

/**
 * @brief Reset GNSS configuration to defaults and delete persisted settings.
 *
 * @return 0 on success, negative errno on failure.
 */
int meshbus_gnss_config_reset(void);

/**
 * @brief Get current GNSS runtime state.
 *
 * The state reflects the current operating mode managed by the meshbus GNSS
 * service.
 *
 * @return Current GNSS state
 */
enum meshbus_gnss_state meshbus_gnss_state_get(void);

/**
 * @brief Trigger an immediate GNSS acquisition cycle.
 *
 * @return 0 on success, -EBUSY if sampling is in progress, -EACCES when GNSS
 *         is disabled, or -ENODEV when its device is unavailable.
 */
int meshbus_gnss_acquisition(void);

/**
 * @brief Atomically copy the latest valid GNSS fix and its receive uptime.
 *
 * Unlike the periodic GNSS ZBus publication, this getter exposes the latest
 * driver callback sample. Consumers can reject stale course-over-ground data
 * without treating a retained cached fix as fresh.
 *
 * @param[out] event Latest navigation, fix metadata, and UTC sample.
 * @param[out] source_timestamp_ms Uptime when the driver fix was received.
 * @return 0 on success, -EINVAL for NULL output, or -ENODATA before a fix.
 */
int meshbus_gnss_fix_snapshot_get(struct meshbus_gnss_data_event *event,
				  uint32_t *source_timestamp_ms);

/**
 * @brief Acquire one Compass-widget heading runtime lease.
 *
 * The first client starts the configured electronic provider or GNSS course
 * polling. Electronic hardware is powered only while a lease is held.
 *
 * @return 0 on success, -EOVERFLOW when the reference count is full,
 *         -ESHUTDOWN after terminal power quiescing has started, or a source
 *         acquisition/provider error.
 */
int meshbus_gnss_heading_acquire(void);

/**
 * @brief Release one Compass-widget heading runtime lease.
 *
 * A provider teardown error keeps the final lease quiesced and owned by the
 * caller so that release can be retried without orphaning a runtime-PM hold.
 *
 * @return 0 on success, -EALREADY when no lease is held, or a retryable
 *         provider error.
 */
int meshbus_gnss_heading_release(void);

/**
 * @brief Copy the latest immutable Compass-widget heading snapshot.
 *
 * @param[out] snapshot Destination snapshot.
 * @return 0 on success, or -EINVAL when @p snapshot is NULL.
 */
int meshbus_gnss_heading_snapshot_get(struct meshbus_gnss_heading_snapshot *snapshot);

/**
 * @brief Copy current Compass-widget heading runtime status.
 *
 * @param[out] status Destination status.
 * @return 0 on success, or -EINVAL when @p status is NULL.
 */
int meshbus_gnss_heading_runtime_status_get(
	struct meshbus_gnss_heading_runtime_status *status);

/**
 * @brief Reset live calibration state on the electronic heading provider.
 *
 * @return 0 on success, -ENOTSUP when no electronic provider is configured,
 *         -ESHUTDOWN after terminal power quiescing has started, or a provider
 *         error.
 */
int meshbus_gnss_heading_calibration_reset(void);

/**
 * @brief Get cached navigation data.
 *
 * Returns the most recent valid fix. During a new acquisition window (before
 * the next fix is ready), this may be data from the previous window.
 *
 * @param[out] nav Navigation data buffer.
 * @return 0 on success, -EINVAL if NULL, -ENODATA if no valid data.
 */
int meshbus_gnss_position_get(struct navigation_data *nav);

/**
 * @brief Get cached GNSS info.
 *
 * Returns metadata from the most recent valid fix.
 *
 * @param[out] info GNSS info buffer.
 * @return 0 on success, -EINVAL if NULL, -ENODATA if no valid data.
 */
int meshbus_gnss_info_get(struct gnss_info *info);

/**
 * @brief Get cached UTC time.
 *
 * Returns UTC from the most recent valid fix.
 *
 * @param[out] time GNSS time buffer.
 * @return 0 on success, -EINVAL if NULL, -ENODATA if no valid data.
 */
int meshbus_gnss_time_get(struct gnss_time *time);

/**
 * @brief Get cached satellite information
 *
 * Returns the cached satellite array containing satellites from all enabled
 * GNSS systems (GPS, GLONASS, Galileo, BeiDou, etc.). The satellites are
 * deduplicated by PRN and system type.
 *
 * @param[out] satellites Buffer to store satellite array (caller-provided)
 * @param[in] max_count Maximum number of satellites the buffer can hold
 * @param[out] count Actual number of satellites copied to buffer
 * @return 0 on success, -EINVAL if NULL, -ENODATA if no satellite data.
 */
int meshbus_gnss_satellites_get(struct gnss_satellite *satellites, uint16_t max_count,
				uint16_t *count);

/**
 * @brief Get one cached satellite entry by index.
 *
 * @param[in] index Satellite index in current cache snapshot.
 * @param[out] satellite Output satellite buffer.
 * @return 0 on success, -EINVAL if NULL, -ENODATA if index is out of range.
 */
int meshbus_gnss_satellite_get_by_index(uint16_t index, struct gnss_satellite *satellite);

/**
 * @brief Get the number of cached satellites.
 *
 * @return Number of satellites currently cached.
 */
uint16_t meshbus_gnss_satellites_count(void);

/**
 * @brief Clear the satellite cache.
 *
 * Clears all cached satellite data.
 */
void meshbus_gnss_satellites_cache_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MESHBUS_GNSS_H_ */
