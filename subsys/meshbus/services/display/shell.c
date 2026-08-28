/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/meshbus/display.h>
#include <zephyr/shell/shell.h>

#include "common/shell.h"

#define DISPLAY_HELP_ROOT         SHELL_HELP("Display service control and configuration", NULL)
#define DISPLAY_HELP_STATUS       SHELL_HELP("Display service status", NULL)
#define DISPLAY_HELP_WAKE         SHELL_HELP("Wake display", NULL)
#define DISPLAY_HELP_SLEEP        SHELL_HELP("Put display to sleep", NULL)
#define DISPLAY_HELP_CONFIG       SHELL_HELP("Display configuration", NULL)
#define DISPLAY_HELP_CONFIG_GET   SHELL_HELP("Show current configuration", NULL)
#define DISPLAY_HELP_CONFIG_SET   SHELL_HELP("Set configuration",                                 \
				       "<brightness> <sleep_timeout> <invert>")
#define DISPLAY_HELP_CONFIG_RESET SHELL_HELP("Reset configuration to defaults", NULL)

static bool shell_display_device_ready(void)
{
#if DT_HAS_CHOSEN(zephyr_display)
	const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	return device_is_ready(dev);
#else
	return false;
#endif
}

static int cmd_display_config_get(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_display_config cfg;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = meshbus_display_config_get(&cfg);
	if (ret != 0) {
		mb_shell_error(sh, ret);
		return ret;
	}

	shell_print(sh, "Settings:");
	shell_print(sh, "  brightness:    %u", cfg.brightness);
	shell_print(sh, "  sleep_timeout: %u s", cfg.sleep_timeout);
	shell_print(sh, "  invert:        %s", cfg.invert ? "yes" : "no");

	return 0;
}

static int cmd_display_config_set(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_display_config cfg;
	int ret;

	ARG_UNUSED(argc);

	ret = meshbus_display_config_get(&cfg);
	if (ret != 0) {
		mb_shell_error(sh, ret);
		return ret;
	}

	ret = mb_shell_parse_u32_arg(argv[1], &cfg.brightness);
	ret |= mb_shell_parse_u32_arg(argv[2], &cfg.sleep_timeout);
	ret |= mb_shell_parse_bool_arg(argv[3], &cfg.invert);
	if (ret != 0) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	ret = meshbus_display_config_set(&cfg);
	if (ret != 0) {
		mb_shell_error(sh, ret);
		return ret;
	}

	return cmd_display_config_get(sh, 0, NULL);
}

static int cmd_display_config_reset(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int ret = meshbus_display_config_reset();
	if (ret != 0) {
		mb_shell_error(sh, ret);
		return ret;
	}

	return cmd_display_config_get(sh, 0, NULL);
}

static int cmd_display_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Runtime:");
	shell_print(sh, "  active:      %s", meshbus_display_is_active() ? "yes" : "no");
	shell_print(sh, "  device_ready:%s", shell_display_device_ready() ? " yes" : " no");

	return cmd_display_config_get(sh, 0, NULL);
}

static int cmd_display_wake(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	meshbus_display_active(true);
	shell_print(sh, "Display wake requested");
	return 0;
}

static int cmd_display_sleep(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	meshbus_display_active(false);
	shell_print(sh, "Display sleep requested");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(meshbus_display_config_subcmds,
	SHELL_CMD_ARG(get, NULL, DISPLAY_HELP_CONFIG_GET, cmd_display_config_get, 1, 0),
	SHELL_CMD_ARG(set, NULL, DISPLAY_HELP_CONFIG_SET, cmd_display_config_set, 4, 0),
	SHELL_CMD_ARG(reset, NULL, DISPLAY_HELP_CONFIG_RESET, cmd_display_config_reset, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_SET_CREATE(meshbus_display_subcmds, (meshbus, display));

SHELL_SUBCMD_ADD((meshbus, display), status, NULL, DISPLAY_HELP_STATUS, cmd_display_status, 1, 0);
SHELL_SUBCMD_ADD((meshbus, display), wake, NULL, DISPLAY_HELP_WAKE, cmd_display_wake, 1, 0);
SHELL_SUBCMD_ADD((meshbus, display), sleep, NULL, DISPLAY_HELP_SLEEP, cmd_display_sleep, 1, 0);
SHELL_SUBCMD_ADD((meshbus, display), config, &meshbus_display_config_subcmds, DISPLAY_HELP_CONFIG,
		 NULL, 0, 0);

SHELL_SUBCMD_ADD((meshbus), display, &meshbus_display_subcmds, DISPLAY_HELP_ROOT, NULL, 0, 0);
