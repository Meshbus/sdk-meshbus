/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/meshbus/meshcore.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include "common/shell.h"

static const char *role_str(meshbus_meshcore_role role)
{
	switch (role) {
	case MESHBUS_MESHCORE_ROLE_CHAT:
		return "chat";
	case MESHBUS_MESHCORE_ROLE_REPEATER:
		return "repeater";
	case MESHBUS_MESHCORE_ROLE_ROOM:
		return "room";
	case MESHBUS_MESHCORE_ROLE_SENSOR:
		return "sensor";
	default:
		return "unknown";
	}
}

static int parse_hex_arg(const char *arg, uint8_t *out, size_t out_size, size_t *out_len)
{
	size_t arg_len;
	int parsed;

	if (arg == NULL || out == NULL || out_len == NULL) {
		return -EINVAL;
	}

	arg_len = strlen(arg);
	if (arg_len == 0U || (arg_len % 2U) != 0U) {
		return -EINVAL;
	}

	parsed = hex2bin(arg, arg_len, out, out_size);
	if (parsed < 0) {
		return parsed;
	}

	*out_len = (size_t)parsed;
	return 0;
}

static void print_config(const struct shell *sh, const meshbus_meshcore_config *cfg)
{
	if (cfg == NULL) {
		return;
	}

	shell_print(sh, "Settings:");
	shell_print(sh, "  firmware_role:             %s (%u)",
		    role_str(meshbus_meshcore_firmware_role_get()),
		    (unsigned int)meshbus_meshcore_firmware_role_get());
	shell_print(sh, "  name:                      %s", cfg->name);
	shell_print(sh, "  public_key:                %u bytes", (unsigned int)cfg->public_key.size);
	shell_hexdump(sh, cfg->public_key.bytes, cfg->public_key.size);
	shell_print(sh, "  private_key:               %u bytes", (unsigned int)cfg->private_key.size);
	shell_hexdump(sh, cfg->private_key.bytes, cfg->private_key.size);
	shell_print(sh, "  latitude:                  %d", cfg->latitude);
	shell_print(sh, "  longitude:                 %d", cfg->longitude);
	shell_print(sh, "  advert_position:           %s",
		    cfg->advert_position ? "yes" : "no");
	shell_print(sh, "  disable_fwd:               %s", cfg->disable_fwd ? "yes" : "no");
	shell_print(sh, "  flood_max:                 %u", (unsigned int)cfg->flood_max);
	shell_print(sh, "  multi_acks:                %u", (unsigned int)cfg->multi_acks);
	shell_print(sh, "  advert_interval:           %u s",
		    (unsigned int)cfg->advert_interval);
	shell_print(sh, "  flood_advert_interval:     %u s",
		    (unsigned int)cfg->flood_advert_interval);
	shell_print(sh, "  tx_delay_factor:           %f", (double)cfg->tx_delay_factor);
	shell_print(sh, "  direct_tx_delay_factor:    %f",
		    (double)cfg->direct_tx_delay_factor);
	shell_print(sh, "  path_hash_size:            %u",
		    (unsigned int)cfg->path_hash_size);
	shell_print(sh, "  loop_detect:               %u", (unsigned int)cfg->loop_detect);
	shell_print(sh, "  client_repeat:             %s", cfg->client_repeat ? "yes" : "no");
	shell_print(sh, "  add_contact_config:        0x%02x",
		    (unsigned int)cfg->add_contact_config);
	shell_print(sh, "  add_contact_hops_limit:    %u",
		    (unsigned int)cfg->add_contact_hops_limit);
	shell_print(sh, "  telemetry_mode_base:       %u",
		    (unsigned int)cfg->telemetry_mode_base);
	shell_print(sh, "  telemetry_mode_locat:      %u",
		    (unsigned int)cfg->telemetry_mode_locat);
	shell_print(sh, "  telemetry_mode_environment: %u",
		    (unsigned int)cfg->telemetry_mode_environment);
}

static int cmd_meshcore_config_get(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_meshcore_config cfg;
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = meshbus_meshcore_config_get(&cfg);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	print_config(sh, &cfg);
	return 0;
}

static int cmd_meshcore_config_reset(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = meshbus_meshcore_config_reset();
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	shell_print(sh, "ok");
	return 0;
}

static int meshcore_shell_parse_position(const char *latitude_arg, const char *longitude_arg,
					 int32_t *latitude, int32_t *longitude)
{
	if (latitude_arg == NULL || longitude_arg == NULL ||
	    latitude == NULL || longitude == NULL ||
	    mb_shell_parse_i32_arg(latitude_arg, latitude) != 0 ||
	    mb_shell_parse_i32_arg(longitude_arg, longitude) != 0 ||
	    *latitude < -90000000 || *latitude > 90000000 ||
	    *longitude < -180000000 || *longitude > 180000000) {
		return -EINVAL;
	}

	return 0;
}

static int cmd_meshcore_config_set(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_meshcore_config cfg;
	uint32_t flood_max;
	uint32_t multi_acks;
	uint32_t advert_interval;
	uint32_t flood_advert_interval;
	uint32_t add_contact_config;
	uint32_t telemetry_mode_base;
	uint32_t telemetry_mode_locat;
	uint32_t telemetry_mode_environment;
	uint32_t path_hash_size;
	uint32_t loop_detect;
	uint32_t add_contact_hops_limit;
	bool disable_fwd_value;
	bool advert_position_value;
	bool client_repeat_value;
	int rc;

	rc = meshbus_meshcore_config_get(&cfg);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	if (strcmp(argv[1], "position") == 0) {
		if (argc != 4U ||
		    meshcore_shell_parse_position(argv[2], argv[3], &cfg.latitude,
						  &cfg.longitude) != 0) {
			mb_shell_invalid(sh);
			return -EINVAL;
		}
	} else {
		if (argc != 20U ||
		    meshcore_shell_parse_position(argv[2], argv[3], &cfg.latitude,
						  &cfg.longitude) != 0 ||
		    mb_shell_parse_bool_arg(argv[4], &disable_fwd_value) != 0 ||
		    mb_shell_parse_u32_arg(argv[5], &flood_max) != 0 ||
		    mb_shell_parse_float_arg(argv[6], &cfg.tx_delay_factor) != 0 ||
		    mb_shell_parse_float_arg(argv[7], &cfg.direct_tx_delay_factor) != 0 ||
		    mb_shell_parse_u32_arg(argv[8], &multi_acks) != 0 ||
		    mb_shell_parse_u32_arg(argv[9], &advert_interval) != 0 ||
		    mb_shell_parse_u32_arg(argv[10], &flood_advert_interval) != 0 ||
		    mb_shell_parse_bool_arg(argv[11], &advert_position_value) != 0 ||
		    mb_shell_parse_u32_arg(argv[12], &add_contact_config) != 0 ||
		    mb_shell_parse_u32_arg(argv[13], &telemetry_mode_base) != 0 ||
		    mb_shell_parse_u32_arg(argv[14], &telemetry_mode_locat) != 0 ||
		    mb_shell_parse_u32_arg(argv[15], &telemetry_mode_environment) != 0 ||
		    mb_shell_parse_u32_arg(argv[16], &path_hash_size) != 0 ||
		    mb_shell_parse_u32_arg(argv[17], &loop_detect) != 0 ||
		    mb_shell_parse_bool_arg(argv[18], &client_repeat_value) != 0 ||
		    mb_shell_parse_u32_arg(argv[19], &add_contact_hops_limit) != 0) {
			mb_shell_invalid(sh);
			return -EINVAL;
		}

		memset(cfg.name, 0, sizeof(cfg.name));
		strncpy(cfg.name, argv[1], sizeof(cfg.name) - 1U);
		cfg.disable_fwd = disable_fwd_value;
		cfg.flood_max = flood_max;
		cfg.multi_acks = multi_acks;
		cfg.advert_interval = advert_interval;
		cfg.flood_advert_interval = flood_advert_interval;
		cfg.advert_position = advert_position_value;
		cfg.add_contact_config = add_contact_config;
		cfg.telemetry_mode_base =
			(meshbus_MeshcoreConfig_TelemetryMode)telemetry_mode_base;
		cfg.telemetry_mode_locat =
			(meshbus_MeshcoreConfig_TelemetryMode)telemetry_mode_locat;
		cfg.telemetry_mode_environment =
			(meshbus_MeshcoreConfig_TelemetryMode)telemetry_mode_environment;
		cfg.path_hash_size = path_hash_size;
		cfg.loop_detect = (meshbus_MeshcoreConfig_LoopDetect)loop_detect;
		cfg.client_repeat = client_repeat_value;
		cfg.add_contact_hops_limit = add_contact_hops_limit;
	}

	rc = meshbus_meshcore_config_set(&cfg);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	shell_print(sh, "ok");
	return 0;
}

static int cmd_meshcore_advert(const struct shell *sh, size_t argc, char **argv)
{
	bool flood = false;
	int rc;

	if (argc > 1) {
		rc = mb_shell_parse_bool_arg(argv[1], &flood);
		if (rc != 0) {
			mb_shell_invalid(sh);
			return rc;
		}
	}

	rc = meshbus_meshcore_advert_request(flood);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	shell_print(sh, "accepted flood=%u", flood ? 1U : 0U);
	return 0;
}

static int parse_discover_filter(const char *arg, uint8_t *filter)
{
	uint32_t value;
	int rc;

	if (arg == NULL || filter == NULL || strcmp(arg, "all") == 0) {
		*filter = MESHBUS_MESHCORE_DISCOVER_FILTER_ALL;
		return 0;
	}
	if (strcmp(arg, "chat") == 0) {
		*filter = MESHBUS_MESHCORE_DISCOVER_FILTER_CHAT;
		return 0;
	}
	if (strcmp(arg, "repeater") == 0) {
		*filter = MESHBUS_MESHCORE_DISCOVER_FILTER_REPEATER;
		return 0;
	}
	if (strcmp(arg, "room") == 0) {
		*filter = MESHBUS_MESHCORE_DISCOVER_FILTER_ROOM;
		return 0;
	}
	if (strcmp(arg, "sensor") == 0) {
		*filter = MESHBUS_MESHCORE_DISCOVER_FILTER_SENSOR;
		return 0;
	}

	rc = mb_shell_parse_u32_arg(arg, &value);
	if (rc != 0 || value == 0U ||
	    (value & ~MESHBUS_MESHCORE_DISCOVER_FILTER_ALL) != 0U) {
		return -EINVAL;
	}

	*filter = (uint8_t)value;
	return 0;
}

static int cmd_meshcore_node_discover(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t filter = MESHBUS_MESHCORE_DISCOVER_FILTER_ALL;
	uint32_t since = 0U;
	uint32_t tag = 0U;
	int rc;

	if (argc > 1) {
		rc = parse_discover_filter(argv[1], &filter);
		if (rc != 0) {
			mb_shell_invalid(sh);
			return rc;
		}
	}
	if (argc > 2) {
		rc = mb_shell_parse_u32_arg(argv[2], &since);
		if (rc != 0) {
			mb_shell_invalid(sh);
			return rc;
		}
	}

	rc = meshbus_meshcore_node_discover_request(filter, since, &tag);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	shell_print(sh, "accepted tag=%u", (unsigned int)tag);
	return 0;
}

static int cmd_meshcore_trace(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t path[MESHBUS_MESHCORE_PATH_MAX_LEN] = {0};
	size_t path_len = 0U;
	uint8_t path_hash_size = 0U;
	uint32_t tag = 0U;
	int rc;

	ARG_UNUSED(argc);

	rc = mb_shell_parse_u8_arg(argv[1], &path_hash_size);
	if (rc != 0) {
		mb_shell_invalid(sh);
		return rc;
	}
	rc = parse_hex_arg(argv[2], path, sizeof(path), &path_len);
	if (rc != 0 || path_len == 0U || path_len > UINT8_MAX) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	rc = meshbus_meshcore_trace_request(path, (uint8_t)path_len, path_hash_size,
					    &tag);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	shell_print(sh, "accepted tag=%u", (unsigned int)tag);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(meshbus_meshcore_config_cmds,
	SHELL_CMD_ARG(get, NULL, "Show MeshCore config",
		      cmd_meshcore_config_get, 1, 0),
	SHELL_CMD_ARG(reset, NULL, "Reset MeshCore config",
		      cmd_meshcore_config_reset, 1, 0),
	SHELL_CMD_ARG(set, NULL,
		      "Set MeshCore config: <name> <latitude> <longitude> <disable_fwd> <flood_max> <tx_delay_factor> <direct_tx_delay_factor> <multi_acks> <advert_interval> <flood_advert_interval> <advert_position> <add_contact_config> <telemetry_mode_base> <telemetry_mode_locat> <telemetry_mode_environment> <path_hash_size> <loop_detect> <client_repeat> <add_contact_hops_limit> | position <latitude> <longitude>",
		      cmd_meshcore_config_set, 4, 16),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(meshbus_meshcore_request_cmds,
	SHELL_CMD_ARG(advert, NULL, "Request advert [flood]",
		      cmd_meshcore_advert, 1, 1),
	SHELL_CMD_ARG(node_discover, NULL, "Request node discovery [filter] [since]",
		      cmd_meshcore_node_discover, 1, 2),
	SHELL_CMD_ARG(trace, NULL, "Request trace <path_hash_size> <path_hex>",
		      cmd_meshcore_trace, 3, 0),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(meshbus_meshcore_cmds,
	SHELL_CMD(config, &meshbus_meshcore_config_cmds, "MeshCore config", NULL),
	SHELL_CMD(request, &meshbus_meshcore_request_cmds, "MeshCore requests", NULL),
	SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((meshbus), meshcore, &meshbus_meshcore_cmds, "MeshCore management",
		 NULL, 0, 0);
