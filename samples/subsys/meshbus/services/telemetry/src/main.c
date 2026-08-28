/*
 * Copyright (c) 2025 FoBE Projects
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Meshbus subsystem test application
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>

#include <zephyr/meshbus/telemetry.h>

LOG_MODULE_REGISTER(meshbus_test, LOG_LEVEL_INF);

static void sensor_value_snprint(char *buf, size_t len, const struct sensor_value *v)
{
	int64_t micro = sensor_value_to_micro(v);
	bool neg = (micro < 0);
	uint64_t abs_micro = (uint64_t)(neg ? -micro : micro);

	uint64_t ip = abs_micro / 1000000ULL;
	uint64_t fp = abs_micro % 1000000ULL;

	(void)snprintk(buf, len, "%s%llu.%06llu", neg ? "-" : "", (unsigned long long)ip,
		       (unsigned long long)fp);
}

static void sensor_values_snprint(char *buf, size_t len, const struct sensor_value *vals,
				  size_t count)
{
	size_t off = 0U;

	if (len == 0U) {
		return;
	}

	off += (size_t)snprintk(buf + off, len - off, "(");
	for (size_t i = 0; i < count && off < len; i++) {
		char vbuf[24];

		sensor_value_snprint(vbuf, sizeof(vbuf), &vals[i]);
		off += (size_t)snprintk(buf + off, len - off, "%s%s", (i == 0U) ? "" : ", ", vbuf);
	}
	(void)snprintk(buf + off, len - off, ")");
}

static void telemetry_data_listener_cb(const struct zbus_channel *chan, const void *message)
{
	if (chan != &meshbus_telemetry_data_chan || message == NULL) {
		return;
	}

	const struct meshbus_telemetry_data_event *event = message;
	size_t count = MIN((size_t)event->value_count, (size_t)MESHBUS_TELEMETRY_MAX_VALUES);
	char tuple[96];

	sensor_values_snprint(tuple, sizeof(tuple), event->values, count);
	LOG_INF("telemetry: ts=%u chan=%u values=%s", event->timestamp, (unsigned int)event->chan,
		tuple);
}

ZBUS_ASYNC_LISTENER_DEFINE(telemetry_data_listener, telemetry_data_listener_cb);

int main(void)
{
	LOG_INF("Meshbus test application started");
	LOG_INF("Build timestamp: " __DATE__ " " __TIME__);

#ifdef CONFIG_BOOTLOADER_MCUBOOT
	LOG_INF("MCUboot bootloader support enabled");
#endif

	int rc = zbus_chan_add_obs(&meshbus_telemetry_data_chan, &telemetry_data_listener,
				   K_MSEC(100));
	if (rc != 0) {
		LOG_ERR("Failed to subscribe telemetry data channel: %d", rc);
	} else {
		LOG_INF("Subscribed to meshbus_telemetry_data_chan");
	}

	return 0;
}
