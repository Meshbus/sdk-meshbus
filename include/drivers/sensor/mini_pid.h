#ifndef MESHBUS_INCLUDE_DRIVERS_SENSOR_MINI_PID_H_
#define MESHBUS_INCLUDE_DRIVERS_SENSOR_MINI_PID_H_

#include <zephyr/drivers/sensor.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Baseline voltage calibration attribute (sensor_value in microvolts). */
#define SENSOR_ATTR_MINI_PID_BASELINE SENSOR_ATTR_PRIV_START

/** Sensitivity scaling attribute (sensor_value in microvolts/ppm). */
#define SENSOR_ATTR_MINI_PID_SENSITIVITY (SENSOR_ATTR_PRIV_START + 1)

#ifdef __cplusplus
}
#endif

#endif /* MESHBUS_INCLUDE_DRIVERS_SENSOR_MINI_PID_H_ */
