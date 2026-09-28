/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FOBE_SUBSYS_MBS_COMMON_MB_MGMT_H_
#define FOBE_SUBSYS_MBS_COMMON_MB_MGMT_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pb.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>

struct smp_streamer;

#define MBS_MGMT_DATA_KEY "data"

#ifdef CONFIG_MCUMGR_GRP_FS_FILE_ACCESS_HOOK
#include <zephyr/mgmt/mcumgr/grp/fs_mgmt/fs_mgmt_callbacks.h>

bool mbs_mgmt_fs_access_type_audited(enum fs_mgmt_file_access_types access);
#endif

#ifdef CONFIG_MCUMGR_GRP_SETTINGS_ACCESS_HOOK
#include <zephyr/mgmt/mcumgr/grp/settings_mgmt/settings_mgmt_callbacks.h>

bool mbs_mgmt_settings_name_allowed(const char *name);
bool mbs_mgmt_settings_access_allowed(enum settings_mgmt_access_types access, const char *name);
#endif

int mbs_mgmt_decode_proto(struct smp_streamer *ctxt, void *dst, size_t dst_size,
			 const pb_msgdesc_t *fields, bool allow_empty);

int mbs_mgmt_encode_proto(struct smp_streamer *ctxt, const void *src, const pb_msgdesc_t *fields,
			 size_t max_size);

#define MBS_MGMT_CONFIG_GET_HANDLER_DEFINE(name, req_type, rsp_type, get_fn, req_fields,           \
                                         rsp_fields, max_rsp_size)                                \
	static int name(struct smp_streamer *ctxt)                                                \
	{                                                                                         \
		req_type req;                                                                     \
		rsp_type rsp = {0};                                                               \
		int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req), req_fields, true);         \
                                                                                                  \
		if (rc != 0) {                                                                    \
			return rc;                                                                \
		}                                                                                 \
		rc = get_fn(&rsp.config);                                                         \
		if (rc != 0) {                                                                    \
			return rc;                                                                \
		}                                                                                 \
		rsp.has_config = true;                                                            \
		return mbs_mgmt_encode_proto(ctxt, &rsp, rsp_fields, max_rsp_size);                \
	}

#define MBS_MGMT_CONFIG_SET_HANDLER_DEFINE(name, req_type, rsp_type, set_fn, req_fields,           \
                                         rsp_fields, max_rsp_size)                                \
	static int name(struct smp_streamer *ctxt)                                                \
	{                                                                                         \
		req_type req;                                                                     \
		rsp_type rsp = {0};                                                               \
		int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req), req_fields, false);        \
                                                                                                  \
		if (rc != 0) {                                                                    \
			return rc;                                                                \
		}                                                                                 \
		if (!req.has_config) {                                                            \
			return -EINVAL;                                                           \
		}                                                                                 \
		rc = set_fn(&req.config);                                                         \
		if (rc != 0) {                                                                    \
			return rc;                                                                \
		}                                                                                 \
		rsp.has_config = true;                                                            \
		rsp.config = req.config;                                                          \
		return mbs_mgmt_encode_proto(ctxt, &rsp, rsp_fields, max_rsp_size);                \
	}

#define MBS_MGMT_CONFIG_RESET_HANDLER_DEFINE(name, req_type, rsp_type, reset_fn, get_fn,           \
                                           req_fields, rsp_fields, max_rsp_size)                  \
	static int name(struct smp_streamer *ctxt)                                                \
	{                                                                                         \
		req_type req;                                                                     \
		rsp_type rsp = {0};                                                               \
		int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req), req_fields, true);         \
                                                                                                  \
		if (rc != 0) {                                                                    \
			return rc;                                                                \
		}                                                                                 \
		rc = reset_fn();                                                                  \
		if (rc != 0) {                                                                    \
			return rc;                                                                \
		}                                                                                 \
		rc = get_fn(&rsp.config);                                                         \
		if (rc != 0) {                                                                    \
			return rc;                                                                \
		}                                                                                 \
		rsp.has_config = true;                                                            \
		return mbs_mgmt_encode_proto(ctxt, &rsp, rsp_fields, max_rsp_size);                \
	}

#endif /* FOBE_SUBSYS_MBS_COMMON_MB_MGMT_H_ */
