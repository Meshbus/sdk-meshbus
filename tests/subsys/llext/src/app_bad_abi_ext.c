/* SPDX-License-Identifier: Apache-2.0 */
#include <llext/metadata.h>
#include <zephyr/llext/symbol.h>
#include "app_api.h"

static const struct mbs_llext_app_metadata metadata MBS_LLEXT_APP_METADATA_ATTR = {
	.magic = MBS_LLEXT_APP_METADATA_MAGIC,
	.metadata_version = 2U,
	.size = sizeof(struct mbs_llext_app_metadata),
	.stack_size = 1024U,
	.heap_size = 16384U,
	.id = "mbs_badabi",
	.name = "Bad ABI constructor",
	.app_version = "1.0.0",
	.entry_point_symbol = "app_bad_abi_entry",
	.edk_version = "1.0.0",
	.compatibility_reserved = {0xff, 0xff, 0xff, 0x7f},
	.target = CONFIG_BOARD_TARGET,
};

static void __attribute__((constructor)) forbidden_constructor(void)
{
	mbs_llext_test_hook(MBS_LLEXT_TEST_EVT_APP);
}

void app_bad_abi_entry(void *args)
{
	(void)args;
	mbs_llext_test_hook(MBS_LLEXT_TEST_EVT_APP);
}
EXPORT_SYMBOL(app_bad_abi_entry);
