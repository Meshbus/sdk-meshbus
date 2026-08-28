/**
 * @file compress.c
 * @brief Icon compression/decompression support
 *
 * Implements heatshrink decompression for compressed icons.
 */

#include "zui_compress.h"
#include <zephyr/kernel.h>

#ifdef CONFIG_ZUI_COMPRESS_ICONS

#include <heatshrink_decoder.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>

#include "zui_mem.h"

struct compress_icon_instance {
	struct k_mutex mutex;
	heatshrink_decoder decoder;
	size_t buffer_size;
	uint8_t buffer[];
};

CompressIcon *compress_icon_alloc(size_t size)
{
	if (size == 0) {
		size = CONFIG_ZUI_ICON_DECODE_BUFFER_SIZE;
	}

	struct compress_icon_instance *inst = zui_malloc(sizeof(*inst) + size);
	if (!inst) {
		return NULL;
	}

	k_mutex_init(&inst->mutex);
	inst->buffer_size = size;
	heatshrink_decoder_reset(&inst->decoder);

	return (CompressIcon *)inst;
}

void compress_icon_free(CompressIcon *icon)
{
	zui_free(icon);
}

bool compress_icon_decode(CompressIcon *instance, const uint8_t *src, size_t src_len,
			  size_t expected_len, uint8_t **dst)
{
	bool ok = false;

	if (!src || !dst) {
		return false;
	}

	*dst = NULL;

	if (expected_len == 0 || src_len == 0) {
		return false;
	}

	/* Handle framed uncompressed icons: 0x00 + raw data */
	if (src[0] == 0x00 && src_len == expected_len + 1U) {
		*dst = (uint8_t *)&src[1];
		return true;
	}

	/* Strict compressed header: 0x01 0x00 + 2-byte size (LE) + data */
	if (src_len < 4U || src[0] != 0x01 || src[1] != 0x00 ||
	    sys_get_le16(&src[2]) != src_len - 4U) {
		if (src_len == expected_len) {
			*dst = (uint8_t *)src;
			return true;
		}
		return false;
	}

	/* Fallback if instance is NULL */
	if (!instance) {
		return false;
	}

	uint16_t compressed_size = sys_get_le16(&src[2]);
	const uint8_t *compressed_data = &src[4];

	struct compress_icon_instance *inst = (void *)instance;

	if (expected_len > inst->buffer_size) {
		return false;
	}

	k_mutex_lock(&inst->mutex, K_FOREVER);

	/* Reset decoder for new decompression */
	heatshrink_decoder_reset(&inst->decoder);

	/*
	 * Always clear output buffer. This makes error cases deterministic and avoids
	 * accidentally rendering stale data from a previous successful decode.
	 */
	memset(inst->buffer, 0, inst->buffer_size);

	size_t sunk = 0;
	size_t total_output = 0;
	const size_t buffer_size = inst->buffer_size;

	/* Sink and poll in a loop */
	while (sunk < compressed_size) {
		/* Sink more data */
		size_t count = 0;
		HSD_sink_res sres =
			heatshrink_decoder_sink(&inst->decoder, (uint8_t *)&compressed_data[sunk],
						compressed_size - sunk, &count);
		if (sres < 0) {
			goto out;
		}
		if (count == 0U && sres != HSDR_SINK_FULL) {
			goto out;
		}
		sunk += count;

		/* Poll for output */
		HSD_poll_res pres;
		do {
			size_t out_count = 0;
			if (total_output >= buffer_size) {
				goto out;
			}
			pres = heatshrink_decoder_poll(&inst->decoder, &inst->buffer[total_output],
						       buffer_size - total_output, &out_count);
			if (pres < 0) {
				goto out;
			}
			total_output += out_count;
			if (total_output > expected_len) {
				goto out;
			}
		} while (pres == HSDR_POLL_MORE);
	}

	/* Finish decompression */
	HSD_finish_res fres = heatshrink_decoder_finish(&inst->decoder);

	while (fres == HSDR_FINISH_MORE) {
		HSD_poll_res pres;
		size_t out_count = 0;

		if (total_output >= buffer_size) {
			goto out;
		}
		pres = heatshrink_decoder_poll(&inst->decoder, &inst->buffer[total_output],
					       buffer_size - total_output, &out_count);
		if (pres < 0) {
			goto out;
		}
		if (out_count == 0U && pres != HSDR_POLL_MORE) {
			goto out;
		}
		total_output += out_count;
		if (total_output > expected_len) {
			goto out;
		}
		fres = heatshrink_decoder_finish(&inst->decoder);
	}
	if (fres != HSDR_FINISH_DONE) {
		goto out;
	}

out:
	if (total_output == expected_len) {
		*dst = inst->buffer;
		ok = true;
	}
	k_mutex_unlock(&inst->mutex);

	return ok;
}

#else /* !CONFIG_ZUI_COMPRESS_ICONS */

CompressIcon *compress_icon_alloc(size_t size)
{
	ARG_UNUSED(size);
	return NULL;
}

void compress_icon_free(CompressIcon *icon)
{
	ARG_UNUSED(icon);
}

bool compress_icon_decode(CompressIcon *icon, const uint8_t *src, size_t src_len,
			  size_t expected_len, uint8_t **dst)
{
	ARG_UNUSED(icon);
	if (!src || !dst) {
		return false;
	}

	*dst = NULL;

	if (expected_len == 0U || src_len == 0U) {
		return false;
	}

	if (src[0] == 0x00 && src_len == expected_len + 1U) {
		*dst = (uint8_t *)&src[1];
		return true;
	}

	if (src_len >= 4U && src[0] == 0x01 && src[1] == 0x00 &&
	    (uint16_t)(src[2] | ((uint16_t)src[3] << 8)) == src_len - 4U) {
		return false;
	}

	if (src_len == expected_len) {
		*dst = (uint8_t *)src;
		return true;
	}

	/* Compressed data not supported in this configuration */
	return false;
}

#endif /* CONFIG_ZUI_COMPRESS_ICONS */
