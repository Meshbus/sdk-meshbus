// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <zephyr/meshbus/llext.h>

#include "service_api.h"

static const struct meshbus_llext_service_metadata service_metadata
	MESHBUS_LLEXT_SERVICE_METADATA_ATTR = {
		.magic = MESHBUS_LLEXT_SERVICE_METADATA_MAGIC,
		.metadata_version = MESHBUS_LLEXT_SERVICE_METADATA_VERSION,
		.size = sizeof(struct meshbus_llext_service_metadata),
		.thread_stack_size = 1024U,
		.heap_size = 16384U,
		.id = "mb_test",
		.name = "Meshbus LLEXT Test",
		.description = "Real load service for meshbus llext tests",
		.service_version = "1.0.0",
		.entry_point_symbol = "service_thread_entry",
		.edk_version = "0.1.0",
		.target = CONFIG_BOARD_TARGET,
	};

void service_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	(void)mb_llext_test_hook(MB_LLEXT_TEST_EVT_SETUP);

	while (true) {
		k_sleep(K_MSEC(20));
	}
}
EXPORT_SYMBOL(service_thread_entry);
