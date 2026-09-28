/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/init.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

#define SETTINGS DT_NODELABEL(storage_partition)
#define FILESYSTEM DT_NODELABEL(extra_partition)
#define PRODUCT DT_NODELABEL(product_storage_partition)
#define END(node) (DT_REG_ADDR(node) + DT_REG_SIZE(node))

BUILD_ASSERT(!IS_ENABLED(CONFIG_FLASH_SIMULATOR), "real RRAM is required");
BUILD_ASSERT(DT_REG_ADDR(SETTINGS) == 0x164000 && DT_REG_SIZE(SETTINGS) == 0x3000);
BUILD_ASSERT(DT_REG_ADDR(FILESYSTEM) == 0x167000 && DT_REG_SIZE(FILESYSTEM) == 0xd000);
BUILD_ASSERT(DT_REG_ADDR(PRODUCT) == 0x174000 && DT_REG_SIZE(PRODUCT) == 0x9000);
BUILD_ASSERT(END(DT_NODELABEL(slot0_partition)) <= DT_REG_ADDR(DT_NODELABEL(slot1_partition)));
BUILD_ASSERT(END(DT_NODELABEL(slot1_partition)) <= DT_REG_ADDR(SETTINGS));
BUILD_ASSERT(END(SETTINGS) <= DT_REG_ADDR(FILESYSTEM));
BUILD_ASSERT(END(FILESYSTEM) <= DT_REG_ADDR(PRODUCT));
BUILD_ASSERT(END(PRODUCT) <= DT_REG_SIZE(DT_MEM_FROM_PARTITION(PRODUCT)));

int mba_test_storage_prepare_rc;

static int flatten(int id)
{
	const struct flash_area *area;
	int ret = flash_area_open(id, &area);

	if (ret == 0) {
		ret = flash_area_flatten(area, 0, area->fa_size);
		flash_area_close(area);
	}
	return ret;
}

int mba_test_storage_cleanup(void)
{
	int ret = flatten(DT_PARTITION_ID(FILESYSTEM));

	if (ret == 0) {
		ret = flatten(DT_PARTITION_ID(SETTINGS));
	}
	return ret;
}

static int prepare(void)
{
	mba_test_storage_prepare_rc = mba_test_storage_cleanup();
	return mba_test_storage_prepare_rc;
}
SYS_INIT(prepare, POST_KERNEL, 98);
