// SPDX-License-Identifier: Apache-2.0

#include <errno.h>

#include <llext/llext.h>
#include <zephyr/shell/shell.h>
#include <zui/zui.h>

#include "mbs_shell_internal.h"

#define DESKTOP_HELP_ROOT SHELL_HELP("Desktop service commands", NULL)
#define DESKTOP_HELP_STATUS SHELL_HELP("Print desktop(ZUI) status", NULL)

static int cmd_desktop_status(const struct shell *sh, size_t argc, char **argv)
{
	struct zui_runtime_stats stats = {0};
	int ret;

	ARG_UNUSED(argv);

	if (argc != 1U) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}

	ret = zui_get_runtime_stats(&stats);
	if (ret != 0) {
		return ret;
	}

	if (!stats.heap_stats_available) {
		shell_error(sh, "heap stats unavailable");
		return -ENOTSUP;
	}

	shell_print(sh, "free:           %u", (unsigned int)stats.heap_free_bytes);
	shell_print(sh, "allocated:      %u", (unsigned int)stats.heap_allocated_bytes);
	shell_print(sh, "max. allocated: %u", (unsigned int)stats.heap_max_allocated_bytes);
	return 0;
}

SHELL_SUBCMD_SET_CREATE(mbs_desktop_subcmds, (meshbus, desktop));

SHELL_SUBCMD_ADD((meshbus, desktop), status, NULL, DESKTOP_HELP_STATUS, cmd_desktop_status, 1,
		 0);
SHELL_SUBCMD_ADD((meshbus), desktop, &mbs_desktop_subcmds, DESKTOP_HELP_ROOT, NULL, 0, 0);
