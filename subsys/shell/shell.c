/*
 * Copyright (c) 2025 FoBE Projects
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Meshbus shell root commands
 *
 * This file provides the root "meshbus" shell command and creates a
 * placeholder for subcommands from various meshbus modules (radio, etc.).
 * Subcommands are added using SHELL_SUBCMD_ADD from their respective modules.
 */

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "mbs_shell_internal.h"

int mbs_shell_parse_u64_arg(const char *arg, uint64_t *out)
{
	int err = 0;
	unsigned long long value;

	value = shell_strtoull(arg, 10, &err);
	if (err != 0) {
		return -EINVAL;
	}

	*out = (uint64_t)value;
	return 0;
}

int mbs_shell_parse_u32_arg(const char *arg, uint32_t *out)
{
	uint64_t value;
	int ret;

	ret = mbs_shell_parse_u64_arg(arg, &value);
	if ((ret != 0) || (value > UINT32_MAX)) {
		return -EINVAL;
	}

	*out = (uint32_t)value;
	return 0;
}

int mbs_shell_parse_u16_arg(const char *arg, uint16_t *out)
{
	uint32_t value;
	int ret;

	ret = mbs_shell_parse_u32_arg(arg, &value);
	if ((ret != 0) || (value > UINT16_MAX)) {
		return -EINVAL;
	}

	*out = (uint16_t)value;
	return 0;
}

int mbs_shell_parse_u8_arg(const char *arg, uint8_t *out)
{
	uint16_t value;
	int ret;

	ret = mbs_shell_parse_u16_arg(arg, &value);
	if ((ret != 0) || (value > UINT8_MAX)) {
		return -EINVAL;
	}

	*out = (uint8_t)value;
	return 0;
}

int mbs_shell_parse_i32_arg(const char *arg, int32_t *out)
{
	int err = 0;
	long value;

	value = shell_strtol(arg, 10, &err);
	if ((err != 0) || (value < INT32_MIN) || (value > INT32_MAX)) {
		return -EINVAL;
	}

	*out = (int32_t)value;
	return 0;
}

int mbs_shell_parse_i16_arg(const char *arg, int16_t *out)
{
	int32_t value;
	int ret;

	ret = mbs_shell_parse_i32_arg(arg, &value);
	if ((ret != 0) || (value < INT16_MIN) || (value > INT16_MAX)) {
		return -EINVAL;
	}

	*out = (int16_t)value;
	return 0;
}

int mbs_shell_parse_bool_arg(const char *arg, bool *out)
{
	if ((arg == NULL) || (out == NULL)) {
		return -EINVAL;
	}

	/* Fast-path: accept 0/1 (and only 0/1) to preserve existing behavior. */
	uint32_t value;
	int ret = mbs_shell_parse_u32_arg(arg, &value);
	if ((ret == 0) && (value <= 1U)) {
		*out = (value == 1U);
		return 0;
	}

	/* Also accept common boolean tokens (case-insensitive). */
	while (isspace((unsigned char)*arg)) {
		arg++;
	}

	size_t len = strlen(arg);
	while ((len > 0U) && isspace((unsigned char)arg[len - 1U])) {
		len--;
	}
	if (len == 0U) {
		return -EINVAL;
	}

	char tmp[16];
	if (len >= sizeof(tmp)) {
		return -EINVAL;
	}

	for (size_t i = 0; i < len; i++) {
		tmp[i] = (char)tolower((unsigned char)arg[i]);
	}
	tmp[len] = '\0';

	if (!strcmp(tmp, "true") || !strcmp(tmp, "on") || !strcmp(tmp, "yes") ||
	    !strcmp(tmp, "enable") || !strcmp(tmp, "enabled")) {
		*out = true;
		return 0;
	}

	if (!strcmp(tmp, "false") || !strcmp(tmp, "off") || !strcmp(tmp, "no") ||
	    !strcmp(tmp, "disable") || !strcmp(tmp, "disabled")) {
		*out = false;
		return 0;
	}

	return -EINVAL;
}

int mbs_shell_parse_float_arg(const char *arg, float *out)
{
	char *end = NULL;
	float value;

	errno = 0;
	value = strtof(arg, &end);
	if ((errno != 0) || (end == arg) || (*end != '\0')) {
		return -EINVAL;
	}

	*out = value;
	return 0;
}

void mbs_shell_error(const struct shell *sh, int32_t err)
{
	shell_error(sh, "%s: execute failed (err %d)", sh->ctx->active_cmd.syntax, err);
}

void mbs_shell_invalid(const struct shell *sh)
{
	shell_error(sh, "%s: invalid argument", sh->ctx->active_cmd.syntax);
	shell_help(sh);
}

/* Create expandable subcommand set for meshbus root command.
 * Subcommands are added from other files using SHELL_SUBCMD_ADD((meshbus), ...)
 */
SHELL_SUBCMD_SET_CREATE(mbs_subcmds, (meshbus));

/* Register the root 'meshbus' command.
 * Handler is NULL so shell automatically displays subcommands when called without args.
 */
SHELL_CMD_REGISTER(meshbus, &mbs_subcmds, "Meshbus subsystem commands", NULL);
