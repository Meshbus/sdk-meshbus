/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <sys/types.h>

#include <zephyr/app_version.h>
#include <dfu/flash_delta.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

struct patch_read_ctx {
	const uint8_t *data;
	size_t size;
	size_t offset;
};

static int patch_read_cb(void *user_data, uint8_t *buf, size_t size)
{
	struct patch_read_ctx *ctx = user_data;

	if (ctx->offset > ctx->size || size > ctx->size - ctx->offset) {
		return -ENODATA;
	}

	memcpy(buf, &ctx->data[ctx->offset], size);
	ctx->offset += size;
	return 0;
}

static void put_patch_header(uint8_t raw[FLASH_DELTA_PATCH_HEADER_SIZE],
			     const struct flash_delta_patch_version *source,
			     const struct flash_delta_patch_version *target, uint32_t patch_size,
			     uint32_t source_size, uint32_t target_size)
{
	sys_put_le32(FLASH_DELTA_PATCH_MAGIC, &raw[0]);
	raw[4] = source->major;
	raw[5] = source->minor;
	sys_put_le16(source->revision, &raw[6]);
	raw[8] = target->major;
	raw[9] = target->minor;
	sys_put_le16(target->revision, &raw[10]);
	sys_put_le32(patch_size, &raw[12]);
	sys_put_le32(source_size, &raw[16]);
	sys_put_le32(target_size, &raw[20]);
	for (size_t i = 0; i < FLASH_DELTA_IMAGE_HASH_SIZE; i++) {
		raw[24 + i] = (uint8_t)i;
		raw[56 + i] = (uint8_t)(0x80U + i);
	}
}

ZTEST(flash_delta, test_patch_info_decodes_header)
{
	const struct flash_delta_patch_version source = {
		.major = 1,
		.minor = 2,
		.revision = 0x0403,
	};
	const struct flash_delta_patch_version target = {
		.major = 5,
		.minor = 6,
		.revision = 0x0807,
	};
	uint8_t raw[FLASH_DELTA_PATCH_HEADER_SIZE];
	struct patch_read_ctx ctx = {
		.data = raw,
		.size = sizeof(raw),
	};
	struct flash_delta_patch_header header;

	put_patch_header(raw, &source, &target, 0x0c0b0a09, 0x11121314, 0x21222324);

	zassert_ok(flash_delta_patch_info(patch_read_cb, &ctx, &header));
	zassert_equal(ctx.offset, FLASH_DELTA_PATCH_HEADER_SIZE);
	zassert_equal(header.magic, FLASH_DELTA_PATCH_MAGIC);
	zassert_equal(header.source.major, source.major);
	zassert_equal(header.source.minor, source.minor);
	zassert_equal(header.source.revision, source.revision);
	zassert_equal(header.target.major, target.major);
	zassert_equal(header.target.minor, target.minor);
	zassert_equal(header.target.revision, target.revision);
	zassert_equal(header.patch_size, 0x0c0b0a09);
	zassert_equal(header.source_size, 0x11121314);
	zassert_equal(header.target_size, 0x21222324);
	for (size_t i = 0; i < FLASH_DELTA_IMAGE_HASH_SIZE; i++) {
		zassert_equal(header.source_hash[i], (uint8_t)i);
		zassert_equal(header.target_hash[i], (uint8_t)(0x80U + i));
	}
}

ZTEST(flash_delta, test_patch_info_rejects_invalid_magic)
{
	const struct flash_delta_patch_version version = {0};
	uint8_t raw[FLASH_DELTA_PATCH_HEADER_SIZE];
	struct patch_read_ctx ctx = {
		.data = raw,
		.size = sizeof(raw),
	};
	struct flash_delta_patch_header header;

	put_patch_header(raw, &version, &version, 0, 0, 0);
	sys_put_le32(0x21444142, &raw[0]);

	zassert_equal(flash_delta_patch_info(patch_read_cb, &ctx, &header), -EINVAL);
}

ZTEST(flash_delta, test_patch_info_reports_short_read)
{
	uint8_t raw[FLASH_DELTA_PATCH_HEADER_SIZE - 1U] = {0};
	struct patch_read_ctx ctx = {
		.data = raw,
		.size = sizeof(raw),
	};
	struct flash_delta_patch_header header;

	zassert_equal(flash_delta_patch_info(patch_read_cb, &ctx, &header), -ENODATA);
}

ZTEST(flash_delta, test_patch_apply_rejects_size_mismatch_before_flash)
{
	const struct flash_delta_patch_version source = {
		.major = APP_VERSION_MAJOR,
		.minor = APP_VERSION_MINOR,
		.revision = APP_PATCHLEVEL,
	};
	const struct flash_delta_patch_version target = {
		.major = APP_VERSION_MAJOR,
		.minor = APP_VERSION_MINOR,
		.revision = APP_PATCHLEVEL + 1,
	};
	uint8_t raw[FLASH_DELTA_PATCH_HEADER_SIZE + 5U] = {0};
	struct patch_read_ctx ctx = {
		.data = raw,
		.size = sizeof(raw),
	};

	put_patch_header(raw, &source, &target, 4, 64, 64);

	zassert_equal(flash_delta_patch_apply(patch_read_cb, &ctx, sizeof(raw)), -EINVAL);
}

ZTEST(flash_delta, test_patch_apply_rejects_too_small_size)
{
	uint8_t raw[FLASH_DELTA_PATCH_HEADER_SIZE] = {0};
	struct patch_read_ctx ctx = {
		.data = raw,
		.size = sizeof(raw),
	};

	zassert_equal(flash_delta_patch_apply(patch_read_cb, &ctx,
					      FLASH_DELTA_PATCH_HEADER_SIZE),
		      -EINVAL);
	zassert_equal(ctx.offset, 0);
}

ZTEST(flash_delta, test_patch_apply_rejects_source_version_mismatch)
{
	const struct flash_delta_patch_version source = {
		.major = APP_VERSION_MAJOR + 1,
		.minor = APP_VERSION_MINOR,
		.revision = APP_PATCHLEVEL,
	};
	const struct flash_delta_patch_version target = {
		.major = APP_VERSION_MAJOR,
		.minor = APP_VERSION_MINOR,
		.revision = APP_PATCHLEVEL + 1,
	};
	uint8_t raw[FLASH_DELTA_PATCH_HEADER_SIZE + 4U] = {0};
	struct patch_read_ctx ctx = {
		.data = raw,
		.size = sizeof(raw),
	};

	put_patch_header(raw, &source, &target, sizeof(raw) - FLASH_DELTA_PATCH_HEADER_SIZE,
			 64, 64);

	zassert_equal(flash_delta_patch_apply(patch_read_cb, &ctx, sizeof(raw)), -ENOTSUP);
}

ZTEST_SUITE(flash_delta, NULL, NULL, NULL, NULL, NULL);
