/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public Meshbus firmware update coordination API.
 */

#ifndef MESHBUS_INCLUDE_FIRMWARE_H_
#define MESHBUS_INCLUDE_FIRMWARE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Delta wire-protocol version accepted by this implementation. */
#define MBS_FIRMWARE_DELTA_PROTOCOL_VERSION 1U
/** Persistent partition-layout ABI required by delta manifests. */
#define MBS_FIRMWARE_DELTA_PARTITION_ABI 1U
/** SHA-256 transfer identifier size, in bytes. */
#define MBS_FIRMWARE_TRANSFER_ID_SIZE 32U
/** SHA-256 digest size, in bytes. */
#define MBS_FIRMWARE_HASH_SIZE 32U
/** Detached Ed25519 signature size, in bytes. */
#define MBS_FIRMWARE_SIGNATURE_SIZE 64U
/** Opaque campaign identifier size, in bytes. */
#define MBS_FIRMWARE_CAMPAIGN_ID_SIZE 16U
/** Maximum canonical encoded manifest size, in bytes. */
#define MBS_FIRMWARE_MANIFEST_MAX_SIZE 384U
/** Maximum complete staged patch size, in bytes. */
#define MBS_FIRMWARE_PATCH_MAX_SIZE 24576U
/** Required sequential patch chunk size, in bytes, except for the final chunk. */
#define MBS_FIRMWARE_CHUNK_SIZE 448U
/** Maximum number of chunks needed to carry @ref MBS_FIRMWARE_PATCH_MAX_SIZE. */
#define MBS_FIRMWARE_MAX_CHUNKS 55U

/** Firmware payload kind occupying the shared update slot or lifecycle. */
enum mbs_firmware_update_kind {
	MBS_FIRMWARE_UPDATE_KIND_NONE = 0, /**< No update owns the lifecycle. */
	MBS_FIRMWARE_UPDATE_KIND_DELTA, /**< Signed delta staged through Meshbus. */
	MBS_FIRMWARE_UPDATE_KIND_FULL_IMAGE, /**< Full image uploaded through MCUmgr. */
};

/** Target firmware role bound into a signed delta manifest. */
enum mbs_firmware_role {
	MBS_FIRMWARE_ROLE_REPEATER = 1, /**< Mesh relay and management endpoint. */
	MBS_FIRMWARE_ROLE_ROOM = 2,    /**< Room/base endpoint. */
	MBS_FIRMWARE_ROLE_SENSOR = 3,  /**< Headless sensor endpoint. */
};

/** Firmware transfer and boot lifecycle state. */
enum mbs_firmware_state {
	MBS_FIRMWARE_STATE_IDLE = 0,       /**< No active transfer. */
	MBS_FIRMWARE_STATE_RECEIVING,      /**< Sequential patch chunks are being staged. */
	MBS_FIRMWARE_STATE_STAGED,         /**< Complete patch bytes are durably staged. */
	MBS_FIRMWARE_STATE_VERIFIED,       /**< Staged patch hash and format are verified. */
	MBS_FIRMWARE_STATE_APPLYING,       /**< Candidate reconstruction is in progress. */
	MBS_FIRMWARE_STATE_PENDING_REBOOT, /**< Verified test image is armed for reboot. */
	MBS_FIRMWARE_STATE_TESTING,        /**< MCUboot is running an unconfirmed candidate. */
	MBS_FIRMWARE_STATE_CONFIRMED,      /**< Candidate passed health and was confirmed. */
	MBS_FIRMWARE_STATE_ROLLED_BACK,    /**< MCUboot reverted an unconfirmed candidate. */
	MBS_FIRMWARE_STATE_FAILED,         /**< Transfer or apply reached a terminal failure. */
	MBS_FIRMWARE_STATE_ABORTED,        /**< Transfer was explicitly aborted. */
};

/** Stable protocol-level result reported with a firmware status snapshot. */
enum mbs_firmware_result {
	MBS_FIRMWARE_RESULT_OK = 0,          /**< Operation completed or remains valid. */
	MBS_FIRMWARE_RESULT_INVALID,         /**< Request or manifest encoding is invalid. */
	MBS_FIRMWARE_RESULT_UNSUPPORTED,     /**< Requested protocol or patch feature is unsupported. */
	MBS_FIRMWARE_RESULT_BAD_SIGNATURE,   /**< Detached manifest signature is invalid. */
	MBS_FIRMWARE_RESULT_WRONG_TARGET,    /**< Manifest does not identify this target. */
	MBS_FIRMWARE_RESULT_WRONG_SOURCE,    /**< Running source image does not match the manifest. */
	MBS_FIRMWARE_RESULT_DOWNGRADE,       /**< Downgrade policy rejected the candidate. */
	MBS_FIRMWARE_RESULT_CONFLICT,        /**< Transfer identity or replay data conflicts. */
	MBS_FIRMWARE_RESULT_BAD_OFFSET,      /**< Chunk offset or length violates sequential staging. */
	MBS_FIRMWARE_RESULT_BUSY,            /**< Current lifecycle state rejects the operation. */
	MBS_FIRMWARE_RESULT_UNSAFE_POWER,    /**< Target power policy currently blocks the operation. */
	MBS_FIRMWARE_RESULT_SHUTDOWN_PENDING, /**< Shutdown or reboot quiescing is in progress. */
	MBS_FIRMWARE_RESULT_FLASH,           /**< Flash, journal, or image-arm operation failed. */
	MBS_FIRMWARE_RESULT_VERIFY,          /**< Patch, image, or role-health verification failed. */
	MBS_FIRMWARE_RESULT_NO_ROUTE,        /**< Required direct MeshCore route is unavailable. */
	MBS_FIRMWARE_RESULT_RATE_LIMITED,    /**< Airtime or fairness policy deferred the request. */
	MBS_FIRMWARE_RESULT_NOT_PROVISIONED, /**< Required target credentials are unavailable. */
	MBS_FIRMWARE_RESULT_ROLLBACK,        /**< Candidate boot was rolled back. */
	MBS_FIRMWARE_RESULT_INTERNAL,        /**< Unexpected internal failure. */
};

/** Four-component application version carried by the signed manifest. */
struct mbs_firmware_version {
	uint32_t major; /**< Major version component. */
	uint32_t minor; /**< Minor version component. */
	uint32_t patch; /**< Patch version component. */
	uint32_t build; /**< Build or tweak version component. */
};

/** Decoded, authenticated delta manifest used by the target lifecycle. */
struct mbs_firmware_manifest {
	uint8_t campaign_id[MBS_FIRMWARE_CAMPAIGN_ID_SIZE]; /**< Opaque campaign ID. */
	uint8_t role; /**< Target role encoded as @ref mbs_firmware_role. */
	char board_id[32]; /**< NUL-terminated target board identifier. */
	char soc_id[16]; /**< NUL-terminated target SoC identifier. */
	uint32_t hardware_revision_min; /**< Lowest accepted target hardware revision. */
	uint32_t hardware_revision_max; /**< Highest accepted target hardware revision. */
	uint32_t partition_abi; /**< Required target partition ABI. */
	struct mbs_firmware_version source_version; /**< Required running source version. */
	struct mbs_firmware_version target_version; /**< Candidate application version. */
	uint8_t source_hash[MBS_FIRMWARE_HASH_SIZE]; /**< SHA-256 of the source image. */
	uint8_t target_hash[MBS_FIRMWARE_HASH_SIZE]; /**< SHA-256 of the reconstructed image. */
	uint8_t patch_hash[MBS_FIRMWARE_HASH_SIZE]; /**< SHA-256 of the staged patch blob. */
	uint32_t patch_size; /**< Complete staged patch size, in bytes. */
	uint32_t chunk_size; /**< Required sequential chunk size, in bytes. */
	uint32_t image_key_id; /**< MCUboot image-signing key identifier. */
	uint32_t manifest_key_id; /**< Detached manifest-signing key identifier. */
	uint32_t security_counter; /**< Required MCUboot protected security counter. */
	uint32_t patch_format; /**< Signed patch-format identifier. */
};

/** Thread-safe snapshot of firmware progress and current policy. */
struct mbs_firmware_status {
	enum mbs_firmware_update_kind update_kind; /**< Active update payload kind. */
	enum mbs_firmware_state state; /**< Current lifecycle state. */
	enum mbs_firmware_result result; /**< Stable protocol-level result. */
	uint8_t transfer_id[MBS_FIRMWARE_TRANSFER_ID_SIZE]; /**< Active transfer ID. */
	uint32_t durable_received; /**< Patch bytes covered by the persisted checkpoint. */
	uint32_t next_offset; /**< Next sequential offset accepted by the running service. */
	uint32_t patch_size; /**< Expected complete patch size, in bytes. */
	uint32_t chunk_size; /**< Target chunk capability, in bytes. */
	bool retryable; /**< True when retrying or resuming may make progress. */
	bool safe_to_receive; /**< Current target-owned receive power decision. */
	bool safe_to_apply; /**< Current target-owned apply/activate power decision. */
	bool shutdown_pending; /**< True while shutdown or reboot quiescing is pending. */
	int32_t detail; /**< Last operation result as a zero or negative errno value. */
};

/**
 * @brief Read the current firmware lifecycle status.
 *
 * @param[out] status Destination for the copied status snapshot.
 *
 * @retval 0 Status copied successfully.
 * @retval -EINVAL @p status is NULL.
 * @retval -ENODEV Persistent state did not load, so the service is fail-closed.
 */
int mbs_firmware_status_get(struct mbs_firmware_status *status);

/**
 * @brief Admit a signed delta manifest and start or resume its transfer.
 *
 * @param[in] manifest Canonical CBOR manifest bytes. Ownership remains with the caller.
 * @param manifest_size Number of valid bytes in @p manifest.
 * @param[in] signature Detached Ed25519 signature over @p manifest.
 * @param[out] status Optional lifecycle snapshot, including a stable rejection result.
 *
 * @retval 0 Manifest admitted, or the matching transfer resumed idempotently.
 * @retval -EAGAIN Target power policy temporarily blocks reception.
 * @retval -EBUSY A conflicting transfer or lifecycle operation is active.
 * @retval -EKEYREJECTED Manifest authentication failed.
 * @retval -EINVAL Manifest pointers, size, encoding, or fields are invalid.
 * @retval -ENODEV Persistent state did not load, so the service is fail-closed.
 * @return Another negative errno on target/source policy, flash, or journal failure.
 */
int mbs_firmware_delta_begin(const uint8_t *manifest, size_t manifest_size,
			const uint8_t signature[MBS_FIRMWARE_SIGNATURE_SIZE],
			struct mbs_firmware_status *status);

/**
 * @brief Write one offset-bound patch chunk.
 *
 * @param[in] transfer_id Active transfer identifier.
 * @param offset Sequential patch offset, in bytes.
 * @param[in] data Patch bytes. Ownership remains with the caller.
 * @param data_size Number of valid bytes in @p data.
 * @param[out] status Optional lifecycle snapshot after the operation.
 *
 * @retval 0 Chunk accepted, or identical already accepted bytes replayed idempotently.
 * @retval -EINVAL A required pointer, chunk size, or aligned offset is invalid.
 * @retval -ENOENT @p transfer_id does not identify the active transfer.
 * @retval -EALREADY The active transfer is no longer receiving chunks.
 * @retval -EAGAIN Target power policy temporarily blocks reception.
 * @retval -ESPIPE @p offset skips ahead of the next accepted sequential offset.
 * @retval -EILSEQ Replayed bytes conflict with the already staged chunk.
 * @retval -ENODEV Persistent state did not load, so the service is fail-closed.
 * @return Another negative errno on flash or journal failure.
 */
int mbs_firmware_delta_write(const uint8_t transfer_id[MBS_FIRMWARE_TRANSFER_ID_SIZE],
			uint32_t offset, const uint8_t *data, size_t data_size,
			struct mbs_firmware_status *status);

/**
 * @brief Finish and verify a complete staged patch.
 *
 * @param[in] transfer_id Active transfer identifier.
 * @param[out] status Optional lifecycle snapshot after verification.
 *
 * @retval 0 Patch verified, or an already verified transfer acknowledged.
 * @retval -EINVAL @p transfer_id is NULL.
 * @retval -ENOENT @p transfer_id does not identify the active transfer.
 * @retval -ENODATA Required patch bytes are incomplete or the state cannot be finished.
 * @retval -EAGAIN Target power policy temporarily blocks verification.
 * @retval -EILSEQ Staged patch bytes fail hash or format verification.
 * @retval -ENODEV Persistent state did not load, so the service is fail-closed.
 * @return Another negative errno on flash or journal failure.
 */
int mbs_firmware_delta_finish(const uint8_t transfer_id[MBS_FIRMWARE_TRANSFER_ID_SIZE],
			 struct mbs_firmware_status *status);

/**
 * @brief Start asynchronous candidate reconstruction and verification.
 *
 * @param[in] transfer_id Active transfer identifier.
 * @param[out] status Optional lifecycle snapshot after apply admission.
 *
 * @retval 0 Apply accepted, or an active/pending apply acknowledged idempotently.
 * @retval -EINVAL @p transfer_id is NULL.
 * @retval -ENOENT @p transfer_id does not identify the active transfer.
 * @retval -EACCES The transfer has not reached the verified state.
 * @retval -EAGAIN Target power or shutdown policy temporarily blocks apply.
 * @retval -ENODEV Persistent state did not load, so the service is fail-closed.
 * @return Another negative errno on journal or work-submission failure.
 */
int mbs_firmware_delta_apply(const uint8_t transfer_id[MBS_FIRMWARE_TRANSFER_ID_SIZE],
			struct mbs_firmware_status *status);

/**
 * @brief Persist activation intent and schedule a delayed reboot.
 *
 * @param[in] transfer_id Active transfer identifier.
 * @param[out] status Optional lifecycle snapshot after activation admission.
 *
 * @retval 0 Reboot intent persisted and delayed reboot scheduled.
 * @retval -EINVAL @p transfer_id is NULL.
 * @retval -ENOENT @p transfer_id does not identify the active transfer.
 * @retval -EACCES The transfer is not pending reboot.
 * @retval -EAGAIN Target power or shutdown policy temporarily blocks activation.
 * @retval -ENODEV Persistent state did not load, so the service is fail-closed.
 * @return Another negative errno on journal or work-scheduling failure.
 */
int mbs_firmware_delta_activate(const uint8_t transfer_id[MBS_FIRMWARE_TRANSFER_ID_SIZE],
			   struct mbs_firmware_status *status);

/**
 * @brief Complete a pending delta activation after response delivery.
 *
 * An SMP transport adapter may call this after it has reliable evidence that
 * the activation response was delivered. A bounded fallback timer remains
 * armed when the adapter has no delivery-completion signal or that signal is
 * lost, so activation can still be reconciled after reboot.
 *
 * @retval 0 A pending reboot was rescheduled for immediate handoff.
 * @retval -EACCES No delta activation intent is pending.
 * @retval -EAGAIN Power or shutdown policy blocks the handoff.
 * @retval -ENODEV The Firmware service is not ready.
 */
int mbs_firmware_delta_activate_handoff(void);

/**
 * @brief Abort a delta transfer that has not entered apply/testing.
 *
 * @param[in] transfer_id Active transfer identifier.
 * @param[out] status Optional lifecycle snapshot after the abort attempt.
 *
 * @retval 0 Transfer aborted, or an already aborted transfer acknowledged.
 * @retval -EINVAL @p transfer_id is NULL.
 * @retval -ENOENT @p transfer_id does not identify the active transfer.
 * @retval -EBUSY Apply, pending-reboot, or testing state cannot be aborted.
 * @retval -EAGAIN Shutdown or reboot quiescing blocks new journal mutations.
 * @retval -ENODEV Persistent state did not load, so the service is fail-closed.
 * @return Another negative errno on journal persistence failure.
 */
int mbs_firmware_delta_abort(const uint8_t transfer_id[MBS_FIRMWARE_TRANSFER_ID_SIZE],
			struct mbs_firmware_status *status);

/**
 * @brief Update target-owned power policy inputs.
 *
 * Defaults are fail-closed unless an engineering target explicitly enables
 * @kconfig{CONFIG_MBS_FIRMWARE_ASSUME_SAFE_POWER}.
 * A true @p shutdown_pending value is latched until the next boot and cannot be
 * cleared by a later policy update.
 *
 * @param safe_to_receive True when begin, write, and finish operations are safe.
 * @param safe_to_apply True when apply, activation, and confirmation are safe.
 * @param shutdown_pending True when shutdown/reboot quiescing must reject new work.
 */
void mbs_firmware_power_policy_set(bool safe_to_receive, bool safe_to_apply,
				    bool shutdown_pending);

/**
 * @brief Accept or reject the current testing candidate.
 *
 * Confirmation is permitted only in @ref MBS_FIRMWARE_STATE_TESTING and only
 * when the running application version matches the manifest target version.
 *
 * @param healthy True when the application accepts the running candidate.
 *
 * @retval 0 Candidate is confirmed and the durable state is updated.
 * @retval -EALREADY The candidate was already confirmed.
 * @retval -EACCES The FIRMWARE lifecycle is not testing a candidate.
 * @retval -EAGAIN Target power or shutdown policy temporarily blocks confirmation.
 * @retval -EXDEV Running application version differs from the manifest target.
 * @retval -EHOSTDOWN The application rejected the running candidate.
 * @retval -ENODEV Persistent state did not load, so the service is fail-closed.
 * @return Another negative errno on MCUboot confirmation or journal failure.
 */
int mbs_firmware_health_report(bool healthy);

#ifdef __cplusplus
}
#endif

#endif /* MESHBUS_INCLUDE_FIRMWARE_H_ */
