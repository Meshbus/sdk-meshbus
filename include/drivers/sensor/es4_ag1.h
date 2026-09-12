#ifndef MESHBUS_INCLUDE_DRIVERS_SENSOR_ES4_AG1_H_
#define MESHBUS_INCLUDE_DRIVERS_SENSOR_ES4_AG1_H_

#include <zephyr/drivers/sensor.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Zero-offset calibration attribute in microvolts. */
#define SENSOR_ATTR_ES4_AG1_ZERO_OFFSET SENSOR_ATTR_PRIV_START

/** TIA transimpedance calibration attribute in ohms. */
#define SENSOR_ATTR_ES4_AG1_TRANSIMPEDANCE (SENSOR_ATTR_PRIV_START + 1)

/** Sensor sensitivity calibration attribute in nA/ppm. */
#define SENSOR_ATTR_ES4_AG1_SENSITIVITY (SENSOR_ATTR_PRIV_START + 2)

#ifdef __cplusplus
}
#endif

#endif /* MESHBUS_INCLUDE_DRIVERS_SENSOR_ES4_AG1_H_ */
