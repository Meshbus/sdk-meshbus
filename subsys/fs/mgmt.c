/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <fs/fs.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#include "mbs_mgmt_internal.h"
#include "meshbus/fs.pb.h"

LOG_MODULE_REGISTER(mbs_fs_mgmt, CONFIG_MBS_FS_LOG_LEVEL);

#define MBS_FS_MGMT_PROTO_RSP_MAX_SIZE MESHBUS_MESHBUS_FS_PB_H_MAX_SIZE
#define MBS_FS_MGMT_LIST_DEFAULT_LIMIT 8U
#define MBS_FS_MGMT_LIST_MAX_LIMIT 8U
#define MBS_FS_MGMT_FORMAT_REBOOT_DELAY K_MSEC(750)

#if defined(CONFIG_MBS_FS_REBOOT_AFTER_FORMAT)
static void mbs_fs_format_reboot_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	LOG_INF("Rebooting after Meshbus FS format");
	sys_reboot(SYS_REBOOT_COLD);
}

static K_WORK_DELAYABLE_DEFINE(mbs_fs_format_reboot_work,
			       mbs_fs_format_reboot_work_handler);

static void mbs_fs_schedule_format_reboot(void)
{
	(void)k_work_schedule(&mbs_fs_format_reboot_work,
			      MBS_FS_MGMT_FORMAT_REBOOT_DELAY);
}
#else
static void mbs_fs_schedule_format_reboot(void)
{
}
#endif

#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
static int mbs_fs_mgmt_translate_error_code(uint16_t err)
{
	return (int)err;
}
#endif

static const char *request_volume_or_default(const char *volume_id)
{
	return (volume_id != NULL && volume_id[0] != '\0') ? volume_id : "extra";
}

static void volume_status_to_proto(const struct mbs_fs_volume_status *status,
				   meshbus_FsVolumeStatus *proto)
{
	if (status == NULL || proto == NULL) {
		return;
	}

	memset(proto, 0, sizeof(*proto));
	(void)snprintk(proto->id, sizeof(proto->id), "%s", status->id);
	(void)snprintk(proto->mount_point, sizeof(proto->mount_point), "%s",
		       status->mount_point);
	proto->mounted = status->mounted;
	proto->has_capacity = status->has_capacity;
	proto->total_bytes = status->total_bytes;
	proto->free_bytes = status->free_bytes;
}

static meshbus_FsEntryType entry_type_to_proto(enum mbs_fs_entry_type type)
{
	switch (type) {
	case MBS_FS_ENTRY_FILE:
		return meshbus_FsEntryType_FS_ENTRY_TYPE_FILE;
	case MBS_FS_ENTRY_DIR:
		return meshbus_FsEntryType_FS_ENTRY_TYPE_DIR;
	default:
		return meshbus_FsEntryType_FS_ENTRY_TYPE_UNSPECIFIED;
	}
}

static void entry_to_proto(const struct mbs_fs_entry *entry, meshbus_FsEntry *proto)
{
	if (entry == NULL || proto == NULL) {
		return;
	}

	memset(proto, 0, sizeof(*proto));
	(void)snprintk(proto->path, sizeof(proto->path), "%s", entry->path);
	(void)snprintk(proto->name, sizeof(proto->name), "%s", entry->name);
	proto->type = entry_type_to_proto(entry->type);
	proto->size = entry->size;
}

static int mbs_fs_mgmt_status(struct smp_streamer *ctxt)
{
	meshbus_FsStatusRequest req = meshbus_FsStatusRequest_init_zero;
	meshbus_FsStatusResponse rsp = meshbus_FsStatusResponse_init_zero;
	struct mbs_fs_volume_status status;
	const char *volume_id;
	int rc;

	rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_FsStatusRequest_fields,
				  true);
	if (rc != 0) {
		return rc;
	}

	volume_id = request_volume_or_default(req.has_volume_id ? req.volume_id : NULL);
	rc = mbs_fs_volume_status(volume_id, &status);
	if (rc != 0) {
		return rc;
	}

	rsp.has_status = true;
	volume_status_to_proto(&status, &rsp.status);
	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_FsStatusResponse_fields,
				    MBS_FS_MGMT_PROTO_RSP_MAX_SIZE);
}

struct mbs_fs_mgmt_list_ctx {
	meshbus_FsListResponse *rsp;
};

static int mbs_fs_mgmt_list_cb(const struct mbs_fs_entry *entry, void *user_data)
{
	struct mbs_fs_mgmt_list_ctx *ctx = user_data;

	if (ctx == NULL || ctx->rsp == NULL || entry == NULL) {
		return -EINVAL;
	}
	if (ctx->rsp->entries_count >= ARRAY_SIZE(ctx->rsp->entries)) {
		return -ENOMEM;
	}

	entry_to_proto(entry, &ctx->rsp->entries[ctx->rsp->entries_count++]);
	return 0;
}

static int mbs_fs_mgmt_list(struct smp_streamer *ctxt)
{
	meshbus_FsListRequest req = meshbus_FsListRequest_init_zero;
	/* The bounded entry array exceeds the MCUmgr transport workqueue stack. */
	meshbus_FsListResponse *rsp = k_calloc(1, sizeof(*rsp));
	struct mbs_fs_mgmt_list_ctx list_ctx = {
		.rsp = rsp,
	};
	size_t total_count = 0U;
	size_t next_offset = 0U;
	size_t offset;
	size_t limit;
	int rc = -ENOMEM;

	if (rsp == NULL) {
		return rc;
	}

	rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_FsListRequest_fields,
				  false);
	if (rc != 0) {
		goto out;
	}

	offset = req.has_offset ? req.offset : 0U;
	limit = req.has_limit ? req.limit : MBS_FS_MGMT_LIST_DEFAULT_LIMIT;
	if (limit > MBS_FS_MGMT_LIST_MAX_LIMIT) {
		limit = MBS_FS_MGMT_LIST_MAX_LIMIT;
	}

	rc = mbs_fs_path_normalize(req.path, rsp->path, sizeof(rsp->path));
	if (rc != 0) {
		goto out;
	}

	rc = mbs_fs_list(rsp->path, offset, limit, mbs_fs_mgmt_list_cb, &list_ctx,
			     &total_count, &next_offset);
	if (rc != 0) {
		goto out;
	}
	if (total_count > UINT16_MAX || next_offset > UINT16_MAX) {
		rc = -EOVERFLOW;
		goto out;
	}

	rsp->total_count = (uint32_t)total_count;
	rsp->next_offset = (uint32_t)next_offset;
	rc = mbs_mgmt_encode_proto(ctxt, rsp, meshbus_FsListResponse_fields,
				  MBS_FS_MGMT_PROTO_RSP_MAX_SIZE);

out:
	k_free(rsp);
	return rc;
}

static int mbs_fs_mgmt_mkdir(struct smp_streamer *ctxt)
{
	meshbus_FsMkdirRequest req = meshbus_FsMkdirRequest_init_zero;
	meshbus_FsMkdirResponse rsp = meshbus_FsMkdirResponse_init_zero;
	int rc;

	rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_FsMkdirRequest_fields,
				  false);
	if (rc != 0) {
		return rc;
	}

	rc = mbs_fs_mkdir(req.path);
	if (rc != 0) {
		return rc;
	}

	rsp.accepted = true;
	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_FsMkdirResponse_fields,
				    MBS_FS_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_fs_mgmt_stat(struct smp_streamer *ctxt)
{
	meshbus_FsStatRequest req = meshbus_FsStatRequest_init_zero;
	meshbus_FsStatResponse rsp = meshbus_FsStatResponse_init_zero;
	struct mbs_fs_entry entry;
	int rc;

	rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_FsStatRequest_fields,
				  false);
	if (rc != 0) {
		return rc;
	}

	rc = mbs_fs_stat(req.path, &entry);
	if (rc != 0) {
		return rc;
	}

	rsp.has_entry = true;
	entry_to_proto(&entry, &rsp.entry);
	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_FsStatResponse_fields,
				    MBS_FS_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_fs_mgmt_delete(struct smp_streamer *ctxt)
{
	meshbus_FsDeleteRequest req = meshbus_FsDeleteRequest_init_zero;
	meshbus_FsDeleteResponse rsp = meshbus_FsDeleteResponse_init_zero;
	int rc;

	rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_FsDeleteRequest_fields,
				  false);
	if (rc != 0) {
		return rc;
	}

	rc = mbs_fs_delete(req.path);
	if (rc != 0) {
		return rc;
	}

	rsp.accepted = true;
	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_FsDeleteResponse_fields,
				    MBS_FS_MGMT_PROTO_RSP_MAX_SIZE);
}

static int mbs_fs_mgmt_format(struct smp_streamer *ctxt)
{
	meshbus_FsFormatRequest req = meshbus_FsFormatRequest_init_zero;
	meshbus_FsFormatResponse rsp = meshbus_FsFormatResponse_init_zero;
	struct mbs_fs_volume_status status;
	const char *volume_id;
	int rc;

	rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_FsFormatRequest_fields,
				  false);
	if (rc != 0) {
		return rc;
	}

	volume_id = request_volume_or_default(req.volume_id);
	rc = mbs_fs_format(volume_id, req.confirm);
	if (rc != 0) {
		return rc;
	}
	rc = mbs_fs_volume_status(volume_id, &status);
	if (rc != 0) {
		return rc;
	}

	rsp.has_status = true;
	volume_status_to_proto(&status, &rsp.status);
	rc = mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_FsFormatResponse_fields,
				  MBS_FS_MGMT_PROTO_RSP_MAX_SIZE);
	if (rc == 0) {
		mbs_fs_schedule_format_reboot();
	}

	return rc;
}

static const struct mgmt_handler mbs_fs_mgmt_group_handlers[] = {
	[meshbus_FsMgmtCommandId_FS_MGMT_COMMAND_ID_STATUS] =
		{mbs_fs_mgmt_status, NULL},
	[meshbus_FsMgmtCommandId_FS_MGMT_COMMAND_ID_LIST] =
		{mbs_fs_mgmt_list, NULL},
	[meshbus_FsMgmtCommandId_FS_MGMT_COMMAND_ID_MKDIR] =
		{NULL, mbs_fs_mgmt_mkdir},
	[meshbus_FsMgmtCommandId_FS_MGMT_COMMAND_ID_STAT] =
		{mbs_fs_mgmt_stat, NULL},
	[meshbus_FsMgmtCommandId_FS_MGMT_COMMAND_ID_DELETE] =
		{NULL, mbs_fs_mgmt_delete},
	[meshbus_FsMgmtCommandId_FS_MGMT_COMMAND_ID_FORMAT] =
		{NULL, mbs_fs_mgmt_format},
};

#define MBS_FS_MGMT_GROUP_SZ ARRAY_SIZE(mbs_fs_mgmt_group_handlers)

static struct mgmt_group mbs_fs_mgmt_group = {
	.mg_handlers = mbs_fs_mgmt_group_handlers,
	.mg_handlers_count = MBS_FS_MGMT_GROUP_SZ,
	.mg_group_id = meshbus_FsMgmtGroupId_FS_MGMT_GROUP_ID_MESHBUS_FS,
#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
	.mg_translate_error = mbs_fs_mgmt_translate_error_code,
#endif
#ifdef CONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME
	.mg_group_name = "meshbus fs mgmt",
#endif
};

static void mbs_fs_mgmt_register_group(void)
{
	mgmt_register_group(&mbs_fs_mgmt_group);
}

MCUMGR_HANDLER_DEFINE(mbs_fs_mgmt, mbs_fs_mgmt_register_group);
