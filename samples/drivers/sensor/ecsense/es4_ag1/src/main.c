/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(es4_ag1_sample, LOG_LEVEL_INF);

#define ES4_AG1_NODE DT_NODELABEL(es4_ag1)

#if !DT_NODE_HAS_STATUS(ES4_AG1_NODE, okay)
#error "No es4_ag1 node found. Check board overlay for ecsense,es4-ag1 node label es4_ag1"
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
	const struct device *sensor = DEVICE_DT_GET(ES4_AG1_NODE);
	struct sensor_value voltage = {0};
	struct sensor_value voc = {0};
	char voltage_buf[24];
	char voc_buf[24];
	int rc;

	if (!device_is_ready(sensor)) {
		LOG_ERR("Sensor device not ready: %s", sensor->name);
		return -ENODEV;
	}

	LOG_INF("ES4-AG1 sample started");
	LOG_INF("Sensor device: %s", sensor->name);

	while (1) {
		rc = sensor_sample_fetch(sensor);
		if (rc < 0) {
			LOG_ERR("sample_fetch failed: %d", rc);
			k_sleep(K_MSEC(250));
			continue;
		}

		rc = sensor_channel_get(sensor, SENSOR_CHAN_VOLTAGE, &voltage);
		if (rc < 0) {
			LOG_ERR("voltage get failed: %d", rc);
			k_sleep(K_MSEC(250));
			continue;
		}

		rc = sensor_channel_get(sensor, SENSOR_CHAN_VOC, &voc);
		if (rc < 0) {
			LOG_ERR("VOC get failed: %d", rc);
			k_sleep(K_MSEC(250));
			continue;
		}

		sensor_value_snprint(voltage_buf, sizeof(voltage_buf), &voltage);
		sensor_value_snprint(voc_buf, sizeof(voc_buf), &voc);

		LOG_INF("voltage=%s V, voc=%s ppm", voltage_buf, voc_buf);

		k_sleep(K_MSEC(250));
	}

	return 0;
}
