/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <channel/channel.h>
#include <zephyr/sys/util.h>

#include "mbs_mgmt_internal.h"
#include "meshbus/channel.pb.h"

LOG_MODULE_REGISTER(mbs_channel_mgmt, CONFIG_MBS_CHANNEL_LOG_LEVEL);

#define MBS_CHANNEL_MGMT_FIND_BY_HASH_RSP_MAX_SIZE                                      \
	(((size_t)meshbus_Channel_size + 8U) * CONFIG_MBS_CHANNEL_MAX_CHANNELS + 16U)

#define MBS_CHANNEL_MGMT_PROTO_RSP_MAX_SIZE MBS_CHANNEL_MGMT_FIND_BY_HASH_RSP_MAX_SIZE

#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
static int mbs_channel_mgmt_translate_error_code(uint16_t err)
{
	return (int)err;
}
#endif

static int mbs_channel_mgmt_copy_channel(const mbs_channel *src, meshbus_Channel *dst)
{
	if (src == NULL || dst == NULL) {
		return -EINVAL;
	}
	if ((src->secret.size != MBS_CHANNEL_SECRET_DEFAULT_LEN) &&
	    (src->secret.size != MBS_CHANNEL_SECRET_SIZE)) {
		return -EINVAL;
	}
	if (src->hash.size != 1U || src->hash.size > sizeof(src->hash.bytes)) {
		return -EINVAL;
	}
	if (strnlen(src->name, sizeof(src->name)) >= sizeof(dst->name)) {
		return -EINVAL;
	}

	*dst = *src;
	dst->name[sizeof(dst->name) - 1U] = '\0';

	return 0;
}

static int mbs_channel_mgmt_get(struct smp_streamer *ctxt)
{
	meshbus_ChannelGetRequest req = meshbus_ChannelGetRequest_init_zero;
	meshbus_ChannelGetResponse rsp = meshbus_ChannelGetResponse_init_zero;
	mbs_channel channel = meshbus_Channel_init_zero;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_ChannelGetRequest_fields,
				      false);

	if (rc != 0) {
		return rc;
	}

	rc = mbs_channel_get((size_t)req.index, &channel);
	if (rc != 0) {
		return rc;
	}

	rc = mbs_channel_mgmt_copy_channel(&channel, &rsp.channel);
	if (rc != 0) {
		return rc;
	}
	rsp.has_channel = true;

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_ChannelGetResponse_fields,
				    MBS_CHANNEL_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_channel_mgmt_set(struct smp_streamer *ctxt)
{
	meshbus_ChannelSetRequest req = meshbus_ChannelSetRequest_init_zero;
	meshbus_ChannelSetResponse rsp = meshbus_ChannelSetResponse_init_zero;
	const char *name = NULL;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_ChannelSetRequest_fields,
				      false);

	if (rc != 0) {
		return rc;
	}

	req.name[sizeof(req.name) - 1U] = '\0';
	if (req.name[0] != '\0') {
		name = req.name;
	}

	rc = mbs_channel_set((size_t)req.index, req.secret.bytes, req.secret.size, name);
	if (rc != 0) {
		return rc;
	}

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_ChannelSetResponse_fields,
				    MBS_CHANNEL_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_channel_mgmt_reset(struct smp_streamer *ctxt)
{
	meshbus_ChannelResetRequest req = meshbus_ChannelResetRequest_init_zero;
	meshbus_ChannelResetResponse rsp = meshbus_ChannelResetResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_ChannelResetRequest_fields, false);

	if (rc != 0) {
		return rc;
	}

	rc = mbs_channel_reset((size_t)req.index);
	if (rc != 0) {
		return rc;
	}

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_ChannelResetResponse_fields,
				    MBS_CHANNEL_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_channel_mgmt_store_count(struct smp_streamer *ctxt)
{
	meshbus_ChannelStoreCountRequest req = meshbus_ChannelStoreCountRequest_init_zero;
	meshbus_ChannelStoreCountResponse rsp = meshbus_ChannelStoreCountResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_ChannelStoreCountRequest_fields, true);

	if (rc != 0) {
		return rc;
	}

	rsp.count = mbs_channel_store_count();

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_ChannelStoreCountResponse_fields,
				    MBS_CHANNEL_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_channel_mgmt_store_size(struct smp_streamer *ctxt)
{
	meshbus_ChannelStoreSizeRequest req = meshbus_ChannelStoreSizeRequest_init_zero;
	meshbus_ChannelStoreSizeResponse rsp = meshbus_ChannelStoreSizeResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_ChannelStoreSizeRequest_fields, true);

	if (rc != 0) {
		return rc;
	}

	rsp.size = mbs_channel_store_size();

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_ChannelStoreSizeResponse_fields,
				    MBS_CHANNEL_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_channel_mgmt_next_free_slot(struct smp_streamer *ctxt)
{
	meshbus_ChannelNextFreeSlotRequest req = meshbus_ChannelNextFreeSlotRequest_init_zero;
	meshbus_ChannelNextFreeSlotResponse rsp =
		meshbus_ChannelNextFreeSlotResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_ChannelNextFreeSlotRequest_fields, true);

	if (rc != 0) {
		return rc;
	}

	rsp.index = mbs_channel_next_free_slot();

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_ChannelNextFreeSlotResponse_fields,
				    MBS_CHANNEL_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_channel_mgmt_next_by_hash(struct smp_streamer *ctxt)
{
	meshbus_ChannelNextByHashRequest req =
		meshbus_ChannelNextByHashRequest_init_zero;
	meshbus_ChannelNextByHashResponse rsp =
		meshbus_ChannelNextByHashResponse_init_zero;
	mbs_channel channel = meshbus_Channel_init_zero;
	size_t slot_id = 0U;
	int rc = mbs_mgmt_decode_proto(
		ctxt, &req, sizeof(req),
		meshbus_ChannelNextByHashRequest_fields, false);

	if (rc != 0) {
		return rc;
	}
	if (req.hash.size != 1U) {
		return -EINVAL;
	}

	rc = mbs_channel_next_by_hash(
		req.hash.bytes, (size_t)req.start_slot, &slot_id, &channel);
	if (rc != 0) {
		return rc;
	}
	rc = mbs_channel_mgmt_copy_channel(&channel, &rsp.channel);
	if (rc != 0) {
		return rc;
	}

	rsp.index = (uint32_t)slot_id;
	rsp.has_channel = true;
	return mbs_mgmt_encode_proto(ctxt, &rsp,
				    meshbus_ChannelNextByHashResponse_fields,
				    MBS_CHANNEL_MGMT_PROTO_RSP_MAX_SIZE);
}

static const struct mgmt_handler mbs_channel_mgmt_group_handlers[] = {
	[meshbus_ChannelMgmtCommandId_CHANNEL_MGMT_COMMAND_ID_GET] =
		{mbs_channel_mgmt_get, NULL},
	[meshbus_ChannelMgmtCommandId_CHANNEL_MGMT_COMMAND_ID_SET] =
		{NULL, mbs_channel_mgmt_set},
	[meshbus_ChannelMgmtCommandId_CHANNEL_MGMT_COMMAND_ID_RESET] =
		{NULL, mbs_channel_mgmt_reset},
	[meshbus_ChannelMgmtCommandId_CHANNEL_MGMT_COMMAND_ID_STORE_COUNT] =
		{mbs_channel_mgmt_store_count, NULL},
	[meshbus_ChannelMgmtCommandId_CHANNEL_MGMT_COMMAND_ID_STORE_SIZE] =
		{mbs_channel_mgmt_store_size, NULL},
	[meshbus_ChannelMgmtCommandId_CHANNEL_MGMT_COMMAND_ID_NEXT_FREE_SLOT] =
		{mbs_channel_mgmt_next_free_slot, NULL},
	[meshbus_ChannelMgmtCommandId_CHANNEL_MGMT_COMMAND_ID_NEXT_BY_HASH] = {
		mbs_channel_mgmt_next_by_hash, NULL
	},
};

#define MBS_CHANNEL_MGMT_GROUP_SZ ARRAY_SIZE(mbs_channel_mgmt_group_handlers)

static struct mgmt_group mbs_channel_mgmt_group = {
	.mg_handlers = mbs_channel_mgmt_group_handlers,
	.mg_handlers_count = MBS_CHANNEL_MGMT_GROUP_SZ,
	.mg_group_id = meshbus_ChannelMgmtGroupId_CHANNEL_MGMT_GROUP_ID_MESHBUS_CHANNEL,
#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
	.mg_translate_error = mbs_channel_mgmt_translate_error_code,
#endif
#ifdef CONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME
	.mg_group_name = "meshbus channel mgmt",
#endif
};

static void mbs_channel_mgmt_register_group(void)
{
	mgmt_register_group(&mbs_channel_mgmt_group);
}

MCUMGR_HANDLER_DEFINE(mbs_channel_mgmt, mbs_channel_mgmt_register_group);
