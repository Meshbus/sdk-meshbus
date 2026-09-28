/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/sys/util.h>

#include "mbs_mgmt_internal.h"
#include "meshbus/bluetooth.pb.h"

#include <bluetooth/bluetooth.h>

LOG_MODULE_REGISTER(mbs_bluetooth_mgmt, CONFIG_MBS_BLUETOOTH_LOG_LEVEL);

#define MBS_BLUETOOTH_MGMT_PROTO_RSP_MAX_SIZE \
	MAX(MAX(meshbus_BluetoothStatusResponse_size, meshbus_BluetoothConfigGetResponse_size), \
	    MAX(meshbus_BluetoothConfigSetResponse_size,                                      \
		MAX(meshbus_BluetoothConfigResetResponse_size,                               \
		    meshbus_BluetoothEnableResponse_size)))

#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
static int mbs_bluetooth_mgmt_translate_error_code(uint16_t err)
{
	return (int)err;
}
#endif

struct conn_status {
	bool connected;
	char peer_address_str[BT_ADDR_LE_STR_LEN];
	uint8_t security;
};

static void foreach_conn_status(struct bt_conn *conn, void *data)
{
	struct conn_status *st = (struct conn_status *)data;
	struct bt_conn_info info;

	if (st == NULL || st->connected) {
		return;
	}

	if (bt_conn_get_info(conn, &info) != 0) {
		return;
	}

	if (info.type != BT_CONN_TYPE_LE || info.state != BT_CONN_STATE_CONNECTED) {
		return;
	}

	const bt_addr_le_t *dst = bt_conn_get_dst(conn);

	if (dst != NULL) {
		bt_addr_le_to_str(dst, st->peer_address_str, sizeof(st->peer_address_str));
	}

	st->security = (uint8_t)bt_conn_get_security(conn);
	st->connected = true;
}

struct bond_count_ctx {
	uint32_t count;
};

static void bond_count_cb(const struct bt_bond_info *info, void *user_data)
{
	ARG_UNUSED(info);
	struct bond_count_ctx *ctx = (struct bond_count_ctx *)user_data;

	if (ctx != NULL) {
		ctx->count++;
	}
}

static int mbs_bluetooth_mgmt_status(struct smp_streamer *ctxt)
{
	meshbus_BluetoothStatusRequest req = meshbus_BluetoothStatusRequest_init_zero;
	mbs_bluetooth_config cfg;
	struct conn_status st = { 0 };
	struct bond_count_ctx bonds = { 0 };
	meshbus_BluetoothStatusResponse rsp = meshbus_BluetoothStatusResponse_init_zero;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_BluetoothStatusRequest_fields,
				      true);

	if (rc != 0) {
		return rc;
	}

	rc = mbs_bluetooth_config_get(&cfg);
	if (rc != 0) {
		return rc;
	}

	if (bt_is_ready()) {
		bt_conn_foreach(BT_CONN_TYPE_LE, foreach_conn_status, &st);
		bt_foreach_bond(BT_ID_DEFAULT, bond_count_cb, &bonds);
	}

	rsp.has_config = true;
	rsp.config = cfg;
	rsp.connected = st.connected;
	rsp.bond_count = bonds.count;
	rsp.state = (meshbus_BluetoothState)(!cfg.enabled ?
						     MBS_BLUETOOTH_STATE_DISABLED :
						     (!st.connected ?
							      MBS_BLUETOOTH_STATE_DISCONNECTED :
							      (st.security >= BT_SECURITY_L4 ?
								       MBS_BLUETOOTH_STATE_CONNECTED :
								       MBS_BLUETOOTH_STATE_PAIRING)));

	if (st.connected) {
		rsp.has_security = true;
		rsp.security = st.security;
		if (st.peer_address_str[0] != '\0') {
			rsp.has_peer_address_str = true;
			strcpy(rsp.peer_address_str, st.peer_address_str);
		}
	}

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_BluetoothStatusResponse_fields,
				    MBS_BLUETOOTH_MGMT_PROTO_RSP_MAX_SIZE);
}

MBS_MGMT_CONFIG_GET_HANDLER_DEFINE(
	mbs_bluetooth_mgmt_config_get, meshbus_BluetoothConfigGetRequest,
	meshbus_BluetoothConfigGetResponse, mbs_bluetooth_config_get,
	meshbus_BluetoothConfigGetRequest_fields, meshbus_BluetoothConfigGetResponse_fields,
	MBS_BLUETOOTH_MGMT_PROTO_RSP_MAX_SIZE);

MBS_MGMT_CONFIG_SET_HANDLER_DEFINE(
	mbs_bluetooth_mgmt_config_set, meshbus_BluetoothConfigSetRequest,
	meshbus_BluetoothConfigSetResponse, mbs_bluetooth_config_set,
	meshbus_BluetoothConfigSetRequest_fields, meshbus_BluetoothConfigSetResponse_fields,
	MBS_BLUETOOTH_MGMT_PROTO_RSP_MAX_SIZE);

static int mbs_bluetooth_mgmt_enable(struct smp_streamer *ctxt)
{
	meshbus_BluetoothEnableRequest req = meshbus_BluetoothEnableRequest_init_zero;
	meshbus_BluetoothEnableResponse rsp = meshbus_BluetoothEnableResponse_init_zero;
	mbs_bluetooth_config cfg;
	int rc = mbs_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_BluetoothEnableRequest_fields, true);

	if (rc != 0) {
		return rc;
	}

	rc = mbs_bluetooth_config_get(&cfg);
	if (rc != 0) {
		return rc;
	}

	cfg.enabled = req.enabled;

	rc = mbs_bluetooth_config_set(&cfg);
	if (rc != 0) {
		return rc;
	}

	rsp.has_config = true;
	rsp.config = cfg;

	return mbs_mgmt_encode_proto(ctxt, &rsp, meshbus_BluetoothEnableResponse_fields,
				    MBS_BLUETOOTH_MGMT_PROTO_RSP_MAX_SIZE);
}

MBS_MGMT_CONFIG_RESET_HANDLER_DEFINE(
	mbs_bluetooth_mgmt_config_reset, meshbus_BluetoothConfigResetRequest,
	meshbus_BluetoothConfigResetResponse, mbs_bluetooth_config_reset,
	mbs_bluetooth_config_get, meshbus_BluetoothConfigResetRequest_fields,
	meshbus_BluetoothConfigResetResponse_fields, MBS_BLUETOOTH_MGMT_PROTO_RSP_MAX_SIZE);

static const struct mgmt_handler mbs_bluetooth_mgmt_group_handlers[] = {
	[meshbus_BluetoothMgmtCommandId_BLUETOOTH_MGMT_COMMAND_ID_STATUS] =
		{mbs_bluetooth_mgmt_status, NULL},
	[meshbus_BluetoothMgmtCommandId_BLUETOOTH_MGMT_COMMAND_ID_ENABLE] =
		{NULL, mbs_bluetooth_mgmt_enable},
	[meshbus_BluetoothMgmtCommandId_BLUETOOTH_MGMT_COMMAND_ID_CONFIG] =
		{mbs_bluetooth_mgmt_config_get, mbs_bluetooth_mgmt_config_set},
	[meshbus_BluetoothMgmtCommandId_BLUETOOTH_MGMT_COMMAND_ID_CONFIG_RESET] =
		{NULL, mbs_bluetooth_mgmt_config_reset},
};

#define MBS_BLUETOOTH_MGMT_GROUP_SZ ARRAY_SIZE(mbs_bluetooth_mgmt_group_handlers)

static struct mgmt_group mbs_bluetooth_mgmt_group = {
	.mg_handlers = mbs_bluetooth_mgmt_group_handlers,
	.mg_handlers_count = MBS_BLUETOOTH_MGMT_GROUP_SZ,
	.mg_group_id = meshbus_BluetoothMgmtGroupId_BLUETOOTH_MGMT_GROUP_ID_MESHBUS_BLUETOOTH,
#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
	.mg_translate_error = mbs_bluetooth_mgmt_translate_error_code,
#endif
#ifdef CONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME
	.mg_group_name = "meshbus bluetooth mgmt",
#endif
};

static void mbs_bluetooth_mgmt_register_group(void)
{
	mgmt_register_group(&mbs_bluetooth_mgmt_group);
}

MCUMGR_HANDLER_DEFINE(mbs_bluetooth_mgmt, mbs_bluetooth_mgmt_register_group);
