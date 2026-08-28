/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FOBE_SUBSYS_MESHBUS_SERVICES_MESHCORE_COMPANION_BLUETOOTH_H_
#define FOBE_SUBSYS_MESHBUS_SERVICES_MESHCORE_COMPANION_BLUETOOTH_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_ZTEST)
uint32_t meshbus_meshcore_test_companion_bluetooth_rx_count(void);
uint32_t meshbus_meshcore_test_companion_bluetooth_tx_count(void);
uint32_t meshbus_meshcore_test_companion_bluetooth_drop_count(void);
int meshbus_meshcore_test_companion_bluetooth_drain(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* FOBE_SUBSYS_MESHBUS_SERVICES_MESHCORE_COMPANION_BLUETOOTH_H_ */
