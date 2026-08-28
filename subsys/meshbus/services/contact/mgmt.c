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

#include <zephyr/meshbus/contact.h>

#include "common/mgmt.h"
#include "meshbus/contact.pb.h"

LOG_MODULE_REGISTER(meshbus_contact_mgmt, CONFIG_MESHBUS_CONTACT_LOG_LEVEL);

#define CONTACT_PREFIX_BYTES CONFIG_MESHBUS_CONTACT_PREFIX_BYTES
#define MESHBUS_CONTACT_MGMT_PROTO_RSP_MAX_SIZE MESHBUS_MESHBUS_CONTACT_PB_H_MAX_SIZE

#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
static int meshbus_contact_mgmt_translate_error_code(uint16_t err)
{
	return (int)err;
}
#endif

static int meshbus_contact_mgmt_decode_prefix(
	const pb_bytes_array_t *prefix, uint8_t public_key_prefix[CONTACT_PREFIX_BYTES])
{
	if (prefix == NULL || prefix->size != CONTACT_PREFIX_BYTES) {
		return -EINVAL;
	}

	memcpy(public_key_prefix, prefix->bytes, CONTACT_PREFIX_BYTES);
	return 0;
}

static int meshbus_contact_mgmt_decode_public_key(
	const pb_bytes_array_t *public_key, uint8_t out[MESHBUS_CONTACT_PUBLIC_KEY_SIZE])
{
	if (public_key == NULL || public_key->size != MESHBUS_CONTACT_PUBLIC_KEY_SIZE) {
		return -EINVAL;
	}

	memcpy(out, public_key->bytes, MESHBUS_CONTACT_PUBLIC_KEY_SIZE);
	return 0;
}

static int meshbus_contact_mgmt_find_index_by_prefix(
	const uint8_t public_key_prefix[CONTACT_PREFIX_BYTES], size_t *index_out)
{
	if (index_out == NULL) {
		return -EINVAL;
	}

	for (size_t index = 0U; index < meshbus_contact_store_size(); index++) {
		meshbus_contact contact = meshbus_Contact_init_zero;
		int rc = meshbus_contact_get(index, &contact);

		if (rc == -ENOENT) {
			continue;
		}
		if (rc != 0) {
			return rc;
		}
		if (memcmp(contact.public_key.bytes, public_key_prefix,
			   CONTACT_PREFIX_BYTES) != 0) {
			continue;
		}

		*index_out = index;
		return 0;
	}

	return -ENOENT;
}

static int meshbus_contact_mgmt_find_index_by_key(
	const uint8_t public_key[MESHBUS_CONTACT_PUBLIC_KEY_SIZE], size_t *index_out)
{
	if (index_out == NULL) {
		return -EINVAL;
	}

	for (size_t index = 0U; index < meshbus_contact_store_size(); index++) {
		meshbus_contact contact = meshbus_Contact_init_zero;
		int rc = meshbus_contact_get(index, &contact);

		if (rc == -ENOENT) {
			continue;
		}
		if (rc != 0) {
			return rc;
		}
		if (memcmp(contact.public_key.bytes, public_key,
			   MESHBUS_CONTACT_PUBLIC_KEY_SIZE) != 0) {
			continue;
		}

		*index_out = index;
		return 0;
	}

	return -ENOENT;
}

static void meshbus_contact_mgmt_redact_secret(meshbus_contact *contact)
{
	if (contact == NULL) {
		return;
	}

	memset(&contact->management_secret, 0,
	       sizeof(contact->management_secret));
}

static void meshbus_contact_mgmt_secure_free(void *ptr, size_t len)
{
	volatile uint8_t *bytes = ptr;

	while (bytes != NULL && len > 0U) {
		*bytes++ = 0U;
		len--;
	}
	k_free(ptr);
}

struct meshbus_contact_mgmt_add_context {
	meshbus_ContactAddRequest req;
	meshbus_ContactAddResponse rsp;
	uint8_t public_key[MESHBUS_CONTACT_PUBLIC_KEY_SIZE];
};

struct meshbus_contact_mgmt_set_context {
	meshbus_ContactSetRequest req;
	meshbus_ContactSetResponse rsp;
	uint8_t public_key_prefix[CONTACT_PREFIX_BYTES];
};

static int meshbus_contact_mgmt_find_by_prefix(struct smp_streamer *ctxt)
{
	meshbus_ContactFindByPrefixRequest req =
		meshbus_ContactFindByPrefixRequest_init_zero;
	meshbus_ContactFindByPrefixResponse *rsp;
	uint8_t public_key_prefix[CONTACT_PREFIX_BYTES];
	size_t index = 0U;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_ContactFindByPrefixRequest_fields, false);

	if (rc != 0) {
		return rc;
	}
	rsp = k_calloc(1U, sizeof(*rsp));
	if (rsp == NULL) {
		return -ENOMEM;
	}

	rc = meshbus_contact_mgmt_decode_prefix(
		(const pb_bytes_array_t *)&req.public_key_prefix, public_key_prefix);
	if (rc != 0) {
		goto out;
	}

	rc = meshbus_contact_find_by_prefix(public_key_prefix, &rsp->contact);
	if (rc != 0) {
		goto out;
	}

	rc = meshbus_contact_mgmt_find_index_by_prefix(public_key_prefix, &index);
	if (rc != 0) {
		goto out;
	}

	rsp->index = (uint32_t)index;
	rsp->has_contact = true;
	meshbus_contact_mgmt_redact_secret(&rsp->contact);

	rc = mb_mgmt_encode_proto(ctxt, rsp,
				  meshbus_ContactFindByPrefixResponse_fields,
				  MESHBUS_CONTACT_MGMT_PROTO_RSP_MAX_SIZE);
out:
	meshbus_contact_mgmt_secure_free(rsp, sizeof(*rsp));
	return rc;
}

static int meshbus_contact_mgmt_find_by_key(struct smp_streamer *ctxt)
{
	meshbus_ContactFindByKeyRequest req = meshbus_ContactFindByKeyRequest_init_zero;
	meshbus_ContactFindByKeyResponse *rsp;
	uint8_t public_key[MESHBUS_CONTACT_PUBLIC_KEY_SIZE];
	size_t index = 0U;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_ContactFindByKeyRequest_fields, false);

	if (rc != 0) {
		return rc;
	}
	rsp = k_calloc(1U, sizeof(*rsp));
	if (rsp == NULL) {
		return -ENOMEM;
	}

	rc = meshbus_contact_mgmt_decode_public_key((const pb_bytes_array_t *)&req.public_key,
						    public_key);
	if (rc != 0) {
		goto out;
	}

	rc = meshbus_contact_find_by_key(public_key, &rsp->contact);
	if (rc != 0) {
		goto out;
	}

	rc = meshbus_contact_mgmt_find_index_by_key(public_key, &index);
	if (rc != 0) {
		goto out;
	}

	rsp->index = (uint32_t)index;
	rsp->has_contact = true;
	meshbus_contact_mgmt_redact_secret(&rsp->contact);

	rc = mb_mgmt_encode_proto(ctxt, rsp,
				  meshbus_ContactFindByKeyResponse_fields,
				  MESHBUS_CONTACT_MGMT_PROTO_RSP_MAX_SIZE);
out:
	meshbus_contact_mgmt_secure_free(rsp, sizeof(*rsp));
	return rc;
}

static int meshbus_contact_mgmt_get(struct smp_streamer *ctxt)
{
	meshbus_ContactGetRequest req = meshbus_ContactGetRequest_init_zero;
	meshbus_ContactGetResponse *rsp;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_ContactGetRequest_fields, true);

	if (rc != 0) {
		return rc;
	}
	rsp = k_calloc(1U, sizeof(*rsp));
	if (rsp == NULL) {
		return -ENOMEM;
	}

	rc = meshbus_contact_get((size_t)req.index, &rsp->contact);
	if (rc != 0) {
		goto out;
	}

	rsp->has_contact = true;
	meshbus_contact_mgmt_redact_secret(&rsp->contact);

	rc = mb_mgmt_encode_proto(ctxt, rsp, meshbus_ContactGetResponse_fields,
				  MESHBUS_CONTACT_MGMT_PROTO_RSP_MAX_SIZE);
out:
	meshbus_contact_mgmt_secure_free(rsp, sizeof(*rsp));
	return rc;
}

static int meshbus_contact_mgmt_store_count(struct smp_streamer *ctxt)
{
	meshbus_ContactStoreCountRequest req = meshbus_ContactStoreCountRequest_init_zero;
	meshbus_ContactStoreCountResponse rsp = meshbus_ContactStoreCountResponse_init_zero;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_ContactStoreCountRequest_fields, true);

	if (rc != 0) {
		return rc;
	}

	rsp.count = meshbus_contact_store_count();

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_ContactStoreCountResponse_fields,
				    MESHBUS_CONTACT_MGMT_PROTO_RSP_MAX_SIZE);
}

static int meshbus_contact_mgmt_store_size(struct smp_streamer *ctxt)
{
	meshbus_ContactStoreSizeRequest req = meshbus_ContactStoreSizeRequest_init_zero;
	meshbus_ContactStoreSizeResponse rsp = meshbus_ContactStoreSizeResponse_init_zero;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_ContactStoreSizeRequest_fields, true);

	if (rc != 0) {
		return rc;
	}

	rsp.size = meshbus_contact_store_size();

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_ContactStoreSizeResponse_fields,
				    MESHBUS_CONTACT_MGMT_PROTO_RSP_MAX_SIZE);
}

static int meshbus_contact_mgmt_next_by_hash(struct smp_streamer *ctxt)
{
	meshbus_ContactNextByHashRequest req =
		meshbus_ContactNextByHashRequest_init_zero;
	meshbus_ContactNextByHashResponse *rsp;
	size_t slot_id = 0U;
	int rc = mb_mgmt_decode_proto(
		ctxt, &req, sizeof(req),
		meshbus_ContactNextByHashRequest_fields, false);

	if (rc != 0) {
		return rc;
	}
	if (req.hash.size != 1U) {
		return -EINVAL;
	}
	rsp = k_calloc(1U, sizeof(*rsp));
	if (rsp == NULL) {
		return -ENOMEM;
	}

	rc = meshbus_contact_next_by_hash(
		req.hash.bytes, (size_t)req.start_slot, &slot_id, &rsp->contact);
	if (rc != 0) {
		goto out;
	}

	rsp->index = (uint32_t)slot_id;
	rsp->has_contact = true;
	meshbus_contact_mgmt_redact_secret(&rsp->contact);
	rc = mb_mgmt_encode_proto(ctxt, rsp,
				  meshbus_ContactNextByHashResponse_fields,
				  MESHBUS_CONTACT_MGMT_PROTO_RSP_MAX_SIZE);
out:
	meshbus_contact_mgmt_secure_free(rsp, sizeof(*rsp));
	return rc;
}

static int meshbus_contact_mgmt_add(struct smp_streamer *ctxt)
{
	struct meshbus_contact_mgmt_add_context *context =
		k_calloc(1U, sizeof(*context));
	int rc = -ENOMEM;

	if (context == NULL) {
		return rc;
	}

	rc = mb_mgmt_decode_proto(ctxt, &context->req, sizeof(context->req),
				      meshbus_ContactAddRequest_fields, false);

	if (rc != 0) {
		goto out;
	}
	rc = meshbus_contact_mgmt_decode_public_key(
		(const pb_bytes_array_t *)&context->req.public_key,
		context->public_key);
	if (rc != 0) {
		goto out;
	}

	rc = meshbus_contact_insert(context->public_key, context->req.name,
				    context->req.role);
	if (rc != 0) {
		goto out;
	}
	rc = meshbus_contact_find_by_key(context->public_key,
					 &context->rsp.contact);
	if (rc != 0) {
		goto out;
	}

	context->rsp.has_contact = true;
	meshbus_contact_mgmt_redact_secret(&context->rsp.contact);
	rc = mb_mgmt_encode_proto(
		ctxt, &context->rsp, meshbus_ContactAddResponse_fields,
		MESHBUS_CONTACT_MGMT_PROTO_RSP_MAX_SIZE);

out:
	meshbus_contact_mgmt_secure_free(context, sizeof(*context));
	return rc;
}

static int meshbus_contact_mgmt_set(struct smp_streamer *ctxt)
{
	struct meshbus_contact_mgmt_set_context *context =
		k_calloc(1U, sizeof(*context));
	int rc = -ENOMEM;

	if (context == NULL) {
		return rc;
	}

	rc = mb_mgmt_decode_proto(ctxt, &context->req, sizeof(context->req),
				      meshbus_ContactSetRequest_fields, false);

	if (rc != 0) {
		goto out;
	}
	rc = meshbus_contact_mgmt_decode_prefix(
		(const pb_bytes_array_t *)&context->req.public_key_prefix,
		context->public_key_prefix);
	if (rc != 0) {
		goto out;
	}
	if (!context->req.has_contact) {
		rc = -EINVAL;
		goto out;
	}
	if (context->req.clear_management_secret &&
	    context->req.contact.management_secret.size != 0U) {
		rc = -EINVAL;
		goto out;
	}
	if (context->req.clear_management_secret) {
		memset(&context->req.contact.management_secret, 0,
		       sizeof(context->req.contact.management_secret));
	} else if (context->req.contact.management_secret.size == 0U) {
		rc = meshbus_contact_find_by_prefix(context->public_key_prefix,
						    &context->rsp.contact);
		if (rc == 0) {
			context->req.contact.management_secret =
				context->rsp.contact.management_secret;
			memset(&context->rsp, 0, sizeof(context->rsp));
		} else if (rc != -ENOENT) {
			goto out;
		}
	}

	rc = meshbus_contact_set(context->public_key_prefix,
				 &context->req.contact);
	if (rc != 0) {
		goto out;
	}

	rc = meshbus_contact_find_by_prefix(context->public_key_prefix,
					    &context->rsp.contact);
	if (rc != 0) {
		goto out;
	}
	context->rsp.has_contact = true;
	meshbus_contact_mgmt_redact_secret(&context->rsp.contact);

	rc = mb_mgmt_encode_proto(ctxt, &context->rsp,
				  meshbus_ContactSetResponse_fields,
				  MESHBUS_CONTACT_MGMT_PROTO_RSP_MAX_SIZE);

out:
	meshbus_contact_mgmt_secure_free(context, sizeof(*context));
	return rc;
}

static int meshbus_contact_mgmt_reset(struct smp_streamer *ctxt)
{
	meshbus_ContactResetRequest req = meshbus_ContactResetRequest_init_zero;
	meshbus_ContactResetResponse rsp = meshbus_ContactResetResponse_init_zero;
	uint8_t public_key_prefix[CONTACT_PREFIX_BYTES];
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_ContactResetRequest_fields, false);

	if (rc != 0) {
		return rc;
	}

	rc = meshbus_contact_mgmt_decode_prefix(
		(const pb_bytes_array_t *)&req.public_key_prefix, public_key_prefix);
	if (rc != 0) {
		return rc;
	}

	rc = meshbus_contact_reset(public_key_prefix);
	if (rc != 0) {
		return rc;
	}

	rsp.reset = true;
	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_ContactResetResponse_fields,
				    MESHBUS_CONTACT_MGMT_PROTO_RSP_MAX_SIZE);
}

static int meshbus_contact_mgmt_share(struct smp_streamer *ctxt)
{
	meshbus_ContactShareRequest req = meshbus_ContactShareRequest_init_zero;
	meshbus_ContactShareResponse rsp = meshbus_ContactShareResponse_init_zero;
	uint8_t public_key_prefix[CONTACT_PREFIX_BYTES];
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_ContactShareRequest_fields, false);

	if (rc != 0) {
		return rc;
	}

	rc = meshbus_contact_mgmt_decode_prefix(
		(const pb_bytes_array_t *)&req.public_key_prefix, public_key_prefix);
	if (rc != 0) {
		return rc;
	}

	rc = meshbus_contact_share_request(public_key_prefix);
	if (rc != 0) {
		return rc;
	}

	rsp.accepted = true;
	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_ContactShareResponse_fields,
				    MESHBUS_CONTACT_MGMT_PROTO_RSP_MAX_SIZE);
}

static int meshbus_contact_mgmt_discover_path(struct smp_streamer *ctxt)
{
	meshbus_ContactDiscoverPathRequest req =
		meshbus_ContactDiscoverPathRequest_init_zero;
	meshbus_ContactDiscoverPathResponse rsp =
		meshbus_ContactDiscoverPathResponse_init_zero;
	uint8_t public_key_prefix[CONTACT_PREFIX_BYTES];
	uint32_t tag = 0U;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_ContactDiscoverPathRequest_fields, false);

	if (rc != 0) {
		return rc;
	}

	rc = meshbus_contact_mgmt_decode_prefix(
		(const pb_bytes_array_t *)&req.public_key_prefix, public_key_prefix);
	if (rc != 0) {
		return rc;
	}

	rc = meshbus_contact_discover_path_request(public_key_prefix, &tag);
	if (rc != 0) {
		return rc;
	}

	rsp.accepted = true;
	rsp.tag = tag;
	return mb_mgmt_encode_proto(ctxt, &rsp,
				    meshbus_ContactDiscoverPathResponse_fields,
				    MESHBUS_CONTACT_MGMT_PROTO_RSP_MAX_SIZE);
}

static int meshbus_contact_mgmt_trace_path(struct smp_streamer *ctxt)
{
	meshbus_ContactTracePathRequest req = meshbus_ContactTracePathRequest_init_zero;
	meshbus_ContactTracePathResponse rsp = meshbus_ContactTracePathResponse_init_zero;
	uint8_t public_key_prefix[CONTACT_PREFIX_BYTES];
	uint32_t tag = 0U;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_ContactTracePathRequest_fields, false);

	if (rc != 0) {
		return rc;
	}

	rc = meshbus_contact_mgmt_decode_prefix(
		(const pb_bytes_array_t *)&req.public_key_prefix, public_key_prefix);
	if (rc != 0) {
		return rc;
	}

	rc = meshbus_contact_trace_path_request(public_key_prefix, &tag);
	if (rc != 0) {
		return rc;
	}

	rsp.accepted = true;
	rsp.tag = tag;
	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_ContactTracePathResponse_fields,
				    MESHBUS_CONTACT_MGMT_PROTO_RSP_MAX_SIZE);
}

static int meshbus_contact_mgmt_telemetry(struct smp_streamer *ctxt)
{
	meshbus_ContactTelemetryRequest req = meshbus_ContactTelemetryRequest_init_zero;
	meshbus_ContactTelemetryResponse rsp = meshbus_ContactTelemetryResponse_init_zero;
	uint8_t public_key_prefix[CONTACT_PREFIX_BYTES];
	uint32_t tag = 0U;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_ContactTelemetryRequest_fields, false);

	if (rc != 0) {
		return rc;
	}

	rc = meshbus_contact_mgmt_decode_prefix(
		(const pb_bytes_array_t *)&req.public_key_prefix, public_key_prefix);
	if (rc != 0) {
		return rc;
	}

	rc = meshbus_contact_telemetry_request(public_key_prefix, &tag);
	if (rc != 0) {
		return rc;
	}

	rsp.accepted = true;
	rsp.tag = tag;
	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_ContactTelemetryResponse_fields,
				    MESHBUS_CONTACT_MGMT_PROTO_RSP_MAX_SIZE);
}

static int meshbus_contact_mgmt_binary(struct smp_streamer *ctxt)
{
	meshbus_ContactBinaryRequest req =
		meshbus_ContactBinaryRequest_init_zero;
	meshbus_ContactBinaryResponse rsp =
		meshbus_ContactBinaryResponse_init_zero;
	uint8_t public_key_prefix[CONTACT_PREFIX_BYTES];
	uint32_t tag = 0U;
	int rc = mb_mgmt_decode_proto(
		ctxt, &req, sizeof(req),
		meshbus_ContactBinaryRequest_fields, false);

	if (rc != 0) {
		return rc;
	}

	rc = meshbus_contact_mgmt_decode_prefix(
		(const pb_bytes_array_t *)&req.public_key_prefix,
		public_key_prefix);
	if (rc != 0) {
		return rc;
	}

	rc = meshbus_contact_binary_request(
		public_key_prefix, req.payload.bytes, req.payload.size, &tag);
	if (rc != 0) {
		return rc;
	}

	rsp.accepted = true;
	rsp.tag = tag;
	return mb_mgmt_encode_proto(
		ctxt, &rsp, meshbus_ContactBinaryResponse_fields,
		MESHBUS_CONTACT_MGMT_PROTO_RSP_MAX_SIZE);
}

static const struct mgmt_handler meshbus_contact_mgmt_group_handlers[] = {
	[meshbus_ContactMgmtCommandId_CONTACT_MGMT_COMMAND_ID_GET] =
		{meshbus_contact_mgmt_get, NULL},
	[meshbus_ContactMgmtCommandId_CONTACT_MGMT_COMMAND_ID_FIND_BY_PREFIX] =
		{meshbus_contact_mgmt_find_by_prefix, NULL},
	[meshbus_ContactMgmtCommandId_CONTACT_MGMT_COMMAND_ID_FIND_BY_KEY] =
		{meshbus_contact_mgmt_find_by_key, NULL},
	[meshbus_ContactMgmtCommandId_CONTACT_MGMT_COMMAND_ID_STORE_COUNT] =
		{meshbus_contact_mgmt_store_count, NULL},
	[meshbus_ContactMgmtCommandId_CONTACT_MGMT_COMMAND_ID_STORE_SIZE] =
		{meshbus_contact_mgmt_store_size, NULL},
	[meshbus_ContactMgmtCommandId_CONTACT_MGMT_COMMAND_ID_NEXT_BY_HASH] = {
		meshbus_contact_mgmt_next_by_hash, NULL
	},
	[meshbus_ContactMgmtCommandId_CONTACT_MGMT_COMMAND_ID_ADD] = {
		NULL, meshbus_contact_mgmt_add
	},
	[meshbus_ContactMgmtCommandId_CONTACT_MGMT_COMMAND_ID_SET] =
		{NULL, meshbus_contact_mgmt_set},
	[meshbus_ContactMgmtCommandId_CONTACT_MGMT_COMMAND_ID_RESET] =
		{NULL, meshbus_contact_mgmt_reset},
	[meshbus_ContactMgmtCommandId_CONTACT_MGMT_COMMAND_ID_SHARE] =
		{NULL, meshbus_contact_mgmt_share},
	[meshbus_ContactMgmtCommandId_CONTACT_MGMT_COMMAND_ID_DISCOVER_PATH] =
		{NULL, meshbus_contact_mgmt_discover_path},
	[meshbus_ContactMgmtCommandId_CONTACT_MGMT_COMMAND_ID_TRACE_PATH] =
		{NULL, meshbus_contact_mgmt_trace_path},
	[meshbus_ContactMgmtCommandId_CONTACT_MGMT_COMMAND_ID_TELEMETRY] =
		{NULL, meshbus_contact_mgmt_telemetry},
	[meshbus_ContactMgmtCommandId_CONTACT_MGMT_COMMAND_ID_BINARY] = {
		NULL, meshbus_contact_mgmt_binary
	},
};

#define MESHBUS_CONTACT_MGMT_GROUP_SZ ARRAY_SIZE(meshbus_contact_mgmt_group_handlers)

static struct mgmt_group meshbus_contact_mgmt_group = {
	.mg_handlers = meshbus_contact_mgmt_group_handlers,
	.mg_handlers_count = MESHBUS_CONTACT_MGMT_GROUP_SZ,
	.mg_group_id =
		meshbus_ContactMgmtGroupId_CONTACT_MGMT_GROUP_ID_MESHBUS_CONTACT,
#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
	.mg_translate_error = meshbus_contact_mgmt_translate_error_code,
#endif
#ifdef CONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME
	.mg_group_name = "meshbus contact mgmt",
#endif
};

static void meshbus_contact_mgmt_register_group(void)
{
	mgmt_register_group(&meshbus_contact_mgmt_group);
}

MCUMGR_HANDLER_DEFINE(meshbus_contact_mgmt, meshbus_contact_mgmt_register_group);
