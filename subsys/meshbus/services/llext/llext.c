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
#include <zephyr/meshbus/fs.h>
#include <zephyr/meshbus/llext.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#include "common/settings.h"

#if defined(CONFIG_USERSPACE)
#include <zephyr/app_memory/mem_domain.h>
#endif

LOG_MODULE_REGISTER(meshbus_llext, CONFIG_MESHBUS_LLEXT_LOG_LEVEL);

#ifndef CONFIG_MESHBUS_LLEXT_APP_MAX_HEAP_SIZE
#define CONFIG_MESHBUS_LLEXT_APP_MAX_HEAP_SIZE 0
#endif

#ifndef CONFIG_MESHBUS_LLEXT_APP_HEAP_RESERVE_SIZE
#define CONFIG_MESHBUS_LLEXT_APP_HEAP_RESERVE_SIZE 0
#endif

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_APP_SERVICES)
#define MESHBUS_LLEXT_APP_ACCEPTED_HEAP_MAX					\
	MIN(CONFIG_MESHBUS_LLEXT_APP_MAX_HEAP_SIZE,				\
	    CONFIG_MESHBUS_LLEXT_APP_HEAP_RESERVE_SIZE)
#define MESHBUS_LLEXT_APP_ALLOC_MAX						\
	DIV_ROUND_UP(MESHBUS_LLEXT_APP_ACCEPTED_HEAP_MAX *			\
			     (100U + CONFIG_MESHBUS_LLEXT_DYNAMIC_HEAP_MARGIN_PERCENT), \
		     100U)

BUILD_ASSERT(K_HEAP_MEM_POOL_SIZE >=
		     MESHBUS_LLEXT_APP_ALLOC_MAX +
			     CONFIG_MESHBUS_LLEXT_APP_SYSTEM_HEAP_HEADROOM_SIZE,
	     "system heap cannot hold the maximum LLEXT app and headroom");
#endif

/* -------------------------------------------------------------------------- */
/* ZBus Channels                                                              */
/* -------------------------------------------------------------------------- */

ZBUS_CHAN_DEFINE(meshbus_llext_state_chan, struct meshbus_llext_state_event,
		 NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

/* -------------------------------------------------------------------------- */
/* Defaults And State                                                         */
/* -------------------------------------------------------------------------- */

#define SERVICE_ELF_SCAN_MAX_SECTIONS 256U
#define SERVICE_ELF_SCAN_MAX_SHSTRTAB_SIZE 4096U
#define MESHBUS_LLEXT_SETTINGS_SUBTREE "meshbus/llext"
#define MESHBUS_LLEXT_SETTINGS_KEY_CONFIG "config"
#define MESHBUS_LLEXT_BOOT_DELAY_MIN_MS 0U
#define MESHBUS_LLEXT_BOOT_DELAY_MAX_MS 60000U
#define MESHBUS_LLEXT_CONFIG_DEFAULTS							\
	{										\
		.enabled = CONFIG_MESHBUS_LLEXT_DEFAULT_ENABLED,			\
		.boot_delay = CONFIG_MESHBUS_LLEXT_DEFAULT_BOOT_DELAY,			\
	}

static const meshbus_llext_config llext_cfg_defaults = MESHBUS_LLEXT_CONFIG_DEFAULTS;

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
struct meshbus_llext_slot {
	bool in_use;
	bool loading;
	struct meshbus_llext_service_info info;
	char path[MESHBUS_LLEXT_PATH_MAX_LEN + 1];
	struct meshbus_llext_service_metadata metadata;
	struct llext *ext;
	k_thread_entry_t thread_entry_fn;
	struct k_thread thread;
	k_tid_t thread_tid;
	k_thread_stack_t *thread_stack;
	bool thread_active;
	bool brought_up;
	bool domain_added;
	struct k_work exit_cleanup_work;
};
#endif

struct discovered_service {
	char id[MESHBUS_LLEXT_ID_MAX_LEN + 1];
	bool conflict;
	char path[MESHBUS_LLEXT_PATH_MAX_LEN + 1];
	struct meshbus_llext_service_metadata metadata;
};

struct meshbus_llext_app_session {
	bool in_use;
	bool loading;
	bool unloading;
	char path[MESHBUS_LLEXT_PATH_MAX_LEN + 1];
	struct meshbus_llext_app_metadata metadata;
	struct meshbus_llext_app_info info;
	struct llext *ext;
	meshbus_llext_app_entry_t entry_fn;
	bool brought_up;
	bool domain_added;
};

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
static struct meshbus_llext_slot service_slots[CONFIG_MESHBUS_LLEXT_MAX_SERVICES];
static K_MUTEX_DEFINE(service_mutex);
#endif
#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_APP_SERVICES)
static K_MUTEX_DEFINE(app_mutex);
#endif
static K_MUTEX_DEFINE(metadata_mutex);
K_THREAD_STACK_DEFINE(init_stack, CONFIG_MESHBUS_LLEXT_INIT_STACK_SIZE);
static struct k_thread init_thread;
static meshbus_llext_config llext_cfg = MESHBUS_LLEXT_CONFIG_DEFAULTS;
static K_MUTEX_DEFINE(settings_mutex);
static bool settings_initial_apply;
static struct k_work_delayable settings_persistence_work;
static meshbus_llext_config settings_load_cfg = MESHBUS_LLEXT_CONFIG_DEFAULTS;
static struct mb_settings_blob_load_state settings_load_state;
#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
static struct discovered_service service_boot_discovered[CONFIG_MESHBUS_LLEXT_MAX_SERVICES];
#endif
static char service_shstrtab_buf[SERVICE_ELF_SCAN_MAX_SHSTRTAB_SIZE];
#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_APP_SERVICES)
static struct meshbus_llext_app_session app_session;
#endif
#if defined(CONFIG_LLEXT_HEAP_DYNAMIC)
static K_MUTEX_DEFINE(dynamic_llext_heap_mutex);
static void *dynamic_llext_heap_mem;
static size_t dynamic_llext_heap_size;
#endif

static int meshbus_llext_heap_prepare(const struct discovered_service *services,
				      size_t service_cnt, size_t extra_heap);
#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_APP_SERVICES)
static void meshbus_llext_heap_release_if_idle(void);
#endif
#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
static void service_exit_cleanup_work_handler(struct k_work *work);
#endif

/* -------------------------------------------------------------------------- */
/* Settings Schema                                                            */
/* -------------------------------------------------------------------------- */

MB_SETTINGS_BLOB_SCHEMA_DEFINE(llext_settings_schema, MESHBUS_LLEXT_SETTINGS_SUBTREE,
			       MESHBUS_LLEXT_SETTINGS_KEY_CONFIG, meshbus_LlextConfig,
			       meshbus_llext_config);

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

static bool service_id_is_valid(const char *id)
{
	size_t len;

	if (id == NULL) {
		return false;
	}

	len = strlen(id);
	if ((len == 0U) || (len > MESHBUS_LLEXT_ID_MAX_LEN)) {
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

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
static void llext_load_name_build(const char *id, char *out, size_t out_len)
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

	(void)snprintk(out, out_len, "s_%08" PRIx32, hash);
}
#endif

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_APP_SERVICES)
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
#endif

static void llext_config_copy(meshbus_llext_config *cfg)
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

static bool llext_service_enabled(void)
{
	bool enabled;

	k_mutex_lock(&settings_mutex, K_FOREVER);
	enabled = llext_cfg.enabled;
	k_mutex_unlock(&settings_mutex);

	return enabled;
}

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
static int service_resolve_path(const char *name, char *path, size_t path_len)
{
	if ((name == NULL) || (path == NULL)) {
		return -EINVAL;
	}

	if (snprintk(path, path_len, "%s/%s", CONFIG_MESHBUS_LLEXT_DEFAULT_PATH,
		     name) >= path_len) {
		return -EINVAL;
	}

	return 0;
}
#endif

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

static int metadata_matches_host(const char *kind, const char *id, const char *target)
{
	if (strcmp(target, CONFIG_BOARD_TARGET) != 0) {
		LOG_WRN("Reject %s '%s': target '%s' != host '%s'", kind, id,
			target, CONFIG_BOARD_TARGET);
		return -EXDEV;
	}

	return 0;
}

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
static int service_metadata_validate(const struct meshbus_llext_service_metadata *metadata)
{
	size_t len;

	if (metadata == NULL) {
		return -EINVAL;
	}
	if (metadata->magic != MESHBUS_LLEXT_SERVICE_METADATA_MAGIC) {
		return -ENOEXEC;
	}
	if (metadata->metadata_version != MESHBUS_LLEXT_SERVICE_METADATA_VERSION) {
		return -EPROTONOSUPPORT;
	}
	if (metadata->size != sizeof(*metadata)) {
		return -ENOEXEC;
	}
	len = strnlen(metadata->id, sizeof(metadata->id));
	if ((len == 0U) || (len >= sizeof(metadata->id)) ||
	    !service_id_is_valid(metadata->id)) {
		return -ENOEXEC;
	}
	len = strnlen(metadata->name, sizeof(metadata->name));
	if ((len == 0U) || (len >= sizeof(metadata->name))) {
		return -ENOEXEC;
	}
	if (strnlen(metadata->description, sizeof(metadata->description)) >=
	    sizeof(metadata->description)) {
		return -ENOEXEC;
	}
	len = strnlen(metadata->service_version, sizeof(metadata->service_version));
	if ((len == 0U) || (len >= sizeof(metadata->service_version))) {
		return -ENOEXEC;
	}
	if (!symbol_is_valid(metadata->entry_point_symbol,
			     sizeof(metadata->entry_point_symbol) - 1U)) {
		return -ENOEXEC;
	}
	if ((metadata->thread_stack_size == 0U) ||
	    (metadata->thread_stack_size >
	     (uint32_t)CONFIG_MESHBUS_LLEXT_SERVICE_MAX_STACK_SIZE)) {
		return -E2BIG;
	}
	if ((metadata->heap_size == 0U) ||
	    (metadata->heap_size >
	     (uint32_t)CONFIG_MESHBUS_LLEXT_SERVICE_MAX_HEAP_SIZE)) {
		return -E2BIG;
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

static int service_metadata_matches_host(const struct meshbus_llext_service_metadata *metadata)
{
	return metadata_matches_host("service", metadata->id, metadata->target);
}
#endif

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_APP_SERVICES)
static int app_metadata_validate(const struct meshbus_llext_app_metadata *metadata)
{
	size_t len;

	if (metadata == NULL) {
		return -EINVAL;
	}
	if (metadata->magic != MESHBUS_LLEXT_APP_METADATA_MAGIC) {
		return -ENOEXEC;
	}
	if (metadata->metadata_version != MESHBUS_LLEXT_APP_METADATA_VERSION) {
		return -EPROTONOSUPPORT;
	}
	if (metadata->size != sizeof(*metadata)) {
		return -ENOEXEC;
	}
	len = strnlen(metadata->id, sizeof(metadata->id));
	if ((len == 0U) || (len >= sizeof(metadata->id)) ||
	    !service_id_is_valid(metadata->id)) {
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
	    (metadata->heap_size > (uint32_t)CONFIG_MESHBUS_LLEXT_APP_MAX_HEAP_SIZE)) {
		return -E2BIG;
	}
	if (metadata->icon_data_size != 0U &&
	    metadata->icon_data_size != MESHBUS_LLEXT_APP_ICON_DATA_SIZE) {
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

static int app_metadata_matches_host(const struct meshbus_llext_app_metadata *metadata)
{
	return metadata_matches_host("app", metadata->id, metadata->target);
}

static void app_metadata_fill_info(struct meshbus_llext_app_info *info, const char *path,
				   const struct meshbus_llext_app_metadata *metadata,
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
#endif

static int metadata_read_blob(const char *path, void *metadata, size_t metadata_size)
{
	static const uint8_t elf_magic[4] = {0x7f, 'E', 'L', 'F'};
	struct fs_dirent stat = {0};
	struct fs_file_t file;
	elf_ehdr_t ehdr;
	elf_shdr_t shdr;
	elf_shdr_t shstrtab_shdr;
	char *shstrtab = service_shstrtab_buf;
	size_t file_size;
	size_t shoff;
	size_t shentsize;
	size_t shnum;
	size_t shstrtab_size;
	size_t sh_table_size;
	bool metadata_found = false;
	int rc = 0;

	if ((path == NULL) || (metadata == NULL) || (metadata_size == 0U)) {
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
	    (ehdr.e_shnum > SERVICE_ELF_SCAN_MAX_SECTIONS) ||
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
	    (shstrtab_size > SERVICE_ELF_SCAN_MAX_SHSTRTAB_SIZE) ||
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
		if (strcmp(name, MESHBUS_LLEXT_SERVICE_METADATA_SECTION) != 0) {
			continue;
		}
		if (metadata_found || shdr.sh_type != SHT_PROGBITS ||
		    ((shdr.sh_flags & SHF_ALLOC) != 0U) ||
		    (shdr.sh_size != metadata_size) ||
		    !file_range_valid(file_size, (size_t)shdr.sh_offset,
				      (size_t)shdr.sh_size)) {
			rc = -ENOEXEC;
			goto out;
		}

		rc = file_read_exact(&file, (size_t)shdr.sh_offset, metadata,
				     metadata_size);
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

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
static int metadata_read_file(const char *path, struct meshbus_llext_service_metadata *metadata)
{
	int rc;

	rc = metadata_read_blob(path, metadata, sizeof(*metadata));
	if (rc != 0) {
		return rc;
	}

	return service_metadata_validate(metadata);
}
#endif

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_APP_SERVICES)
static int app_metadata_read_file(const char *path, struct meshbus_llext_app_metadata *metadata)
{
	int rc;

	rc = metadata_read_blob(path, metadata, sizeof(*metadata));
	if (rc != 0) {
		return rc;
	}

	return app_metadata_validate(metadata);
}
#endif

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
static void service_metadata_apply_slot(struct meshbus_llext_slot *slot,
					const struct meshbus_llext_service_metadata *metadata)
{
	slot->metadata = *metadata;
	cstr_copy(slot->info.id, sizeof(slot->info.id), metadata->id);
	cstr_copy(slot->info.name, sizeof(slot->info.name), metadata->name);
	cstr_copy(slot->info.description, sizeof(slot->info.description),
		  metadata->description);
	cstr_copy(slot->info.version, sizeof(slot->info.version), metadata->service_version);
	cstr_copy(slot->info.edk_version, sizeof(slot->info.edk_version),
		  metadata->edk_version);
	cstr_copy(slot->info.target, sizeof(slot->info.target), metadata->target);
	slot->info.stack_size = metadata->thread_stack_size;
	slot->info.heap_size = metadata->heap_size;
}
#endif

/* -------------------------------------------------------------------------- */
/* Service Loading                                                            */
/* -------------------------------------------------------------------------- */

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
static void event_publish(const struct meshbus_llext_state_event *evt)
{
	if (evt == NULL || evt->id[0] == '\0') {
		return;
	}

	(void)zbus_chan_pub(&meshbus_llext_state_chan, evt, K_NO_WAIT);
}

static void slot_set_state_locked(struct meshbus_llext_slot *slot,
				  enum meshbus_llext_state state, int rc,
				  struct meshbus_llext_state_event *evt)
{
	slot->info.state = state;
	slot->info.last_error = (rc < 0) ? rc : 0;

	if (evt == NULL) {
		return;
	}

	memset(evt, 0, sizeof(*evt));
	evt->kind = MESHBUS_LLEXT_KIND_SERVICE;
	evt->state = state;
	evt->rc = rc;
	cstr_copy(evt->id, sizeof(evt->id), slot->info.id);
}

static struct meshbus_llext_slot *slot_find_by_id(const char *id)
{
	for (size_t i = 0; i < ARRAY_SIZE(service_slots); i++) {
		if (service_slots[i].in_use &&
		    (strcmp(service_slots[i].info.id, id) == 0)) {
			return &service_slots[i];
		}
	}

	return NULL;
}

static struct meshbus_llext_slot *slot_alloc(const char *id)
{
	for (size_t i = 0; i < ARRAY_SIZE(service_slots); i++) {
		if (service_slots[i].in_use) {
			continue;
		}

		memset(&service_slots[i], 0, sizeof(service_slots[i]));
		service_slots[i].in_use = true;
		k_work_init(&service_slots[i].exit_cleanup_work,
			    service_exit_cleanup_work_handler);
		cstr_copy(service_slots[i].info.id, sizeof(service_slots[i].info.id), id);
		cstr_copy(service_slots[i].info.name, sizeof(service_slots[i].info.name), id);
		cstr_copy(service_slots[i].info.version,
			  sizeof(service_slots[i].info.version), "unknown");
		return &service_slots[i];
	}

	return NULL;
}

static struct discovered_service *discovered_find(struct discovered_service *out,
						  size_t out_cnt,
						  const char *id)
{
	for (size_t i = 0; i < out_cnt; i++) {
		if (strcmp(out[i].id, id) == 0) {
			return &out[i];
		}
	}

	return NULL;
}

static int service_scan_discovery(struct discovered_service *out, size_t out_len,
				  size_t *out_cnt)
{
	struct fs_dir_t dir;
	struct fs_dirent entry;
	size_t candidate_cnt = 0U;
	size_t valid_cnt = 0U;
	int rc;

	if ((out == NULL) || (out_cnt == NULL)) {
		return -EINVAL;
	}

	memset(out, 0, sizeof(*out) * out_len);
	*out_cnt = 0U;

	rc = meshbus_fs_ensure_product_dirs();
	if (rc != 0) {
		return rc;
	}

	fs_dir_t_init(&dir);
	rc = fs_opendir(&dir, CONFIG_MESHBUS_LLEXT_DEFAULT_PATH);
	if (rc != 0) {
		return rc;
	}

	while (true) {
		char path[MESHBUS_LLEXT_PATH_MAX_LEN + 1];
		struct meshbus_llext_service_metadata metadata;
		struct discovered_service *disc;

		rc = fs_readdir(&dir, &entry);
		if (rc != 0 || entry.name[0] == '\0') {
			break;
		}
		if ((entry.type != FS_DIR_ENTRY_FILE) ||
		    !has_suffix(entry.name, MESHBUS_LLEXT_SERVICE_SUFFIX)) {
			continue;
		}

		rc = service_resolve_path(entry.name, path, sizeof(path));
		if (rc != 0) {
			LOG_WRN("Skip service file '%s': invalid path", entry.name);
			continue;
		}
		rc = metadata_read_file(path, &metadata);
		if (rc != 0) {
			LOG_WRN("Skip service file '%s': invalid metadata (%d)", entry.name, rc);
			continue;
		}
		rc = service_metadata_matches_host(&metadata);
		if (rc != 0) {
			LOG_WRN("Skip service '%s': target check failed (%d)", metadata.id, rc);
			continue;
		}

		disc = discovered_find(out, candidate_cnt, metadata.id);
		if (disc != NULL) {
			if (!disc->conflict) {
				LOG_WRN("Skip service id '%s': conflict between '%s' and '%s'",
					metadata.id, disc->path, path);
			}
			disc->conflict = true;
			continue;
		}

		if (candidate_cnt >= out_len) {
			LOG_WRN("Skip service '%s': registry full", metadata.id);
			continue;
		}

		disc = &out[candidate_cnt++];
		memset(disc, 0, sizeof(*disc));
		cstr_copy(disc->id, sizeof(disc->id), metadata.id);
		cstr_copy(disc->path, sizeof(disc->path), path);
		disc->metadata = metadata;
	}

	(void)fs_closedir(&dir);
	if (rc != 0) {
		return rc;
	}

	for (size_t i = 0; i < candidate_cnt; i++) {
		if (out[i].conflict) {
			LOG_WRN("Skip service id '%s': duplicate metadata id", out[i].id);
			continue;
		}
		if (valid_cnt != i) {
			out[valid_cnt] = out[i];
		}
		valid_cnt++;
	}

	*out_cnt = valid_cnt;
	return 0;
}

static int service_register_discovered(const struct discovered_service *disc,
				       struct meshbus_llext_state_event *evt)
{
	struct meshbus_llext_slot *slot;

	slot = slot_find_by_id(disc->id);
	if (slot == NULL) {
		slot = slot_alloc(disc->id);
		if (slot == NULL) {
			return -EBUSY;
		}
	}

	cstr_copy(slot->path, sizeof(slot->path), disc->path);
	cstr_copy(slot->info.path, sizeof(slot->info.path), disc->path);
	service_metadata_apply_slot(slot, &disc->metadata);
	slot_set_state_locked(slot, MESHBUS_LLEXT_STATE_DISCOVERED, 0, evt);

	return 0;
}

static void service_thread_trampoline(void *p1, void *p2, void *p3)
{
	struct meshbus_llext_slot *slot = p1;
	struct meshbus_llext_state_event evt = {0};
	bool cleanup = false;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	if ((slot == NULL) || (slot->thread_entry_fn == NULL)) {
		return;
	}

	slot->thread_entry_fn(NULL, NULL, NULL);

	k_mutex_lock(&service_mutex, K_FOREVER);
	if (slot->in_use && (slot->thread_tid == k_current_get())) {
		slot->thread_active = false;
		slot->thread_tid = NULL;
		slot_set_state_locked(slot, MESHBUS_LLEXT_STATE_EXITED, 0, &evt);
		cleanup = true;
	}
	k_mutex_unlock(&service_mutex);

	event_publish(&evt);
	if (cleanup) {
		(void)k_work_submit(&slot->exit_cleanup_work);
	}
}
#endif

static int loaded_resource_cleanup(struct llext **ext, bool *brought_up,
				   bool *domain_added, k_thread_stack_t **thread_stack)
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
	if (thread_stack != NULL && *thread_stack != NULL) {
		int src = k_thread_stack_free(*thread_stack);

		if (src == 0) {
			*thread_stack = NULL;
		} else if (rc == 0) {
			rc = src;
		}
	}

	return rc;
}

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
static void service_exit_cleanup_work_handler(struct k_work *work)
{
	struct meshbus_llext_slot *slot =
		CONTAINER_OF(work, struct meshbus_llext_slot, exit_cleanup_work);
	struct llext *ext;
	k_thread_stack_t *thread_stack;
	bool brought_up;
	bool domain_added;
	int rc;

	rc = k_thread_join(&slot->thread, K_FOREVER);
	if (rc != 0) {
		LOG_ERR("Service exit cleanup join failed: id='%s' rc=%d",
			slot->info.id, rc);
		return;
	}

	k_mutex_lock(&service_mutex, K_FOREVER);
	if (!slot->in_use || slot->loading || slot->thread_active) {
		k_mutex_unlock(&service_mutex);
		return;
	}

	ext = slot->ext;
	thread_stack = slot->thread_stack;
	brought_up = slot->brought_up;
	domain_added = slot->domain_added;
	slot->ext = NULL;
	slot->thread_stack = NULL;
	slot->brought_up = false;
	slot->domain_added = false;
	slot->thread_entry_fn = NULL;
	k_mutex_unlock(&service_mutex);

	rc = loaded_resource_cleanup(&ext, &brought_up, &domain_added, &thread_stack);
	if (rc != 0) {
		LOG_ERR("Service exit cleanup failed: id='%s' rc=%d", slot->info.id, rc);
	}
}

static int service_start_common(const char *service_id)
{
	struct meshbus_llext_slot *slot;
	struct meshbus_llext_service_metadata metadata;
	struct llext_fs_loader fs_loader;
	struct llext_load_param load_param = LLEXT_LOAD_PARAM_DEFAULT;
	char load_name[LLEXT_MAX_NAME_LEN + 1];
	char id[MESHBUS_LLEXT_ID_MAX_LEN + 1];
	char path[MESHBUS_LLEXT_PATH_MAX_LEN + 1];
	struct llext *ext = NULL;
	k_thread_stack_t *thread_stack = NULL;
	bool brought_up = false;
	bool domain_added = false;
	const void *sym;
	uint32_t stack_size;
	k_tid_t tid;
	struct meshbus_llext_state_event events[3];
	struct meshbus_llext_state_event fault_event = {0};
	size_t event_cnt = 0U;
	int rc;

	if (!service_id_is_valid(service_id)) {
		return -EINVAL;
	}
	if (!llext_service_enabled()) {
		return -ENODEV;
	}

	k_mutex_lock(&service_mutex, K_FOREVER);
	slot = slot_find_by_id(service_id);
	if (slot == NULL) {
		k_mutex_unlock(&service_mutex);
		return -ENOENT;
	}
	if (slot->loading || slot->thread_active) {
		k_mutex_unlock(&service_mutex);
		return -EBUSY;
	}

	slot->loading = true;
	cstr_copy(id, sizeof(id), slot->info.id);
	cstr_copy(path, sizeof(path), slot->path);
	llext_load_name_build(id, load_name, sizeof(load_name));
	k_mutex_unlock(&service_mutex);

	rc = metadata_read_file(path, &metadata);
	if (rc != 0) {
		goto fail;
	}
	if ((strcmp(metadata.id, id) != 0) ||
	    (service_metadata_matches_host(&metadata) != 0)) {
		rc = -ENOEXEC;
		goto fail;
	}

	fs_loader = (struct llext_fs_loader)LLEXT_FS_LOADER(path);
	rc = llext_load(&fs_loader.loader, load_name, &ext, &load_param);
	if (rc != 0) {
		LOG_ERR("Service llext_load failed: id='%s' path='%s' rc=%d", id, path, rc);
		goto fail;
	}

	rc = llext_bringup(ext);
	if (rc != 0) {
		LOG_ERR("Service llext_bringup failed: id='%s' path='%s' rc=%d", id, path, rc);
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
		LOG_ERR("Service entry symbol not found: id='%s' symbol='%s'",
			id, metadata.entry_point_symbol);
		rc = -ENOEXEC;
		goto fail;
	}

	stack_size = metadata.thread_stack_size;
	thread_stack = k_thread_stack_alloc(stack_size, 0);
	if (thread_stack == NULL) {
		rc = -ENOMEM;
		goto fail;
	}

	k_mutex_lock(&service_mutex, K_FOREVER);
	slot = slot_find_by_id(id);
	if ((slot == NULL) || !slot->loading) {
		k_mutex_unlock(&service_mutex);
		rc = -ENOENT;
		goto fail;
	}

	slot->metadata = metadata;
	service_metadata_apply_slot(slot, &metadata);
	cstr_copy(slot->path, sizeof(slot->path), path);
	cstr_copy(slot->info.path, sizeof(slot->info.path), path);
	slot->ext = ext;
	slot->brought_up = brought_up;
	slot->domain_added = domain_added;
	slot->thread_entry_fn = (k_thread_entry_t)sym;
	slot->thread_stack = thread_stack;
	ext = NULL;
	brought_up = false;
	domain_added = false;
	thread_stack = NULL;

	slot_set_state_locked(slot, MESHBUS_LLEXT_STATE_LOADED, 0,
			      &events[event_cnt++]);
	slot_set_state_locked(slot, MESHBUS_LLEXT_STATE_BROUGHT_UP, 0,
			      &events[event_cnt++]);
	tid = k_thread_create(&slot->thread, slot->thread_stack, stack_size,
			      service_thread_trampoline, slot, NULL, NULL,
			      CONFIG_MESHBUS_LLEXT_SERVICE_PRIORITY, 0, K_FOREVER);
	if (tid == NULL) {
		struct llext *slot_ext = slot->ext;
		k_thread_stack_t *slot_stack = slot->thread_stack;
		bool slot_brought_up = slot->brought_up;
		bool slot_domain_added = slot->domain_added;

		slot->ext = NULL;
		slot->thread_stack = NULL;
		slot->brought_up = false;
		slot->domain_added = false;
		slot->thread_entry_fn = NULL;
		rc = -ENOMEM;
		slot->loading = false;
		slot_set_state_locked(slot, MESHBUS_LLEXT_STATE_FAULTED, rc,
				      &fault_event);
		k_mutex_unlock(&service_mutex);
		for (size_t i = 0U; i < event_cnt; i++) {
			event_publish(&events[i]);
		}
		event_publish(&fault_event);
		(void)loaded_resource_cleanup(&slot_ext, &slot_brought_up,
					       &slot_domain_added, &slot_stack);
		return rc;
	}
	slot->thread_tid = tid;
	slot->thread_active = true;
	slot->loading = false;
	(void)k_thread_name_set(tid, slot->info.id);
	slot_set_state_locked(slot, MESHBUS_LLEXT_STATE_RUNNING, 0,
			      &events[event_cnt++]);
	k_mutex_unlock(&service_mutex);

	for (size_t i = 0U; i < event_cnt; i++) {
		event_publish(&events[i]);
	}
	k_thread_start(tid);

	return 0;

fail:
	(void)loaded_resource_cleanup(&ext, &brought_up, &domain_added, &thread_stack);
	k_mutex_lock(&service_mutex, K_FOREVER);
	slot = slot_find_by_id(id);
	if ((slot != NULL) && slot->loading) {
		slot->loading = false;
		slot_set_state_locked(slot, MESHBUS_LLEXT_STATE_FAULTED, rc,
				      &fault_event);
	}
	k_mutex_unlock(&service_mutex);
	event_publish(&fault_event);
	return rc;
}

static int service_boot_impl(void)
{
	size_t discovered_cnt = 0U;
	struct meshbus_llext_state_event discovered_events[CONFIG_MESHBUS_LLEXT_MAX_SERVICES];
	size_t discovered_event_cnt = 0U;
	int first_err = 0;
	int rc;

	memset(service_slots, 0, sizeof(service_slots));

	rc = service_scan_discovery(service_boot_discovered,
				    ARRAY_SIZE(service_boot_discovered),
				    &discovered_cnt);
	if (rc != 0) {
		return rc;
	}

	rc = meshbus_llext_heap_prepare(service_boot_discovered, discovered_cnt,
					0U);
	if (rc != 0) {
		return rc;
	}

	k_mutex_lock(&service_mutex, K_FOREVER);
	for (size_t i = 0; i < discovered_cnt; i++) {
		struct meshbus_llext_state_event *evt = NULL;

		if (discovered_event_cnt < ARRAY_SIZE(discovered_events)) {
			evt = &discovered_events[discovered_event_cnt];
		}
		rc = service_register_discovered(&service_boot_discovered[i], evt);
		if (rc != 0) {
			LOG_WRN("Skip service '%s': registry full",
				service_boot_discovered[i].id);
		} else if (evt != NULL) {
			discovered_event_cnt++;
		}
	}
	k_mutex_unlock(&service_mutex);

	for (size_t i = 0U; i < discovered_event_cnt; i++) {
		event_publish(&discovered_events[i]);
	}

	for (size_t i = 0; i < discovered_cnt; i++) {
		rc = service_start_common(service_boot_discovered[i].id);
		if (rc != 0) {
			LOG_WRN("Start service '%s' failed: %d",
				service_boot_discovered[i].id, rc);
			if (first_err == 0) {
				first_err = rc;
			}
		}
	}

	return first_err;
}
#endif

/* -------------------------------------------------------------------------- */
/* Desktop App Loading                                                        */
/* -------------------------------------------------------------------------- */

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_APP_SERVICES)
static int app_path_validate(const char *path)
{
	size_t len;

	if (path == NULL) {
		return -EINVAL;
	}

	len = strnlen(path, MESHBUS_LLEXT_PATH_MAX_LEN + 1U);
	if ((len == 0U) || (len > MESHBUS_LLEXT_PATH_MAX_LEN)) {
		return -EINVAL;
	}
	if (!has_suffix(path, MESHBUS_LLEXT_APP_SUFFIX)) {
		return -EINVAL;
	}

	return 0;
}

static int app_metadata_probe_common(const char *path,
				     struct meshbus_llext_app_metadata *metadata,
				     struct meshbus_llext_app_info *info)
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

static bool app_session_matches(const struct meshbus_llext_app_session *session)
{
	return session == &app_session;
}

static int app_heap_check(const struct meshbus_llext_app_metadata *metadata)
{
	if (metadata == NULL) {
		return -EINVAL;
	}
	if ((metadata->heap_size == 0U) ||
	    (metadata->heap_size > (uint32_t)CONFIG_MESHBUS_LLEXT_APP_MAX_HEAP_SIZE) ||
	    (metadata->heap_size > (uint32_t)CONFIG_MESHBUS_LLEXT_APP_HEAP_RESERVE_SIZE)) {
		return -E2BIG;
	}

	return 0;
}

static int app_loaded_service_heap(size_t *heap_out)
{
	if (heap_out == NULL) {
		return -EINVAL;
	}

	*heap_out = 0U;
	return 0;
}

static int app_heap_prepare(const struct meshbus_llext_app_metadata *metadata)
{
	size_t requested;
	int rc;

	if (metadata == NULL) {
		return -EINVAL;
	}

	rc = app_loaded_service_heap(&requested);
	if (rc != 0) {
		return rc;
	}
	if (SIZE_MAX - requested < metadata->heap_size) {
		return -EOVERFLOW;
	}
	requested += metadata->heap_size;

	return meshbus_llext_heap_prepare(NULL, 0U, requested);
}

int meshbus_llext_app_probe(const char *path, struct meshbus_llext_app_info *info)
{
	struct meshbus_llext_app_metadata metadata;
	int rc;

	if (info == NULL) {
		return -EINVAL;
	}
	rc = app_path_validate(path);
	if (rc != 0) {
		return rc;
	}
	if (!llext_service_enabled()) {
		memset(info, 0, sizeof(*info));
		cstr_copy(info->path, sizeof(info->path), path);
		info->last_error = -ENODEV;
		return -ENODEV;
	}

	return app_metadata_probe_common(path, &metadata, info);
}

int meshbus_llext_app_load(const char *path,
			   struct meshbus_llext_app_session **session_out)
{
	struct meshbus_llext_app_metadata metadata;
	struct llext_fs_loader fs_loader;
	struct llext_load_param load_param = LLEXT_LOAD_PARAM_DEFAULT;
	char load_name[LLEXT_MAX_NAME_LEN + 1];
	struct llext *ext = NULL;
	bool brought_up = false;
	bool domain_added = false;
	const void *sym;
	int rc;

	if (session_out == NULL) {
		return -EINVAL;
	}
	*session_out = NULL;
	rc = app_path_validate(path);
	if (rc != 0) {
		return rc;
	}
	if (!llext_service_enabled()) {
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

	rc = app_heap_prepare(&metadata);
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
	app_session.entry_fn = (meshbus_llext_app_entry_t)sym;
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
	(void)loaded_resource_cleanup(&ext, &brought_up, &domain_added, NULL);
	k_mutex_lock(&app_mutex, K_FOREVER);
	memset(&app_session, 0, sizeof(app_session));
	k_mutex_unlock(&app_mutex);
	meshbus_llext_heap_release_if_idle();
	return rc;
}

int meshbus_llext_app_get_info(const struct meshbus_llext_app_session *session,
			       struct meshbus_llext_app_info *info)
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

int meshbus_llext_app_get_entry(const struct meshbus_llext_app_session *session,
				meshbus_llext_app_entry_t *entry_out)
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

int meshbus_llext_app_unload(struct meshbus_llext_app_session *session)
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
	k_mutex_unlock(&app_mutex);

	rc = loaded_resource_cleanup(&ext, &brought_up, &domain_added, NULL);

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
		meshbus_llext_heap_release_if_idle();
	}

	return rc;
}
#else
int meshbus_llext_app_probe(const char *path, struct meshbus_llext_app_info *info)
{
	ARG_UNUSED(path);
	ARG_UNUSED(info);

	return -ENOTSUP;
}

int meshbus_llext_app_load(const char *path,
			   struct meshbus_llext_app_session **session_out)
{
	ARG_UNUSED(path);

	if (session_out == NULL) {
		return -EINVAL;
	}
	*session_out = NULL;
	return -ENOTSUP;
}

int meshbus_llext_app_get_info(const struct meshbus_llext_app_session *session,
			       struct meshbus_llext_app_info *info)
{
	ARG_UNUSED(session);
	ARG_UNUSED(info);

	return -ENOTSUP;
}

int meshbus_llext_app_get_entry(const struct meshbus_llext_app_session *session,
				meshbus_llext_app_entry_t *entry_out)
{
	ARG_UNUSED(session);
	ARG_UNUSED(entry_out);

	return -ENOTSUP;
}

int meshbus_llext_app_unload(struct meshbus_llext_app_session *session)
{
	ARG_UNUSED(session);

	return -ENOTSUP;
}
#endif

/* -------------------------------------------------------------------------- */
/* Settings Apply                                                             */
/* -------------------------------------------------------------------------- */

static int llext_config_validate(const meshbus_llext_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}
	if (cfg->boot_delay < MESHBUS_LLEXT_BOOT_DELAY_MIN_MS ||
	    cfg->boot_delay > MESHBUS_LLEXT_BOOT_DELAY_MAX_MS) {
		LOG_ERR("Invalid boot_delay: %u (valid: %u..%u)",
			(unsigned int)cfg->boot_delay,
			(unsigned int)MESHBUS_LLEXT_BOOT_DELAY_MIN_MS,
			(unsigned int)MESHBUS_LLEXT_BOOT_DELAY_MAX_MS);
		return -EINVAL;
	}

	return 0;
}

static int settings_handler_apply(const meshbus_llext_config *cfg, bool persistence,
				  bool force)
{
	meshbus_llext_config prev_cfg;

	if (llext_config_validate(cfg) != 0) {
		return -EINVAL;
	}

	k_mutex_lock(&settings_mutex, K_FOREVER);
	prev_cfg = llext_cfg;
	if (!force &&
	    (prev_cfg.enabled == cfg->enabled) &&
	    (prev_cfg.boot_delay == cfg->boot_delay)) {
		settings_initial_apply = true;
		k_mutex_unlock(&settings_mutex);
		return 0;
	}

	llext_cfg = *cfg;
	settings_initial_apply = true;
	k_mutex_unlock(&settings_mutex);

	LOG_INF("Settings apply: enabled=%s boot_delay=%u", cfg->enabled ? "yes" : "no",
		(unsigned int)cfg->boot_delay);

	if (persistence) {
		(void)k_work_reschedule(&settings_persistence_work,
				       K_MSEC(CONFIG_MESHBUS_SETTINGS_PERSISTENCE_DELAY));
	}

	return 0;
}

MB_SETTINGS_BLOB_CONFIG_DEFINE(llext_settings_schema, settings_mutex, settings_load_state,
			       settings_load_cfg, llext_cfg, settings_initial_apply,
			       meshbus_llext_config, meshbus_LlextConfig_size,
			       settings_handler_apply, "LLEXT")

SETTINGS_STATIC_HANDLER_DEFINE(meshbus_llext, MESHBUS_LLEXT_SETTINGS_SUBTREE, NULL,
			       settings_handle_set, settings_handle_commit,
			       settings_handle_export);

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

int meshbus_llext_config_get(meshbus_llext_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	llext_config_copy(cfg);
	return 0;
}

int meshbus_llext_config_set(const meshbus_llext_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	return settings_handler_apply(cfg, true, false);
}

int meshbus_llext_config_reset(void)
{
	struct k_work_sync sync;
	meshbus_llext_config cfg = llext_cfg_defaults;
	int rc;

	(void)k_work_cancel_delayable_sync(&settings_persistence_work, &sync);

	rc = settings_handler_apply(&cfg, false, true);
	if (rc != 0) {
		return rc;
	}

	rc = mb_settings_blob_delete(&llext_settings_schema);
	if (rc != 0) {
		LOG_ERR("Failed to delete persisted settings: %d", rc);
		return rc;
	}

	return 0;
}

int meshbus_llext_service_count(size_t *count)
{
#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
	size_t c = 0U;

	if (count == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&service_mutex, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(service_slots); i++) {
		if (service_slots[i].in_use) {
			c++;
		}
	}
	k_mutex_unlock(&service_mutex);

	*count = c;
	return 0;
#else
	if (count == NULL) {
		return -EINVAL;
	}
	*count = 0U;
	return -ENOTSUP;
#endif
}

int meshbus_llext_service_get(size_t index, struct meshbus_llext_service_info *info)
{
#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
	size_t pos = 0U;

	if (info == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&service_mutex, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(service_slots); i++) {
		if (!service_slots[i].in_use) {
			continue;
		}
		if (pos == index) {
			*info = service_slots[i].info;
			k_mutex_unlock(&service_mutex);
			return 0;
		}
		pos++;
	}
	k_mutex_unlock(&service_mutex);

	return -ENOENT;
#else
	ARG_UNUSED(index);

	if (info == NULL) {
		return -EINVAL;
	}
	return -ENOTSUP;
#endif
}

int meshbus_llext_service_status(const char *service_id,
				 struct meshbus_llext_service_info *info)
{
#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
	struct meshbus_llext_slot *slot;

	if (!service_id_is_valid(service_id) || (info == NULL)) {
		return -EINVAL;
	}

	k_mutex_lock(&service_mutex, K_FOREVER);
	slot = slot_find_by_id(service_id);
	if (slot == NULL) {
		k_mutex_unlock(&service_mutex);
		return -ENOENT;
	}

	*info = slot->info;
	k_mutex_unlock(&service_mutex);
	return 0;
#else
	ARG_UNUSED(service_id);

	if (info == NULL) {
		return -EINVAL;
	}
	return -ENOTSUP;
#endif
}

bool meshbus_llext_runtime_busy(void)
{
	bool busy = false;

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
	k_mutex_lock(&service_mutex, K_FOREVER);
	for (size_t i = 0U; i < ARRAY_SIZE(service_slots); i++) {
		if (service_slots[i].in_use || service_slots[i].loading ||
		    service_slots[i].thread_active || service_slots[i].brought_up) {
			busy = true;
			break;
		}
	}
	k_mutex_unlock(&service_mutex);

	if (busy) {
		return true;
	}
#endif

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_APP_SERVICES)
	k_mutex_lock(&app_mutex, K_FOREVER);
	busy = app_session.in_use || app_session.loading ||
	       app_session.unloading || app_session.brought_up;
	k_mutex_unlock(&app_mutex);
#endif

	return busy;
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

static int meshbus_llext_heap_prepare(const struct discovered_service *services,
				      size_t service_cnt, size_t extra_heap)
{
#if defined(CONFIG_LLEXT_HEAP_DYNAMIC)
	size_t requested = extra_heap;
	size_t heap_size;
	int rc;

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
	for (size_t i = 0; i < service_cnt; i++) {
		size_t service_heap = services[i].metadata.heap_size;

		if (services[i].metadata.thread_stack_size >
		    (uint32_t)CONFIG_MESHBUS_LLEXT_SERVICE_MAX_STACK_SIZE) {
			return -E2BIG;
		}
		if (service_heap > (size_t)CONFIG_MESHBUS_LLEXT_SERVICE_MAX_HEAP_SIZE) {
			return -E2BIG;
		}
		if (SIZE_MAX - requested < service_heap) {
			return -EOVERFLOW;
		}
		requested += service_heap;
	}
#else
	ARG_UNUSED(services);
	ARG_UNUSED(service_cnt);
#endif

	if (requested == 0U) {
		return 0;
	}

	if (requested > SIZE_MAX /
	    (100U + CONFIG_MESHBUS_LLEXT_DYNAMIC_HEAP_MARGIN_PERCENT)) {
		return -EOVERFLOW;
	}
	heap_size = DIV_ROUND_UP(requested *
				 (100U + CONFIG_MESHBUS_LLEXT_DYNAMIC_HEAP_MARGIN_PERCENT),
				 100U);
	if (heap_size > (size_t)CONFIG_MESHBUS_LLEXT_TOTAL_HEAP_MAX_SIZE) {
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
		(unsigned int)CONFIG_MESHBUS_LLEXT_DYNAMIC_HEAP_MARGIN_PERCENT);
#else
	ARG_UNUSED(services);
	ARG_UNUSED(service_cnt);
	ARG_UNUSED(extra_heap);
#endif
	return 0;
}

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_APP_SERVICES)
static void meshbus_llext_heap_release_if_idle(void)
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
#endif

static void meshbus_llext_bootstrap(void *p1, void *p2, void *p3)
{
	meshbus_llext_config cfg;
	int rc;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	rc = settings_load_subtree(MESHBUS_LLEXT_SETTINGS_SUBTREE);
	if (rc != 0) {
		LOG_WRN("LLEXT settings load failed: %d", rc);
	}

	if (!settings_initial_apply_get()) {
		cfg = llext_cfg_defaults;
		rc = settings_handler_apply(&cfg, false, true);
		if (rc != 0) {
			LOG_WRN("LLEXT default settings apply failed: %d", rc);
		}
	}

	llext_config_copy(&cfg);
	if (cfg.enabled) {
#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
		if (cfg.boot_delay > 0U) {
			k_sleep(K_MSEC(cfg.boot_delay));
		}
		rc = service_boot_impl();
		if (rc != 0) {
			LOG_WRN("LLEXT service boot scan failed: %d", rc);
		}
#else
		LOG_INF("LLEXT app service runtime enabled");
#endif
	} else {
		LOG_INF("LLEXT runtime disabled by config");
	}

	LOG_INF("Meshbus LLEXT runtime ready");
}

static int meshbus_llext_init(void)
{
	k_work_init_delayable(&settings_persistence_work, settings_persistence_work_handler);

#if IS_ENABLED(CONFIG_MESHBUS_LLEXT_BOOT_SERVICES)
	k_mutex_lock(&service_mutex, K_FOREVER);
	memset(service_slots, 0, sizeof(service_slots));
	k_mutex_unlock(&service_mutex);
#endif

	(void)k_thread_create(&init_thread, init_stack, K_THREAD_STACK_SIZEOF(init_stack),
			      meshbus_llext_bootstrap, NULL, NULL, NULL,
			      CONFIG_MESHBUS_LLEXT_SERVICE_PRIORITY, 0, K_NO_WAIT);
	(void)k_thread_name_set(&init_thread, "mb_llext_init");

	return 0;
}
SYS_INIT(meshbus_llext_init, APPLICATION, 99);
