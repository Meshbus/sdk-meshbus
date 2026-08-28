/**
 * @file compress.h
 * @brief Icon compression/decompression API
 *
 * Provides heatshrink decompression for compressed icons.
 * Format:
 * - Uncompressed: 0x00 + raw XBM data
 * - Compressed:   0x01 0x00 + 2-byte size (LE) + heatshrink data
 *
 * Heatshrink parameters: window_sz2=8, lookahead_sz2=4
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque type for compress icon instance */
typedef void CompressIcon;

/**
 * @brief Allocate a compress icon instance
 * @param size Buffer size hint (ignored, uses CONFIG_ZUI_ICON_DECODE_BUFFER_SIZE)
 * @return Pointer to compress icon instance
 */
CompressIcon *compress_icon_alloc(size_t size);

/**
 * @brief Free a compress icon instance
 * @param icon Instance to free
 */
void compress_icon_free(CompressIcon *icon);

/**
 * @brief Decode icon data (compressed or uncompressed)
 *
 * For compressed icons, the output is written into an internal buffer owned by
 * the CompressIcon instance, and a pointer to that buffer is returned via @p dst.
 *
 * @param icon Compress icon instance (required for compressed data)
 * @param src Source data (with compression header)
 * @param expected_len Expected decoded payload length (bytes)
 * @param dst Output pointer to decoded payload
 * @return true on success, false on failure
 */
bool compress_icon_decode(CompressIcon *icon, const uint8_t *src, size_t src_len,
			  size_t expected_len, uint8_t **dst);

#ifdef __cplusplus
}
#endif
