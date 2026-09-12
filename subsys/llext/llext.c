// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/llext/fs_loader.h>
#include <zephyr/llext/llext.h>
#include <zephyr/logging/log.h>
#include <fs/fs.h>
#include <llext/llext.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#include "mbs_settings_internal.h"

#if defined(CONFIG_USERSPACE)
#include <zephyr/app_memory/mem_domain.h>
#endif

LOG_MODULE_REGISTER(mbs_llext, CONFIG_MBS_LLEXT_LOG_LEVEL);

#define MBS_LLEXT_APP_ACCEPTED_HEAP_MAX					\
	MIN(CONFIG_MBS_LLEXT_APP_MAX_HEAP_SIZE,				\
	    CONFIG_MBS_LLEXT_APP_HEAP_RESERVE_SIZE)
#define MBS_LLEXT_APP_ALLOC_MAX						\
	DIV_ROUND_UP(MBS_LLEXT_APP_ACCEPTED_HEAP_MAX *			\
			     (100U + CONFIG_MBS_LLEXT_DYNAMIC_HEAP_MARGIN_PERCENT), \
		     100U)

BUILD_ASSERT(K_HEAP_MEM_POOL_SIZE >=
		     MBS_LLEXT_APP_ALLOC_MAX +
			     CONFIG_MBS_LLEXT_APP_SYSTEM_HEAP_HEADROOM_SIZE,
	     "system heap cannot hold the maximum LLEXT app and headroom");

/* -------------------------------------------------------------------------- */
/* Defaults And State                                                         */
/* -------------------------------------------------------------------------- */

#define METADATA_ELF_SCAN_MAX_SECTIONS 256U
#define METADATA_ELF_SCAN_MAX_SHSTRTAB_SIZE 4096U
#define MBS_LLEXT_SETTINGS_SUBTREE "meshbus/llext"
#define MBS_LLEXT_SETTINGS_KEY_CONFIG "config"
#define MBS_LLEXT_CONFIG_DEFAULTS							\
	{										\
		.enabled = CONFIG_MBS_LLEXT_DEFAULT_ENABLED,			\
	}

static const mbs_llext_config llext_cfg_defaults = MBS_LLEXT_CONFIG_DEFAULTS;

struct mbs_llext_app_session {
	bool in_use;
	bool loading;
	bool unloading;
	char path[MBS_LLEXT_PATH_MAX_LEN + 1];
	struct mbs_llext_app_metadata metadata;
	struct mbs_llext_app_info info;
	struct llext *ext;
	mbs_llext_app_entry_t entry_fn;
	bool brought_up;
	bool domain_added;
};

static K_MUTEX_DEFINE(app_mutex);
static K_MUTEX_DEFINE(metadata_mutex);
static mbs_llext_config llext_cfg = MBS_LLEXT_CONFIG_DEFAULTS;
static K_MUTEX_DEFINE(settings_mutex);
static bool settings_initial_apply;
static struct k_work_delayable settings_persistence_work;
static mbs_llext_config settings_load_cfg = MBS_LLEXT_CONFIG_DEFAULTS;
static struct mbs_settings_blob_load_state settings_load_state;
static char metadata_shstrtab_buf[METADATA_ELF_SCAN_MAX_SHSTRTAB_SIZE];
static struct mbs_llext_app_session app_session;
#if defined(CONFIG_LLEXT_HEAP_DYNAMIC)
static K_MUTEX_DEFINE(dynamic_llext_heap_mutex);
static void *dynamic_llext_heap_mem;
static size_t dynamic_llext_heap_size;
#endif

static int mbs_llext_heap_prepare(size_t requested);
static void mbs_llext_heap_release_if_idle(void);

/* -------------------------------------------------------------------------- */
/* Settings Schema                                                            */
/* -------------------------------------------------------------------------- */

MBS_SETTINGS_BLOB_SCHEMA_DEFINE(llext_settings_schema, MBS_LLEXT_SETTINGS_SUBTREE,
			       MBS_LLEXT_SETTINGS_KEY_CONFIG, meshbus_LlextConfig,
			       mbs_llext_config);

/* -------------------------------------------------------------------------- */
/* Validation And Helpers                                                     */
/* -------------------------------------------------------------------------- */

static bool id_char_is_valid(char ch)
{
	return ((ch >= 'a') && (ch <= 'z')) || ((ch >= '0') && (ch <= '9')) ||
	       (ch == '_') || (ch == '-') || (ch == '.');
}

static bool symbol_char_is_valid(char ch)
{
	return ((ch >= 'a') && (ch <= 'z')) || ((ch >= 'A') && (ch <= 'Z')) ||
	       ((ch >= '0') && (ch <= '9')) || (ch == '_');
}

static bool app_id_is_valid(const char *id)
{
	size_t len;

	if (id == NULL) {
		return false;
	}

	len = strlen(id);
	if ((len == 0U) || (len > MBS_LLEXT_ID_MAX_LEN)) {
		return false;
	}
	if ((id[0] < 'a') || (id[0] > 'z')) {
		return false;
	}

	for (size_t i = 1; i < len; i++) {
		if (!id_char_is_valid(id[i])) {
			return false;
		}
	}

	return true;
}

static bool symbol_is_valid(const char *name, size_t max_len)
{
	size_t len;

	if (name == NULL) {
		return false;
	}

	len = strnlen(name, max_len + 1U);
	if ((len == 0U) || (len > max_len)) {
		return false;
	}
	if (((name[0] < 'a') || (name[0] > 'z')) &&
	    ((name[0] < 'A') || (name[0] > 'Z')) &&
	    (name[0] != '_')) {
		return false;
	}

	for (size_t i = 1U; i < len; i++) {
		if (!symbol_char_is_valid(name[i])) {
			return false;
		}
	}

	return true;
}

static void cstr_copy(char *dst, size_t dst_len, const char *src)
{
	size_t n;

	if ((dst == NULL) || (dst_len == 0U)) {
		return;
	}

	if (src == NULL) {
		dst[0] = '\0';
		return;
	}

	n = strnlen(src, dst_len - 1U);
	memcpy(dst, src, n);
	dst[n] = '\0';
}

static bool has_suffix(const char *name, const char *suffix)
{
	size_t name_len;
	size_t suffix_len;

	if ((name == NULL) || (suffix == NULL)) {
		return false;
	}

	name_len = strlen(name);
	suffix_len = strlen(suffix);

	return (name_len > suffix_len) &&
	       (strcmp(&name[name_len - suffix_len], suffix) == 0);
}

static void llext_app_load_name_build(const char *id, char *out, size_t out_len)
{
	uint32_t hash = 2166136261U;
	const unsigned char *p = (const unsigned char *)id;

	if ((out == NULL) || (out_len == 0U)) {
		return;
	}
	if (id == NULL) {
		id = "";
		p = (const unsigned char *)id;
	}

	while (*p != '\0') {
		hash ^= (uint32_t)(*p++);
		hash *= 16777619U;
	}

	(void)snprintk(out, out_len, "a_%08" PRIx32, hash);
}

static void llext_config_copy(mbs_llext_config *cfg)
{
	k_mutex_lock(&settings_mutex, K_FOREVER);
	*cfg = llext_cfg;
	k_mutex_unlock(&settings_mutex);
}

static bool settings_initial_apply_get(void)
{
	bool initial_apply;

	k_mutex_lock(&settings_mutex, K_FOREVER);
	initial_apply = settings_initial_apply;
	k_mutex_unlock(&settings_mutex);

	return initial_apply;
}

static bool llext_enabled(void)
{
	bool enabled;

	k_mutex_lock(&settings_mutex, K_FOREVER);
	enabled = llext_cfg.enabled;
	k_mutex_unlock(&settings_mutex);

	return enabled;
}

static bool file_range_valid(size_t file_size, size_t offset, size_t len)
{
	if (offset > file_size) {
		return false;
	}

	return len <= (file_size - offset);
}

static int file_read_exact(struct fs_file_t *file, size_t offset, void *buf, size_t len)
{
	ssize_t bytes_read;
	uint8_t *out = buf;
	size_t remaining = len;
	int rc;

	rc = fs_seek(file, (off_t)offset, FS_SEEK_SET);
	if (rc != 0) {
		return rc;
	}

	while (remaining > 0U) {
		bytes_read = fs_read(file, out, remaining);
		if (bytes_read < 0) {
			return (int)bytes_read;
		}
		if (bytes_read == 0) {
			return -ENOEXEC;
		}

		out += (size_t)bytes_read;
		remaining -= (size_t)bytes_read;
	}

	return 0;
}

static int elf_section_name(const char *shstrtab, size_t shstrtab_size,
			    uint32_t sh_name, const char **name)
{
	const char *section_name;
	size_t max_len;

	if ((shstrtab == NULL) || (name == NULL)) {
		return -EINVAL;
	}
	if (sh_name >= shstrtab_size) {
		return -ENOEXEC;
	}

	section_name = &shstrtab[sh_name];
	max_len = shstrtab_size - sh_name;
	if (strnlen(section_name, max_len) == max_len) {
		return -ENOEXEC;
	}

	*name = section_name;
	return 0;
}

static int generated_string_validate(const char *value, size_t value_size)
{
	size_t len;

	if (value == NULL) {
		return -EINVAL;
	}

	len = strnlen(value, value_size);
	if ((len == 0U) || (len >= value_size)) {
		return -ENOEXEC;
	}

	return 0;
}

static int app_metadata_validate(const struct mbs_llext_app_metadata *metadata)
{
	size_t len;

	if (metadata == NULL) {
		return -EINVAL;
	}
	if (metadata->magic != MBS_LLEXT_APP_METADATA_MAGIC) {
		return -ENOEXEC;
	}
	if (metadata->metadata_version != MBS_LLEXT_APP_METADATA_VERSION) {
		return -EPROTONOSUPPORT;
	}
	if (metadata->size != sizeof(*metadata)) {
		return -ENOEXEC;
	}
	len = strnlen(metadata->id, sizeof(metadata->id));
	if ((len == 0U) || (len >= sizeof(metadata->id)) ||
	    !app_id_is_valid(metadata->id)) {
		return -ENOEXEC;
	}
	len = strnlen(metadata->name, sizeof(metadata->name));
	if ((len == 0U) || (len >= sizeof(metadata->name))) {
		return -ENOEXEC;
	}
	len = strnlen(metadata->app_version, sizeof(metadata->app_version));
	if ((len == 0U) || (len >= sizeof(metadata->app_version))) {
		return -ENOEXEC;
	}
	if (!symbol_is_valid(metadata->entry_point_symbol,
			     sizeof(metadata->entry_point_symbol) - 1U)) {
		return -ENOEXEC;
	}
	if (metadata->stack_size == 0U) {
		return -E2BIG;
	}
	if ((metadata->heap_size == 0U) ||
	    (metadata->heap_size > (uint32_t)CONFIG_MBS_LLEXT_APP_MAX_HEAP_SIZE)) {
		return -E2BIG;
	}
	if (metadata->icon_data_size != 0U &&
	    metadata->icon_data_size != MBS_LLEXT_APP_ICON_DATA_SIZE) {
		return -ENOEXEC;
	}
	for (size_t i = metadata->icon_data_size; i < sizeof(metadata->icon_data); i++) {
		if (metadata->icon_data[i] != 0U) {
			return -ENOEXEC;
		}
	}
	if ((generated_string_validate(metadata->edk_version,
				       sizeof(metadata->edk_version)) != 0) ||
	    (generated_string_validate(metadata->target, sizeof(metadata->target)) != 0)) {
		return -ENOEXEC;
	}
	for (size_t i = 0U; i < sizeof(metadata->compatibility_reserved); i++) {
		if (metadata->compatibility_reserved[i] != 0U) {
			return -ENOEXEC;
		}
	}
	for (size_t i = 0U; i < sizeof(metadata->reserved); i++) {
		if (metadata->reserved[i] != 0U) {
			return -ENOEXEC;
		}
	}

	return 0;
}

static int app_metadata_matches_host(const struct mbs_llext_app_metadata *metadata)
{
	if (strcmp(metadata->target, CONFIG_BOARD_TARGET) != 0) {
		LOG_WRN("Reject app '%s': target '%s' != host '%s'", metadata->id,
			metadata->target, CONFIG_BOARD_TARGET);
		return -EXDEV;
	}
	return 0;
}

static void app_metadata_fill_info(struct mbs_llext_app_info *info, const char *path,
				   const struct mbs_llext_app_metadata *metadata,
				   int last_error)
{
	if ((info == NULL) || (metadata == NULL)) {
		return;
	}

	memset(info, 0, sizeof(*info));
	cstr_copy(info->id, sizeof(info->id), metadata->id);
	cstr_copy(info->name, sizeof(info->name), metadata->name);
	cstr_copy(info->version, sizeof(info->version), metadata->app_version);
	cstr_copy(info->path, sizeof(info->path), path);
	cstr_copy(info->entry_point_symbol, sizeof(info->entry_point_symbol),
		  metadata->entry_point_symbol);
	cstr_copy(info->edk_version, sizeof(info->edk_version), metadata->edk_version);
	cstr_copy(info->target, sizeof(info->target), metadata->target);
	info->stack_size = metadata->stack_size;
	info->heap_size = metadata->heap_size;
	info->icon_data_size = metadata->icon_data_size;
	if (metadata->icon_data_size > 0U) {
		memcpy(info->icon_data, metadata->icon_data, metadata->icon_data_size);
	}
	info->last_error = last_error;
}

static int metadata_read_blob(const char *path, struct mbs_llext_app_metadata *metadata)
{
	static const uint8_t elf_magic[4] = {0x7f, 'E', 'L', 'F'};
	struct fs_dirent stat = {0};
	struct fs_file_t file;
	elf_ehdr_t ehdr;
	elf_shdr_t shdr;
	elf_shdr_t shstrtab_shdr;
	char *shstrtab = metadata_shstrtab_buf;
	size_t file_size;
	size_t shoff;
	size_t shentsize;
	size_t shnum;
	size_t shstrtab_size;
	size_t sh_table_size;
	bool metadata_found = false;
	int rc = 0;

	if ((path == NULL) || (metadata == NULL)) {
		return -EINVAL;
	}

	rc = fs_stat(path, &stat);
	if (rc != 0) {
		return rc;
	}

	file_size = stat.size;
	if (file_size < sizeof(ehdr)) {
		return -ENOEXEC;
	}

	k_mutex_lock(&metadata_mutex, K_FOREVER);

	fs_file_t_init(&file);
	rc = fs_open(&file, path, FS_O_READ);
	if (rc != 0) {
		k_mutex_unlock(&metadata_mutex);
		return rc;
	}

	rc = file_read_exact(&file, 0U, &ehdr, sizeof(ehdr));
	if (rc != 0) {
		goto out;
	}

	if (memcmp(ehdr.e_ident, elf_magic, sizeof(elf_magic)) != 0 ||
	    ((ehdr.e_type != ET_REL) && (ehdr.e_type != ET_DYN)) ||
	    (ehdr.e_shentsize != sizeof(elf_shdr_t)) ||
	    (ehdr.e_shnum == 0U) ||
	    (ehdr.e_shnum > METADATA_ELF_SCAN_MAX_SECTIONS) ||
	    (ehdr.e_shstrndx >= ehdr.e_shnum)) {
		rc = -ENOEXEC;
		goto out;
	}

	shoff = (size_t)ehdr.e_shoff;
	shentsize = (size_t)ehdr.e_shentsize;
	shnum = (size_t)ehdr.e_shnum;
	if (shnum > (SIZE_MAX / shentsize)) {
		rc = -ENOEXEC;
		goto out;
	}
	sh_table_size = shnum * shentsize;
	if (!file_range_valid(file_size, shoff, sh_table_size)) {
		rc = -ENOEXEC;
		goto out;
	}

	rc = file_read_exact(&file, shoff + ((size_t)ehdr.e_shstrndx * shentsize),
			     &shstrtab_shdr, sizeof(shstrtab_shdr));
	if (rc != 0) {
		goto out;
	}
	if (shstrtab_shdr.sh_type != SHT_STRTAB) {
		rc = -ENOEXEC;
		goto out;
	}

	shstrtab_size = (size_t)shstrtab_shdr.sh_size;
	if ((shstrtab_size == 0U) ||
	    (shstrtab_size > METADATA_ELF_SCAN_MAX_SHSTRTAB_SIZE) ||
	    !file_range_valid(file_size, (size_t)shstrtab_shdr.sh_offset, shstrtab_size)) {
		rc = -ENOEXEC;
		goto out;
	}

	rc = file_read_exact(&file, (size_t)shstrtab_shdr.sh_offset,
			     shstrtab, shstrtab_size);
	if (rc != 0) {
		goto out;
	}

	for (size_t i = 0U; i < shnum; i++) {
		const char *name;

		rc = file_read_exact(&file, shoff + (i * shentsize), &shdr, sizeof(shdr));
		if (rc != 0) {
			goto out;
		}

		rc = elf_section_name(shstrtab, shstrtab_size, shdr.sh_name, &name);
		if (rc != 0) {
			goto out;
		}
		if (strcmp(name, MBS_LLEXT_APP_METADATA_SECTION) != 0) {
			continue;
		}
		if (metadata_found || shdr.sh_type != SHT_PROGBITS ||
		    ((shdr.sh_flags & SHF_ALLOC) != 0U) ||
		    (shdr.sh_size != sizeof(*metadata)) ||
		    !file_range_valid(file_size, (size_t)shdr.sh_offset,
				      (size_t)shdr.sh_size)) {
			rc = -ENOEXEC;
			goto out;
		}

		rc = file_read_exact(&file, (size_t)shdr.sh_offset, metadata,
				     sizeof(*metadata));
		if (rc != 0) {
			goto out;
		}
		metadata_found = true;
	}

	rc = metadata_found ? 0 : -ENOENT;

out:
	(void)fs_close(&file);
	k_mutex_unlock(&metadata_mutex);
	return rc;
}

static int app_metadata_read_file(const char *path, struct mbs_llext_app_metadata *metadata)
{
	int rc;

	rc = metadata_read_blob(path, metadata);
	if (rc != 0) {
		return rc;
	}

	return app_metadata_validate(metadata);
}

/* -------------------------------------------------------------------------- */
/* App Resource Cleanup                                                       */
/* -------------------------------------------------------------------------- */

static int loaded_resource_cleanup(struct llext **ext, bool *brought_up,
				   bool *domain_added)
{
	int rc = 0;

#if defined(CONFIG_USERSPACE)
	if (domain_added != NULL && *domain_added && ext != NULL && *ext != NULL) {
		for (size_t i = 0; i < LLEXT_MEM_PARTITIONS; i++) {
			if ((*ext)->mem_size[i] == 0U) {
				continue;
			}
			(void)k_mem_domain_remove_partition(&k_mem_domain_default,
							    &(*ext)->mem_parts[i]);
		}
		*domain_added = false;
	}
#else
	ARG_UNUSED(domain_added);
#endif

	if (brought_up != NULL && *brought_up && ext != NULL && *ext != NULL) {
		int trc = llext_teardown(*ext);

		if ((trc < 0) && (rc == 0)) {
			rc = trc;
		}
		*brought_up = false;
	}
	if (ext != NULL && *ext != NULL) {
		int urc = llext_unload(ext);

		if ((urc < 0) && (rc == 0)) {
			rc = urc;
		}
	}

	return rc;
}

/* -------------------------------------------------------------------------- */
/* Desktop App Loading                                                        */
/* -------------------------------------------------------------------------- */

static int app_path_validate(const char *path)
{
	size_t len;

	if (path == NULL) {
		return -EINVAL;
	}

	len = strnlen(path, MBS_LLEXT_PATH_MAX_LEN + 1U);
	if ((len == 0U) || (len > MBS_LLEXT_PATH_MAX_LEN)) {
		return -EINVAL;
	}
	if (!has_suffix(path, MBS_LLEXT_APP_SUFFIX)) {
		return -EINVAL;
	}

	return 0;
}

static int app_metadata_probe_common(const char *path,
				     struct mbs_llext_app_metadata *metadata,
				     struct mbs_llext_app_info *info)
{
	int rc;

	rc = app_path_validate(path);
	if (rc != 0) {
		return rc;
	}

	rc = app_metadata_read_file(path, metadata);
	if (rc != 0) {
		if (info != NULL) {
			memset(info, 0, sizeof(*info));
			cstr_copy(info->path, sizeof(info->path), path);
			info->last_error = rc;
		}
		return rc;
	}

	rc = app_metadata_matches_host(metadata);
	if (rc != 0) {
		if (info != NULL) {
			app_metadata_fill_info(info, path, metadata, rc);
		}
		return rc;
	}

	if (info != NULL) {
		app_metadata_fill_info(info, path, metadata, 0);
	}

	return 0;
}

static bool app_session_matches(const struct mbs_llext_app_session *session)
{
	return session == &app_session;
}

static int app_heap_check(const struct mbs_llext_app_metadata *metadata)
{
	if (metadata == NULL) {
		return -EINVAL;
	}
	if ((metadata->heap_size == 0U) ||
	    (metadata->heap_size > (uint32_t)CONFIG_MBS_LLEXT_APP_MAX_HEAP_SIZE) ||
	    (metadata->heap_size > (uint32_t)CONFIG_MBS_LLEXT_APP_HEAP_RESERVE_SIZE)) {
		return -E2BIG;
	}

	return 0;
}

int mbs_llext_app_probe(const char *path, struct mbs_llext_app_info *info)
{
	struct mbs_llext_app_metadata metadata;
	int rc;

	if (info == NULL) {
		return -EINVAL;
	}
	rc = app_path_validate(path);
	if (rc != 0) {
		return rc;
	}
	if (!llext_enabled()) {
		memset(info, 0, sizeof(*info));
		cstr_copy(info->path, sizeof(info->path), path);
		info->last_error = -ENODEV;
		return -ENODEV;
	}

	return app_metadata_probe_common(path, &metadata, info);
}

int mbs_llext_app_load(const char *path,
			   struct mbs_llext_app_session **session_out)
{
	struct mbs_llext_app_metadata metadata;
	struct llext_fs_loader fs_loader;
	struct llext_load_param load_param = LLEXT_LOAD_PARAM_DEFAULT;
	char load_name[LLEXT_MAX_NAME_LEN + 1];
	struct llext *ext = NULL;
	bool brought_up = false;
	bool domain_added = false;
	const void *sym;
	int cleanup_rc;
	int rc;

	if (session_out == NULL) {
		return -EINVAL;
	}
	*session_out = NULL;
	rc = app_path_validate(path);
	if (rc != 0) {
		return rc;
	}
	if (!llext_enabled()) {
		return -ENODEV;
	}

	rc = app_metadata_probe_common(path, &metadata, NULL);
	if (rc != 0) {
		return rc;
	}
	rc = app_heap_check(&metadata);
	if (rc != 0) {
		return rc;
	}
	k_mutex_lock(&app_mutex, K_FOREVER);
	if (app_session.in_use || app_session.loading || app_session.unloading) {
		k_mutex_unlock(&app_mutex);
		return -EBUSY;
	}
	memset(&app_session, 0, sizeof(app_session));
	app_session.loading = true;
	k_mutex_unlock(&app_mutex);

	rc = mbs_llext_heap_prepare(metadata.heap_size);
	if (rc != 0) {
		goto fail;
	}

	llext_app_load_name_build(metadata.id, load_name, sizeof(load_name));
	fs_loader = (struct llext_fs_loader)LLEXT_FS_LOADER(path);
	rc = llext_load(&fs_loader.loader, load_name, &ext, &load_param);
	if (rc != 0) {
		LOG_ERR("App llext_load failed: id='%s' path='%s' rc=%d",
			metadata.id, path, rc);
		goto fail;
	}

	rc = llext_bringup(ext);
	if (rc != 0) {
		LOG_ERR("App llext_bringup failed: id='%s' path='%s' rc=%d",
			metadata.id, path, rc);
		goto fail;
	}
	brought_up = true;

#if defined(CONFIG_USERSPACE)
	rc = llext_add_domain(ext, &k_mem_domain_default);
	if (rc != 0) {
		goto fail;
	}
	domain_added = true;
#endif

	sym = llext_find_sym(&ext->exp_tab, metadata.entry_point_symbol);
	if (sym == NULL) {
		LOG_ERR("App entry symbol not found: id='%s' symbol='%s'",
			metadata.id, metadata.entry_point_symbol);
		rc = -ENOEXEC;
		goto fail;
	}

	k_mutex_lock(&app_mutex, K_FOREVER);
	if (!app_session.loading) {
		k_mutex_unlock(&app_mutex);
		rc = -ENOENT;
		goto fail;
	}
	app_session.metadata = metadata;
	app_metadata_fill_info(&app_session.info, path, &metadata, 0);
	cstr_copy(app_session.path, sizeof(app_session.path), path);
	app_session.ext = ext;
	app_session.entry_fn = (mbs_llext_app_entry_t)sym;
	app_session.brought_up = brought_up;
	app_session.domain_added = domain_added;
	app_session.in_use = true;
	app_session.loading = false;
	*session_out = &app_session;
	k_mutex_unlock(&app_mutex);

	ext = NULL;
	brought_up = false;
	domain_added = false;
	return 0;

fail:
	cleanup_rc = loaded_resource_cleanup(&ext, &brought_up, &domain_added);
	k_mutex_lock(&app_mutex, K_FOREVER);
	memset(&app_session, 0, sizeof(app_session));
	if (cleanup_rc != 0) {
		app_session.metadata = metadata;
		app_metadata_fill_info(&app_session.info, path, &metadata, cleanup_rc);
		cstr_copy(app_session.path, sizeof(app_session.path), path);
		app_session.ext = ext;
		app_session.brought_up = brought_up;
		app_session.domain_added = domain_added;
		app_session.in_use = true;
		*session_out = &app_session;
	}
	k_mutex_unlock(&app_mutex);
	if (cleanup_rc != 0) {
		LOG_ERR("App load failed: %d; cleanup failed: %d", rc, cleanup_rc);
		return cleanup_rc;
	}
	mbs_llext_heap_release_if_idle();
	return rc;
}

int mbs_llext_app_get_info(const struct mbs_llext_app_session *session,
			       struct mbs_llext_app_info *info)
{
	if ((session == NULL) || (info == NULL) || !app_session_matches(session)) {
		return -EINVAL;
	}

	k_mutex_lock(&app_mutex, K_FOREVER);
	if (!app_session.in_use) {
		k_mutex_unlock(&app_mutex);
		return -ENOENT;
	}
	*info = app_session.info;
	k_mutex_unlock(&app_mutex);

	return 0;
}

int mbs_llext_app_get_entry(const struct mbs_llext_app_session *session,
				mbs_llext_app_entry_t *entry_out)
{
	if ((session == NULL) || (entry_out == NULL) || !app_session_matches(session)) {
		return -EINVAL;
	}

	k_mutex_lock(&app_mutex, K_FOREVER);
	if (!app_session.in_use || app_session.entry_fn == NULL) {
		k_mutex_unlock(&app_mutex);
		return -ENOENT;
	}
	*entry_out = app_session.entry_fn;
	k_mutex_unlock(&app_mutex);

	return 0;
}

int mbs_llext_app_unload(struct mbs_llext_app_session *session)
{
	struct llext *ext;
	bool brought_up;
	bool domain_added;
	int rc;

	if ((session == NULL) || !app_session_matches(session)) {
		return -EINVAL;
	}

	k_mutex_lock(&app_mutex, K_FOREVER);
	if (!app_session.in_use) {
		k_mutex_unlock(&app_mutex);
		return -ENOENT;
	}
	if (app_session.unloading) {
		k_mutex_unlock(&app_mutex);
		return -EBUSY;
	}

	ext = app_session.ext;
	brought_up = app_session.brought_up;
	domain_added = app_session.domain_added;
	app_session.unloading = true;
	/* Teardown can partially succeed even when reclamation reports an error. */
	app_session.entry_fn = NULL;
	k_mutex_unlock(&app_mutex);

	rc = loaded_resource_cleanup(&ext, &brought_up, &domain_added);

	k_mutex_lock(&app_mutex, K_FOREVER);
	if (rc == 0) {
		memset(&app_session, 0, sizeof(app_session));
	} else {
		app_session.ext = ext;
		app_session.brought_up = brought_up;
		app_session.domain_added = domain_added;
		app_session.unloading = false;
		app_session.info.last_error = rc;
	}
	k_mutex_unlock(&app_mutex);
	if (rc == 0) {
		mbs_llext_heap_release_if_idle();
	}

	return rc;
}

/* -------------------------------------------------------------------------- */
/* Settings Apply                                                             */
/* -------------------------------------------------------------------------- */

static int llext_config_validate(const mbs_llext_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	return 0;
}

static int settings_handler_apply(const mbs_llext_config *cfg, bool persistence,
				  bool force)
{
	mbs_llext_config prev_cfg;

	if (llext_config_validate(cfg) != 0) {
		return -EINVAL;
	}

	k_mutex_lock(&settings_mutex, K_FOREVER);
	prev_cfg = llext_cfg;
	if (!force && prev_cfg.enabled == cfg->enabled) {
		settings_initial_apply = true;
		k_mutex_unlock(&settings_mutex);
		return 0;
	}

	llext_cfg = *cfg;
	settings_initial_apply = true;
	k_mutex_unlock(&settings_mutex);

	LOG_INF("Settings apply: enabled=%s", cfg->enabled ? "yes" : "no");

	if (persistence) {
		(void)k_work_reschedule(&settings_persistence_work,
				       K_MSEC(CONFIG_MBS_SETTINGS_PERSISTENCE_DELAY));
	}

	return 0;
}

MBS_SETTINGS_BLOB_CONFIG_DEFINE(llext_settings_schema, settings_mutex, settings_load_state,
			       settings_load_cfg, llext_cfg, settings_initial_apply,
			       mbs_llext_config, meshbus_LlextConfig_size,
			       settings_handler_apply, "LLEXT")

SETTINGS_STATIC_HANDLER_DEFINE(mbs_llext, MBS_LLEXT_SETTINGS_SUBTREE, NULL,
			       settings_handle_set, settings_handle_commit,
			       settings_handle_export);

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

int mbs_llext_config_get(mbs_llext_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	llext_config_copy(cfg);
	return 0;
}

int mbs_llext_config_set(const mbs_llext_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	return settings_handler_apply(cfg, true, false);
}

int mbs_llext_config_reset(void)
{
	struct k_work_sync sync;
	mbs_llext_config cfg = llext_cfg_defaults;
	int rc;

	(void)k_work_cancel_delayable_sync(&settings_persistence_work, &sync);

	rc = settings_handler_apply(&cfg, false, true);
	if (rc != 0) {
		return rc;
	}

	rc = mbs_settings_blob_delete(&llext_settings_schema);
	if (rc != 0) {
		LOG_ERR("Failed to delete persisted settings: %d", rc);
		return rc;
	}

	return 0;
}

bool mbs_llext_runtime_busy(void)
{
	bool busy = false;

	k_mutex_lock(&app_mutex, K_FOREVER);
	busy = app_session.in_use || app_session.loading ||
	       app_session.unloading || app_session.brought_up;
	k_mutex_unlock(&app_mutex);

	return busy;
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

static int mbs_llext_heap_prepare(size_t requested)
{
#if defined(CONFIG_LLEXT_HEAP_DYNAMIC)
	size_t heap_size;
	int rc;

	if (requested == 0U) {
		return 0;
	}

	if (requested > SIZE_MAX /
	    (100U + CONFIG_MBS_LLEXT_DYNAMIC_HEAP_MARGIN_PERCENT)) {
		return -EOVERFLOW;
	}
	heap_size = DIV_ROUND_UP(requested *
				 (100U + CONFIG_MBS_LLEXT_DYNAMIC_HEAP_MARGIN_PERCENT),
				 100U);
	if (heap_size > (size_t)CONFIG_MBS_LLEXT_TOTAL_HEAP_MAX_SIZE) {
		return -ENOMEM;
	}

	k_mutex_lock(&dynamic_llext_heap_mutex, K_FOREVER);
	if (dynamic_llext_heap_mem != NULL) {
		if (dynamic_llext_heap_size < heap_size) {
			LOG_WRN("LLEXT dynamic heap too small: have=%zu required=%zu requested=%zu",
				dynamic_llext_heap_size, heap_size, requested);
			k_mutex_unlock(&dynamic_llext_heap_mutex);
			return -ENOMEM;
		}

		k_mutex_unlock(&dynamic_llext_heap_mutex);
		return 0;
	}

	dynamic_llext_heap_mem = k_malloc(heap_size);
	if (dynamic_llext_heap_mem == NULL) {
		k_mutex_unlock(&dynamic_llext_heap_mutex);
		return -ENOMEM;
	}

	rc = llext_heap_init(dynamic_llext_heap_mem, heap_size);
	if (rc != 0) {
		k_free(dynamic_llext_heap_mem);
		dynamic_llext_heap_mem = NULL;
		dynamic_llext_heap_size = 0U;
		k_mutex_unlock(&dynamic_llext_heap_mutex);
		return rc;
	}

	dynamic_llext_heap_size = heap_size;
	k_mutex_unlock(&dynamic_llext_heap_mutex);
	LOG_INF("LLEXT dynamic heap initialized: %zu bytes (requested=%zu margin=%u%%)",
		heap_size, requested,
		(unsigned int)CONFIG_MBS_LLEXT_DYNAMIC_HEAP_MARGIN_PERCENT);
#else
	ARG_UNUSED(requested);
#endif
	return 0;
}

static void mbs_llext_heap_release_if_idle(void)
{
#if defined(CONFIG_LLEXT_HEAP_DYNAMIC)
	int rc;

	k_mutex_lock(&app_mutex, K_FOREVER);
	if (app_session.in_use || app_session.loading || app_session.unloading) {
		k_mutex_unlock(&app_mutex);
		return;
	}
	k_mutex_unlock(&app_mutex);

	k_mutex_lock(&dynamic_llext_heap_mutex, K_FOREVER);
	if (dynamic_llext_heap_mem == NULL) {
		k_mutex_unlock(&dynamic_llext_heap_mutex);
		return;
	}

	rc = llext_heap_uninit();
	if (rc != 0) {
		LOG_WRN("LLEXT dynamic heap release deferred: rc=%d", rc);
		k_mutex_unlock(&dynamic_llext_heap_mutex);
		return;
	}

	k_free(dynamic_llext_heap_mem);
	LOG_INF("LLEXT dynamic heap released: %zu bytes", dynamic_llext_heap_size);
	dynamic_llext_heap_mem = NULL;
	dynamic_llext_heap_size = 0U;
	k_mutex_unlock(&dynamic_llext_heap_mutex);
#endif
}

static int mbs_llext_init(void)
{
	mbs_llext_config cfg;
	int rc;

	k_work_init_delayable(&settings_persistence_work, settings_persistence_work_handler);
	rc = settings_load_subtree(MBS_LLEXT_SETTINGS_SUBTREE);
	if (rc != 0) {
		LOG_WRN("LLEXT settings load failed: %d", rc);
	}
	if (!settings_initial_apply_get()) {
		cfg = llext_cfg_defaults;
		rc = settings_handler_apply(&cfg, false, true);
		if (rc != 0) {
			return rc;
		}
	}

	LOG_INF("Meshbus LLEXT app runtime ready");
	return 0;
}
SYS_INIT(mbs_llext_init, APPLICATION, 99);
