/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Copyright (c) 2026 FoBE Studio
 */

/**
 * @file
 * @brief Meshbus Desktop app loading and runtime configuration API
 *
 * This module provides path-based APIs for Desktop to inspect and run `.mba` app
 * extensions from `/extra/apps`.
 */

#ifndef MESHBUS_INCLUDE_LLEXT_H_
#define MESHBUS_INCLUDE_LLEXT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <llext/metadata.h>

#include "meshbus/llext.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum stored filesystem path length (without trailing NUL). */
#define MBS_LLEXT_PATH_MAX_LEN 191
/** Filesystem suffix for Meshbus LLEXT Desktop app artifacts. */
#define MBS_LLEXT_APP_SUFFIX ".mba"
/** Default filesystem directory for Meshbus LLEXT Desktop app artifacts. */
#define MBS_LLEXT_APP_DEFAULT_PATH "/extra/apps"


/** @brief Desktop app entry function type resolved from a loaded `.mba`. */
typedef void (*mbs_llext_app_entry_t)(void *args);

/** @brief Opaque loaded Desktop app LLEXT session. */
struct mbs_llext_app_session;

/** @brief Desktop app metadata snapshot returned by probe/load APIs. */
struct mbs_llext_app_info {
	char id[MBS_LLEXT_ID_MAX_LEN + 1];
	char name[MBS_LLEXT_NAME_MAX_LEN + 1];
	char version[MBS_LLEXT_VERSION_MAX_LEN + 1];
	char path[MBS_LLEXT_PATH_MAX_LEN + 1];
	char entry_point_symbol[MBS_LLEXT_SYMBOL_MAX_LEN + 1];
	char edk_version[MBS_LLEXT_VERSION_MAX_LEN + 1];
	char target[MBS_LLEXT_TARGET_MAX_LEN + 1];
	uint32_t stack_size;
	uint32_t heap_size;
	uint32_t icon_data_size;
	uint8_t icon_data[MBS_LLEXT_APP_ICON_DATA_MAX_LEN];
	int32_t last_error;
};

/** @brief LLEXT runtime configuration (maps to meshbus_LlextConfig). */
typedef meshbus_LlextConfig mbs_llext_config;

/** @brief Copy device, firmware image and EDK compatibility identities.
 * May block while reading flash. Does not modify settings or firmware.
 * @param info Caller-owned output, valid on success.
 * @return 0, -EINVAL, -ENOTSUP, or a negative flash/hardware error.
 */
int mbs_llext_host_info_get(meshbus_LlextHostInfoResponse *info);

/**
 * @brief Copy the current LLEXT application configuration.
 *
 * @param cfg Output configuration snapshot.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p cfg is NULL.
 */
int mbs_llext_config_get(mbs_llext_config *cfg);

/**
 * @brief Apply and persist the LLEXT application configuration.
 *
 * When `enabled` is false, new Desktop app probe/load requests are rejected.
 * Changing configuration does not interrupt an already loaded application.
 *
 * @param cfg Configuration to apply.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p cfg is NULL or contains an invalid value.
 */
int mbs_llext_config_set(const mbs_llext_config *cfg);

/**
 * @brief Reset LLEXT application configuration to defaults.
 *
 * Resetting configuration does not interrupt an already loaded application.
 *
 * @retval 0 on success.
 * @retval negative errno on settings delete failure.
 */
int mbs_llext_config_reset(void);

/**
 * @brief Return whether any LLEXT runtime resource is active.
 *
 * Filesystem lifecycle code uses this as a conservative busy gate before
 * destructive storage operations such as formatting the extension volume.
 *
 * @retval true if a Desktop app session is active.
 * @retval false if no LLEXT runtime object is active.
 */
bool mbs_llext_runtime_busy(void);

/**
 * @brief Read and validate `.mba` app metadata without loading the extension.
 *
 * Probe is metadata-only and does not reserve LLEXT heap. The exact target
 * must match the running firmware for probe to succeed. The EDK version is
 * provenance only and is not a compatibility gate. Probe is rejected when
 * LLEXT is disabled by configuration.
 *
 * @param path Full filesystem path to a `.mba` file.
 * @param info Output metadata snapshot.
 *
 * @retval 0 on success.
 * @retval -EINVAL if arguments are invalid.
 * @retval -ENODEV if LLEXT is disabled by configuration.
 * @retval -ENOEXEC if metadata is malformed.
 * @retval -EPROTONOSUPPORT if the metadata version is unsupported.
 * @retval -EXDEV if the package target differs from the host target.
 * @retval negative errno from filesystem access.
 */
int mbs_llext_app_probe(const char *path, struct mbs_llext_app_info *info);

/**
 * @brief Load one selected `.mba` app and prepare its entry symbol.
 *
 * Load re-reads metadata and is authoritative even if a previous probe
 * succeeded. Only one app session can be loaded at a time. Load is rejected
 * when LLEXT is disabled by configuration.
 *
 * @param path Full filesystem path to a `.mba` file.
 * @param session_out Output app session handle. On failure this is normally
 *                    NULL. If failure cleanup also fails, a non-NULL handle
 *                    retains the partial resources; the caller must keep it
 *                    and retry mbs_llext_app_unload() before loading again.
 *                    Such a session has no callable app entry.
 *
 * @retval 0 on success.
 * @retval -EINVAL if arguments are invalid.
 * @retval -ENODEV if LLEXT is disabled by configuration.
 * @retval -EBUSY if another app session is loaded or loading.
 * @retval -E2BIG if the app heap estimate exceeds the configured app budget.
 * @retval -ENOEXEC if metadata, bring-up, or entry resolution fails.
 * @retval negative errno from filesystem, Zephyr LLEXT loading, or failure cleanup.
 */
int mbs_llext_app_load(const char *path,
			   struct mbs_llext_app_session **session_out);

/**
 * @brief Copy metadata for a loaded app session.
 *
 * @param session Loaded app session.
 * @param info Output app metadata snapshot.
 *
 * @retval 0 on success.
 * @retval -EINVAL if arguments are invalid.
 * @retval -ENOENT if @p session is not a loaded app session.
 */
int mbs_llext_app_get_info(const struct mbs_llext_app_session *session,
			       struct mbs_llext_app_info *info);

/**
 * @brief Return the typed Desktop app entry for a loaded app session.
 *
 * The entry is unavailable once unloading begins, including when reclamation
 * fails and the session is retained for a later retry.
 *
 * @param session Loaded app session.
 * @param entry_out Output Desktop-compatible blocking entry function.
 *
 * @retval 0 on success.
 * @retval -EINVAL if arguments are invalid.
 * @retval -ENOENT if @p session has no executable app entry.
 */
int mbs_llext_app_get_entry(const struct mbs_llext_app_session *session,
				mbs_llext_app_entry_t *entry_out);

/**
 * @brief Unload a loaded app session after its entry has returned.
 *
 * The caller must ensure no app-owned callbacks, work items, timers, or ZBus
 * observers can still call into extension code.
 * On failure the session remains owned by the caller and blocks new loads;
 * keep the handle and retry unloading. This also reclaims a partial session
 * returned when load failure cleanup could not finish.
 *
 * @param session Loaded app session.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p session is NULL.
 * @retval -ENOENT if @p session is not currently loaded.
 * @retval -EBUSY if unload is already in progress.
 * @retval negative errno from LLEXT teardown/unload.
 */
int mbs_llext_app_unload(struct mbs_llext_app_session *session);

#ifdef __cplusplus
}
#endif

#endif /* MESHBUS_INCLUDE_LLEXT_H_ */
