// SPDX-License-Identifier: Apache-2.0

#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <zephyr/meshbus/llext.h>

static const struct meshbus_llext_service_metadata service_metadata
	MESHBUS_LLEXT_SERVICE_METADATA_ATTR = {
		.magic = MESHBUS_LLEXT_SERVICE_METADATA_MAGIC,
		.metadata_version = MESHBUS_LLEXT_SERVICE_METADATA_VERSION + 1U,
		.size = sizeof(struct meshbus_llext_service_metadata),
		.thread_stack_size = 1024U,
		.heap_size = 16384U,
		.id = "mb_oldmeta",
		.name = "Old Metadata Service",
		.service_version = "1.0.0",
		.entry_point_symbol = "service_old_metadata_entry",
		.edk_version = "0.1.0",
		.target = CONFIG_BOARD_TARGET,
	};

void service_old_metadata_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
}
EXPORT_SYMBOL(service_old_metadata_entry);
