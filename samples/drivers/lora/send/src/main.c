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

#define MAX_DATA_LEN 12
#define CAD_BUSY_BACKOFF_MIN_MS 50U
#define CAD_BUSY_BACKOFF_MAX_MS 300U

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lora_send);

char data[MAX_DATA_LEN] = {'h', 'e', 'l', 'l', 'o', 'w', 'o', 'r', 'l', 'd', ' ', '0'};

static uint32_t cad_busy_backoff_ms(void)
{
	uint32_t span = CAD_BUSY_BACKOFF_MAX_MS - CAD_BUSY_BACKOFF_MIN_MS + 1U;

	return CAD_BUSY_BACKOFF_MIN_MS + (k_cycle_get_32() % span);
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
	config.cad.mode = LORA_CAD_MODE_LBT;
	config.tx_power = 0;
	config.tx = true;

	ret = lora_config(lora_dev, &config);
	if (ret < 0) {
		LOG_ERR("LoRa config failed");
		return 0;
	}

	LOG_INF("Expected packet airtime: %u ms", lora_airtime(lora_dev, MAX_DATA_LEN));

	while (1) {
		ret = lora_send(lora_dev, data, MAX_DATA_LEN);
		if (ret < 0) {
			if (ret == -EBUSY) {
				uint32_t backoff_ms = cad_busy_backoff_ms();

				LOG_WRN("Channel busy (CAD LBT), backoff %u ms", backoff_ms);
				k_sleep(K_MSEC(backoff_ms));
				continue;
			}

			LOG_ERR("LoRa send failed (%d)", ret);
			return 0;
		}

		LOG_INF("Data sent %c!", data[MAX_DATA_LEN - 1]);

		/* Send data at 1s interval */
		k_sleep(K_MSEC(1000));

		/* Increment final character to differentiate packets */
		if (data[MAX_DATA_LEN - 1] == '9') {
			data[MAX_DATA_LEN - 1] = '0';
		} else {
			data[MAX_DATA_LEN - 1] += 1;
		}
	}
	return 0;
}
