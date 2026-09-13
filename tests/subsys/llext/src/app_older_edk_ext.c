// SPDX-License-Identifier: Apache-2.0

#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <llext/metadata.h>

static const struct mbs_llext_app_metadata app_metadata
	MBS_LLEXT_APP_METADATA_ATTR = {
		.magic = MBS_LLEXT_APP_METADATA_MAGIC,
		.metadata_version = 1U,
		.size = sizeof(struct mbs_llext_app_metadata),
		.stack_size = 1024U,
		.heap_size = 32768U,
		.id = "mbs_olderedk",
		.name = "Older EDK",
		.app_version = "1.0.0",
		.entry_point_symbol = "app_older_edk_entry",
		.edk_version = "0.0.1",
		.target = CONFIG_BOARD_TARGET,
	};

void app_older_edk_entry(void *args)
{
	ARG_UNUSED(args);
}
EXPORT_SYMBOL(app_older_edk_entry);
