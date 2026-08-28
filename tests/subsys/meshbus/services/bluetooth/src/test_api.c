// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/nus.h>
#include <zephyr/kernel.h>
#include <zephyr/meshbus/bluetooth.h>
#include <zephyr/meshbus/notify.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/printk.h>
#include <zephyr/ztest.h>

#include "meshcore/platform.h"

#include "companion/bluetooth.h"
#include "companion/protocol.h"

static struct bt_conn *const fake_conn = (struct bt_conn *)0x1;
static struct bt_conn *const fake_other_conn = (struct bt_conn *)0x2;
static const bt_addr_le_t fake_peer = {
	.type = BT_ADDR_LE_PUBLIC,
	.a = { .val = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 } },
};
static bool fake_bt_ready;
static bool fake_connected;
static bt_security_t fake_security_level = BT_SECURITY_L1;
static uint16_t fake_mtu = 64U;
static bool fake_is_subscribed;
static bool fake_nus_subscribed;
static uint32_t fake_notify_count;
static size_t fake_last_notify_len;
static struct bt_conn *fake_last_notify_conn;
static bool fake_adv_has_flags;
static bool fake_adv_has_tx_power;
static bool fake_adv_has_nus_uuid;
static bool fake_scan_has_name;
static bool fake_scan_has_meshcore_name;
static bool fake_scan_has_meshbus_name;
static char fake_bt_name[CONFIG_BT_DEVICE_NAME_MAX + 1U] = CONFIG_BT_DEVICE_NAME;

static void workq_drain(void);
static void fake_notify_ccc_set(bool enabled);
static void fake_adv_reset(void);
static void fake_adv_capture(const struct bt_data *ad, size_t ad_len,
			     const struct bt_data *sd, size_t sd_len);
static const struct bt_gatt_attr *find_companion_bluetooth_rx_attr(void);

bool __wrap_bt_is_ready(void)
{
	return fake_bt_ready;
}

int __wrap_bt_enable(bt_ready_cb_t cb)
{
	fake_bt_ready = true;
	if (cb != NULL) {
		cb(0);
	}

	return 0;
}

int __wrap_bt_conn_auth_cb_register(const struct bt_conn_auth_cb *cb)
{
	ARG_UNUSED(cb);
	return 0;
}

int __wrap_bt_conn_auth_info_cb_register(struct bt_conn_auth_info_cb *cb)
{
	ARG_UNUSED(cb);
	return 0;
}

int __wrap_bt_le_adv_start(const struct bt_le_adv_param *param,
			   const struct bt_data *ad, size_t ad_len,
			   const struct bt_data *sd, size_t sd_len)
{
	ARG_UNUSED(param);
	fake_adv_capture(ad, ad_len, sd, sd_len);
	return 0;
}

int __wrap_bt_le_adv_update_data(const struct bt_data *ad, size_t ad_len,
				 const struct bt_data *sd, size_t sd_len)
{
	fake_adv_capture(ad, ad_len, sd, sd_len);
	return 0;
}

int __wrap_bt_le_adv_stop(void)
{
	return 0;
}

int __wrap_bt_set_name(const char *name)
{
	if (name == NULL || strlen(name) > CONFIG_BT_DEVICE_NAME_MAX) {
		return -ENOMEM;
	}

	(void)snprintk(fake_bt_name, sizeof(fake_bt_name), "%s", name);
	return 0;
}

const char *__wrap_bt_get_name(void)
{
	return fake_bt_name;
}

int __wrap_settings_load_subtree(const char *subtree)
{
	ARG_UNUSED(subtree);
	return 0;
}

struct bt_conn *__wrap_bt_conn_ref(struct bt_conn *conn)
{
	return conn;
}

void __wrap_bt_conn_unref(struct bt_conn *conn)
{
	ARG_UNUSED(conn);
}

const bt_addr_le_t *__wrap_bt_conn_get_dst(const struct bt_conn *conn)
{
	ARG_UNUSED(conn);
	return &fake_peer;
}

int __wrap_bt_conn_set_security(struct bt_conn *conn, bt_security_t sec)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(sec);
	return 0;
}

bt_security_t __wrap_bt_conn_get_security(const struct bt_conn *conn)
{
	ARG_UNUSED(conn);
	return fake_security_level;
}

int __wrap_bt_conn_disconnect(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(reason);
	fake_connected = false;
	return 0;
}

int __wrap_bt_unpair(uint8_t id, const bt_addr_le_t *addr)
{
	ARG_UNUSED(id);
	ARG_UNUSED(addr);
	return 0;
}

void __wrap_bt_conn_foreach(enum bt_conn_type type,
			    void (*func)(struct bt_conn *conn, void *data),
			    void *data)
{
	if (fake_connected && type == BT_CONN_TYPE_LE && func != NULL) {
		func(fake_conn, data);
	}
}

int __wrap_bt_conn_get_info(const struct bt_conn *conn, struct bt_conn_info *info)
{
	if (conn == NULL || info == NULL) {
		return -EINVAL;
	}

	memset(info, 0, sizeof(*info));
	info->type = BT_CONN_TYPE_LE;
	info->state = fake_connected ? BT_CONN_STATE_CONNECTED : BT_CONN_STATE_DISCONNECTED;
	info->security.level = fake_security_level;
	return 0;
}

uint16_t __wrap_bt_gatt_get_mtu(struct bt_conn *conn)
{
	ARG_UNUSED(conn);
	return fake_mtu;
}

bool __wrap_bt_gatt_is_subscribed(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				  uint16_t ccc_type)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(ccc_type);
	return fake_is_subscribed;
}

int __wrap_bt_gatt_notify_cb(struct bt_conn *conn, struct bt_gatt_notify_params *params)
{
	zassert_not_null(params, "notify params");
	zassert_not_null(params->attr, "notify attr");
	zassert_not_null(params->data, "notify data");

	if (conn == NULL && !fake_is_subscribed && !fake_nus_subscribed) {
		return -ENOTCONN;
	}

	if (fake_mtu <= 3U || params->len > (fake_mtu - 3U)) {
		return -EMSGSIZE;
	}

	fake_notify_count++;
	fake_last_notify_len = params->len;
	fake_last_notify_conn = conn;
	return 0;
}

int meshcore_platform_telemetry_node_get(
	const meshcore_platform_request_source_t *requester,
	uint8_t permission_mask, meshcore_platform_telemetry_payload_t *out)
{
	ARG_UNUSED(requester);
	ARG_UNUSED(permission_mask);
	ARG_UNUSED(out);

	return -ENOSYS;
}

static void bluetooth_test_apply_enabled(bool enabled)
{
	meshbus_bluetooth_config cfg;

	zassert_ok(meshbus_bluetooth_config_get(&cfg), "config get failed");
	cfg.enabled = enabled;
	zassert_ok(meshbus_bluetooth_config_set(&cfg), "config set failed");
	workq_drain();
}

static void bluetooth_test_apply_companion_enabled(bool enabled)
{
	meshbus_bluetooth_config cfg;

	zassert_ok(meshbus_bluetooth_config_get(&cfg), "config get failed");
	cfg.meshcore_companion_enabled = enabled;
	zassert_ok(meshbus_bluetooth_config_set(&cfg), "config set failed");
	workq_drain();
}

static void bluetooth_test_conn_started(void)
{
	STRUCT_SECTION_FOREACH(bt_conn_cb, cb) {
		if (cb->connected != NULL) {
			cb->connected(fake_conn, 0);
		}
	}

	fake_connected = true;
	workq_drain();
}

static void bluetooth_test_conn_security_changed(bt_security_t level)
{
	fake_security_level = level;
	STRUCT_SECTION_FOREACH(bt_conn_cb, cb) {
		if (cb->security_changed != NULL) {
			cb->security_changed(fake_conn, level, BT_SECURITY_ERR_SUCCESS);
		}
	}
	workq_drain();
}

static void bluetooth_test_conn_connected(void)
{
	bluetooth_test_conn_started();
	bluetooth_test_conn_security_changed(BT_SECURITY_L4);
}

static void bluetooth_test_conn_disconnected(void)
{
	STRUCT_SECTION_FOREACH(bt_conn_cb, cb) {
		if (cb->disconnected != NULL) {
			cb->disconnected(fake_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		}
	}

	fake_connected = false;
	workq_drain();
}

static void workq_drain(void)
{
	int rc = k_work_queue_drain(&k_sys_work_q, false);

	zassert_true(rc >= 0, "workq drain failed: %d", rc);
	rc = meshbus_meshcore_test_companion_bluetooth_drain();
	zassert_true(rc >= 0, "Companion Bluetooth workq drain failed: %d", rc);
}

static void fake_notify_ccc_set(bool enabled)
{
	bool changed = false;

	fake_nus_subscribed = enabled;

	STRUCT_SECTION_FOREACH(bt_gatt_service_static, svc) {
		for (size_t i = 0; i < svc->attr_count; i++) {
			const struct bt_gatt_attr *attr = &svc->attrs[i];

			if (bt_uuid_cmp(attr->uuid, BT_UUID_GATT_CCC) != 0 || attr->user_data == NULL) {
				continue;
			}

			struct bt_gatt_ccc_managed_user_data *ccc = attr->user_data;

			if (ccc->cfg_changed != NULL) {
				ccc->cfg_changed(attr, enabled ? BT_GATT_CCC_NOTIFY : 0);
				changed = true;
			}
		}
	}

	zassert_true(changed, "No CCC descriptor was updated");
}

static void fake_adv_reset(void)
{
	fake_adv_has_flags = false;
	fake_adv_has_tx_power = false;
	fake_adv_has_nus_uuid = false;
	fake_scan_has_name = false;
	fake_scan_has_meshcore_name = false;
	fake_scan_has_meshbus_name = false;
}

static bool bt_data_payload_equals(const struct bt_data *data, const uint8_t *payload,
				   size_t payload_len)
{
	return data->data_len == payload_len && memcmp(data->data, payload, payload_len) == 0;
}

static void fake_adv_capture_list(const struct bt_data *data, size_t data_len, bool scan_rsp)
{
	static const uint8_t nus_uuid[] = {
		BT_UUID_NUS_SRV_VAL,
	};
	static const char meshcore_name_prefix[] = "MeshCore-";
	static const char meshbus_name_prefix[] = "Meshbus ";

	for (size_t i = 0; i < data_len; i++) {
		switch (data[i].type) {
		case BT_DATA_FLAGS:
			if (!scan_rsp) {
				fake_adv_has_flags = true;
			}
			break;
		case BT_DATA_TX_POWER:
			if (!scan_rsp) {
				fake_adv_has_tx_power = true;
			}
			break;
		case BT_DATA_UUID128_ALL:
			if (bt_data_payload_equals(&data[i], nus_uuid, sizeof(nus_uuid))) {
				fake_adv_has_nus_uuid = true;
			}
			break;
		case BT_DATA_NAME_COMPLETE:
			if (scan_rsp) {
				fake_scan_has_name = true;
				fake_scan_has_meshcore_name =
					data[i].data_len >= (sizeof(meshcore_name_prefix) - 1U) &&
					memcmp(data[i].data, meshcore_name_prefix,
					       sizeof(meshcore_name_prefix) - 1U) == 0;
				fake_scan_has_meshbus_name =
					data[i].data_len >= (sizeof(meshbus_name_prefix) - 1U) &&
					memcmp(data[i].data, meshbus_name_prefix,
					       sizeof(meshbus_name_prefix) - 1U) == 0;
			}
			break;
		default:
			break;
		}
	}
}

static void fake_adv_capture(const struct bt_data *ad, size_t ad_len,
			     const struct bt_data *sd, size_t sd_len)
{
	fake_adv_capture_list(ad, ad_len, false);
	fake_adv_capture_list(sd, sd_len, true);
}

static const struct bt_gatt_attr *find_companion_bluetooth_rx_attr(void)
{
	struct bt_uuid_128 rx_uuid = BT_UUID_INIT_128(BT_UUID_NUS_RX_CHAR_VAL);

	STRUCT_SECTION_FOREACH(bt_gatt_service_static, svc) {
		for (size_t i = 0; i < svc->attr_count; i++) {
			const struct bt_gatt_attr *attr = &svc->attrs[i];

			if (bt_uuid_cmp(attr->uuid, &rx_uuid.uuid) == 0 && attr->write != NULL) {
				return attr;
			}
		}
	}

	return NULL;
}

static ssize_t companion_bluetooth_write_frame_from(struct bt_conn *conn,
						    const uint8_t *frame, size_t len)
{
	const struct bt_gatt_attr *attr = find_companion_bluetooth_rx_attr();

	zassert_not_null(attr, "Companion NUS RX characteristic not found");
	return attr->write(conn, attr, frame, len, 0, BT_GATT_WRITE_FLAG_CMD);
}

static ssize_t companion_bluetooth_write_frame(const uint8_t *frame, size_t len)
{
	return companion_bluetooth_write_frame_from(fake_conn, frame, len);
}

static void fill_prefix(pb_bytes_array_t *dst, uint8_t seed)
{
	zassert_not_null(dst, "dst");
	dst->size = CONFIG_MESHBUS_CONTACT_PREFIX_BYTES;
	for (size_t i = 0; i < dst->size; i++) {
		dst->bytes[i] = (uint8_t)(seed + i);
	}
}

static meshbus_notify make_node_update_payload(void)
{
	meshbus_notify payload = meshbus_Notify_init_zero;

	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_NODE;
	payload.payload_variant.node.has_public_key_prefix = true;
	fill_prefix((pb_bytes_array_t *)&payload.payload_variant.node.public_key_prefix, 0x20);
	return payload;
}

static meshbus_notify make_large_telemetry_payload(void)
{
	meshbus_notify payload = meshbus_Notify_init_zero;

	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_NODE_TELEMETRY;
	fill_prefix((pb_bytes_array_t *)&payload.payload_variant.node_telemetry.public_key_prefix,
		    0x30);
	payload.payload_variant.node_telemetry.payload.size =
		sizeof(payload.payload_variant.node_telemetry.payload.bytes);
	memset(payload.payload_variant.node_telemetry.payload.bytes, 0x5a,
	       payload.payload_variant.node_telemetry.payload.size);
	return payload;
}

static void test_before(void *fixture)
{
	ARG_UNUSED(fixture);

	fake_mtu = 64U;
	fake_security_level = BT_SECURITY_L1;
	fake_is_subscribed = false;
	fake_nus_subscribed = false;
	fake_notify_count = 0U;
	fake_last_notify_len = 0U;
	fake_last_notify_conn = NULL;
	(void)snprintk(fake_bt_name, sizeof(fake_bt_name), "%s", CONFIG_BT_DEVICE_NAME);
	fake_adv_reset();
	fake_notify_ccc_set(false);
	bluetooth_test_conn_disconnected();
	bluetooth_test_apply_enabled(true);
	bluetooth_test_apply_companion_enabled(true);
	workq_drain();
}

ZTEST(meshbus_bluetooth_contract, test_companion_bluetooth_nus_uuid_is_advertised)
{
	zassert_true(fake_adv_has_flags, "advertising flags missing");
	zassert_true(fake_adv_has_tx_power, "advertising tx power missing");
	zassert_true(fake_adv_has_nus_uuid, "Companion NUS UUID missing from advertising");
	zassert_true(fake_scan_has_name, "scan response name missing");
	zassert_true(fake_scan_has_meshcore_name,
		     "scan response name must use MeshCore- prefix for iOS app discovery");
}

ZTEST(meshbus_bluetooth_contract, test_companion_bluetooth_disabled_removes_nus_uuid_from_advertising)
{
	fake_adv_reset();
	bluetooth_test_apply_companion_enabled(false);

	zassert_true(fake_adv_has_flags, "advertising flags missing");
	zassert_true(fake_adv_has_tx_power, "advertising tx power missing");
	zassert_false(fake_adv_has_nus_uuid, "Companion NUS UUID should be hidden");
	zassert_true(fake_scan_has_name, "scan response name missing");
	zassert_false(fake_scan_has_meshcore_name, "disabled Companion should not use MeshCore-");
	zassert_true(fake_scan_has_meshbus_name, "disabled Companion should use Meshbus prefix");
}

ZTEST(meshbus_bluetooth_contract, test_notify_drops_without_connection)
{
	meshbus_notify payload = make_node_update_payload();
	int rc;

	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_NODES_CHANGED, &payload);
	zassert_ok(rc, "notify publish failed: %d", rc);
	workq_drain();

	zassert_equal(fake_notify_count, 0U, "bt_gatt_notify should not be called");
}

ZTEST(meshbus_bluetooth_contract, test_notify_drops_when_unsubscribed)
{
	meshbus_notify payload = make_node_update_payload();
	int rc;

	bluetooth_test_conn_connected();
	fake_is_subscribed = false;

	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_NODES_CHANGED, &payload);
	zassert_ok(rc, "notify publish failed: %d", rc);
	workq_drain();

	zassert_equal(fake_notify_count, 0U, "bt_gatt_notify should not be called");
}

ZTEST(meshbus_bluetooth_contract, test_notify_drops_before_l4)
{
	const bt_security_t unauthorized_levels[] = {
		BT_SECURITY_L1,
		BT_SECURITY_L2,
		BT_SECURITY_L3,
	};
	meshbus_notify payload = make_node_update_payload();
	uint32_t attempted_before;
	uint32_t sent_before;
	uint32_t dropped_before;

	bluetooth_test_conn_started();
	fake_is_subscribed = true;
	fake_notify_ccc_set(true);
	fake_mtu = CONFIG_BT_L2CAP_TX_MTU;
	fake_notify_count = 0U;
	attempted_before = meshbus_bluetooth_stats.notify_attempted;
	sent_before = meshbus_bluetooth_stats.notify_sent;
	dropped_before = meshbus_bluetooth_stats.notify_dropped;

	for (size_t i = 0; i < ARRAY_SIZE(unauthorized_levels); i++) {
		bluetooth_test_conn_security_changed(unauthorized_levels[i]);
		zassert_ok(meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_NODES_CHANGED, &payload),
			   "notify publish failed at security level %u",
			   (unsigned int)unauthorized_levels[i]);
		workq_drain();
	}

	zassert_equal(fake_notify_count, 0U, "pre-L4 notify must not reach bt_gatt_notify");
	zassert_equal(meshbus_bluetooth_stats.notify_attempted,
		      attempted_before + ARRAY_SIZE(unauthorized_levels),
		      "each pre-L4 notify should be attempted");
	zassert_equal(meshbus_bluetooth_stats.notify_sent, sent_before,
		      "pre-L4 notify must not be counted as sent");
	zassert_equal(meshbus_bluetooth_stats.notify_dropped,
		      dropped_before + ARRAY_SIZE(unauthorized_levels),
		      "each pre-L4 notify should be dropped");
}

ZTEST(meshbus_bluetooth_contract, test_notify_sends_when_connected_and_subscribed)
{
	meshbus_notify payload = make_node_update_payload();
	int rc;

	bluetooth_test_conn_connected();
	fake_is_subscribed = true;
	fake_notify_ccc_set(true);
	fake_mtu = CONFIG_BT_L2CAP_TX_MTU;
	fake_notify_count = 0U;
	fake_last_notify_len = 0U;
	fake_last_notify_conn = fake_conn;

	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_NODES_CHANGED, &payload);
	zassert_ok(rc, "notify publish failed: %d", rc);
	workq_drain();

	zassert_equal(fake_notify_count, 1U, "bt_gatt_notify call count mismatch");
	zassert_is_null(fake_last_notify_conn, "notify should use CCC table connection lookup");
	zassert_true(fake_last_notify_len > 0U, "encoded notify length should be non-zero");
}

ZTEST(meshbus_bluetooth_contract, test_notify_uses_ccc_table_when_conn_subscription_lookup_is_stale)
{
	meshbus_notify payload = make_node_update_payload();
	int rc;

	bluetooth_test_conn_connected();
	fake_is_subscribed = false;
	fake_notify_ccc_set(true);
	fake_mtu = CONFIG_BT_L2CAP_TX_MTU;
	fake_notify_count = 0U;
	fake_last_notify_len = 0U;
	fake_last_notify_conn = fake_conn;

	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_NODES_CHANGED, &payload);
	zassert_ok(rc, "notify publish failed: %d", rc);
	workq_drain();

	zassert_equal(fake_notify_count, 1U, "bt_gatt_notify call count mismatch");
	zassert_is_null(fake_last_notify_conn, "notify should use CCC table connection lookup");
	zassert_true(fake_last_notify_len > 0U, "encoded notify length should be non-zero");
}

ZTEST(meshbus_bluetooth_contract, test_notify_drops_oversize_payload_for_current_mtu)
{
	meshbus_notify payload = make_large_telemetry_payload();
	int rc;

	bluetooth_test_conn_connected();
	fake_is_subscribed = true;
	fake_mtu = 20U;
	fake_notify_count = 0U;
	fake_last_notify_len = 0U;

	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_NODE_TELEMETRY, &payload);
	zassert_ok(rc, "notify publish failed: %d", rc);
	workq_drain();

	zassert_equal(fake_notify_count, 0U, "oversize notify should not reach bt_gatt_notify");
}

ZTEST(meshbus_bluetooth_contract, test_companion_bluetooth_rx_write_calls_protocol_and_notifies)
{
	const uint8_t get_time_frame[] = { 5 };
	uint32_t rx_before = meshbus_meshcore_test_companion_bluetooth_rx_count();
	uint32_t tx_before = meshbus_meshcore_test_companion_bluetooth_tx_count();
	ssize_t rc;

	bluetooth_test_conn_connected();
	fake_is_subscribed = true;
	fake_notify_ccc_set(true);
	fake_mtu = CONFIG_BT_L2CAP_TX_MTU;
	fake_notify_count = 0U;
	fake_last_notify_len = 0U;
	fake_last_notify_conn = fake_conn;

	rc = companion_bluetooth_write_frame(get_time_frame, sizeof(get_time_frame));
	zassert_equal(rc, sizeof(get_time_frame), "NUS RX write failed: %d", (int)rc);
	workq_drain();

	zassert_equal(meshbus_meshcore_test_companion_bluetooth_rx_count(), rx_before + 1U,
		      "NUS RX count mismatch");
	zassert_equal(meshbus_meshcore_test_companion_bluetooth_tx_count(), tx_before + 1U,
		      "NUS TX count mismatch");
	zassert_equal(fake_notify_count, 1U, "NUS response should notify once");
	zassert_is_null(fake_last_notify_conn, "NUS TX should use CCC table connection lookup");
	zassert_equal(fake_last_notify_len, 5U, "GET_DEVICE_TIME response length mismatch");
}

ZTEST(meshbus_bluetooth_contract, test_companion_bluetooth_rejects_rx_before_l4)
{
	const uint8_t get_time_frame[] = { 5 };
	const bt_security_t unauthorized_levels[] = {
		BT_SECURITY_L1,
		BT_SECURITY_L2,
		BT_SECURITY_L3,
	};
	uint32_t rx_before = meshbus_meshcore_test_companion_bluetooth_rx_count();
	uint32_t tx_before = meshbus_meshcore_test_companion_bluetooth_tx_count();
	uint32_t drop_before = meshbus_meshcore_test_companion_bluetooth_drop_count();

	bluetooth_test_conn_started();

	for (size_t i = 0; i < ARRAY_SIZE(unauthorized_levels); i++) {
		bluetooth_test_conn_security_changed(unauthorized_levels[i]);
		zassert_equal(companion_bluetooth_write_frame(get_time_frame,
							       sizeof(get_time_frame)),
			      sizeof(get_time_frame), "NUS write should retain ATT semantics");
		workq_drain();
	}

	zassert_equal(meshbus_meshcore_test_companion_bluetooth_rx_count(), rx_before,
		      "pre-L4 RX must not reach protocol");
	zassert_equal(meshbus_meshcore_test_companion_bluetooth_tx_count(), tx_before,
		      "pre-L4 RX must not produce a response");
	zassert_equal(meshbus_meshcore_test_companion_bluetooth_drop_count(),
		      drop_before + ARRAY_SIZE(unauthorized_levels),
		      "each pre-L4 frame should be counted as dropped");
}

ZTEST(meshbus_bluetooth_contract, test_companion_bluetooth_rejects_rx_from_non_active_connection)
{
	const uint8_t get_time_frame[] = { 5 };
	uint32_t rx_before = meshbus_meshcore_test_companion_bluetooth_rx_count();
	uint32_t tx_before = meshbus_meshcore_test_companion_bluetooth_tx_count();
	uint32_t drop_before = meshbus_meshcore_test_companion_bluetooth_drop_count();

	bluetooth_test_conn_connected();
	zassert_equal(companion_bluetooth_write_frame_from(fake_other_conn, get_time_frame,
							  sizeof(get_time_frame)),
		      sizeof(get_time_frame), "NUS write should retain ATT semantics");
	workq_drain();

	zassert_equal(meshbus_meshcore_test_companion_bluetooth_rx_count(), rx_before,
		      "non-active connection RX must not reach protocol");
	zassert_equal(meshbus_meshcore_test_companion_bluetooth_tx_count(), tx_before,
		      "non-active connection RX must not produce a response");
	zassert_equal(meshbus_meshcore_test_companion_bluetooth_drop_count(), drop_before + 1U,
		      "non-active connection frame should be counted as dropped");
}

ZTEST(meshbus_bluetooth_contract, test_companion_bluetooth_rejects_rx_after_disconnect)
{
	const uint8_t get_time_frame[] = { 5 };
	uint32_t rx_before;
	uint32_t tx_before;
	uint32_t drop_before;

	bluetooth_test_conn_connected();
	bluetooth_test_conn_disconnected();
	rx_before = meshbus_meshcore_test_companion_bluetooth_rx_count();
	tx_before = meshbus_meshcore_test_companion_bluetooth_tx_count();
	drop_before = meshbus_meshcore_test_companion_bluetooth_drop_count();

	zassert_equal(companion_bluetooth_write_frame(get_time_frame, sizeof(get_time_frame)),
		      sizeof(get_time_frame), "NUS write should retain ATT semantics");
	workq_drain();

	zassert_equal(meshbus_meshcore_test_companion_bluetooth_rx_count(), rx_before,
		      "disconnected RX must not reach protocol");
	zassert_equal(meshbus_meshcore_test_companion_bluetooth_tx_count(), tx_before,
		      "disconnected RX must not produce a response");
	zassert_equal(meshbus_meshcore_test_companion_bluetooth_drop_count(), drop_before + 1U,
		      "disconnected frame should be counted as dropped");
}

ZTEST(meshbus_bluetooth_contract, test_companion_bluetooth_disabled_drops_rx_without_response)
{
	const uint8_t get_time_frame[] = { 5 };
	uint32_t rx_before = meshbus_meshcore_test_companion_bluetooth_rx_count();
	uint32_t tx_before = meshbus_meshcore_test_companion_bluetooth_tx_count();
	uint32_t drop_before = meshbus_meshcore_test_companion_bluetooth_drop_count();
	ssize_t rc;

	bluetooth_test_conn_connected();
	fake_is_subscribed = true;
	fake_notify_ccc_set(true);
	fake_mtu = CONFIG_BT_L2CAP_TX_MTU;
	fake_notify_count = 0U;

	bluetooth_test_apply_companion_enabled(false);
	fake_notify_count = 0U;

	rc = companion_bluetooth_write_frame(get_time_frame, sizeof(get_time_frame));
	zassert_equal(rc, sizeof(get_time_frame), "static NUS RX write should be accepted");
	workq_drain();

	zassert_equal(meshbus_meshcore_test_companion_bluetooth_rx_count(), rx_before,
		      "disabled Companion should not reach protocol");
	zassert_equal(meshbus_meshcore_test_companion_bluetooth_tx_count(), tx_before,
		      "disabled Companion should not send a response");
	zassert_true(meshbus_meshcore_test_companion_bluetooth_drop_count() > drop_before,
		     "disabled Companion should count the dropped RX frame");
	zassert_equal(fake_notify_count, 0U, "disabled Companion should not notify");
}

ZTEST(meshbus_bluetooth_contract, test_companion_bluetooth_device_query_notifies_with_configured_mtu)
{
	const uint8_t device_query_frame[] = { 22, 11 };
	ssize_t rc;

	bluetooth_test_conn_connected();
	fake_is_subscribed = true;
	fake_notify_ccc_set(true);
	fake_mtu = CONFIG_BT_L2CAP_TX_MTU;
	fake_notify_count = 0U;
	fake_last_notify_len = 0U;
	fake_last_notify_conn = fake_conn;

	rc = companion_bluetooth_write_frame(device_query_frame, sizeof(device_query_frame));
	zassert_equal(rc, sizeof(device_query_frame), "NUS RX write failed: %d", (int)rc);
	workq_drain();

	zassert_equal(fake_notify_count, 1U, "device query should notify once");
	zassert_is_null(fake_last_notify_conn, "NUS TX should use CCC table connection lookup");
	zassert_equal(fake_last_notify_len, 82U, "DEVICE_INFO response length mismatch");
}

ZTEST(meshbus_bluetooth_contract, test_companion_bluetooth_reconnect_uses_restored_ccc_table)
{
	const uint8_t device_query_frame[] = { 22, 11 };
	ssize_t rc;

	bluetooth_test_conn_connected();
	fake_is_subscribed = true;
	fake_notify_ccc_set(true);
	fake_mtu = CONFIG_BT_L2CAP_TX_MTU;
	bluetooth_test_conn_disconnected();

	bluetooth_test_conn_connected();
	fake_is_subscribed = false;
	fake_notify_count = 0U;
	fake_last_notify_len = 0U;
	fake_last_notify_conn = fake_conn;

	rc = companion_bluetooth_write_frame(device_query_frame, sizeof(device_query_frame));
	zassert_equal(rc, sizeof(device_query_frame), "NUS RX write failed: %d", (int)rc);
	workq_drain();

	zassert_equal(fake_notify_count, 1U, "device query should notify after bonded reconnect");
	zassert_is_null(fake_last_notify_conn, "NUS TX should use CCC table connection lookup");
	zassert_equal(fake_last_notify_len, 82U, "DEVICE_INFO response length mismatch");
}

ZTEST(meshbus_bluetooth_contract, test_companion_bluetooth_tx_waits_for_subscription)
{
	const uint8_t get_time_frame[] = { 5 };
	uint32_t drop_before = meshbus_meshcore_test_companion_bluetooth_drop_count();

	bluetooth_test_conn_connected();
	fake_is_subscribed = false;
	fake_notify_count = 0U;
	fake_last_notify_len = 0U;

	zassert_equal(companion_bluetooth_write_frame(get_time_frame, sizeof(get_time_frame)),
		      sizeof(get_time_frame), "NUS RX write should be accepted");
	workq_drain();

	zassert_equal(fake_notify_count, 0U, "unsubscribed NUS TX should wait");
	zassert_equal(meshbus_meshcore_test_companion_bluetooth_drop_count(), drop_before,
		      "unsubscribed NUS TX should not be dropped");

	fake_notify_ccc_set(true);
	workq_drain();

	zassert_equal(fake_notify_count, 1U, "pending NUS TX should notify after subscribe");
	zassert_equal(fake_last_notify_len, 5U, "GET_DEVICE_TIME response length mismatch");
}

ZTEST(meshbus_bluetooth_contract, test_companion_bluetooth_drops_oversize_rx)
{
	uint8_t frame[MESHCORE_COMPANION_MAX_FRAME_SIZE + 1U] = {0};
	uint32_t rx_before = meshbus_meshcore_test_companion_bluetooth_rx_count();
	ssize_t rc;

	bluetooth_test_conn_connected();

	rc = companion_bluetooth_write_frame(frame, sizeof(frame));
	zassert_equal(rc, sizeof(frame), "upstream NUS RX write should be accepted");
	workq_drain();

	zassert_equal(meshbus_meshcore_test_companion_bluetooth_rx_count(), rx_before,
		      "oversize NUS RX should not reach protocol");
	zassert_true(meshbus_meshcore_test_companion_bluetooth_drop_count() > 0U,
		     "NUS drop count should increase");
}

ZTEST(meshbus_bluetooth_contract, test_companion_bluetooth_drops_oversize_tx_for_current_mtu)
{
	const uint8_t get_time_frame[] = { 5 };
	uint32_t tx_before = meshbus_meshcore_test_companion_bluetooth_tx_count();

	bluetooth_test_conn_connected();
	fake_is_subscribed = true;
	fake_notify_ccc_set(true);
	fake_mtu = 4U;
	fake_notify_count = 0U;
	fake_last_notify_len = 0U;

	zassert_equal(companion_bluetooth_write_frame(get_time_frame, sizeof(get_time_frame)),
		      sizeof(get_time_frame), "NUS RX write should be accepted");
	workq_drain();

	zassert_equal(fake_notify_count, 0U, "oversize NUS TX should not notify");
	zassert_equal(meshbus_meshcore_test_companion_bluetooth_tx_count(), tx_before,
		      "oversize NUS TX should not increment sent count");
	zassert_true(meshbus_meshcore_test_companion_bluetooth_drop_count() > 0U,
		     "NUS drop count should increase");
}

ZTEST_SUITE(meshbus_bluetooth_contract, NULL, NULL, test_before, NULL, NULL);
