/*
 * Copyright (c) 2019 Manivannan Sadhasivam
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <errno.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>

#define DEFAULT_RADIO_NODE DT_ALIAS(lora0)
BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(DEFAULT_RADIO_NODE), "No default LoRa radio specified in DT");

#define MAX_DATA_LEN 255
#define CAD_IDLE_BACKOFF_MS 50

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lora_receive);

void lora_receive_cb(const struct device *dev, uint8_t *data, uint16_t size, int16_t rssi,
		     int8_t snr, void *user_data)
{
	static int cnt;

	ARG_UNUSED(dev);
	ARG_UNUSED(size);
	ARG_UNUSED(user_data);

	LOG_INF("LoRa RX RSSI: %d dBm, SNR: %d dB", rssi, snr);
	LOG_HEXDUMP_INF(data, size, "LoRa RX payload");

	/* Stop receiving after 10 packets */
	if (++cnt == 10) {
		LOG_INF("Stopping packet receptions");
		lora_recv_async(dev, NULL, NULL);
	}
}

int main(void)
{
	const struct device *const lora_dev = DEVICE_DT_GET(DEFAULT_RADIO_NODE);
	struct lora_modem_config config = {0};
	int ret, len;
	uint8_t data[MAX_DATA_LEN] = {0};
	int16_t rssi;
	int8_t snr;

	if (!device_is_ready(lora_dev)) {
		LOG_ERR("%s Device not ready", lora_dev->name);
		return 0;
	}

	config.frequency = 915125000;
	config.bandwidth = BW_125_KHZ;
	config.datarate = SF_8;
	config.preamble_len = 96;
	config.coding_rate = CR_4_7;
	config.iq_inverted = false;
	config.public_network = false;
	config.rx_boosted = RX_BOOST_DISABLED;
	config.cad.mode = LORA_CAD_MODE_RX;
	config.tx_power = 0;
	config.tx = false;

	ret = lora_config(lora_dev, &config);
	if (ret < 0) {
		LOG_ERR("LoRa config failed");
		return 0;
	}

	/* Receive 4 packets synchronously */
	LOG_INF("Synchronous reception");
	for (int i = 0; i < 4; i++) {
		while (true) {
			/* Wait for a packet. With CAD RX gating, 0 means no activity
			 * was detected during this probe window, so keep waiting.
			 */
			len = lora_recv(lora_dev, data, MAX_DATA_LEN, K_FOREVER, &rssi, &snr);
			if (len == 0) {
				k_sleep(K_MSEC(CAD_IDLE_BACKOFF_MS));
				continue;
			}
			if (len < 0) {
				LOG_ERR("LoRa receive failed: %d", len);
				return 0;
			}
			break;
		}

		LOG_INF("LoRa RX RSSI: %d dBm, SNR: %d dB", rssi, snr);
		LOG_HEXDUMP_INF(data, len, "LoRa RX payload");
	}

	/* Enable asynchronous reception */
	LOG_INF("Asynchronous reception");
	lora_recv_async(lora_dev, lora_receive_cb, NULL);
	k_sleep(K_FOREVER);
	return 0;
}
