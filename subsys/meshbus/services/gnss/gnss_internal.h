/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MESHBUS_SERVICES_GNSS_INTERNAL_H_
#define MESHBUS_SERVICES_GNSS_INTERNAL_H_

#include <zephyr/meshbus/gnss.h>

/*
 * Firmware-internal full protobuf setter.  Unlike the exported ABI-v1 setter,
 * this entry may consume fields added after the original meshbus_gnss_config
 * layout, including electronic_compass.
 */
int meshbus_gnss_config_set_full(const meshbus_gnss_config *cfg);

#endif /* MESHBUS_SERVICES_GNSS_INTERNAL_H_ */
