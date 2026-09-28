/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define DEFAULT_RADIO_NODE DT_ALIAS(lora0)
BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(DEFAULT_RADIO_NODE),
	     "No default LoRa radio specified in DT");

#define MAX_DATA_LEN 255
#define RSSI_POLL_INTERVAL K_MSEC(1000)

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lora_rssi_inst);

static void lora_receive_cb(const struct device *dev, uint8_t *data, uint16_t size,
			    int16_t rssi, int8_t snr, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	LOG_INF("LoRa RX packet RSSI: %d dBm, SNR: %d dB", rssi, snr);
	LOG_HEXDUMP_INF(data, size, "LoRa RX payload");
}

int main(void)
{
	const struct device *const lora_dev = DEVICE_DT_GET(DEFAULT_RADIO_NODE);
	struct lora_modem_config config = {0};
	int ret;

	if (!device_is_ready(lora_dev)) {
		LOG_ERR("%s Device not ready", lora_dev->name);
		return 0;
	}

	/* Match the send/receive samples' default configuration. */
	config.frequency = 915125000;
	config.bandwidth = BW_125_KHZ;
	config.datarate = SF_8;
	config.preamble_len = 96;
	config.coding_rate = CR_4_7;
	config.iq_inverted = false;
	config.public_network = false;
	config.rx_boosted = RX_BOOST_ENABLED;
	config.tx_power = 0;
	config.tx = false;

	ret = lora_config(lora_dev, &config);
	if (ret < 0) {
		LOG_ERR("LoRa config failed (%d)", ret);
		return 0;
	}

	/*
	 * Per the API contract, lora_rssi_inst() should be called while the modem
	 * is in receive mode, e.g. during asynchronous receive.
	 */
	LOG_INF("Starting asynchronous reception (required for rssi_inst)");
	ret = lora_recv_async(lora_dev, lora_receive_cb, NULL);
	if (ret < 0) {
		LOG_ERR("LoRa async receive setup failed (%d)", ret);
		return 0;
	}

	while (1) {
		int16_t rssi = 0;

		ret = lora_rssi_inst(lora_dev, &rssi);
		if (ret == -ENOSYS) {
			LOG_WRN("Instantaneous RSSI not supported by this driver");
			break;
		} else if (ret < 0) {
			LOG_WRN("Instantaneous RSSI query failed (%d)", ret);
		} else {
			LOG_INF("Instantaneous RSSI: %d dBm", rssi);
		}

		k_sleep(RSSI_POLL_INTERVAL);
	}

	/* Keep the application alive so logs can be inspected. */
	k_sleep(K_FOREVER);
	return 0;
}
