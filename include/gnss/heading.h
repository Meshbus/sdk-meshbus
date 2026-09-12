/* SPDX-License-Identifier: Apache-2.0 */

/* Copyright (c) 2026 FoBE Studio */

/**
 * @file
 * @brief Meshbus GNSS heading acquisition and snapshots
 */

#ifndef MESHBUS_INCLUDE_GNSS_HEADING_H_
#define MESHBUS_INCLUDE_GNSS_HEADING_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Effective source used by the page-scoped Compass widget heading runtime. */
enum mbs_gnss_heading_source {
	MBS_GNSS_HEADING_SOURCE_UNSPECIFIED = 0,
	MBS_GNSS_HEADING_SOURCE_ELECTRONIC = 1,
	MBS_GNSS_HEADING_SOURCE_COURSE = 2,
};

/** Current page-scoped heading runtime state. */
enum mbs_gnss_heading_state {
	MBS_GNSS_HEADING_STATE_UNAVAILABLE = 0,
	MBS_GNSS_HEADING_STATE_IDLE = 1,
	MBS_GNSS_HEADING_STATE_STARTING = 2,
	MBS_GNSS_HEADING_STATE_READY = 3,
	MBS_GNSS_HEADING_STATE_WAITING_FOR_FIX = 4,
	MBS_GNSS_HEADING_STATE_WAITING_FOR_MOTION = 5,
	MBS_GNSS_HEADING_STATE_STALE = 6,
	MBS_GNSS_HEADING_STATE_SOURCE_DISABLED = 7,
	MBS_GNSS_HEADING_STATE_ERROR = 8,
};

/** Source-specific heading quality. */
enum mbs_gnss_heading_accuracy {
	MBS_GNSS_HEADING_ACCURACY_UNRELIABLE = 0,
	MBS_GNSS_HEADING_ACCURACY_LOW = 1,
	MBS_GNSS_HEADING_ACCURACY_MEDIUM = 2,
	MBS_GNSS_HEADING_ACCURACY_HIGH = 3,
};

/** Optional electronic-Compass calibration movement hint. */
enum mbs_gnss_heading_calibration_hint {
	MBS_GNSS_HEADING_CALIBRATION_HINT_NONE = 0,
	MBS_GNSS_HEADING_CALIBRATION_HINT_FIGURE_EIGHT = 1,
	MBS_GNSS_HEADING_CALIBRATION_HINT_KEEP_LEVEL = 2,
};

/** Immutable heading snapshot copied by mbs_gnss_heading_snapshot_get(). */
struct mbs_gnss_heading_snapshot {
	/** Monotonic sequence incremented for every completed update. */
	uint32_t sequence;
	/** Source acquisition/fix uptime in milliseconds. */
	uint32_t source_timestamp_ms;
	/** Heading in milli-degrees, normalized to [0, 360000). */
	int32_t heading_milli_deg;
	/** Last provider error, or zero for non-error waiting states. */
	int32_t last_error;
	/** Effective source from enum mbs_gnss_heading_source. */
	uint8_t source;
	/** Runtime state from enum mbs_gnss_heading_state. */
	uint8_t state;
	/** Quality from enum mbs_gnss_heading_accuracy. */
	uint8_t accuracy;
	/** Calibration guidance from enum mbs_gnss_heading_calibration_hint. */
	uint8_t calibration_hint;
	/** True only when heading_milli_deg is currently usable. */
	bool valid;
};

/** Volatile Compass-widget heading lifecycle status. */
struct mbs_gnss_heading_runtime_status {
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
 * @brief Acquire one Compass-widget heading runtime lease.
 *
 * The first client starts the configured electronic provider or GNSS course
 * polling. Electronic hardware is powered only while a lease is held.
 *
 * @return 0 on success, -EOVERFLOW when the reference count is full,
 *         -ESHUTDOWN after terminal power quiescing has started, or a source
 *         acquisition/provider error.
 */
int mbs_gnss_heading_acquire(void);

/**
 * @brief Release one Compass-widget heading runtime lease.
 *
 * A provider teardown error keeps the final lease quiesced and owned by the
 * caller so that release can be retried without orphaning a runtime-PM hold.
 *
 * @return 0 on success, -EALREADY when no lease is held, or a retryable
 *         provider error.
 */
int mbs_gnss_heading_release(void);

/**
 * @brief Copy the latest immutable Compass-widget heading snapshot.
 *
 * @param[out] snapshot Destination snapshot.
 * @return 0 on success, or -EINVAL when @p snapshot is NULL.
 */
int mbs_gnss_heading_snapshot_get(struct mbs_gnss_heading_snapshot *snapshot);

/**
 * @brief Copy current Compass-widget heading runtime status.
 *
 * @param[out] status Destination status.
 * @return 0 on success, or -EINVAL when @p status is NULL.
 */
int mbs_gnss_heading_runtime_status_get(
	struct mbs_gnss_heading_runtime_status *status);

/**
 * @brief Reset live calibration state on the electronic heading provider.
 *
 * @return 0 on success, -ENOTSUP when no electronic provider is configured,
 *         -ESHUTDOWN after terminal power quiescing has started, or a provider
 *         error.
 */
int mbs_gnss_heading_calibration_reset(void);


#ifdef __cplusplus
}
#endif

#endif /* MESHBUS_INCLUDE_GNSS_HEADING_H_ */
