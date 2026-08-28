/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include "common/shell.h"
#include "input.h"

#define INPUT_HELP_ROOT       SHELL_HELP("Input module control and diagnostics", NULL)
#define INPUT_HELP_STATUS     SHELL_HELP("Show input module status", NULL)
#define INPUT_HELP_INJECT     SHELL_HELP("Inject synthetic input events", NULL)
#define INPUT_HELP_INJECT_ACT SHELL_HELP("Inject action event", "<type> <code> <action>")
#define INPUT_HELP_INJECT_RAW SHELL_HELP("Inject input event", "<type> <code> <value>")

static const char *input_type_str(uint8_t type)
{
	switch (type) {
	case INPUT_EV_KEY:
		return "key";
	case INPUT_EV_REL:
		return "rel";
	case INPUT_EV_ABS:
		return "abs";
	case INPUT_EV_MSC:
		return "msc";
	default:
		return "unknown";
	}
}

static const char *input_action_str(uint8_t action)
{
	switch (action) {
	case INPUT_ACT_KEY_LONG:
		return "long";
	case INPUT_ACT_KEY_SHORT:
		return "short";
	case INPUT_ACT_SCROLL_CW:
		return "scroll_cw";
	case INPUT_ACT_SCROLL_CCW:
		return "scroll_ccw";
	default:
		return "unknown";
	}
}

static int parse_input_type_arg(const char *arg, uint8_t *out)
{
	uint32_t value;
	int ret;

	if (strcmp(arg, "key") == 0) {
		*out = INPUT_EV_KEY;
		return 0;
	}
	if (strcmp(arg, "rel") == 0) {
		*out = INPUT_EV_REL;
		return 0;
	}
	if (strcmp(arg, "abs") == 0) {
		*out = INPUT_EV_ABS;
		return 0;
	}
	if (strcmp(arg, "msc") == 0) {
		*out = INPUT_EV_MSC;
		return 0;
	}

	ret = mb_shell_parse_u32_arg(arg, &value);
	if ((ret != 0) || (value > UINT8_MAX)) {
		return -EINVAL;
	}

	*out = (uint8_t)value;
	return 0;
}

static int parse_action_arg(const char *arg, uint8_t *out)
{
	uint32_t value;
	int ret;

	if (strcmp(arg, "short") == 0) {
		*out = INPUT_ACT_KEY_SHORT;
		return 0;
	}
	if (strcmp(arg, "long") == 0) {
		*out = INPUT_ACT_KEY_LONG;
		return 0;
	}
	if (strcmp(arg, "cw") == 0 || strcmp(arg, "scroll_cw") == 0) {
		*out = INPUT_ACT_SCROLL_CW;
		return 0;
	}
	if (strcmp(arg, "ccw") == 0 || strcmp(arg, "scroll_ccw") == 0) {
		*out = INPUT_ACT_SCROLL_CCW;
		return 0;
	}

	ret = mb_shell_parse_u32_arg(arg, &value);
	if ((ret != 0) || (value > INPUT_ACT_SCROLL_CCW)) {
		return -EINVAL;
	}

	*out = (uint8_t)value;
	return 0;
}

static int cmd_input_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Status:");
	shell_print(sh, "  input_button:  %s",
		    IS_ENABLED(CONFIG_MESHBUS_INPUT_BUTTON) ? "on" : "off");
	shell_print(sh, "  input_encoder: %s",
		    IS_ENABLED(CONFIG_MESHBUS_INPUT_ENCODER) ? "on" : "off");
	shell_print(sh, "  input_keypad:  %s",
		    IS_ENABLED(CONFIG_MESHBUS_INPUT_KEYPAD) ? "on" : "off");
	shell_print(sh, "  long_press_ms: %u", CONFIG_MESHBUS_INPUT_LONG_PRESS_MS);
	shell_print(sh, "  repeat_delay_ms: %u", CONFIG_MESHBUS_INPUT_REPEAT_DELAY_MS);
	shell_print(sh, "  repeat_interval_ms: %u", CONFIG_MESHBUS_INPUT_REPEAT_INTERVAL_MS);
	shell_print(sh, "  debounce_ms:   %u", CONFIG_MESHBUS_INPUT_DEBOUNCE_MS);

	return 0;
}

static int cmd_input_inject_act(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t type;
	uint16_t code;
	uint8_t action;
	int ret;

	ARG_UNUSED(argc);

	ret = parse_input_type_arg(argv[1], &type);
	if (ret != 0) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	ret = mb_shell_parse_u16_arg(argv[2], &code);
	if (ret != 0) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	ret = parse_action_arg(argv[3], &action);
	if (ret != 0) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	ret = meshbus_input_action_event_publish(type, code, action);
	if (ret != 0) {
		mb_shell_error(sh, ret);
		return ret;
	}

	shell_print(sh, "Injected action event: type=%s(%u) code=0x%04x action=%s(%u)",
		    input_type_str(type), type, code, input_action_str(action), action);

	return 0;
}

static int cmd_input_inject_raw(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t type;
	uint16_t code;
	int32_t value;
	int ret;

	ARG_UNUSED(argc);

	ret = mb_shell_parse_u8_arg(argv[1], &type);
	ret |= mb_shell_parse_u16_arg(argv[2], &code);
	ret |= mb_shell_parse_i32_arg(argv[3], &value);
	if (ret != 0) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	ret = meshbus_input_key_event_publish(type, code, value);
	if (ret != 0) {
		mb_shell_error(sh, ret);
		return ret;
	}

	shell_print(sh, "Injected raw event: type=%s(%u) code=0x%04x value=%d",
		    input_type_str(type), type, code, value);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(meshbus_input_inject_subcmds,
			       SHELL_CMD_ARG(act, NULL, INPUT_HELP_INJECT_ACT, cmd_input_inject_act,
					     4, 0),
			       SHELL_CMD_ARG(raw, NULL, INPUT_HELP_INJECT_RAW, cmd_input_inject_raw,
					     4, 0),
			       SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_SET_CREATE(meshbus_input_subcmds, (meshbus, input));

SHELL_SUBCMD_ADD((meshbus, input), status, NULL, INPUT_HELP_STATUS, cmd_input_status, 1, 0);
SHELL_SUBCMD_ADD((meshbus, input), inject, &meshbus_input_inject_subcmds, INPUT_HELP_INJECT, NULL,
		 0, 0);

SHELL_SUBCMD_ADD((meshbus), input, &meshbus_input_subcmds, INPUT_HELP_ROOT, NULL, 0, 0);
