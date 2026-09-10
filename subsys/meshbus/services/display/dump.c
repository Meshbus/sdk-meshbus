/* Copyright (c) 2026 FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <zephyr/display/u8g2_snapshot.h>
#include <zephyr/kernel.h>
#include <zephyr/meshbus/display.h>

#ifdef CONFIG_U8G2_SNAPSHOT
static K_MUTEX_DEFINE(dump_mutex);
static uint8_t dump_data[CONFIG_MESHBUS_DISPLAY_DUMP_MAX_SIZE];
static struct u8g2_snapshot_info dump_info;
static uint32_t dump_id;
#endif

int meshbus_display_dump_read(uint32_t snapshot_id, uint32_t offset, uint32_t length,
			      struct meshbus_display_dump_chunk *chunk)
{
	if (chunk == NULL || length > MESHBUS_DISPLAY_DUMP_CHUNK_SIZE ||
	    (snapshot_id == 0U && offset != 0U)) {
		return -EINVAL;
	}
#ifndef CONFIG_U8G2_SNAPSHOT
	return -ENOTSUP;
#else
	int rc = 0;

	k_mutex_lock(&dump_mutex, K_FOREVER);
	if (snapshot_id == 0U) {
		/* Memory copy only; never call panel I/O while holding this lock. */
		rc = u8g2_snapshot_copy(dump_data, sizeof(dump_data), &dump_info);
		if (rc != 0) {
			goto out;
		}
		if (++dump_id == 0U) {
			++dump_id;
		}
	} else if (snapshot_id != dump_id) {
		rc = -ENOENT;
		goto out;
	}
	if (offset >= dump_info.len) {
		rc = -EINVAL;
		goto out;
	}
	length = length == 0U ? MESHBUS_DISPLAY_DUMP_CHUNK_SIZE : length;
	*chunk = (struct meshbus_display_dump_chunk){
		.snapshot_id = dump_id,
		.offset = offset,
		.total_size = dump_info.len,
		.width = dump_info.width,
		.height = dump_info.height,
		.format = meshbus_DisplayDumpFormat_DISPLAY_DUMP_FORMAT_SSD1306_PAGE,
		.orientation = (meshbus_DisplayDumpOrientation)dump_info.orientation,
		.inverted = dump_info.inverted,
		.data_len = MIN(length, dump_info.len - offset),
	};
	memcpy(chunk->data, &dump_data[offset], chunk->data_len);
out:
	k_mutex_unlock(&dump_mutex);
	return rc;
#endif
}
