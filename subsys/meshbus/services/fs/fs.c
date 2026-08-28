/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/meshbus/fs.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_FILE_SYSTEM)
#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>
#endif

#if defined(CONFIG_MESHBUS_LLEXT)
#include <zephyr/meshbus/llext.h>
#endif

LOG_MODULE_REGISTER(meshbus_fs, CONFIG_MESHBUS_FS_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* Known Volumes                                                              */
/* -------------------------------------------------------------------------- */

#if defined(CONFIG_FILE_SYSTEM)
#define MESHBUS_FS_EXTRA_NODE DT_PATH(fstab, extra)
#if DT_NODE_EXISTS(MESHBUS_FS_EXTRA_NODE)
FS_FSTAB_DECLARE_ENTRY(MESHBUS_FS_EXTRA_NODE);
#define MESHBUS_FS_HAS_EXTRA_FSTAB 1
#else
#define MESHBUS_FS_HAS_EXTRA_FSTAB 0
#endif
#endif

static const char *const product_dirs[] = {
	MESHBUS_FS_APPS_PATH,
	MESHBUS_FS_GAMES_PATH,
	MESHBUS_FS_SVCS_PATH,
};

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static void cstr_copy(char *dst, size_t dst_size, const char *src)
{
	size_t n;

	if (dst == NULL || dst_size == 0U) {
		return;
	}
	if (src == NULL) {
		dst[0] = '\0';
		return;
	}

	n = strnlen(src, dst_size - 1U);
	memcpy(dst, src, n);
	dst[n] = '\0';
}

static bool root_allowed(const char *path)
{
	size_t root_len = strlen(MESHBUS_FS_EXTRA_MOUNT_POINT);

	return strcmp(path, MESHBUS_FS_EXTRA_MOUNT_POINT) == 0 ||
	       (strncmp(path, MESHBUS_FS_EXTRA_MOUNT_POINT, root_len) == 0 &&
		path[root_len] == '/');
}

static bool managed_dir_protected(const char *path)
{
	return strcmp(path, MESHBUS_FS_EXTRA_MOUNT_POINT) == 0 ||
	       strcmp(path, MESHBUS_FS_APPS_PATH) == 0 ||
	       strcmp(path, MESHBUS_FS_GAMES_PATH) == 0 ||
	       strcmp(path, MESHBUS_FS_SVCS_PATH) == 0;
}

static const char *path_basename(const char *path)
{
	const char *slash;

	if (path == NULL) {
		return "";
	}

	slash = strrchr(path, '/');
	return slash == NULL ? path : slash + 1;
}

#if defined(CONFIG_FILE_SYSTEM)
struct meshbus_fs_list_scratch {
	char normalized[MESHBUS_FS_PATH_MAX_LEN + 1];
	struct fs_dirent dirent;
	struct meshbus_fs_entry entry;
};

static int entry_type_from_fs(enum fs_dir_entry_type type, enum meshbus_fs_entry_type *out)
{
	if (out == NULL) {
		return -EINVAL;
	}

	switch (type) {
	case FS_DIR_ENTRY_FILE:
		*out = MESHBUS_FS_ENTRY_FILE;
		return 0;
	case FS_DIR_ENTRY_DIR:
		*out = MESHBUS_FS_ENTRY_DIR;
		return 0;
	default:
		return -EINVAL;
	}
}

static int volume_extra_status(struct meshbus_fs_volume_status *status)
{
	struct fs_statvfs stat;
	int rc;

	if (status == NULL) {
		return -EINVAL;
	}

	memset(status, 0, sizeof(*status));
	cstr_copy(status->id, sizeof(status->id), "extra");
	cstr_copy(status->mount_point, sizeof(status->mount_point), MESHBUS_FS_EXTRA_MOUNT_POINT);

	rc = fs_statvfs(MESHBUS_FS_EXTRA_MOUNT_POINT, &stat);
	if (rc != 0) {
		status->mounted = false;
		return 0;
	}

	status->mounted = true;
	status->has_capacity = true;
	status->total_bytes = (uint64_t)stat.f_frsize * (uint64_t)stat.f_blocks;
	status->free_bytes = (uint64_t)stat.f_frsize * (uint64_t)stat.f_bfree;
	return 0;
}

#if defined(CONFIG_MESHBUS_FS_FORMAT)
static struct fs_mount_t *volume_extra_mount(void)
{
#if MESHBUS_FS_HAS_EXTRA_FSTAB
	return &FS_FSTAB_ENTRY(MESHBUS_FS_EXTRA_NODE);
#else
	return NULL;
#endif
}
#endif
#endif

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

int meshbus_fs_path_normalize(const char *path, char *out, size_t out_size)
{
	size_t path_len;
	size_t out_len = 0U;
	bool last_was_slash = false;

	if (path == NULL || out == NULL || out_size == 0U) {
		return -EINVAL;
	}

	path_len = strnlen(path, MESHBUS_FS_PATH_MAX_LEN + 2U);
	if (path_len == 0U || path_len > MESHBUS_FS_PATH_MAX_LEN || path[0] != '/') {
		return (path_len > MESHBUS_FS_PATH_MAX_LEN) ? -ENAMETOOLONG : -EINVAL;
	}
	if (out_size <= path_len) {
		return -ENAMETOOLONG;
	}

	for (size_t i = 0U; i < path_len;) {
		size_t seg_start;
		size_t seg_len;

		if (path[i] == '\\') {
			return -EINVAL;
		}
		if (path[i] == '/') {
			if (!last_was_slash) {
				if (out_len >= out_size - 1U) {
					return -ENAMETOOLONG;
				}
				out[out_len++] = '/';
				last_was_slash = true;
			}
			i++;
			continue;
		}

		seg_start = i;
		while (i < path_len && path[i] != '/') {
			if (path[i] == '\\') {
				return -EINVAL;
			}
			i++;
		}
		seg_len = i - seg_start;
		if ((seg_len == 1U && path[seg_start] == '.') ||
		    (seg_len == 2U && path[seg_start] == '.' && path[seg_start + 1U] == '.')) {
			return -EINVAL;
		}
		if (out_len + seg_len >= out_size) {
			return -ENAMETOOLONG;
		}
		memcpy(&out[out_len], &path[seg_start], seg_len);
		out_len += seg_len;
		last_was_slash = false;
	}

	if (out_len > 1U && out[out_len - 1U] == '/') {
		out_len--;
	}
	out[out_len] = '\0';

	if (!root_allowed(out)) {
		return -EACCES;
	}

	return 0;
}

int meshbus_fs_volume_status(const char *volume_id, struct meshbus_fs_volume_status *status)
{
#if defined(CONFIG_FILE_SYSTEM)
	if (volume_id == NULL || status == NULL) {
		return -EINVAL;
	}
	if (strcmp(volume_id, "extra") != 0) {
		return -ENOENT;
	}

	return volume_extra_status(status);
#else
	ARG_UNUSED(volume_id);
	ARG_UNUSED(status);
	return -ENOTSUP;
#endif
}

int meshbus_fs_mkdir(const char *path)
{
#if defined(CONFIG_FILE_SYSTEM)
	char normalized[MESHBUS_FS_PATH_MAX_LEN + 1];
	struct fs_dirent entry;
	int rc;

	rc = meshbus_fs_path_normalize(path, normalized, sizeof(normalized));
	if (rc != 0) {
		return rc;
	}

	rc = fs_stat(normalized, &entry);
	if (rc == 0) {
		return (entry.type == FS_DIR_ENTRY_DIR) ? 0 : -EEXIST;
	}
	if (rc != -ENOENT) {
		return rc;
	}

	rc = fs_mkdir(normalized);
	return (rc == -EEXIST) ? 0 : rc;
#else
	ARG_UNUSED(path);
	return -ENOTSUP;
#endif
}

int meshbus_fs_ensure_product_dirs(void)
{
#if defined(CONFIG_FILE_SYSTEM)
	int rc;

	for (size_t i = 0U; i < ARRAY_SIZE(product_dirs); i++) {
		rc = meshbus_fs_mkdir(product_dirs[i]);
		if (rc != 0) {
			return rc;
		}
	}

	return 0;
#else
	return -ENOTSUP;
#endif
}

int meshbus_fs_stat(const char *path, struct meshbus_fs_entry *entry)
{
#if defined(CONFIG_FILE_SYSTEM)
	char normalized[MESHBUS_FS_PATH_MAX_LEN + 1];
	struct fs_dirent stat;
	enum meshbus_fs_entry_type type;
	const char *name;
	int rc;

	if (entry == NULL) {
		return -EINVAL;
	}

	rc = meshbus_fs_path_normalize(path, normalized, sizeof(normalized));
	if (rc != 0) {
		return rc;
	}

	rc = fs_stat(normalized, &stat);
	if (rc != 0) {
		return rc;
	}
	rc = entry_type_from_fs(stat.type, &type);
	if (rc != 0) {
		return rc;
	}

	name = path_basename(normalized);
	if (strnlen(name, MESHBUS_FS_NAME_MAX_LEN + 1U) > MESHBUS_FS_NAME_MAX_LEN) {
		return -ENAMETOOLONG;
	}

	memset(entry, 0, sizeof(*entry));
	cstr_copy(entry->path, sizeof(entry->path), normalized);
	cstr_copy(entry->name, sizeof(entry->name), name);
	entry->type = type;
	entry->size = stat.size;
	return 0;
#else
	ARG_UNUSED(path);
	ARG_UNUSED(entry);
	return -ENOTSUP;
#endif
}

int meshbus_fs_list(const char *path, size_t offset, size_t limit,
		    meshbus_fs_list_cb cb, void *user_data,
		    size_t *total_count, size_t *next_offset)
{
#if defined(CONFIG_FILE_SYSTEM)
	struct meshbus_fs_list_scratch *scratch;
	struct fs_dir_t dir;
	size_t total = 0U;
	size_t returned = 0U;
	int rc = -ENOMEM;

	if (limit > 0U && cb == NULL) {
		return -EINVAL;
	}
	if (total_count != NULL) {
		*total_count = 0U;
	}
	if (next_offset != NULL) {
		*next_offset = 0U;
	}

	/* Keep the directory and entry snapshots off potentially constrained caller stacks. */
	scratch = k_malloc(sizeof(*scratch));
	if (scratch == NULL) {
		return rc;
	}

	rc = meshbus_fs_path_normalize(path, scratch->normalized,
				       sizeof(scratch->normalized));
	if (rc != 0) {
		goto out;
	}

	fs_dir_t_init(&dir);
	rc = fs_opendir(&dir, scratch->normalized);
	if (rc != 0) {
		goto out;
	}

	for (;;) {
		rc = fs_readdir(&dir, &scratch->dirent);
		if (rc != 0 || scratch->dirent.name[0] == '\0') {
			break;
		}

		if (total >= offset && returned < limit) {
			enum meshbus_fs_entry_type type;

			memset(&scratch->entry, 0, sizeof(scratch->entry));
			if (strnlen(scratch->dirent.name, sizeof(scratch->entry.name)) >=
			    sizeof(scratch->entry.name)) {
				rc = -ENAMETOOLONG;
				break;
			}
			rc = entry_type_from_fs(scratch->dirent.type, &type);
			if (rc != 0) {
				break;
			}
			if (snprintk(scratch->entry.path, sizeof(scratch->entry.path), "%s/%s",
				     scratch->normalized, scratch->dirent.name) >=
			    sizeof(scratch->entry.path)) {
				rc = -ENAMETOOLONG;
				break;
			}
			cstr_copy(scratch->entry.name, sizeof(scratch->entry.name),
				  scratch->dirent.name);
			scratch->entry.type = type;
			scratch->entry.size = scratch->dirent.size;

			rc = cb(&scratch->entry, user_data);
			if (rc != 0) {
				break;
			}
			returned++;
		}

		total++;
	}

	(void)fs_closedir(&dir);
	if (rc != 0) {
		goto out;
	}

	if (total_count != NULL) {
		*total_count = total;
	}
	if (next_offset != NULL && offset + returned < total) {
		*next_offset = offset + returned;
	}

out:
	k_free(scratch);
	return rc;
#else
	ARG_UNUSED(path);
	ARG_UNUSED(offset);
	ARG_UNUSED(limit);
	ARG_UNUSED(cb);
	ARG_UNUSED(user_data);
	ARG_UNUSED(total_count);
	ARG_UNUSED(next_offset);
	return -ENOTSUP;
#endif
}

int meshbus_fs_delete(const char *path)
{
#if defined(CONFIG_FILE_SYSTEM)
	char normalized[MESHBUS_FS_PATH_MAX_LEN + 1];
	int rc;

	rc = meshbus_fs_path_normalize(path, normalized, sizeof(normalized));
	if (rc != 0) {
		return rc;
	}
	if (managed_dir_protected(normalized)) {
		return -EACCES;
	}

	return fs_unlink(normalized);
#else
	ARG_UNUSED(path);
	return -ENOTSUP;
#endif
}

int meshbus_fs_format(const char *volume_id, const char *confirm)
{
#if !defined(CONFIG_FILE_SYSTEM)
	ARG_UNUSED(volume_id);
	ARG_UNUSED(confirm);
	return -ENOTSUP;
#else
#if defined(CONFIG_MESHBUS_FS_FORMAT)
	struct meshbus_fs_volume_status status;
	struct fs_mount_t *mp;
	int rc;
#endif

	if (volume_id == NULL || confirm == NULL) {
		return -EINVAL;
	}
	if (strcmp(confirm, MESHBUS_FS_FORMAT_CONFIRM) != 0) {
		return -EACCES;
	}
	if (strcmp(volume_id, "extra") != 0) {
		return -ENOENT;
	}
#if defined(CONFIG_MESHBUS_LLEXT)
	if (meshbus_llext_runtime_busy()) {
		return -EBUSY;
	}
#endif

#if !defined(CONFIG_MESHBUS_FS_FORMAT)
	return -ENOTSUP;
#else
	mp = volume_extra_mount();
	if (mp == NULL) {
		return -ENODEV;
	}

	rc = volume_extra_status(&status);
	if (rc != 0) {
		return rc;
	}
	if (status.mounted) {
		rc = fs_unmount(mp);
		if (rc != 0) {
			return rc;
		}
	}

	rc = fs_mkfs(mp->type, (uintptr_t)mp->storage_dev, mp->fs_data, 0);
	if (rc != 0) {
		return rc;
	}

	rc = fs_mount(mp);
	if (rc != 0 && rc != -EBUSY) {
		return rc;
	}

	return meshbus_fs_ensure_product_dirs();
#endif
#endif
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

static int meshbus_fs_init(void)
{
	int rc;

	rc = meshbus_fs_ensure_product_dirs();
	if (rc == -ENOTSUP || rc == -ENOENT || rc == -ENODEV) {
		LOG_DBG("Meshbus FS unavailable at boot (%d)", rc);
		return 0;
	}
	if (rc != 0) {
		LOG_WRN("Meshbus FS product directory setup failed (%d)", rc);
		return 0;
	}

	LOG_INF("Meshbus FS ready");
	return 0;
}

SYS_INIT(meshbus_fs_init, APPLICATION, CONFIG_MESHBUS_FS_INIT_PRIORITY);
