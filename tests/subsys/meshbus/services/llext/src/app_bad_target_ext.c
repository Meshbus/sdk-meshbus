// SPDX-License-Identifier: Apache-2.0

#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <zephyr/meshbus/llext.h>

static const struct meshbus_llext_app_metadata app_metadata
	MESHBUS_LLEXT_APP_METADATA_ATTR = {
		.magic = MESHBUS_LLEXT_APP_METADATA_MAGIC,
		.metadata_version = MESHBUS_LLEXT_APP_METADATA_VERSION,
		.size = sizeof(struct meshbus_llext_app_metadata),
		.stack_size = 1024U,
		.heap_size = 16384U,
		.id = "mb_badtargetapp",
		.name = "Bad Target App",
		.app_version = "1.0.0",
		.entry_point_symbol = "app_bad_target_entry",
		.edk_version = "0.1.0",
		.target = "other/target",
	};

void app_bad_target_entry(void *args)
{
	ARG_UNUSED(args);
}
EXPORT_SYMBOL(app_bad_target_entry);
