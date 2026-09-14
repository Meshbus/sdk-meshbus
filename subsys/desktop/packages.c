/* SPDX-License-Identifier: Apache-2.0 */
#include "desktop_private.h"
#include <desktop/package.h>
#include <desktop/session.h>
#include <llext/llext.h>
#include <zephyr/llext/llext.h>
#include <zephyr/data/json.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/util.h>
#include <psa/crypto.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define STORE        "/extra/apps/.meshbus"
#define INCOMING     STORE "/incoming.json"
#define PENDING      STORE "/pending.json"
#define GARBAGE      STORE "/garbage.json"
#define MAX_RECORD   32768U
#define MAX_FILES    32U
#define ACTION(name) meshbus_DesktopPackageAction_DESKTOP_PACKAGE_ACTION_##name

struct package_file {
	char *path;
	int32_t length;
	char *sha256;
};
struct package_record {
	char *document;
	int32_t schema;
	char *id;
	char *version;
	char *bundle;
	char *image;
	char *target;
	char *firmware;
	int32_t metadata_version;
	int32_t interface_abi;
	char *mba_path;
	bool atomic;
	int32_t operation; /* 0 install, 1 rollback, 2 uninstall */
	bool purge_saves;
	struct package_file files[MAX_FILES];
	size_t files_count;
	char *requires[128];
	size_t requires_count;
};
static const struct json_obj_descr file_fields[] = {
	JSON_OBJ_DESCR_PRIM(struct package_file, path, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct package_file, length, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct package_file, sha256, JSON_TOK_STRING),
};
static const struct json_obj_descr record_fields[] = {
	JSON_OBJ_DESCR_PRIM(struct package_record, schema, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct package_record, id, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct package_record, version, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct package_record, bundle, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct package_record, image, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct package_record, target, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct package_record, firmware, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct package_record, metadata_version, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct package_record, interface_abi, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct package_record, mba_path, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct package_record, atomic, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct package_record, operation, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct package_record, purge_saves, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_OBJ_ARRAY(struct package_record, files, MAX_FILES, files_count, file_fields,
				 ARRAY_SIZE(file_fields)),
	JSON_OBJ_DESCR_ARRAY(struct package_record, requires, 128, requires_count, JSON_TOK_STRING),
};
static bool valid_id(const char *id)
{
	if (id == NULL || id[0] < 'a' || id[0] > 'z' || strlen(id) > 31U) {
		return false;
	}
	for (const char *p = id; *p != '\0'; ++p) {
		if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' ||
		      *p == '-' || *p == '.')) {
			return false;
		}
	}
	return true;
}
static bool valid_hash(const char *hash)
{
	if (hash == NULL || strlen(hash) != 64U) {
		return false;
	}
	for (size_t i = 0; i < 64U; ++i) {
		if (!((hash[i] >= '0' && hash[i] <= '9') || (hash[i] >= 'a' && hash[i] <= 'f'))) {
			return false;
		}
	}
	return true;
}
static bool valid_file_path(const char *path)
{
	const char *prefix = "/extra/apps/";
	bool first = true;

	if (path == NULL || strlen(path) > 191U || strncmp(path, prefix, strlen(prefix)) != 0) {
		return false;
	}
	for (const char *p = path + strlen(prefix); *p != '\0'; ++p) {
		if (*p == '/') {
			if (first) {
				return false;
			}
			first = true;
		} else {
			if ((first && *p == '.') ||
			    !((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
			      (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.')) {
				return false;
			}
			first = false;
		}
	}
	return !first;
}
static void record_free(struct package_record *record)
{
	if (record != NULL) {
		k_free(record->document);
		k_free(record);
	}
}
static int record_path(const char *id, bool previous, char path[192])
{
	if (!valid_id(id)) {
		return -EINVAL;
	}
	snprintf(path, 192, STORE "/%s/%s.json", id, previous ? "previous" : "current");
	return 0;
}
static int record_shape(const struct package_record *record)
{
	char version_root[160];
	bool mba = false;

	if (record->schema != 1 || record->operation < 0 || record->operation > 2 ||
	    !valid_id(record->id) || !valid_hash(record->bundle) || !valid_hash(record->image) ||
	    record->version == NULL || record->version[0] == '\0' ||
	    strlen(record->version) > 15U || record->target == NULL || record->firmware == NULL ||
	    record->metadata_version <= 0 || record->interface_abi <= 0 ||
	    !valid_file_path(record->mba_path) || record->files_count == 0U) {
		return -EINVAL;
	}
	snprintf(version_root, sizeof(version_root), "/extra/apps/%s/versions/%.32s/", record->id,
		 record->bundle);
	for (size_t i = 0; i < record->files_count; ++i) {
		const struct package_file *file = &record->files[i];

		if (!valid_file_path(file->path) || !valid_hash(file->sha256) || file->length < 0 ||
		    file->length > 64 * 1024 * 1024 ||
		    (record->atomic &&
		     strncmp(file->path, version_root, strlen(version_root)) != 0) ||
		    (!record->atomic && strstr(file->path, "/versions/") != NULL)) {
			return -EINVAL;
		}
		bool is_mba = strcmp(file->path, record->mba_path) == 0;
		size_t length = strlen(file->path);
		bool suffix = length >= 4U && strcmp(file->path + length - 4U, ".mba") == 0;

		if (is_mba != suffix || (is_mba && mba)) {
			return -EINVAL;
		}
		mba |= is_mba;
		for (size_t j = 0; j < i; ++j) {
			if (strcmp(file->path, record->files[j].path) == 0) {
				return -EINVAL;
			}
		}
	}
	for (size_t i = 0; i < record->requires_count; ++i) {
		if (record->requires[i] == NULL || record->requires[i][0] == '\0' ||
		    strlen(record->requires[i]) > 127U) {
			return -EINVAL;
		}
	}
	return mba ? 0 : -EINVAL;
}
static int record_read(const char *path, struct package_record **out)
{
	struct fs_dirent entry;
	struct fs_file_t file;
	struct package_record *record = NULL;
	int rc = fs_stat(path, &entry);

	*out = NULL;
	if (rc != 0) {
		return rc;
	}
	if (entry.type != FS_DIR_ENTRY_FILE || entry.size == 0U || entry.size > MAX_RECORD) {
		return -EFBIG;
	}
	record = k_calloc(1, sizeof(*record));
	if (record == NULL) {
		return -ENOMEM;
	}
	record->document = k_malloc(entry.size + 1U);
	if (record->document == NULL) {
		record_free(record);
		return -ENOMEM;
	}
	fs_file_t_init(&file);
	rc = fs_open(&file, path, FS_O_READ);
	if (rc == 0) {
		size_t offset = 0U;

		while (offset < entry.size) {
			ssize_t n = fs_read(&file, record->document + offset, entry.size - offset);
			if (n <= 0) {
				rc = n < 0 ? (int)n : -EIO;
				break;
			}
			offset += n;
		}
		int close_rc = fs_close(&file);
		if (rc == 0) {
			rc = close_rc;
		}
	}
	record->document[entry.size] = '\0';
	if (rc == 0) {
		int64_t fields = json_obj_parse(record->document, entry.size, record_fields,
						ARRAY_SIZE(record_fields), record);

		rc = fields == BIT64(ARRAY_SIZE(record_fields)) - 1 ? record_shape(record)
								    : -EINVAL;
	}
	if (rc != 0) {
		record_free(record);
		return rc;
	}
	*out = record;
	return 0;
}
static int record_for(const char *id, bool previous, struct package_record **out)
{
	char path[192];
	int rc = record_path(id, previous, path);

	if (rc == 0) {
		rc = record_read(path, out);
	}
	if (rc == 0 && strcmp((*out)->id, id) != 0) {
		record_free(*out);
		*out = NULL;
		rc = -EINVAL;
	}
	return rc;
}
static int unlink_optional(const char *path)
{
	int rc = fs_unlink(path);

	return rc == -ENOENT ? 0 : rc;
}
static int copy_atomic(const char *source, const char *destination)
{
	struct fs_file_t input, output;
	char temporary[192];
	uint8_t buffer[384];
	int rc;
	int length = snprintf(temporary, sizeof(temporary), "%s.tmp", destination);

	if (length < 0 || length >= sizeof(temporary)) {
		return -ENAMETOOLONG;
	}
	fs_file_t_init(&input);
	fs_file_t_init(&output);
	rc = fs_open(&input, source, FS_O_READ);
	if (rc != 0) {
		return rc;
	}
	rc = fs_open(&output, temporary, FS_O_CREATE | FS_O_TRUNC | FS_O_WRITE);
	if (rc == 0) {
		while (true) {
			ssize_t n = fs_read(&input, buffer, sizeof(buffer));

			if (n <= 0) {
				rc = n < 0 ? (int)n : 0;
				break;
			}
			if (fs_write(&output, buffer, n) != n) {
				rc = -EIO;
				break;
			}
		}
		if (rc == 0) {
			rc = fs_sync(&output);
		}
		int close_rc = fs_close(&output);
		if (rc == 0) {
			rc = close_rc;
		}
	}
	int close_rc = fs_close(&input);
	if (rc == 0) {
		rc = close_rc;
	}
	if (rc == 0) {
		rc = fs_rename(temporary, destination);
	}
	if (rc != 0) {
		(void)unlink_optional(temporary);
	}
	return rc;
}
static int record_write(const char *path, struct package_record *record)
{
	struct fs_file_t file;
	char temporary[192];
	ssize_t size = json_calc_encoded_len(record_fields, ARRAY_SIZE(record_fields), record);
	char *encoded;
	int rc;

	if (size <= 0 || size > MAX_RECORD) {
		return -EFBIG;
	}
	encoded = k_malloc(size + 1U);
	if (encoded == NULL) {
		return -ENOMEM;
	}
	rc = json_obj_encode_buf(record_fields, ARRAY_SIZE(record_fields), record, encoded,
				 size + 1U);
	if (rc != 0) {
		k_free(encoded);
		return rc;
	}
	snprintf(temporary, sizeof(temporary), "%s.tmp", path);
	fs_file_t_init(&file);
	rc = fs_open(&file, temporary, FS_O_CREATE | FS_O_TRUNC | FS_O_WRITE);
	if (rc == 0) {
		if (fs_write(&file, encoded, size) != size) {
			rc = -EIO;
		}
		if (rc == 0) {
			rc = fs_sync(&file);
		}
		int close_rc = fs_close(&file);
		if (rc == 0) {
			rc = close_rc;
		}
	}
	k_free(encoded);
	if (rc == 0) {
		rc = fs_rename(temporary, path);
	}
	if (rc != 0) {
		(void)unlink_optional(temporary);
	}
	return rc;
}

static int file_verify(const struct package_file *expected)
{
	struct fs_file_t file;
	struct fs_dirent entry;
	uint8_t buffer[384], hash[32], digest[32];
	psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
	size_t hash_size = 0U;
	int rc = fs_stat(expected->path, &entry);

	if (rc != 0) {
		return rc;
	}
	if (entry.type != FS_DIR_ENTRY_FILE || entry.size != (size_t)expected->length ||
	    hex2bin(expected->sha256, 64, digest, sizeof(digest)) != sizeof(digest)) {
		return -EBADMSG;
	}
	fs_file_t_init(&file);
	rc = fs_open(&file, expected->path, FS_O_READ);
	if (rc != 0) {
		return rc;
	}
	if (psa_hash_setup(&operation, PSA_ALG_SHA_256) != PSA_SUCCESS) {
		rc = -EIO;
	}
	while (rc == 0) {
		ssize_t n = fs_read(&file, buffer, sizeof(buffer));

		if (n <= 0) {
			rc = n < 0 ? (int)n : 0;
			break;
		}
		if (psa_hash_update(&operation, buffer, n) != PSA_SUCCESS) {
			rc = -EIO;
		}
	}
	if (rc == 0 &&
	    (psa_hash_finish(&operation, hash, sizeof(hash), &hash_size) != PSA_SUCCESS ||
	     hash_size != sizeof(hash) || memcmp(hash, digest, sizeof(hash)) != 0)) {
		rc = -EBADMSG;
	}
	psa_hash_abort(&operation);
	int close_rc = fs_close(&file);
	return rc != 0 ? rc : close_rc;
}
static int record_host(const struct package_record *record)
{
	meshbus_LlextHostInfoResponse info;
	uint8_t hash[32];
	int rc = mbs_llext_host_info_get(&info);

	if (rc != 0) {
		return rc;
	}
	if (hex2bin(record->image, 64, hash, sizeof(hash)) != sizeof(hash) ||
	    info.image_sha256.size != sizeof(hash) ||
	    memcmp(hash, info.image_sha256.bytes, sizeof(hash)) ||
	    strcmp(record->target, info.target) || strcmp(record->firmware, info.build_revision) ||
	    record->metadata_version != info.metadata_version ||
	    record->interface_abi != info.interface_abi) {
		return -EXDEV;
	}
	for (size_t i = 0; i < record->requires_count; ++i) {
		if (llext_find_sym(NULL, record->requires[i]) == NULL) {
			return -ENODATA;
		}
	}
	return 0;
}
static int record_verify(const struct package_record *record)
{
	struct mbs_llext_app_info *info;
	int rc = record_host(record);

	for (size_t i = 0; rc == 0 && i < record->files_count; ++i) {
		rc = file_verify(&record->files[i]);
	}
	if (rc != 0) {
		return rc;
	}
	info = k_malloc(sizeof(*info));
	if (info == NULL) {
		return -ENOMEM;
	}
	rc = mbs_llext_app_probe(record->mba_path, info);
	if (rc == 0 && (strcmp(record->id, info->id) || strcmp(record->version, info->version))) {
		rc = -EINVAL;
	}
	k_free(info);
	return rc;
}
static bool owns(const struct package_record *record, const char *path)
{
	for (size_t i = 0; record != NULL && i < record->files_count; ++i) {
		if (strcmp(record->files[i].path, path) == 0) {
			return true;
		}
	}
	return false;
}
static int remove_files(const struct package_record *record, const struct package_record *keep,
			const struct package_record *also_keep)
{
	int rc = 0;

	for (size_t i = 0; record != NULL && i < record->files_count && rc == 0; ++i) {
		if (!owns(keep, record->files[i].path) && !owns(also_keep, record->files[i].path)) {
			rc = unlink_optional(record->files[i].path);
		}
	}
	return rc;
}
/* Remove only empty directories within an owned version, never the app root. */
static void prune_version(const struct package_record *record)
{
	if (record == NULL || !record->atomic) {
		return;
	}
	char root[160];
	snprintf(root, sizeof(root), "/extra/apps/%s/versions/%.32s", record->id, record->bundle);
	for (size_t i = 0; i < record->files_count; ++i) {
		char path[192];
		strcpy(path, record->files[i].path);
		char *slash;
		while ((slash = strrchr(path, '/')) != NULL) {
			*slash = '\0';
			if (strlen(path) < strlen(root)) {
				break;
			}
			if (fs_unlink(path) != 0) {
				break;
			}
		}
	}
}
static int capacity(meshbus_DesktopPackageResponse *response, uint64_t *block)
{
	struct fs_statvfs stat;
	int rc = fs_statvfs("/extra", &stat);

	if (rc == 0) {
		*block = MAX(stat.f_frsize, 1U);
		response->free_bytes = (uint64_t)stat.f_bfree * stat.f_frsize;
	}
	return rc;
}
static int snapshot(const char *id, meshbus_DesktopPackageResponse *response)
{
	struct package_record *current = NULL, *pending = NULL, *previous = NULL;
	uint64_t block;
	int rc = capacity(response, &block);

	if (rc != 0) {
		return rc;
	}
	rc = record_for(id, false, &current);
	if (rc != 0 && rc != -ENOENT) {
		goto out;
	}
	rc = record_read(PENDING, &pending);
	if (rc != 0 && rc != -ENOENT) {
		goto out;
	}
	rc = record_for(id, true, &previous);
	if (rc != 0 && rc != -ENOENT) {
		goto out;
	}
	strcpy(response->state, current ? "committed" : "absent");
	if (current != NULL) {
		strcpy(response->current_bundle, current->bundle);
		strcpy(response->mba_path, current->mba_path);
	}
	if (previous != NULL) {
		strcpy(response->previous_bundle, previous->bundle);
	}
	if (pending != NULL && strcmp(pending->id, id) == 0) {
		strcpy(response->pending_bundle, pending->bundle);
		strcpy(response->state,
		       current != NULL && strcmp(current->bundle, pending->bundle) == 0
			       ? "committed-recovery"
			       : "installing");
	}
	if (pending != NULL && strcmp(pending->id, id) == 0 && pending->operation == 2) {
		strcpy(response->state, "uninstalling");
	}
	rc = 0;
out:
	record_free(current);
	record_free(pending);
	record_free(previous);
	return rc;
}
static int ownership_check(const struct package_record *next)
{
	struct fs_dir_t directory;
	struct fs_dirent entry;
	int rc;

	fs_dir_t_init(&directory);
	rc = fs_opendir(&directory, STORE);
	if (rc != 0) {
		return rc;
	}
	while ((rc = fs_readdir(&directory, &entry)) == 0 && entry.name[0] != '\0') {
		if (entry.type != FS_DIR_ENTRY_DIR || !valid_id(entry.name) ||
		    strcmp(entry.name, next->id) == 0) {
			continue;
		}
		for (int prior = 0; prior < 2; ++prior) {
			struct package_record *other = NULL;

			rc = record_for(entry.name, prior != 0, &other);
			if (rc == -ENOENT) {
				rc = 0;
				continue;
			}
			if (rc != 0) {
				break;
			}
			for (size_t i = 0; i < next->files_count; ++i) {
				if (owns(other, next->files[i].path)) {
					rc = -EACCES;
					break;
				}
			}
			record_free(other);
			if (rc != 0) {
				break;
			}
		}
		if (rc != 0) {
			break;
		}
	}
	int close_rc = fs_closedir(&directory);
	return rc != 0 ? rc : close_rc;
}

static int transaction_budget(const struct package_record *next, uint64_t block, uint64_t *bytes)
{
	struct fs_dirent entry;
	char path[192];
	uint64_t largest = 0;
	const char *records[] = {INCOMING, NULL, NULL};
	char current[192], previous[192];
	record_path(next->id, false, current);
	record_path(next->id, true, previous);
	records[1] = current;
	records[2] = previous;
	for (size_t i = 0; i < ARRAY_SIZE(records); ++i) {
		int rc = fs_stat(records[i], &entry);
		if (rc != 0 && rc != -ENOENT) {
			return rc;
		}
		if (rc == 0) {
			largest = MAX(largest, entry.size);
		}
	}
	/* Pending, commit temporary, previous/garbage copies and metadata pairs.
	 * This is deliberately conservative, not an allocator guarantee.
	 */
	*bytes = 4U * DIV_ROUND_UP(largest, block) * block + 2U * block;
	snprintf(path, sizeof(path), STORE "/%s", next->id);
	int rc = fs_stat(path, &entry);
	if (rc == -ENOENT) {
		*bytes += 2U * block;
	} else if (rc != 0) {
		return rc;
	}
	for (size_t i = 0; i < next->files_count; ++i) {
		strcpy(path, next->files[i].path);
		for (char *p = path + strlen("/extra/apps/"); *p; ++p) {
			if (*p != '/') {
				continue;
			}
			size_t length = p - path;
			bool counted = false;
			for (size_t j = 0; j < i; ++j) {
				counted |= strncmp(next->files[j].path, path, length) == 0 &&
					   strlen(next->files[j].path) > length &&
					   next->files[j].path[length] == '/';
			}
			if (counted) {
				continue;
			}
			*p = '\0';
			rc = fs_stat(path, &entry);
			*p = '/';
			if (rc == -ENOENT) {
				*bytes += 2U * block;
			} else if (rc != 0) {
				return rc;
			}
		}
	}
	return 0;
}

static int prepare(const meshbus_DesktopPackageRequest *request,
		   meshbus_DesktopPackageResponse *response)
{
	struct package_record *next = NULL, *current = NULL, *old_previous = NULL;
	struct fs_dirent entry;
	uint64_t block, reclaimed = 0U;
	int rc;

	if (strcmp(request->manifest_path, INCOMING) != 0) {
		return -EINVAL;
	}
	rc = fs_stat(PENDING, &entry);
	if (rc != -ENOENT) {
		return rc == 0 ? -EBUSY : rc;
	}
	rc = record_read(INCOMING, &next);
	if (rc != 0) {
		return rc;
	}
	if (next->operation != 0 || next->purge_saves || strcmp(next->id, request->app_id) != 0) {
		rc = -EINVAL;
		goto out;
	}
	rc = record_host(next);
	if (rc == 0) {
		rc = ownership_check(next);
	}
	if (rc != 0) {
		goto out;
	}
	rc = record_for(next->id, false, &current);
	if (rc != 0 && rc != -ENOENT) {
		goto out;
	}
	if (current != NULL && strcmp(current->bundle, next->bundle) == 0) {
		rc = record_verify(current);
		goto out;
	}
	if (current != NULL && !request->replace) {
		rc = -EEXIST;
		goto out;
	}
	/* Versioned resources must use the exported location resolver. */
	if (next->atomic && next->files_count > 1U) {
		bool relocatable = false;

		for (size_t i = 0; i < next->requires_count; ++i) {
			relocatable |=
				strcmp(next->requires[i], "mbs_desktop_app_resource_path") == 0;
		}
		if (!relocatable) {
			rc = -ENOTSUP;
			goto out;
		}
	}
	rc = capacity(response, &block);
	if (rc != 0) {
		goto out;
	}
	rc = transaction_budget(next, block, &response->required_bytes);
	if (rc != 0) {
		goto out;
	}
	for (size_t i = 0; i < next->files_count; ++i) {
		const struct package_file *file = &next->files[i];

		rc = fs_stat(file->path, &entry);
		if (rc != 0 && rc != -ENOENT) {
			goto out;
		}
		if (rc == 0 && (!owns(current, file->path) || next->atomic)) {
			rc = -EEXIST;
			goto out;
		}
		response->required_bytes += DIV_ROUND_UP((uint64_t)file->length, block) * block;
	}
	if (!next->atomic) {
		for (size_t i = 0; current != NULL && i < current->files_count; ++i) {
			rc = fs_stat(current->files[i].path, &entry);
			if (rc != 0 && rc != -ENOENT) {
				goto out;
			}
			if (rc == 0) {
				reclaimed += DIV_ROUND_UP((uint64_t)entry.size, block) * block;
			}
		}
	}
	if (response->required_bytes > response->free_bytes + reclaimed) {
		rc = -ENOSPC;
		goto out;
	}
	char directory[192];
	snprintf(directory, sizeof(directory), STORE "/%s", next->id);
	rc = fs_mkdir(directory);
	if (rc != 0 && rc != -EEXIST) {
		goto out;
	}
	/* Persist the launch barrier before invalidating any previous payload. */
	rc = copy_atomic(INCOMING, PENDING);
	if (rc == 0 && !next->atomic) {
		char previous[192];

		record_path(next->id, true, previous);
		rc = record_for(next->id, true, &old_previous);
		if (rc == -ENOENT) {
			rc = 0;
		}
		if (rc == 0) {
			rc = remove_files(old_previous, NULL, NULL);
		}
		if (rc == 0) {
			prune_version(old_previous);
			rc = unlink_optional(previous);
		}
		if (rc == 0) {
			rc = remove_files(current, NULL, NULL);
		}
		if (rc == 0) {
			prune_version(current);
		}
	}
out:
	record_free(next);
	record_free(current);
	record_free(old_previous);
	return rc;
}
static int collect_garbage(const char *id, const struct package_record *current)
{
	struct package_record *garbage = NULL, *previous = NULL;
	int rc = record_read(GARBAGE, &garbage);

	if (rc == -ENOENT) {
		return 0;
	}
	if (rc != 0) {
		return rc;
	}
	if (strcmp(garbage->id, id) != 0) {
		rc = -EBUSY;
		goto out;
	}
	rc = record_for(id, true, &previous);
	if (rc != 0 && rc != -ENOENT) {
		goto out;
	}
	rc = remove_files(garbage, current, previous);
	if (rc == 0) {
		prune_version(garbage);
		rc = unlink_optional(GARBAGE);
	}
out:
	record_free(garbage);
	record_free(previous);
	return rc;
}

static int commit(const char *id)
{
	struct package_record *pending = NULL, *current = NULL;
	char path[192], previous[192];
	int rc = record_read(PENDING, &pending);

	if (rc == -ENOENT) {
		rc = record_for(id, false, &current);
		if (rc == 0) {
			rc = record_verify(current);
		}
		goto out;
	}
	if (rc != 0) {
		goto out;
	}
	if (strcmp(pending->id, id) != 0) {
		rc = -EBUSY;
		goto out;
	}
	rc = record_verify(pending);
	if (rc != 0) {
		goto out;
	}
	record_path(id, false, path);
	record_path(id, true, previous);
	rc = record_for(id, false, &current);
	if (rc != 0 && rc != -ENOENT) {
		goto out;
	}
	if (current != NULL && strcmp(current->bundle, pending->bundle) == 0) {
		goto finish;
	}
	if (current != NULL && record_verify(current) == 0) {
		struct package_record *old_previous = NULL;

		rc = record_for(id, true, &old_previous);
		if (rc != 0 && rc != -ENOENT) {
			goto out;
		}
		if (old_previous != NULL && strcmp(old_previous->bundle, pending->bundle) != 0 &&
		    strcmp(old_previous->bundle, current->bundle) != 0) {
			rc = copy_atomic(previous, GARBAGE);
		}
		record_free(old_previous);
		if (rc != 0 && rc != -ENOENT) {
			goto out;
		}
		rc = copy_atomic(path, previous);
		if (rc != 0) {
			goto out;
		}
	}
	/* One atomic rename selects the complete file set; pending removal is
	 * repeatable after a disconnect or reboot following that commit point.
	 */
	rc = copy_atomic(PENDING, path);
	if (rc != 0) {
		goto out;
	}
finish:
	rc = collect_garbage(id, pending);
	if (rc == 0) {
		rc = unlink_optional(PENDING);
	}
out:
	record_free(pending);
	record_free(current);
	return rc;
}
static int abort_install(const char *id)
{
	struct package_record *pending = NULL, *current = NULL;
	char path[192];
	int rc = record_read(PENDING, &pending);

	if (rc == -ENOENT) {
		return 0;
	}
	if (rc != 0) {
		return rc;
	}
	if (strcmp(pending->id, id) != 0) {
		rc = -EBUSY;
		goto out;
	}
	if (pending->operation != 0) {
		rc = -EBUSY;
		goto out;
	}
	rc = record_for(id, false, &current);
	if (rc != 0 && rc != -ENOENT) {
		goto out;
	}
	if (current != NULL && strcmp(current->bundle, pending->bundle) == 0) {
		rc = -EALREADY;
		goto out;
	}
	rc = remove_files(pending, pending->atomic ? current : NULL, NULL);
	if (rc == 0) {
		prune_version(pending);
	}
	if (rc == 0 && !pending->atomic) {
		struct package_record *previous = NULL;
		rc = record_for(id, true, &previous);
		if (rc == -ENOENT) {
			rc = 0;
		}
		if (rc == 0) {
			rc = remove_files(previous, NULL, NULL);
		}
		if (rc == 0) {
			prune_version(previous);
			rc = remove_files(current, NULL, NULL);
		}
		if (rc == 0) {
			prune_version(current);
			record_path(id, true, path);
			rc = unlink_optional(path);
		}
		if (rc == 0) {
			record_path(id, false, path);
			rc = unlink_optional(path);
		}
		record_free(previous);
	}
	if (rc == 0) {
		rc = collect_garbage(id, current);
	}
	if (rc == 0) {
		rc = unlink_optional(PENDING);
	}
out:
	record_free(pending);
	record_free(current);
	return rc;
}
static int uninstall(const char *id, bool remove_saves)
{
	struct package_record *current = NULL, *previous = NULL;
	char path[192];
	int rc = record_read(PENDING, &current);
	bool resuming = rc == 0;

	if (rc != 0 && rc != -ENOENT) {
		return rc;
	}
	if (resuming && (strcmp(current->id, id) != 0 || current->operation != 2)) {
		rc = -EBUSY;
		goto out;
	}
	if (resuming) {
		remove_saves = current->purge_saves;
	} else {
		rc = record_for(id, false, &current);
		if (rc == -ENOENT) {
			return 0;
		}
		if (rc != 0) {
			return rc;
		}
	}
	rc = record_for(id, true, &previous);
	if (rc != 0 && rc != -ENOENT) {
		goto out;
	}
	if (!resuming) {
		current->operation = 2;
		current->purge_saves = remove_saves;
		rc = record_write(PENDING, current);
		if (rc != 0) {
			goto out;
		}
	}
	rc = remove_files(previous, NULL, NULL);
	if (rc == 0) {
		rc = remove_files(current, NULL, NULL);
	}
	if (rc == 0) {
		prune_version(previous);
		prune_version(current);
	}
	if (rc == 0 && remove_saves) {
		const char *extensions[] = {"dat", "sav", "dat.tmp", "sav.tmp"};

		for (size_t i = 0; rc == 0 && i < ARRAY_SIZE(extensions); ++i) {
			snprintf(path, sizeof(path), "/extra/saves/%s.%s", id, extensions[i]);
			rc = unlink_optional(path);
		}
	}
	if (rc == 0) {
		record_path(id, true, path);
		rc = unlink_optional(path);
	}
	if (rc == 0) {
		record_path(id, false, path);
		rc = unlink_optional(path);
	}
	if (rc == 0) {
		rc = unlink_optional(PENDING);
	}
out:
	record_free(current);
	record_free(previous);
	return rc;
}

static int rollback(const char *id, const char *bundle)
{
	struct package_record *previous = NULL, *current = NULL, *pending = NULL;
	int rc;

	if (!valid_hash(bundle)) {
		return -EINVAL;
	}
	rc = record_read(PENDING, &pending);
	if (rc == 0) {
		bool same = pending->operation == 1 && strcmp(pending->id, id) == 0 &&
			    strcmp(pending->bundle, bundle) == 0;
		record_free(pending);
		return same ? commit(id) : -EBUSY;
	}
	if (rc != -ENOENT) {
		return rc;
	}
	rc = record_for(id, false, &current);
	if (rc != 0) {
		goto out;
	}
	if (strcmp(current->bundle, bundle) == 0) {
		rc = record_verify(current);
		goto out;
	}
	rc = record_for(id, true, &previous);
	if (rc != 0) {
		goto out;
	}
	if (strcmp(previous->bundle, bundle) != 0) {
		rc = -ESTALE;
		goto out;
	}
	rc = record_verify(previous);
	if (rc == 0) {
		previous->operation = 1;
		previous->purge_saves = false;
		rc = record_write(PENDING, previous);
	}
	if (rc == 0) {
		rc = commit(id);
	}
out:
	record_free(previous);
	record_free(current);
	return rc;
}

int mbs_desktop_package_manage(const meshbus_DesktopPackageRequest *request,
			       meshbus_DesktopPackageResponse *response)
{
	int rc;

	if (request == NULL || response == NULL) {
		return -EINVAL;
	}
	memset(response, 0, sizeof(*response));
	response->protocol_version = 1U;
	strcpy(response->state, "unknown");
	if (!valid_id(request->app_id)) {
		return -EINVAL;
	}
	strcpy(response->app_id, request->app_id);
	if (!desktop_app_lifecycle_acquire()) {
		return -EBUSY;
	}
	if (request->action != ACTION(STATUS)) {
		mbs_desktop_mba_status *session = k_malloc(sizeof(*session));
		if (session == NULL) {
			rc = -ENOMEM;
			goto out;
		}
		rc = mbs_desktop_mba_get_status(0, session);
		if (rc == 0 && !session->resources_reclaimed) {
			rc = -EBUSY;
		}
		k_free(session);
		if (rc != 0) {
			goto out;
		}
	}
	struct package_record *journal = NULL;
	int journal_rc = record_read(PENDING, &journal);
	if (journal_rc == 0 && journal->operation == 2 && request->action != ACTION(STATUS)) {
		if (strcmp(journal->id, request->app_id) != 0 ||
		    (request->action != ACTION(COMMIT) && request->action != ACTION(UNINSTALL))) {
			rc = -EBUSY;
		} else {
			rc = uninstall(request->app_id, journal->purge_saves);
		}
		record_free(journal);
		goto status;
	}
	record_free(journal);
	if (journal_rc != 0 && journal_rc != -ENOENT) {
		rc = journal_rc;
		goto out;
	}
	switch (request->action) {
	case ACTION(STATUS):
		rc = 0;
		break;
	case ACTION(PREPARE):
		rc = prepare(request, response);
		break;
	case ACTION(COMMIT):
		rc = commit(request->app_id);
		break;
	case ACTION(ABORT):
		rc = abort_install(request->app_id);
		break;
	case ACTION(UNINSTALL):
		rc = uninstall(request->app_id, request->remove_saves);
		break;
	case ACTION(ROLLBACK):
		rc = rollback(request->app_id, request->bundle);
		break;
	default:
		rc = -EINVAL;
		break;
	}
status:;
	int status_rc = snapshot(request->app_id, response);
	if (rc == 0) {
		rc = status_rc;
	}
out:
	response->detail = rc;
	desktop_app_lifecycle_release();
	return rc;
}
int desktop_package_path_validate(const char *path)
{
	struct fs_dirent entry;
	struct fs_dir_t directory;
	bool versioned = strstr(path, "/versions/") != NULL;
	int rc = fs_stat(PENDING, &entry);

	if (rc != -ENOENT) {
		return rc == 0 ? -EBUSY : rc;
	}
	fs_dir_t_init(&directory);
	rc = fs_opendir(&directory, STORE);
	if (rc == -ENOENT) {
		return versioned ? -ENOENT : 0;
	}
	if (rc != 0) {
		return rc;
	}
	while ((rc = fs_readdir(&directory, &entry)) == 0 && entry.name[0] != '\0') {
		struct package_record *record = NULL;

		if (entry.type != FS_DIR_ENTRY_DIR || !valid_id(entry.name)) {
			continue;
		}
		rc = record_for(entry.name, false, &record);
		if (rc == -ENOENT) {
			rc = 0;
			continue;
		}
		if (rc != 0) {
			break;
		}
		if (strcmp(record->mba_path, path) == 0) {
			rc = record_verify(record);
			record_free(record);
			(void)fs_closedir(&directory);
			return rc;
		}
		record_free(record);
	}
	int close_rc = fs_closedir(&directory);
	return rc != 0 ? rc : close_rc != 0 ? close_rc : versioned ? -ENOENT : 0;
}
