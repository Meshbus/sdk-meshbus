/*
 * Copyright (c) 2025 FoBE Projects
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Meshbus Radio API
 *
 * Meshbus radio provides an asynchronous TX/RX interface via ZBus and a persisted configuration
 * interface for the underlying radio driver.
 */

#ifndef MESHBUS_INCLUDE_RADIO_H_
#define MESHBUS_INCLUDE_RADIO_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_MBS_RADIO_STATS
#include <zephyr/stats/stats.h>
#endif
#include <zephyr/zbus/zbus.h>

#include "meshbus/radio.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum radio payload length in bytes. */
#define MBS_RADIO_MAX_PAYLOAD 255

/**
 * @brief Radio runtime configuration (maps to meshbus_RadioConfig).
 */
typedef meshbus_RadioConfig mbs_radio_config;

/**
 * @brief Radio runtime state.
 */
enum mbs_radio_state {
	MBS_RADIO_STATE_IDLE = 0, /**< Radio is idle */
	MBS_RADIO_STATE_RECEIVE,  /**< Radio is in receive mode */
	MBS_RADIO_STATE_TRANSMIT, /**< Radio is transmitting */
};

/**
 * @brief Radio lifecycle/state event.
 *
 * Published when the radio service changes its enabled flag, receive-only
 * flag, or runtime operating state.
 */
struct mbs_radio_state_event {
	/** True when radio runtime is enabled and available for MeshCore traffic. */
	bool enabled;
	/** True when TX is disabled but RX may remain active. */
	bool receive_only;
	/** Current radio runtime operating state. */
	enum mbs_radio_state state;
	/** Monotonic event sequence assigned by the radio service. */
	uint32_t sequence;
};

/**
 * @brief Cached radio runtime status.
 *
 * This snapshot never performs radio-driver I/O. Signal values reflect the
 * most recent receive callback or instantaneous RSSI sample collected by the
 * radio service.
 */
struct mbs_radio_status {
	/** Current radio runtime state. */
	enum mbs_radio_state state;
	/** Best-effort indication derived from a recent cached RSSI sample. */
	bool receiving;
	/** RSSI of the most recently received packet, in dBm. */
	int16_t last_rssi_dbm;
	/** SNR of the most recently received packet, in Q4 format. */
	int8_t last_snr_q4;
	/** Most recently calibrated noise floor, in dBm, or 0 if unavailable. */
	int16_t noise_floor_dbm;
	/** True when @ref rssi_inst_dbm contains a recent cached sample. */
	bool has_rssi_inst_dbm;
	/** Most recent instantaneous RSSI sample, in dBm. */
	int16_t rssi_inst_dbm;
};

/**
 * @brief RX event published when a packet is received.
 *
 * Published to mbs_radio_receive_chan when a packet is received.
 * Subscribers (e.g., meshcore) can use ZBUS_MSG_SUBSCRIBER to receive copies.
 */
struct mbs_radio_receive_event {
	/** Received data buffer */
	uint8_t data[MBS_RADIO_MAX_PAYLOAD];
	/** Length of received data in bytes */
	uint16_t len;
	/** RSSI of received packet in dBm */
	int16_t rssi;
	/** SNR of received packet in dB (scaled by 4 for precision) */
	int8_t snr;
};

/**
 * @brief TX request event published to request asynchronous transmit.
 *
 * Published to mbs_radio_publish_chan to request asynchronous TX.
 */
struct mbs_radio_publish_event {
	/** Data buffer to transmit */
	uint8_t data[MBS_RADIO_MAX_PAYLOAD];
	/** Length of data in bytes */
	uint16_t len;
};

/**
 * @brief TX completion event published after asynchronous transmit finishes.
 *
 * `status` is 0 on successful transmit or a negative errno-style value on
 * failure.
 */
struct mbs_radio_tx_done_event {
	/** Completion status for the transmit request. */
	int status;
};

/** Cached service health. Driver failures are distinct from policy rejection.
 * tx_failures counts consecutive actual driver failures in a 60-second window.
 * A successful TX only clears TX health; RX success must be independently proven.
 */
struct mbs_radio_health_event {
	bool ready;
	bool rx_failed;
	bool tx_failed;
};
ZBUS_CHAN_DECLARE(mbs_radio_health_chan);
int mbs_radio_health_get(struct mbs_radio_health_event *health);

/**
 * @brief Continuous-wave transmit request.
 *
 * Published to mbs_radio_cw_chan to request an asynchronous
 * continuous-wave transmission.
 */
struct mbs_radio_cw_request_event {
	/** RF frequency in Hz. */
	uint32_t frequency_hz;
	/** Transmission duration in seconds. */
	uint16_t duration_s;
	/** Transmit power in dBm. */
	int8_t tx_power_dbm;
};

/**
 * @brief Continuous-wave completion event.
 *
 * Published after an asynchronously started continuous-wave transmission has
 * stopped and the radio service has attempted to restore its previous runtime
 * configuration.
 */
struct mbs_radio_cw_done_event {
	/** Completion status, or a negative errno-style value on failure. */
	int status;
	/** Monotonic completion sequence assigned by the radio service. */
	uint32_t sequence;
};

/** @brief ZBus channel used to publish RX events. */
ZBUS_CHAN_DECLARE(mbs_radio_receive_chan);
/** @brief ZBus channel used to publish TX requests. */
ZBUS_CHAN_DECLARE(mbs_radio_publish_chan);
/** @brief ZBus channel used to publish TX completion events. */
ZBUS_CHAN_DECLARE(mbs_radio_tx_done_chan);
/** @brief ZBus channel used to publish continuous-wave transmit requests. */
ZBUS_CHAN_DECLARE(mbs_radio_cw_chan);
/** @brief ZBus channel used to publish continuous-wave completion events. */
ZBUS_CHAN_DECLARE(mbs_radio_cw_done_chan);
/** @brief ZBus channel used to publish radio lifecycle/state events. */
ZBUS_CHAN_DECLARE(mbs_radio_state_chan);

#ifdef CONFIG_MBS_RADIO_STATS
/** @brief Radio statistics counters. */
STATS_SECT_START(mbs_radio_stats)
STATS_SECT_ENTRY32(packets_recv)       /** Number of packets successfully received */
STATS_SECT_ENTRY32(packets_sent)       /** Number of packets successfully sent */
STATS_SECT_ENTRY32(send_failures)      /** Number of send failures */
STATS_SECT_ENTRY32(recv_errors)        /** Number of receive errors */
STATS_SECT_ENTRY32(tx_air_time_ms)     /** Total TX air time used in milliseconds */
STATS_SECT_ENTRY32(rx_air_time_ms)     /** Total RX air time used in milliseconds */
STATS_SECT_ENTRY32(noise_calibrations) /** Noise floor calibration count */
STATS_SECT_END;

STATS_NAME_START(mbs_radio_stats)
STATS_NAME(mbs_radio_stats, packets_recv)
STATS_NAME(mbs_radio_stats, packets_sent)
STATS_NAME(mbs_radio_stats, send_failures)
STATS_NAME(mbs_radio_stats, recv_errors)
STATS_NAME(mbs_radio_stats, tx_air_time_ms)
STATS_NAME(mbs_radio_stats, rx_air_time_ms)
STATS_NAME(mbs_radio_stats, noise_calibrations)
STATS_NAME_END(mbs_radio_stats);
extern STATS_SECT_DECL(mbs_radio_stats) mbs_radio_stats;
#endif

/**
 * @brief Get current radio configuration.
 *
 * Copies the current active configuration to the provided buffer.
 * Thread-safe.
 *
 * @param[out] cfg Pointer to configuration structure to fill.
 * @return 0 on success, -EINVAL if cfg is NULL.
 */
int mbs_radio_config_get(mbs_radio_config *cfg);

/**
 * @brief Set a new radio configuration.
 *
 * Validates the configuration, applies it to hardware, and schedules
 * async persistence to NVS. Thread-safe.
 *
 * If the new configuration matches the current one, no action is taken.
 *
 * @param cfg Pointer to new configuration.
 * @return 0 on success, negative error code on failure:
 *         -EINVAL: Invalid parameter or validation failed
 *         -ENODEV: Radio hardware not ready
 *         -EBUSY: Radio is currently transmitting
 */
int mbs_radio_config_set(const mbs_radio_config *cfg);

/**
 * @brief Reset radio configuration to defaults.
 *
 * Applies default configuration to hardware and deletes saved
 * configuration from NVS. Thread-safe.
 *
 * @return 0 on success, negative error code on failure.
 */
int mbs_radio_config_reset(void);

/**
 * @brief Get current radio runtime state.
 *
 * The state reflects the current operating mode managed by the meshbus radio
 * service.
 *
 * Use mbs_radio_channel_activity() for a best-effort indication of current
 * channel activity/packet reception.
 *
 * @return Current radio state
 */
enum mbs_radio_state mbs_radio_state_get(void);

/**
 * @brief Copy the current cached radio runtime status.
 *
 * The call is bounded to in-memory locking and does not access the radio
 * driver, so it is safe for management and UI status paths.
 *
 * @param[out] status Destination status snapshot.
 * @return 0 on success, or -EINVAL when @p status is NULL.
 */
int mbs_radio_status_get(struct mbs_radio_status *status);

/**
 * @brief Check if radio is currently receiving a packet.
 *
 * Combines preamble detection and channel activity check.
 *
 * @return true if radio is mid-receive of a packet or channel is active.
 */
bool mbs_radio_channel_activity(void);

/**
 * @brief Get RSSI of last received packet.
 *
 * @return RSSI value in dBm.
 */
int16_t mbs_radio_last_rssi(void);

/**
 * @brief Get SNR of last received packet.
 *
 * @return SNR value in dB (scaled by 4 for precision, divide by 4.0 for actual).
 */
int8_t mbs_radio_last_snr(void);

/**
 * @brief Get the current noise floor estimate.
 *
 * @return Noise floor in dBm, or 0 if not yet calibrated.
 */
int16_t mbs_radio_noise_floor(void);

/**
 * @brief Trigger noise floor calibration.
 *
 * Starts collecting RSSI samples to compute the noise floor.
 * Results are available via mbs_radio_noise_floor() after
 * MBS_RADIO_NOISE_FLOOR_SAMPLES samples are collected.
 *
 * @param threshold Threshold above noise floor for channel activity detection
 */
void mbs_radio_noise_calibrate(int16_t threshold);

/**
 * @brief Reset the AGC (Automatic Gain Control).
 *
 * Forces the radio to recalibrate its AGC by transitioning
 * back to receive mode. Should not be called mid-receive.
 */
void mbs_radio_agc_reset(void);

/**
 * @brief Get instantaneous RSSI.
 *
 * Returns the current channel energy level. Should be called
 * while in receive mode for meaningful results.
 *
 * @param[out] rssi Pointer to store RSSI value in dBm.
 * @return 0 on success, negative error code on failure.
 */
int mbs_radio_rssi_inst(int16_t *rssi);

/**
 * @brief Calculate estimated air time for a packet.
 *
 * Uses the current radio configuration to estimate transmission time.
 *
 * @param len Payload length in bytes.
 * @return Estimated air time in milliseconds, or 0 when len is invalid or
 *         radio hardware is unavailable
 */
uint32_t mbs_radio_airtime(uint16_t len);

/**
 * @brief Calculate packet reception quality score.
 *
 * Computes a score (0.0 - 1.0) based on SNR and packet length.
 * Used by meshcore for retransmission delay calculation.
 *
 * The score considers:
 * - SNR threshold for current spreading factor
 * - Collision penalty based on packet length
 *
 * @param snr Signal-to-noise ratio in dB
 * @param len Packet length in bytes
 * @return Quality score from 0.0 (poor) to 1.0 (excellent)
 */
float mbs_radio_packet_score(float snr, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* MESHBUS_INCLUDE_RADIO_H_ */
