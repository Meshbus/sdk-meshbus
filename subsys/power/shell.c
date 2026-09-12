/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <power/power.h>

#include "mbs_shell_internal.h"

#define POWER_HELP_ROOT         SHELL_HELP("Power module control and configuration", NULL)
#define POWER_HELP_STATUS       SHELL_HELP("Display power status", NULL)
#define POWER_HELP_SHUTDOWN     SHELL_HELP("Shutdown the system", NULL)
#define POWER_HELP_REBOOT       SHELL_HELP("Reboot the system", NULL)
#define POWER_HELP_BOOTLOADER   SHELL_HELP("Reboot into bootloader recovery", NULL)
#define POWER_HELP_CONFIG       SHELL_HELP("Power configuration", NULL)
#define POWER_HELP_CONFIG_GET   SHELL_HELP("Show current configuration", NULL)
#define POWER_HELP_CONFIG_SET                                                                  \
	SHELL_HELP("Set configuration",                                                         \
		   "<low_voltage_shutdown_timeout> <losing_power_shutdown_timeout> "          \
		   "<no_connection_shutdown_timeout>")
#define POWER_HELP_CONFIG_RESET SHELL_HELP("Reset configuration to defaults", NULL)

static int cmd_power_status(const struct shell *sh, size_t argc, char **argv)
{
	mbs_power_config cfg;
	uint16_t voltage_mv = 0U;
	uint16_t temperature_dk = 0U;
	uint8_t soc_percent = 0U;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = mbs_power_config_get(&cfg);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	ret = mbs_power_fuel_gauge_get(&voltage_mv, &soc_percent, &temperature_dk);
	if (ret != 0) {
		shell_warn(sh, "Fuel gauge data unavailable: %d", ret);
	}

	shell_print(sh, "Status:");
	shell_print(sh, "  low_voltage_shutdown_timeout:   %u s", cfg.low_voltage_shutdown_timeout);
	shell_print(sh, "  losing_power_shutdown_timeout:  %u s", cfg.losing_power_shutdown_timeout);
	shell_print(sh, "  no_connection_shutdown_timeout: %u s",
		    cfg.no_connection_shutdown_timeout);
	shell_print(sh, "Battery:");
	shell_print(sh, "  voltage: %u mV", voltage_mv);
	shell_print(sh, "  soc:     %u%%", soc_percent);
	if (temperature_dk == 0U) {
		shell_print(sh, "  temp:    NC");
	} else {
		int32_t temp_c_x10 = (int32_t)temperature_dk - 2731;
		int32_t temp_c_abs_x10 = (temp_c_x10 < 0) ? -temp_c_x10 : temp_c_x10;
		shell_print(sh, "  temp:    %u dK (%s%u.%u C)", temperature_dk,
			    (temp_c_x10 < 0) ? "-" : "",
			    (uint32_t)temp_c_abs_x10 / 10U,
			    (uint32_t)temp_c_abs_x10 % 10U);
	}
	shell_print(sh, "  charging:%s", mbs_power_is_charging() ? " yes" : " no");
	shell_print(sh, "  online:  %s", mbs_power_is_online() ? " yes" : " no");

	return 0;
}

static int cmd_power_shutdown(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_warn(sh, "System shutdown now...");
	k_sleep(K_MSEC(100));

	ret = mbs_power_shutdown();
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	return 0;
}

static int cmd_power_reboot(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_warn(sh, "System reboot now...");
	k_sleep(K_MSEC(100));

	ret = mbs_power_reboot();
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	return 0;
}

static int cmd_power_bootloader(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_warn(sh, "System reboot into bootloader recovery now...");
	k_sleep(K_MSEC(100));

	ret = mbs_power_reboot_to_bootloader();
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	return 0;
}

static int cmd_power_config_get(const struct shell *sh, size_t argc, char **argv)
{
	mbs_power_config cfg;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = mbs_power_config_get(&cfg);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	shell_print(sh, "Settings:");
	shell_print(sh, "  low_voltage_shutdown_timeout:   %u s", cfg.low_voltage_shutdown_timeout);
	shell_print(sh, "  losing_power_shutdown_timeout:  %u s", cfg.losing_power_shutdown_timeout);
	shell_print(sh, "  no_connection_shutdown_timeout: %u s",
		    cfg.no_connection_shutdown_timeout);
	return 0;
}

static int cmd_power_config_set(const struct shell *sh, size_t argc, char **argv)
{
	mbs_power_config cfg;
	int ret;

	ARG_UNUSED(argc);

	ret = mbs_shell_parse_u32_arg(argv[1], &cfg.low_voltage_shutdown_timeout);
	ret |= mbs_shell_parse_u32_arg(argv[2], &cfg.losing_power_shutdown_timeout);
	ret |= mbs_shell_parse_u32_arg(argv[3], &cfg.no_connection_shutdown_timeout);
	if (ret != 0) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}

	ret = mbs_power_config_set(&cfg);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	return cmd_power_config_get(sh, 0, NULL);
}

static int cmd_power_config_reset(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = mbs_power_config_reset();
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	return cmd_power_config_get(sh, 0, NULL);
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	mbs_power_config_subcmds,
	SHELL_CMD_ARG(get, NULL, POWER_HELP_CONFIG_GET, cmd_power_config_get, 1, 0),
	SHELL_CMD_ARG(set, NULL, POWER_HELP_CONFIG_SET, cmd_power_config_set, 4, 0),
	SHELL_CMD_ARG(reset, NULL, POWER_HELP_CONFIG_RESET, cmd_power_config_reset, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_SET_CREATE(mbs_power_subcmds, (meshbus, power));

SHELL_SUBCMD_ADD((meshbus, power), status, NULL, POWER_HELP_STATUS, cmd_power_status, 1, 0);
SHELL_SUBCMD_ADD((meshbus, power), shutdown, NULL, POWER_HELP_SHUTDOWN, cmd_power_shutdown, 1, 0);
SHELL_SUBCMD_ADD((meshbus, power), reboot, NULL, POWER_HELP_REBOOT, cmd_power_reboot, 1, 0);
SHELL_SUBCMD_ADD((meshbus, power), bootloader, NULL, POWER_HELP_BOOTLOADER,
		 cmd_power_bootloader, 1, 0);
SHELL_SUBCMD_ADD((meshbus, power), config, &mbs_power_config_subcmds, POWER_HELP_CONFIG, NULL,
		 0, 0);

SHELL_SUBCMD_ADD((meshbus), power, &mbs_power_subcmds, POWER_HELP_ROOT, NULL, 0, 0);
