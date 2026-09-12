/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <pb_decode.h>
#include <pb_encode.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include "mbs_mgmt_internal.h"
#include "mbs_settings_internal.h"

#define MBS_RADIO_SETTINGS_SUBTREE     "meshbus/radio"
#define MBS_GNSS_SETTINGS_SUBTREE      "meshbus/gnss"
#define MBS_TELEMETRY_SETTINGS_SUBTREE "meshbus/telemetry"
#define MBS_POWER_SETTINGS_SUBTREE     "meshbus/power"
#define MBS_INDICATOR_SETTINGS_SUBTREE "meshbus/indicator"
#define MBS_MESHCORE_SETTINGS_SUBTREE  "meshbus/meshcore"
#define MBS_CONTACT_SETTINGS_SUBTREE   "meshbus/contact"
#define MBS_BLUETOOTH_SETTINGS_SUBTREE "meshbus/bluetooth"
#define MBS_CLOCK_SETTINGS_SUBTREE     "meshbus/clock"
#define MBS_CHANNEL_SETTINGS_SUBTREE   "meshbus/channel"
#define MBS_DISPLAY_SETTINGS_SUBTREE   "meshbus/display"
#define MBS_LLEXT_SETTINGS_SUBTREE     "meshbus/llext"
#define MBS_MGMT_REQ_ZCBOR_STATES      3U

#ifdef CONFIG_MCUMGR_GRP_FS_FILE_ACCESS_HOOK
#include <zephyr/mgmt/mcumgr/grp/fs_mgmt/fs_mgmt_callbacks.h>
#endif

#ifdef CONFIG_MCUMGR_GRP_SETTINGS_ACCESS_HOOK
#include <zephyr/mgmt/mcumgr/grp/settings_mgmt/settings_mgmt_callbacks.h>
#endif

LOG_MODULE_REGISTER(mbs_mgmt, CONFIG_MBS_LOG_LEVEL);

static int mbs_mgmt_decode_data_bstr(const uint8_t *payload, size_t payload_len,
				    struct zcbor_string *data)
{
	ZCBOR_STATE_D(zsd, MBS_MGMT_REQ_ZCBOR_STATES, payload, payload_len, 1, 0);

	if (!zcbor_map_start_decode(zsd) || !zcbor_tstr_expect_lit(zsd, MBS_MGMT_DATA_KEY) ||
	    !zcbor_bstr_decode(zsd, data) || !zcbor_map_end_decode(zsd)) {
		return -EINVAL;
	}

	return ((size_t)(zsd->payload - payload) == payload_len) ? 0 : -EINVAL;
}

int mbs_mgmt_decode_proto(struct smp_streamer *ctxt, void *dst, size_t dst_size,
			 const pb_msgdesc_t *fields, bool allow_empty)
{
	struct zcbor_string data = {0};
	pb_istream_t stream;
	int rc;

	memset(dst, 0, dst_size);

	if (ctxt->reader->nb->len == 0U) {
		return allow_empty ? 0 : -EINVAL;
	}

	rc = mbs_mgmt_decode_data_bstr(ctxt->reader->nb->data, ctxt->reader->nb->len, &data);
	if (rc != 0) {
		LOG_DBG("Management CBOR data decode failed: payload_len=%u rc=%d",
			(unsigned int)ctxt->reader->nb->len, rc);
		return rc;
	}

	if (data.len == 0U) {
		return allow_empty ? 0 : -EINVAL;
	}

	stream = pb_istream_from_buffer(data.value, data.len);
	if (!pb_decode(&stream, fields, dst)) {
		LOG_DBG("Management protobuf decode failed: data_len=%u error=%s",
			(unsigned int)data.len, PB_GET_ERROR(&stream));
		return -EINVAL;
	}

	if (stream.bytes_left != 0U) {
		LOG_DBG("Management protobuf has trailing data: data_len=%u left=%u",
			(unsigned int)data.len, (unsigned int)stream.bytes_left);
		return -EINVAL;
	}

	return 0;
}

int mbs_mgmt_encode_proto(struct smp_streamer *ctxt, const void *src, const pb_msgdesc_t *fields,
			 size_t max_size)
{
	zcbor_state_t *zse = ctxt->writer->zs;
	uint8_t *payload;
	pb_ostream_t stream;
	size_t encoded_size;

	if (!pb_get_encoded_size(&encoded_size, fields, src)) {
		return -EINVAL;
	}
	if (encoded_size > max_size) {
		return -EOVERFLOW;
	}

	payload = k_malloc(MAX(encoded_size, 1U));
	if (payload == NULL) {
		return -ENOMEM;
	}

	stream = pb_ostream_from_buffer(payload, encoded_size);

	if (!pb_encode(&stream, fields, src)) {
		k_free(payload);
		return -EINVAL;
	}

	if (!zcbor_tstr_put_lit(zse, MBS_MGMT_DATA_KEY) ||
	    !zcbor_bstr_encode_ptr(zse, (const char *)payload, stream.bytes_written)) {
		k_free(payload);
		return -ENOMEM;
	}

	k_free(payload);
	return 0;
}

#ifdef CONFIG_MCUMGR_GRP_FS_FILE_ACCESS_HOOK
/**
 * @brief File system audit callback for MCUmgr
 *
 * This callback is invoked for every file access request via MCUmgr.
 * Meshbus currently treats this as an audit hook: transport authentication,
 * mounted filesystem permissions, and sample policy decide which paths exist
 * and are writable. The hook only rejects unknown access types so that future
 * Zephyr enum additions are not silently treated as allowed operations.
 *
 * @param event The event type (MGMT_EVT_OP_FS_MGMT_FILE_ACCESS)
 * @param prev_status Previous handler's return status
 * @param rc Return code pointer (set on error)
 * @param group Group ID pointer (set on error)
 * @param abort_more Whether to abort further handlers
 * @param data Pointer to fs_mgmt_file_access structure
 * @param data_size Size of the data structure
 * @return MGMT_CB_OK to allow, MGMT_CB_ERROR_RC to deny
 */
bool mbs_mgmt_fs_access_type_audited(enum fs_mgmt_file_access_types access)
{
	switch (access) {
	case FS_MGMT_FILE_ACCESS_READ:
	case FS_MGMT_FILE_ACCESS_WRITE:
	case FS_MGMT_FILE_ACCESS_STATUS:
	case FS_MGMT_FILE_ACCESS_HASH_CHECKSUM:
		return true;
	default:
		return false;
	}
}

static enum mgmt_cb_return fs_access_callback(uint32_t event, enum mgmt_cb_return prev_status,
					      int32_t *rc, uint16_t *group, bool *abort_more,
					      void *data, size_t data_size)
{
	if (event == MGMT_EVT_OP_FS_MGMT_FILE_ACCESS) {
		struct fs_mgmt_file_access *fs_data = (struct fs_mgmt_file_access *)data;

		if (!mbs_mgmt_fs_access_type_audited(fs_data->access)) {
			LOG_WRN("File system access denied: unsupported access %d", fs_data->access);
			*rc = MGMT_ERR_EACCESSDENIED;
			return MGMT_CB_ERROR_RC;
		}

		LOG_DBG("File system access: op=%d path=%s", fs_data->access,
			fs_data->filename != NULL ? fs_data->filename : "(null)");
	}

	return MGMT_CB_OK;
}

static struct mgmt_callback fs_access_cb = {
	.callback = fs_access_callback,
	.event_id = MGMT_EVT_OP_FS_MGMT_FILE_ACCESS,
};
#endif /* CONFIG_MCUMGR_GRP_FS_FILE_ACCESS_HOOK */

#ifdef CONFIG_MCUMGR_GRP_SETTINGS_ACCESS_HOOK
struct mbs_mgmt_settings_subtree {
	const char *service;
	const char *subtree;
};

static const struct mbs_mgmt_settings_subtree mbs_settings_subtrees[] = {
	{"bluetooth", MBS_BLUETOOTH_SETTINGS_SUBTREE},
	{"channel", MBS_CHANNEL_SETTINGS_SUBTREE},
	{"clock", MBS_CLOCK_SETTINGS_SUBTREE},
	{"display", MBS_DISPLAY_SETTINGS_SUBTREE},
	{"gnss", MBS_GNSS_SETTINGS_SUBTREE},
	{"indicator", MBS_INDICATOR_SETTINGS_SUBTREE},
	{"llext", MBS_LLEXT_SETTINGS_SUBTREE},
	{"meshcore", MBS_MESHCORE_SETTINGS_SUBTREE},
	{"contact", MBS_CONTACT_SETTINGS_SUBTREE},
	{"power", MBS_POWER_SETTINGS_SUBTREE},
	{"radio", MBS_RADIO_SETTINGS_SUBTREE},
	{"telemetry", MBS_TELEMETRY_SETTINGS_SUBTREE},
};

static bool settings_name_in_subtree(const char *name, const char *subtree)
{
	size_t len;

	if (name == NULL || subtree == NULL) {
		return false;
	}

	len = strlen(subtree);
	return (strcmp(name, subtree) == 0) ||
	       ((strncmp(name, subtree, len) == 0) && (name[len] == '/'));
}

bool mbs_mgmt_settings_name_allowed(const char *name)
{
	for (size_t i = 0; i < ARRAY_SIZE(mbs_settings_subtrees); i++) {
		if (settings_name_in_subtree(name, mbs_settings_subtrees[i].subtree)) {
			return true;
		}
	}

	return false;
}

bool mbs_mgmt_settings_access_allowed(enum settings_mgmt_access_types access, const char *name)
{
	switch (access) {
	case SETTINGS_ACCESS_READ:
	case SETTINGS_ACCESS_WRITE:
	case SETTINGS_ACCESS_DELETE:
	case SETTINGS_ACCESS_SAVE:
		return mbs_mgmt_settings_name_allowed(name);
	case SETTINGS_ACCESS_COMMIT:
	case SETTINGS_ACCESS_LOAD:
		return name == NULL;
	default:
		return false;
	}
}

/**
 * @brief Settings access callback for MCUmgr
 *
 * This callback is invoked for every settings access request via MCUmgr.
 * It can be used to:
 * - Allow/deny access to specific settings keys
 * - Redirect settings access to different keys
 * - Log settings access attempts
 *
 * @param event The event type (MGMT_EVT_OP_SETTINGS_MGMT_ACCESS)
 * @param prev_status Previous handler's return status
 * @param rc Return code pointer (set on error)
 * @param group Group ID pointer (set on error)
 * @param abort_more Whether to abort further handlers
 * @param data Pointer to settings_mgmt_access structure
 * @param data_size Size of the data structure
 * @return MGMT_CB_OK to allow, MGMT_CB_ERROR_RC to deny
 */
static enum mgmt_cb_return settings_access_callback(uint32_t event, enum mgmt_cb_return prev_status,
						    int32_t *rc, uint16_t *group, bool *abort_more,
						    void *data, size_t data_size)
{
	if (event == MGMT_EVT_OP_SETTINGS_MGMT_ACCESS) {
		struct settings_mgmt_access *settings_data = (struct settings_mgmt_access *)data;

		if (!mbs_mgmt_settings_access_allowed(settings_data->access, settings_data->name)) {
			LOG_WRN("Settings access denied: op=%d name=%s", settings_data->access,
				settings_data->name != NULL ? settings_data->name : "(none)");
			*rc = MGMT_ERR_EACCESSDENIED;
			return MGMT_CB_ERROR_RC;
		}
	}

	return MGMT_CB_OK;
}

static struct mgmt_callback settings_access_cb = {
	.callback = settings_access_callback,
	.event_id = MGMT_EVT_OP_SETTINGS_MGMT_ACCESS,
};
#endif /* CONFIG_MCUMGR_GRP_SETTINGS_ACCESS_HOOK */

/**
 * @brief Initialize MCUmgr access hooks
 *
 * Registers callbacks for file system and settings access control.
 * Called automatically during system initialization.
 */
static int mbs_mgmt_init(void)
{
	int rc = mbs_settings_init_status();
	if (rc == 0) {
#ifdef CONFIG_MCUMGR_GRP_SETTINGS_ACCESS_HOOK
		mgmt_callback_register(&settings_access_cb);
		LOG_DBG("Settings access hook registered");
#endif
	} else {
		LOG_ERR("Settings subsystem initialization failed: %d", rc);
		return rc;
	}

#ifdef CONFIG_MCUMGR_GRP_FS_FILE_ACCESS_HOOK
	mgmt_callback_register(&fs_access_cb);
	LOG_DBG("FS access hook registered");
#endif

	return 0;
}

SYS_INIT(mbs_mgmt_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
