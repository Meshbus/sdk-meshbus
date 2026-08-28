/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <strings.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/meshbus/clock.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/clock.h>

#include "common/shell.h"

#define CLOCK_HELP_ROOT         SHELL_HELP("Clock service control and configuration", NULL)
#define CLOCK_HELP_STATUS       SHELL_HELP("Clock service status", NULL)
#define CLOCK_HELP_TIME         SHELL_HELP("Show current device real-time clock", NULL)
#define CLOCK_HELP_CONFIG       SHELL_HELP("Clock configuration", NULL)
#define CLOCK_HELP_CONFIG_GET   SHELL_HELP("Show current configuration", NULL)
#define CLOCK_HELP_CONFIG_SET   SHELL_HELP("Set clock configuration",                     \
				       "<time_format: 12h|24h|0|1> <utc_offset_minutes>")
#define CLOCK_HELP_CONFIG_RESET SHELL_HELP("Reset clock configuration to defaults", NULL)

static const char *clock_time_format_to_str(uint32_t time_format)
{
	if (time_format == meshbus_ClockConfig_ClockTimeFormat_TIME_FORMAT_12H) {
		return "12h";
	}

	if (time_format == meshbus_ClockConfig_ClockTimeFormat_TIME_FORMAT_24H) {
		return "24h";
	}

	return "unknown";
}

static int shell_parse_time_format(const char *arg, uint32_t *time_format)
{
	if (arg == NULL || time_format == NULL) {
		return -EINVAL;
	}

	uint32_t val;
	if (mb_shell_parse_u32_arg(arg, &val) == 0) {
		if (val <= meshbus_ClockConfig_ClockTimeFormat_TIME_FORMAT_24H) {
			*time_format = val;
			return 0;
		}
		return -EINVAL;
	}

	if (strcasecmp(arg, "12h") == 0) {
		*time_format = meshbus_ClockConfig_ClockTimeFormat_TIME_FORMAT_12H;
		return 0;
	}

	if (strcasecmp(arg, "24h") == 0) {
		*time_format = meshbus_ClockConfig_ClockTimeFormat_TIME_FORMAT_24H;
		return 0;
	}

	return -EINVAL;
}

static int shell_timespec_to_unix_ms(const struct timespec *ts, uint64_t *unix_time_ms)
{
	if (ts == NULL || unix_time_ms == NULL || ts->tv_sec < 0 ||
	    ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000L) {
		return -EINVAL;
	}

	*unix_time_ms = ((uint64_t)ts->tv_sec * 1000ULL) + (uint64_t)(ts->tv_nsec / 1000000L);
	return 0;
}

static int cmd_clock_config_get(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_clock_config cfg;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = meshbus_clock_config_get(&cfg);
	if (ret != 0) {
		mb_shell_error(sh, ret);
		return ret;
	}

	shell_print(sh, "Settings:");
	shell_print(sh, "  time_format:        %s (%u)", clock_time_format_to_str(cfg.time_format),
		    (unsigned int)cfg.time_format);
	shell_print(sh, "  utc_offset_minutes: %d", cfg.utc_offset_minutes);

	return 0;
}

static int cmd_clock_config_set(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_clock_config cfg;
	uint32_t time_format;
	int32_t utc_offset_minutes;
	int ret;

	ARG_UNUSED(argc);

	ret = meshbus_clock_config_get(&cfg);
	if (ret != 0) {
		mb_shell_error(sh, ret);
		return ret;
	}

	ret = shell_parse_time_format(argv[1], &time_format);
	if (ret != 0) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	ret = mb_shell_parse_i32_arg(argv[2], &utc_offset_minutes);
	if (ret != 0) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	cfg.time_format = (meshbus_ClockConfig_ClockTimeFormat)time_format;
	cfg.utc_offset_minutes = utc_offset_minutes;

	ret = meshbus_clock_config_set(&cfg);
	if (ret != 0) {
		mb_shell_error(sh, ret);
		return ret;
	}

	return cmd_clock_config_get(sh, 0, NULL);
}

static int cmd_clock_config_reset(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int ret = meshbus_clock_config_reset();
	if (ret != 0) {
		mb_shell_error(sh, ret);
		return ret;
	}

	return cmd_clock_config_get(sh, 0, NULL);
}

static int cmd_clock_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Runtime:");
	return cmd_clock_config_get(sh, 0, NULL);
}

static int cmd_clock_time(const struct shell *sh, size_t argc, char **argv)
{
	struct timespec ts;
	uint64_t unix_ms;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = sys_clock_gettime(SYS_CLOCK_REALTIME, &ts);
	if (ret != 0) {
		mb_shell_error(sh, ret);
		return ret;
	}

	ret = shell_timespec_to_unix_ms(&ts, &unix_ms);
	if (ret != 0) {
		mb_shell_error(sh, ret);
		return ret;
	}

	shell_print(sh, "Device time:");
	shell_print(sh, "  unix_ms:   %llu", (unsigned long long)unix_ms);
	shell_print(sh, "  unix_s:    %llu", (unsigned long long)ts.tv_sec);
	shell_print(sh, "  nsec:      %ld", (long)ts.tv_nsec);
	shell_print(sh, "  uptime_ms: %lld", (long long)k_uptime_get());
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(meshbus_clock_config_subcmds,
	SHELL_CMD_ARG(get, NULL, CLOCK_HELP_CONFIG_GET, cmd_clock_config_get, 1, 0),
	SHELL_CMD_ARG(set, NULL, CLOCK_HELP_CONFIG_SET, cmd_clock_config_set, 3, 0),
	SHELL_CMD_ARG(reset, NULL, CLOCK_HELP_CONFIG_RESET, cmd_clock_config_reset, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_SET_CREATE(meshbus_clock_subcmds, (meshbus, clock));

SHELL_SUBCMD_ADD((meshbus, clock), status, NULL, CLOCK_HELP_STATUS, cmd_clock_status, 1, 0);
SHELL_SUBCMD_ADD((meshbus, clock), time, NULL, CLOCK_HELP_TIME, cmd_clock_time, 1, 0);
SHELL_SUBCMD_ADD((meshbus, clock), config, &meshbus_clock_config_subcmds, CLOCK_HELP_CONFIG,
		 NULL, 0, 0);

SHELL_SUBCMD_ADD((meshbus), clock, &meshbus_clock_subcmds, CLOCK_HELP_ROOT, NULL, 0, 0);
