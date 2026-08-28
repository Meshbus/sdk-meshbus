/* SPDX-License-Identifier: Apache-2.0 */
#ifndef FOBE_SUBSYS_U8G2_DUMP_H_
#define FOBE_SUBSYS_U8G2_DUMP_H_

#include <stdint.h>
#include <stddef.h>

struct u8g2_dump_info {
	const uint8_t *buf;
	size_t len;
	uint16_t width;
	uint16_t height;
	uint32_t orientation;
};

int u8g2_display_get_dump_info(struct u8g2_dump_info *info);

#endif /* FOBE_SUBSYS_U8G2_DUMP_H_ */
