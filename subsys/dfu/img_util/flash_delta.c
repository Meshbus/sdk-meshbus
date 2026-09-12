/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <dfu/flash_delta.h>
#include <detools.h>

#include <bootutil/image.h>

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/app_version.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <psa/crypto.h>
#include <string.h>

LOG_MODULE_REGISTER(flash_delta, CONFIG_IMG_MANAGER_LOG_LEVEL);

#define PRIMARY_AREA_ID   PARTITION_ID(slot0_partition)
#define SECONDARY_AREA_ID PARTITION_ID(slot1_partition)

BUILD_ASSERT(sizeof(struct flash_delta_patch_header) == FLASH_DELTA_PATCH_HEADER_SIZE);

/* Internal context for delta patching */
struct delta_context {
	const struct flash_area *from_fa;
	const struct flash_area *to_fa;
	flash_delta_patch_read_cb_t patch_read_cb;
	void *patch_user_data;
	off_t from_offset;
	off_t from_end;
	off_t image_start;
	off_t image_end;
	off_t write_offset;
	uint8_t write_buf[CONFIG_IMG_BLOCK_BUF_SIZE];
	size_t write_buf_used;
	size_t write_block_size;
	size_t bytes_written;
	uint8_t erased_val;
	int error;
};

static K_MUTEX_DEFINE(flash_delta_lock);

struct delta_image_header {
	uint32_t magic;
	uint16_t header_size;
	uint32_t image_size;
	struct flash_delta_patch_version version;
};

static int delta_set_error(struct delta_context *ctx, int error)
{
	if (ctx->error == 0) {
		ctx->error = error;
	}

	return error;
}

static size_t align_up_size(size_t value, size_t align)
{
	return ((value + align - 1U) / align) * align;
}

static bool off_size_exceeds(off_t offset, size_t size, off_t limit)
{
	if (offset < 0 || limit < offset) {
		return true;
	}

	return size > (size_t)(limit - offset);
}

static void patch_header_decode(const uint8_t raw[FLASH_DELTA_PATCH_HEADER_SIZE],
				struct flash_delta_patch_header *header)
{
	header->magic = sys_get_le32(&raw[0]);
	header->source.major = raw[4];
	header->source.minor = raw[5];
	header->source.revision = sys_get_le16(&raw[6]);
	header->target.major = raw[8];
	header->target.minor = raw[9];
	header->target.revision = sys_get_le16(&raw[10]);
	header->patch_size = sys_get_le32(&raw[12]);
	header->source_size = sys_get_le32(&raw[16]);
	header->target_size = sys_get_le32(&raw[20]);
	memcpy(header->source_hash, &raw[24], sizeof(header->source_hash));
	memcpy(header->target_hash, &raw[56], sizeof(header->target_hash));
}

static int patch_header_read(flash_delta_patch_read_cb_t read_cb, void *user_data,
			     struct flash_delta_patch_header *header)
{
	uint8_t raw[FLASH_DELTA_PATCH_HEADER_SIZE];
	int ret;

	ret = read_cb(user_data, raw, sizeof(raw));
	if (ret != 0) {
		return ret < 0 ? ret : -EIO;
	}

	patch_header_decode(raw, header);
	return 0;
}

static void parse_mcuboot_header(const uint8_t raw[IMAGE_HEADER_SIZE],
				 struct delta_image_header *header)
{
	header->magic = sys_get_le32(&raw[0]);
	header->header_size = sys_get_le16(&raw[8]);
	header->image_size = sys_get_le32(&raw[12]);
	header->version.major = raw[20];
	header->version.minor = raw[21];
	header->version.revision = sys_get_le16(&raw[22]);
}

static bool patch_version_equal(const struct flash_delta_patch_version *lhs,
				const struct flash_delta_patch_version *rhs)
{
	return lhs->major == rhs->major && lhs->minor == rhs->minor &&
	       lhs->revision == rhs->revision;
}

/*
 * Detools callbacks
 */

static int delta_flush_write(struct delta_context *ctx, bool pad)
{
	size_t write_size;
	int ret;

	if (ctx->write_buf_used == 0U) {
		return 0;
	}

	write_size = ctx->write_buf_used;
	if (pad) {
		size_t padded_size = align_up_size(write_size, ctx->write_block_size);

		memset(&ctx->write_buf[write_size], ctx->erased_val, padded_size - write_size);
		write_size = padded_size;
	}

	if ((write_size % ctx->write_block_size) != 0U) {
		LOG_ERR("Unaligned flash write: size=%zu align=%zu", write_size,
			ctx->write_block_size);
		return delta_set_error(ctx, -EINVAL);
	}

	if (off_size_exceeds(ctx->write_offset, write_size, ctx->image_end)) {
		LOG_ERR("Slot1 out of space");
		return delta_set_error(ctx, -ENOSPC);
	}

	ret = flash_area_write(ctx->to_fa, ctx->write_offset, ctx->write_buf, write_size);
	if (ret != 0) {
		LOG_ERR("Slot1 write failed at 0x%lx: %d", (long)ctx->write_offset, ret);
		return delta_set_error(ctx, ret < 0 ? ret : -EIO);
	}

	ctx->write_offset += (off_t)write_size;
	ctx->write_buf_used = 0U;
	return 0;
}

static int cb_flash_write(void *arg, const uint8_t *buf, size_t size)
{
	struct delta_context *ctx = arg;
	size_t capacity = (size_t)(ctx->image_end - ctx->image_start);

	if (ctx->bytes_written > capacity || size > capacity - ctx->bytes_written) {
		LOG_ERR("Slot1 out of space");
		return delta_set_error(ctx, -ENOSPC);
	}

	while (size > 0U) {
		size_t chunk = MIN(sizeof(ctx->write_buf) - ctx->write_buf_used, size);

		memcpy(&ctx->write_buf[ctx->write_buf_used], buf, chunk);
		ctx->write_buf_used += chunk;
		ctx->bytes_written += chunk;
		buf += chunk;
		size -= chunk;

		if (ctx->write_buf_used == sizeof(ctx->write_buf)) {
			int ret = delta_flush_write(ctx, false);

			if (ret != 0) {
				return ret;
			}
		}
	}

	return 0;
}

static int cb_flash_read(void *arg, uint8_t *buf, size_t size)
{
	struct delta_context *ctx = arg;

	if (off_size_exceeds(ctx->from_offset, size, ctx->from_end)) {
		LOG_ERR("Slot0 read overflow");
		return delta_set_error(ctx, -EIO);
	}

	if (flash_area_read(ctx->from_fa, ctx->from_offset, buf, size) != 0) {
		return delta_set_error(ctx, -EIO);
	}

	ctx->from_offset += (off_t)size;
	return 0;
}

static int cb_patch_read(void *arg, uint8_t *buf, size_t size)
{
	struct delta_context *ctx = arg;
	int ret;

	ret = ctx->patch_read_cb(ctx->patch_user_data, buf, size);
	if (ret != 0) {
		return delta_set_error(ctx, ret < 0 ? ret : -EIO);
	}

	return 0;
}

static int cb_flash_seek(void *arg, int offset)
{
	struct delta_context *ctx = arg;
	int64_t new_pos = (int64_t)ctx->from_offset + offset;

	if (new_pos < 0 || new_pos > ctx->from_end) {
		LOG_ERR("Slot0 seek out of bounds: %lld", (long long)new_pos);
		return delta_set_error(ctx, -EINVAL);
	}

	ctx->from_offset = (off_t)new_pos;
	return 0;
}

/*
 * Internal: Initialize flash context.
 */
static int delta_open_flash(struct delta_context *ctx)
{
	size_t image_start;
	ssize_t trailer_offset;
	int ret;

	ret = flash_area_open(PRIMARY_AREA_ID, &ctx->from_fa);
	if (ret != 0) {
		LOG_ERR("Failed to open slot0: %d", ret);
		return ret;
	}

	ret = flash_area_open(SECONDARY_AREA_ID, &ctx->to_fa);
	if (ret != 0) {
		LOG_ERR("Failed to open slot1: %d", ret);
		flash_area_close(ctx->from_fa);
		ctx->from_fa = NULL;
		return ret;
	}

	image_start = boot_get_image_start_offset(SECONDARY_AREA_ID);
	trailer_offset = boot_get_trailer_status_offset(ctx->to_fa->fa_size);
	if (trailer_offset < 0 || image_start >= (size_t)trailer_offset) {
		LOG_ERR("Invalid slot1 image window: start=0x%zx trailer=0x%zx", image_start,
			(size_t)trailer_offset);
		return -ENOSPC;
	}

	ctx->from_offset = 0;
	ctx->from_end = (off_t)ctx->from_fa->fa_size;
	ctx->image_start = (off_t)image_start;
	ctx->image_end = (off_t)trailer_offset;
	ctx->write_offset = ctx->image_start;
	ctx->write_block_size = flash_area_align(ctx->to_fa);
	ctx->erased_val = flash_area_erased_val(ctx->to_fa);

	if (ctx->write_block_size == 0U || ctx->write_block_size > sizeof(ctx->write_buf) ||
	    (sizeof(ctx->write_buf) % ctx->write_block_size) != 0U) {
		LOG_ERR("Unsupported flash write alignment: %zu (buffer %zu)",
			ctx->write_block_size, sizeof(ctx->write_buf));
		return -EINVAL;
	}

	LOG_DBG("Slot0: size=0x%zx, Slot1 image: 0x%lx-0x%lx align=%zu",
		ctx->from_fa->fa_size, (long)ctx->image_start, (long)ctx->image_end,
		ctx->write_block_size);

	return 0;
}

static void delta_close_flash(struct delta_context *ctx)
{
	if (ctx->from_fa != NULL) {
		flash_area_close(ctx->from_fa);
		ctx->from_fa = NULL;
	}

	if (ctx->to_fa != NULL) {
		flash_area_close(ctx->to_fa);
		ctx->to_fa = NULL;
	}
}

static int delta_prepare_secondary(struct delta_context *ctx)
{
	int ret;

	LOG_DBG("Erasing slot1: size=0x%zx", ctx->to_fa->fa_size);

	ret = flash_area_flatten(ctx->to_fa, 0, ctx->to_fa->fa_size);
	if (ret != 0) {
		LOG_ERR("Failed to erase slot1: %d", ret);
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

/*
 * Internal: Read and verify patch header.
 */
static int delta_verify_header(struct delta_context *ctx, struct flash_delta_patch_header *header)
{
	int ret;

	ret = patch_header_read(ctx->patch_read_cb, ctx->patch_user_data, header);
	if (ret != 0) {
		LOG_ERR("Failed to read patch header: %d", ret);
		return ret;
	}

	if (header->magic != FLASH_DELTA_PATCH_MAGIC) {
		LOG_ERR("Invalid patch magic: 0x%08x", header->magic);
		return -EINVAL;
	}

	LOG_INF("Patch: v%u.%u.%u -> v%u.%u.%u (%u bytes)", header->source.major,
		header->source.minor, header->source.revision, header->target.major,
		header->target.minor, header->target.revision, header->patch_size);

	/* Verify source version matches running firmware */
	if (APP_VERSION_MAJOR != header->source.major ||
	    APP_VERSION_MINOR != header->source.minor ||
	    APP_PATCHLEVEL != header->source.revision) {
		LOG_ERR("Version mismatch: running v%u.%u.%u, patch requires v%u.%u.%u",
			APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_PATCHLEVEL, header->source.major,
			header->source.minor, header->source.revision);
		return -ENOTSUP;
	}

	return 0;
}

/*
 * Internal: Verify MCUboot header in a flash slot.
 */
static int delta_validate_image_header(const uint8_t raw[IMAGE_HEADER_SIZE], const char *slot_name,
				       const struct flash_delta_patch_version *expected_version,
				       size_t max_size)
{
	struct delta_image_header parsed;
	uint64_t image_end;

	parse_mcuboot_header(raw, &parsed);

	LOG_DBG("%s: magic=0x%08x hdr=%u size=%u ver=v%u.%u.%u", slot_name, parsed.magic,
		parsed.header_size, parsed.image_size, parsed.version.major, parsed.version.minor,
		parsed.version.revision);

	if (parsed.magic != IMAGE_MAGIC) {
		LOG_ERR("%s: invalid magic 0x%08x", slot_name, parsed.magic);
		return -EINVAL;
	}

	if (parsed.header_size < IMAGE_HEADER_SIZE || parsed.image_size == 0U) {
		LOG_ERR("%s: invalid image header fields", slot_name);
		return -EINVAL;
	}

	image_end = (uint64_t)parsed.header_size + parsed.image_size;
	if (image_end > max_size) {
		LOG_ERR("%s: image exceeds available data (%llu > %zu)", slot_name,
			(unsigned long long)image_end, max_size);
		return -ENOSPC;
	}

	if (expected_version != NULL && !patch_version_equal(&parsed.version, expected_version)) {
		LOG_ERR("%s: version mismatch v%u.%u.%u != v%u.%u.%u", slot_name,
			parsed.version.major, parsed.version.minor, parsed.version.revision,
			expected_version->major, expected_version->minor,
			expected_version->revision);
		return -ENOTSUP;
	}

	return 0;
}

static int delta_verify_slot_header(const struct flash_area *fa, off_t offset, const char *slot_name,
				    const struct flash_delta_patch_version *expected_version,
				    size_t max_size)
{
	uint8_t hdr[IMAGE_HEADER_SIZE];

	if (flash_area_read(fa, offset, hdr, sizeof(hdr)) != 0) {
		LOG_ERR("Failed to read %s header", slot_name);
		return -EIO;
	}

	return delta_validate_image_header(hdr, slot_name, expected_version, max_size);
}

static int delta_hash_flash_area(const struct flash_area *fa, off_t offset, size_t size,
				 uint8_t hash[FLASH_DELTA_IMAGE_HASH_SIZE])
{
	psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
	uint8_t buf[CONFIG_IMG_BLOCK_BUF_SIZE];
	size_t output_len;
	psa_status_t status;
	off_t pos = offset;

	if (off_size_exceeds(offset, size, (off_t)fa->fa_size)) {
		return -EINVAL;
	}

	status = psa_hash_setup(&op, PSA_ALG_SHA_256);
	if (status != PSA_SUCCESS) {
		return -EIO;
	}

	while (size > 0U) {
		size_t chunk = MIN(size, sizeof(buf));

		if (flash_area_read(fa, pos, buf, chunk) != 0) {
			psa_hash_abort(&op);
			return -EIO;
		}

		status = psa_hash_update(&op, buf, chunk);
		if (status != PSA_SUCCESS) {
			psa_hash_abort(&op);
			return -EIO;
		}

		pos += (off_t)chunk;
		size -= chunk;
	}

	status = psa_hash_finish(&op, hash, FLASH_DELTA_IMAGE_HASH_SIZE, &output_len);
	psa_hash_abort(&op);
	if (status != PSA_SUCCESS || output_len != FLASH_DELTA_IMAGE_HASH_SIZE) {
		return -EIO;
	}

	return 0;
}

static int delta_verify_image_hash(const struct flash_area *fa, off_t offset, size_t size,
				   const uint8_t expected[FLASH_DELTA_IMAGE_HASH_SIZE],
				   const char *slot_name)
{
	uint8_t actual[FLASH_DELTA_IMAGE_HASH_SIZE];
	int ret;

	ret = delta_hash_flash_area(fa, offset, size, actual);
	if (ret != 0) {
		LOG_ERR("%s: hash calculation failed: %d", slot_name, ret);
		return ret;
	}

	if (memcmp(actual, expected, sizeof(actual)) != 0) {
		LOG_ERR("%s: image hash mismatch", slot_name);
		return -EILSEQ;
	}

	return 0;
}

static int delta_validate_patch_size(const struct flash_delta_patch_header *header, size_t patch_size)
{
	size_t expected_patch_size;

	if (patch_size <= FLASH_DELTA_PATCH_HEADER_SIZE) {
		LOG_ERR("Patch size too small: %zu", patch_size);
		return -EINVAL;
	}

	expected_patch_size = patch_size - FLASH_DELTA_PATCH_HEADER_SIZE;
	if (expected_patch_size > UINT32_MAX) {
		LOG_ERR("Patch size too large: %zu", expected_patch_size);
		return -EINVAL;
	}

	if (header->patch_size != (uint32_t)expected_patch_size) {
		LOG_ERR("Patch size mismatch: header=%u actual=%zu", header->patch_size,
			expected_patch_size);
		return -EINVAL;
	}

	if (header->source_size == 0U || header->target_size == 0U) {
		LOG_ERR("Invalid image size in patch header");
		return -EINVAL;
	}

	return 0;
}

static int delta_map_detools_error(const struct delta_context *ctx, int ret)
{
	if (ctx->error != 0) {
		return ctx->error;
	}

	if (ret == 0) {
		return -EIO;
	}

	switch (-ret) {
	case DETOOLS_OUT_OF_MEMORY:
		return -ENOMEM;
	case DETOOLS_NOT_ENOUGH_PATCH_DATA:
	case DETOOLS_SHORT_HEADER:
		return -ENODATA;
	case DETOOLS_IO_FAILED:
	case DETOOLS_FILE_READ_FAILED:
	case DETOOLS_FILE_WRITE_FAILED:
	case DETOOLS_FILE_SEEK_FAILED:
		return -EIO;
	default:
		return -EINVAL;
	}
}

/*
 * Public API
 */

int flash_delta_patch_info(flash_delta_patch_read_cb_t read_cb, void *user_data,
			   struct flash_delta_patch_header *header)
{
	int ret;

	if (read_cb == NULL || header == NULL) {
		return -EINVAL;
	}

	ret = patch_header_read(read_cb, user_data, header);
	if (ret != 0) {
		LOG_ERR("Failed to read patch header: %d", ret);
		return ret;
	}

	if (header->magic != FLASH_DELTA_PATCH_MAGIC) {
		LOG_ERR("Invalid patch magic: 0x%08x", header->magic);
		return -EINVAL;
	}

	LOG_INF("Patch info: v%u.%u.%u -> v%u.%u.%u (%u bytes)", header->source.major,
		header->source.minor, header->source.revision, header->target.major,
		header->target.minor, header->target.revision, header->patch_size);

	return 0;
}

int flash_delta_patch_prepare(flash_delta_patch_read_cb_t read_cb, void *user_data,
			      size_t patch_size)
{
	static struct delta_context ctx;
	static struct flash_delta_patch_header header;
	int ret;

	if (read_cb == NULL) {
		return -EINVAL;
	}

	if (patch_size <= FLASH_DELTA_PATCH_HEADER_SIZE) {
		LOG_ERR("Patch size too small: %zu", patch_size);
		return -EINVAL;
	}

	k_mutex_lock(&flash_delta_lock, K_FOREVER);

	memset(&ctx, 0, sizeof(ctx));
	memset(&header, 0, sizeof(header));

	ctx.patch_read_cb = read_cb;
	ctx.patch_user_data = user_data;

	LOG_INF("Delta update: %zu bytes", patch_size);

	/* Step 1: Read and verify patch header */
	ret = delta_verify_header(&ctx, &header);
	if (ret != 0) {
		goto out;
	}

	ret = delta_validate_patch_size(&header, patch_size);
	if (ret != 0) {
		goto out;
	}

	/* Step 2: Initialize flash areas and verify source slot */
	ret = delta_open_flash(&ctx);
	if (ret != 0) {
		delta_close_flash(&ctx);
		goto out;
	}

	ret = delta_verify_slot_header(ctx.from_fa, 0, "Slot0", &header.source,
				       ctx.from_fa->fa_size);
	if (ret != 0) {
		goto close_flash;
	}

	ret = delta_verify_image_hash(ctx.from_fa, 0, header.source_size, header.source_hash,
				      "Slot0");
	if (ret != 0) {
		goto close_flash;
	}

	/* Step 3: Erase slot1 before writing the reconstructed image */
	ret = delta_prepare_secondary(&ctx);
	if (ret != 0) {
		goto close_flash;
	}

	/* Step 4: Apply patch via detools */
	LOG_INF("Applying patch...");
	ret = detools_apply_patch_callbacks(cb_flash_read, cb_flash_seek, cb_patch_read,
					    (size_t)header.patch_size, cb_flash_write, &ctx);

	if (ret <= 0) {
		LOG_ERR("Patch failed: %d", ret);
		ret = delta_map_detools_error(&ctx, ret);
		goto close_flash;
	}

	if ((size_t)ret != ctx.bytes_written) {
		LOG_ERR("Patch output mismatch: detools=%d written=%zu", ret, ctx.bytes_written);
		ret = -EIO;
		goto close_flash;
	}

	if (ctx.bytes_written != header.target_size) {
		LOG_ERR("Patch output size mismatch: header=%u written=%zu", header.target_size,
			ctx.bytes_written);
		ret = -EILSEQ;
		goto close_flash;
	}

	ret = delta_flush_write(&ctx, true);
	if (ret != 0) {
		goto close_flash;
	}

	LOG_INF("Patch complete: %zu bytes written", ctx.bytes_written);

	/* Step 5: Verify new image in slot1 */
	ret = delta_verify_slot_header(ctx.to_fa, ctx.image_start, "Slot1", &header.target,
				       ctx.bytes_written);
	if (ret != 0) {
		goto close_flash;
	}

	ret = delta_verify_image_hash(ctx.to_fa, ctx.image_start, header.target_size,
				      header.target_hash, "Slot1");
	if (ret != 0) {
		goto close_flash;
	}

	LOG_INF("Delta image prepared and verified; upgrade not armed");
	ret = 0;

close_flash:
	delta_close_flash(&ctx);
out:
	k_mutex_unlock(&flash_delta_lock);
	return ret;
}

int flash_delta_patch_arm(void)
{
	int ret;

	k_mutex_lock(&flash_delta_lock, K_FOREVER);
	ret = boot_request_upgrade(BOOT_UPGRADE_TEST);
	k_mutex_unlock(&flash_delta_lock);
	if (ret != 0) {
		LOG_ERR("boot_request_upgrade failed: %d", ret);
		return -EIO;
	}
	LOG_INF("Delta upgrade test ready.");
	return 0;
}

int flash_delta_patch_validate_candidate(
	const uint8_t public_key[32], uint32_t expected_security_counter,
	flash_delta_ed25519_verify_cb_t verify_cb)
{
	static const uint8_t ed25519_spki_prefix[] = {
		0x30, 0x2a, 0x30, 0x05, 0x06, 0x03,
		0x2b, 0x65, 0x70, 0x03, 0x21, 0x00,
	};
	const struct flash_area *fa = NULL;
	struct image_header header;
	struct image_tlv_info info;
	struct image_tlv tlv;
	uint8_t digest[FLASH_DELTA_IMAGE_HASH_SIZE];
	uint8_t stored_digest[FLASH_DELTA_IMAGE_HASH_SIZE] = {0};
	uint8_t key_hash[FLASH_DELTA_IMAGE_HASH_SIZE] = {0};
	uint8_t stored_key_hash[FLASH_DELTA_IMAGE_HASH_SIZE] = {0};
	uint8_t signature[64] = {0};
	uint8_t der_key[sizeof(ed25519_spki_prefix) + 32];
	uint32_t security_counter = UINT32_MAX;
	size_t image_start;
	size_t protected_size;
	size_t off;
	size_t end;
	bool have_digest = false;
	bool have_key_hash = false;
	bool have_signature = false;
	bool have_counter = false;
	int ret;

	if (public_key == NULL || verify_cb == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&flash_delta_lock, K_FOREVER);
	ret = flash_area_open(SECONDARY_AREA_ID, &fa);
	if (ret != 0) {
		goto out;
	}
	image_start = boot_get_image_start_offset(SECONDARY_AREA_ID);
	ret = flash_area_read(fa, image_start, &header, sizeof(header));
	if (ret != 0 || header.ih_magic != IMAGE_MAGIC ||
	    header.ih_hdr_size < IMAGE_HEADER_SIZE) {
		ret = -EINVAL;
		goto out;
	}
	protected_size = (size_t)header.ih_hdr_size + header.ih_img_size +
			 header.ih_protect_tlv_size;
	if (off_size_exceeds((off_t)image_start, protected_size,
			     (off_t)fa->fa_size)) {
		ret = -ENOSPC;
		goto out;
	}
	ret = delta_hash_flash_area(fa, image_start, protected_size, digest);
	if (ret != 0) {
		goto out;
	}

	if (header.ih_protect_tlv_size < sizeof(info)) {
		ret = -EINVAL;
		goto out;
	}
	off = image_start + header.ih_hdr_size + header.ih_img_size;
	ret = flash_area_read(fa, off, &info, sizeof(info));
	if (ret != 0 || info.it_magic != IMAGE_TLV_PROT_INFO_MAGIC ||
	    info.it_tlv_tot != header.ih_protect_tlv_size) {
		ret = -EINVAL;
		goto out;
	}
	end = off + info.it_tlv_tot;
	for (off += sizeof(info); off + sizeof(tlv) <= end; off += tlv.it_len) {
		ret = flash_area_read(fa, off, &tlv, sizeof(tlv));
		if (ret != 0 || tlv.it_len > end - off - sizeof(tlv)) {
			ret = -EINVAL;
			goto out;
		}
		off += sizeof(tlv);
		if (tlv.it_type == IMAGE_TLV_SEC_CNT && tlv.it_len == sizeof(security_counter)) {
			ret = flash_area_read(fa, off, &security_counter,
					      sizeof(security_counter));
			have_counter = ret == 0;
		}
	}
	if (!have_counter || security_counter != expected_security_counter) {
		ret = -EPERM;
		goto out;
	}

	off = image_start + protected_size;
	ret = flash_area_read(fa, off, &info, sizeof(info));
	if (ret != 0 || info.it_magic != IMAGE_TLV_INFO_MAGIC ||
	    info.it_tlv_tot < sizeof(info) ||
	    off_size_exceeds((off_t)off, info.it_tlv_tot, (off_t)fa->fa_size)) {
		ret = -EINVAL;
		goto out;
	}
	end = off + info.it_tlv_tot;
	for (off += sizeof(info); off + sizeof(tlv) <= end; off += tlv.it_len) {
		ret = flash_area_read(fa, off, &tlv, sizeof(tlv));
		if (ret != 0 || tlv.it_len > end - off - sizeof(tlv)) {
			ret = -EINVAL;
			goto out;
		}
		off += sizeof(tlv);
		if (tlv.it_type == IMAGE_TLV_SHA256 && tlv.it_len == sizeof(stored_digest)) {
			ret = flash_area_read(fa, off, stored_digest, sizeof(stored_digest));
			have_digest = ret == 0;
		} else if (tlv.it_type == IMAGE_TLV_KEYHASH && tlv.it_len == sizeof(stored_key_hash)) {
			ret = flash_area_read(fa, off, stored_key_hash, sizeof(stored_key_hash));
			have_key_hash = ret == 0;
		} else if (tlv.it_type == IMAGE_TLV_ED25519 && tlv.it_len == sizeof(signature)) {
			ret = flash_area_read(fa, off, signature, sizeof(signature));
			have_signature = ret == 0;
		}
		if (ret != 0) {
			goto out;
		}
	}
	memcpy(der_key, ed25519_spki_prefix, sizeof(ed25519_spki_prefix));
	memcpy(der_key + sizeof(ed25519_spki_prefix), public_key, 32);
	{
		size_t key_hash_len = 0U;
		psa_status_t status = psa_hash_compute(
			PSA_ALG_SHA_256, der_key, sizeof(der_key), key_hash,
			sizeof(key_hash), &key_hash_len);

		ret = status == PSA_SUCCESS && key_hash_len == sizeof(key_hash)
			      ? 0 : -EIO;
	}
	if (ret != 0 || !have_digest || !have_key_hash || !have_signature ||
	    memcmp(digest, stored_digest, sizeof(digest)) != 0 ||
	    memcmp(key_hash, stored_key_hash, sizeof(key_hash)) != 0) {
		ret = -EKEYREJECTED;
		goto out;
	}
	ret = verify_cb(public_key, signature, digest, sizeof(digest));

out:
	if (fa != NULL) {
		flash_area_close(fa);
	}
	k_mutex_unlock(&flash_delta_lock);
	return ret;
}

int flash_delta_patch_apply(flash_delta_patch_read_cb_t read_cb, void *user_data,
			    size_t patch_size)
{
	int ret = flash_delta_patch_prepare(read_cb, user_data, patch_size);

	return ret == 0 ? flash_delta_patch_arm() : ret;
}
