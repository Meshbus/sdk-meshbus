/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WEATHER_BLE_H_
#define WEATHER_BLE_H_
#include "readings.h"
int weather_ble_start(void);
void weather_ble_publish(const struct readings *value);
#endif
