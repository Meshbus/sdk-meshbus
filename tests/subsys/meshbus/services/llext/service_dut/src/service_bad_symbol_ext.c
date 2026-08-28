// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <zephyr/meshbus/llext.h>

static const struct meshbus_llext_service_metadata service_metadata
	MESHBUS_LLEXT_SERVICE_METADATA_ATTR = {
		.magic = MESHBUS_LLEXT_SERVICE_METADATA_MAGIC,
		.metadata_version = MESHBUS_LLEXT_SERVICE_METADATA_VERSION,
		.size = sizeof(struct meshbus_llext_service_metadata),
		.thread_stack_size = 1024U,
		.heap_size = 32768U,
		.id = "mb_badsym",
		.name = "Missing Symbol C2",
		.description = "C2 missing entry recovery",
		.service_version = "1.0.0",
		.entry_point_symbol = "missing_service_entry",
		.edk_version = "0.1.0",
		.target = CONFIG_BOARD_TARGET,
};

void present_service_symbol(void)
{
}
EXPORT_SYMBOL(present_service_symbol);
