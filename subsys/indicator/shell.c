/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <indicator/indicator.h>

#include "mbs_shell_internal.h"

#define INDICATOR_HELP_ROOT         SHELL_HELP("Indicator subsystem control and configuration", NULL)
#define INDICATOR_HELP_STATUS       SHELL_HELP("Show indicator status", NULL)
#define INDICATOR_HELP_CONFIG       SHELL_HELP("Indicator configuration", NULL)
#define INDICATOR_HELP_CONFIG_GET   SHELL_HELP("Show current configuration", NULL)
#define INDICATOR_HELP_CONFIG_SET                                                               \
	SHELL_HELP("Set configuration",                                                               \
		   "<light_enabled> <buzzer_enabled> <light_heartbeat> <buzzer_dm> "                \
		   "<buzzer_channel> <buzzer_system>")
#define INDICATOR_HELP_CONFIG_RESET SHELL_HELP("Reset configuration to defaults", NULL)
#define INDICATOR_HELP_LIGHT        SHELL_HELP("Light indicator control", NULL)
#define INDICATOR_HELP_LIGHT_PLAY   SHELL_HELP("Play light pattern", "<on_ms> <off_ms> <count>")
#define INDICATOR_HELP_LIGHT_COLOR  SHELL_HELP("Set light color", "<r> <g> <b>")
#define INDICATOR_HELP_LIGHT_STOP   SHELL_HELP("Stop light playback", NULL)
#define INDICATOR_HELP_BUZZER       SHELL_HELP("Buzzer indicator control", NULL)
#define INDICATOR_HELP_BUZZER_PLAY  SHELL_HELP("Play buzzer tone", "<freq_hz> <duration_ms>")
#define INDICATOR_HELP_BUZZER_RTTTL SHELL_HELP("Play RTTTL melody", "<rtttl_string>")
#define INDICATOR_HELP_BUZZER_STOP  SHELL_HELP("Stop buzzer playback", NULL)

static int cmd_indicator_config_get(const struct shell *sh, size_t argc, char **argv)
{
	mbs_indicator_config cfg;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = mbs_indicator_config_get(&cfg);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	shell_print(sh, "Settings:");
	shell_print(sh, "  light_enabled:  %s", cfg.light_enabled ? "yes" : "no");
	shell_print(sh, "  buzzer_enabled: %s", cfg.buzzer_enabled ? "yes" : "no");
	shell_print(sh, "  light_heartbeat: %s",
		    cfg.light_feedback.heartbeat_enabled ? "yes" : "no");
	shell_print(sh, "  buzzer_direct_message: %s",
		    cfg.buzzer_feedback.direct_message_enabled ? "yes" : "no");
	shell_print(sh, "  buzzer_channel_message: %s",
		    cfg.buzzer_feedback.channel_message_enabled ? "yes" : "no");
	shell_print(sh, "  buzzer_system: %s", cfg.buzzer_feedback.system_enabled ? "yes" : "no");

	return 0;
}

static int cmd_indicator_config_set(const struct shell *sh, size_t argc, char **argv)
{
	mbs_indicator_config cfg;
	int ret;

	ARG_UNUSED(argc);

	ret = mbs_indicator_config_get(&cfg);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	ret = mbs_shell_parse_bool_arg(argv[1], &cfg.light_enabled);
	ret |= mbs_shell_parse_bool_arg(argv[2], &cfg.buzzer_enabled);
	cfg.has_light_feedback = true;
	ret |= mbs_shell_parse_bool_arg(argv[3], &cfg.light_feedback.heartbeat_enabled);
	cfg.has_buzzer_feedback = true;
	ret |= mbs_shell_parse_bool_arg(argv[4], &cfg.buzzer_feedback.direct_message_enabled);
	ret |= mbs_shell_parse_bool_arg(argv[5], &cfg.buzzer_feedback.channel_message_enabled);
	ret |= mbs_shell_parse_bool_arg(argv[6], &cfg.buzzer_feedback.system_enabled);
	if (ret != 0) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}

	ret = mbs_indicator_config_set(&cfg);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	return cmd_indicator_config_get(sh, 0, NULL);
}

static int cmd_indicator_config_reset(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = mbs_indicator_config_reset();
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	return cmd_indicator_config_get(sh, 0, NULL);
}

static int cmd_indicator_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Hardware:");
	shell_print(sh, "  light_ready:  %s", mbs_indicator_light_is_ready() ? "yes" : "no");
	shell_print(sh, "  buzzer_ready: %s", mbs_indicator_buzzer_is_ready() ? "yes" : "no");

	return cmd_indicator_config_get(sh, 0, NULL);
}

static int cmd_indicator_light_play(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t on_ms;
	uint32_t off_ms;
	uint8_t count;
	int ret;

	ARG_UNUSED(argc);

	ret = mbs_shell_parse_u32_arg(argv[1], &on_ms);
	ret |= mbs_shell_parse_u32_arg(argv[2], &off_ms);
	ret |= mbs_shell_parse_u8_arg(argv[3], &count);
	if (ret != 0) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}

	ret = mbs_indicator_light_play(on_ms, off_ms, count);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	return 0;
}

static int cmd_indicator_light_color(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
	int ret;

	ARG_UNUSED(argc);

	ret = mbs_shell_parse_u8_arg(argv[1], &r);
	ret |= mbs_shell_parse_u8_arg(argv[2], &g);
	ret |= mbs_shell_parse_u8_arg(argv[3], &b);
	if (ret != 0) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}

	ret = mbs_indicator_light_color(r, g, b);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	return 0;
}

static int cmd_indicator_light_stop(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = mbs_indicator_light_stop();
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	return 0;
}

static int cmd_indicator_buzzer_play(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t freq;
	uint16_t duration;
	int ret;
	/* Playback stores this pointer asynchronously, so keep it static. */
	static struct indicator_buzzer_note note;
	static struct indicator_buzzer_melody melody;

	ARG_UNUSED(argc);

	ret = mbs_shell_parse_u16_arg(argv[1], &freq);
	ret |= mbs_shell_parse_u16_arg(argv[2], &duration);
	if ((ret != 0) || (freq == 0U) || (duration == 0U)) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}

	note.freq_hz = freq;
	note.duration_ms = duration;
	melody.notes = &note;
	melody.length = 1;

	ret = mbs_indicator_buzzer_play(INDICATOR_SOURCE_SYSTEM, &melody);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	return 0;
}

static int cmd_indicator_buzzer_rtttl(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	if ((argc != 2U) || (argv[1] == NULL) || (argv[1][0] == '\0')) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}

	ret = mbs_indicator_buzzer_rtttl(argv[1]);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	return 0;
}

static int cmd_indicator_buzzer_stop(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	mbs_indicator_buzzer_stop();
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	mbs_indicator_config_subcmds,
	SHELL_CMD_ARG(get, NULL, INDICATOR_HELP_CONFIG_GET, cmd_indicator_config_get, 1, 0),
	SHELL_CMD_ARG(set, NULL, INDICATOR_HELP_CONFIG_SET, cmd_indicator_config_set, 7, 0),
	SHELL_CMD_ARG(reset, NULL, INDICATOR_HELP_CONFIG_RESET, cmd_indicator_config_reset, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
	mbs_indicator_light_subcmds,
	SHELL_CMD_ARG(play, NULL, INDICATOR_HELP_LIGHT_PLAY, cmd_indicator_light_play, 4, 0),
	SHELL_CMD_ARG(color, NULL, INDICATOR_HELP_LIGHT_COLOR, cmd_indicator_light_color, 4, 0),
	SHELL_CMD_ARG(stop, NULL, INDICATOR_HELP_LIGHT_STOP, cmd_indicator_light_stop, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
	mbs_indicator_buzzer_subcmds,
	SHELL_CMD_ARG(play, NULL, INDICATOR_HELP_BUZZER_PLAY, cmd_indicator_buzzer_play, 3, 0),
	SHELL_CMD_ARG(rtttl, NULL, INDICATOR_HELP_BUZZER_RTTTL, cmd_indicator_buzzer_rtttl, 1,
		      SHELL_OPT_ARG_RAW),
	SHELL_CMD_ARG(stop, NULL, INDICATOR_HELP_BUZZER_STOP, cmd_indicator_buzzer_stop, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_SET_CREATE(mbs_indicator_subcmds, (meshbus, indicator));

SHELL_SUBCMD_ADD((meshbus, indicator), status, NULL, INDICATOR_HELP_STATUS, cmd_indicator_status, 1,
		 0);
SHELL_SUBCMD_ADD((meshbus, indicator), config, &mbs_indicator_config_subcmds,
		 INDICATOR_HELP_CONFIG, NULL, 0, 0);
SHELL_SUBCMD_ADD((meshbus, indicator), light, &mbs_indicator_light_subcmds, INDICATOR_HELP_LIGHT,
		 NULL, 0, 0);
SHELL_SUBCMD_ADD((meshbus, indicator), buzzer, &mbs_indicator_buzzer_subcmds,
		 INDICATOR_HELP_BUZZER, NULL, 0, 0);

SHELL_SUBCMD_ADD((meshbus), indicator, &mbs_indicator_subcmds, INDICATOR_HELP_ROOT, NULL, 0, 0);
