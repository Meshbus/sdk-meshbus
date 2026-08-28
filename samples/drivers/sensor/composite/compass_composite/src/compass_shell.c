/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "compass_shell.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/compass_composite.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_SHELL)
#include <zephyr/shell/shell.h>
#endif

#define COMPASS_NODE DT_NODELABEL(compass)

#if !DT_NODE_HAS_STATUS(COMPASS_NODE, okay)
#error "No compass node found. Check board overlay for zephyr,compass-composite node label compass"
#endif

#define COMPASS_STREAM_INTERVAL_MS_DEFAULT 100U
#define COMPASS_STREAM_INTERVAL_MS_MIN     20U
#define COMPASS_STREAM_INTERVAL_MS_MAX     5000U

static const struct device *compass_shell_dev;
static atomic_t compass_stream_enabled = ATOMIC_INIT(1);
static atomic_t compass_stream_interval_ms =
	ATOMIC_INIT(COMPASS_STREAM_INTERVAL_MS_DEFAULT);

void compass_sample_shell_bind_device(const struct device *dev)
{
	compass_shell_dev = dev;
}

bool compass_sample_stream_enabled_get(void)
{
	return atomic_get(&compass_stream_enabled) != 0;
}

uint32_t compass_sample_stream_interval_ms_get(void)
{
	int32_t interval = atomic_get(&compass_stream_interval_ms);

	if (interval <= 0) {
		return COMPASS_STREAM_INTERVAL_MS_DEFAULT;
	}

	return (uint32_t)interval;
}

#if defined(CONFIG_SHELL)

struct compass_snapshot {
	struct sensor_value heading;
	struct sensor_value declination;
	struct sensor_value mount_offset;
	struct sensor_value fusion_alpha;
	struct sensor_value cal_hint;
	struct sensor_value accuracy;
	struct sensor_value bias[3];
	struct sensor_value scale[3];
	struct sensor_value bias_est[3];
	struct sensor_value scale_est[3];
	int bias_est_rc[3];
	int scale_est_rc[3];
};

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

static const struct device *compass_resolve_device(void)
{
	const struct device *dev = compass_shell_dev;

	if (dev == NULL) {
		dev = DEVICE_DT_GET(COMPASS_NODE);
	}

	if (!device_is_ready(dev)) {
		return NULL;
	}

	return dev;
}

static enum sensor_channel axis_to_channel(int axis)
{
	static const enum sensor_channel channels[3] = {
		SENSOR_CHAN_MAGN_X,
		SENSOR_CHAN_MAGN_Y,
		SENSOR_CHAN_MAGN_Z,
	};

	return channels[axis];
}

static int parse_axis(const char *axis_str)
{
	if ((axis_str == NULL) || (strlen(axis_str) != 1U)) {
		return -EINVAL;
	}

	switch (axis_str[0]) {
	case 'x':
	case 'X':
		return 0;
	case 'y':
	case 'Y':
		return 1;
	case 'z':
	case 'Z':
		return 2;
	default:
		return -EINVAL;
	}
}

static int parse_double_arg(const char *str, double *out)
{
	char *endptr;
	double value;

	if ((str == NULL) || (out == NULL)) {
		return -EINVAL;
	}

	value = strtod(str, &endptr);
	if ((endptr == str) || (*endptr != '\0')) {
		return -EINVAL;
	}

	*out = value;
	return 0;
}

static int set_heading_attr_from_double(const struct device *dev, enum sensor_attribute attr,
					double value)
{
	struct sensor_value sensor_val = {0};
	int rc;

	rc = sensor_value_from_double(&sensor_val, value);
	if (rc != 0) {
		return rc;
	}

	return sensor_attr_set(dev, SENSOR_CHAN_COMPASS_HEADING, attr, &sensor_val);
}

static int set_magn_attr_from_double(const struct device *dev, enum sensor_attribute attr, int axis,
				     double value)
{
	struct sensor_value sensor_val = {0};
	int rc;

	rc = sensor_value_from_double(&sensor_val, value);
	if (rc != 0) {
		return rc;
	}

	return sensor_attr_set(dev, axis_to_channel(axis), attr, &sensor_val);
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

static int compass_read_snapshot(const struct device *dev, struct compass_snapshot *snap,
				 bool fetch_sample)
{
	int rc;

	if ((dev == NULL) || (snap == NULL)) {
		return -EINVAL;
	}

	if (fetch_sample) {
		rc = sensor_sample_fetch(dev);
		if (rc != 0) {
			return rc;
		}
	}

	rc = sensor_channel_get(dev, SENSOR_CHAN_COMPASS_HEADING, &snap->heading);
	if (rc != 0) {
		return rc;
	}

	rc = sensor_attr_get(dev, SENSOR_CHAN_COMPASS_HEADING,
			     SENSOR_ATTR_COMPASS_DECLINATION, &snap->declination);
	if (rc != 0) {
		return rc;
	}

	rc = sensor_attr_get(dev, SENSOR_CHAN_COMPASS_HEADING,
			     SENSOR_ATTR_COMPASS_MOUNT_OFFSET, &snap->mount_offset);
	if (rc != 0) {
		return rc;
	}

	rc = sensor_attr_get(dev, SENSOR_CHAN_COMPASS_HEADING,
			     SENSOR_ATTR_COMPASS_FUSION_ALPHA, &snap->fusion_alpha);
	if (rc != 0) {
		return rc;
	}

	rc = sensor_attr_get(dev, SENSOR_CHAN_COMPASS_HEADING,
			     SENSOR_ATTR_COMPASS_CAL_HINT, &snap->cal_hint);
	if (rc != 0) {
		return rc;
	}

	rc = sensor_attr_get(dev, SENSOR_CHAN_COMPASS_HEADING,
			     SENSOR_ATTR_COMPASS_ACCURACY, &snap->accuracy);
	if (rc != 0) {
		return rc;
	}

	for (int i = 0; i < 3; i++) {
		enum sensor_channel channel = axis_to_channel(i);

		rc = sensor_attr_get(dev, channel, SENSOR_ATTR_COMPASS_MAG_BIAS, &snap->bias[i]);
		if (rc != 0) {
			return rc;
		}

		rc = sensor_attr_get(dev, channel, SENSOR_ATTR_COMPASS_MAG_SCALE, &snap->scale[i]);
		if (rc != 0) {
			return rc;
		}

		snap->bias_est_rc[i] = sensor_attr_get(dev, channel, SENSOR_ATTR_COMPASS_MAG_BIAS_EST,
						      &snap->bias_est[i]);
		snap->scale_est_rc[i] = sensor_attr_get(dev, channel, SENSOR_ATTR_COMPASS_MAG_SCALE_EST,
						       &snap->scale_est[i]);
	}

	return 0;
}

static void compass_print_snapshot(const struct shell *sh, const struct compass_snapshot *snap)
{
	char heading_buf[24];
	char decl_buf[24];
	char mount_buf[24];
	char alpha_buf[24];
	char bias_buf[3][24];
	char scale_buf[3][24];
	char bias_est_buf[3][24];
	char scale_est_buf[3][24];

	sensor_value_snprint(heading_buf, sizeof(heading_buf), &snap->heading);
	sensor_value_snprint(decl_buf, sizeof(decl_buf), &snap->declination);
	sensor_value_snprint(mount_buf, sizeof(mount_buf), &snap->mount_offset);
	sensor_value_snprint(alpha_buf, sizeof(alpha_buf), &snap->fusion_alpha);

	for (int i = 0; i < 3; i++) {
		sensor_value_snprint(bias_buf[i], sizeof(bias_buf[i]), &snap->bias[i]);
		sensor_value_snprint(scale_buf[i], sizeof(scale_buf[i]), &snap->scale[i]);
		sensor_value_or_na_snprint(bias_est_buf[i], sizeof(bias_est_buf[i]), &snap->bias_est[i],
					  snap->bias_est_rc[i]);
		sensor_value_or_na_snprint(scale_est_buf[i], sizeof(scale_est_buf[i]), &snap->scale_est[i],
					   snap->scale_est_rc[i]);
	}

	shell_print(sh,
		    "heading=%s deg decl=%s deg mount=%s deg alpha=%s hint=%s(%d) accuracy=%s(%d)",
		    heading_buf, decl_buf, mount_buf, alpha_buf,
		    cal_hint_to_str(snap->cal_hint.val1), snap->cal_hint.val1,
		    accuracy_to_str(snap->accuracy.val1), snap->accuracy.val1);
	shell_print(sh,
		    "active_bias=[%s %s %s] active_scale=[%s %s %s]",
		    bias_buf[0], bias_buf[1], bias_buf[2],
		    scale_buf[0], scale_buf[1], scale_buf[2]);
	shell_print(sh,
		    "est_bias=[%s %s %s] est_scale=[%s %s %s]",
		    bias_est_buf[0], bias_est_buf[1], bias_est_buf[2],
		    scale_est_buf[0], scale_est_buf[1], scale_est_buf[2]);
}

static int cmd_compass_show(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = compass_resolve_device();
	struct compass_snapshot snap = {0};
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (dev == NULL) {
		shell_error(sh, "Compass device not ready");
		return -ENODEV;
	}

	rc = compass_read_snapshot(dev, &snap, true);
	if (rc != 0) {
		shell_error(sh, "Failed to read compass snapshot: %d", rc);
		return rc;
	}

	compass_print_snapshot(sh, &snap);
	shell_print(sh, "stream=%s interval_ms=%u",
		    compass_sample_stream_enabled_get() ? "on" : "off",
		    compass_sample_stream_interval_ms_get());

	return 0;
}

static int cmd_compass_dump(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = compass_resolve_device();
	struct compass_snapshot snap = {0};
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (dev == NULL) {
		shell_error(sh, "Compass device not ready");
		return -ENODEV;
	}

	rc = compass_read_snapshot(dev, &snap, true);
	if (rc != 0) {
		shell_error(sh, "Failed to read compass snapshot: %d", rc);
		return rc;
	}

	shell_print(sh, "# Replay current runtime tuning:");
	shell_print(sh, "compass set decl %.6f", sensor_value_to_double(&snap.declination));
	shell_print(sh, "compass set mount %.6f", sensor_value_to_double(&snap.mount_offset));
	shell_print(sh, "compass set alpha %.6f", sensor_value_to_double(&snap.fusion_alpha));
	for (int i = 0; i < 3; i++) {
		char axis_name = (i == 0) ? 'x' : (i == 1) ? 'y' : 'z';
		bool estimate_available =
			snap.bias_est_rc[i] == 0 && snap.scale_est_rc[i] == 0;
		bool active_non_default = sensor_value_to_micro(&snap.bias[i]) != 0 ||
					  sensor_value_to_micro(&snap.scale[i]) != 1000000LL;

		if (!estimate_available && !active_non_default) {
			shell_print(sh, "# axis %c has no estimate; replay omitted", axis_name);
			continue;
		}

		shell_print(sh, "compass set bias %c %.6f", axis_name,
			    sensor_value_to_double(&snap.bias[i]));
		shell_print(sh, "compass set scale %c %.6f", axis_name,
			    sensor_value_to_double(&snap.scale[i]));
	}

	return 0;
}

static int cmd_compass_stream_on(const struct shell *sh, size_t argc, char **argv)
{
	char *endptr;
	unsigned long parsed;

	if (argc != 2U) {
		shell_error(sh, "Usage: compass stream on <ms>");
		return -EINVAL;
	}

	parsed = strtoul(argv[1], &endptr, 0);
	if ((endptr == argv[1]) || (*endptr != '\0')) {
		shell_error(sh, "Invalid interval: %s", argv[1]);
		return -EINVAL;
	}

	parsed = CLAMP(parsed, COMPASS_STREAM_INTERVAL_MS_MIN, COMPASS_STREAM_INTERVAL_MS_MAX);
	atomic_set(&compass_stream_interval_ms, (atomic_val_t)parsed);
	atomic_set(&compass_stream_enabled, 1);

	shell_print(sh, "stream=on interval_ms=%lu", parsed);
	return 0;
}

static int cmd_compass_stream_off(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	atomic_set(&compass_stream_enabled, 0);
	shell_print(sh, "stream=off");

	return 0;
}

static int cmd_compass_set_decl(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = compass_resolve_device();
	double value;
	int rc;

	if (argc != 2U) {
		shell_error(sh, "Usage: compass set decl <deg>");
		return -EINVAL;
	}
	if (dev == NULL) {
		shell_error(sh, "Compass device not ready");
		return -ENODEV;
	}
	if (parse_double_arg(argv[1], &value) != 0) {
		shell_error(sh, "Invalid value: %s", argv[1]);
		return -EINVAL;
	}

	rc = set_heading_attr_from_double(dev, SENSOR_ATTR_COMPASS_DECLINATION, value);
	if (rc != 0) {
		shell_error(sh, "Failed to set declination: %d", rc);
		return rc;
	}

	shell_print(sh, "declination set to %.6f deg", value);
	return 0;
}

static int cmd_compass_set_mount(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = compass_resolve_device();
	double value;
	int rc;

	if (argc != 2U) {
		shell_error(sh, "Usage: compass set mount <deg>");
		return -EINVAL;
	}
	if (dev == NULL) {
		shell_error(sh, "Compass device not ready");
		return -ENODEV;
	}
	if (parse_double_arg(argv[1], &value) != 0) {
		shell_error(sh, "Invalid value: %s", argv[1]);
		return -EINVAL;
	}

	rc = set_heading_attr_from_double(dev, SENSOR_ATTR_COMPASS_MOUNT_OFFSET, value);
	if (rc != 0) {
		shell_error(sh, "Failed to set mount offset: %d", rc);
		return rc;
	}

	shell_print(sh, "mount offset set to %.6f deg", value);
	return 0;
}

static int cmd_compass_set_alpha(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = compass_resolve_device();
	double value;
	int rc;

	if (argc != 2U) {
		shell_error(sh, "Usage: compass set alpha <0..1>");
		return -EINVAL;
	}
	if (dev == NULL) {
		shell_error(sh, "Compass device not ready");
		return -ENODEV;
	}
	if (parse_double_arg(argv[1], &value) != 0) {
		shell_error(sh, "Invalid value: %s", argv[1]);
		return -EINVAL;
	}

	rc = set_heading_attr_from_double(dev, SENSOR_ATTR_COMPASS_FUSION_ALPHA, value);
	if (rc != 0) {
		shell_error(sh, "Failed to set fusion alpha: %d", rc);
		return rc;
	}

	shell_print(sh, "fusion alpha set to %.6f", value);
	return 0;
}

static int cmd_compass_set_bias(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = compass_resolve_device();
	double value;
	int axis;
	int rc;

	if (argc != 3U) {
		shell_error(sh, "Usage: compass set bias <x|y|z> <gauss>");
		return -EINVAL;
	}
	if (dev == NULL) {
		shell_error(sh, "Compass device not ready");
		return -ENODEV;
	}
	axis = parse_axis(argv[1]);
	if (axis < 0) {
		shell_error(sh, "Invalid axis: %s", argv[1]);
		return -EINVAL;
	}
	if (parse_double_arg(argv[2], &value) != 0) {
		shell_error(sh, "Invalid value: %s", argv[2]);
		return -EINVAL;
	}

	rc = set_magn_attr_from_double(dev, SENSOR_ATTR_COMPASS_MAG_BIAS, axis, value);
	if (rc != 0) {
		shell_error(sh, "Failed to set magnet bias: %d", rc);
		return rc;
	}

	shell_print(sh, "mag bias %s set to %.6f", argv[1], value);
	return 0;
}

static int cmd_compass_set_scale(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = compass_resolve_device();
	double value;
	int axis;
	int rc;

	if (argc != 3U) {
		shell_error(sh, "Usage: compass set scale <x|y|z> <ratio>");
		return -EINVAL;
	}
	if (dev == NULL) {
		shell_error(sh, "Compass device not ready");
		return -ENODEV;
	}
	axis = parse_axis(argv[1]);
	if (axis < 0) {
		shell_error(sh, "Invalid axis: %s", argv[1]);
		return -EINVAL;
	}
	if (parse_double_arg(argv[2], &value) != 0) {
		shell_error(sh, "Invalid value: %s", argv[2]);
		return -EINVAL;
	}

	rc = set_magn_attr_from_double(dev, SENSOR_ATTR_COMPASS_MAG_SCALE, axis, value);
	if (rc != 0) {
		shell_error(sh, "Failed to set magnet scale: %d", rc);
		return rc;
	}

	shell_print(sh, "mag scale %s set to %.6f", argv[1], value);
	return 0;
}

static int cmd_compass_clear(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = compass_resolve_device();
	int axis;
	int rc;

	if (argc != 2U) {
		shell_error(sh, "Usage: compass clear <x|y|z>");
		return -EINVAL;
	}
	if (dev == NULL) {
		shell_error(sh, "Compass device not ready");
		return -ENODEV;
	}
	axis = parse_axis(argv[1]);
	if (axis < 0) {
		shell_error(sh, "Invalid axis: %s", argv[1]);
		return -EINVAL;
	}

	rc = set_magn_attr_from_double(dev, SENSOR_ATTR_COMPASS_MAG_SCALE, axis, 0.0);
	if (rc != 0) {
		shell_error(sh, "Failed to clear axis override: %d", rc);
		return rc;
	}

	shell_print(sh, "mag override cleared for axis %s", argv[1]);
	return 0;
}

static int cmd_compass_reset(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = compass_resolve_device();
	struct sensor_value trigger = {.val1 = 1};
	bool clear_manual = false;
	int rc;

	if (argc == 2U) {
		if (strcmp(argv[1], "all") != 0) {
			shell_error(sh, "Usage: compass reset [all]");
			return -EINVAL;
		}
		clear_manual = true;
	}
	if (dev == NULL) {
		shell_error(sh, "Compass device not ready");
		return -ENODEV;
	}

	if (clear_manual) {
		for (int axis = 0; axis < 3; axis++) {
			rc = set_magn_attr_from_double(dev, SENSOR_ATTR_COMPASS_MAG_SCALE, axis,
						       0.0);
			if (rc != 0) {
				shell_error(sh, "Failed to clear axis %d override: %d", axis, rc);
				return rc;
			}
		}
	}

	rc = sensor_attr_set(dev, SENSOR_CHAN_COMPASS_HEADING, SENSOR_ATTR_COMPASS_CAL_RESET,
			     &trigger);
	if (rc != 0) {
		shell_error(sh, "Failed to reset runtime calibration: %d", rc);
		return rc;
	}

	shell_print(sh, "runtime calibration reset; manual overrides %s",
		    clear_manual ? "cleared" : "preserved");
	shell_print(sh, "move the board through a slow 3D figure-eight until accuracy=high");

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	compass_set_subcmds,
	SHELL_CMD_ARG(decl, NULL, "Set declination in degrees", cmd_compass_set_decl, 2, 0),
	SHELL_CMD_ARG(mount, NULL, "Set mounting yaw offset in degrees", cmd_compass_set_mount, 2, 0),
	SHELL_CMD_ARG(alpha, NULL, "Set fusion alpha in [0, 1]", cmd_compass_set_alpha, 2, 0),
	SHELL_CMD_ARG(bias, NULL, "Set manual magnet bias: <x|y|z> <gauss>",
		      cmd_compass_set_bias, 3, 0),
	SHELL_CMD_ARG(scale, NULL, "Set manual magnet scale: <x|y|z> <ratio>",
		      cmd_compass_set_scale, 3, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(
	compass_stream_subcmds,
	SHELL_CMD_ARG(on, NULL, "Enable periodic logging: on <ms>", cmd_compass_stream_on, 2, 0),
	SHELL_CMD(off, NULL, "Disable periodic logging", cmd_compass_stream_off),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(
	compass_subcmds,
	SHELL_CMD(show, NULL, "Fetch and print a compass snapshot", cmd_compass_show),
	SHELL_CMD(dump, NULL, "Print shell commands that recreate current runtime tuning",
		  cmd_compass_dump),
	SHELL_CMD(stream, &compass_stream_subcmds, "Control periodic logging", NULL),
	SHELL_CMD(set, &compass_set_subcmds, "Set runtime compass attributes", NULL),
	SHELL_CMD_ARG(reset, NULL, "Reset calibration; optional 'all' clears manual overrides",
		      cmd_compass_reset, 1, 1),
	SHELL_CMD_ARG(clear, NULL, "Clear manual magnet override for axis: <x|y|z>",
		      cmd_compass_clear, 2, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(compass, &compass_subcmds, "Compass composite sample commands", NULL);

#endif /* CONFIG_SHELL */
