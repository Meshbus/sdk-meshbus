/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include <telemetry/telemetry.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <telemetry/telemetry.h>

#include "mbs_shell_internal.h"

#define TELEMETRY_HELP_ROOT         SHELL_HELP("Telemetry commands", NULL)
#define TELEMETRY_HELP_STATUS       SHELL_HELP("Show telemetry status", NULL)
#define TELEMETRY_HELP_ENABLE       SHELL_HELP("Enable telemetry", NULL)
#define TELEMETRY_HELP_DISABLE      SHELL_HELP("Disable telemetry", NULL)
#define TELEMETRY_HELP_READ         SHELL_HELP("Read channel", "<channel_id>")
#define TELEMETRY_HELP_TRIGGER      SHELL_HELP("Trigger immediate telemetry sample", NULL)
#define TELEMETRY_HELP_CONFIG       SHELL_HELP("Telemetry configuration", NULL)
#define TELEMETRY_HELP_CONFIG_GET   SHELL_HELP("Show current configuration", NULL)
#define TELEMETRY_HELP_CONFIG_SET   SHELL_HELP("Set configuration", "<enabled> <sample_interval_ms>")
#define TELEMETRY_HELP_CONFIG_RESET SHELL_HELP("Reset configuration to defaults", NULL)

static void telemetry_shell_sensor_value_snprint(char *buf, size_t len,
						 const struct sensor_value *v)
{
	int64_t micro = sensor_value_to_micro(v);
	bool neg = (micro < 0);
	uint64_t abs_micro = (uint64_t)(neg ? -micro : micro);
	uint64_t ip = abs_micro / 1000000ULL;
	uint64_t fp = abs_micro % 1000000ULL;

	(void)snprintk(buf, len, "%s%llu.%06llu", neg ? "-" : "", (unsigned long long)ip,
		       (unsigned long long)fp);
}

static void telemetry_shell_sensor_values_print(const struct shell *sh, enum sensor_channel chan,
						const struct sensor_value *vals, size_t count)
{
	char vbuf[4][24];

	for (size_t i = 0; i < count; i++) {
		telemetry_shell_sensor_value_snprint(vbuf[i], sizeof(vbuf[i]), &vals[i]);
	}

	if (count == 1U) {
		shell_print(sh, "chan[%u]: %s", (unsigned int)chan, vbuf[0]);
	} else if (count == 2U) {
		shell_print(sh, "chan[%u]: (%s, %s)", (unsigned int)chan, vbuf[0], vbuf[1]);
	} else if (count == 3U) {
		shell_print(sh, "chan[%u]: (%s, %s, %s)", (unsigned int)chan, vbuf[0], vbuf[1],
			    vbuf[2]);
	} else {
		shell_print(sh, "chan[%u]: (%s, %s, %s, %s)", (unsigned int)chan, vbuf[0], vbuf[1],
			    vbuf[2], vbuf[3]);
	}
}

static int parse_channel_arg(const char *arg, enum sensor_channel *out_chan)
{
	uint32_t chan_id;
	int ret = mbs_shell_parse_u32_arg(arg, &chan_id);

	if ((ret != 0) || (chan_id >= SENSOR_CHAN_ALL)) {
		return -EINVAL;
	}

	*out_chan = (enum sensor_channel)chan_id;
	return 0;
}

static int cmd_telemetry_config_get(const struct shell *sh, size_t argc, char **argv)
{
	mbs_telemetry_config cfg;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = mbs_telemetry_config_get(&cfg);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	shell_print(sh, "Settings:");
	shell_print(sh, "  enabled:         %s", cfg.enabled ? "yes" : "no");
	shell_print(sh, "  sample_interval: %u ms", cfg.sample_interval);

	return 0;
}

static int cmd_telemetry_config_set(const struct shell *sh, size_t argc, char **argv)
{
	mbs_telemetry_config cfg;
	int ret;

	ARG_UNUSED(argc);

	ret = mbs_telemetry_config_get(&cfg);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	ret = mbs_shell_parse_bool_arg(argv[1], &cfg.enabled);
	ret |= mbs_shell_parse_u32_arg(argv[2], &cfg.sample_interval);
	if (ret != 0) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}

	ret = mbs_telemetry_config_set(&cfg);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	return cmd_telemetry_config_get(sh, 0, NULL);
}

static int cmd_telemetry_config_reset(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = mbs_telemetry_config_reset();
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	return cmd_telemetry_config_get(sh, 0, NULL);
}

static int cmd_telemetry_status(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = cmd_telemetry_config_get(sh, 0, NULL);
	if (ret != 0) {
		return ret;
	}

	size_t bindings = mbs_telemetry_bindings_count();
	shell_print(sh, "Bindings:");
	shell_print(sh, "  configured:   %u", (unsigned int)bindings);
	for (size_t i = 0; i < bindings; i++) {
		struct mbs_telemetry_binding binding;

		ret = mbs_telemetry_binding_get(i, &binding);
		if (ret != 0) {
			continue;
		}

		shell_print(sh, "  [%2u] chan=%u sensor=%s", (unsigned int)i,
			    (unsigned int)binding.chan,
			    binding.sensor_name != NULL ? binding.sensor_name : "unknown");
	}

	return 0;
}

static int cmd_telemetry_enable(const struct shell *sh, size_t argc, char **argv)
{
	mbs_telemetry_config cfg;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = mbs_telemetry_config_get(&cfg);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	if (!cfg.enabled) {
		cfg.enabled = true;
		ret = mbs_telemetry_config_set(&cfg);
		if (ret != 0) {
			mbs_shell_error(sh, ret);
			return ret;
		}
	}

	return 0;
}

static int cmd_telemetry_disable(const struct shell *sh, size_t argc, char **argv)
{
	mbs_telemetry_config cfg;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = mbs_telemetry_config_get(&cfg);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	if (cfg.enabled) {
		cfg.enabled = false;
		ret = mbs_telemetry_config_set(&cfg);
		if (ret != 0) {
			mbs_shell_error(sh, ret);
			return ret;
		}
	}

	return 0;
}

static int cmd_telemetry_read(const struct shell *sh, size_t argc, char **argv)
{
	enum sensor_channel target_chan;
	struct sensor_value vals[MBS_TELEMETRY_MAX_VALUES] = {0};
	size_t value_count;
	int ret;

	ARG_UNUSED(argc);

	ret = parse_channel_arg(argv[1], &target_chan);
	if (ret != 0) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}

	value_count = mbs_telemetry_channel_value_count(target_chan);
	value_count = MIN(value_count, MBS_TELEMETRY_MAX_VALUES);

	ret = mbs_telemetry_channel_get(target_chan, vals);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	telemetry_shell_sensor_values_print(sh, target_chan, vals, value_count);
	return 0;
}

static int cmd_telemetry_trigger(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = mbs_telemetry_sample_trigger();
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	mbs_telemetry_config_subcmds,
	SHELL_CMD_ARG(get, NULL, TELEMETRY_HELP_CONFIG_GET, cmd_telemetry_config_get, 1, 0),
	SHELL_CMD_ARG(set, NULL, TELEMETRY_HELP_CONFIG_SET, cmd_telemetry_config_set, 3, 0),
	SHELL_CMD_ARG(reset, NULL, TELEMETRY_HELP_CONFIG_RESET, cmd_telemetry_config_reset, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_SET_CREATE(mbs_telemetry_subcmds, (meshbus, telemetry));

SHELL_SUBCMD_ADD((meshbus, telemetry), status, NULL, TELEMETRY_HELP_STATUS, cmd_telemetry_status, 1,
		 0);
SHELL_SUBCMD_ADD((meshbus, telemetry), enable, NULL, TELEMETRY_HELP_ENABLE, cmd_telemetry_enable, 1,
		 0);
SHELL_SUBCMD_ADD((meshbus, telemetry), disable, NULL, TELEMETRY_HELP_DISABLE, cmd_telemetry_disable,
		 1, 0);
SHELL_SUBCMD_ADD((meshbus, telemetry), read, NULL, TELEMETRY_HELP_READ, cmd_telemetry_read, 2, 0);
SHELL_SUBCMD_ADD((meshbus, telemetry), trigger, NULL, TELEMETRY_HELP_TRIGGER, cmd_telemetry_trigger,
		 1, 0);
SHELL_SUBCMD_ADD((meshbus, telemetry), config, &mbs_telemetry_config_subcmds,
		 TELEMETRY_HELP_CONFIG, NULL, 0, 0);

SHELL_SUBCMD_ADD((meshbus), telemetry, &mbs_telemetry_subcmds, TELEMETRY_HELP_ROOT, NULL, 0, 0);
