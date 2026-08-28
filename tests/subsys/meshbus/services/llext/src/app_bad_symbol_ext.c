// SPDX-License-Identifier: Apache-2.0

#include <zephyr/meshbus/llext.h>
#include <zephyr/llext/symbol.h>

static const struct meshbus_llext_app_metadata app_metadata
	MESHBUS_LLEXT_APP_METADATA_ATTR = {
		.magic = MESHBUS_LLEXT_APP_METADATA_MAGIC,
		.metadata_version = MESHBUS_LLEXT_APP_METADATA_VERSION,
		.size = sizeof(struct meshbus_llext_app_metadata),
		.stack_size = 1024U,
		.heap_size = 32768U,
		.icon_data_size = 0U,
		.id = "mb_badsymapp",
		.name = "Missing Symbol App",
		.app_version = "1.0.0",
		.entry_point_symbol = "missing_app_entry",
		.edk_version = "0.1.0",
		.target = CONFIG_BOARD_TARGET,
	};

void present_app_symbol(void)
{
}
EXPORT_SYMBOL(present_app_symbol);
