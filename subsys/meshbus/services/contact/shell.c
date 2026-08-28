/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/meshbus/contact.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include "common/shell.h"

#define CONTACT_PREFIX_BYTES CONFIG_MESHBUS_CONTACT_PREFIX_BYTES

static int hex_nibble(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}

	return -EINVAL;
}

static int parse_hex_bytes(const char *arg, uint8_t *out, size_t out_len)
{
	if (arg == NULL || out == NULL || strlen(arg) != out_len * 2U) {
		return -EINVAL;
	}

	for (size_t i = 0; i < out_len; i++) {
		int hi = hex_nibble(arg[i * 2U]);
		int lo = hex_nibble(arg[i * 2U + 1U]);

		if (hi < 0 || lo < 0) {
			return -EINVAL;
		}
		out[i] = (uint8_t)((hi << 4) | lo);
	}

	return 0;
}

static int parse_hex_bytes_variable(const char *arg, uint8_t *out, size_t out_size,
				    size_t *out_len)
{
	size_t arg_len;
	size_t len;

	if (arg == NULL || out == NULL || out_len == NULL) {
		return -EINVAL;
	}
	if (strcmp(arg, "-") == 0) {
		*out_len = 0U;
		return 0;
	}

	arg_len = strlen(arg);
	if ((arg_len % 2U) != 0U) {
		return -EINVAL;
	}

	len = arg_len / 2U;
	if (len > out_size) {
		return -EINVAL;
	}

	for (size_t i = 0; i < len; i++) {
		int hi = hex_nibble(arg[i * 2U]);
		int lo = hex_nibble(arg[i * 2U + 1U]);

		if (hi < 0 || lo < 0) {
			return -EINVAL;
		}
		out[i] = (uint8_t)((hi << 4) | lo);
	}

	*out_len = len;
	return 0;
}

static const char *role_str(meshbus_contact_role role)
{
	switch (role) {
	case MESHBUS_CONTACT_ROLE_CHAT:
		return "chat";
	case MESHBUS_CONTACT_ROLE_REPEATER:
		return "repeater";
	case MESHBUS_CONTACT_ROLE_ROOM:
		return "room";
	case MESHBUS_CONTACT_ROLE_SENSOR:
		return "sensor";
	default:
		return "unknown";
	}
}

static int parse_role_arg(const char *arg, meshbus_contact_role *role)
{
	uint32_t value;

	if (arg == NULL || role == NULL) {
		return -EINVAL;
	}
	if (strcmp(arg, "chat") == 0) {
		*role = MESHBUS_CONTACT_ROLE_CHAT;
		return 0;
	}
	if (strcmp(arg, "repeater") == 0) {
		*role = MESHBUS_CONTACT_ROLE_REPEATER;
		return 0;
	}
	if (strcmp(arg, "room") == 0) {
		*role = MESHBUS_CONTACT_ROLE_ROOM;
		return 0;
	}
	if (strcmp(arg, "sensor") == 0) {
		*role = MESHBUS_CONTACT_ROLE_SENSOR;
		return 0;
	}

	if (mb_shell_parse_u32_arg(arg, &value) != 0) {
		return -EINVAL;
	}

	switch (value) {
	case MESHBUS_CONTACT_ROLE_CHAT:
	case MESHBUS_CONTACT_ROLE_REPEATER:
	case MESHBUS_CONTACT_ROLE_ROOM:
	case MESHBUS_CONTACT_ROLE_SENSOR:
		*role = (meshbus_contact_role)value;
		return 0;
	default:
		return -EINVAL;
	}
}

static void print_hex(const struct shell *sh, const uint8_t *bytes, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		shell_fprintf(sh, SHELL_NORMAL, "%02x", bytes[i]);
	}
}

static void print_contact(const struct shell *sh, size_t index, const meshbus_contact *contact)
{
	if (contact == NULL) {
		return;
	}

	shell_print(sh, "Contact:");
	shell_print(sh, "  index:             %u", (unsigned int)index);
	shell_fprintf(sh, SHELL_NORMAL, "  public_key:        ");
	print_hex(sh, contact->public_key.bytes, contact->public_key.size);
	shell_fprintf(sh, SHELL_NORMAL, "\n");
	shell_print(sh, "  name:              %s", contact->name);
	shell_print(sh, "  alias:             %s", contact->alias);
	shell_print(sh, "  role:              %s (%u)", role_str(contact->role),
		    (unsigned int)contact->role);
	shell_print(sh, "  first_seen:        %u", (unsigned int)contact->first_seen_timestamp);
	shell_print(sh, "  last_seen:         %u", (unsigned int)contact->last_seen_timestamp);
	shell_print(sh, "  last_seen_snr_q4:  %d", (int)contact->last_seen_snr);
	shell_print(sh, "  latitude:          %d", (int)contact->latitude);
	shell_print(sh, "  longitude:         %d", (int)contact->longitude);
	shell_print(sh, "  flags:             0x%08x", (unsigned int)contact->flags);
	shell_print(sh, "  is_neighbor:       %s", contact->is_neighbor ? "yes" : "no");
	shell_print(sh, "  path_hash_size:    %u", (unsigned int)contact->path_hash_size);
	shell_print(sh, "  out_path_len:      %u", (unsigned int)contact->out_path.size);
	shell_fprintf(sh, SHELL_NORMAL, "  out_path:          ");
	if (contact->out_path.size > 0U) {
		print_hex(sh, contact->out_path.bytes, contact->out_path.size);
	} else {
		shell_fprintf(sh, SHELL_NORMAL, "unset");
	}
	shell_fprintf(sh, SHELL_NORMAL, "\n");
	shell_print(sh, "  management_password: %s",
		    contact->management_secret.size > 0U ? "set" : "unset");
}

static int cmd_contact_count(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "%u/%u", (unsigned int)meshbus_contact_store_count(),
		    (unsigned int)meshbus_contact_store_size());
	return 0;
}

static int cmd_contact_size(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "%u", (unsigned int)meshbus_contact_store_size());
	return 0;
}

static int cmd_contact_get(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t index;
	meshbus_contact contact = meshbus_Contact_init_zero;
	int rc;

	ARG_UNUSED(argc);

	rc = mb_shell_parse_u32_arg(argv[1], &index);
	if (rc != 0) {
		mb_shell_invalid(sh);
		return rc;
	}

	rc = meshbus_contact_get((size_t)index, &contact);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	print_contact(sh, (size_t)index, &contact);
	return 0;
}

static int cmd_contact_find_prefix(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t prefix[CONTACT_PREFIX_BYTES];
	meshbus_contact contact = meshbus_Contact_init_zero;
	int rc;

	ARG_UNUSED(argc);

	rc = parse_hex_bytes(argv[1], prefix, sizeof(prefix));
	if (rc != 0) {
		mb_shell_invalid(sh);
		return rc;
	}

	rc = meshbus_contact_find_by_prefix(prefix, &contact);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	print_contact(sh, 0U, &contact);
	return 0;
}

static int cmd_contact_find_key(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t public_key[MESHBUS_CONTACT_PUBLIC_KEY_SIZE];
	meshbus_contact contact = meshbus_Contact_init_zero;
	int rc;

	ARG_UNUSED(argc);

	rc = parse_hex_bytes(argv[1], public_key, sizeof(public_key));
	if (rc != 0) {
		mb_shell_invalid(sh);
		return rc;
	}

	rc = meshbus_contact_find_by_key(public_key, &contact);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	print_contact(sh, 0U, &contact);
	return 0;
}

static int cmd_contact_add(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t public_key[MESHBUS_CONTACT_PUBLIC_KEY_SIZE];
	meshbus_contact_role role;
	int rc;

	ARG_UNUSED(argc);

	if (argv[2] == NULL || argv[2][0] == '\0' ||
	    parse_hex_bytes(argv[1], public_key, sizeof(public_key)) != 0 ||
	    parse_role_arg(argv[3], &role) != 0) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	rc = meshbus_contact_insert(public_key, argv[2], role);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	shell_print(sh, "ok %u/%u", (unsigned int)meshbus_contact_store_count(),
		    (unsigned int)meshbus_contact_store_size());
	return 0;
}

static int cmd_contact_set(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_contact contact = meshbus_Contact_init_zero;
	uint8_t prefix[CONTACT_PREFIX_BYTES];
	int rc;

	if (argc < 4U) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	rc = parse_hex_bytes(argv[1], prefix, sizeof(prefix));
	if (rc != 0) {
		mb_shell_invalid(sh);
		return rc;
	}

	rc = meshbus_contact_find_by_prefix(prefix, &contact);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	if (strcmp(argv[2], "alias") == 0) {
		if (argc != 4U) {
			mb_shell_invalid(sh);
			return -EINVAL;
		}
		memset(contact.alias, 0, sizeof(contact.alias));
		strncpy(contact.alias, argv[3], sizeof(contact.alias) - 1U);
	} else if (strcmp(argv[2], "out_path") == 0) {
		uint8_t path_hash_size;
		bool is_neighbor;
		size_t out_path_len = 0U;

		if (argc != 6U ||
		    mb_shell_parse_u8_arg(argv[3], &path_hash_size) != 0 ||
		    mb_shell_parse_bool_arg(argv[4], &is_neighbor) != 0) {
			mb_shell_invalid(sh);
			return -EINVAL;
		}

		memset(contact.out_path.bytes, 0, sizeof(contact.out_path.bytes));
		if (parse_hex_bytes_variable(argv[5], contact.out_path.bytes,
					     sizeof(contact.out_path.bytes),
					     &out_path_len) != 0 ||
		    out_path_len > UINT8_MAX ||
		    path_hash_size > MESHBUS_CONTACT_PATH_HASH_SIZE_MAX ||
		    (out_path_len > 0U && path_hash_size == 0U) ||
		    (path_hash_size > 0U && (out_path_len % path_hash_size) != 0U)) {
			mb_shell_invalid(sh);
			return -EINVAL;
		}

		contact.path_hash_size = path_hash_size;
		contact.is_neighbor = is_neighbor;
		contact.out_path.size = (pb_size_t)out_path_len;
	} else if (strcmp(argv[2], "management_password") == 0) {
		size_t password_len = strlen(argv[3]);

		if (argc != 4U) {
			mb_shell_invalid(sh);
			return -EINVAL;
		}

		memset(&contact.management_secret, 0, sizeof(contact.management_secret));
		if (strcmp(argv[3], "--clear") != 0) {
			if (password_len < MESHBUS_CONTACT_MANAGEMENT_SECRET_MIN_LEN ||
			    password_len > MESHBUS_CONTACT_MANAGEMENT_SECRET_MAX_LEN) {
				mb_shell_invalid(sh);
				return -EINVAL;
			}
			contact.management_secret.size = (pb_size_t)password_len;
			memcpy(contact.management_secret.bytes, argv[3], password_len);
		}
	} else if (strcmp(argv[2], "position") == 0) {
		int32_t latitude;
		int32_t longitude;

		if (argc != 5U ||
		    mb_shell_parse_i32_arg(argv[3], &latitude) != 0 ||
		    mb_shell_parse_i32_arg(argv[4], &longitude) != 0 ||
		    latitude < -90000000 || latitude > 90000000 ||
		    longitude < -180000000 || longitude > 180000000) {
			mb_shell_invalid(sh);
			return -EINVAL;
		}

		contact.latitude = latitude;
		contact.longitude = longitude;
	} else {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	rc = meshbus_contact_set(contact.public_key.bytes, &contact);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	shell_print(sh, "ok");
	return 0;
}

static int cmd_contact_reset(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t prefix[CONTACT_PREFIX_BYTES];
	int rc;

	ARG_UNUSED(argc);

	rc = parse_hex_bytes(argv[1], prefix, sizeof(prefix));
	if (rc != 0) {
		mb_shell_invalid(sh);
		return rc;
	}

	rc = meshbus_contact_reset(prefix);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	shell_print(sh, "ok");
	return 0;
}

static int parse_prefix_and_request(const struct shell *sh, const char *arg,
				    int (*request)(const uint8_t *prefix, uint32_t *tag))
{
	uint8_t prefix[CONTACT_PREFIX_BYTES];
	uint32_t tag = 0U;
	int rc = parse_hex_bytes(arg, prefix, sizeof(prefix));

	if (rc != 0) {
		mb_shell_invalid(sh);
		return rc;
	}

	rc = request(prefix, &tag);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	shell_print(sh, "accepted tag=%u", (unsigned int)tag);
	return 0;
}

static int cmd_contact_discover_path(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	return parse_prefix_and_request(sh, argv[1], meshbus_contact_discover_path_request);
}

static int cmd_contact_trace_path(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	return parse_prefix_and_request(sh, argv[1], meshbus_contact_trace_path_request);
}

static int cmd_contact_telemetry(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	return parse_prefix_and_request(sh, argv[1], meshbus_contact_telemetry_request);
}

static int cmd_contact_share(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t prefix[CONTACT_PREFIX_BYTES];
	int rc;

	ARG_UNUSED(argc);

	rc = parse_hex_bytes(argv[1], prefix, sizeof(prefix));
	if (rc != 0) {
		mb_shell_invalid(sh);
		return rc;
	}

	rc = meshbus_contact_share_request(prefix);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	shell_print(sh, "accepted");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(meshbus_contact_cmds,
	SHELL_CMD_ARG(add, NULL,
		      "Add contact: <full_public_key_hex> <name> <chat|repeater|room|sensor>",
		      cmd_contact_add, 4, 0),
	SHELL_CMD_ARG(count, NULL, "Show current contact count/capacity",
		      cmd_contact_count, 1, 0),
	SHELL_CMD_ARG(size, NULL, "Show configured contact capacity",
		      cmd_contact_size, 1, 0),
	SHELL_CMD_ARG(get, NULL, "Get contact by slot index",
		      cmd_contact_get, 2, 0),
	SHELL_CMD_ARG(find_prefix, NULL, "Find contact by public-key prefix",
		      cmd_contact_find_prefix, 2, 0),
	SHELL_CMD_ARG(find_key, NULL, "Find contact by full public key",
		      cmd_contact_find_key, 2, 0),
	SHELL_CMD_ARG(set, NULL,
		      "Set contact field: <prefix> alias <value> | <prefix> out_path <path_hash_size> <is_neighbor> <out_path_hex|-> | <prefix> management_password <password|--clear> | <prefix> position <latitude> <longitude>",
		      cmd_contact_set, 4, 2),
	SHELL_CMD_ARG(reset, NULL, "Reset contact by public-key prefix",
		      cmd_contact_reset, 2, 0),
	SHELL_CMD_ARG(share, NULL, "Share cached contact advert by prefix",
		      cmd_contact_share, 2, 0),
	SHELL_CMD_ARG(discover_path, NULL, "Request discover_path by prefix",
		      cmd_contact_discover_path, 2, 0),
	SHELL_CMD_ARG(trace_path, NULL, "Request trace_path by prefix",
		      cmd_contact_trace_path, 2, 0),
	SHELL_CMD_ARG(telemetry, NULL, "Request telemetry by prefix",
		      cmd_contact_telemetry, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((meshbus), contact, &meshbus_contact_cmds, "Contact management",
		 NULL, 0, 0);
