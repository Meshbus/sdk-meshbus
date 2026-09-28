/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/kernel.h>

#define DEFAULT_RADIO_NODE DT_ALIAS(lora0)
BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(DEFAULT_RADIO_NODE), "No default LoRa radio specified in DT");

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lora_duty_cycle);

/* Validated for the SF8, BW125, 96-symbol preamble configured below. */
#define RX_PERIOD_MS    73
#define SLEEP_PERIOD_MS 141

static void duty_cycle_recv_cb(const struct device *dev, uint8_t *data, uint16_t size, int16_t rssi,
			       int8_t snr, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	LOG_INF("RX %d bytes, RSSI: %d dBm, SNR: %d dB", size, rssi, snr);
	LOG_HEXDUMP_INF(data, size, "payload");
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

	config.frequency = 915125000;
	config.bandwidth = BW_125_KHZ;
	config.datarate = SF_8;
	config.preamble_len = 96;
	config.coding_rate = CR_4_7;
	config.iq_inverted = false;
	config.public_network = false;
	config.rx_boosted = RX_BOOST_DISABLED;
	config.cad.mode = LORA_CAD_MODE_NONE;
	config.tx_power = 0;
	config.tx = false;

	ret = lora_config(lora_dev, &config);
	if (ret < 0) {
		LOG_ERR("LoRa config failed (%d)", ret);
		return 0;
	}

	LOG_INF("RX duty-cycle started (rx=%d ms, sleep=%d ms)", RX_PERIOD_MS, SLEEP_PERIOD_MS);

	ret = lora_recv_duty_cycle_async(lora_dev, K_MSEC(RX_PERIOD_MS), K_MSEC(SLEEP_PERIOD_MS),
					 duty_cycle_recv_cb, NULL);
	if (ret < 0) {
		LOG_ERR("lora_recv_duty_cycle_async failed (%d)", ret);
		return 0;
	}

	k_sleep(K_FOREVER);
	return 0;
}
