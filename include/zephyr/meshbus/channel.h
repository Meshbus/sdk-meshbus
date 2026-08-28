/*
 * Copyright (c) 2025 FoBE Projects
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Meshbus Channel API
 *
 * This module manages Meshbus channels stored in settings.
 */

#ifndef ZEPHYR_INCLUDE_MESHBUS_CHANNEL_H_
#define ZEPHYR_INCLUDE_MESHBUS_CHANNEL_H_

#include <stddef.h>
#include <stdint.h>

#include "meshbus/channel.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum channel secret size in bytes. */
#define MESHBUS_CHANNEL_SECRET_SIZE        32
/** Default channel secret size used by MeshCore-generated channels. */
#define MESHBUS_CHANNEL_SECRET_DEFAULT_LEN 16
/** Maximum channel name length (bytes, including NUL). */
#define MESHBUS_CHANNEL_NAME_MAX_LEN       32

/**
 * @brief Public Meshbus channel type.
 */
typedef meshbus_Channel meshbus_channel;

/**
 * @brief Set a channel slot record by stable local slot index.
 *
 * The stored channel hash is derived from @p secret by the channel service.
 *
 * @param index Channel slot index.
 * @param secret Channel secret bytes (16 or 32 bytes).
 * @param secret_len Secret length in bytes.
 * @param name Optional display name; NULL or empty means use default naming.
 * @return 0 on success, negative errno on failure.
 */
int meshbus_channel_set(size_t index, const uint8_t *secret, size_t secret_len,
			const char *name);

/**
 * @brief Get a channel by stable local slot index.
 *
 * @param index Channel slot index.
 * @param[out] channel Channel output.
 * @return 0 on success; -ENOENT when the slot is empty or out of range;
 * negative errno on error.
 */
int meshbus_channel_get(size_t index, meshbus_channel *channel);

/**
 * @brief Reset a channel slot by stable local slot index.
 *
 * @param index Channel slot index.
 * @return 0 on success; -ENOENT when index is out of range; negative errno on
 * error. Empty slots are treated as already reset.
 */
int meshbus_channel_reset(size_t index);

/**
 * @brief Find the next persistent channel matching a 1-byte mesh hash.
 *
 * Current mesh packet header uses 1-byte channel hash. Use @p start_slot as a
 * cursor: pass 0 for the first match, then pass @p slot_id + 1 to continue
 * after a returned match.
 *
 * @param hash Pointer to hash bytes. First byte is used.
 * @param start_slot First stable channel slot index to consider.
 * @param[out] slot_id Stable channel slot index for the returned channel.
 * @param[out] channel Output channel record.
 * @return 0 on success; -ENOENT when no further match exists; negative errno
 * on error.
 */
int meshbus_channel_next_by_hash(const uint8_t *hash, size_t start_slot, size_t *slot_id,
				 meshbus_channel *channel);

/**
 * @brief Get the number of stored channel records currently in use.
 *
 * @return Current channel-store record count.
 */
uint8_t meshbus_channel_store_count(void);

/**
 * @brief Get the configured channel-store capacity.
 *
 * @return Maximum number of channel records supported by the current build.
 */
uint8_t meshbus_channel_store_size(void);

/**
 * @brief Find the first free channel slot index.
 *
 * @return First empty channel slot index, or @ref meshbus_channel_store_size
 * when no slot is free.
 */
uint8_t meshbus_channel_next_free_slot(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MESHBUS_CHANNEL_H_ */
