/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/fs/fs.h>

#include <dfu/flash_delta.h>

/*
 * File-based patch read context and callback
 */

struct file_read_ctx {
	struct fs_file_t file;
};

static int file_read_cb(void *user_data, uint8_t *buf, size_t size)
{
	struct file_read_ctx *ctx = user_data;
	ssize_t ret;

	ret = fs_read(&ctx->file, buf, size);
	if (ret < 0) {
		return (int)ret;
	}

	if ((size_t)ret != size) {
		return -ENODATA;
	}

	return 0;
}

static const char *delta_error_reason(int ret)
{
	switch (ret) {
	case -ENODATA:
		return "short read";
	case -EINVAL:
		return "invalid patch";
	case -ENOSPC:
		return "slot1 out of space";
	case -ENOTSUP:
		return "version mismatch";
	case -EILSEQ:
		return "image verification failed";
	case -EIO:
		return "I/O error";
	default:
		return "error";
	}
}

/*
 * delta apply fs <path>
 */
static int cmd_apply_fs(const struct shell *sh, size_t argc, char **argv)
{
	const char *path = argv[1];
	struct file_read_ctx ctx;
	struct fs_dirent entry;
	int ret;

	ARG_UNUSED(argc);

	ret = fs_stat(path, &entry);
	if (ret < 0) {
		shell_error(sh, "File not found: %s (%d)", path, ret);
		return ret;
	}

	if (entry.type != FS_DIR_ENTRY_FILE) {
		shell_error(sh, "Not a file: %s", path);
		return -EINVAL;
	}

	shell_print(sh, "Applying patch: %s (%zu bytes)", path, entry.size);

	fs_file_t_init(&ctx.file);
	ret = fs_open(&ctx.file, path, FS_O_READ);
	if (ret < 0) {
		shell_error(sh, "Failed to open: %d", ret);
		return ret;
	}

	ret = flash_delta_patch_apply(file_read_cb, &ctx, entry.size);
	fs_close(&ctx.file);

	if (ret == 0) {
		shell_print(sh, "Success. Reboot to apply.");
	} else {
		shell_error(sh, "Failed: %s (%d)", delta_error_reason(ret), ret);
	}

	return ret;
}

/*
 * delta info fs <path>
 */
static int cmd_info_fs(const struct shell *sh, size_t argc, char **argv)
{
	const char *path = argv[1];
	struct file_read_ctx ctx;
	struct fs_dirent entry;
	struct flash_delta_patch_header hdr;
	int ret;

	ARG_UNUSED(argc);

	ret = fs_stat(path, &entry);
	if (ret < 0) {
		shell_error(sh, "File not found: %s (%d)", path, ret);
		return ret;
	}

	if (entry.type != FS_DIR_ENTRY_FILE) {
		shell_error(sh, "Not a file: %s", path);
		return -EINVAL;
	}

	fs_file_t_init(&ctx.file);
	ret = fs_open(&ctx.file, path, FS_O_READ);
	if (ret < 0) {
		shell_error(sh, "Failed to open: %s (%d)", path, ret);
		return ret;
	}

	ret = flash_delta_patch_info(file_read_cb, &ctx, &hdr);
	fs_close(&ctx.file);

	if (ret < 0) {
		shell_error(sh, "Failed to read header: %s (%d)", delta_error_reason(ret), ret);
		return ret;
	}

	shell_print(sh, "Patch: v%u.%u.%u -> v%u.%u.%u", hdr.source.major, hdr.source.minor,
		    hdr.source.revision, hdr.target.major, hdr.target.minor, hdr.target.revision);
	shell_print(sh, "Size: %u bytes", hdr.patch_size);

	return 0;
}

/*
 * Shell command structure:
 *   delta apply fs <path>
 *   delta info fs <path>
 */

/* delta apply [source] */
SHELL_STATIC_SUBCMD_SET_CREATE(apply_cmds,
			       SHELL_CMD_ARG(fs, NULL,
					     "Apply patch from filesystem\n"
					     "Usage: delta apply fs <path>",
					     cmd_apply_fs, 2, 0),
			       /* Future: flash, ble, lora, etc. */
			       SHELL_SUBCMD_SET_END);

/* delta info [source] */
SHELL_STATIC_SUBCMD_SET_CREATE(info_cmds,
			       SHELL_CMD_ARG(fs, NULL,
					     "Show patch info from filesystem\n"
					     "Usage: delta info fs <path>",
					     cmd_info_fs, 2, 0),
			       /* Future: flash, ble, lora, etc. */
			       SHELL_SUBCMD_SET_END);

/* delta [command] */
SHELL_STATIC_SUBCMD_SET_CREATE(delta_cmds, SHELL_CMD(apply, &apply_cmds, "Apply delta patch", NULL),
			       SHELL_CMD(info, &info_cmds, "Show delta patch info", NULL),
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(delta, &delta_cmds, "Delta firmware update", NULL);
