// SPDX-License-Identifier: Apache-2.0

#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <llext/metadata.h>

static const struct mbs_llext_app_metadata app_metadata
	MBS_LLEXT_APP_METADATA_ATTR = {
		.magic = MBS_LLEXT_APP_METADATA_MAGIC,
		.metadata_version = MBS_LLEXT_APP_METADATA_VERSION,
		.compatibility_reserved = MBS_LLEXT_INTERFACE_ABI_BYTES,
		.size = sizeof(struct mbs_llext_app_metadata),
		.stack_size = 1024U,
		.heap_size = CONFIG_MBS_LLEXT_APP_HEAP_RESERVE_SIZE + 1U,
		.icon_data_size = 0U,
		.id = "mbs_bigapp",
		.name = "Big App",
		.app_version = "1.0.0",
		.entry_point_symbol = "app_big_heap_entry",
		.edk_version = "0.1.0",
		.target = CONFIG_BOARD_TARGET,
	};

void app_big_heap_entry(void *args)
{
	ARG_UNUSED(args);
}
EXPORT_SYMBOL(app_big_heap_entry);
