/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <llext/metadata.h>

#include "app_api.h"

static const struct mbs_llext_app_metadata app_metadata
	MBS_LLEXT_APP_METADATA_ATTR = {
		.magic = MBS_LLEXT_APP_METADATA_MAGIC,
		.metadata_version = MBS_LLEXT_APP_METADATA_VERSION + 1U,
		.size = sizeof(struct mbs_llext_app_metadata),
		.stack_size = 1024U,
		.heap_size = 16384U,
		.id = "mbs_unsupportedmeta",
		.name = "Unsupported Metadata App",
		.app_version = "1.0.0",
		.entry_point_symbol = "app_unsupported_metadata_entry",
		.edk_version = "0.1.0",
		.target = CONFIG_BOARD_TARGET,
	};

static void __attribute__((constructor)) forbidden_constructor(void)
{
	mbs_llext_test_hook(MBS_LLEXT_TEST_EVT_APP);
}

void app_unsupported_metadata_entry(void *args)
{
	ARG_UNUSED(args);
	mbs_llext_test_hook(MBS_LLEXT_TEST_EVT_APP);
}
EXPORT_SYMBOL(app_unsupported_metadata_entry);
