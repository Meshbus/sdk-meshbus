/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/meshbus/llext.h>
#include <zephyr/shell/shell.h>

#include "common/shell.h"

#define LLEXT_HELP_ROOT SHELL_HELP("LLEXT app configuration", NULL)
#define LLEXT_HELP_CONFIG SHELL_HELP("LLEXT app configuration", NULL)
#define LLEXT_HELP_CONFIG_GET SHELL_HELP("Show current configuration", NULL)
#define LLEXT_HELP_CONFIG_SET \
	SHELL_HELP("Enable or disable new app loads", "<enabled: true|false>")
#define LLEXT_HELP_CONFIG_RESET SHELL_HELP("Reset configuration to defaults", NULL)

static void print_config(const struct shell *sh, const meshbus_llext_config *cfg)
{
	shell_print(sh, "Settings:");
	shell_print(sh, "  LLEXT enabled: %s", cfg->enabled ? "yes" : "no");
	shell_print(sh, "  note: disabling blocks new app loads; the running app continues");
}

static int cmd_llext_config_get(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_llext_config cfg;
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = meshbus_llext_config_get(&cfg);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	print_config(sh, &cfg);
	return 0;
}

static int cmd_llext_config_set(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_llext_config cfg;
	bool enabled;
	int rc;

	if (argc != 2U) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	rc = meshbus_llext_config_get(&cfg);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	rc = mb_shell_parse_bool_arg(argv[1], &enabled);
	if (rc != 0) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	cfg.enabled = enabled;
	rc = meshbus_llext_config_set(&cfg);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	return cmd_llext_config_get(sh, 0, NULL);
}

static int cmd_llext_config_reset(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = meshbus_llext_config_reset();
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	return cmd_llext_config_get(sh, 0, NULL);
}

SHELL_STATIC_SUBCMD_SET_CREATE(meshbus_llext_config_subcmds,
	SHELL_CMD_ARG(get, NULL, LLEXT_HELP_CONFIG_GET, cmd_llext_config_get, 1, 0),
	SHELL_CMD_ARG(set, NULL, LLEXT_HELP_CONFIG_SET, cmd_llext_config_set, 2, 0),
	SHELL_CMD_ARG(reset, NULL, LLEXT_HELP_CONFIG_RESET, cmd_llext_config_reset, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_SET_CREATE(meshbus_llext_subcmds, (meshbus, llext));

SHELL_SUBCMD_ADD((meshbus, llext), config, &meshbus_llext_config_subcmds,
		 LLEXT_HELP_CONFIG, NULL, 0, 0);

SHELL_SUBCMD_ADD((meshbus), llext, &meshbus_llext_subcmds, LLEXT_HELP_ROOT, NULL,
		 0, 0);
