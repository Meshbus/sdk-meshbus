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

#define LLEXT_HELP_ROOT SHELL_HELP("LLEXT service status and configuration", NULL)
#define LLEXT_HELP_SERVICE SHELL_HELP("Boot service status", NULL)
#define LLEXT_HELP_CONFIG SHELL_HELP("LLEXT boot service configuration", NULL)
#define LLEXT_HELP_CONFIG_GET SHELL_HELP("Show current configuration", NULL)
#define LLEXT_HELP_CONFIG_SET \
	SHELL_HELP("Set next-boot scan configuration", "<enabled: true|false> <boot_delay_ms>")
#define LLEXT_HELP_CONFIG_RESET SHELL_HELP("Reset configuration to defaults", NULL)
#define LLEXT_HELP_LIST SHELL_HELP("List boot services", NULL)
#define LLEXT_HELP_STATUS SHELL_HELP("Show service status", "<id>")

static const char *state_str(enum meshbus_llext_state state)
{
	switch (state) {
	case MESHBUS_LLEXT_STATE_DISCOVERED:
		return "DISCOVERED";
	case MESHBUS_LLEXT_STATE_LOADED:
		return "LOADED";
	case MESHBUS_LLEXT_STATE_BROUGHT_UP:
		return "BROUGHT_UP";
	case MESHBUS_LLEXT_STATE_RUNNING:
		return "RUNNING";
	case MESHBUS_LLEXT_STATE_EXITED:
		return "EXITED";
	case MESHBUS_LLEXT_STATE_FAULTED:
		return "FAULTED";
	default:
		return "UNKNOWN";
	}
}

static void print_service(const struct shell *sh,
			  const struct meshbus_llext_service_info *info)
{
	shell_print(sh,
		    "id=%s name=%s state=%s version=%s stack=%u heap=%u last_error=%d "
		    "path=%s edk_version=%s target=%s desc=%s",
		    info->id, (info->name[0] != '\0') ? info->name : "-",
		    state_str(info->state), info->version, (unsigned int)info->stack_size,
		    (unsigned int)info->heap_size, info->last_error, info->path,
		    info->edk_version, info->target,
		    (info->description[0] != '\0') ? info->description : "-");
}

static void print_config(const struct shell *sh, const meshbus_llext_config *cfg)
{
	shell_print(sh, "Settings:");
	shell_print(sh, "  LLEXT enabled: %s", cfg->enabled ? "yes" : "no");
	shell_print(sh, "  boot delay:    %u ms", (unsigned int)cfg->boot_delay);
	shell_print(sh, "  note: boot scan changes require reboot; disabled rejects new app loads");
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
	uint32_t boot_delay;
	int rc;

	if (argc != 3U) {
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

	rc = mb_shell_parse_u32_arg(argv[2], &boot_delay);
	if (rc != 0) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	cfg.enabled = enabled;
	cfg.boot_delay = boot_delay;
	rc = meshbus_llext_config_set(&cfg);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	shell_warn(sh, "configuration changes take effect after reboot");
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

static int cmd_llext_service_list(const struct shell *sh, size_t argc, char **argv)
{
	size_t cnt;
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = meshbus_llext_service_count(&cnt);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	shell_print(sh, "services: %u", (unsigned int)cnt);
	for (size_t i = 0; i < cnt; i++) {
		struct meshbus_llext_service_info info;

		rc = meshbus_llext_service_get(i, &info);
		if (rc != 0) {
			mb_shell_error(sh, rc);
			return rc;
		}
		print_service(sh, &info);
	}

	return 0;
}

static int cmd_llext_service_status(const struct shell *sh, size_t argc, char **argv)
{
	struct meshbus_llext_service_info info;
	int rc;

	if (argc != 2U) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	rc = meshbus_llext_service_status(argv[1], &info);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	print_service(sh, &info);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(meshbus_llext_service_subcmds,
	SHELL_CMD(list, NULL, LLEXT_HELP_LIST, cmd_llext_service_list),
	SHELL_CMD(status, NULL, LLEXT_HELP_STATUS, cmd_llext_service_status),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(meshbus_llext_config_subcmds,
	SHELL_CMD_ARG(get, NULL, LLEXT_HELP_CONFIG_GET, cmd_llext_config_get, 1, 0),
	SHELL_CMD_ARG(set, NULL, LLEXT_HELP_CONFIG_SET, cmd_llext_config_set, 3, 0),
	SHELL_CMD_ARG(reset, NULL, LLEXT_HELP_CONFIG_RESET, cmd_llext_config_reset, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_SET_CREATE(meshbus_llext_subcmds, (meshbus, llext));

SHELL_SUBCMD_ADD((meshbus, llext), service, &meshbus_llext_service_subcmds,
		 LLEXT_HELP_SERVICE, NULL, 0, 0);
SHELL_SUBCMD_ADD((meshbus, llext), config, &meshbus_llext_config_subcmds,
		 LLEXT_HELP_CONFIG, NULL, 0, 0);

SHELL_SUBCMD_ADD((meshbus), llext, &meshbus_llext_subcmds, LLEXT_HELP_ROOT, NULL,
		 0, 0);
