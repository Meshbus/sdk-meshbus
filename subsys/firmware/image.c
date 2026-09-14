/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <psa/crypto.h>
#include <firmware/image.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include "image_private.h"

#define MCUBOOT_IMAGE_MAGIC 0x96f3b83dU
#define MCUBOOT_HEADER_SIZE 32U
#define MCUBOOT_TLV_INFO_MAGIC 0x6907U

int mbs_firmware_flash_hash(const struct flash_area *area, size_t size,
		      uint8_t hash[MBS_FIRMWARE_IMAGE_HASH_SIZE])
{
	psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
	uint8_t buf[512];
	size_t output_size = 0U;
	psa_status_t status = psa_hash_setup(&op, PSA_ALG_SHA_256);

	for (size_t offset = 0U; status == PSA_SUCCESS && offset < size;) {
		size_t chunk = MIN(sizeof(buf), size - offset);

		if (flash_area_read(area, offset, buf, chunk) != 0) {
			status = PSA_ERROR_STORAGE_FAILURE;
			break;
		}
		status = psa_hash_update(&op, buf, chunk);
		offset += chunk;
	}
	if (status == PSA_SUCCESS) {
		status = psa_hash_finish(&op, hash, MBS_FIRMWARE_IMAGE_HASH_SIZE,
					 &output_size);
	}
	psa_hash_abort(&op);
	return status == PSA_SUCCESS && output_size == MBS_FIRMWARE_IMAGE_HASH_SIZE
		       ? 0 : -EIO;
}

int mbs_firmware_image_identity(uint8_t hash[MBS_FIRMWARE_IMAGE_HASH_SIZE], size_t *size)
{
	const struct flash_area *area;
	uint8_t header[MCUBOOT_HEADER_SIZE] = {0};
	uint8_t info[4] = {0};
	uint16_t header_size;
	uint16_t protected_size;
	uint16_t tlv_size;
	uint32_t image_size;
	size_t tlv_offset;
	size_t signed_size;
	int rc;

	if (hash == NULL || size == NULL) {
		return -EINVAL;
	}
	rc = flash_area_open(PARTITION_ID(slot0_partition), &area);

	if (rc != 0) {
		return rc;
	}
	rc = flash_area_read(area, 0, header, sizeof(header));
	header_size = sys_get_le16(&header[8]);
	protected_size = sys_get_le16(&header[10]);
	image_size = sys_get_le32(&header[12]);
	if (rc == 0 && (sys_get_le32(header) != MCUBOOT_IMAGE_MAGIC ||
			 header_size < MCUBOOT_HEADER_SIZE)) {
		rc = -EINVAL;
	}
	if (rc == 0 && ((size_t)header_size > area->fa_size ||
			 image_size > area->fa_size - header_size ||
			 protected_size > area->fa_size - header_size - image_size)) {
		rc = -EINVAL;
	}
	tlv_offset = (size_t)header_size + image_size + protected_size;
	if (rc == 0 && (tlv_offset > area->fa_size ||
			 sizeof(info) > area->fa_size - tlv_offset)) {
		rc = -ENOSPC;
	}
	if (rc == 0) {
		rc = flash_area_read(area, tlv_offset, info, sizeof(info));
	}
	tlv_size = sys_get_le16(&info[2]);
	if (rc == 0 && (sys_get_le16(info) != MCUBOOT_TLV_INFO_MAGIC ||
			 tlv_size < sizeof(info) ||
			 tlv_size > area->fa_size - tlv_offset)) {
		rc = -EINVAL;
	}
	if (rc == 0) {
		signed_size = tlv_offset + tlv_size;
		rc = mbs_firmware_flash_hash(area, signed_size, hash);
	}
	flash_area_close(area);
	if (rc == 0) {
		*size = signed_size;
	}
	return rc;
}
