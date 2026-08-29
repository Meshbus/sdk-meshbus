/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <psa/crypto.h>
#include <zephyr/app_version.h>
#include <zephyr/dfu/flash_delta.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_MESHBUS_POWER)
#include <zephyr/meshbus/power.h>
#include <zephyr/zbus/zbus.h>
#endif

#include "firmware_priv.h"

LOG_MODULE_REGISTER(meshbus_firmware, CONFIG_MESHBUS_FIRMWARE_LOG_LEVEL);

#define PATCH_NODE DT_NODELABEL(firmware_patch_partition)
#define JOURNAL_NODE DT_NODELABEL(firmware_journal_partition)
#define SLOT0_NODE DT_NODELABEL(slot0_partition)
#define SLOT1_NODE DT_NODELABEL(slot1_partition)
#define JOURNAL_MAGIC 0x314a4644U /* DFJ1 */
#define JOURNAL_FORMAT 1U
#define JOURNAL_RECORD_SIZE 4096U
#define JOURNAL_RECORD_COUNT 4U
#define MCUBOOT_IMAGE_MAGIC 0x96f3b83dU
#define MCUBOOT_HEADER_SIZE 32U
#define MCUBOOT_TLV_INFO_MAGIC 0x6907U
#define PATCH_AREA_ID PARTITION_ID(firmware_patch_partition)
#define JOURNAL_AREA_ID PARTITION_ID(firmware_journal_partition)

BUILD_ASSERT(DT_NODE_EXISTS(PATCH_NODE), "FIRMWARE patch partition is required");
BUILD_ASSERT(DT_NODE_EXISTS(JOURNAL_NODE), "FIRMWARE journal partition is required");
BUILD_ASSERT(DT_REG_ADDR(SLOT0_NODE) == 0x020000U, "partition ABI 1 slot0 offset");
BUILD_ASSERT(DT_REG_SIZE(SLOT0_NODE) == 600U * 1024U, "partition ABI 1 slot0 size");
BUILD_ASSERT(DT_REG_ADDR(SLOT1_NODE) == 0x0b6000U, "partition ABI 1 slot1 offset");
BUILD_ASSERT(DT_REG_SIZE(SLOT1_NODE) == 604U * 1024U, "partition ABI 1 slot1 size");
BUILD_ASSERT(DT_REG_SIZE(PATCH_NODE) == MESHBUS_FIRMWARE_PATCH_MAX_SIZE,
	     "partition ABI 1 patch size");
BUILD_ASSERT(DT_REG_SIZE(JOURNAL_NODE) == JOURNAL_RECORD_SIZE * JOURNAL_RECORD_COUNT,
	     "partition ABI 1 journal size");
BUILD_ASSERT(sizeof(struct meshbus_firmware_journal) <= JOURNAL_RECORD_SIZE,
	     "journal record must fit one erase sector");

struct firmware_runtime {
	/* Serializes lifecycle operations and protects shared I/O scratch space. */
	struct k_mutex operation_lock;
	/* Protects only the in-memory journal/status snapshot. */
	struct k_mutex state_lock;
	struct meshbus_firmware_journal journal;
	uint32_t accepted_received;
	uint32_t power_sample_sequence;
	uint8_t chunks_since_checkpoint;
	bool safe_to_receive;
	bool safe_to_apply;
	bool shutdown_pending;
};

static struct firmware_runtime runtime;
static struct k_work apply_work;
static struct k_work_delayable reboot_work;
static struct k_work_q apply_work_q;
K_THREAD_STACK_DEFINE(apply_stack, CONFIG_MESHBUS_FIRMWARE_APPLY_STACK_SIZE);
static uint8_t journal_page[JOURNAL_RECORD_SIZE];
static atomic_t runtime_ready;
static atomic_t runtime_locks_ready;
static atomic_t shutdown_latched;

struct patch_reader {
	const struct flash_area *area;
	size_t offset;
	size_t size;
};

static bool transfer_matches_locked(
	const uint8_t transfer_id[MESHBUS_FIRMWARE_TRANSFER_ID_SIZE])
{
	return memcmp(runtime.journal.transfer_id, transfer_id,
		      MESHBUS_FIRMWARE_TRANSFER_ID_SIZE) == 0;
}

static bool shutdown_pending_locked(void)
{
	return runtime.shutdown_pending || atomic_get(&shutdown_latched);
}

static bool firmware_is_ready(void)
{
	return atomic_get(&runtime_ready) != 0;
}

static struct meshbus_firmware_version current_version_get(void)
{
	return (struct meshbus_firmware_version){
		.major = APP_VERSION_MAJOR,
		.minor = APP_VERSION_MINOR,
		.patch = APP_PATCHLEVEL,
		.build = APP_TWEAK,
	};
}

static uint32_t journal_crc(const struct meshbus_firmware_journal *record)
{
	struct meshbus_firmware_journal copy = *record;

	copy.crc32 = 0U;
	return crc32_ieee((const uint8_t *)&copy, sizeof(copy));
}

static bool journal_valid(const struct meshbus_firmware_journal *record)
{
	return record->magic == JOURNAL_MAGIC && record->format == JOURNAL_FORMAT &&
	       record->size == sizeof(*record) && record->generation != 0U &&
	       record->crc32 == journal_crc(record) &&
	       record->state <= MESHBUS_FIRMWARE_STATE_ABORTED &&
	       record->update_kind <= MESHBUS_FIRMWARE_UPDATE_KIND_FULL_IMAGE;
}

/* Caller holds operation_lock. Never performs flash I/O under state_lock. */
static int journal_commit_candidate(struct meshbus_firmware_journal *candidate)
{
	const struct flash_area *area;
	uint32_t index;
	int rc;

	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	candidate->magic = JOURNAL_MAGIC;
	candidate->format = JOURNAL_FORMAT;
	candidate->size = sizeof(*candidate);
	candidate->generation = runtime.journal.generation + 1U;
	if (candidate->generation == 0U) {
		candidate->generation = 1U;
	}
	candidate->crc32 = journal_crc(candidate);
	index = (candidate->generation - 1U) % JOURNAL_RECORD_COUNT;
	memset(journal_page, 0xff, sizeof(journal_page));
	memcpy(journal_page, candidate, sizeof(*candidate));
	k_mutex_unlock(&runtime.state_lock);

	rc = flash_area_open(JOURNAL_AREA_ID, &area);
	if (rc != 0) {
		return rc;
	}
	rc = flash_area_flatten(area, index * JOURNAL_RECORD_SIZE,
				JOURNAL_RECORD_SIZE);
	if (rc == 0) {
		rc = flash_area_write(area, index * JOURNAL_RECORD_SIZE,
				      journal_page, sizeof(journal_page));
	}
	flash_area_close(area);
	if (rc == 0) {
		k_mutex_lock(&runtime.state_lock, K_FOREVER);
		runtime.journal = *candidate;
		k_mutex_unlock(&runtime.state_lock);
	}
	return rc;
}

/* Caller holds operation_lock. */
static int journal_commit(void)
{
	struct meshbus_firmware_journal candidate;

	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	candidate = runtime.journal;
	k_mutex_unlock(&runtime.state_lock);
	return journal_commit_candidate(&candidate);
}

/* Caller holds operation_lock. */
static int journal_recover_pending_arm(void)
{
	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	runtime.journal.state = MESHBUS_FIRMWARE_STATE_PENDING_REBOOT;
	runtime.journal.result = MESHBUS_FIRMWARE_RESULT_OK;
	runtime.journal.detail = 0;
	runtime.journal.reboot_intent = 0U;
	k_mutex_unlock(&runtime.state_lock);
	return journal_commit();
}

/* Caller holds operation_lock. Publishes durable progress only after flash succeeds. */
static int journal_checkpoint(uint32_t durable_received)
{
	struct meshbus_firmware_journal candidate;
	int rc;

	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	candidate = runtime.journal;
	candidate.durable_received = durable_received;
	candidate.result = MESHBUS_FIRMWARE_RESULT_OK;
	candidate.detail = 0;
	k_mutex_unlock(&runtime.state_lock);

	rc = journal_commit_candidate(&candidate);
	if (rc == 0) {
		k_mutex_lock(&runtime.state_lock, K_FOREVER);
		runtime.chunks_since_checkpoint = 0U;
		k_mutex_unlock(&runtime.state_lock);
	}
	return rc;
}

/* Caller holds operation_lock. Never performs flash I/O under state_lock. */
static int journal_load(void)
{
	const struct flash_area *area;
	struct meshbus_firmware_journal candidate;
	struct meshbus_firmware_journal newest = {0};
	bool found = false;
	int rc = flash_area_open(JOURNAL_AREA_ID, &area);

	if (rc != 0) {
		return rc;
	}
	for (uint32_t i = 0U; i < JOURNAL_RECORD_COUNT; i++) {
		rc = flash_area_read(area, i * JOURNAL_RECORD_SIZE,
				     &candidate, sizeof(candidate));
		if (rc != 0) {
			break;
		}
		if (journal_valid(&candidate) &&
		    (!found || candidate.generation > newest.generation)) {
			newest = candidate;
			found = true;
		}
	}
	flash_area_close(area);
	if (rc != 0) {
		return rc;
	}
	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	if (found) {
		runtime.journal = newest;
		/* Legacy engineering delta records used the first reserved byte as zero. */
		if (runtime.journal.update_kind == MESHBUS_FIRMWARE_UPDATE_KIND_NONE &&
		    runtime.journal.state != MESHBUS_FIRMWARE_STATE_IDLE) {
			runtime.journal.update_kind = MESHBUS_FIRMWARE_UPDATE_KIND_DELTA;
		}
		runtime.accepted_received = newest.durable_received;
	} else {
		memset(&runtime.journal, 0, sizeof(runtime.journal));
		runtime.journal.state = MESHBUS_FIRMWARE_STATE_IDLE;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_OK;
	}
	k_mutex_unlock(&runtime.state_lock);
	return 0;
}

static void status_fill_locked(struct meshbus_firmware_status *status)
{
	if (status == NULL) {
		return;
	}
	memset(status, 0, sizeof(*status));
	status->update_kind = runtime.journal.update_kind;
	status->state = runtime.journal.state;
	status->result = runtime.journal.result;
	memcpy(status->transfer_id, runtime.journal.transfer_id,
	       sizeof(status->transfer_id));
	status->durable_received = runtime.journal.durable_received;
	status->next_offset = runtime.accepted_received;
	status->patch_size = runtime.journal.manifest.patch_size;
	status->chunk_size = MESHBUS_FIRMWARE_CHUNK_SIZE;
	status->shutdown_pending = shutdown_pending_locked();
	status->retryable = !status->shutdown_pending &&
			    (status->state == MESHBUS_FIRMWARE_STATE_RECEIVING ||
			     status->result == MESHBUS_FIRMWARE_RESULT_UNSAFE_POWER ||
			     status->result == MESHBUS_FIRMWARE_RESULT_NO_ROUTE ||
			     status->result == MESHBUS_FIRMWARE_RESULT_RATE_LIMITED);
	status->safe_to_receive = runtime.safe_to_receive && !status->shutdown_pending;
	status->safe_to_apply = runtime.safe_to_apply && !status->shutdown_pending;
	status->detail = runtime.journal.detail;
}

static int patch_reader_cb(void *user_data, uint8_t *buf, size_t size)
{
	struct patch_reader *reader = user_data;

	if (reader == NULL || buf == NULL || reader->offset > reader->size ||
	    size > reader->size - reader->offset) {
		return -ENODATA;
	}
	if (flash_area_read(reader->area, reader->offset, buf, size) != 0) {
		return -EIO;
	}
	reader->offset += size;
	return 0;
}

static int patch_hash(const struct flash_area *area, size_t size,
		      uint8_t hash[MESHBUS_FIRMWARE_HASH_SIZE])
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
		status = psa_hash_finish(&op, hash, MESHBUS_FIRMWARE_HASH_SIZE,
					 &output_size);
	}
	psa_hash_abort(&op);
	return status == PSA_SUCCESS && output_size == MESHBUS_FIRMWARE_HASH_SIZE
		       ? 0 : -EIO;
}

static int source_hash_validate(
	const uint8_t expected[MESHBUS_FIRMWARE_HASH_SIZE])
{
	const struct flash_area *area;
	uint8_t header[MCUBOOT_HEADER_SIZE] = {0};
	uint8_t info[4] = {0};
	uint8_t hash[MESHBUS_FIRMWARE_HASH_SIZE];
	uint16_t header_size;
	uint16_t protected_size;
	uint16_t tlv_size;
	uint32_t image_size;
	size_t tlv_offset;
	size_t signed_size;
	int rc = flash_area_open(PARTITION_ID(slot0_partition), &area);

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
		rc = patch_hash(area, signed_size, hash);
	}
	flash_area_close(area);
	if (rc == 0 && memcmp(hash, expected, sizeof(hash)) != 0) {
		rc = -ESTALE;
	}
	return rc;
}

static int patch_header_validate(
	const struct flash_area *area,
	const struct meshbus_firmware_manifest *manifest)
{
	struct flash_delta_patch_header header;
	struct patch_reader reader = {
		.area = area, .size = manifest->patch_size,
	};
	int rc = flash_delta_patch_info(patch_reader_cb, &reader, &header);

	if (rc != 0 ||
	    header.patch_size + FLASH_DELTA_PATCH_HEADER_SIZE != manifest->patch_size ||
	    header.source.major != manifest->source_version.major ||
	    header.source.minor != manifest->source_version.minor ||
	    header.source.revision != manifest->source_version.patch ||
	    header.target.major != manifest->target_version.major ||
	    header.target.minor != manifest->target_version.minor ||
	    header.target.revision != manifest->target_version.patch ||
	    memcmp(header.source_hash, manifest->source_hash,
		   sizeof(header.source_hash)) != 0 ||
	    memcmp(header.target_hash, manifest->target_hash,
		   sizeof(header.target_hash)) != 0) {
		return -EINVAL;
	}
	return 0;
}

static void apply_handler(struct k_work *work)
{
	const struct flash_area *area;
	struct patch_reader reader;
	struct meshbus_firmware_manifest manifest;
	uint8_t image_public_key[32];
	enum meshbus_firmware_result power_result = MESHBUS_FIRMWARE_RESULT_OK;
	bool candidate_armed = false;
	bool arm_state_uncertain = false;
	bool power_rejected = false;
	int rc;
	int commit_rc;

	ARG_UNUSED(work);
	k_mutex_lock(&runtime.operation_lock, K_FOREVER);
	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	if (runtime.journal.state != MESHBUS_FIRMWARE_STATE_APPLYING) {
		k_mutex_unlock(&runtime.state_lock);
		k_mutex_unlock(&runtime.operation_lock);
		return;
	}
	if (shutdown_pending_locked() || !runtime.safe_to_apply) {
		runtime.journal.state = MESHBUS_FIRMWARE_STATE_VERIFIED;
		runtime.journal.result = shutdown_pending_locked()
			? MESHBUS_FIRMWARE_RESULT_SHUTDOWN_PENDING
			: MESHBUS_FIRMWARE_RESULT_UNSAFE_POWER;
		runtime.journal.detail = -EAGAIN;
		k_mutex_unlock(&runtime.state_lock);
		k_mutex_unlock(&runtime.operation_lock);
		return;
	}
	manifest = runtime.journal.manifest;
	k_mutex_unlock(&runtime.state_lock);

	rc = flash_area_open(PATCH_AREA_ID, &area);
	if (rc == 0) {
		reader.area = area;
		reader.offset = 0U;
		reader.size = manifest.patch_size;
		rc = flash_delta_patch_prepare(patch_reader_cb, &reader, reader.size);
		flash_area_close(area);
	}
	if (rc == 0) {
		if (hex2bin(CONFIG_MESHBUS_FIRMWARE_IMAGE_PUBLIC_KEY_HEX,
			    strlen(CONFIG_MESHBUS_FIRMWARE_IMAGE_PUBLIC_KEY_HEX),
			    image_public_key, sizeof(image_public_key)) !=
		    sizeof(image_public_key)) {
			rc = -EINVAL;
		} else {
			rc = flash_delta_patch_validate_candidate(
				image_public_key,
				manifest.security_counter,
				meshbus_firmware_platform_ed25519_verify);
		}
		memset(image_public_key, 0, sizeof(image_public_key));
	}
	if (rc == 0) {
		k_mutex_lock(&runtime.state_lock, K_FOREVER);
		if (shutdown_pending_locked() || !runtime.safe_to_apply) {
			power_result = shutdown_pending_locked()
				? MESHBUS_FIRMWARE_RESULT_SHUTDOWN_PENDING
				: MESHBUS_FIRMWARE_RESULT_UNSAFE_POWER;
			power_rejected = true;
			rc = -EAGAIN;
		}
		k_mutex_unlock(&runtime.state_lock);
	}
	if (rc == 0) {
		int arm_rc = flash_delta_patch_arm();
		int swap_type = mcuboot_swap_type();

		candidate_armed = swap_type == BOOT_SWAP_TYPE_TEST;
		if (candidate_armed) {
			if (arm_rc != 0) {
				LOG_WRN("Arm rc=%d but MCUboot swap type=%d",
					arm_rc, swap_type);
			}
			rc = 0;
		} else if (swap_type == BOOT_SWAP_TYPE_NONE) {
			rc = arm_rc != 0 ? arm_rc : -EIO;
		} else {
			arm_state_uncertain = true;
			rc = arm_rc != 0 ? arm_rc :
				(swap_type < 0 ? swap_type : -EIO);
		}
	}

	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	if (rc == 0) {
		runtime.journal.state = MESHBUS_FIRMWARE_STATE_PENDING_REBOOT;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_OK;
		runtime.journal.detail = 0;
	} else if (power_rejected) {
		runtime.journal.state = MESHBUS_FIRMWARE_STATE_VERIFIED;
		runtime.journal.result = power_result;
		runtime.journal.detail = rc;
	} else if (arm_state_uncertain) {
		runtime.journal.state = MESHBUS_FIRMWARE_STATE_APPLYING;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_FLASH;
		runtime.journal.detail = rc;
	} else {
		runtime.journal.state = MESHBUS_FIRMWARE_STATE_FAILED;
		runtime.journal.result = rc == -EILSEQ || rc == -EKEYREJECTED
					 ? MESHBUS_FIRMWARE_RESULT_VERIFY
					 : MESHBUS_FIRMWARE_RESULT_FLASH;
		runtime.journal.detail = rc;
	}
	k_mutex_unlock(&runtime.state_lock);
	if (power_rejected) {
		/* The durable APPLYING record is retried on a later safe boot. */
		k_mutex_unlock(&runtime.operation_lock);
		return;
	}
	commit_rc = journal_commit();
	if (commit_rc != 0) {
		k_mutex_lock(&runtime.state_lock, K_FOREVER);
		runtime.journal.state = candidate_armed
			? MESHBUS_FIRMWARE_STATE_PENDING_REBOOT
			: (arm_state_uncertain ? MESHBUS_FIRMWARE_STATE_APPLYING
					       : MESHBUS_FIRMWARE_STATE_FAILED);
		if (candidate_armed) {
			runtime.journal.reboot_intent = 0U;
		}
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_FLASH;
		runtime.journal.detail = commit_rc;
		k_mutex_unlock(&runtime.state_lock);
	}
	k_mutex_unlock(&runtime.operation_lock);
}

static void reboot_handler(struct k_work *work)
{
	ARG_UNUSED(work);
#if defined(CONFIG_MESHBUS_POWER)
	(void)meshbus_power_reboot();
#else
	sys_reboot(SYS_REBOOT_COLD);
#endif
}

static bool update_state_owns_slot(enum meshbus_firmware_state state)
{
	return state == MESHBUS_FIRMWARE_STATE_RECEIVING ||
	       state == MESHBUS_FIRMWARE_STATE_STAGED ||
	       state == MESHBUS_FIRMWARE_STATE_VERIFIED ||
	       state == MESHBUS_FIRMWARE_STATE_APPLYING ||
	       state == MESHBUS_FIRMWARE_STATE_PENDING_REBOOT ||
	       state == MESHBUS_FIRMWARE_STATE_TESTING;
}

static bool update_state_precedes_health(enum meshbus_firmware_state state)
{
	return state == MESHBUS_FIRMWARE_STATE_APPLYING ||
	       state == MESHBUS_FIRMWARE_STATE_PENDING_REBOOT;
}

int meshbus_firmware_full_image_upload_admit(
	const uint8_t image_hash[MESHBUS_FIRMWARE_HASH_SIZE], size_t offset)
{
	uint32_t generation;
	bool commit = false;
	int rc = 0;

	if (image_hash == NULL) {
		return -EINVAL;
	}
	if (!firmware_is_ready()) {
		return -ENODEV;
	}

	k_mutex_lock(&runtime.operation_lock, K_FOREVER);
	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	if (shutdown_pending_locked() || !runtime.safe_to_receive) {
		rc = -EAGAIN;
	} else if (offset != 0U) {
		if (runtime.journal.update_kind !=
			    MESHBUS_FIRMWARE_UPDATE_KIND_FULL_IMAGE ||
		    runtime.journal.state != MESHBUS_FIRMWARE_STATE_RECEIVING ||
		    memcmp(runtime.journal.manifest.target_hash, image_hash,
			   MESHBUS_FIRMWARE_HASH_SIZE) != 0) {
			rc = -ESTALE;
		}
	} else if (update_state_owns_slot(runtime.journal.state) &&
		   runtime.journal.update_kind !=
			   MESHBUS_FIRMWARE_UPDATE_KIND_FULL_IMAGE) {
		rc = -EBUSY;
	} else if (runtime.journal.state == MESHBUS_FIRMWARE_STATE_TESTING) {
		rc = -EBUSY;
	} else {
		generation = runtime.journal.generation;
		memset(&runtime.journal, 0, sizeof(runtime.journal));
		runtime.journal.generation = generation;
		runtime.journal.update_kind =
			MESHBUS_FIRMWARE_UPDATE_KIND_FULL_IMAGE;
		runtime.journal.state = MESHBUS_FIRMWARE_STATE_RECEIVING;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_OK;
		memcpy(runtime.journal.manifest.target_hash, image_hash,
		       MESHBUS_FIRMWARE_HASH_SIZE);
		runtime.accepted_received = 0U;
		runtime.chunks_since_checkpoint = 0U;
		commit = true;
	}
	k_mutex_unlock(&runtime.state_lock);

	if (commit) {
		rc = journal_commit();
		if (rc != 0) {
			k_mutex_lock(&runtime.state_lock, K_FOREVER);
			runtime.journal.state = MESHBUS_FIRMWARE_STATE_FAILED;
			runtime.journal.result = MESHBUS_FIRMWARE_RESULT_FLASH;
			runtime.journal.detail = rc;
			k_mutex_unlock(&runtime.state_lock);
		}
	}
	k_mutex_unlock(&runtime.operation_lock);
	return rc;
}

static void full_image_state_commit(enum meshbus_firmware_state state,
				    enum meshbus_firmware_result result,
				    int detail)
{
	struct meshbus_firmware_journal candidate;
	int rc;

	if (!firmware_is_ready()) {
		return;
	}
	k_mutex_lock(&runtime.operation_lock, K_FOREVER);
	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	if (runtime.journal.update_kind !=
	    MESHBUS_FIRMWARE_UPDATE_KIND_FULL_IMAGE) {
		k_mutex_unlock(&runtime.state_lock);
		k_mutex_unlock(&runtime.operation_lock);
		return;
	}
	candidate = runtime.journal;
	candidate.state = state;
	candidate.result = result;
	candidate.detail = detail;
	k_mutex_unlock(&runtime.state_lock);
	rc = journal_commit_candidate(&candidate);
	if (rc != 0) {
		k_mutex_lock(&runtime.state_lock, K_FOREVER);
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_FLASH;
		runtime.journal.detail = rc;
		k_mutex_unlock(&runtime.state_lock);
	}
	k_mutex_unlock(&runtime.operation_lock);
	if (rc != 0) {
		LOG_ERR("Full-image lifecycle checkpoint failed: %d", rc);
	}
}

int meshbus_firmware_full_image_state_write_admit(void)
{
	int rc = 0;

	if (!firmware_is_ready()) {
		return -ENODEV;
	}
	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	if (shutdown_pending_locked() || !runtime.safe_to_apply) {
		rc = -EAGAIN;
	} else if (runtime.journal.update_kind !=
			   MESHBUS_FIRMWARE_UPDATE_KIND_FULL_IMAGE ||
		   runtime.journal.state != MESHBUS_FIRMWARE_STATE_STAGED) {
		/* Only a fully received image may be armed for a test boot.  In
		 * particular, reject active-image confirmation while the candidate is
		 * TESTING: the running application is the sole confirmation authority.
		 */
		rc = -EBUSY;
	}
	k_mutex_unlock(&runtime.state_lock);
	return rc;
}

void meshbus_firmware_full_image_upload_pending(void)
{
	full_image_state_commit(MESHBUS_FIRMWARE_STATE_STAGED,
				MESHBUS_FIRMWARE_RESULT_OK, 0);
}

void meshbus_firmware_full_image_upload_stopped(int detail)
{
	/* The image group does not report why it stopped.  Admission rejections
	 * (including temporary low-power conditions) use the same notification as
	 * flash failures, so retaining the last durable state is the only safe
	 * resumable interpretation.
	 */
	ARG_UNUSED(detail);
}

void meshbus_firmware_full_image_confirmed(void)
{
	if (!firmware_is_ready()) {
		return;
	}
	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	if (runtime.journal.update_kind !=
		    MESHBUS_FIRMWARE_UPDATE_KIND_FULL_IMAGE ||
	    runtime.journal.state != MESHBUS_FIRMWARE_STATE_TESTING) {
		k_mutex_unlock(&runtime.state_lock);
		return;
	}
	k_mutex_unlock(&runtime.state_lock);
	full_image_state_commit(MESHBUS_FIRMWARE_STATE_CONFIRMED,
				MESHBUS_FIRMWARE_RESULT_OK, 0);
}

int meshbus_firmware_status_get(struct meshbus_firmware_status *status)
{
	if (status == NULL) {
		return -EINVAL;
	}
	if (!firmware_is_ready()) {
		return -ENODEV;
	}
	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	status_fill_locked(status);
	k_mutex_unlock(&runtime.state_lock);
	return 0;
}

int meshbus_firmware_delta_begin(const uint8_t *encoded, size_t encoded_size,
			const uint8_t signature[MESHBUS_FIRMWARE_SIGNATURE_SIZE],
			struct meshbus_firmware_status *status)
{
	struct meshbus_firmware_manifest manifest;
	uint8_t transfer_id[MESHBUS_FIRMWARE_TRANSFER_ID_SIZE];
	enum meshbus_firmware_result result;
	const struct flash_area *area;
	int rc;

	if (!firmware_is_ready()) {
		return -ENODEV;
	}
	rc = meshbus_firmware_manifest_admit(encoded, encoded_size, signature,
					 &manifest, transfer_id, &result);

	k_mutex_lock(&runtime.operation_lock, K_FOREVER);
	if (rc == 0) {
		k_mutex_lock(&runtime.state_lock, K_FOREVER);
		if (shutdown_pending_locked() || !runtime.safe_to_receive) {
			runtime.journal.result = shutdown_pending_locked()
				? MESHBUS_FIRMWARE_RESULT_SHUTDOWN_PENDING
				: MESHBUS_FIRMWARE_RESULT_UNSAFE_POWER;
			rc = -EAGAIN;
			runtime.journal.detail = rc;
			status_fill_locked(status);
			k_mutex_unlock(&runtime.state_lock);
			goto out_operation;
		}
		k_mutex_unlock(&runtime.state_lock);
		rc = source_hash_validate(manifest.source_hash);
		if (rc != 0) {
			result = MESHBUS_FIRMWARE_RESULT_WRONG_SOURCE;
		}
	}

	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	if (rc != 0) {
		runtime.journal.result = result;
		runtime.journal.detail = rc;
		status_fill_locked(status);
		k_mutex_unlock(&runtime.state_lock);
		goto out_operation;
	}
	if (transfer_matches_locked(transfer_id) &&
	    runtime.journal.state != MESHBUS_FIRMWARE_STATE_IDLE &&
	    runtime.journal.state != MESHBUS_FIRMWARE_STATE_ABORTED &&
	    runtime.journal.state != MESHBUS_FIRMWARE_STATE_FAILED) {
		rc = 0;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_OK;
		runtime.journal.detail = 0;
		status_fill_locked(status);
		k_mutex_unlock(&runtime.state_lock);
		goto out_operation;
	}
	if (shutdown_pending_locked() || !runtime.safe_to_receive) {
		runtime.journal.result = shutdown_pending_locked()
			? MESHBUS_FIRMWARE_RESULT_SHUTDOWN_PENDING
			: MESHBUS_FIRMWARE_RESULT_UNSAFE_POWER;
		rc = -EAGAIN;
		runtime.journal.detail = rc;
		status_fill_locked(status);
		k_mutex_unlock(&runtime.state_lock);
		goto out_operation;
	}
	if (runtime.journal.state == MESHBUS_FIRMWARE_STATE_RECEIVING ||
	    runtime.journal.state == MESHBUS_FIRMWARE_STATE_STAGED ||
	    runtime.journal.state == MESHBUS_FIRMWARE_STATE_VERIFIED ||
	    runtime.journal.state == MESHBUS_FIRMWARE_STATE_APPLYING ||
	    runtime.journal.state == MESHBUS_FIRMWARE_STATE_PENDING_REBOOT ||
	    runtime.journal.state == MESHBUS_FIRMWARE_STATE_TESTING) {
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_BUSY;
		rc = -EBUSY;
		runtime.journal.detail = rc;
		status_fill_locked(status);
		k_mutex_unlock(&runtime.state_lock);
		goto out_operation;
	}
	k_mutex_unlock(&runtime.state_lock);

	rc = flash_area_open(PATCH_AREA_ID, &area);
	if (rc == 0) {
		rc = flash_area_flatten(area, 0, area->fa_size);
		flash_area_close(area);
	}
	if (rc != 0) {
		k_mutex_lock(&runtime.state_lock, K_FOREVER);
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_FLASH;
		runtime.journal.detail = rc;
		status_fill_locked(status);
		k_mutex_unlock(&runtime.state_lock);
		goto out_operation;
	}

	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	{
		uint32_t next_generation = runtime.journal.generation;

		memset(&runtime.journal, 0, sizeof(runtime.journal));
		runtime.journal.generation = next_generation;
	}
	memcpy(runtime.journal.transfer_id, transfer_id, sizeof(transfer_id));
	memcpy(runtime.journal.manifest_hash, transfer_id, sizeof(transfer_id));
	runtime.journal.manifest = manifest;
	runtime.journal.update_kind = MESHBUS_FIRMWARE_UPDATE_KIND_DELTA;
	runtime.journal.state = MESHBUS_FIRMWARE_STATE_RECEIVING;
	runtime.journal.result = MESHBUS_FIRMWARE_RESULT_OK;
	runtime.accepted_received = 0U;
	runtime.chunks_since_checkpoint = 0U;
	k_mutex_unlock(&runtime.state_lock);

	rc = journal_commit();
	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	if (rc != 0) {
		runtime.journal.state = MESHBUS_FIRMWARE_STATE_FAILED;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_FLASH;
		runtime.journal.detail = rc;
	}
	status_fill_locked(status);
	k_mutex_unlock(&runtime.state_lock);

out_operation:
	k_mutex_unlock(&runtime.operation_lock);
	return rc;
}

int meshbus_firmware_delta_write(const uint8_t transfer_id[MESHBUS_FIRMWARE_TRANSFER_ID_SIZE],
			uint32_t offset, const uint8_t *data, size_t data_size,
			struct meshbus_firmware_status *status)
{
	const struct flash_area *area;
	uint8_t write_buf[MESHBUS_FIRMWARE_CHUNK_SIZE];
	uint8_t verify[MESHBUS_FIRMWARE_CHUNK_SIZE];
	size_t expected;
	size_t aligned;
	uint32_t chunk_index;
	uint32_t patch_size;
	uint32_t accepted_received;
	uint32_t checkpoint_received = 0U;
	bool checkpoint = false;
	int rc = 0;

	if (transfer_id == NULL || data == NULL || data_size == 0U ||
	    data_size > MESHBUS_FIRMWARE_CHUNK_SIZE) {
		return -EINVAL;
	}
	if (!firmware_is_ready()) {
		return -ENODEV;
	}
	k_mutex_lock(&runtime.operation_lock, K_FOREVER);
	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	if (!transfer_matches_locked(transfer_id)) {
		rc = -ENOENT;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_CONFLICT;
		goto out_state;
	}
	if (runtime.journal.state != MESHBUS_FIRMWARE_STATE_RECEIVING) {
		rc = -EALREADY;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_BUSY;
		goto out_state;
	}
	if (shutdown_pending_locked() || !runtime.safe_to_receive) {
		runtime.journal.result = shutdown_pending_locked()
			? MESHBUS_FIRMWARE_RESULT_SHUTDOWN_PENDING
			: MESHBUS_FIRMWARE_RESULT_UNSAFE_POWER;
		rc = -EAGAIN;
		goto out_state;
	}
	if (offset > runtime.journal.manifest.patch_size) {
		rc = -EINVAL;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_BAD_OFFSET;
		goto out_state;
	}
	patch_size = runtime.journal.manifest.patch_size;
	accepted_received = runtime.accepted_received;
	expected = MIN((size_t)MESHBUS_FIRMWARE_CHUNK_SIZE,
		       patch_size - offset);
	if (data_size != expected || (offset % MESHBUS_FIRMWARE_CHUNK_SIZE) != 0U) {
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_BAD_OFFSET;
		rc = -EINVAL;
		goto out_state;
	}
	k_mutex_unlock(&runtime.state_lock);

	rc = flash_area_open(PATCH_AREA_ID, &area);
	if (rc != 0) {
		k_mutex_lock(&runtime.state_lock, K_FOREVER);
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_FLASH;
		goto out_state;
	}
	if (offset < accepted_received) {
		rc = flash_area_read(area, offset, verify, data_size);
		if (rc == 0 && memcmp(verify, data, data_size) != 0) {
			rc = -EILSEQ;
		}
		flash_area_close(area);
		k_mutex_lock(&runtime.state_lock, K_FOREVER);
		runtime.journal.result = rc == 0 ? MESHBUS_FIRMWARE_RESULT_OK
						 : rc == -EILSEQ
							 ? MESHBUS_FIRMWARE_RESULT_CONFLICT
							 : MESHBUS_FIRMWARE_RESULT_FLASH;
		if (rc == 0 &&
		    (runtime.chunks_since_checkpoint >= 4U ||
		     accepted_received == patch_size)) {
			checkpoint = true;
			checkpoint_received = accepted_received;
		}
		if (!checkpoint) {
			goto out_state;
		}
		k_mutex_unlock(&runtime.state_lock);
		rc = journal_checkpoint(checkpoint_received);
		k_mutex_lock(&runtime.state_lock, K_FOREVER);
		if (rc != 0) {
			runtime.journal.result = MESHBUS_FIRMWARE_RESULT_FLASH;
		}
		goto out_state;
	}
	if (offset != accepted_received) {
		flash_area_close(area);
		k_mutex_lock(&runtime.state_lock, K_FOREVER);
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_BAD_OFFSET;
		rc = -ESPIPE;
		goto out_state;
	}
	aligned = ROUND_UP(data_size, flash_area_align(area));
	if (aligned > sizeof(write_buf)) {
		flash_area_close(area);
		k_mutex_lock(&runtime.state_lock, K_FOREVER);
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_FLASH;
		rc = -ENOTSUP;
		goto out_state;
	}
	memset(write_buf, flash_area_erased_val(area), aligned);
	memcpy(write_buf, data, data_size);
	rc = flash_area_write(area, offset, write_buf, aligned);
	if (rc == 0) {
		rc = flash_area_read(area, offset, verify, data_size);
	}
	flash_area_close(area);
	if (rc != 0 || memcmp(verify, data, data_size) != 0) {
		k_mutex_lock(&runtime.state_lock, K_FOREVER);
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_FLASH;
		rc = rc != 0 ? rc : -EIO;
		goto out_state;
	}

	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	chunk_index = offset / MESHBUS_FIRMWARE_CHUNK_SIZE;
	runtime.journal.received_bitmap |= BIT64(chunk_index);
	runtime.accepted_received += data_size;
	runtime.chunks_since_checkpoint++;
	if (runtime.chunks_since_checkpoint >= 4U ||
	    runtime.accepted_received == runtime.journal.manifest.patch_size) {
		checkpoint = true;
		checkpoint_received = runtime.accepted_received;
	}
	runtime.journal.result = MESHBUS_FIRMWARE_RESULT_OK;
	runtime.journal.detail = 0;
	k_mutex_unlock(&runtime.state_lock);

	if (checkpoint) {
		rc = journal_checkpoint(checkpoint_received);
		if (rc != 0) {
			k_mutex_lock(&runtime.state_lock, K_FOREVER);
			runtime.journal.result = MESHBUS_FIRMWARE_RESULT_FLASH;
			goto out_state;
		}
	}
	k_mutex_lock(&runtime.state_lock, K_FOREVER);

out_state:
	runtime.journal.detail = rc;
	status_fill_locked(status);
	k_mutex_unlock(&runtime.state_lock);
	k_mutex_unlock(&runtime.operation_lock);
	return rc;
}

int meshbus_firmware_delta_finish(const uint8_t transfer_id[MESHBUS_FIRMWARE_TRANSFER_ID_SIZE],
			 struct meshbus_firmware_status *status)
{
	const struct flash_area *area;
	struct meshbus_firmware_manifest manifest;
	struct meshbus_firmware_journal staged_candidate;
	uint8_t hash[MESHBUS_FIRMWARE_HASH_SIZE];
	bool commit_staged = false;
	int rc = 0;

	if (transfer_id == NULL) {
		return -EINVAL;
	}
	if (!firmware_is_ready()) {
		return -ENODEV;
	}
	k_mutex_lock(&runtime.operation_lock, K_FOREVER);
	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	if (!transfer_matches_locked(transfer_id)) {
		rc = -ENOENT;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_CONFLICT;
	} else if (runtime.journal.state == MESHBUS_FIRMWARE_STATE_VERIFIED) {
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_OK;
	} else if (runtime.journal.state != MESHBUS_FIRMWARE_STATE_RECEIVING &&
		   runtime.journal.state != MESHBUS_FIRMWARE_STATE_STAGED) {
		rc = -ENODATA;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_BUSY;
	} else if (runtime.journal.state == MESHBUS_FIRMWARE_STATE_RECEIVING &&
		   runtime.accepted_received != runtime.journal.manifest.patch_size) {
		rc = -ENODATA;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_BAD_OFFSET;
	} else if (shutdown_pending_locked() || !runtime.safe_to_receive) {
		rc = -EAGAIN;
		runtime.journal.result = shutdown_pending_locked()
			? MESHBUS_FIRMWARE_RESULT_SHUTDOWN_PENDING
			: MESHBUS_FIRMWARE_RESULT_UNSAFE_POWER;
	}
	if (rc != 0 || runtime.journal.state == MESHBUS_FIRMWARE_STATE_VERIFIED) {
		goto out_state;
	}
	if (runtime.journal.state == MESHBUS_FIRMWARE_STATE_RECEIVING) {
		staged_candidate = runtime.journal;
		staged_candidate.durable_received = runtime.accepted_received;
		staged_candidate.state = MESHBUS_FIRMWARE_STATE_STAGED;
		staged_candidate.result = MESHBUS_FIRMWARE_RESULT_OK;
		staged_candidate.detail = 0;
		commit_staged = true;
	}
	manifest = runtime.journal.manifest;
	k_mutex_unlock(&runtime.state_lock);

	if (commit_staged) {
		rc = journal_commit_candidate(&staged_candidate);
		if (rc != 0) {
			k_mutex_lock(&runtime.state_lock, K_FOREVER);
			runtime.journal.state = MESHBUS_FIRMWARE_STATE_FAILED;
			runtime.journal.result = MESHBUS_FIRMWARE_RESULT_FLASH;
			goto out_state;
		}
	}

	rc = flash_area_open(PATCH_AREA_ID, &area);
	if (rc == 0) {
		rc = patch_hash(area, manifest.patch_size, hash);
		if (rc == 0 && memcmp(hash, manifest.patch_hash, sizeof(hash)) != 0) {
			rc = -EILSEQ;
		}
		if (rc == 0) {
			rc = patch_header_validate(area, &manifest);
		}
		flash_area_close(area);
	}

	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	if (rc == 0) {
		runtime.journal.state = MESHBUS_FIRMWARE_STATE_VERIFIED;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_OK;
	} else {
		runtime.journal.state = MESHBUS_FIRMWARE_STATE_FAILED;
		runtime.journal.result = rc == -EILSEQ || rc == -EINVAL
						      ? MESHBUS_FIRMWARE_RESULT_VERIFY
						      : MESHBUS_FIRMWARE_RESULT_FLASH;
	}
	k_mutex_unlock(&runtime.state_lock);
	{
		int commit_rc = journal_commit();

		if (commit_rc != 0) {
			rc = commit_rc;
			k_mutex_lock(&runtime.state_lock, K_FOREVER);
			runtime.journal.state = MESHBUS_FIRMWARE_STATE_FAILED;
			runtime.journal.result = MESHBUS_FIRMWARE_RESULT_FLASH;
			goto out_state;
		}
	}
	k_mutex_lock(&runtime.state_lock, K_FOREVER);

out_state:
	runtime.journal.detail = rc;
	status_fill_locked(status);
	k_mutex_unlock(&runtime.state_lock);
	k_mutex_unlock(&runtime.operation_lock);
	return rc;
}

int meshbus_firmware_delta_apply(const uint8_t transfer_id[MESHBUS_FIRMWARE_TRANSFER_ID_SIZE],
			struct meshbus_firmware_status *status)
{
	int rc = 0;
	bool submit = false;

	if (transfer_id == NULL) {
		return -EINVAL;
	}
	if (!firmware_is_ready()) {
		return -ENODEV;
	}
	k_mutex_lock(&runtime.operation_lock, K_FOREVER);
	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	if (!transfer_matches_locked(transfer_id)) {
		rc = -ENOENT;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_CONFLICT;
	} else if (runtime.journal.state == MESHBUS_FIRMWARE_STATE_APPLYING &&
		   runtime.journal.result == MESHBUS_FIRMWARE_RESULT_FLASH) {
		rc = runtime.journal.detail != 0 ? runtime.journal.detail : -EIO;
	} else if (runtime.journal.state == MESHBUS_FIRMWARE_STATE_APPLYING ||
		   runtime.journal.state == MESHBUS_FIRMWARE_STATE_PENDING_REBOOT) {
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_OK;
	} else if (runtime.journal.state != MESHBUS_FIRMWARE_STATE_VERIFIED) {
		rc = -EACCES;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_BUSY;
	} else if (shutdown_pending_locked() || !runtime.safe_to_apply) {
		runtime.journal.result = shutdown_pending_locked()
			? MESHBUS_FIRMWARE_RESULT_SHUTDOWN_PENDING
			: MESHBUS_FIRMWARE_RESULT_UNSAFE_POWER;
		rc = -EAGAIN;
	} else {
		runtime.journal.state = MESHBUS_FIRMWARE_STATE_APPLYING;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_OK;
		submit = true;
	}
	runtime.journal.detail = rc;
	k_mutex_unlock(&runtime.state_lock);

	if (submit) {
		int commit_rc = journal_commit();

		if (commit_rc == 0) {
			rc = k_work_submit_to_queue(&apply_work_q, &apply_work);
			if (rc >= 0) {
				rc = 0;
			}
		} else {
			rc = commit_rc;
		}
		if (rc != 0) {
			k_mutex_lock(&runtime.state_lock, K_FOREVER);
			runtime.journal.state = MESHBUS_FIRMWARE_STATE_FAILED;
			runtime.journal.result = commit_rc != 0
				? MESHBUS_FIRMWARE_RESULT_FLASH
				: MESHBUS_FIRMWARE_RESULT_INTERNAL;
			runtime.journal.detail = rc;
			k_mutex_unlock(&runtime.state_lock);
		}
	}

	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	status_fill_locked(status);
	k_mutex_unlock(&runtime.state_lock);
	k_mutex_unlock(&runtime.operation_lock);
	return rc;
}

int meshbus_firmware_delta_activate(const uint8_t transfer_id[MESHBUS_FIRMWARE_TRANSFER_ID_SIZE],
			   struct meshbus_firmware_status *status)
{
	int rc = 0;
	bool commit_intent = false;
	bool schedule_reboot = false;

	if (transfer_id == NULL) {
		return -EINVAL;
	}
	if (!firmware_is_ready()) {
		return -ENODEV;
	}
	k_mutex_lock(&runtime.operation_lock, K_FOREVER);
	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	if (!transfer_matches_locked(transfer_id)) {
		rc = -ENOENT;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_CONFLICT;
	} else if (runtime.journal.state != MESHBUS_FIRMWARE_STATE_PENDING_REBOOT) {
		rc = -EACCES;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_BUSY;
	} else if (shutdown_pending_locked() || !runtime.safe_to_apply) {
		rc = -EAGAIN;
		runtime.journal.result = shutdown_pending_locked()
			? MESHBUS_FIRMWARE_RESULT_SHUTDOWN_PENDING
			: MESHBUS_FIRMWARE_RESULT_UNSAFE_POWER;
	} else {
		if (!runtime.journal.reboot_intent) {
			runtime.journal.reboot_intent = 1U;
			commit_intent = true;
		}
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_OK;
		schedule_reboot = true;
	}
	runtime.journal.detail = rc;
	k_mutex_unlock(&runtime.state_lock);

	if (commit_intent) {
		rc = journal_commit();
		if (rc != 0) {
			schedule_reboot = false;
			k_mutex_lock(&runtime.state_lock, K_FOREVER);
			runtime.journal.reboot_intent = 0U;
			runtime.journal.result = MESHBUS_FIRMWARE_RESULT_FLASH;
			runtime.journal.detail = rc;
			k_mutex_unlock(&runtime.state_lock);
		}
	}
	if (schedule_reboot) {
		int schedule_rc = k_work_reschedule(
			&reboot_work,
			K_MSEC(CONFIG_MESHBUS_FIRMWARE_ACTIVATE_HANDOFF_TIMEOUT_MS));

		if (schedule_rc < 0) {
			rc = schedule_rc;
			k_mutex_lock(&runtime.state_lock, K_FOREVER);
			runtime.journal.result = MESHBUS_FIRMWARE_RESULT_INTERNAL;
			runtime.journal.detail = rc;
			k_mutex_unlock(&runtime.state_lock);
		}
	}

	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	status_fill_locked(status);
	k_mutex_unlock(&runtime.state_lock);
	k_mutex_unlock(&runtime.operation_lock);
	return rc;
}

int meshbus_firmware_delta_activate_handoff(void)
{
	int rc;

	if (!firmware_is_ready()) {
		return -ENODEV;
	}
	k_mutex_lock(&runtime.operation_lock, K_FOREVER);
	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	if (runtime.journal.update_kind != MESHBUS_FIRMWARE_UPDATE_KIND_DELTA ||
	    runtime.journal.state != MESHBUS_FIRMWARE_STATE_PENDING_REBOOT ||
	    runtime.journal.reboot_intent == 0U) {
		rc = -EACCES;
	} else if (shutdown_pending_locked() || !runtime.safe_to_apply) {
		rc = -EAGAIN;
	} else {
		rc = 0;
	}
	k_mutex_unlock(&runtime.state_lock);
	if (rc == 0) {
		rc = k_work_reschedule(&reboot_work, K_MSEC(100));
		if (rc >= 0) {
			rc = 0;
		}
	}
	k_mutex_unlock(&runtime.operation_lock);
	return rc;
}

int meshbus_firmware_delta_abort(const uint8_t transfer_id[MESHBUS_FIRMWARE_TRANSFER_ID_SIZE],
			struct meshbus_firmware_status *status)
{
	int rc = 0;
	enum meshbus_firmware_state previous_state = MESHBUS_FIRMWARE_STATE_IDLE;
	bool commit_abort = false;

	if (transfer_id == NULL) {
		return -EINVAL;
	}
	if (!firmware_is_ready()) {
		return -ENODEV;
	}
	k_mutex_lock(&runtime.operation_lock, K_FOREVER);
	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	if (shutdown_pending_locked()) {
		rc = -EAGAIN;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_SHUTDOWN_PENDING;
	} else if (!transfer_matches_locked(transfer_id)) {
		rc = -ENOENT;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_CONFLICT;
	} else if (runtime.journal.state == MESHBUS_FIRMWARE_STATE_ABORTED) {
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_OK;
	} else if (runtime.journal.state == MESHBUS_FIRMWARE_STATE_APPLYING ||
		   runtime.journal.state == MESHBUS_FIRMWARE_STATE_PENDING_REBOOT ||
		   runtime.journal.state == MESHBUS_FIRMWARE_STATE_TESTING) {
		rc = -EBUSY;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_BUSY;
	} else {
		previous_state = runtime.journal.state;
		runtime.journal.state = MESHBUS_FIRMWARE_STATE_ABORTED;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_OK;
		commit_abort = true;
	}
	runtime.journal.detail = rc;
	k_mutex_unlock(&runtime.state_lock);

	if (commit_abort) {
		rc = journal_commit();
		if (rc != 0) {
			k_mutex_lock(&runtime.state_lock, K_FOREVER);
			runtime.journal.state = previous_state;
			runtime.journal.result = MESHBUS_FIRMWARE_RESULT_FLASH;
			runtime.journal.detail = rc;
			k_mutex_unlock(&runtime.state_lock);
		}
	}

	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	status_fill_locked(status);
	k_mutex_unlock(&runtime.state_lock);
	k_mutex_unlock(&runtime.operation_lock);
	return rc;
}

void meshbus_firmware_power_policy_set(bool safe_to_receive, bool safe_to_apply,
				    bool shutdown_pending)
{
	if (shutdown_pending) {
		atomic_set(&shutdown_latched, 1);
	}
	if (!atomic_get(&runtime_locks_ready)) {
		return;
	}
	if (shutdown_pending) {
		/* Let the active lifecycle operation reach a safe boundary first. */
		k_mutex_lock(&runtime.operation_lock, K_FOREVER);
	}
	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	if (shutdown_pending_locked()) {
		runtime.safe_to_receive = false;
		runtime.safe_to_apply = false;
		runtime.shutdown_pending = true;
	} else {
		runtime.safe_to_receive = safe_to_receive;
		runtime.safe_to_apply = safe_to_apply;
	}
	k_mutex_unlock(&runtime.state_lock);
	if (shutdown_pending) {
		k_mutex_unlock(&runtime.operation_lock);
	}
}

int meshbus_firmware_health_report(bool healthy)
{
	const struct meshbus_firmware_version current = current_version_get();
	bool commit_failure = false;
	int rc;

	if (!firmware_is_ready()) {
		return -ENODEV;
	}
	k_mutex_lock(&runtime.operation_lock, K_FOREVER);
	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	if (runtime.journal.state != MESHBUS_FIRMWARE_STATE_TESTING) {
		if (healthy &&
		    runtime.journal.state == MESHBUS_FIRMWARE_STATE_CONFIRMED &&
		    runtime.journal.result == MESHBUS_FIRMWARE_RESULT_FLASH) {
			if (shutdown_pending_locked() || !runtime.safe_to_apply) {
				rc = -EAGAIN;
				k_mutex_unlock(&runtime.state_lock);
				goto out_operation;
			}
			runtime.journal.result = MESHBUS_FIRMWARE_RESULT_OK;
			runtime.journal.detail = 0;
			k_mutex_unlock(&runtime.state_lock);
			rc = journal_commit();
			if (rc != 0) {
				k_mutex_lock(&runtime.state_lock, K_FOREVER);
				runtime.journal.result = MESHBUS_FIRMWARE_RESULT_FLASH;
				runtime.journal.detail = rc;
				k_mutex_unlock(&runtime.state_lock);
			}
			goto out_operation;
		}
		rc = runtime.journal.state == MESHBUS_FIRMWARE_STATE_CONFIRMED
			? -EALREADY : -EACCES;
		k_mutex_unlock(&runtime.state_lock);
		goto out_operation;
	}
	if (shutdown_pending_locked() || !runtime.safe_to_apply) {
		rc = -EAGAIN;
		runtime.journal.result = shutdown_pending_locked()
			? MESHBUS_FIRMWARE_RESULT_SHUTDOWN_PENDING
			: MESHBUS_FIRMWARE_RESULT_UNSAFE_POWER;
		runtime.journal.detail = rc;
		k_mutex_unlock(&runtime.state_lock);
		goto out_operation;
	}
	if (runtime.journal.update_kind == MESHBUS_FIRMWARE_UPDATE_KIND_DELTA &&
	    memcmp(&current, &runtime.journal.manifest.target_version,
		   sizeof(current)) != 0) {
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_WRONG_TARGET;
		runtime.journal.detail = -EXDEV;
		rc = -EXDEV;
		commit_failure = true;
		k_mutex_unlock(&runtime.state_lock);
		goto persist_failure;
	}
	if (!healthy) {
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_VERIFY;
		runtime.journal.detail = -EHOSTDOWN;
		rc = -EHOSTDOWN;
		commit_failure = true;
		k_mutex_unlock(&runtime.state_lock);
		goto persist_failure;
	}
	k_mutex_unlock(&runtime.state_lock);

	rc = boot_is_img_confirmed() ? 0 : boot_write_img_confirmed();
	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	if (rc == 0) {
		runtime.journal.state = MESHBUS_FIRMWARE_STATE_CONFIRMED;
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_OK;
		runtime.journal.detail = 0;
	} else {
		runtime.journal.result = MESHBUS_FIRMWARE_RESULT_FLASH;
		runtime.journal.detail = rc;
	}
	k_mutex_unlock(&runtime.state_lock);
	{
		int commit_rc = journal_commit();

		if (commit_rc != 0 && rc == 0) {
			rc = commit_rc;
			k_mutex_lock(&runtime.state_lock, K_FOREVER);
			runtime.journal.result = MESHBUS_FIRMWARE_RESULT_FLASH;
			runtime.journal.detail = commit_rc;
			k_mutex_unlock(&runtime.state_lock);
		}
	}
	goto out_operation;

persist_failure:
	if (commit_failure) {
		int commit_rc = journal_commit();

		if (commit_rc != 0) {
			LOG_ERR("Failed to persist rejected candidate state: %d",
				commit_rc);
			k_mutex_lock(&runtime.state_lock, K_FOREVER);
			runtime.journal.result = MESHBUS_FIRMWARE_RESULT_FLASH;
			runtime.journal.detail = commit_rc;
			k_mutex_unlock(&runtime.state_lock);
			rc = commit_rc;
		}
	}

out_operation:
	k_mutex_unlock(&runtime.operation_lock);
	return rc;
}

#if defined(CONFIG_MESHBUS_POWER)
static void firmware_power_sample_listener_cb(const struct zbus_channel *chan)
{
	const struct meshbus_power_fuel_gauge_data_event *event;
	bool external_power;

	if (chan != &meshbus_power_fuel_gauge_data_chan) {
		return;
	}
	event = zbus_chan_const_msg(chan);
	if (event == NULL) {
		return;
	}
	if (IS_ENABLED(CONFIG_MESHBUS_FIRMWARE_ASSUME_SAFE_POWER)) {
		return;
	}
	external_power = event->charging || event->online;
	if (!atomic_get(&runtime_locks_ready)) {
		return;
	}
	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	runtime.power_sample_sequence++;
	if (!shutdown_pending_locked()) {
		runtime.safe_to_receive = external_power;
		runtime.safe_to_apply = external_power;
	}
	k_mutex_unlock(&runtime.state_lock);
}

ZBUS_LISTENER_DEFINE(meshbus_firmware_power_sample_listener,
		     firmware_power_sample_listener_cb);
ZBUS_CHAN_ADD_OBS(meshbus_power_fuel_gauge_data_chan,
		  meshbus_firmware_power_sample_listener, 3);

static void firmware_power_action_cb(enum meshbus_power_action action,
				  void *user_data)
{
	ARG_UNUSED(user_data);

	if (action == MESHBUS_POWER_ACTION_SHUTDOWN ||
	    action == MESHBUS_POWER_ACTION_REBOOT) {
		meshbus_firmware_power_policy_set(false, false, true);
	}
}

MESHBUS_POWER_ACTION_CALLBACK_DEFINE(firmware_power_action_cb, NULL);
#endif

static int firmware_init(void)
{
	uint8_t target_hash[MESHBUS_FIRMWARE_HASH_SIZE] = {0};
	struct meshbus_firmware_version target_version = {0};
	bool delta_target_running = false;
	bool resume_apply = false;
	bool initial_safe = IS_ENABLED(CONFIG_MESHBUS_FIRMWARE_ASSUME_SAFE_POWER);
	enum meshbus_firmware_state state = MESHBUS_FIRMWARE_STATE_FAILED;
	int rc;

	k_mutex_init(&runtime.operation_lock);
	k_mutex_init(&runtime.state_lock);
	runtime.safe_to_receive = initial_safe;
	runtime.safe_to_apply = initial_safe;
	if (atomic_get(&shutdown_latched)) {
		runtime.safe_to_receive = false;
		runtime.safe_to_apply = false;
		runtime.shutdown_pending = true;
	}
	atomic_set(&runtime_locks_ready, 1);

	k_work_init(&apply_work, apply_handler);
	k_work_init_delayable(&reboot_work, reboot_handler);
	k_work_queue_start(&apply_work_q, apply_stack, K_THREAD_STACK_SIZEOF(apply_stack),
			   K_LOWEST_APPLICATION_THREAD_PRIO, NULL);
	k_thread_name_set(k_work_queue_thread_get(&apply_work_q), "meshbus_firmware");

	k_mutex_lock(&runtime.operation_lock, K_FOREVER);
	rc = atomic_get(&shutdown_latched) ? -EAGAIN : journal_load();
	if (rc == 0) {
		bool confirmed = false;
		bool reconcile = false;
		enum meshbus_firmware_update_kind update_kind;
		const struct meshbus_firmware_version current = current_version_get();

		k_mutex_lock(&runtime.state_lock, K_FOREVER);
		state = runtime.journal.state;
		update_kind = runtime.journal.update_kind;
		if (update_kind == MESHBUS_FIRMWARE_UPDATE_KIND_DELTA &&
		    (state == MESHBUS_FIRMWARE_STATE_APPLYING ||
		     state == MESHBUS_FIRMWARE_STATE_PENDING_REBOOT ||
		     state == MESHBUS_FIRMWARE_STATE_TESTING)) {
			memcpy(target_hash,
			       runtime.journal.manifest.target_hash,
			       sizeof(target_hash));
			target_version =
				runtime.journal.manifest.target_version;
		}
		k_mutex_unlock(&runtime.state_lock);

		if (update_kind == MESHBUS_FIRMWARE_UPDATE_KIND_DELTA &&
		    (state == MESHBUS_FIRMWARE_STATE_APPLYING ||
		     state == MESHBUS_FIRMWARE_STATE_PENDING_REBOOT ||
		     state == MESHBUS_FIRMWARE_STATE_TESTING)) {
			delta_target_running =
				source_hash_validate(target_hash) == 0 &&
				memcmp(&current, &target_version,
				       sizeof(current)) == 0;
			if (state == MESHBUS_FIRMWARE_STATE_APPLYING &&
			    !delta_target_running) {
				int swap_type = mcuboot_swap_type();

				if (swap_type == BOOT_SWAP_TYPE_NONE) {
					resume_apply = true;
				} else if (swap_type == BOOT_SWAP_TYPE_TEST) {
					rc = journal_recover_pending_arm();
				} else {
					rc = swap_type < 0 ? swap_type : -EIO;
				}
			}
		}
		reconcile = state == MESHBUS_FIRMWARE_STATE_PENDING_REBOOT ||
			    state == MESHBUS_FIRMWARE_STATE_TESTING ||
			    delta_target_running ||
			    (update_kind == MESHBUS_FIRMWARE_UPDATE_KIND_FULL_IMAGE &&
			     state == MESHBUS_FIRMWARE_STATE_STAGED);

		if (reconcile) {
			confirmed = boot_is_img_confirmed();
			if (update_kind == MESHBUS_FIRMWARE_UPDATE_KIND_FULL_IMAGE) {
				bool commit_reconcile = false;
				bool target_running = source_hash_validate(
					runtime.journal.manifest.target_hash) == 0;
				bool confirmation_bypassed = target_running && confirmed &&
					state == MESHBUS_FIRMWARE_STATE_STAGED;

				k_mutex_lock(&runtime.state_lock, K_FOREVER);
				if (confirmation_bypassed) {
					runtime.journal.result =
						MESHBUS_FIRMWARE_RESULT_FLASH;
					runtime.journal.detail = -EPERM;
					commit_reconcile = true;
				} else if (target_running && confirmed) {
					runtime.journal.state =
						MESHBUS_FIRMWARE_STATE_CONFIRMED;
					runtime.journal.result =
						MESHBUS_FIRMWARE_RESULT_OK;
					commit_reconcile = true;
				} else if (target_running) {
					runtime.journal.state = MESHBUS_FIRMWARE_STATE_TESTING;
					runtime.journal.reboot_intent = 0U;
					commit_reconcile = true;
				} else if (state == MESHBUS_FIRMWARE_STATE_TESTING) {
					runtime.journal.state =
						MESHBUS_FIRMWARE_STATE_ROLLED_BACK;
					runtime.journal.result =
						MESHBUS_FIRMWARE_RESULT_ROLLBACK;
					commit_reconcile = true;
				}
				k_mutex_unlock(&runtime.state_lock);
				if (commit_reconcile) {
					int commit_rc = journal_commit();

					rc = commit_rc != 0 ? commit_rc :
						(confirmation_bypassed ? -EPERM : 0);
				}
			} else if (state == MESHBUS_FIRMWARE_STATE_PENDING_REBOOT ||
				   delta_target_running || confirmed) {
				bool confirmation_bypassed = confirmed &&
					delta_target_running &&
					update_state_precedes_health(state);

				k_mutex_lock(&runtime.state_lock, K_FOREVER);
				if (confirmation_bypassed) {
					runtime.journal.result =
						MESHBUS_FIRMWARE_RESULT_FLASH;
					runtime.journal.detail = -EPERM;
				} else if (!confirmed) {
					runtime.journal.state = MESHBUS_FIRMWARE_STATE_TESTING;
					runtime.journal.reboot_intent = 0U;
				} else if (delta_target_running) {
					runtime.journal.state = MESHBUS_FIRMWARE_STATE_CONFIRMED;
					runtime.journal.result = MESHBUS_FIRMWARE_RESULT_OK;
				} else {
					runtime.journal.state = MESHBUS_FIRMWARE_STATE_ROLLED_BACK;
					runtime.journal.result = MESHBUS_FIRMWARE_RESULT_ROLLBACK;
				}
				k_mutex_unlock(&runtime.state_lock);
				{
					int commit_rc = journal_commit();

					if (commit_rc != 0) {
						rc = commit_rc;
					} else if (confirmation_bypassed) {
						rc = -EPERM;
					}
				}
			}
		}
	}
	k_mutex_lock(&runtime.state_lock, K_FOREVER);
	state = runtime.journal.state;
	k_mutex_unlock(&runtime.state_lock);
	k_mutex_unlock(&runtime.operation_lock);

#if defined(CONFIG_MESHBUS_POWER)
	if (rc == 0 && !IS_ENABLED(CONFIG_MESHBUS_FIRMWARE_ASSUME_SAFE_POWER) &&
	    !atomic_get(&shutdown_latched)) {
		bool external_power = false;
		uint32_t sample_sequence;
		int sample_rc;

		k_mutex_lock(&runtime.state_lock, K_FOREVER);
		sample_sequence = runtime.power_sample_sequence;
		k_mutex_unlock(&runtime.state_lock);
		sample_rc = meshbus_power_fuel_gauge_get(NULL, NULL, NULL);
		if (sample_rc == 0) {
			external_power = meshbus_power_is_charging() ||
					 meshbus_power_is_online();
		}
		k_mutex_lock(&runtime.state_lock, K_FOREVER);
		if (!shutdown_pending_locked() &&
		    runtime.power_sample_sequence == sample_sequence) {
			runtime.safe_to_receive = sample_rc == 0 && external_power;
			runtime.safe_to_apply = sample_rc == 0 && external_power;
		}
		k_mutex_unlock(&runtime.state_lock);
	}
#endif

	if (rc == 0 && resume_apply) {
		bool can_resume;
		int submit_rc;

		k_mutex_lock(&runtime.state_lock, K_FOREVER);
		can_resume = !shutdown_pending_locked() && runtime.safe_to_apply;
		if (!can_resume) {
			runtime.journal.state = MESHBUS_FIRMWARE_STATE_VERIFIED;
			runtime.journal.result = shutdown_pending_locked()
				? MESHBUS_FIRMWARE_RESULT_SHUTDOWN_PENDING
				: MESHBUS_FIRMWARE_RESULT_UNSAFE_POWER;
			runtime.journal.detail = -EAGAIN;
			state = runtime.journal.state;
		}
		k_mutex_unlock(&runtime.state_lock);

		if (can_resume) {
			submit_rc = k_work_submit_to_queue(&apply_work_q, &apply_work);
			if (submit_rc < 0) {
				rc = submit_rc;
			}
		}
	}
	if (rc == 0) {
		atomic_set(&runtime_ready, 1);
		/* Close the race with an action callback that ran during init. */
		if (atomic_get(&shutdown_latched)) {
			meshbus_firmware_power_policy_set(false, false, true);
		}
	} else {
		atomic_set(&runtime_ready, 0);
		LOG_ERR("FIRMWARE initialization failed: %d", rc);
	}
	LOG_INF("FIRMWARE state %u, partition ABI %u, role %u", state,
		MESHBUS_FIRMWARE_DELTA_PARTITION_ABI, CONFIG_MESHBUS_FIRMWARE_ROLE);
	return rc;
}

SYS_INIT(firmware_init, APPLICATION, CONFIG_MESHBUS_FIRMWARE_INIT_PRIORITY);
