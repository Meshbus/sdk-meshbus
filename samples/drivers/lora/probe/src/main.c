/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define DEFAULT_RADIO_NODE DT_ALIAS(lora0)
BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(DEFAULT_RADIO_NODE), "No default LoRa radio specified in DT");

#define LORA_PROBE_BUILD_ID "lr11xx-private-sync-wait-heartbeat-v4"

#if DT_NODE_HAS_STATUS(DT_ALIAS(led0), okay)
static const struct gpio_dt_spec heartbeat_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
#endif

static uint8_t payload[] = "probe";

static void heartbeat_thread(void *arg1, void *arg2, void *arg3)
{
	uint32_t counter = 0;
	bool led_ready = false;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

#if DT_NODE_HAS_STATUS(DT_ALIAS(led0), okay)
	led_ready = gpio_is_ready_dt(&heartbeat_led);
	if (led_ready) {
		(void)gpio_pin_configure_dt(&heartbeat_led, GPIO_OUTPUT_INACTIVE);
	}
#endif

	while (1) {
#if DT_NODE_HAS_STATUS(DT_ALIAS(led0), okay)
		if (led_ready) {
			(void)gpio_pin_toggle_dt(&heartbeat_led);
		}
#endif
		printk("lora_probe: heartbeat %u\n", counter++);
		k_sleep(K_SECONDS(2));
	}
}

K_THREAD_DEFINE(heartbeat_tid, 1024, heartbeat_thread, NULL, NULL, NULL, K_PRIO_COOP(0), 0, 0);

int main(void)
{
	const struct device *const lora_dev = DEVICE_DT_GET(DEFAULT_RADIO_NODE);
	struct lora_modem_config config = {0};
	int ret;

	printk("lora_probe: booted, waiting for CDC host\n");
	printk("lora_probe: build=%s\n", LORA_PROBE_BUILD_ID);
	for (int i = 20; i > 0; i--) {
		printk("lora_probe: pre-radio countdown %d\n", i);
		k_sleep(K_SECONDS(1));
	}

	printk("lora_probe: device=%s\n", lora_dev->name);
	printk("lora_probe: checking device_is_ready\n");
	if (!device_is_ready(lora_dev)) {
		printk("lora_probe: device not ready\n");
		return 0;
	}
	printk("lora_probe: device ready\n");

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

	printk("lora_probe: calling lora_config\n");
	ret = lora_config(lora_dev, &config);
	printk("lora_probe: lora_config returned %d\n", ret);
	if (ret < 0) {
		return 0;
	}

	printk("lora_probe: calling lora_send\n");
	ret = lora_send(lora_dev, payload, sizeof(payload) - 1);
	printk("lora_probe: lora_send returned %d\n", ret);
	if (ret == -EBUSY) {
		printk("lora_probe: channel busy\n");
	}

	while (1) {
		k_sleep(K_SECONDS(1));
	}
}
