/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Meshbus Bluetooth Service API
 *
 * Meshbus Bluetooth service provides:
 * - Secure BLE advertising and connection policy (single connection, SC+MITM).
 * - MCUmgr transport via SMP over BLE (when enabled in the application).
 * - Persisted runtime configuration using Zephyr Settings and nanopb.
 */

#ifndef ZEPHYR_INCLUDE_MESHBUS_BLUETOOTH_H_
#define ZEPHYR_INCLUDE_MESHBUS_BLUETOOTH_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_MESHBUS_BLUETOOTH_STATS
#include <zephyr/stats/stats.h>
#endif
#include <zephyr/zbus/zbus.h>

#include "meshbus/bluetooth.pb.h"

struct bt_conn;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bluetooth runtime configuration (maps to meshbus_BluetoothConfig).
 */
typedef meshbus_BluetoothConfig meshbus_bluetooth_config;

/**
 * @brief Bluetooth passkey mode (protobuf enum).
 */
typedef meshbus_BluetoothConfig_BluetoothPasskeyMode meshbus_bluetooth_passkey_mode;

#define MESHBUS_BLUETOOTH_PASSKEY_MODE_RANDOM                                                     \
	meshbus_BluetoothConfig_BluetoothPasskeyMode_RANDOM
#define MESHBUS_BLUETOOTH_PASSKEY_MODE_FIXED                                                      \
	meshbus_BluetoothConfig_BluetoothPasskeyMode_FIXED

/**
 * @brief Bluetooth runtime state.
 */
enum meshbus_bluetooth_state {
	MESHBUS_BLUETOOTH_STATE_DISABLED = 0,     /**< Bluetooth is disabled */
	MESHBUS_BLUETOOTH_STATE_DISCONNECTED = 1, /**< Bluetooth is disconnected */
	MESHBUS_BLUETOOTH_STATE_PAIRING = 2,      /**< Bluetooth is in pairing mode */
	MESHBUS_BLUETOOTH_STATE_CONNECTED = 3,    /**< Bluetooth is connected */
};

/**
 * @brief Bluetooth pairing event.
 *
 * Sent when a pairing request is received or a passkey is displayed.
 */
struct meshbus_bluetooth_pairing_event {
	uint32_t passkey;
};

/**
 * @brief Bluetooth state event.
 *
 * Sent when the Bluetooth state changes.
 */
struct meshbus_bluetooth_state_event {
	enum meshbus_bluetooth_state state;
};

/** @brief Zbus channel for Bluetooth pairing requests. */
ZBUS_CHAN_DECLARE(meshbus_bluetooth_pairing_chan);
/** @brief Zbus channel for Bluetooth state notifications. */
ZBUS_CHAN_DECLARE(meshbus_bluetooth_state_chan);

#ifdef CONFIG_MESHBUS_BLUETOOTH_STATS
/**
 * @brief Meshbus Bluetooth GATT notification statistics.
 *
 * Attempts count notifications dequeued for GATT delivery. Drops include
 * rejected delivery attempts as well as queue and work-submission failures.
 */
STATS_SECT_START(meshbus_bluetooth_stats)
STATS_SECT_ENTRY32(notify_attempted) /** Notifications submitted for GATT delivery */
STATS_SECT_ENTRY32(notify_sent)      /** Notifications accepted by the GATT stack */
STATS_SECT_ENTRY32(notify_dropped)   /** Notifications dropped before or during delivery */
STATS_SECT_END;

STATS_NAME_START(meshbus_bluetooth_stats)
STATS_NAME(meshbus_bluetooth_stats, notify_attempted)
STATS_NAME(meshbus_bluetooth_stats, notify_sent)
STATS_NAME(meshbus_bluetooth_stats, notify_dropped)
STATS_NAME_END(meshbus_bluetooth_stats);
extern STATS_SECT_DECL(meshbus_bluetooth_stats) meshbus_bluetooth_stats;
#endif

/**
 * @brief Get current Bluetooth configuration.
 *
 * Copies the current active configuration to the provided buffer.
 *
 * @param[out] cfg Pointer to configuration structure to fill.
 * @return 0 on success, -EINVAL if cfg is NULL.
 */
int meshbus_bluetooth_config_get(meshbus_bluetooth_config *cfg);

/**
 * @brief Set a new Bluetooth configuration.
 *
 * Validates the configuration, applies it to runtime behavior, and schedules
 * async persistence to Settings.
 *
 * @param cfg Pointer to new configuration.
 * @return 0 on success, negative error code on failure.
 */
int meshbus_bluetooth_config_set(const meshbus_bluetooth_config *cfg);

/**
 * @brief Query effective MeshCore Companion availability.
 *
 * This reflects the active Bluetooth configuration and the compiled transport
 * support. It is intended for the MeshCore Companion transport to gate NUS
 * RX/TX while the static GATT service remains registered.
 *
 * @return true when MeshCore Companion responses are enabled.
 */
bool meshbus_bluetooth_meshcore_companion_enabled(void);

/**
 * @brief Check whether a Bluetooth connection may access trusted services.
 *
 * Authorization requires the connection to be the service's current active
 * connection and to have completed Bluetooth Security Mode 1 Level 4.
 *
 * @param conn Bluetooth connection to check. Ownership remains with the caller.
 * @return true when @p conn is the active L4 connection.
 */
bool meshbus_bluetooth_connection_is_authorized(const struct bt_conn *conn);

/**
 * @brief Reset Bluetooth configuration to defaults.
 *
 * Applies default configuration and deletes the persisted Settings record.
 *
 * @return 0 on success, negative error code on failure.
 */
int meshbus_bluetooth_config_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MESHBUS_BLUETOOTH_H_ */
