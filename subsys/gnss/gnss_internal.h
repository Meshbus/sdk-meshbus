/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MBS_SERVICES_GNSS_INTERNAL_H_
#define MBS_SERVICES_GNSS_INTERNAL_H_

#include <gnss/gnss.h>

/*
 * Firmware-internal full protobuf setter.  Unlike the exported ABI-v1 setter,
 * this entry may consume fields added after the original mbs_gnss_config
 * layout, including electronic_compass.
 */
int mbs_gnss_config_set_full(const mbs_gnss_config *cfg);

#endif /* MBS_SERVICES_GNSS_INTERNAL_H_ */
