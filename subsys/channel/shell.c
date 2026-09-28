/*
 * Copyright (c) 2025 FoBE Projects
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <channel/channel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include "mbs_shell_internal.h"

#define CHANNEL_HELP_ROOT SHELL_HELP("Channel module control and configuration", NULL)
#define CHANNEL_HELP_SET SHELL_HELP("Set channel slot", "<index> <secret_hex> [name]")
#define CHANNEL_HELP_GET SHELL_HELP("Get channel by index", "<index>")
#define CHANNEL_HELP_RESET SHELL_HELP("Reset channel slot by index", "<index>")
#define CHANNEL_HELP_STORE_COUNT SHELL_HELP("Get current channel store count", NULL)
#define CHANNEL_HELP_STORE_SIZE SHELL_HELP("Get configured channel store size", NULL)
#define CHANNEL_HELP_NEXT_FREE_SLOT SHELL_HELP("Get first free channel slot index", NULL)

static void print_channel(const struct shell *sh, const mbs_channel *channel)
{
	uint8_t hash = (channel->hash.size > 0U) ? channel->hash.bytes[0] : 0U;
	uint8_t pref0 = (channel->secret.size > 0U) ? channel->secret.bytes[0] : 0U;
	uint8_t pref1 = (channel->secret.size > 1U) ? channel->secret.bytes[1] : 0U;
	uint8_t pref2 = (channel->secret.size > 2U) ? channel->secret.bytes[2] : 0U;

	shell_print(sh, "  name:		 %s", channel->name);
	shell_print(sh, "  hash:         %02x", hash);
	shell_print(sh, "  secret_len:   %u", (unsigned int)channel->secret.size);
	shell_print(sh, "  secret_prefix:%02x%02x%02x", pref0, pref1, pref2);
	shell_hexdump(sh, channel->secret.bytes, channel->secret.size);
}

static int parse_hex_arg(const char *arg, uint8_t *out, size_t out_size, size_t *out_len)
{
	size_t arg_len;
	int parsed;

	if ((arg == NULL) || (out == NULL) || (out_len == NULL)) {
		return -EINVAL;
	}

	arg_len = strlen(arg);
	if ((arg_len == 0U) || ((arg_len % 2U) != 0U)) {
		return -EINVAL;
	}

	parsed = hex2bin(arg, arg_len, out, out_size);
	if (parsed < 0) {
		return parsed;
	}

	*out_len = (size_t)parsed;
	return 0;
}

static int parse_secret_arg(const char *arg, uint8_t *out, size_t out_size, size_t *out_len)
{
	int rc = parse_hex_arg(arg, out, out_size, out_len);

	if (rc != 0) {
		return rc;
	}
	if ((*out_len != MBS_CHANNEL_SECRET_DEFAULT_LEN) &&
	    (*out_len != MBS_CHANNEL_SECRET_SIZE)) {
		return -EINVAL;
	}

	return 0;
}

static int cmd_channel_set(const struct shell *sh, size_t argc, char **argv)
{
	mbs_channel channel = meshbus_Channel_init_zero;
	uint8_t secret[MBS_CHANNEL_SECRET_SIZE] = {0};
	uint32_t index = 0U;
	size_t secret_len;
	const char *name = NULL;
	int rc;

	if ((argc != 3U) && (argc != 4U)) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}

	rc = mbs_shell_parse_u32_arg(argv[1], &index);
	if (rc != 0 || index > UINT8_MAX) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}
	rc = parse_secret_arg(argv[2], secret, sizeof(secret), &secret_len);
	if (rc != 0) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}
	if (argc == 4U) {
		name = argv[3];
	}

	rc = mbs_channel_set((size_t)index, secret, secret_len, name);
	if (rc != 0) {
		mbs_shell_error(sh, rc);
		return rc;
	}

	rc = mbs_channel_get((size_t)index, &channel);
	if (rc != 0) {
		mbs_shell_error(sh, rc);
		return rc;
	}

	print_channel(sh, &channel);
	return 0;
}

static int cmd_channel_get(const struct shell *sh, size_t argc, char **argv)
{
	mbs_channel channel = meshbus_Channel_init_zero;
	uint32_t index = 0U;
	int rc;

	ARG_UNUSED(argc);

	rc = mbs_shell_parse_u32_arg(argv[1], &index);
	if (rc != 0 || index > UINT8_MAX) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}

	rc = mbs_channel_get((size_t)index, &channel);
	if (rc != 0) {
		mbs_shell_error(sh, rc);
		return rc;
	}

	print_channel(sh, &channel);
	return 0;
}

static int cmd_channel_reset(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t index = 0U;
	int rc;

	ARG_UNUSED(argc);

	rc = mbs_shell_parse_u32_arg(argv[1], &index);
	if (rc != 0 || index > UINT8_MAX) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}

	rc = mbs_channel_reset((size_t)index);
	if (rc != 0) {
		mbs_shell_error(sh, rc);
		return rc;
	}

	return 0;
}

static int cmd_channel_store_count(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "%u", (unsigned int)mbs_channel_store_count());
	return 0;
}

static int cmd_channel_store_size(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "%u", (unsigned int)mbs_channel_store_size());
	return 0;
}

static int cmd_channel_next_free_slot(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "%u", (unsigned int)mbs_channel_next_free_slot());
	return 0;
}

SHELL_SUBCMD_SET_CREATE(mbs_channel_subcmds, (meshbus, channel));

SHELL_SUBCMD_ADD((meshbus, channel), set, NULL, CHANNEL_HELP_SET, cmd_channel_set, 4, 1);
SHELL_SUBCMD_ADD((meshbus, channel), get, NULL, CHANNEL_HELP_GET, cmd_channel_get, 2, 0);
SHELL_SUBCMD_ADD((meshbus, channel), reset, NULL, CHANNEL_HELP_RESET, cmd_channel_reset, 2, 0);
SHELL_SUBCMD_ADD((meshbus, channel), count, NULL, CHANNEL_HELP_STORE_COUNT,
		 cmd_channel_store_count, 1, 0);
SHELL_SUBCMD_ADD((meshbus, channel), size, NULL, CHANNEL_HELP_STORE_SIZE,
		 cmd_channel_store_size, 1, 0);
SHELL_SUBCMD_ADD((meshbus, channel), next_free, NULL, CHANNEL_HELP_NEXT_FREE_SLOT,
		 cmd_channel_next_free_slot, 1, 0);

SHELL_SUBCMD_ADD((meshbus), channel, &mbs_channel_subcmds, CHANNEL_HELP_ROOT, NULL, 0, 0);
