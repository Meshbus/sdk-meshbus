/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bluetooth.h"

#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

extern atomic_t companion_bluetooth_drop_count;
extern atomic_t companion_bluetooth_rx_count;
extern atomic_t companion_bluetooth_tx_count;
extern struct k_work_q companion_bluetooth_workq;
extern bool companion_bluetooth_workq_started;

uint32_t meshbus_meshcore_test_companion_bluetooth_rx_count(void)
{
	return (uint32_t)atomic_get(&companion_bluetooth_rx_count);
}

uint32_t meshbus_meshcore_test_companion_bluetooth_tx_count(void)
{
	return (uint32_t)atomic_get(&companion_bluetooth_tx_count);
}

uint32_t meshbus_meshcore_test_companion_bluetooth_drop_count(void)
{
	return (uint32_t)atomic_get(&companion_bluetooth_drop_count);
}

int meshbus_meshcore_test_companion_bluetooth_drain(void)
{
	if (!companion_bluetooth_workq_started) {
		return 0;
	}

	return k_work_queue_drain(&companion_bluetooth_workq, false);
}
