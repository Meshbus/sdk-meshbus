/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Meshbus MeshCore low-level protocol API
 *
 * This module exposes MeshCore wire-level capabilities that do not belong to
 * the higher-level node, channel-store, or text-message services.
 */

#ifndef ZEPHYR_INCLUDE_MESHBUS_MESHCORE_H_
#define ZEPHYR_INCLUDE_MESHBUS_MESHCORE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef CONFIG_MESHBUS_MESHCORE_STATS
#include <zephyr/stats/stats.h>
#endif
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#include "meshbus/meshcore.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MeshCore runtime configuration (maps to meshbus_MeshcoreConfig).
 */
typedef meshbus_MeshcoreConfig meshbus_meshcore_config;

/** @brief MeshCore protocol role value. */
typedef uint8_t meshbus_meshcore_role;

/** @brief MeshCore loop-detect mode (protobuf enum). */
typedef meshbus_MeshcoreConfig_LoopDetect meshbus_meshcore_loop_detect;

/** @name MeshCore roles
 *  @{
 */
#define MESHBUS_MESHCORE_ROLE_CHAT     1U
#define MESHBUS_MESHCORE_ROLE_REPEATER 2U
#define MESHBUS_MESHCORE_ROLE_ROOM     3U
#define MESHBUS_MESHCORE_ROLE_SENSOR   4U
/** @} */



/** @name Loop detect modes (protobuf enum values)
 *  @{
 */
#define MESHBUS_MESHCORE_LOOP_DETECT_OFF meshbus_MeshcoreConfig_LoopDetect_LOOP_DETECT_OFF
#define MESHBUS_MESHCORE_LOOP_DETECT_MINIMAL \
	meshbus_MeshcoreConfig_LoopDetect_LOOP_DETECT_MINIMAL
#define MESHBUS_MESHCORE_LOOP_DETECT_MODERATE \
	meshbus_MeshcoreConfig_LoopDetect_LOOP_DETECT_MODERATE
#define MESHBUS_MESHCORE_LOOP_DETECT_STRICT \
	meshbus_MeshcoreConfig_LoopDetect_LOOP_DETECT_STRICT
/** @} */

/** @name Telemetry mode flags (protobuf enum values)
 *  @{
 */
#define MESHBUS_MESHCORE_TELEMETRY_MODE_FLAGS \
	meshbus_MeshcoreConfig_TelemetryMode_TELEMETRY_ALLOW_FLAGS
#define MESHBUS_MESHCORE_TELEMETRY_MODE_ALL \
	meshbus_MeshcoreConfig_TelemetryMode_TELEMETRY_ALLOW_ALL
/** @} */

#define MESHBUS_MESHCORE_PATH_MAX_LEN 64U
#define MESHBUS_MESHCORE_CHANNEL_DATA_PAYLOAD_MAX_LEN 165U
#define MESHBUS_MESHCORE_BINARY_REQUEST_PAYLOAD_MAX_LEN 163U
#define MESHBUS_MESHCORE_BINARY_RESPONSE_PAYLOAD_MAX_LEN 163U
/** Maximum anonymous payload accepted for transmission. */
#define MESHBUS_MESHCORE_ANON_DATA_PAYLOAD_MAX_LEN 136U
/** Maximum zero-padded anonymous payload delivered after AES decryption. */
#define MESHBUS_MESHCORE_ANON_DATA_RECEIVED_MAX_LEN 144U
#define MESHBUS_MESHCORE_RAW_DATA_PAYLOAD_MAX_LEN 184U
#define MESHBUS_MESHCORE_CONTROL_DATA_PAYLOAD_MAX_LEN 184U
#define MESHBUS_MESHCORE_OUT_PATH_UNKNOWN 0xFFU
#define MESHBUS_MESHCORE_CHANNEL_DATA_TYPE_RESERVED 0x0000U
#define MESHBUS_MESHCORE_CHANNEL_DATA_TYPE_DEV 0xFFFFU
#define MESHBUS_MESHCORE_PRIVATE_KEY_SIZE 64U
#define MESHBUS_MESHCORE_PUBLIC_KEY_SIZE 32U
#define MESHBUS_MESHCORE_PATH_HASH_SIZE_MAX 3U
#define MESHBUS_MESHCORE_NAME_MAX_LEN 32U

/** @name MeshCore route classifications surfaced to host services.
 *  @{
 */
#define MESHBUS_MESHCORE_ROUTE_UNSPECIFIED 0U
#define MESHBUS_MESHCORE_ROUTE_FLOOD       1U
#define MESHBUS_MESHCORE_ROUTE_DIRECT      2U
/** @} */

/** @name add_contact_config bit definitions
 *  @{
 */
#define MESHBUS_MESHCORE_CONTACT_ADD_FILTER_OVERWRITE_OLDEST 0b00000001U
#define MESHBUS_MESHCORE_CONTACT_ADD_FILTER_CHAT             0b00000010U
#define MESHBUS_MESHCORE_CONTACT_ADD_FILTER_REPEATER         0b00000100U
#define MESHBUS_MESHCORE_CONTACT_ADD_FILTER_ROOM             0b00001000U
#define MESHBUS_MESHCORE_CONTACT_ADD_FILTER_SENSOR           0b00010000U
#define MESHBUS_MESHCORE_CONTACT_ADD_FILTER_MANUAL_MODE      0b00100000U
/** @} */

/** @name node_discover filter bits
 *  @{
 */
#define MESHBUS_MESHCORE_DISCOVER_FILTER_CHAT     BIT(MESHBUS_MESHCORE_ROLE_CHAT)
#define MESHBUS_MESHCORE_DISCOVER_FILTER_REPEATER BIT(MESHBUS_MESHCORE_ROLE_REPEATER)
#define MESHBUS_MESHCORE_DISCOVER_FILTER_ROOM     BIT(MESHBUS_MESHCORE_ROLE_ROOM)
#define MESHBUS_MESHCORE_DISCOVER_FILTER_SENSOR   BIT(MESHBUS_MESHCORE_ROLE_SENSOR)
#define MESHBUS_MESHCORE_DISCOVER_FILTER_ALL \
	(MESHBUS_MESHCORE_DISCOVER_FILTER_CHAT | MESHBUS_MESHCORE_DISCOVER_FILTER_REPEATER | \
	 MESHBUS_MESHCORE_DISCOVER_FILTER_ROOM | MESHBUS_MESHCORE_DISCOVER_FILTER_SENSOR)
/** @} */

ZBUS_CHAN_DECLARE(meshbus_meshcore_advert_request_chan);
ZBUS_CHAN_DECLARE(meshbus_meshcore_node_discover_request_chan);
ZBUS_CHAN_DECLARE(meshbus_meshcore_trace_request_chan);
ZBUS_CHAN_DECLARE(meshbus_meshcore_trace_response_chan);

/** @brief MeshCore advert request event. */
typedef struct meshbus_meshcore_advert_request_event {
	bool flood;
} meshbus_meshcore_advert_request_event;

/** @brief MeshCore node-discover request event. */
typedef struct meshbus_meshcore_node_discover_request_event {
	/** Role filter bitmask using @ref MESHBUS_MESHCORE_DISCOVER_FILTER_CHAT, etc. */
	uint8_t filter;
	/** Responder last-modify cutoff timestamp in seconds; 0 disables cutoff. */
	uint32_t since;
	/** Request correlation tag generated by @ref meshbus_meshcore_node_discover_request. */
	uint32_t tag;
} meshbus_meshcore_node_discover_request_event;

/** @brief MeshCore explicit trace request event. */
typedef struct meshbus_meshcore_trace_request_event {
	/** Number of hash bytes per hop in @ref path; valid range is 1..3. */
	uint8_t path_hash_size;
	/** Number of valid encoded route bytes in @ref path. */
	uint8_t path_len;
	/** Full explicit trace route. Callers own route construction. */
	uint8_t path[MESHBUS_MESHCORE_PATH_MAX_LEN];
	/** Request correlation tag generated by @ref meshbus_meshcore_trace_request. */
	uint32_t tag;
} meshbus_meshcore_trace_request_event;

/** @brief MeshCore explicit trace response event. */
typedef struct meshbus_meshcore_trace_response_event {
	/** Request correlation tag echoed by the trace response. */
	uint32_t tag;
	/** Response timestamp in seconds. */
	uint32_t timestamp;
	/** Trace state code, reserved for future extensions. */
	uint8_t state;
	/** True when @ref response_snr carries the observed response SNR. */
	bool has_response_snr;
	/** Response SNR in MeshCore trace units. */
	int8_t response_snr;
	/** Number of valid forward path SNR entries in @ref out_path_snr. */
	uint8_t out_path_snr_count;
	/** Forward path SNR entries in MeshCore trace units. */
	int8_t out_path_snr[MESHBUS_MESHCORE_PATH_MAX_LEN];
	/** Number of valid return path SNR entries in @ref return_path_snr. */
	uint8_t return_path_snr_count;
	/** Return path SNR entries in MeshCore trace units. */
	int8_t return_path_snr[MESHBUS_MESHCORE_PATH_MAX_LEN];
} meshbus_meshcore_trace_response_event;

struct meshbus_meshcore_channel_data_send_request_event {
	uint8_t channel_index;
	uint8_t path_len;
	uint8_t path[MESHBUS_MESHCORE_PATH_MAX_LEN];
	uint16_t data_type;
	uint8_t payload_len;
	uint8_t payload[MESHBUS_MESHCORE_CHANNEL_DATA_PAYLOAD_MAX_LEN];
};

struct meshbus_meshcore_channel_data_response_event {
	uint8_t channel_index;
	uint8_t path_len;
	uint8_t path[MESHBUS_MESHCORE_PATH_MAX_LEN];
	uint16_t data_type;
	uint8_t payload_len;
	uint8_t payload[MESHBUS_MESHCORE_CHANNEL_DATA_PAYLOAD_MAX_LEN];
	bool has_rx_snr;
	int8_t rx_snr_q4;
};

struct meshbus_meshcore_raw_data_send_request_event {
	uint8_t path_len;
	uint8_t path[MESHBUS_MESHCORE_PATH_MAX_LEN];
	uint8_t payload_len;
	uint8_t payload[MESHBUS_MESHCORE_RAW_DATA_PAYLOAD_MAX_LEN];
};

struct meshbus_meshcore_raw_data_response_event {
	uint8_t path_len;
	uint8_t path[MESHBUS_MESHCORE_PATH_MAX_LEN];
	uint8_t payload_len;
	uint8_t payload[MESHBUS_MESHCORE_RAW_DATA_PAYLOAD_MAX_LEN];
	bool has_rx_snr;
	int8_t rx_snr_q4;
};

struct meshbus_meshcore_binary_request_event {
	uint8_t route;
	uint8_t public_key[MESHBUS_MESHCORE_PUBLIC_KEY_SIZE];
	uint32_t tag;
	uint8_t path_len;
	uint8_t path[MESHBUS_MESHCORE_PATH_MAX_LEN];
	uint8_t payload_len;
	uint8_t payload[MESHBUS_MESHCORE_BINARY_REQUEST_PAYLOAD_MAX_LEN];
	bool has_rx_snr;
	int8_t rx_snr_q4;
};

struct meshbus_meshcore_binary_response_send_request_event {
	uint8_t route;
	uint8_t public_key[MESHBUS_MESHCORE_PUBLIC_KEY_SIZE];
	uint32_t tag;
	uint8_t path_len;
	uint8_t path[MESHBUS_MESHCORE_PATH_MAX_LEN];
	uint8_t payload_len;
	uint8_t payload[MESHBUS_MESHCORE_BINARY_RESPONSE_PAYLOAD_MAX_LEN];
};

struct meshbus_meshcore_anon_data_send_request_event {
	uint8_t public_key[MESHBUS_MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t payload_len;
	uint8_t payload[MESHBUS_MESHCORE_ANON_DATA_PAYLOAD_MAX_LEN];
	/** Minimum dispatcher delay before radio transmission. */
	uint32_t delay_ms;
	/** Fail closed instead of using MeshCore flood fallback. */
	bool direct_only;
	/** Use the authenticated, caller-owned route below instead of Contact. */
	bool has_explicit_path;
	/** Number of valid bytes in @ref path; zero denotes a verified neighbor. */
	uint8_t path_byte_len;
	/** Hash width in bytes for each hop in @ref path. */
	uint8_t path_hash_size;
	/** Encoded intermediary-hop hashes for an explicit direct route. */
	uint8_t path[MESHBUS_MESHCORE_PATH_MAX_LEN];
};

struct meshbus_meshcore_anon_data_response_event {
	uint8_t route;
	uint8_t public_key[MESHBUS_MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t path_len;
	uint8_t path[MESHBUS_MESHCORE_PATH_MAX_LEN];
	uint8_t payload_len;
	uint8_t payload[MESHBUS_MESHCORE_ANON_DATA_RECEIVED_MAX_LEN];
	bool has_rx_snr;
	int8_t rx_snr_q4;
};

struct meshbus_meshcore_control_data_send_request_event {
	uint8_t payload_len;
	uint8_t payload[MESHBUS_MESHCORE_CONTROL_DATA_PAYLOAD_MAX_LEN];
};

struct meshbus_meshcore_control_data_response_event {
	uint8_t path_len;
	uint8_t path[MESHBUS_MESHCORE_PATH_MAX_LEN];
	uint8_t payload_len;
	uint8_t payload[MESHBUS_MESHCORE_CONTROL_DATA_PAYLOAD_MAX_LEN];
	bool has_rx_snr;
	int8_t rx_snr_q4;
};

ZBUS_CHAN_DECLARE(meshbus_meshcore_channel_data_send_request_chan);
ZBUS_CHAN_DECLARE(meshbus_meshcore_channel_data_response_chan);
ZBUS_CHAN_DECLARE(meshbus_meshcore_binary_request_chan);
ZBUS_CHAN_DECLARE(meshbus_meshcore_binary_response_send_request_chan);
ZBUS_CHAN_DECLARE(meshbus_meshcore_anon_data_send_request_chan);
ZBUS_CHAN_DECLARE(meshbus_meshcore_anon_data_response_chan);
ZBUS_CHAN_DECLARE(meshbus_meshcore_raw_data_send_request_chan);
ZBUS_CHAN_DECLARE(meshbus_meshcore_raw_data_response_chan);
ZBUS_CHAN_DECLARE(meshbus_meshcore_control_data_send_request_chan);
ZBUS_CHAN_DECLARE(meshbus_meshcore_control_data_response_chan);

#ifdef CONFIG_MESHBUS_MESHCORE_STATS
/**
 * @brief MeshCore request-admission statistics.
 *
 * These counters describe whether request-channel publications reached the
 * bounded MeshCore runtime queue. They do not describe later radio execution,
 * protocol completion, or response delivery.
 */
STATS_SECT_START(meshbus_meshcore_stats)
STATS_SECT_ENTRY32(requests_accepted)      /** Requests accepted by the runtime queue */
STATS_SECT_ENTRY32(requests_queue_full)    /** Requests rejected by queue or slab limits */
STATS_SECT_ENTRY32(requests_no_consumer)   /** Requests with no runtime consumer */
STATS_SECT_ENTRY32(requests_submit_errors) /** Other admission-layer submission failures */
STATS_SECT_END;

STATS_NAME_START(meshbus_meshcore_stats)
STATS_NAME(meshbus_meshcore_stats, requests_accepted)
STATS_NAME(meshbus_meshcore_stats, requests_queue_full)
STATS_NAME(meshbus_meshcore_stats, requests_no_consumer)
STATS_NAME(meshbus_meshcore_stats, requests_submit_errors)
STATS_NAME_END(meshbus_meshcore_stats);
extern STATS_SECT_DECL(meshbus_meshcore_stats) meshbus_meshcore_stats;
#endif

/**
 * @brief Set the MeshCore configuration.
 *
 * Apply settings before committing them and scheduling coalesced persistence.
 * Role or identity changes restart the protocol engine without rebooting the
 * device. The call waits for initialization; readers see the previous settings
 * until success. Failure leaves settings unchanged and restores the old engine.
 * Queued protocol work is discarded during restart and cannot be recovered.
 * If restoring the engine also fails, readiness is false; config_set may retry.
 * Invalid roles return -EINVAL; roles unsupported by compiled services return -ENOTSUP.
 * Concurrent changes return -EBUSY. A restart from an engine callback returns
 * -EWOULDBLOCK instead of waiting on its own queue. Call from thread context.
 *
 * @param cfg New configuration, copied for the duration of the call.
 * @retval 0 Configuration applied; persistence follows the shared settings delay.
 * @retval -ESHUTDOWN The service is already stopping for a power action.
 * @return Other negative errno on validation or runtime apply failure.
 */
int meshbus_meshcore_config_set(meshbus_meshcore_config *cfg);

/**
 * @brief Get the last successfully applied MeshCore settings.
 *
 * @param cfg Output parameter for current configuration.
 * @return 0 on success, negative errno on failure.
 */
int meshbus_meshcore_config_get(meshbus_meshcore_config *cfg);

/** @brief Get the currently active protocol role. */
meshbus_meshcore_role meshbus_meshcore_active_role_get(void);

/**
 * @brief Check whether the MeshCore runtime completed initialization.
 *
 * This becomes true only after the protocol core and backend bootstrap both
 * succeed. Radio availability remains a separate runtime condition.
 *
 * @return true when the MeshCore runtime is initialized.
 */
#if defined(CONFIG_MESHBUS_MESHCORE_RUNTIME)
bool meshbus_meshcore_runtime_is_ready(void);
#else
static inline bool meshbus_meshcore_runtime_is_ready(void)
{
	return false;
}
#endif

/**
 * @brief Restore MeshCore defaults and restart only the protocol runtime.
 *
 * Contacts and channels are preserved. With runtime support, a new identity is
 * generated and the engine rebuilt before committing defaults. Without runtime
 * support, identity stays unset until a runtime boot. The call waits for apply;
 * failure leaves configuration unchanged and attempts to restore the old engine.
 * Device, Desktop, and extensions keep running. Protocol queues are discarded.
 * Successful defaults use the same coalesced persistence as config_set.
 *
 * @retval 0 Defaults applied; persistence scheduled.
 * @retval -EBUSY Another settings operation is in progress.
 * @retval -ESHUTDOWN The service is already stopping for a power action.
 * @return Other negative errno on failure.
 */
int meshbus_meshcore_config_reset(void);

/**
 * @name Asynchronous request submission
 *
 * These calls serialize through one global blocking submission lock and must
 * run in thread context. A successful return means the runtime accepted the
 * request into its bounded queue; radio execution and protocol completion stay
 * asynchronous.
 * @{
 */

/**
 * @brief Request advert publish (async).
 *
 * @param flood true for flood advert, false for local advert.
 * @retval 0 Request accepted by the runtime queue.
 * @retval -ENOBUFS Runtime request queue is full.
 * @retval -ENODEV No runtime consumer accepted the request.
 * @return Other negative errno on validation or publication failure.
 */
int meshbus_meshcore_advert_request(bool flood);

/**
 * @brief Request zero-hop node discovery (async).
 *
 * @param filter Role filter bitmask using @ref MESHBUS_MESHCORE_DISCOVER_FILTER_CHAT, etc.
 * @param since Responder last-modify cutoff timestamp in seconds; 0 disables cutoff.
 * @param[out] out_tag Optional generated request tag, written only when the
 *             runtime accepts the request.
 * @retval 0 Request accepted by the runtime queue; completion remains
 *         asynchronous.
 * @retval -ENOBUFS Runtime request queue is full.
 * @retval -ENODEV No runtime consumer accepted the request.
 * @return Other negative errno on validation or publication failure.
 */
int meshbus_meshcore_node_discover_request(uint8_t filter, uint32_t since,
					   uint32_t *out_tag);

/**
 * @brief Request MeshCore TRACE over an explicit route path (async).
 *
 * @param path Explicit encoded route bytes. The route should include both the
 *        forward leg and the return leg expected by TRACE response parsing.
 * @param path_len Number of bytes in @p path.
 * @param path_hash_size Number of hash bytes per hop in @p path; valid range is 1..3.
 * @param[out] out_tag Optional generated request tag, written only when the
 *             runtime accepts the request.
 * @retval 0 Request accepted by the runtime queue; completion remains
 *         asynchronous.
 * @retval -ENOBUFS Runtime request queue is full.
 * @retval -ENODEV No runtime consumer accepted the request.
 * @return Other negative errno on validation or publication failure.
 */
int meshbus_meshcore_trace_request(const uint8_t *path, uint8_t path_len,
				   uint8_t path_hash_size, uint32_t *out_tag);

/**
 * @brief Queue channel data for asynchronous MeshCore transmission.
 *
 * @retval 0 Request accepted by the runtime queue.
 * @retval -ENOBUFS Runtime request queue is full.
 * @retval -ENODEV No runtime consumer accepted the request.
 * @return Other negative errno on validation or publication failure.
 */
int meshbus_meshcore_channel_data_send(size_t channel_index,
				       const uint8_t *path, uint8_t path_len,
				       uint16_t data_type,
				       const uint8_t *payload,
				       size_t payload_len);

/**
 * @brief Queue a binary response for asynchronous MeshCore transmission.
 *
 * @retval 0 Request accepted by the runtime queue.
 * @retval -ENOBUFS Runtime request queue is full.
 * @retval -ENODEV No runtime consumer accepted the request.
 * @return Other negative errno on validation or publication failure.
 */
int meshbus_meshcore_binary_response_send(
	const struct meshbus_meshcore_binary_response_send_request_event *request);

/**
 * @brief Queue anonymous data for asynchronous MeshCore transmission.
 *
 * @retval 0 Request accepted by the runtime queue.
 * @retval -ENOBUFS Runtime request queue is full.
 * @retval -ENODEV No runtime consumer accepted the request.
 * @return Other negative errno on validation or publication failure.
 */
int meshbus_meshcore_anon_data_send(const uint8_t public_key[MESHBUS_MESHCORE_PUBLIC_KEY_SIZE],
				    const uint8_t *payload, size_t payload_len);

/** Queue anonymous data with a radio turn-around delay. */
int meshbus_meshcore_anon_data_send_delayed(
	const uint8_t public_key[MESHBUS_MESHCORE_PUBLIC_KEY_SIZE],
	const uint8_t *payload, size_t payload_len, uint32_t delay_ms);

/** Queue anonymous data with atomic no-flood route policy. */
int meshbus_meshcore_anon_data_send_direct(
	const uint8_t public_key[MESHBUS_MESHCORE_PUBLIC_KEY_SIZE],
	const uint8_t *payload, size_t payload_len);

/** Queue delayed anonymous data with atomic no-flood route policy. */
int meshbus_meshcore_anon_data_send_direct_delayed(
	const uint8_t public_key[MESHBUS_MESHCORE_PUBLIC_KEY_SIZE],
	const uint8_t *payload, size_t payload_len, uint32_t delay_ms);

/** Queue anonymous data over an authenticated caller-owned direct path. */
int meshbus_meshcore_anon_data_send_via_path(
	const uint8_t public_key[MESHBUS_MESHCORE_PUBLIC_KEY_SIZE],
	const uint8_t *payload, size_t payload_len, const uint8_t *path,
	uint8_t path_byte_len, uint8_t path_hash_size);

/** Queue delayed anonymous data over an authenticated explicit path. */
int meshbus_meshcore_anon_data_send_via_path_delayed(
	const uint8_t public_key[MESHBUS_MESHCORE_PUBLIC_KEY_SIZE],
	const uint8_t *payload, size_t payload_len, const uint8_t *path,
	uint8_t path_byte_len, uint8_t path_hash_size, uint32_t delay_ms);

/**
 * @brief Queue raw data for asynchronous MeshCore transmission.
 *
 * @retval 0 Request accepted by the runtime queue.
 * @retval -ENOBUFS Runtime request queue is full.
 * @retval -ENODEV No runtime consumer accepted the request.
 * @return Other negative errno on validation or publication failure.
 */
int meshbus_meshcore_raw_data_send(const uint8_t *path, uint8_t path_len,
				   const uint8_t *payload, size_t payload_len);

/**
 * @brief Queue control data for asynchronous MeshCore transmission.
 *
 * @retval 0 Request accepted by the runtime queue.
 * @retval -ENOBUFS Runtime request queue is full.
 * @retval -ENODEV No runtime consumer accepted the request.
 * @return Other negative errno on validation or publication failure.
 */
int meshbus_meshcore_control_data_send(const uint8_t *payload,
				       size_t payload_len);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MESHBUS_MESHCORE_H_ */
