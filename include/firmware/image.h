/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MESHBUS_INCLUDE_FIRMWARE_IMAGE_H_
#define MESHBUS_INCLUDE_FIRMWARE_IMAGE_H_
#include <stddef.h>
#include <stdint.h>
#define MBS_FIRMWARE_IMAGE_HASH_SIZE 32U
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Hash the installed primary MCUboot image, excluding slot padding.
 *
 * The identity includes header, payload and both TLV areas; no signature policy
 * is changed. This blocking read does not mutate flash or update state.
 * @param hash Caller-owned MBS_FIRMWARE_IMAGE_HASH_SIZE byte output, valid on success.
 * @param image_size Caller-owned output for the number of hashed image bytes.
 * @retval 0 Identity was read successfully.
 * @return Negative errno for invalid arguments, malformed image or flash failure.
 */
int mbs_firmware_image_identity(uint8_t hash[MBS_FIRMWARE_IMAGE_HASH_SIZE], size_t *image_size);

#ifdef __cplusplus
}
#endif
#endif /* MESHBUS_INCLUDE_FIRMWARE_IMAGE_H_ */
