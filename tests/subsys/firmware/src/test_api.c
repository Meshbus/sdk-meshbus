/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <psa/crypto.h>
#include <meshbus/firmware.pb.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <firmware/firmware.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#define MCUBOOT_IMAGE_MAGIC 0x96f3b83dU
#define MCUBOOT_TLV_INFO_MAGIC 0x6907U
#define MCUBOOT_HEADER_SIZE 32U

static off_t journal_start;
static off_t journal_end;

static int journal_write_fail_cb(const struct device *dev, off_t offset,
				 uint8_t data)
{
	ARG_UNUSED(dev);

	return offset >= journal_start && offset < journal_end ? -EIO : data;
}

static const struct flash_simulator_cb journal_write_fail_callbacks = {
	.write_byte = journal_write_fail_cb,
};

static const uint8_t valid_manifest[] = {
	0xb4, 0x01, 0x01, 0x02, 0x50, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
	0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x03, 0x01, 0x04,
	0x6b, 0x75, 0x6e, 0x73, 0x75, 0x70, 0x70, 0x6f, 0x72, 0x74, 0x65, 0x64,
	0x05,
	0x68, 0x6e, 0x72, 0x66, 0x35, 0x34, 0x6c, 0x31, 0x35, 0x06, 0x01, 0x07,
	0x01, 0x08, 0x01, 0x09, 0x84, 0x01, 0x02, 0x03, 0x00, 0x0a, 0x84, 0x01,
	0x02, 0x04, 0x00, 0x0b, 0x58, 0x20, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
	0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11,
	0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
	0x1e, 0x1f, 0x0c, 0x58, 0x20, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
	0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32,
	0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e,
	0x3f, 0x0d, 0x58, 0x20, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53,
	0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
	0x0e, 0x19, 0x03, 0xe8, 0x0f, 0x19, 0x01, 0xc0, 0x10, 0x01, 0x11, 0x01,
	0x12, 0x01, 0x13, 0xf4, 0x14, 0x01,
};

ZTEST(mbs_firmware_contract, test_mcumgr_group_is_transport_independent)
{
	const struct mgmt_group *firmware_group = mgmt_find_group(
		meshbus_FirmwareMgmtGroupId_FIRMWARE_MGMT_GROUP_ID_MESHBUS_FIRMWARE);
	const struct mgmt_group *image_group = mgmt_find_group(MGMT_GROUP_ID_IMAGE);

	zassert_not_null(firmware_group);
	zassert_true(IS_ENABLED(CONFIG_MBS_FIRMWARE_MCUMGR_IMAGE));
	zassert_not_null(image_group);
}

int crypto_ed25519_check(const uint8_t signature[64],
			 const uint8_t public_key[32],
			 const uint8_t *message, size_t message_size)
{
	zassert_not_null(signature);
	zassert_not_null(public_key);
	zassert_not_null(message);
	zassert_true(message_size > 0U);
	return 0;
}

static size_t manifest_source_hash_offset(const uint8_t *manifest, size_t size)
{
	static const uint8_t marker[] = {0x0b, 0x58, 0x20};

	for (size_t i = 0U; i + sizeof(marker) + MBS_FIRMWARE_HASH_SIZE <= size;
	     i++) {
		if (memcmp(&manifest[i], marker, sizeof(marker)) == 0) {
			return i + sizeof(marker);
		}
	}
	return SIZE_MAX;
}

static void source_image_prepare(uint8_t hash[MBS_FIRMWARE_HASH_SIZE])
{
	const struct flash_area *area;
	uint8_t image[MCUBOOT_HEADER_SIZE + 4U] = {0};
	size_t hash_size = 0U;

	sys_put_le32(MCUBOOT_IMAGE_MAGIC, &image[0]);
	sys_put_le16(MCUBOOT_HEADER_SIZE, &image[8]);
	sys_put_le16(0U, &image[10]);
	sys_put_le32(0U, &image[12]);
	sys_put_le16(MCUBOOT_TLV_INFO_MAGIC, &image[MCUBOOT_HEADER_SIZE]);
	sys_put_le16(4U, &image[MCUBOOT_HEADER_SIZE + 2U]);

	zassert_ok(flash_area_open(PARTITION_ID(slot0_partition), &area));
	zassert_ok(flash_area_flatten(area, 0U, area->fa_size));
	zassert_ok(flash_area_write(area, 0U, image, sizeof(image)));
	flash_area_close(area);
	zassert_equal(psa_hash_compute(PSA_ALG_SHA_256, image, sizeof(image), hash,
				       MBS_FIRMWARE_HASH_SIZE, &hash_size),
		      PSA_SUCCESS);
	zassert_equal(hash_size, MBS_FIRMWARE_HASH_SIZE);
}

static const struct device *journal_fault_enable(void)
{
	const struct flash_area *area;
	const struct device *flash;

	zassert_ok(flash_area_open(PARTITION_ID(firmware_journal_partition), &area));
	journal_start = area->fa_off;
	journal_end = area->fa_off + area->fa_size;
	flash = flash_area_get_device(area);
	flash_area_close(area);
	zassert_true(device_is_ready(flash));
	flash_simulator_set_callbacks(flash, &journal_write_fail_callbacks);
	return flash;
}

ZTEST(mbs_firmware_contract, test_fail_closed_lifecycle_and_health_gate)
{
	struct mbs_firmware_status status;
	uint8_t manifest[sizeof(valid_manifest)];
	uint8_t signature[MBS_FIRMWARE_SIGNATURE_SIZE] = {0};
	uint8_t source_hash[MBS_FIRMWARE_HASH_SIZE];
	uint8_t chunk[MBS_FIRMWARE_CHUNK_SIZE] = {0};
	size_t source_hash_offset;
	int rc;

	zassert_ok(mbs_firmware_status_get(&status));
	zassert_equal(status.state, MBS_FIRMWARE_STATE_IDLE);
	zassert_equal(status.result, MBS_FIRMWARE_RESULT_OK);
	zassert_false(status.safe_to_receive);
	zassert_false(status.safe_to_apply);
	zassert_false(status.shutdown_pending);
	zassert_equal(mbs_firmware_health_report(true), -EACCES);
	zassert_equal(mbs_firmware_delta_abort(status.transfer_id, &status), -ENOENT);
	zassert_equal(status.state, MBS_FIRMWARE_STATE_IDLE);
	zassert_equal(status.update_kind, MBS_FIRMWARE_UPDATE_KIND_NONE);

	memcpy(manifest, valid_manifest, sizeof(manifest));
	source_image_prepare(source_hash);
	source_hash_offset = manifest_source_hash_offset(manifest, sizeof(manifest));
	zassert_not_equal(source_hash_offset, SIZE_MAX);
	memcpy(&manifest[source_hash_offset], source_hash, sizeof(source_hash));

	rc = mbs_firmware_delta_begin(manifest, sizeof(manifest), signature, &status);
	zassert_equal(rc, -EAGAIN);
	zassert_equal(status.state, MBS_FIRMWARE_STATE_IDLE);
	zassert_equal(status.result, MBS_FIRMWARE_RESULT_UNSAFE_POWER);
	zassert_equal(status.detail, -EAGAIN);

	mbs_firmware_power_policy_set(true, true, false);
	zassert_ok(mbs_firmware_delta_begin(manifest, sizeof(manifest), signature, &status));
	zassert_equal(status.state, MBS_FIRMWARE_STATE_RECEIVING);
	zassert_equal(status.result, MBS_FIRMWARE_RESULT_OK);

	rc = mbs_firmware_delta_activate(status.transfer_id, &status);
	zassert_equal(rc, -EACCES);
	zassert_equal(status.state, MBS_FIRMWARE_STATE_RECEIVING);
	zassert_equal(status.result, MBS_FIRMWARE_RESULT_BUSY);
	zassert_equal(status.detail, -EACCES);

	{
		const struct device *flash = journal_fault_enable();

		zassert_ok(mbs_firmware_delta_write(status.transfer_id, 0U, chunk,
					 MBS_FIRMWARE_CHUNK_SIZE, &status));
		zassert_ok(mbs_firmware_delta_write(status.transfer_id,
					 MBS_FIRMWARE_CHUNK_SIZE, chunk,
					 MBS_FIRMWARE_CHUNK_SIZE, &status));
		rc = mbs_firmware_delta_write(status.transfer_id,
					 2U * MBS_FIRMWARE_CHUNK_SIZE, chunk,
					 1000U - 2U * MBS_FIRMWARE_CHUNK_SIZE,
					 &status);
		flash_simulator_set_callbacks(flash, NULL);
	}
	zassert_equal(rc, -EIO);
	zassert_equal(status.state, MBS_FIRMWARE_STATE_RECEIVING);
	zassert_equal(status.result, MBS_FIRMWARE_RESULT_FLASH);
	zassert_equal(status.detail, -EIO);
	zassert_equal(status.durable_received, 0U);
	zassert_equal(status.next_offset, 1000U);

	zassert_ok(mbs_firmware_delta_write(status.transfer_id,
				  2U * MBS_FIRMWARE_CHUNK_SIZE, chunk,
				  1000U - 2U * MBS_FIRMWARE_CHUNK_SIZE,
				  &status));
	zassert_equal(status.durable_received, 1000U);
	zassert_equal(status.next_offset, 1000U);

	mbs_firmware_power_policy_set(false, false, true);
	rc = mbs_firmware_delta_write(status.transfer_id, 0U, chunk, sizeof(chunk),
				 &status);
	zassert_equal(rc, -EAGAIN);
	zassert_equal(status.result, MBS_FIRMWARE_RESULT_SHUTDOWN_PENDING);
	zassert_equal(status.detail, -EAGAIN);
	zassert_false(status.safe_to_receive);
	zassert_false(status.safe_to_apply);
	zassert_true(status.shutdown_pending);
	zassert_false(status.retryable);

	mbs_firmware_power_policy_set(true, true, false);
	rc = mbs_firmware_delta_abort(status.transfer_id, &status);
	zassert_equal(rc, -EAGAIN);
	zassert_equal(status.state, MBS_FIRMWARE_STATE_RECEIVING);
	zassert_equal(status.result, MBS_FIRMWARE_RESULT_SHUTDOWN_PENDING);
	zassert_equal(status.detail, -EAGAIN);
	zassert_true(status.shutdown_pending);
	zassert_equal(mbs_firmware_health_report(true), -EACCES);
}

ZTEST_SUITE(mbs_firmware_contract, NULL, NULL, NULL, NULL, NULL);
