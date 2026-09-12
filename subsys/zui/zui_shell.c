/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zui/zui.h>

#include <string.h>

#include <display/u8g2_snapshot.h>

#define ZUI_DUMP_LINE_BYTES   64u
#define ZUI_DUMP_HEX_LINE_MAX (5u + (ZUI_DUMP_LINE_BYTES * 2u) + 1u) /* "DUMP " + hex + NUL */

static atomic_t g_key_seq;
static bool g_key_emul_active;

enum zui_shell_key_action {
	ZUI_SHELL_KEY_ACTION_CLICK,
	ZUI_SHELL_KEY_ACTION_LONG_PRESS,
	ZUI_SHELL_KEY_ACTION_PRESS,
	ZUI_SHELL_KEY_ACTION_RELEASE,
};

static const char *zui_shell_key_action_name(enum zui_shell_key_action action)
{
	switch (action) {
	case ZUI_SHELL_KEY_ACTION_CLICK:
		return "click";
	case ZUI_SHELL_KEY_ACTION_LONG_PRESS:
		return "long_press";
	case ZUI_SHELL_KEY_ACTION_PRESS:
		return "press";
	case ZUI_SHELL_KEY_ACTION_RELEASE:
		return "release";
	default:
		return "unknown";
	}
}

static int parse_key(const char *arg, enum zui_input_code *out_code)
{
	if (arg == NULL || out_code == NULL) {
		return -EINVAL;
	}

	if (strcmp(arg, "up") == 0) {
		*out_code = ZUI_INPUT_CODE_UP;
		return 0;
	}
	if (strcmp(arg, "down") == 0) {
		*out_code = ZUI_INPUT_CODE_DOWN;
		return 0;
	}
	if (strcmp(arg, "left") == 0) {
		*out_code = ZUI_INPUT_CODE_LEFT;
		return 0;
	}
	if (strcmp(arg, "right") == 0) {
		*out_code = ZUI_INPUT_CODE_RIGHT;
		return 0;
	}
	if (strcmp(arg, "ok") == 0 || strcmp(arg, "enter") == 0) {
		*out_code = ZUI_INPUT_CODE_SELECT;
		return 0;
	}
	if (strcmp(arg, "back") == 0) {
		*out_code = ZUI_INPUT_CODE_BACK;
		return 0;
	}

	return -EINVAL;
}

static int parse_action(const char *arg, enum zui_shell_key_action *out_action)
{
	if (arg == NULL || out_action == NULL) {
		return -EINVAL;
	}

	if (strcmp(arg, "short") == 0 || strcmp(arg, "click") == 0) {
		*out_action = ZUI_SHELL_KEY_ACTION_CLICK;
		return 0;
	}
	if (strcmp(arg, "long") == 0 || strcmp(arg, "long_press") == 0) {
		*out_action = ZUI_SHELL_KEY_ACTION_LONG_PRESS;
		return 0;
	}
	if (strcmp(arg, "press") == 0) {
		*out_action = ZUI_SHELL_KEY_ACTION_PRESS;
		return 0;
	}
	if (strcmp(arg, "release") == 0) {
		*out_action = ZUI_SHELL_KEY_ACTION_RELEASE;
		return 0;
	}

	return -EINVAL;
}

static int publish_input(enum zui_input_code code, enum zui_input_action action)
{
	struct zui_input_event event = {
		.sequence = (uint32_t)atomic_inc(&g_key_seq) + 1U,
		.code = code,
		.action = action,
		.value = action == ZUI_INPUT_ACTION_RELEASE ? 0 : 1,
	};
	struct zui_host *host = zui_get_default_host();

	if (host != NULL) {
		return zui_host_submit_input(host, &event);
	}

	return -ENODEV;
}

static int publish_shell_input(enum zui_input_code code, enum zui_input_action action)
{
	int ret = publish_input(code, action);

	return ret == -ENODATA ? 0 : ret;
}

static int inject_action(enum zui_input_code code, enum zui_shell_key_action action)
{
	switch (action) {
	case ZUI_SHELL_KEY_ACTION_PRESS:
		return publish_shell_input(code, ZUI_INPUT_ACTION_PRESS);
	case ZUI_SHELL_KEY_ACTION_RELEASE:
		return publish_shell_input(code, ZUI_INPUT_ACTION_RELEASE);
	case ZUI_SHELL_KEY_ACTION_CLICK:
		return publish_shell_input(code, ZUI_INPUT_ACTION_CLICK);
	case ZUI_SHELL_KEY_ACTION_LONG_PRESS:
		return publish_shell_input(code, ZUI_INPUT_ACTION_LONG_PRESS);
	default:
		return -EINVAL;
	}
}

static int emul_key(uint8_t byte, enum zui_input_code *out_code, const char **out_name)
{
	if (out_code == NULL || out_name == NULL) {
		return -EINVAL;
	}

	switch (byte) {
	case 'w':
	case 'W':
		*out_code = ZUI_INPUT_CODE_UP;
		*out_name = "up";
		return 0;
	case 'a':
	case 'A':
		*out_code = ZUI_INPUT_CODE_LEFT;
		*out_name = "left";
		return 0;
	case 's':
	case 'S':
		*out_code = ZUI_INPUT_CODE_DOWN;
		*out_name = "down";
		return 0;
	case 'd':
	case 'D':
		*out_code = ZUI_INPUT_CODE_RIGHT;
		*out_name = "right";
		return 0;
	case 'e':
	case 'E':
		*out_code = ZUI_INPUT_CODE_SELECT;
		*out_name = "ok";
		return 0;
	case 'q':
	case 'Q':
		*out_code = ZUI_INPUT_CODE_BACK;
		*out_name = "back";
		return 0;
	default:
		return -EINVAL;
	}
}

static void zui_key_emul_stop(const struct shell *sh)
{
	g_key_emul_active = false;
	shell_set_bypass(sh, NULL, NULL);
}

static void zui_key_emul_bypass(const struct shell *sh, uint8_t *recv, size_t len,
				void *user_data)
{
	ARG_UNUSED(user_data);

	for (size_t i = 0; i < len; i++) {
		enum zui_input_code code;
		const char *name;
		int ret;

		if (recv[i] == 'r' || recv[i] == 'R') {
			shell_print(sh, "ZUI key emulator stopped");
			zui_key_emul_stop(sh);
			return;
		}

		ret = emul_key(recv[i], &code, &name);
		if (ret != 0) {
			continue;
		}

		ret = inject_action(code, ZUI_SHELL_KEY_ACTION_CLICK);
		if (ret != 0) {
			shell_error(sh, "Failed to inject key input: %d", ret);
			continue;
		}

		shell_print(sh, "Injected: key=%s action=click", name);
	}
}

static int cmd_zui_key(const struct shell *sh, size_t argc, char **argv)
{
	enum zui_input_code code;
	enum zui_shell_key_action action = ZUI_SHELL_KEY_ACTION_CLICK;
	int ret;

	if (argc < 2U || argc > 3U) {
		shell_error(sh, "Usage: zui key <up|down|left|right|ok|enter|back> "
			       "[short|click|long|long_press|press|release]");
		return -EINVAL;
	}

	ret = parse_key(argv[1], &code);
	if (ret != 0) {
		shell_error(sh, "Invalid key: %s", argv[1]);
		shell_error(sh, "Allowed: up down left right ok enter back");
		return -EINVAL;
	}

	if (argc == 3U) {
		ret = parse_action(argv[2], &action);
		if (ret != 0) {
			shell_error(sh, "Invalid action: %s", argv[2]);
			shell_error(sh, "Allowed: short click long long_press press release");
			return -EINVAL;
		}
	}

	ret = inject_action(code, action);
	if (ret != 0) {
		shell_error(sh, "Failed to inject key input: %d", ret);
		return ret;
	}

	shell_print(sh, "Injected: key=%s action=%s", argv[1],
		    zui_shell_key_action_name(action));
	return 0;
}

static int cmd_zui_key_emul(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (g_key_emul_active) {
		shell_error(sh, "ZUI key emulator is already active");
		return -EBUSY;
	}

	g_key_emul_active = true;
	shell_print(sh, "ZUI key emulator started");
	shell_print(sh, "W=UP A=LEFT S=DOWN D=RIGHT E=OK Q=BACK R=QUIT");
	shell_set_bypass(sh, zui_key_emul_bypass, NULL);
	return 0;
}

static int cmd_zui_dump(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	static const char hexdig[] = "0123456789abcdef";

	struct u8g2_snapshot_info info;
	int ret = u8g2_snapshot_copy(NULL, 0, &info);
	if (ret != 0) {
		shell_error(sh, "u8g2 dump unavailable: %d", ret);
		return ret;
	}

	/* Per-call storage also isolates concurrent Shell backends. */
	uint8_t *snapshot = k_malloc(info.len);
	if (snapshot == NULL) {
		return -ENOMEM;
	}
	ret = u8g2_snapshot_copy(snapshot, info.len, &info);
	if (ret != 0) {
		k_free(snapshot);
		shell_error(sh, "u8g2 snapshot unavailable: %d", ret);
		return ret;
	}

	shell_print(sh, "DUMP_DISP %u %u BUF %u FMT SSD1306_PAGE ORI %u",
		    (unsigned)info.width,
		    (unsigned)info.height,
		    (unsigned)info.len,
		    (unsigned)info.orientation);

	for (size_t off = 0; off < info.len; off += ZUI_DUMP_LINE_BYTES) {
		const size_t n = MIN(ZUI_DUMP_LINE_BYTES, info.len - off);
		char line[ZUI_DUMP_HEX_LINE_MAX];

		memcpy(line, "DUMP ", 5);
		for (size_t i = 0; i < n; i++) {
			const uint8_t b = snapshot[off + i];
			line[5 + (i * 2) + 0] = hexdig[b >> 4];
			line[5 + (i * 2) + 1] = hexdig[b & 0x0F];
		}
		line[5 + (n * 2)] = '\0';

		shell_print(sh, "%s", line);
	}

	k_free(snapshot);
	shell_print(sh, "DUMP_END");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	zui_key_subcmds,
	SHELL_CMD_ARG(emul, NULL,
		      "Continuously inject key input from W/A/S/D/E/Q; R exits",
		      cmd_zui_key_emul, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
	zui_subcmds,
	SHELL_CMD_ARG(key, &zui_key_subcmds,
		      "Inject key input: key <up|down|left|right|ok|enter|back> "
		      "[short|click|long|long_press|press|release]; key emul",
		      cmd_zui_key, 2, 1),
	SHELL_CMD_ARG(dump, NULL, "Dump last framebuffer as hex (SSD1306 page format)",
		      cmd_zui_dump, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(zui, &zui_subcmds, "ZUI diagnostic commands", NULL);
