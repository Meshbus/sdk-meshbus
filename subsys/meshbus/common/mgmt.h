/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FOBE_SUBSYS_MESHBUS_COMMON_MB_MGMT_H_
#define FOBE_SUBSYS_MESHBUS_COMMON_MB_MGMT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pb.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>

struct smp_streamer;

#define MESHBUS_MGMT_DATA_KEY "data"

#ifdef CONFIG_MCUMGR_GRP_FS_FILE_ACCESS_HOOK
#include <zephyr/mgmt/mcumgr/grp/fs_mgmt/fs_mgmt_callbacks.h>

bool mb_mgmt_fs_access_type_audited(enum fs_mgmt_file_access_types access);
#endif

#ifdef CONFIG_MCUMGR_GRP_SETTINGS_ACCESS_HOOK
#include <zephyr/mgmt/mcumgr/grp/settings_mgmt/settings_mgmt_callbacks.h>

bool mb_mgmt_settings_name_allowed(const char *name);
bool mb_mgmt_settings_access_allowed(enum settings_mgmt_access_types access, const char *name);
#endif

int mb_mgmt_decode_proto(struct smp_streamer *ctxt, void *dst, size_t dst_size,
			 const pb_msgdesc_t *fields, bool allow_empty);

int mb_mgmt_encode_proto(struct smp_streamer *ctxt, const void *src, const pb_msgdesc_t *fields,
			 size_t max_size);

typedef int (*mb_mgmt_config_get_fn)(void *cfg);
typedef int (*mb_mgmt_config_set_fn)(const void *cfg);
typedef int (*mb_mgmt_config_reset_fn)(void);
typedef int (*mb_mgmt_config_validate_fn)(const void *cfg);

int mb_mgmt_config_get_proto(struct smp_streamer *ctxt, void *req, size_t req_size,
			     const pb_msgdesc_t *req_fields, void *rsp, size_t rsp_size,
			     const pb_msgdesc_t *rsp_fields, size_t max_rsp_size, void *cfg,
			     size_t cfg_size, mb_mgmt_config_get_fn get_fn,
			     size_t rsp_has_config_offset, size_t rsp_config_offset);

int mb_mgmt_config_set_proto(struct smp_streamer *ctxt, void *req, size_t req_size,
			     const pb_msgdesc_t *req_fields, void *rsp, size_t rsp_size,
			     const pb_msgdesc_t *rsp_fields, size_t max_rsp_size, void *cfg,
			     size_t cfg_size, mb_mgmt_config_set_fn set_fn,
			     mb_mgmt_config_validate_fn validate_fn, size_t req_has_config_offset,
			     size_t req_config_offset, size_t rsp_has_config_offset,
			     size_t rsp_config_offset);

int mb_mgmt_config_reset_proto(struct smp_streamer *ctxt, void *req, size_t req_size,
			       const pb_msgdesc_t *req_fields, void *rsp, size_t rsp_size,
			       const pb_msgdesc_t *rsp_fields, size_t max_rsp_size, void *cfg,
			       size_t cfg_size, mb_mgmt_config_reset_fn reset_fn,
			       mb_mgmt_config_get_fn get_fn, size_t rsp_has_config_offset,
			       size_t rsp_config_offset);

#define MB_MGMT_CONFIG_GET_HANDLER_DEFINE(name, req_type, rsp_type, cfg_type, get_fn, req_fields,  \
					  rsp_fields, max_rsp_size)                                \
	static int name##_get_config(void *cfg)                                                    \
	{                                                                                          \
		return get_fn((cfg_type *)cfg);                                                    \
	}                                                                                          \
                                                                                                   \
	static int name(struct smp_streamer *ctxt)                                                 \
	{                                                                                          \
		req_type req;                                                                      \
		rsp_type rsp;                                                                      \
		cfg_type cfg;                                                                      \
		return mb_mgmt_config_get_proto(                                                   \
			ctxt, &req, sizeof(req), req_fields, &rsp, sizeof(rsp), rsp_fields,        \
			max_rsp_size, &cfg, sizeof(cfg), name##_get_config,                        \
			offsetof(rsp_type, has_config), offsetof(rsp_type, config));               \
	}

#define MB_MGMT_CONFIG_SET_HANDLER_DEFINE(name, req_type, rsp_type, cfg_type, set_fn, validate_fn, \
					  req_fields, rsp_fields, max_rsp_size)                    \
	static int name##_set_config(const void *cfg)                                              \
	{                                                                                          \
		return set_fn((const cfg_type *)cfg);                                              \
	}                                                                                          \
                                                                                                   \
	static int name##_validate_config(const void *cfg)                                         \
	{                                                                                          \
		return validate_fn((const cfg_type *)cfg);                                         \
	}                                                                                          \
                                                                                                   \
	static int name(struct smp_streamer *ctxt)                                                 \
	{                                                                                          \
		req_type req;                                                                      \
		rsp_type rsp;                                                                      \
		cfg_type cfg;                                                                      \
		return mb_mgmt_config_set_proto(                                                   \
			ctxt, &req, sizeof(req), req_fields, &rsp, sizeof(rsp), rsp_fields,        \
			max_rsp_size, &cfg, sizeof(cfg), name##_set_config,                        \
			name##_validate_config, offsetof(req_type, has_config),                    \
			offsetof(req_type, config), offsetof(rsp_type, has_config),                \
			offsetof(rsp_type, config));                                               \
	}

#define MB_MGMT_CONFIG_SET_HANDLER_DEFINE_NO_VALIDATE(name, req_type, rsp_type, cfg_type, set_fn,   \
						      req_fields, rsp_fields, max_rsp_size)            \
	static int name##_set_config(const void *cfg)                                              \
	{                                                                                          \
		return set_fn((const cfg_type *)cfg);                                              \
	}                                                                                          \
                                                                                                   \
	static int name(struct smp_streamer *ctxt)                                                 \
	{                                                                                          \
		req_type req;                                                                      \
		rsp_type rsp;                                                                      \
		cfg_type cfg;                                                                      \
		return mb_mgmt_config_set_proto(                                                   \
			ctxt, &req, sizeof(req), req_fields, &rsp, sizeof(rsp), rsp_fields,        \
			max_rsp_size, &cfg, sizeof(cfg), name##_set_config, NULL,                  \
			offsetof(req_type, has_config), offsetof(req_type, config),                \
			offsetof(rsp_type, has_config), offsetof(rsp_type, config));               \
	}

#define MB_MGMT_CONFIG_RESET_HANDLER_DEFINE(name, req_type, rsp_type, cfg_type, reset_fn, get_fn,  \
					    req_fields, rsp_fields, max_rsp_size)                  \
	static int name##_get_config(void *cfg)                                                    \
	{                                                                                          \
		return get_fn((cfg_type *)cfg);                                                    \
	}                                                                                          \
                                                                                                   \
	static int name(struct smp_streamer *ctxt)                                                 \
	{                                                                                          \
		req_type req;                                                                      \
		rsp_type rsp;                                                                      \
		cfg_type cfg;                                                                      \
		return mb_mgmt_config_reset_proto(                                                 \
			ctxt, &req, sizeof(req), req_fields, &rsp, sizeof(rsp), rsp_fields,        \
			max_rsp_size, &cfg, sizeof(cfg), reset_fn, name##_get_config,              \
			offsetof(rsp_type, has_config), offsetof(rsp_type, config));               \
	}

#endif /* FOBE_SUBSYS_MESHBUS_COMMON_MB_MGMT_H_ */
