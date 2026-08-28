/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <errno.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>

#define DEFAULT_RADIO_NODE DT_ALIAS(lora0)
BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(DEFAULT_RADIO_NODE),
	     "No default LoRa radio specified in DT");

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lora_cad);

static struct k_sem cad_done_sem;

static void lora_cad_done_cb(const struct device *dev, bool detected, void *user_data)
{
	bool *detected_out = user_data;

	ARG_UNUSED(dev);

	if (detected_out != NULL) {
		*detected_out = detected;
	}
	k_sem_give(&cad_done_sem);
}

static void config_defaults(struct lora_modem_config *config)
{
	*config = (struct lora_modem_config){0};

	config->frequency = 915125000;
	config->bandwidth = BW_125_KHZ;
	config->datarate = SF_8;
	config->preamble_len = 96;
	config->coding_rate = CR_4_7;
	config->iq_inverted = false;
	config->public_network = false;
	config->tx_power = 0;
	config->tx = false;
	config->cad.mode = LORA_CAD_MODE_NONE;
	config->cad.symbol_num = LORA_CAD_SYMB_2;
}

int main(void)
{
	const struct device *const lora_dev = DEVICE_DT_GET(DEFAULT_RADIO_NODE);
	struct lora_modem_config config;
	bool detected = false;
	int ret;

	if (!device_is_ready(lora_dev)) {
		LOG_ERR("%s Device not ready", lora_dev->name);
		return 0;
	}

	config_defaults(&config);

	ret = lora_config(lora_dev, &config);
	if (ret < 0) {
		LOG_ERR("LoRa config failed (%d)", ret);
		return 0;
	}

	/* Demonstrate synchronous CAD. */
	config.cad.symbol_num = LORA_CAD_SYMB_8;
	ret = lora_config(lora_dev, &config);
	if (ret < 0) {
		LOG_ERR("LoRa config for sync CAD failed (%d)", ret);
		return 0;
	}

	ret = lora_cad(lora_dev, K_SECONDS(1));
	if (ret == -ENOSYS) {
		LOG_INF("CAD not supported by this driver");
		k_sleep(K_FOREVER);
		return 0;
	}

	LOG_INF("CAD supported");

	if (ret == 0 || ret == 1) {
		detected = (ret == 1);
		LOG_INF("Synchronous CAD result: channel %s", detected ? "busy" : "free");
	} else if (ret == -ETIMEDOUT) {
		LOG_INF("Synchronous CAD timed out");
	} else {
		LOG_ERR("Synchronous CAD failed (%d)", ret);
	}

	/* Demonstrate asynchronous CAD. */
	k_sem_init(&cad_done_sem, 0, 1);
	detected = false;
	config.cad.symbol_num = LORA_CAD_SYMB_16;
	ret = lora_config(lora_dev, &config);
	if (ret < 0) {
		LOG_ERR("LoRa config for async CAD failed (%d)", ret);
		k_sleep(K_FOREVER);
		return 0;
	}

	ret = lora_cad_async(lora_dev, lora_cad_done_cb, &detected);
	if (ret == -ENOSYS) {
		LOG_INF("Async CAD not supported by this driver");
		k_sleep(K_FOREVER);
		return 0;
	} else if (ret < 0) {
		LOG_ERR("Async CAD start failed (%d)", ret);
		k_sleep(K_FOREVER);
		return 0;
	}

	if (k_sem_take(&cad_done_sem, K_SECONDS(2)) < 0) {
		LOG_ERR("Async CAD timed out");
		(void)lora_cad_async(lora_dev, NULL, NULL);
	} else {
		LOG_INF("Asynchronous CAD result: channel %s", detected ? "busy" : "free");
	}

	/* Periodically run synchronous CAD. */
	while (1) {
		detected = false;
		ret = lora_cad(lora_dev, K_SECONDS(1));
		if (ret == 0 || ret == 1) {
			detected = (ret == 1);
			LOG_INF("CAD loop: channel %s", detected ? "busy" : "free");
		} else if (ret == -ETIMEDOUT) {
			LOG_INF("CAD loop: timed out");
		} else if (ret == -ENOSYS) {
			LOG_INF("CAD no longer supported");
		} else {
			LOG_ERR("CAD loop failed (%d)", ret);
		}

		k_sleep(K_MSEC(1000));
	}

	return 0;
}
