/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Flash delta header file
 *
 * This header file declares prototypes for the flash delta APIs used for DFU.
 */

#ifndef MESHBUS_INCLUDE_DFU_FLASH_DELTA_H_
#define MESHBUS_INCLUDE_DFU_FLASH_DELTA_H_

/**
 * @brief Abstraction layer to write firmware patch to flash
 *
 * @defgroup flash_delta_api Flash delta API
 * @ingroup os_services
 * @{
 */

#include <zephyr/kernel.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Patch header magic value ('NEWP' = 0x5057454E) */
#define FLASH_DELTA_PATCH_MAGIC 0x5057454E

/** @brief Patch header size in bytes */
#define FLASH_DELTA_PATCH_HEADER_SIZE 88

/** @brief SHA256 image hash size in bytes */
#define FLASH_DELTA_IMAGE_HASH_SIZE 32

/**
 * @brief Patch version structure
 */
struct flash_delta_patch_version {
	uint8_t major;
	uint8_t minor;
	uint16_t revision;
};

/**
 * @brief Patch header structure
 *
 * This structure defines the patch header format for delta patches.
 * The header is followed by the compressed patch data.
 */
struct flash_delta_patch_header {
	uint32_t magic;                          /**< Magic value (FLASH_DELTA_PATCH_MAGIC) */
	struct flash_delta_patch_version source; /**< Source firmware version */
	struct flash_delta_patch_version target; /**< Target firmware version */
	uint32_t patch_size;                     /**< Patch data size (excluding header) */
	uint32_t source_size;                    /**< Source image size in bytes */
	uint32_t target_size;                    /**< Target image size in bytes */
	uint8_t source_hash[FLASH_DELTA_IMAGE_HASH_SIZE]; /**< Source image SHA256 */
	uint8_t target_hash[FLASH_DELTA_IMAGE_HASH_SIZE]; /**< Target image SHA256 */
};

/**
 * @brief Callback function type for reading patch data
 *
 * This callback is used to read patch data from any source (file, BLE, LoRa,
 * storage partition, etc.). The callback maintains its own read cursor and
 * reads data sequentially starting from offset 0.
 *
 * @param user_data User-provided context pointer
 * @param buf Buffer to read data into
 * @param size Number of bytes to read
 * @return 0 on success, negative errno on failure
 */
typedef int (*flash_delta_patch_read_cb_t)(void *user_data, uint8_t *buf, size_t size);

/** Detached Ed25519 verification callback used for prepared image admission. */
typedef int (*flash_delta_ed25519_verify_cb_t)(
	const uint8_t public_key[32], const uint8_t signature[64],
	const uint8_t *message, size_t message_size);

/**
 * @brief Read and validate patch header
 *
 * Reads the patch header bytes via the read callback,
 * validates the magic value, and returns the header information.
 *
 * @note The read callback cursor starts at offset 0. After this function
 *       returns successfully, the cursor will be at offset
 *       FLASH_DELTA_PATCH_HEADER_SIZE.
 *
 * @param read_cb Callback function to read patch data
 * @param user_data User context passed to read_cb
 * @param header Pointer to store the patch header information
 * @return 0 on success, negative errno on failure
 * @retval -EINVAL Invalid parameter (NULL pointer)
 * @retval -EIO Read callback failed
 * @retval -ENODATA Patch source ended before a complete header was read
 * @retval -EINVAL Invalid patch magic
 */
int flash_delta_patch_info(flash_delta_patch_read_cb_t read_cb, void *user_data,
			   struct flash_delta_patch_header *header);

/**
 * @brief Apply delta patch to flash
 *
 * Applies a delta patch to upgrade firmware from slot0 to slot1.
 * The function reads patch data via the provided callback, validates
 * the header, applies the patch using detools, and requests MCUboot upgrade.
 *
 * @note The read callback cursor starts at offset 0. The function first
 *       reads the header, then continues reading the patch data.
 *
 * @param read_cb Callback function to read patch data
 * @param user_data User context passed to read_cb
 * @param patch_size Total patch size including header (in bytes)
 * @return 0 on success, negative errno on failure
 * @retval -EINVAL Invalid parameter
 * @retval -EIO Flash or read operation failed
 * @retval -ENODATA Patch source ended before all expected bytes were read
 * @retval -ENOSPC Reconstructed image does not fit in the secondary slot
 * @retval -EINVAL Invalid patch magic, size, or reconstructed image header
 * @retval -ENOTSUP Version mismatch
 * @retval -EILSEQ Source or target image hash mismatch
 */
int flash_delta_patch_apply(flash_delta_patch_read_cb_t read_cb, void *user_data,
			    size_t patch_size);

/**
 * @brief Reconstruct and verify a delta image without arming MCUboot.
 *
 * This is the safe preparation half of @ref flash_delta_patch_apply. It erases
 * and reconstructs slot1 and verifies the declared target hash, but leaves the
 * boot request untouched so a policy owner can perform additional manifest,
 * key, and security-counter checks before activation.
 */
int flash_delta_patch_prepare(flash_delta_patch_read_cb_t read_cb,
			      void *user_data, size_t patch_size);

/** @brief Arm an already prepared secondary image as an MCUboot test upgrade. */
int flash_delta_patch_arm(void);

/**
 * @brief Validate the prepared image signature, key hash, and security counter.
 *
 * This performs the application-side pre-arm cross-check. MCUboot performs an
 * independent validation again during boot.
 */
int flash_delta_patch_validate_candidate(
	const uint8_t public_key[32], uint32_t expected_security_counter,
	flash_delta_ed25519_verify_cb_t verify_cb);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* MESHBUS_INCLUDE_DFU_FLASH_DELTA_H_ */
