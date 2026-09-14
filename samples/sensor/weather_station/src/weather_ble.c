/* Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "ble_packets.h"
#include "ble_text.h"
#include "weather_ble.h"

#define WEATHER_UUID(n) BT_UUID_128_ENCODE(0x8e7a0000 + (n), 0x6d4b, 0x4f23, 0x9a71, 0x5c2e9f0b3401)
static struct bt_uuid_128 service_uuid = BT_UUID_INIT_128(WEATHER_UUID(0));
static struct bt_uuid_128 env_uuid = BT_UUID_INIT_128(WEATHER_UUID(1));
static struct bt_uuid_128 air_uuid = BT_UUID_INIT_128(WEATHER_UUID(2));
static struct bt_uuid_128 pm_uuid = BT_UUID_INIT_128(WEATHER_UUID(3));
static struct bt_uuid_128 text_uuids[] = {
	BT_UUID_INIT_128(WEATHER_UUID(0x11)), BT_UUID_INIT_128(WEATHER_UUID(0x12)),
	BT_UUID_INIT_128(WEATHER_UUID(0x13)), BT_UUID_INIT_128(WEATHER_UUID(0x14)),
	BT_UUID_INIT_128(WEATHER_UUID(0x15)), BT_UUID_INIT_128(WEATHER_UUID(0x16)),
	BT_UUID_INIT_128(WEATHER_UUID(0x17)), BT_UUID_INIT_128(WEATHER_UUID(0x18)),
	BT_UUID_INIT_128(WEATHER_UUID(0x19))};
static uint8_t snapshot[WEATHER_PACKET_COUNT][WEATHER_PACKET_MAX];
static int64_t sampled_at;
static uint16_t sequence;
static bool enabled;
K_MUTEX_DEFINE(snapshot_lock);

static ssize_t read_packet(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			   uint16_t len, uint16_t offset)
{
	size_t index = (uintptr_t)attr->user_data;
	uint8_t packet[WEATHER_PACKET_MAX];

	k_mutex_lock(&snapshot_lock, K_FOREVER);
	memcpy(packet, snapshot[index], sizeof(packet));
	if (sampled_at == 0 || k_uptime_get() - sampled_at > 6000) {
		packet[1] = 0;
	}
	k_mutex_unlock(&snapshot_lock);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, packet, weather_packet_sizes[index]);
}

static ssize_t read_text(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			 uint16_t len, uint16_t offset)
{
	uint8_t packets[WEATHER_PACKET_COUNT][WEATHER_PACKET_MAX];
	char text[WEATHER_TEXT_MAX + 1];
	size_t text_len;
	k_mutex_lock(&snapshot_lock, K_FOREVER);
	memcpy(packets, snapshot, sizeof(packets));
	if (sampled_at != 0 && k_uptime_get() - sampled_at > 6000) {
		for (size_t i = 0; i < WEATHER_PACKET_COUNT; i++) {
			packets[i][1] = 0;
		}
		packets[1][13] = 0xff;
	}
	k_mutex_unlock(&snapshot_lock);
	text_len = weather_text((uintptr_t)attr->user_data, packets, text);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, text, text_len);
}

#define WEATHER_TEXT_CHARACTERISTIC(index, label)                                                  \
	BT_GATT_CHARACTERISTIC(&text_uuids[index].uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,   \
			       BT_GATT_PERM_READ, read_text, NULL, (void *)(uintptr_t)(index)),    \
		BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),                         \
		BT_GATT_CUD(label, BT_GATT_PERM_READ)

#define WEATHER_CHARACTERISTIC(uuid, index)                                                        \
	BT_GATT_CHARACTERISTIC(uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_READ,   \
			       read_packet, NULL, (void *)(uintptr_t)(index)),                     \
		BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)

BT_GATT_SERVICE_DEFINE(
	weather_svc, BT_GATT_PRIMARY_SERVICE(&service_uuid),
	WEATHER_CHARACTERISTIC(&env_uuid.uuid, 0), WEATHER_CHARACTERISTIC(&air_uuid.uuid, 1),
	WEATHER_CHARACTERISTIC(&pm_uuid.uuid, 2),
	WEATHER_TEXT_CHARACTERISTIC(0, "Temperature text"),
	WEATHER_TEXT_CHARACTERISTIC(1, "Humidity text"),
	WEATHER_TEXT_CHARACTERISTIC(2, "Pressure text"),
	WEATHER_TEXT_CHARACTERISTIC(3, "eCO2 text"), WEATHER_TEXT_CHARACTERISTIC(4, "TVOC text"),
	WEATHER_TEXT_CHARACTERISTIC(5, "AQI text"), WEATHER_TEXT_CHARACTERISTIC(6, "PM1.0 text"),
	WEATHER_TEXT_CHARACTERISTIC(7, "PM2.5 text"), WEATHER_TEXT_CHARACTERISTIC(8, "PM10 text"));

static const struct bt_data advertising[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, WEATHER_UUID(0)),
};
static const struct bt_data scan_response[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void advertise(struct k_work *work)
{
	int ret;

	ARG_UNUSED(work);
	ret = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, advertising, ARRAY_SIZE(advertising),
			      scan_response, ARRAY_SIZE(scan_response));
	printk("WEATHER_BLE advertising rc=%d name=%s\n", ret, CONFIG_BT_DEVICE_NAME);
}
K_WORK_DEFINE(advertise_work, advertise);

static void connected(struct bt_conn *conn, uint8_t err)
{
	ARG_UNUSED(conn);
	printk("WEATHER_BLE connected err=%u\n", err);
}
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	printk("WEATHER_BLE disconnected reason=%u\n", reason);
}
static void recycled(void)
{
	k_work_submit(&advertise_work);
}
BT_CONN_CB_DEFINE(callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled,
};

int weather_ble_start(void)
{
	const struct readings empty = {0};
	int ret;

	weather_encode(&empty, 0, snapshot);
	ret = bt_enable(NULL);
	if (ret < 0) {
		return ret;
	}
	enabled = true;
	k_work_submit(&advertise_work);
	return 0;
}

void weather_ble_publish(const struct readings *value)
{
	uint8_t packets[WEATHER_PACKET_COUNT][WEATHER_PACKET_MAX];
	static const uint8_t attributes[] = {2, 5, 8};

	if (!enabled) {
		return;
	}
	weather_encode(value, ++sequence, packets);
	k_mutex_lock(&snapshot_lock, K_FOREVER);
	memcpy(snapshot, packets, sizeof(snapshot));
	sampled_at = value->sampled_at;
	k_mutex_unlock(&snapshot_lock);
	for (size_t i = 0; i < WEATHER_PACKET_COUNT; i++) {
		int ret = bt_gatt_notify(NULL, &weather_svc.attrs[attributes[i]], packets[i],
					 weather_packet_sizes[i]);

		if (ret != 0 && ret != -ENOTCONN) {
			printk("WEATHER_BLE notify index=%u rc=%d\n", (unsigned int)i, ret);
		}
	}
	for (size_t i = 0; i < WEATHER_TEXT_COUNT; i++) {
		char text[WEATHER_TEXT_MAX + 1];
		size_t len = weather_text(i, packets, text);
		/* Binary service occupies attributes 0..9, each text adds four. */
		int ret = bt_gatt_notify(NULL, &weather_svc.attrs[11 + 4 * i], text, len);
		if (ret != 0 && ret != -ENOTCONN) {
			printk("WEATHER_BLE text notify index=%u rc=%d\n", (unsigned int)i, ret);
		}
	}
}
