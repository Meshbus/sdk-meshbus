/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (c) 2026 FoBE Studio
 */

/**
 * @file
 * @brief Meshbus filesystem control API
 */

#ifndef ZEPHYR_INCLUDE_MBS_FS_H_
#define ZEPHYR_INCLUDE_MBS_FS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum Meshbus filesystem path length, without trailing NUL. */
#define MBS_FS_PATH_MAX_LEN 191
/** Maximum filesystem entry name length copied by Meshbus FS APIs. */
#define MBS_FS_NAME_MAX_LEN 63
/** Maximum known volume identifier length, without trailing NUL. */
#define MBS_FS_VOLUME_ID_MAX_LEN 15
/** Managed storage root for Meshbus product files. */
#define MBS_FS_EXTRA_MOUNT_POINT "/extra"
/** Managed Desktop app directory. */
#define MBS_FS_APPS_PATH "/extra/apps"
/** Managed Desktop game app directory. */
#define MBS_FS_GAMES_PATH "/extra/apps/games"
/** Confirmation token required by @ref mbs_fs_format. */
#define MBS_FS_FORMAT_CONFIRM "FORMAT"

/** Meshbus filesystem entry type. */
enum mbs_fs_entry_type {
	/** Regular file entry. */
	MBS_FS_ENTRY_FILE = 1,
	/** Directory entry. */
	MBS_FS_ENTRY_DIR = 2,
};

/** Bounded filesystem entry snapshot. */
struct mbs_fs_entry {
	/** Canonical full path. */
	char path[MBS_FS_PATH_MAX_LEN + 1];
	/** Basename within the parent directory. */
	char name[MBS_FS_NAME_MAX_LEN + 1];
	/** Entry type. */
	enum mbs_fs_entry_type type;
	/** File size in bytes, or 0 for directories. */
	size_t size;
};

/** Known volume status snapshot. */
struct mbs_fs_volume_status {
	/** Stable volume id, for example "extra". */
	char id[MBS_FS_VOLUME_ID_MAX_LEN + 1];
	/** Mount point path. */
	char mount_point[MBS_FS_PATH_MAX_LEN + 1];
	/** True when the volume is currently mounted and statvfs succeeds. */
	bool mounted;
	/** True when total/free byte fields are valid. */
	bool has_capacity;
	/** Total filesystem capacity in bytes when available. */
	uint64_t total_bytes;
	/** Free filesystem capacity in bytes when available. */
	uint64_t free_bytes;
};

/**
 * @brief Directory-list callback.
 *
 * Returning a negative errno stops iteration and propagates that error.
 */
typedef int (*mbs_fs_list_cb)(const struct mbs_fs_entry *entry, void *user_data);

/**
 * @brief Normalize and validate a Meshbus-managed filesystem path.
 *
 * The accepted first-version root is `/extra`. Relative paths, empty paths,
 * `.`/`..` segments, backslashes, and paths outside the allowlist are rejected.
 *
 * @param path Input path.
 * @param out Output canonical path.
 * @param out_size Size of @p out in bytes.
 *
 * @retval 0 on success.
 * @retval -EINVAL if arguments or path content are invalid.
 * @retval -EACCES if the path is outside the Meshbus allowlist.
 * @retval -ENAMETOOLONG if the normalized path cannot fit.
 */
int mbs_fs_path_normalize(const char *path, char *out, size_t out_size);

/**
 * @brief Copy known-volume status.
 *
 * @param volume_id Known volume id, currently "extra".
 * @param status Output status snapshot.
 *
 * @retval 0 on success.
 * @retval -EINVAL if arguments are invalid.
 * @retval -ENOENT if @p volume_id is unknown.
 * @retval -ENOTSUP if filesystem support is unavailable.
 */
int mbs_fs_volume_status(const char *volume_id, struct mbs_fs_volume_status *status);

/**
 * @brief Ensure standard Meshbus product directories exist.
 *
 * This creates `/extra/apps` and `/extra/apps/games`
 * idempotently when the backing filesystem is mounted.
 *
 * @retval 0 on success.
 * @retval negative errno from filesystem access.
 */
int mbs_fs_ensure_product_dirs(void);

/**
 * @brief Create one allowed directory.
 *
 * @retval 0 on success, including already-existing directories.
 * @retval negative errno on validation or filesystem failure.
 */
int mbs_fs_mkdir(const char *path);

/**
 * @brief Stat one allowed file or directory.
 *
 * @param path Path to stat.
 * @param entry Output entry snapshot.
 *
 * @retval 0 on success.
 * @retval negative errno on validation or filesystem failure.
 */
int mbs_fs_stat(const char *path, struct mbs_fs_entry *entry);

/**
 * @brief List one allowed directory with bounded pagination.
 *
 * @param path Directory path.
 * @param offset Number of visible entries to skip.
 * @param limit Maximum entries to report. Zero means report no entries but
 *              still count visible entries.
 * @param cb Callback invoked for each returned entry. May be NULL only when
 *           @p limit is zero.
 * @param user_data Caller-owned callback context.
 * @param total_count Optional output total visible entries.
 * @param next_offset Optional output next offset when another page exists.
 *
 * @retval 0 on success.
 * @retval negative errno on validation, callback, or filesystem failure.
 */
int mbs_fs_list(const char *path, size_t offset, size_t limit,
		    mbs_fs_list_cb cb, void *user_data,
		    size_t *total_count, size_t *next_offset);

/**
 * @brief Delete one allowed file or empty directory.
 *
 * Managed root/product directories themselves cannot be deleted.
 *
 * @retval 0 on success.
 * @retval negative errno on validation or filesystem failure.
 */
int mbs_fs_delete(const char *path);

/**
 * @brief Format and remount a known volume.
 *
 * This is destructive. @p confirm must equal @ref MBS_FS_FORMAT_CONFIRM.
 * Formatting is compiled out unless enabled by the product Kconfig.
 *
 * @retval 0 on success.
 * @retval -EACCES if confirmation is wrong or busy-state gates reject the run.
 * @retval -ENOTSUP if formatting is not enabled or supported.
 * @retval negative errno on filesystem failure.
 */
int mbs_fs_format(const char *volume_id, const char *confirm);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MBS_FS_H_ */
