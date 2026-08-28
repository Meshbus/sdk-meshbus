/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FOBE_SUBSYS_MESHBUS_SERVICES_TELEMETRY_DT_H_
#define FOBE_SUBSYS_MESHBUS_SERVICES_TELEMETRY_DT_H_

#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>

#define MB_TELEMETRY_DT_CHANNEL_IS_COMMON(channel_node)                              \
	(DT_PROP(channel_node, channel) >= 0 &&                                      \
	 DT_PROP(channel_node, channel) < SENSOR_CHAN_ALL)

#define MB_TELEMETRY_DT_SENSOR_OVERLAPS_COMPOSITE(sensor_node, compass_node)              \
	(COND_CODE_1(DT_NODE_HAS_COMPAT(compass_node, zephyr_compass_composite),            \
		     (DT_SAME_NODE(sensor_node, DT_PHANDLE(compass_node, accel_source)) ||  \
		      DT_SAME_NODE(sensor_node, DT_PHANDLE(compass_node, gyro_source)) ||   \
		      DT_SAME_NODE(sensor_node, DT_PHANDLE(compass_node, magn_source))),     \
		     (0)))

#define MB_TELEMETRY_DT_SENSOR_IS_COMPASS_OWNED(sensor_node, compass_node)                  \
	(DT_SAME_NODE(sensor_node, compass_node) ||                                         \
	 MB_TELEMETRY_DT_SENSOR_OVERLAPS_COMPOSITE(sensor_node, compass_node))

#endif /* FOBE_SUBSYS_MESHBUS_SERVICES_TELEMETRY_DT_H_ */
