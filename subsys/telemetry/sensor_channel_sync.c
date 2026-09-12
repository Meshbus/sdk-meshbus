/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Devicetree cannot consume C enum constants, and protobuf clients need the
 * same numeric channel IDs. Snapshot Zephyr's enum before including the DTS
 * mirror, then fail every Telemetry build if any of the three contracts drift.
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/util.h>

#include "meshbus/telemetry.pb.h"

#define MBS_COMMON_SENSOR_CHANNELS(X)                                                        \
	X(ACCEL_X)                                                                                \
	X(ACCEL_Y)                                                                                \
	X(ACCEL_Z)                                                                                \
	X(ACCEL_XYZ)                                                                              \
	X(GYRO_X)                                                                                 \
	X(GYRO_Y)                                                                                 \
	X(GYRO_Z)                                                                                 \
	X(GYRO_XYZ)                                                                               \
	X(MAGN_X)                                                                                 \
	X(MAGN_Y)                                                                                 \
	X(MAGN_Z)                                                                                 \
	X(MAGN_XYZ)                                                                               \
	X(DIE_TEMP)                                                                               \
	X(AMBIENT_TEMP)                                                                           \
	X(PRESS)                                                                                  \
	X(PROX)                                                                                   \
	X(HUMIDITY)                                                                               \
	X(AMBIENT_LIGHT)                                                                          \
	X(LIGHT)                                                                                  \
	X(IR)                                                                                     \
	X(RED)                                                                                    \
	X(GREEN)                                                                                  \
	X(BLUE)                                                                                   \
	X(ALTITUDE)                                                                               \
	X(DISTANCE)                                                                               \
	X(CO2)                                                                                    \
	X(O2)                                                                                     \
	X(VOC)                                                                                    \
	X(GAS_RES)                                                                                \
	X(FLOW_RATE)                                                                              \
	X(VOLUME)                                                                                 \
	X(VOLTAGE)                                                                                \
	X(VSHUNT)                                                                                 \
	X(CURRENT)                                                                                \
	X(POWER)                                                                                  \
	X(RESISTANCE)                                                                             \
	X(ROTATION)                                                                               \
	X(POS_DX)                                                                                 \
	X(POS_DY)                                                                                 \
	X(POS_DZ)                                                                                 \
	X(POS_DXYZ)                                                                               \
	X(RPM)                                                                                    \
	X(FREQUENCY)                                                                              \
	X(GAUGE_VOLTAGE)                                                                          \
	X(GAUGE_AVG_CURRENT)                                                                      \
	X(GAUGE_STDBY_CURRENT)                                                                    \
	X(GAUGE_MAX_LOAD_CURRENT)                                                                 \
	X(GAUGE_TEMP)                                                                             \
	X(GAUGE_STATE_OF_CHARGE)                                                                  \
	X(GAUGE_FULL_CHARGE_CAPACITY)                                                             \
	X(GAUGE_REMAINING_CHARGE_CAPACITY)                                                        \
	X(GAUGE_NOM_AVAIL_CAPACITY)                                                               \
	X(GAUGE_FULL_AVAIL_CAPACITY)                                                              \
	X(GAUGE_AVG_POWER)                                                                        \
	X(GAUGE_STATE_OF_HEALTH)                                                                  \
	X(GAUGE_TIME_TO_EMPTY)                                                                    \
	X(GAUGE_TIME_TO_FULL)                                                                     \
	X(GAUGE_CYCLE_COUNT)                                                                      \
	X(GAUGE_DESIGN_VOLTAGE)                                                                   \
	X(GAUGE_DESIRED_VOLTAGE)                                                                  \
	X(GAUGE_DESIRED_CHARGING_CURRENT)                                                         \
	X(GAME_ROTATION_VECTOR)                                                                   \
	X(GRAVITY_VECTOR)                                                                         \
	X(GBIAS_XYZ)                                                                              \
	X(ENCODER_COUNT)                                                                          \
	X(ENCODER_REVOLUTIONS)

/* Protobuf identifiers spell particulate sizes out to avoid protoc's enum-name
 * normalization collisions (for example PM_1_0 and PM_10).
 */
#define MBS_PM_SENSOR_CHANNELS(X)                                                          \
	X(PM_1_0_CF, PM_ONE_CF)                                                                 \
	X(PM_2_5_CF, PM_TWO_POINT_FIVE_CF)                                                     \
	X(PM_10_CF, PM_TEN_CF)                                                                 \
	X(PM_1_0, PM_ONE)                                                                       \
	X(PM_2_5, PM_TWO_POINT_FIVE)                                                           \
	X(PM_10, PM_TEN)                                                                       \
	X(PM_0_3_COUNT, PM_ZERO_POINT_THREE_COUNT)                                             \
	X(PM_0_5_COUNT, PM_ZERO_POINT_FIVE_COUNT)                                              \
	X(PM_1_0_COUNT, PM_ONE_COUNT)                                                         \
	X(PM_2_5_COUNT, PM_TWO_POINT_FIVE_COUNT)                                              \
	X(PM_5_COUNT, PM_FIVE_COUNT)                                                         \
	X(PM_10_COUNT, PM_TEN_COUNT)

#define MBS_SENSOR_CHANNEL_SNAPSHOT(name) MBS_SENSOR_CHAN_C_##name = SENSOR_CHAN_##name,
#define MBS_PM_SENSOR_CHANNEL_SNAPSHOT(name, proto_name)                                    \
	MBS_SENSOR_CHAN_C_##name = SENSOR_CHAN_##name,
enum mbs_sensor_channel_snapshot {
	MBS_COMMON_SENSOR_CHANNELS(MBS_SENSOR_CHANNEL_SNAPSHOT)
	MBS_PM_SENSOR_CHANNELS(MBS_PM_SENSOR_CHANNEL_SNAPSHOT)
	MBS_SENSOR_CHAN_C_ALL = SENSOR_CHAN_ALL,
	MBS_SENSOR_CHAN_C_COMMON_COUNT = SENSOR_CHAN_COMMON_COUNT,
	MBS_SENSOR_CHAN_C_PRIV_START = SENSOR_CHAN_PRIV_START,
	MBS_SENSOR_CHAN_C_MAX = SENSOR_CHAN_MAX,
};
#undef MBS_SENSOR_CHANNEL_SNAPSHOT
#undef MBS_PM_SENSOR_CHANNEL_SNAPSHOT

#include <zephyr/dt-bindings/sensor/sensor_channel.h>

#define MBS_SENSOR_CHANNEL_ASSERT(name)                                                      \
	BUILD_ASSERT(SENSOR_CHAN_##name == MBS_SENSOR_CHAN_C_##name,                               \
		     "DTS sensor channel " #name " drifted from Zephyr");                         \
	BUILD_ASSERT((int)meshbus_TelemetryChannelId_TELEMETRY_CHANNEL_ID_##name ==               \
			     (int)MBS_SENSOR_CHAN_C_##name,                                           \
		     "Telemetry protobuf channel " #name " drifted from Zephyr");
MBS_COMMON_SENSOR_CHANNELS(MBS_SENSOR_CHANNEL_ASSERT)
#undef MBS_SENSOR_CHANNEL_ASSERT

#define MBS_PM_SENSOR_CHANNEL_ASSERT(name, proto_name)                                      \
	BUILD_ASSERT(SENSOR_CHAN_##name == MBS_SENSOR_CHAN_C_##name,                               \
		     "DTS sensor channel " #name " drifted from Zephyr");                         \
	BUILD_ASSERT((int)meshbus_TelemetryChannelId_TELEMETRY_CHANNEL_ID_##proto_name ==         \
			     (int)MBS_SENSOR_CHAN_C_##name,                                           \
		     "Telemetry protobuf channel " #name " drifted from Zephyr");
MBS_PM_SENSOR_CHANNELS(MBS_PM_SENSOR_CHANNEL_ASSERT)
#undef MBS_PM_SENSOR_CHANNEL_ASSERT

BUILD_ASSERT(SENSOR_CHAN_ALL == MBS_SENSOR_CHAN_C_ALL,
	     "DTS SENSOR_CHAN_ALL drifted from Zephyr");
BUILD_ASSERT(SENSOR_CHAN_COMMON_COUNT == MBS_SENSOR_CHAN_C_COMMON_COUNT,
	     "DTS SENSOR_CHAN_COMMON_COUNT drifted from Zephyr");
BUILD_ASSERT(SENSOR_CHAN_PRIV_START == MBS_SENSOR_CHAN_C_PRIV_START,
	     "DTS SENSOR_CHAN_PRIV_START drifted from Zephyr");
BUILD_ASSERT(SENSOR_CHAN_MAX == MBS_SENSOR_CHAN_C_MAX,
	     "DTS SENSOR_CHAN_MAX drifted from Zephyr");
