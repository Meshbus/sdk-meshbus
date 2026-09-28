/* Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MBS_FIRMWARE_PRIV_H_
#define MBS_FIRMWARE_PRIV_H_

#include <firmware/firmware.h>

struct mbs_firmware_journal {
	uint32_t magic;
	uint16_t format;
	uint16_t size;
	uint32_t generation;
	uint32_t crc32;
	uint8_t transfer_id[MBS_FIRMWARE_TRANSFER_ID_SIZE];
	uint8_t manifest_hash[MBS_FIRMWARE_HASH_SIZE];
	struct mbs_firmware_manifest manifest;
	uint64_t received_bitmap;
	uint32_t durable_received;
	int32_t detail;
	uint8_t state;
	uint8_t result;
	uint8_t reboot_intent;
	uint8_t update_kind;
	uint8_t reserved[4];
};

int mbs_firmware_manifest_admit(const uint8_t *encoded, size_t encoded_size,
				 const uint8_t signature[MBS_FIRMWARE_SIGNATURE_SIZE],
				 struct mbs_firmware_manifest *manifest,
				 uint8_t transfer_id[MBS_FIRMWARE_TRANSFER_ID_SIZE],
				 enum mbs_firmware_result *result);
int mbs_firmware_hash(const uint8_t *data, size_t size,
		       uint8_t hash[MBS_FIRMWARE_HASH_SIZE]);
int mbs_firmware_platform_ed25519_verify(
	const uint8_t public_key[32], const uint8_t signature[64],
	const uint8_t *message, size_t message_size);

int mbs_firmware_full_image_upload_admit(
	const uint8_t image_hash[MBS_FIRMWARE_HASH_SIZE], size_t offset);
int mbs_firmware_full_image_state_write_admit(void);
void mbs_firmware_full_image_upload_pending(void);
void mbs_firmware_full_image_upload_stopped(int detail);
void mbs_firmware_full_image_confirmed(void);

#endif
