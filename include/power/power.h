/*
 * Copyright (c) 2025 FoBE Projects
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Meshbus Power API
 *
 * This module provides power management helpers and fuel gauge telemetry.
 * Power behavior is configured by @ref mbs_power_config and includes:
 * - timeout-based auto shutdown after six consecutive low battery samples
 *   (soc=0 and not charging),
 * - timeout-based auto shutdown when external power is lost (if online state is available),
 * - a reserved no-connection timeout field (persisted but not currently enforced).
 */

#ifndef ZEPHYR_INCLUDE_MBS_POWER_H_
#define ZEPHYR_INCLUDE_MBS_POWER_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/iterable_sections.h>
#include <zephyr/zbus/zbus.h>

#include "meshbus/power.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Power runtime configuration (maps to meshbus_PowerConfig).
 *
 * Fields:
 * - @c low_voltage_shutdown_timeout (seconds, 0 disables),
 * - @c losing_power_shutdown_timeout (seconds, 0 disables),
 * - @c no_connection_shutdown_timeout (seconds, persisted only for now).
 */
typedef meshbus_PowerConfig mbs_power_config;

/**
 * @brief Power action types for shutdown/reboot hooks.
 */
enum mbs_power_action {
	/** System is shutting down */
	MBS_POWER_ACTION_SHUTDOWN,
	/** System is rebooting */
	MBS_POWER_ACTION_REBOOT,
};

/**
 * @brief Fuel gauge data event published via ZBus.
 *
 * Published when new fuel gauge data is available.
 */
struct mbs_power_fuel_gauge_data_event {
	/** Battery voltage in millivolts */
	uint16_t voltage_mv;

	/** State of charge in percent (0-100) */
	uint8_t soc_percent;

	/** Battery temperature in 0.1 K (0 means unavailable) */
	uint16_t temperature_dk;

	/** Charging status */
	bool charging;

	/** Charger online status */
	bool online;
};

/** @brief ZBus channel used to publish fuel gauge data events. */
ZBUS_CHAN_DECLARE(mbs_power_fuel_gauge_data_chan);

/** @brief Callback signature for power action notifications. */
typedef void (*mbs_power_action_callback_t)(enum mbs_power_action action, void *user_data);

/** @brief Callback registration structure for power action notifications. */
struct mbs_power_action_callback {
	/** Hook callback function */
	mbs_power_action_callback_t callback;
	/** User-provided data passed to handler */
	void *user_data;
};

/**
 * @brief Register a power action callback.
 *
 * @param _callback Callback function.
 * @param _user_data User data passed to callback.
 */
#define MBS_POWER_ACTION_CALLBACK_DEFINE(_callback, _user_data)                                \
	STRUCT_SECTION_ITERABLE(mbs_power_action_callback,                                     \
				_mbs_power_action_callback__##_callback) = {                   \
		.callback = (_callback),                                                           \
		.user_data = (_user_data),                                                         \
	}

/**
 * @brief Get power configuration
 *
 * @param cfg Pointer to configuration structure to fill.
 * @return 0 on success, negative errno on failure.
 */
int mbs_power_config_get(mbs_power_config *cfg);

/**
 * @brief Set power configuration
 *
 * Validates and applies the configuration immediately.
 * Configuration is saved to flash after a delay.
 *
 * @param cfg Pointer to configuration to apply.
 * @return 0 on success, negative errno on failure.
 */
int mbs_power_config_set(const mbs_power_config *cfg);

/**
 * @brief Reset power configuration to defaults
 *
 * @return 0 on success, negative errno on failure.
 */
int mbs_power_config_reset(void);

/**
 * @brief Check if battery is currently charging
 *
 * @return true if battery is charging, false otherwise.
 */
bool mbs_power_is_charging(void);

/**
 * @brief Check if charger is online
 *
 * @return true if charger is online, false otherwise.
 */
bool mbs_power_is_online(void);

/**
 * @brief Get fuel gauge data
 *
 * Returns the cached fuel-gauge sample. If no sample has been cached yet, the
 * service may synchronously collect the initial sample.
 *
 * @param voltage_mv Pointer to store battery voltage in millivolts (can be NULL)
 * @param soc_percent Pointer to store state of charge in percent (can be NULL)
 * @param temperature_dk Pointer to store battery temperature in 0.1 K (can be NULL).
 * The value 0 means temperature is unavailable.
 * @return 0 on success, negative errno on failure (e.g., -ENODATA if no battery data).
 */
int mbs_power_fuel_gauge_get(uint16_t *voltage_mv, uint8_t *soc_percent,
				 uint16_t *temperature_dk);

/**
 * @brief System Shutdown
 *
 * System will shutdown and can only be woken by configured wake sources
 * (e.g., button press). After wake, system performs a complete restart.
 *
 * @return 0 on success, negative errno on failure.
 */
int mbs_power_shutdown(void);

/**
 * @brief System Reboot
 *
 * System will cold reboot.
 *
 * @return 0 on success, negative errno on failure.
 */
int mbs_power_reboot(void);

/**
 * @brief Reboot into bootloader recovery mode.
 *
 * This uses the configured board bootloader handoff. Supported handoffs include
 * Zephyr's retention boot-mode request and nRF GPREGRET bootloader magic.
 *
 * @retval 0 if the bootloader request was stored and reboot was requested.
 * @retval -ENOTSUP if no bootloader handoff support is enabled.
 * @retval -errno if the bootloader request could not be stored.
 */
int mbs_power_reboot_to_bootloader(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MBS_POWER_H_ */
