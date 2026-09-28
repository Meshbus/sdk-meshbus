/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt_defines.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <management/management.h>

#include "mbs_mgmt_internal.h"
#include "management_priv.h"
#include "meshbus/management.pb.h"

LOG_MODULE_REGISTER(mbs_management_mgmt, CONFIG_MBS_MANAGEMENT_LOG_LEVEL);

#define MBS_MANAGEMENT_MGMT_PROTO_RSP_MAX_SIZE \
	MESHBUS_MESHBUS_MANAGEMENT_PB_H_MAX_SIZE
#define MBS_MANAGEMENT_MGMT_INNER_PAYLOAD_MAX_SIZE \
	(MBS_MANAGEMENT_SMP_PACKET_MAX_LEN - MGMT_HDR_SIZE)

static K_MUTEX_DEFINE(mbs_management_mgmt_result_mutex);
static K_MUTEX_DEFINE(mbs_management_mgmt_upload_mutex);

struct mbs_management_mgmt_result_entry {
	bool valid;
	uint32_t tag;
	int32_t status;
	uint16_t response_len;
	uint8_t *response;
};

struct mbs_management_mgmt_exchange_context {
	meshbus_ManagementSmpExchangeRequest request;
	mbs_management_smp_request_event smp_request;
	uint8_t secret[MBS_MANAGEMENT_SECRET_MAX_LEN];
	size_t secret_len;
};

struct mbs_management_mgmt_upload {
	bool active;
	uint32_t transfer_id;
	int64_t last_active_ms;
	uint8_t contact_prefix[MBS_MANAGEMENT_CONTACT_PREFIX_BYTES];
	uint16_t group;
	uint8_t command;
	uint8_t op;
	uint16_t total_length;
	uint16_t received_length;
	uint8_t secret[MBS_MANAGEMENT_SECRET_MAX_LEN];
	uint8_t secret_len;
	uint8_t *payload;
};

static struct mbs_management_mgmt_result_entry
	mbs_management_mgmt_results[
		CONFIG_MBS_MANAGEMENT_MGMT_RESULT_CACHE_SIZE];
static size_t mbs_management_mgmt_result_next;
static struct mbs_management_mgmt_upload mbs_management_mgmt_upload;
static uint32_t mbs_management_mgmt_transfer_id;

static void mbs_management_mgmt_result_clear_locked(
	struct mbs_management_mgmt_result_entry *entry)
{
	if (entry == NULL) {
		return;
	}
	if (entry->response != NULL) {
		management_secure_wipe(entry->response, entry->response_len);
		k_free(entry->response);
	}
	management_secure_wipe(entry, sizeof(*entry));
}

#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
static int mbs_management_mgmt_translate_error_code(uint16_t err)
{
	return (int)err;
}
#endif

static void mbs_management_mgmt_result_listener_cb(
	const struct zbus_channel *chan)
{
	const mbs_management_smp_response_event *event =
		zbus_chan_const_msg(chan);
	struct mbs_management_mgmt_result_entry *entry = NULL;
	uint8_t *response = NULL;

	if (event == NULL) {
		return;
	}
	if (event->response_len > 0U) {
		response = k_malloc(event->response_len);
		if (response == NULL) {
			LOG_WRN("Management result cache allocation failed: len=%u",
				(unsigned int)event->response_len);
			return;
		}
		memcpy(response, event->response, event->response_len);
	}

	k_mutex_lock(&mbs_management_mgmt_result_mutex, K_FOREVER);
	for (size_t i = 0U;
	     i < ARRAY_SIZE(mbs_management_mgmt_results); i++) {
		if (mbs_management_mgmt_results[i].valid &&
		    mbs_management_mgmt_results[i].tag == event->tag) {
			entry = &mbs_management_mgmt_results[i];
			break;
		}
	}
	if (entry == NULL) {
		for (size_t i = 0U;
		     i < ARRAY_SIZE(mbs_management_mgmt_results); i++) {
			if (!mbs_management_mgmt_results[i].valid) {
				entry = &mbs_management_mgmt_results[i];
				break;
			}
		}
	}
	if (entry == NULL) {
		entry = &mbs_management_mgmt_results[
			mbs_management_mgmt_result_next];
		mbs_management_mgmt_result_next =
			(mbs_management_mgmt_result_next + 1U) %
			ARRAY_SIZE(mbs_management_mgmt_results);
	}

	mbs_management_mgmt_result_clear_locked(entry);
	entry->tag = event->tag;
	entry->status = event->status;
	entry->response_len = event->response_len;
	entry->response = response;
	entry->valid = true;
	k_mutex_unlock(&mbs_management_mgmt_result_mutex);
}

ZBUS_LISTENER_DEFINE(mbs_management_mgmt_result_listener,
		     mbs_management_mgmt_result_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_management_smp_response_chan,
		  mbs_management_mgmt_result_listener, 0);

static int mbs_management_mgmt_parse_smp_op(uint32_t value, uint8_t *op)
{
	if (op == NULL) {
		return -EINVAL;
	}

	switch (value) {
	case MGMT_OP_READ:
		*op = MGMT_OP_READ;
		return 0;
	case MGMT_OP_WRITE:
		*op = MGMT_OP_WRITE;
		return 0;
	default:
		return -EINVAL;
	}
}

static int mbs_management_mgmt_smp_packet_build(uint16_t group, uint8_t command,
						    uint8_t op, const uint8_t *payload,
						    size_t payload_len,
						    uint8_t *out, size_t out_size,
						    size_t *out_len)
{
	if (out == NULL || out_len == NULL || (payload == NULL && payload_len > 0U) ||
	    payload_len > UINT16_MAX || MGMT_HDR_SIZE + payload_len > out_size) {
		return -EINVAL;
	}

	out[0] = op;
	out[1] = 0U;
	sys_put_be16((uint16_t)payload_len, &out[2]);
	sys_put_be16(group, &out[4]);
	out[6] = 0U;
	out[7] = command;
	if (payload_len > 0U) {
		memcpy(&out[MGMT_HDR_SIZE], payload, payload_len);
	}
	*out_len = MGMT_HDR_SIZE + payload_len;
	return 0;
}

static void mbs_management_mgmt_upload_clear_locked(void)
{
	if (mbs_management_mgmt_upload.payload != NULL) {
		management_secure_wipe(mbs_management_mgmt_upload.payload,
				       mbs_management_mgmt_upload.total_length);
		k_free(mbs_management_mgmt_upload.payload);
	}
	management_secure_wipe(&mbs_management_mgmt_upload,
			       sizeof(mbs_management_mgmt_upload));
}

static uint32_t mbs_management_mgmt_transfer_id_next_locked(void)
{
	do {
		mbs_management_mgmt_transfer_id++;
	} while (mbs_management_mgmt_transfer_id == 0U);

	return mbs_management_mgmt_transfer_id;
}

static int mbs_management_mgmt_upload_accept(
	const meshbus_ManagementSmpExchangeRequest *request, uint8_t op,
	struct mbs_management_mgmt_exchange_context *exchange,
	meshbus_ManagementSmpExchangeResponse *rsp, bool *ready)
{
	struct mbs_management_mgmt_upload *upload =
		&mbs_management_mgmt_upload;
	size_t end;
	size_t packet_len = 0U;
	int rc;

	if (request == NULL || exchange == NULL || rsp == NULL || ready == NULL) {
		return -EINVAL;
	}
	*ready = false;

	if (request->total_length == 0U && request->offset == 0U &&
	    request->transfer_id == 0U) {
		if (request->contact_prefix.size !=
			    MBS_MANAGEMENT_CONTACT_PREFIX_BYTES ||
		    request->group > UINT16_MAX ||
		    request->command > UINT8_MAX ||
		    request->payload.size >
			    MBS_MANAGEMENT_MGMT_INNER_PAYLOAD_MAX_SIZE) {
			return -EINVAL;
		}

		memcpy(exchange->smp_request.contact_prefix,
		       request->contact_prefix.bytes,
		       sizeof(exchange->smp_request.contact_prefix));
		if (request->secret.size > 0U) {
			exchange->secret_len = request->secret.size;
			memcpy(exchange->secret, request->secret.bytes,
			       exchange->secret_len);
		}
		rc = mbs_management_mgmt_smp_packet_build(
			(uint16_t)request->group, (uint8_t)request->command, op,
			request->payload.size > 0U ? request->payload.bytes : NULL,
			request->payload.size, exchange->smp_request.packet,
			sizeof(exchange->smp_request.packet), &packet_len);
		if (rc == 0) {
			exchange->smp_request.packet_len = (uint16_t)packet_len;
			*ready = true;
		}
		return rc;
	}

	if (request->total_length == 0U ||
	    request->total_length >
		    MBS_MANAGEMENT_MGMT_INNER_PAYLOAD_MAX_SIZE ||
	    request->offset > request->total_length ||
	    request->payload.size == 0U) {
		return -EINVAL;
	}
	end = (size_t)request->offset + request->payload.size;
	if (end > request->total_length) {
		return -EINVAL;
	}

	k_mutex_lock(&mbs_management_mgmt_upload_mutex, K_FOREVER);
	if (upload->active &&
	    k_uptime_get() - upload->last_active_ms > MANAGEMENT_SMP_TIMEOUT_MS) {
		mbs_management_mgmt_upload_clear_locked();
	}

	if (request->transfer_id == 0U) {
		if (request->offset != 0U ||
		    request->contact_prefix.size !=
			    MBS_MANAGEMENT_CONTACT_PREFIX_BYTES ||
		    request->group > UINT16_MAX ||
		    request->command > UINT8_MAX) {
			k_mutex_unlock(&mbs_management_mgmt_upload_mutex);
			return -EINVAL;
		}

		mbs_management_mgmt_upload_clear_locked();
		upload->active = true;
		upload->transfer_id =
			mbs_management_mgmt_transfer_id_next_locked();
		memcpy(upload->contact_prefix, request->contact_prefix.bytes,
		       sizeof(upload->contact_prefix));
		upload->group = (uint16_t)request->group;
		upload->command = (uint8_t)request->command;
		upload->op = op;
		upload->total_length = (uint16_t)request->total_length;
		upload->payload = k_calloc(1U, upload->total_length);
		if (upload->payload == NULL) {
			mbs_management_mgmt_upload_clear_locked();
			k_mutex_unlock(&mbs_management_mgmt_upload_mutex);
			return -ENOMEM;
		}
		if (request->secret.size > 0U) {
			upload->secret_len = request->secret.size;
			memcpy(upload->secret, request->secret.bytes,
			       upload->secret_len);
		}
	} else if (!upload->active ||
		   upload->payload == NULL ||
		   upload->transfer_id != request->transfer_id ||
		   upload->received_length != request->offset ||
		   upload->total_length != request->total_length ||
		   request->secret.size != 0U) {
		k_mutex_unlock(&mbs_management_mgmt_upload_mutex);
		return -ESTALE;
	}

	memcpy(&upload->payload[upload->received_length], request->payload.bytes,
	       request->payload.size);
	upload->received_length = (uint16_t)end;
	upload->last_active_ms = k_uptime_get();
	rsp->transfer_id = upload->transfer_id;
	rsp->next_offset = upload->received_length;
	if (upload->received_length < upload->total_length) {
		k_mutex_unlock(&mbs_management_mgmt_upload_mutex);
		return 0;
	}

	memcpy(exchange->smp_request.contact_prefix, upload->contact_prefix,
	       sizeof(exchange->smp_request.contact_prefix));
	exchange->secret_len = upload->secret_len;
	memcpy(exchange->secret, upload->secret, exchange->secret_len);
	rc = mbs_management_mgmt_smp_packet_build(
		    upload->group, upload->command, upload->op, upload->payload,
		    upload->total_length, exchange->smp_request.packet,
		    sizeof(exchange->smp_request.packet), &packet_len);
	if (rc != 0 || packet_len > UINT16_MAX) {
		mbs_management_mgmt_upload_clear_locked();
		k_mutex_unlock(&mbs_management_mgmt_upload_mutex);
		return -EINVAL;
	}
	exchange->smp_request.packet_len = (uint16_t)packet_len;
	mbs_management_mgmt_upload_clear_locked();
	k_mutex_unlock(&mbs_management_mgmt_upload_mutex);
	*ready = true;
	return 0;
}

static int mbs_management_mgmt_secret_get(struct smp_streamer *ctxt)
{
	meshbus_ManagementSecretGetRequest req =
		meshbus_ManagementSecretGetRequest_init_zero;
	meshbus_ManagementSecretGetResponse rsp =
		meshbus_ManagementSecretGetResponse_init_zero;
	mbs_management_config cfg = meshbus_ManagementConfig_init_zero;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_ManagementSecretGetRequest_fields, true);

	if (rc != 0) {
		return rc;
	}

	rc = mbs_management_config_get(&cfg);
	if (rc != 0) {
		return rc;
	}
	rsp.configured = cfg.secret.size > 0U;
	rsp.effective_max_len =
		(uint32_t)mbs_management_smp_effective_max_len_get();

	rc = mbs_mgmt_encode_proto(ctxt, &rsp,
				  meshbus_ManagementSecretGetResponse_fields,
				  MBS_MANAGEMENT_MGMT_PROTO_RSP_MAX_SIZE);
	management_secure_wipe(&cfg, sizeof(cfg));
	management_secure_wipe(&rsp, sizeof(rsp));
	return rc;
}

static int mbs_management_mgmt_secret_set(struct smp_streamer *ctxt)
{
	meshbus_ManagementSecretSetRequest req =
		meshbus_ManagementSecretSetRequest_init_zero;
	meshbus_ManagementSecretSetResponse rsp =
		meshbus_ManagementSecretSetResponse_init_zero;
	mbs_management_config cfg = meshbus_ManagementConfig_init_zero;
	int rc;

	rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				  meshbus_ManagementSecretSetRequest_fields,
				  false);
	if (rc != 0) {
		goto out;
	}
	rc = mbs_management_config_get(&cfg);
	if (rc != 0) {
		goto out;
	}
	cfg.secret.size = req.secret.size;
	memcpy(cfg.secret.bytes, req.secret.bytes, req.secret.size);
	rc = mbs_management_config_set(&cfg);
	if (rc != 0) {
		goto out;
	}

	rsp.accepted = true;
	rc = mbs_mgmt_encode_proto(ctxt, &rsp,
				  meshbus_ManagementSecretSetResponse_fields,
				  MBS_MANAGEMENT_MGMT_PROTO_RSP_MAX_SIZE);

out:
	management_secure_wipe(&req, sizeof(req));
	management_secure_wipe(&cfg, sizeof(cfg));
	return rc;
}

static int mbs_management_mgmt_smp_exchange(struct smp_streamer *ctxt)
{
	meshbus_ManagementSmpExchangeResponse rsp =
		meshbus_ManagementSmpExchangeResponse_init_zero;
	struct mbs_management_mgmt_exchange_context *exchange =
		k_calloc(1, sizeof(*exchange));
	uint8_t op;
	uint32_t tag = 0U;
	bool ready = false;
	int rc = -ENOMEM;

	if (exchange == NULL) {
		return rc;
	}

	rc = mbs_mgmt_decode_proto(ctxt, &exchange->request,
				  sizeof(exchange->request),
				  meshbus_ManagementSmpExchangeRequest_fields,
				  false);
	if (rc != 0) {
		goto out;
	}
	rc = mbs_management_mgmt_parse_smp_op(exchange->request.op, &op);
	if (rc != 0) {
		goto out;
	}

	rc = mbs_management_mgmt_upload_accept(
		&exchange->request, op, exchange, &rsp, &ready);
	if (rc != 0) {
		goto out;
	}
	if (!ready) {
		rc = mbs_mgmt_encode_proto(
			ctxt, &rsp,
			meshbus_ManagementSmpExchangeResponse_fields,
			MBS_MANAGEMENT_MGMT_PROTO_RSP_MAX_SIZE);
		goto out;
	}

	if (exchange->secret_len > 0U) {
		rc = mbs_management_smp_request_with_secret(
			&exchange->smp_request, exchange->secret,
			exchange->secret_len, &tag);
	} else {
		rc = mbs_management_smp_request(
			&exchange->smp_request, &tag);
	}
	if (rc != 0) {
		goto out;
	}

	rsp.accepted = true;
	rsp.tag = tag;
	rc = mbs_mgmt_encode_proto(ctxt, &rsp,
				  meshbus_ManagementSmpExchangeResponse_fields,
				  MBS_MANAGEMENT_MGMT_PROTO_RSP_MAX_SIZE);

out:
	management_secure_wipe(exchange, sizeof(*exchange));
	k_free(exchange);
	return rc;
}

static int mbs_management_mgmt_smp_result(struct smp_streamer *ctxt)
{
	meshbus_ManagementSmpResultRequest req =
		meshbus_ManagementSmpResultRequest_init_zero;
	meshbus_ManagementSmpResultResponse rsp =
		meshbus_ManagementSmpResultResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(
		ctxt, &req, sizeof(req),
		meshbus_ManagementSmpResultRequest_fields, false);

	if (rc != 0) {
		return rc;
	}
	if (req.tag == 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&mbs_management_mgmt_result_mutex, K_FOREVER);
	for (size_t i = 0U;
	     i < ARRAY_SIZE(mbs_management_mgmt_results); i++) {
		struct mbs_management_mgmt_result_entry *entry =
			&mbs_management_mgmt_results[i];

		if (!entry->valid || entry->tag != req.tag) {
			continue;
		}

		rsp.ready = true;
		rsp.status = entry->status;
		rsp.total_length = entry->response_len;
		if (req.offset > entry->response_len) {
			k_mutex_unlock(
				&mbs_management_mgmt_result_mutex);
			return -EINVAL;
		}
		rsp.response.size = MIN(
			(size_t)entry->response_len - req.offset,
			sizeof(rsp.response.bytes));
		if (rsp.response.size > 0U) {
			memcpy(rsp.response.bytes,
			       &entry->response[req.offset],
			       rsp.response.size);
		}
		if (req.consume &&
		    (entry->status != 0 ||
		     req.offset + rsp.response.size >= entry->response_len)) {
			mbs_management_mgmt_result_clear_locked(entry);
		}
		break;
	}
	k_mutex_unlock(&mbs_management_mgmt_result_mutex);

	return mbs_mgmt_encode_proto(ctxt, &rsp,
				    meshbus_ManagementSmpResultResponse_fields,
				    MBS_MANAGEMENT_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_management_mgmt_config_reset(struct smp_streamer *ctxt)
{
	meshbus_ManagementConfigResetRequest req =
		meshbus_ManagementConfigResetRequest_init_zero;
	meshbus_ManagementConfigResetResponse rsp =
		meshbus_ManagementConfigResetResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(
		ctxt, &req, sizeof(req),
		meshbus_ManagementConfigResetRequest_fields, true);

	if (rc != 0) {
		return rc;
	}
	rc = mbs_management_config_reset();
	if (rc != 0) {
		return rc;
	}

	rsp.accepted = true;
	return mbs_mgmt_encode_proto(
		ctxt, &rsp, meshbus_ManagementConfigResetResponse_fields,
		MBS_MANAGEMENT_MGMT_PROTO_RSP_MAX_SIZE);
}

static const struct mgmt_handler mbs_management_group_handlers[] = {
	[meshbus_ManagementCommandId_MANAGEMENT_COMMAND_ID_SMP_EXCHANGE] =
		{NULL, mbs_management_mgmt_smp_exchange},
	[meshbus_ManagementCommandId_MANAGEMENT_COMMAND_ID_SECRET] =
		{mbs_management_mgmt_secret_get,
		 mbs_management_mgmt_secret_set},
	[meshbus_ManagementCommandId_MANAGEMENT_COMMAND_ID_SMP_RESULT] = {
		mbs_management_mgmt_smp_result, NULL
	},
	[meshbus_ManagementCommandId_MANAGEMENT_COMMAND_ID_CONFIG_RESET] = {
		NULL, mbs_management_mgmt_config_reset
	},
};

#define MBS_MANAGEMENT_GROUP_SZ ARRAY_SIZE(mbs_management_group_handlers)

static struct mgmt_group mbs_management_group = {
	.mg_handlers = mbs_management_group_handlers,
	.mg_handlers_count = MBS_MANAGEMENT_GROUP_SZ,
	.mg_group_id =
		meshbus_ManagementGroupId_MANAGEMENT_GROUP_ID_MESHBUS_MANAGEMENT,
#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
	.mg_translate_error = mbs_management_mgmt_translate_error_code,
#endif
#ifdef CONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME
	.mg_group_name = "meshbus management",
#endif
};

static void mbs_management_mgmt_register_group(void)
{
	mgmt_register_group(&mbs_management_group);
}

MCUMGR_HANDLER_DEFINE(mbs_management_mgmt,
		      mbs_management_mgmt_register_group);
