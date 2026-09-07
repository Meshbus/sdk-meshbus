// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <zephyr/meshbus/llext.h>

#include "app_api.h"

static const struct meshbus_llext_app_metadata app_metadata
	MESHBUS_LLEXT_APP_METADATA_ATTR = {
		.magic = MESHBUS_LLEXT_APP_METADATA_MAGIC,
			.metadata_version = MESHBUS_LLEXT_APP_METADATA_VERSION,
			.size = sizeof(struct meshbus_llext_app_metadata),
			.stack_size = 1024U,
			.heap_size = 32768U,
			.icon_data_size = MESHBUS_LLEXT_APP_ICON_DATA_SIZE,
			.id = "mb_app",
			.name = "Meshbus App",
			.app_version = "1.0.0",
			.entry_point_symbol = "app_test_entry",
			.edk_version = "0.1.0",
			.target = CONFIG_BOARD_TARGET,
			.icon_data = {
				0x00, 0x03, 0x7c, 0x02, 0x44, 0x02, 0x44, 0x02, 0x7c, 0x02,
				0x44, 0x02, 0x44, 0x02, 0x44, 0x02, 0x7c, 0x02, 0x00, 0x00,
			},
		};

void app_test_entry(void *args)
{
	ARG_UNUSED(args);

	(void)mb_llext_test_hook(MB_LLEXT_TEST_EVT_APP);
}
EXPORT_SYMBOL(app_test_entry);
