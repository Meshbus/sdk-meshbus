// SPDX-License-Identifier: Apache-2.0

#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <llext/metadata.h>

static const struct mbs_llext_app_metadata app_metadata
	MBS_LLEXT_APP_METADATA_ATTR = {
		.magic = MBS_LLEXT_APP_METADATA_MAGIC,
		.metadata_version = MBS_LLEXT_APP_METADATA_VERSION,
		.size = sizeof(struct mbs_llext_app_metadata),
		.stack_size = 1024U,
		.heap_size = 16384U,
		.id = "mbs_otheredk",
		.name = "Different EDK",
		.app_version = "1.0.0",
		.entry_point_symbol = "app_other_edk_entry",
		.edk_version = "9.0.0",
		.target = CONFIG_BOARD_TARGET,
	};

void app_other_edk_entry(void *args)
{
	ARG_UNUSED(args);
}
EXPORT_SYMBOL(app_other_edk_entry);
