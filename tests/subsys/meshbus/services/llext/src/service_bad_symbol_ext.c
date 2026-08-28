// SPDX-License-Identifier: Apache-2.0

#include <zephyr/kernel.h>
#include <zephyr/meshbus/llext.h>

static const struct meshbus_llext_service_metadata service_metadata
	MESHBUS_LLEXT_SERVICE_METADATA_ATTR = {
		.magic = MESHBUS_LLEXT_SERVICE_METADATA_MAGIC,
		.metadata_version = MESHBUS_LLEXT_SERVICE_METADATA_VERSION,
		.size = sizeof(struct meshbus_llext_service_metadata),
		.thread_stack_size = 1024U,
		.heap_size = 16384U,
		.id = "mb_badsym",
		.name = "Bad Symbol",
		.service_version = "1.0.0",
		.entry_point_symbol = "missing_service_entry",
		.edk_version = "0.1.0",
		.target = CONFIG_BOARD_TARGET,
	};
