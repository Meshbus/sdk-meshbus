/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/llext/symbol.h>
#include <zephyr/meshbus/llext.h>

extern void mba_test_run(void *args);

static const struct meshbus_llext_app_metadata metadata
	MESHBUS_LLEXT_APP_METADATA_ATTR = {
	.magic = MESHBUS_LLEXT_APP_METADATA_MAGIC,
	.metadata_version = MESHBUS_LLEXT_APP_METADATA_VERSION,
	.size = sizeof(struct meshbus_llext_app_metadata),
	.stack_size = 1024U,
	.heap_size = 32768U,
	.id = "mba-lifecycle",
	.name = "MBA lifecycle",
	.app_version = "1.0.0",
#if defined(TEST_BAD_ENTRY)
	.entry_point_symbol = "missing_entry",
#else
	.entry_point_symbol = "mba_test_entry",
#endif
	.edk_version = "0.1.0",
	.target = CONFIG_BOARD_TARGET,
};

void mba_test_entry(void *args)
{
	mba_test_run(args);
}
EXPORT_SYMBOL(mba_test_entry);
