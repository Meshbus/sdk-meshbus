/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_INCLUDE_DISPLAY_U8G2_SNAPSHOT_H_
#define ZEPHYR_INCLUDE_DISPLAY_U8G2_SNAPSHOT_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Metadata for a complete, submitted software frame, not panel readback. */
struct u8g2_snapshot_info {
	size_t len;
	uint16_t width;
	uint16_t height;
	/** 0=R0, 1=R2, 2=R3, 3=R1; pixels already use physical coordinates. */
	uint8_t orientation;
	/** Output inversion at submission; bytes retain raw MONO01 polarity. */
	bool inverted;
};

/**
 * Copy the last complete full-buffer refresh in SSD1306 page format.
 * Byte (y / 8) * width + x contains bit (y % 8), with 1 meaning lit before
 * inversion. Rendering may continue while the caller owns its independent copy.
 * With dst=NULL and capacity=0, query metadata only. Otherwise capacity must
 * hold info->len bytes. Metadata and pixels are captured together under a lock.
 * Returns -ENODEV without a frame, -ENOSPC for insufficient capacity, -EINVAL
 * for invalid arguments, or -ENOTSUP when snapshot support is disabled.
 * May block; call from thread context. A failed call does not modify dst.
 */
#ifdef CONFIG_U8G2_SNAPSHOT
int u8g2_snapshot_copy(uint8_t *dst, size_t capacity, struct u8g2_snapshot_info *info);
#else
static inline int u8g2_snapshot_copy(uint8_t *dst, size_t capacity,
				     struct u8g2_snapshot_info *info)
{
	(void)dst;
	(void)capacity;
	(void)info;
	return -ENOTSUP;
}
#endif

#ifdef __cplusplus
}
#endif
#endif
