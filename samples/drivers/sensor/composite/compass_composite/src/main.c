/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>

#include "compass_shell.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <drivers/sensor/compass_composite.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(compass_composite_sample, LOG_LEVEL_INF);

#define COMPASS_NODE DT_NODELABEL(compass)
#define COMPASS_PD_NODE DT_NODELABEL(peripheral_power)

#if DT_NODE_HAS_STATUS(COMPASS_PD_NODE, okay) && defined(CONFIG_PM_DEVICE_RUNTIME)
#define COMPASS_PD_BOOTSTRAP_INIT_PRIORITY 80

static int compass_sample_power_bootstrap_init(void)
{
	const struct device *pd = DEVICE_DT_GET(COMPASS_PD_NODE);
	int rc;

	if (!device_is_ready(pd)) {
		return -ENODEV;
	}

	rc = pm_device_runtime_get(pd);
	if (rc < 0 && rc != -ENOTSUP && rc != -ENOSYS) {
		return rc;
	}

	return 0;
}

SYS_INIT(compass_sample_power_bootstrap_init, POST_KERNEL,
	 COMPASS_PD_BOOTSTRAP_INIT_PRIORITY);
#endif

#if !DT_NODE_HAS_STATUS(COMPASS_NODE, okay)
#error "No compass node found. Check board overlay for zephyr,compass-composite node label compass"
#endif

static const char *cal_hint_to_str(int32_t v)
{
	switch (v) {
	case COMPASS_CAL_HINT_NONE:
		return "none";
	case COMPASS_CAL_HINT_FIGURE_EIGHT:
		return "figure-8";
	case COMPASS_CAL_HINT_KEEP_LEVEL:
		return "keep-level";
	default:
		return "unknown";
	}
}

static const char *accuracy_to_str(int32_t v)
{
	switch (v) {
	case COMPASS_ACCURACY_UNRELIABLE:
		return "unreliable";
	case COMPASS_ACCURACY_LOW:
		return "low";
	case COMPASS_ACCURACY_MEDIUM:
		return "medium";
	case COMPASS_ACCURACY_HIGH:
		return "high";
	default:
		return "unknown";
	}
}

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

static void sensor_value_or_na_snprint(char *buf, size_t len, const struct sensor_value *v, int rc)
{
	if (rc == 0) {
		sensor_value_snprint(buf, len, v);
	} else {
		(void)snprintk(buf, len, "n/a");
	}
}

int main(void)
{
	const struct device *compass = DEVICE_DT_GET(COMPASS_NODE);
	const enum sensor_channel magn_axis_channels[3] = {
		SENSOR_CHAN_MAGN_X,
		SENSOR_CHAN_MAGN_Y,
		SENSOR_CHAN_MAGN_Z,
	};
	struct sensor_value heading = {0};
	struct sensor_value declination = {0};
	struct sensor_value mount_offset = {0};
	struct sensor_value fusion_alpha = {0};
	struct sensor_value cal_hint = {0};
	struct sensor_value accuracy = {0};
	struct sensor_value bias[3] = {0};
	struct sensor_value scale[3] = {0};
	struct sensor_value bias_est[3] = {0};
	struct sensor_value scale_est[3] = {0};
	int bias_est_rc[3] = {0};
	int scale_est_rc[3] = {0};
	char heading_buf[24];
	char decl_buf[24];
	char mount_buf[24];
	char alpha_buf[24];
	char bias_buf[3][24];
	char scale_buf[3][24];
	char bias_est_buf[3][24];
	char scale_est_buf[3][24];
	uint32_t sleep_ms;
	int rc;

	LOG_INF("Compass composite sample started");
	LOG_INF("Build timestamp: " __DATE__ " " __TIME__);

	if (!device_is_ready(compass)) {
		LOG_ERR("Compass device not ready: %s", compass->name);
		return -ENODEV;
	}

	compass_sample_shell_bind_device(compass);
	LOG_INF("Compass device ready: %s", compass->name);
	LOG_INF("Shell commands: compass show | compass reset all | compass set ... | "
		"compass stream on <ms>");

	while (1) {
		if (!compass_sample_stream_enabled_get()) {
			k_sleep(K_MSEC(100));
			continue;
		}

		rc = sensor_sample_fetch(compass);
		if (rc != 0) {
			LOG_ERR("sample_fetch failed: %d", rc);
			k_sleep(K_SECONDS(1));
			continue;
		}

		rc = sensor_channel_get(compass, SENSOR_CHAN_COMPASS_HEADING, &heading);
		if (rc != 0) {
			LOG_ERR("heading get failed: %d", rc);
			k_sleep(K_SECONDS(1));
			continue;
		}

		(void)sensor_attr_get(compass, SENSOR_CHAN_COMPASS_HEADING,
				     SENSOR_ATTR_COMPASS_DECLINATION, &declination);
		(void)sensor_attr_get(compass, SENSOR_CHAN_COMPASS_HEADING,
				     SENSOR_ATTR_COMPASS_MOUNT_OFFSET, &mount_offset);
		(void)sensor_attr_get(compass, SENSOR_CHAN_COMPASS_HEADING,
				     SENSOR_ATTR_COMPASS_FUSION_ALPHA, &fusion_alpha);

		rc = sensor_attr_get(compass, SENSOR_CHAN_COMPASS_HEADING,
				     SENSOR_ATTR_COMPASS_CAL_HINT, &cal_hint);
		if (rc != 0) {
			cal_hint.val1 = -1;
			cal_hint.val2 = 0;
		}

		rc = sensor_attr_get(compass, SENSOR_CHAN_COMPASS_HEADING,
				     SENSOR_ATTR_COMPASS_ACCURACY, &accuracy);
		if (rc != 0) {
			accuracy.val1 = -1;
			accuracy.val2 = 0;
		}

		for (int i = 0; i < 3; i++) {
			enum sensor_channel channel = magn_axis_channels[i];

			(void)sensor_attr_get(compass, channel, SENSOR_ATTR_COMPASS_MAG_BIAS, &bias[i]);
			(void)sensor_attr_get(compass, channel, SENSOR_ATTR_COMPASS_MAG_SCALE, &scale[i]);
			bias_est_rc[i] =
				sensor_attr_get(compass, channel, SENSOR_ATTR_COMPASS_MAG_BIAS_EST, &bias_est[i]);
			scale_est_rc[i] =
				sensor_attr_get(compass, channel, SENSOR_ATTR_COMPASS_MAG_SCALE_EST,
						&scale_est[i]);
		}

		sensor_value_snprint(heading_buf, sizeof(heading_buf), &heading);
		sensor_value_snprint(decl_buf, sizeof(decl_buf), &declination);
		sensor_value_snprint(mount_buf, sizeof(mount_buf), &mount_offset);
		sensor_value_snprint(alpha_buf, sizeof(alpha_buf), &fusion_alpha);

		for (int i = 0; i < 3; i++) {
			sensor_value_snprint(bias_buf[i], sizeof(bias_buf[i]), &bias[i]);
			sensor_value_snprint(scale_buf[i], sizeof(scale_buf[i]), &scale[i]);
			sensor_value_or_na_snprint(bias_est_buf[i], sizeof(bias_est_buf[i]), &bias_est[i],
						  bias_est_rc[i]);
			sensor_value_or_na_snprint(scale_est_buf[i], sizeof(scale_est_buf[i]), &scale_est[i],
						   scale_est_rc[i]);
		}

		LOG_INF("heading=%s deg decl=%s deg mount=%s deg alpha=%s hint=%s(%d) accuracy=%s(%d) "
			"bias=[%s,%s,%s] scale=[%s,%s,%s] est_bias=[%s,%s,%s] est_scale=[%s,%s,%s]",
			heading_buf,
			decl_buf,
			mount_buf,
			alpha_buf,
			cal_hint_to_str(cal_hint.val1),
			cal_hint.val1,
			accuracy_to_str(accuracy.val1),
			accuracy.val1,
			bias_buf[0], bias_buf[1], bias_buf[2],
			scale_buf[0], scale_buf[1], scale_buf[2],
			bias_est_buf[0], bias_est_buf[1], bias_est_buf[2],
			scale_est_buf[0], scale_est_buf[1], scale_est_buf[2]);

		sleep_ms = compass_sample_stream_interval_ms_get();
		k_sleep(K_MSEC(sleep_ms));
	}

	return 0;
}
