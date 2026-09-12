/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/sys/util.h>

#include <meshcore/meshcore.h>

#include "mbs_mgmt_internal.h"
#include "meshbus/meshcore.pb.h"

LOG_MODULE_REGISTER(mbs_meshcore_mgmt, CONFIG_MBS_MESHCORE_LOG_LEVEL);

#define MBS_MESHCORE_MGMT_PROTO_RSP_MAX_SIZE MESHBUS_MESHBUS_MESHCORE_PB_H_MAX_SIZE

#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
static int mbs_meshcore_mgmt_translate_error_code(uint16_t err)
{
	return (int)err;
}
#endif

static int mbs_meshcore_mgmt_encode_config_response(struct smp_streamer *ctxt,
							const mbs_meshcore_config *cfg,
							const pb_msgdesc_t *fields,
							void *rsp)
{
	mbs_meshcore_config response_cfg;

	if (cfg == NULL) {
		return -EINVAL;
	}
	response_cfg = *cfg;

	if (fields == meshbus_MeshcoreConfigGetResponse_fields) {
		meshbus_MeshcoreConfigGetResponse *typed_rsp = rsp;

		typed_rsp->has_config = true;
		typed_rsp->config = response_cfg;
	} else if (fields == meshbus_MeshcoreConfigSetResponse_fields) {
		meshbus_MeshcoreConfigSetResponse *typed_rsp = rsp;

		typed_rsp->has_config = true;
		typed_rsp->config = response_cfg;
	} else {
		meshbus_MeshcoreConfigResetResponse *typed_rsp = rsp;

		typed_rsp->has_config = true;
		typed_rsp->config = response_cfg;
	}

	return mbs_mgmt_encode_proto(ctxt, rsp, fields,
				    MBS_MESHCORE_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_meshcore_mgmt_config_get(struct smp_streamer *ctxt)
{
	meshbus_MeshcoreConfigGetRequest req = meshbus_MeshcoreConfigGetRequest_init_zero;
	meshbus_MeshcoreConfigGetResponse rsp = meshbus_MeshcoreConfigGetResponse_init_zero;
	mbs_meshcore_config cfg;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_MeshcoreConfigGetRequest_fields, true);

	if (rc != 0) {
		return rc;
	}

	rc = mbs_meshcore_config_get(&cfg);
	if (rc != 0) {
		return rc;
	}

	return mbs_meshcore_mgmt_encode_config_response(
		ctxt, &cfg, meshbus_MeshcoreConfigGetResponse_fields, &rsp);
}

static int mbs_meshcore_mgmt_config_set(struct smp_streamer *ctxt)
{
	meshbus_MeshcoreConfigSetRequest req = meshbus_MeshcoreConfigSetRequest_init_zero;
	meshbus_MeshcoreConfigSetResponse rsp = meshbus_MeshcoreConfigSetResponse_init_zero;
	mbs_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_MeshcoreConfigSetRequest_fields, false);

	if (rc != 0) {
		return rc;
	}
	if (!req.has_config) {
		return -EINVAL;
	}

	cfg = req.config;
	rc = mbs_meshcore_config_set(&cfg);
	if (rc != 0) {
		return rc;
	}

	rc = mbs_meshcore_config_get(&cfg);
	if (rc != 0) {
		return rc;
	}

	return mbs_meshcore_mgmt_encode_config_response(
		ctxt, &cfg, meshbus_MeshcoreConfigSetResponse_fields, &rsp);
}

static int mbs_meshcore_mgmt_config_reset(struct smp_streamer *ctxt)
{
	meshbus_MeshcoreConfigResetRequest req =
		meshbus_MeshcoreConfigResetRequest_init_zero;
	meshbus_MeshcoreConfigResetResponse rsp =
		meshbus_MeshcoreConfigResetResponse_init_zero;
	mbs_meshcore_config cfg;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_MeshcoreConfigResetRequest_fields, true);

	if (rc != 0) {
		return rc;
	}

	rc = mbs_meshcore_config_reset();
	if (rc != 0) {
		return rc;
	}

	rc = mbs_meshcore_config_get(&cfg);
	if (rc != 0) {
		return rc;
	}

	return mbs_meshcore_mgmt_encode_config_response(
		ctxt, &cfg, meshbus_MeshcoreConfigResetResponse_fields, &rsp);
}

static int mbs_meshcore_mgmt_advert(struct smp_streamer *ctxt)
{
	meshbus_MeshcoreAdvertRequest req = meshbus_MeshcoreAdvertRequest_init_zero;
	meshbus_MeshcoreAdvertResponse rsp = meshbus_MeshcoreAdvertResponse_init_zero;
	bool flood = false;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_MeshcoreAdvertRequest_fields, true);

	if (rc != 0) {
		return rc;
	}

	if (req.has_flood) {
		flood = req.flood;
	}

	rc = mbs_meshcore_advert_request(flood);
	if (rc != 0) {
		return rc;
	}

	rsp.accepted = true;
	rsp.flood = flood;
	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_MeshcoreAdvertResponse_fields,
				    MBS_MESHCORE_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_meshcore_mgmt_node_discover(struct smp_streamer *ctxt)
{
	meshbus_MeshcoreNodeDiscoverRequest req =
		meshbus_MeshcoreNodeDiscoverRequest_init_zero;
	meshbus_MeshcoreNodeDiscoverResponse rsp =
		meshbus_MeshcoreNodeDiscoverResponse_init_zero;
	uint8_t filter = MBS_MESHCORE_DISCOVER_FILTER_ALL;
	uint32_t since = 0U;
	uint32_t tag = 0U;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_MeshcoreNodeDiscoverRequest_fields, true);

	if (rc != 0) {
		return rc;
	}

	if (req.has_filter) {
		if (req.filter == 0U ||
		    (req.filter & ~MBS_MESHCORE_DISCOVER_FILTER_ALL) !=
			    0U) {
			return -ERANGE;
		}
		filter = (uint8_t)req.filter;
	}
	if (req.has_since) {
		since = req.since;
	}

	rc = mbs_meshcore_node_discover_request(filter, since, &tag);
	if (rc != 0) {
		return rc;
	}

	rsp.accepted = true;
	rsp.tag = tag;
	return mbs_mgmt_encode_proto(ctxt, &rsp,
				    meshbus_MeshcoreNodeDiscoverResponse_fields,
				    MBS_MESHCORE_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_meshcore_mgmt_trace(struct smp_streamer *ctxt)
{
	meshbus_MeshcoreTraceRequest req = meshbus_MeshcoreTraceRequest_init_zero;
	meshbus_MeshcoreTraceResponse rsp = meshbus_MeshcoreTraceResponse_init_zero;
	uint32_t tag = 0U;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_MeshcoreTraceRequest_fields, false);

	if (rc != 0) {
		return rc;
	}
	if (req.path.size == 0U ||
	    req.path.size > MBS_MESHCORE_PATH_MAX_LEN ||
	    req.path_hash_size == 0U ||
	    req.path_hash_size > MBS_MESHCORE_PATH_HASH_SIZE_MAX) {
		return -EINVAL;
	}

	rc = mbs_meshcore_trace_request(req.path.bytes, (uint8_t)req.path.size,
					    (uint8_t)req.path_hash_size, &tag);
	if (rc != 0) {
		return rc;
	}

	rsp.accepted = true;
	rsp.tag = tag;
	return mbs_mgmt_encode_proto(ctxt, &rsp,
				    meshbus_MeshcoreTraceResponse_fields,
				    MBS_MESHCORE_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_meshcore_mgmt_path_validate(
	uint8_t path_len, const pb_bytes_array_t *path, bool allow_unknown)
{
	size_t path_bytes;
	uint8_t hash_size;

	if (path == NULL) {
		return -EINVAL;
	}
	if (path_len == MBS_MESHCORE_OUT_PATH_UNKNOWN) {
		return allow_unknown && path->size == 0U ? 0 : -EINVAL;
	}

	hash_size = (uint8_t)((path_len >> 6) + 1U);
	if (hash_size > MBS_MESHCORE_PATH_HASH_SIZE_MAX) {
		return -EINVAL;
	}
	path_bytes = (size_t)hash_size * (size_t)(path_len & 0x3fU);
	if (path_bytes > MBS_MESHCORE_PATH_MAX_LEN ||
	    path->size != path_bytes) {
		return -EINVAL;
	}

	return 0;
}

static int mbs_meshcore_mgmt_channel_data(struct smp_streamer *ctxt)
{
	meshbus_MeshcoreChannelDataRequest req =
		meshbus_MeshcoreChannelDataRequest_init_zero;
	meshbus_MeshcoreChannelDataResponse rsp =
		meshbus_MeshcoreChannelDataResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(
		ctxt, &req, sizeof(req),
		meshbus_MeshcoreChannelDataRequest_fields, false);

	if (rc != 0) {
		return rc;
	}
	rc = mbs_meshcore_mgmt_path_validate(
		(uint8_t)req.path_len,
		(const pb_bytes_array_t *)&req.path, true);
	if (rc != 0) {
		return rc;
	}

	rc = mbs_meshcore_channel_data_send((size_t)req.channel_index,
						req.path.bytes,
						(uint8_t)req.path_len,
						(uint16_t)req.data_type,
						req.payload.bytes,
						req.payload.size);
	if (rc != 0) {
		return rc;
	}

	rsp.accepted = true;
	return mbs_mgmt_encode_proto(ctxt, &rsp,
				    meshbus_MeshcoreChannelDataResponse_fields,
				    MBS_MESHCORE_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_meshcore_mgmt_anon_data(struct smp_streamer *ctxt)
{
	meshbus_MeshcoreAnonDataRequest req =
		meshbus_MeshcoreAnonDataRequest_init_zero;
	meshbus_MeshcoreAnonDataResponse rsp =
		meshbus_MeshcoreAnonDataResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(
		ctxt, &req, sizeof(req),
		meshbus_MeshcoreAnonDataRequest_fields, false);

	if (rc != 0) {
		return rc;
	}
	if (req.public_key.size != MBS_MESHCORE_PUBLIC_KEY_SIZE) {
		return -EINVAL;
	}

	rc = mbs_meshcore_anon_data_send(
		req.public_key.bytes, req.payload.bytes, req.payload.size);
	if (rc != 0) {
		return rc;
	}

	rsp.accepted = true;
	return mbs_mgmt_encode_proto(ctxt, &rsp,
				    meshbus_MeshcoreAnonDataResponse_fields,
				    MBS_MESHCORE_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_meshcore_mgmt_raw_data(struct smp_streamer *ctxt)
{
	meshbus_MeshcoreRawDataRequest req =
		meshbus_MeshcoreRawDataRequest_init_zero;
	meshbus_MeshcoreRawDataResponse rsp =
		meshbus_MeshcoreRawDataResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(
		ctxt, &req, sizeof(req),
		meshbus_MeshcoreRawDataRequest_fields, false);

	if (rc != 0) {
		return rc;
	}
	rc = mbs_meshcore_mgmt_path_validate(
		(uint8_t)req.path_len,
		(const pb_bytes_array_t *)&req.path, false);
	if (rc != 0) {
		return rc;
	}

	rc = mbs_meshcore_raw_data_send(
		req.path.bytes, (uint8_t)req.path_len,
		req.payload.bytes, req.payload.size);
	if (rc != 0) {
		return rc;
	}

	rsp.accepted = true;
	return mbs_mgmt_encode_proto(ctxt, &rsp,
				    meshbus_MeshcoreRawDataResponse_fields,
				    MBS_MESHCORE_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_meshcore_mgmt_control_data(struct smp_streamer *ctxt)
{
	meshbus_MeshcoreControlDataRequest req =
		meshbus_MeshcoreControlDataRequest_init_zero;
	meshbus_MeshcoreControlDataResponse rsp =
		meshbus_MeshcoreControlDataResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(
		ctxt, &req, sizeof(req),
		meshbus_MeshcoreControlDataRequest_fields, false);

	if (rc != 0) {
		return rc;
	}

	rc = mbs_meshcore_control_data_send(req.payload.bytes,
						req.payload.size);
	if (rc != 0) {
		return rc;
	}

	rsp.accepted = true;
	return mbs_mgmt_encode_proto(ctxt, &rsp,
				    meshbus_MeshcoreControlDataResponse_fields,
				    MBS_MESHCORE_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_meshcore_mgmt_binary_response(struct smp_streamer *ctxt)
{
	meshbus_MeshcoreBinaryResponseRequest req =
		meshbus_MeshcoreBinaryResponseRequest_init_zero;
	meshbus_MeshcoreBinaryResponseResponse rsp =
		meshbus_MeshcoreBinaryResponseResponse_init_zero;
	struct mbs_meshcore_binary_response_send_request_event request = {
		0
	};
	int rc = mbs_mgmt_decode_proto(
		ctxt, &req, sizeof(req),
		meshbus_MeshcoreBinaryResponseRequest_fields, false);

	if (rc != 0) {
		return rc;
	}
	if (req.public_key.size != sizeof(request.public_key)) {
		return -EINVAL;
	}
	rc = mbs_meshcore_mgmt_path_validate(
		(uint8_t)req.path_len,
		(const pb_bytes_array_t *)&req.path, false);
	if (rc != 0) {
		return rc;
	}

	request.route = (uint8_t)req.route;
	memcpy(request.public_key, req.public_key.bytes,
	       sizeof(request.public_key));
	request.tag = req.tag;
	request.path_len = (uint8_t)req.path_len;
	memcpy(request.path, req.path.bytes, req.path.size);
	request.payload_len = (uint8_t)req.payload.size;
	memcpy(request.payload, req.payload.bytes, req.payload.size);

	rc = mbs_meshcore_binary_response_send(&request);
	if (rc != 0) {
		return rc;
	}

	rsp.accepted = true;
	return mbs_mgmt_encode_proto(
		ctxt, &rsp, meshbus_MeshcoreBinaryResponseResponse_fields,
		MBS_MESHCORE_MGMT_PROTO_RSP_MAX_SIZE);
}

static const struct mgmt_handler mbs_meshcore_mgmt_group_handlers[] = {
	[meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG] =
		{mbs_meshcore_mgmt_config_get, mbs_meshcore_mgmt_config_set},
	[meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG_RESET] =
		{NULL, mbs_meshcore_mgmt_config_reset},
	[meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_ADVERT] =
		{NULL, mbs_meshcore_mgmt_advert},
	[meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_NODE_DISCOVER] =
		{NULL, mbs_meshcore_mgmt_node_discover},
	[meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_TRACE] =
		{NULL, mbs_meshcore_mgmt_trace},
	[meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CHANNEL_DATA] = {
		NULL, mbs_meshcore_mgmt_channel_data
	},
	[meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_ANON_DATA] = {
		NULL, mbs_meshcore_mgmt_anon_data
	},
	[meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_RAW_DATA] = {
		NULL, mbs_meshcore_mgmt_raw_data
	},
	[meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONTROL_DATA] = {
		NULL, mbs_meshcore_mgmt_control_data
	},
	[meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_BINARY_RESPONSE] = {
		NULL, mbs_meshcore_mgmt_binary_response
	},
};

#define MBS_MESHCORE_MGMT_GROUP_SZ ARRAY_SIZE(mbs_meshcore_mgmt_group_handlers)

static struct mgmt_group mbs_meshcore_mgmt_group = {
	.mg_handlers = mbs_meshcore_mgmt_group_handlers,
	.mg_handlers_count = MBS_MESHCORE_MGMT_GROUP_SZ,
	.mg_group_id =
		meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
	.mg_translate_error = mbs_meshcore_mgmt_translate_error_code,
#endif
#ifdef CONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME
	.mg_group_name = "meshbus meshcore mgmt",
#endif
};

static void mbs_meshcore_mgmt_register_group(void)
{
	mgmt_register_group(&mbs_meshcore_mgmt_group);
}

MCUMGR_HANDLER_DEFINE(mbs_meshcore_mgmt, mbs_meshcore_mgmt_register_group);
