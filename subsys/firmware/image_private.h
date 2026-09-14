/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MBS_FIRMWARE_IMAGE_PRIVATE_H_
#define MBS_FIRMWARE_IMAGE_PRIVATE_H_
#include <firmware/image.h>
struct flash_area;
int mbs_firmware_flash_hash(const struct flash_area *area, size_t size,
                           uint8_t hash[MBS_FIRMWARE_IMAGE_HASH_SIZE]);
#endif
