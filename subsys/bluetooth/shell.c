/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <strings.h>
#include <string.h>

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <bluetooth/bluetooth.h>
#include <zephyr/shell/shell.h>

#include "mbs_shell_internal.h"

#define BT_HELP_ROOT   SHELL_HELP("Bluetooth service control and configuration", NULL)
#define BT_HELP_STATUS SHELL_HELP("Display Bluetooth status", NULL)
#define BT_HELP_ENABLE SHELL_HELP("Enable Bluetooth", NULL)
#define BT_HELP_DISABLE SHELL_HELP("Disable Bluetooth", NULL)

#define BT_HELP_CONFIG       SHELL_HELP("Bluetooth configuration", NULL)
#define BT_HELP_CONFIG_GET   SHELL_HELP("Show current configuration", NULL)
#define BT_HELP_CONFIG_SET                                                               \
	SHELL_HELP("Set configuration", "<enabled> <passkey_mode> <fixed_passkey> [companion]")
#define BT_HELP_CONFIG_RESET SHELL_HELP("Reset configuration to defaults", NULL)

struct conn_status {
	bool connected;
	char peer[BT_ADDR_LE_STR_LEN];
	uint8_t security;
};

static const char *passkey_mode_to_str(mbs_bluetooth_passkey_mode mode)
{
	switch (mode) {
	case MBS_BLUETOOTH_PASSKEY_MODE_RANDOM:
		return "random";
	case MBS_BLUETOOTH_PASSKEY_MODE_FIXED:
		return "fixed";
	default:
		return "unknown";
	}
}

static int parse_passkey_mode_arg(const char *arg, mbs_bluetooth_passkey_mode *out)
{
	uint32_t mode;
	int ret;

	if (arg == NULL || out == NULL) {
		return -EINVAL;
	}

	ret = mbs_shell_parse_u32_arg(arg, &mode);
	if (ret == 0) {
		if (mode <= MBS_BLUETOOTH_PASSKEY_MODE_FIXED) {
			*out = (mbs_bluetooth_passkey_mode)mode;
			return 0;
		}
		return -EINVAL;
	}

	if (strcasecmp(arg, "random") == 0) {
		*out = MBS_BLUETOOTH_PASSKEY_MODE_RANDOM;
		return 0;
	}

	if (strcasecmp(arg, "fixed") == 0) {
		*out = MBS_BLUETOOTH_PASSKEY_MODE_FIXED;
		return 0;
	}

	return -EINVAL;
}

static void foreach_conn_status(struct bt_conn *conn, void *data)
{
	struct conn_status *st = (struct conn_status *)data;
	struct bt_conn_info info;

	if (st == NULL || st->connected) {
		return;
	}

	if (bt_conn_get_info(conn, &info) != 0) {
		return;
	}

	if (info.type != BT_CONN_TYPE_LE || info.state != BT_CONN_STATE_CONNECTED) {
		return;
	}

	const bt_addr_le_t *dst = bt_conn_get_dst(conn);

	if (dst != NULL) {
		bt_addr_le_to_str(dst, st->peer, sizeof(st->peer));
	} else {
		strncpy(st->peer, "<unknown>", sizeof(st->peer));
		st->peer[sizeof(st->peer) - 1U] = '\0';
	}

	st->security = (uint8_t)bt_conn_get_security(conn);
	st->connected = true;
}

static int cmd_bt_config_get(const struct shell *sh, size_t argc, char **argv)
{
	mbs_bluetooth_config cfg;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = mbs_bluetooth_config_get(&cfg);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	shell_print(sh, "Settings:");
	shell_print(sh, "  enabled:      %s", cfg.enabled ? "yes" : "no");
	shell_print(sh, "  passkey_mode: %u (%s)", (uint32_t)cfg.passkey_mode,
		    passkey_mode_to_str(cfg.passkey_mode));
	shell_print(sh, "  fixed_passkey: %06u", cfg.fixed_passkey);
	shell_print(sh, "  companion: %s", cfg.meshcore_companion_enabled ? "yes" : "no");

	return 0;
}

static int cmd_bt_config_set(const struct shell *sh, size_t argc, char **argv)
{
	mbs_bluetooth_config cfg;
	uint32_t fixed_passkey = 0;
	mbs_bluetooth_passkey_mode passkey_mode = MBS_BLUETOOTH_PASSKEY_MODE_RANDOM;
	int ret;

	ARG_UNUSED(argc);

	ret = mbs_bluetooth_config_get(&cfg);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	ret = mbs_shell_parse_bool_arg(argv[1], &cfg.enabled);
	ret |= parse_passkey_mode_arg(argv[2], &passkey_mode);
	ret |= mbs_shell_parse_u32_arg(argv[3], &fixed_passkey);
	if (ret != 0 || fixed_passkey > 999999U) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}

	if (argc > 4U) {
		ret = mbs_shell_parse_bool_arg(argv[4], &cfg.meshcore_companion_enabled);
		if (ret != 0) {
			mbs_shell_invalid(sh);
			return -EINVAL;
		}
	}

	cfg.passkey_mode = passkey_mode;
	cfg.fixed_passkey = fixed_passkey;

	ret = mbs_bluetooth_config_set(&cfg);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	return cmd_bt_config_get(sh, 0, NULL);
}

static int cmd_bt_config_reset(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = mbs_bluetooth_config_reset();
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	return cmd_bt_config_get(sh, 0, NULL);
}

static int cmd_bt_enable(const struct shell *sh, size_t argc, char **argv)
{
	mbs_bluetooth_config cfg;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = mbs_bluetooth_config_get(&cfg);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	if (!cfg.enabled) {
		cfg.enabled = true;
		ret = mbs_bluetooth_config_set(&cfg);
		if (ret != 0) {
			mbs_shell_error(sh, ret);
			return ret;
		}
	}

	return 0;
}

static int cmd_bt_disable(const struct shell *sh, size_t argc, char **argv)
{
	mbs_bluetooth_config cfg;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = mbs_bluetooth_config_get(&cfg);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	if (cfg.enabled) {
		cfg.enabled = false;
		ret = mbs_bluetooth_config_set(&cfg);
		if (ret != 0) {
			mbs_shell_error(sh, ret);
			return ret;
		}
	}

	return 0;
}

static int cmd_bt_status(const struct shell *sh, size_t argc, char **argv)
{
	struct conn_status st = {0};

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (bt_is_ready()) {
		bt_conn_foreach(BT_CONN_TYPE_LE, foreach_conn_status, &st);
	}

	shell_print(sh, "Connection:");
	if (st.connected) {
		shell_print(sh, "  connected: yes");
		shell_print(sh, "  peer:      %s", st.peer);
		shell_print(sh, "  security:  %u", st.security);
	} else {
		shell_print(sh, "  connected: no");
	}

	return cmd_bt_config_get(sh, 0, NULL);
}

SHELL_STATIC_SUBCMD_SET_CREATE(mbs_bt_config_subcmds,
	SHELL_CMD_ARG(get, NULL, BT_HELP_CONFIG_GET, cmd_bt_config_get, 1, 0),
	SHELL_CMD_ARG(set, NULL, BT_HELP_CONFIG_SET, cmd_bt_config_set, 4, 1),
	SHELL_CMD_ARG(reset, NULL, BT_HELP_CONFIG_RESET, cmd_bt_config_reset, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(mbs_bt_subcmds,
	SHELL_CMD(status, NULL, BT_HELP_STATUS, cmd_bt_status),
	SHELL_CMD(enable, NULL, BT_HELP_ENABLE, cmd_bt_enable),
	SHELL_CMD(disable, NULL, BT_HELP_DISABLE, cmd_bt_disable),
	SHELL_CMD(config, &mbs_bt_config_subcmds, BT_HELP_CONFIG, NULL),
	SHELL_SUBCMD_SET_END
);

SHELL_SUBCMD_ADD((meshbus), bluetooth, &mbs_bt_subcmds, BT_HELP_ROOT, NULL, 0, 0);
