/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define AMBIENT_TEMP_NODE DT_ALIAS(ambient_temp0)

#if !DT_NODE_EXISTS(AMBIENT_TEMP_NODE)
#error "No ambient-temp0 alias found in devicetree"
#endif

static void sensor_value_snprint(char *buf, size_t len, const struct sensor_value *v)
{
	int64_t micro = sensor_value_to_micro(v);
	bool neg = (micro < 0);
	uint64_t abs_micro = (uint64_t)(neg ? -micro : micro);
	uint64_t ip = abs_micro / 1000000ULL;
	uint64_t fp = abs_micro % 1000000ULL;

	(void)snprintk(buf, len, "%s%llu.%06llu", neg ? "-" : "",
		       (unsigned long long)ip, (unsigned long long)fp);
}

int main(void)
{
	const struct device *const sensor = DEVICE_DT_GET(AMBIENT_TEMP_NODE);
	struct sensor_value temp = {0};
	char temp_buf[24];
	int rc;

	if (!device_is_ready(sensor)) {
		printk("NTC sensor not ready: %s\n", sensor->name);
		return -ENODEV;
	}

	printk("NTC thermistor sample started\n");
	printk("Sensor device: %s\n", sensor->name);

	while (1) {
		rc = sensor_sample_fetch_chan(sensor, SENSOR_CHAN_AMBIENT_TEMP);
		if (rc < 0) {
			printk("sample_fetch failed: %d\n", rc);
			k_sleep(K_MSEC(500));
			continue;
		}

		rc = sensor_channel_get(sensor, SENSOR_CHAN_AMBIENT_TEMP, &temp);
		if (rc < 0) {
			printk("channel_get failed: %d\n", rc);
			k_sleep(K_MSEC(500));
			continue;
		}

		sensor_value_snprint(temp_buf, sizeof(temp_buf), &temp);
		printk("NTC temperature: %s C\n", temp_buf);

		k_sleep(K_SECONDS(1));
	}

	return 0;
}
