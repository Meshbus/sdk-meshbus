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

#ifndef MESHBUS_INCLUDE_GNSS_H_
#define MESHBUS_INCLUDE_GNSS_H_

#include <gnss/heading.h>

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/gnss.h>
#ifdef CONFIG_MBS_GNSS_STATS
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
typedef meshbus_GnssConfig mbs_gnss_config;

/**
 * @brief GNSS runtime state.
 */
enum mbs_gnss_state {
	MBS_GNSS_STATE_SLEEP = 0,     /**< GNSS is in sleep */
	MBS_GNSS_STATE_ACQUIRING = 1, /**< GNSS is in acquisition */
	MBS_GNSS_STATE_TRACK = 2,     /**< GNSS is tracking */
	MBS_GNSS_STATE_ERROR = 3,     /**< GNSS hardware/config state is degraded */
};

/**
 * @brief GNSS data event published on ZBus.
 *
 * Published to mbs_gnss_data_chan when a valid position is acquired.
 * Subscribers can use ZBUS_MSG_SUBSCRIBER to receive copies.
 */
struct mbs_gnss_data_event {
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
ZBUS_CHAN_DECLARE(mbs_gnss_data_chan);

#ifdef CONFIG_MBS_GNSS_STATS
/**
 * @brief GNSS statistics.
 *
 * Time-to-first-fix values are reported in milliseconds.
 */
STATS_SECT_START(mbs_gnss_stats)
STATS_SECT_ENTRY32(ttff_last)
STATS_SECT_ENTRY32(ttff_min)
STATS_SECT_ENTRY32(ttff_max)
STATS_SECT_END;
STATS_NAME_START(mbs_gnss_stats)
STATS_NAME(mbs_gnss_stats, ttff_last)
STATS_NAME(mbs_gnss_stats, ttff_min)
STATS_NAME(mbs_gnss_stats, ttff_max)
STATS_NAME_END(mbs_gnss_stats);
extern STATS_SECT_DECL(mbs_gnss_stats) mbs_gnss_stats;
#endif

/**
 * @brief Get current GNSS configuration.
 *
 * @param[out] cfg Configuration buffer.
 * @return 0 on success, -EINVAL if cfg is NULL.
 */
int mbs_gnss_config_get(mbs_gnss_config *cfg);

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
int mbs_gnss_config_set(const mbs_gnss_config *cfg);

/**
 * @brief Reset GNSS configuration to defaults and delete persisted settings.
 *
 * @return 0 on success, negative errno on failure.
 */
int mbs_gnss_config_reset(void);

/**
 * @brief Get current GNSS runtime state.
 *
 * The state reflects the current operating mode managed by the meshbus GNSS
 * service.
 *
 * @return Current GNSS state
 */
enum mbs_gnss_state mbs_gnss_state_get(void);

/**
 * @brief Trigger an immediate GNSS acquisition cycle.
 *
 * @return 0 on success, -EBUSY if sampling is in progress, -EACCES when GNSS
 *         is disabled, or -ENODEV when its device is unavailable.
 */
int mbs_gnss_acquisition(void);

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
int mbs_gnss_fix_snapshot_get(struct mbs_gnss_data_event *event,
				  uint32_t *source_timestamp_ms);

/**
 * @brief Get cached navigation data.
 *
 * Returns the most recent valid fix. During a new acquisition window (before
 * the next fix is ready), this may be data from the previous window.
 *
 * @param[out] nav Navigation data buffer.
 * @return 0 on success, -EINVAL if NULL, -ENODATA if no valid data.
 */
int mbs_gnss_position_get(struct navigation_data *nav);

/**
 * @brief Get cached GNSS info.
 *
 * Returns metadata from the most recent valid fix.
 *
 * @param[out] info GNSS info buffer.
 * @return 0 on success, -EINVAL if NULL, -ENODATA if no valid data.
 */
int mbs_gnss_info_get(struct gnss_info *info);

/**
 * @brief Get cached UTC time.
 *
 * Returns UTC from the most recent valid fix.
 *
 * @param[out] time GNSS time buffer.
 * @return 0 on success, -EINVAL if NULL, -ENODATA if no valid data.
 */
int mbs_gnss_time_get(struct gnss_time *time);

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
int mbs_gnss_satellites_get(struct gnss_satellite *satellites, uint16_t max_count,
				uint16_t *count);

/**
 * @brief Get one cached satellite entry by index.
 *
 * @param[in] index Satellite index in current cache snapshot.
 * @param[out] satellite Output satellite buffer.
 * @return 0 on success, -EINVAL if NULL, -ENODATA if index is out of range.
 */
int mbs_gnss_satellite_get_by_index(uint16_t index, struct gnss_satellite *satellite);

/**
 * @brief Get the number of cached satellites.
 *
 * @return Number of satellites currently cached.
 */
uint16_t mbs_gnss_satellites_count(void);

/**
 * @brief Clear the satellite cache.
 *
 * Clears all cached satellite data.
 */
void mbs_gnss_satellites_cache_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* MESHBUS_INCLUDE_GNSS_H_ */
