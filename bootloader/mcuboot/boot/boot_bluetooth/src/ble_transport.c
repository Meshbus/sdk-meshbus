/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <boot_serial/boot_serial.h>
#include <bootutil/bootutil_log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
#include <zephyr/mgmt/mcumgr/transport/smp_bt.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/base64.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

BOOT_LOG_MODULE_DECLARE(mcuboot);

#define SMP_HDR_SIZE 8U
#define SERIAL_FRAME_CRC_SIZE 2U
#define SERIAL_FRAME_LEN_SIZE 2U
#define SERIAL_LINE_PREFIX_SIZE 2U
#define SERIAL_LINE_BINARY_CHUNK 93U
#define SERIAL_LINE_ENCODED_CHUNK 124U
#define SERIAL_LINE_MAX_SIZE \
	(SERIAL_LINE_PREFIX_SIZE + SERIAL_LINE_ENCODED_CHUNK + 2U)

#if CONFIG_BOOT_MAX_LINE_INPUT_LEN < SERIAL_LINE_MAX_SIZE
#error "CONFIG_BOOT_MAX_LINE_INPUT_LEN is too small for BLE serial bridge lines"
#endif

enum ble_bridge_source {
	BLE_BRIDGE_SOURCE_UART,
	BLE_BRIDGE_SOURCE_BLE,
};

static const struct boot_uart_funcs *uart_funcs;
static enum ble_bridge_source current_source;
static struct bt_conn *active_conn;
static atomic_t notify_enabled;
static bool bt_started;

static struct k_mutex bridge_lock;

static uint8_t rx_buf[CONFIG_BOOT_SERIAL_MAX_RECEIVE_SIZE];
static uint16_t rx_len;
static uint16_t rx_expected;
static bool rx_ready;

static uint16_t serial_frame_total;
static uint16_t serial_frame_off;
static uint16_t serial_frame_len_field;
static uint16_t serial_frame_crc;

static char out_line[CONFIG_BOOT_MAX_LINE_INPUT_LEN];
static uint16_t out_line_len;
static uint8_t out_dec[CONFIG_BOOT_SERIAL_MAX_RECEIVE_SIZE +
		       SERIAL_FRAME_LEN_SIZE + SERIAL_FRAME_CRC_SIZE];
static uint16_t out_dec_off;

static int bridge_read(char *str, int cnt, int *newline);
static void bridge_write(const char *ptr, int cnt);
static int notify_raw(const uint8_t *data, uint16_t len);
static void advertising_restart_work_handler(struct k_work *work);

static const struct boot_uart_funcs bridge_funcs = {
	.read = bridge_read,
	.write = bridge_write,
};

K_WORK_DELAYABLE_DEFINE(advertising_restart_work, advertising_restart_work_handler);

static const struct bt_data adv_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1U),
};

static const struct bt_data scan_rsp_data[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, SMP_BT_SVC_UUID_VAL),
};

static int start_advertising(void)
{
	return bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, adv_data, ARRAY_SIZE(adv_data),
			       scan_rsp_data, ARRAY_SIZE(scan_rsp_data));
}

static void advertising_restart_work_handler(struct k_work *work)
{
	int rc;

	ARG_UNUSED(work);

	rc = start_advertising();
	if (rc != 0 && rc != -EALREADY) {
		BOOT_LOG_WRN("MCUboot BLE recovery advertising restart failed: %d", rc);
	}
}

static void rx_reset_locked(void)
{
	rx_len = 0;
	rx_expected = 0;
	rx_ready = false;
	serial_frame_total = 0;
	serial_frame_off = 0;
	serial_frame_len_field = 0;
	serial_frame_crc = 0;
}

static void out_reset_locked(void)
{
	out_line_len = 0;
	out_dec_off = 0;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	if (err != 0U) {
		BOOT_LOG_WRN("MCUboot BLE recovery connection failed: %s err=%u", addr, err);
		return;
	}

	k_mutex_lock(&bridge_lock, K_FOREVER);
	if (active_conn == NULL) {
		active_conn = bt_conn_ref(conn);
		atomic_clear(&notify_enabled);
		rx_reset_locked();
		out_reset_locked();
	}
	k_mutex_unlock(&bridge_lock);

	BOOT_LOG_INF("MCUboot BLE recovery connected: %s", addr);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	k_mutex_lock(&bridge_lock, K_FOREVER);
	if (active_conn != NULL) {
		bt_conn_unref(active_conn);
		active_conn = NULL;
		atomic_clear(&notify_enabled);
		rx_reset_locked();
		out_reset_locked();
	}
	k_mutex_unlock(&bridge_lock);

	if (bt_started) {
		(void)k_work_reschedule(&advertising_restart_work, K_MSEC(250));
	}

	BOOT_LOG_DBG("MCUboot BLE recovery disconnected: reason=%u", reason);
}

BT_CONN_CB_DEFINE(ble_conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void notify_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	if ((value & BT_GATT_CCC_NOTIFY) != 0U) {
		atomic_set(&notify_enabled, 1);
	} else {
		atomic_clear(&notify_enabled);
	}
}

static ssize_t smp_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	const uint8_t *data = buf;
	uint16_t total_len;

	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (conn == NULL || offset != 0U || len == 0U) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	k_mutex_lock(&bridge_lock, K_FOREVER);

	if (active_conn != NULL && active_conn != conn) {
		k_mutex_unlock(&bridge_lock);
		return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
	}

	if (rx_ready) {
		k_mutex_unlock(&bridge_lock);
		return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
	}

	if ((uint32_t)rx_len + len > sizeof(rx_buf)) {
		rx_reset_locked();
		k_mutex_unlock(&bridge_lock);
		return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
	}

	memcpy(&rx_buf[rx_len], data, len);
	rx_len += len;

	if (rx_expected == 0U && rx_len >= SMP_HDR_SIZE) {
		total_len = SMP_HDR_SIZE + sys_get_be16(&rx_buf[2]);
		if (total_len > sizeof(rx_buf)) {
			rx_reset_locked();
			k_mutex_unlock(&bridge_lock);
			return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
		}
		rx_expected = total_len;
	}

	if (rx_expected != 0U) {
		if (rx_len > rx_expected) {
			rx_reset_locked();
			k_mutex_unlock(&bridge_lock);
			return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
		}

		if (rx_len == rx_expected) {
			serial_frame_len_field = rx_expected + SERIAL_FRAME_CRC_SIZE;
			serial_frame_total = SERIAL_FRAME_LEN_SIZE + serial_frame_len_field;
			serial_frame_crc = crc16_itu_t(0, rx_buf, rx_expected);
			serial_frame_off = 0;
			rx_ready = true;
		}
	}

	k_mutex_unlock(&bridge_lock);
	return len;
}

BT_GATT_SERVICE_DEFINE(ble_smp_svc,
	BT_GATT_PRIMARY_SERVICE(SMP_BT_SVC_UUID),
	BT_GATT_CHARACTERISTIC(SMP_BT_CHR_UUID,
			       BT_GATT_CHRC_WRITE_WITHOUT_RESP | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_WRITE, NULL, smp_write, NULL),
	BT_GATT_CCC(notify_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

static uint8_t serial_frame_byte_at(uint16_t off)
{
	if (off < SERIAL_FRAME_LEN_SIZE) {
		return ((const uint8_t *)&(uint16_t){
			sys_cpu_to_be16(serial_frame_len_field)
		})[off];
	}

	off -= SERIAL_FRAME_LEN_SIZE;
	if (off < rx_expected) {
		return rx_buf[off];
	}

	off -= rx_expected;
	return ((const uint8_t *)&(uint16_t){sys_cpu_to_be16(serial_frame_crc)})[off];
}

static int bridge_read_ble_locked(char *str, int cnt, int *newline)
{
	uint8_t binary[SERIAL_LINE_BINARY_CHUNK];
	size_t enc_len;
	uint16_t remaining;
	uint16_t binary_len;
	int rc;

	if (!rx_ready || serial_frame_off >= serial_frame_total) {
		return 0;
	}

	if (cnt < SERIAL_LINE_MAX_SIZE) {
		return 0;
	}

	remaining = serial_frame_total - serial_frame_off;
	binary_len = MIN(remaining, (uint16_t)sizeof(binary));

	for (uint16_t i = 0; i < binary_len; i++) {
		binary[i] = serial_frame_byte_at(serial_frame_off + i);
	}

	str[0] = (serial_frame_off == 0U) ? 6 : 4;
	str[1] = (serial_frame_off == 0U) ? 9 : 20;

	rc = base64_encode(&str[SERIAL_LINE_PREFIX_SIZE],
			   cnt - SERIAL_LINE_PREFIX_SIZE - 2,
			   &enc_len, binary, binary_len);
	if (rc != 0) {
		rx_reset_locked();
		return 0;
	}

	str[SERIAL_LINE_PREFIX_SIZE + enc_len] = '\n';
	str[SERIAL_LINE_PREFIX_SIZE + enc_len + 1U] = '\0';
	*newline = 1;

	serial_frame_off += binary_len;
	if (serial_frame_off >= serial_frame_total) {
		rx_reset_locked();
	}

	current_source = BLE_BRIDGE_SOURCE_BLE;
	return SERIAL_LINE_PREFIX_SIZE + enc_len + 2;
}

static int bridge_read(char *str, int cnt, int *newline)
{
	int rc;

	k_mutex_lock(&bridge_lock, K_FOREVER);
	rc = bridge_read_ble_locked(str, cnt, newline);
	k_mutex_unlock(&bridge_lock);
	if (rc > 0) {
		return rc;
	}

	current_source = BLE_BRIDGE_SOURCE_UART;
	return uart_funcs->read(str, cnt, newline);
}

static int notify_raw(const uint8_t *data, uint16_t len)
{
	struct bt_conn *conn;
	uint16_t mtu;
	uint16_t off = 0;
	int rc = 0;

	if (atomic_get(&notify_enabled) == 0) {
		return -ENOTCONN;
	}

	k_mutex_lock(&bridge_lock, K_FOREVER);
	conn = active_conn;
	if (conn != NULL) {
		bt_conn_ref(conn);
	}
	k_mutex_unlock(&bridge_lock);

	if (conn == NULL) {
		return -ENOTCONN;
	}

	mtu = bt_gatt_get_mtu(conn);
	mtu = (mtu > 3U) ? (mtu - 3U) : 20U;

	while (off < len) {
		uint16_t chunk_len = MIN(mtu, (uint16_t)(len - off));

		rc = bt_gatt_notify(conn, &ble_smp_svc.attrs[2], &data[off], chunk_len);
		if (rc != 0) {
			break;
		}
		off += chunk_len;
		k_yield();
	}

	bt_conn_unref(conn);
	return rc;
}

static void process_output_line_locked(void)
{
	size_t decoded_len;
	uint16_t expected_len;
	uint16_t raw_len;
	uint16_t crc;
	int rc;

	if (out_line_len < 3U) {
		out_line_len = 0;
		return;
	}

	if (out_line[0] == 6 && out_line[1] == 9) {
		out_dec_off = 0;
	} else if (!(out_line[0] == 4 && out_line[1] == 20)) {
		out_reset_locked();
		return;
	}

	rc = base64_decode(&out_dec[out_dec_off], sizeof(out_dec) - out_dec_off,
			   &decoded_len, &out_line[SERIAL_LINE_PREFIX_SIZE],
			   out_line_len - SERIAL_LINE_PREFIX_SIZE - 1U);
	if (rc != 0) {
		out_reset_locked();
		return;
	}

	out_dec_off += decoded_len;
	out_line_len = 0;

	if (out_dec_off <= SERIAL_FRAME_LEN_SIZE) {
		return;
	}

	expected_len = sys_get_be16(out_dec);
	if (expected_len != out_dec_off - SERIAL_FRAME_LEN_SIZE) {
		return;
	}

	if (expected_len <= SERIAL_FRAME_CRC_SIZE) {
		out_reset_locked();
		return;
	}

	crc = crc16_itu_t(0, &out_dec[SERIAL_FRAME_LEN_SIZE], expected_len);
	if (crc != 0U) {
		out_reset_locked();
		return;
	}

	raw_len = expected_len - SERIAL_FRAME_CRC_SIZE;
	k_mutex_unlock(&bridge_lock);
	rc = notify_raw(&out_dec[SERIAL_FRAME_LEN_SIZE], raw_len);
	k_mutex_lock(&bridge_lock, K_FOREVER);

	if (rc != 0) {
		BOOT_LOG_DBG("MCUboot BLE recovery notify failed: %d", rc);
	}

	out_reset_locked();
}

static void bridge_write_ble(const char *ptr, int cnt)
{
	k_mutex_lock(&bridge_lock, K_FOREVER);

	for (int i = 0; i < cnt; i++) {
		if (out_line_len >= sizeof(out_line)) {
			out_reset_locked();
			continue;
		}

		out_line[out_line_len++] = ptr[i];
		if (ptr[i] == '\n') {
			process_output_line_locked();
		}
	}

	k_mutex_unlock(&bridge_lock);
}

static void bridge_write(const char *ptr, int cnt)
{
	if (current_source == BLE_BRIDGE_SOURCE_BLE) {
		bridge_write_ble(ptr, cnt);
		return;
	}

	uart_funcs->write(ptr, cnt);
}

static int ble_transport_start(const struct boot_uart_funcs *f)
{
	int rc;

	uart_funcs = f;
	current_source = BLE_BRIDGE_SOURCE_UART;

	if (bt_started) {
		return 0;
	}

	k_mutex_init(&bridge_lock);
	rx_reset_locked();
	out_reset_locked();

	rc = bt_enable(NULL);
	if (rc != 0 && rc != -EALREADY) {
		BOOT_LOG_WRN("MCUboot BLE recovery init failed: %d", rc);
		return rc;
	}

	rc = start_advertising();
	if (rc != 0 && rc != -EALREADY) {
		BOOT_LOG_WRN("MCUboot BLE recovery advertising failed: %d", rc);
		return rc;
	}

	bt_started = true;
	BOOT_LOG_INF("MCUboot BLE recovery advertising as %s", CONFIG_BT_DEVICE_NAME);
	return 0;
}

extern void __real_boot_serial_start(const struct boot_uart_funcs *f);

void __wrap_boot_serial_start(const struct boot_uart_funcs *f)
{
	if (ble_transport_start(f) == 0) {
		__real_boot_serial_start(&bridge_funcs);
	} else {
		__real_boot_serial_start(f);
	}
}

#ifdef CONFIG_BOOT_SERIAL_WAIT_FOR_DFU
extern void __real_boot_serial_check_start(const struct boot_uart_funcs *f, int timeout_in_ms);

void __wrap_boot_serial_check_start(const struct boot_uart_funcs *f, int timeout_in_ms)
{
	if (ble_transport_start(f) == 0) {
		__real_boot_serial_check_start(&bridge_funcs, timeout_in_ms);
	} else {
		__real_boot_serial_check_start(f, timeout_in_ms);
	}
}
#endif
