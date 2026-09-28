/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <psa/crypto.h>
#include <zephyr/app_version.h>
#include <zephyr/sys/util.h>

#include "firmware_priv.h"

/* Supplied by the already required MeshCore protocol crypto library. */
extern int crypto_ed25519_check(const uint8_t signature[64],
				const uint8_t public_key[32],
				const uint8_t *message, size_t message_size);

struct cbor_cursor {
	const uint8_t *data;
	size_t size;
	size_t pos;
};

static int cbor_head(struct cbor_cursor *c, uint8_t expected_major,
		     uint32_t *value)
{
	uint8_t head;
	uint8_t ai;
	uint32_t v;

	if (c == NULL || value == NULL || c->pos >= c->size) {
		return -ENODATA;
	}
	head = c->data[c->pos++];
	if ((head >> 5) != expected_major) {
		return -EINVAL;
	}
	ai = head & 0x1fU;
	if (ai < 24U) {
		v = ai;
	} else if (ai == 24U) {
		if (c->pos >= c->size) {
			return -ENODATA;
		}
		v = c->data[c->pos++];
		if (v < 24U) {
			return -EINVAL;
		}
	} else if (ai == 25U) {
		if (c->size - c->pos < 2U) {
			return -ENODATA;
		}
		v = ((uint32_t)c->data[c->pos] << 8) | c->data[c->pos + 1U];
		c->pos += 2U;
		if (v <= UINT8_MAX) {
			return -EINVAL;
		}
	} else if (ai == 26U) {
		if (c->size - c->pos < 4U) {
			return -ENODATA;
		}
		v = ((uint32_t)c->data[c->pos] << 24) |
		    ((uint32_t)c->data[c->pos + 1U] << 16) |
		    ((uint32_t)c->data[c->pos + 2U] << 8) |
		    c->data[c->pos + 3U];
		c->pos += 4U;
		if (v <= UINT16_MAX) {
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}
	*value = v;
	return 0;
}

static int cbor_uint(struct cbor_cursor *c, uint32_t *value)
{
	return cbor_head(c, 0U, value);
}

static int cbor_bytes(struct cbor_cursor *c, uint8_t major, void *out,
		      size_t capacity, size_t expected, size_t *out_size)
{
	uint32_t length;
	int rc = cbor_head(c, major, &length);

	if (rc != 0 || length > capacity || c->size - c->pos < length ||
	    (expected != 0U && length != expected)) {
		return rc != 0 ? rc : -EINVAL;
	}
	memcpy(out, &c->data[c->pos], length);
	c->pos += length;
	if (out_size != NULL) {
		*out_size = length;
	}
	return 0;
}

static int cbor_text(struct cbor_cursor *c, char *out, size_t capacity)
{
	size_t length;
	int rc;

	if (capacity == 0U) {
		return -EINVAL;
	}
	rc = cbor_bytes(c, 3U, out, capacity - 1U, 0U, &length);
	if (rc == 0) {
		out[length] = '\0';
	}
	return rc;
}

static int cbor_version(struct cbor_cursor *c, struct mbs_firmware_version *v)
{
	uint32_t count;
	int rc = cbor_head(c, 4U, &count);

	if (rc != 0 || count != 4U) {
		return -EINVAL;
	}
	return cbor_uint(c, &v->major) || cbor_uint(c, &v->minor) ||
	       cbor_uint(c, &v->patch) || cbor_uint(c, &v->build) ? -EINVAL : 0;
}

static int cbor_key(struct cbor_cursor *c, uint32_t expected)
{
	uint32_t key;
	int rc = cbor_uint(c, &key);

	return rc == 0 && key == expected ? 0 : -EINVAL;
}

static int manifest_decode(const uint8_t *encoded, size_t encoded_size,
			   struct mbs_firmware_manifest *m)
{
	struct cbor_cursor c = {.data = encoded, .size = encoded_size};
	uint32_t count;
	uint32_t value;
	int rc;

	if (encoded == NULL || m == NULL || encoded_size == 0U ||
	    encoded_size > MBS_FIRMWARE_MANIFEST_MAX_SIZE) {
		return -EINVAL;
	}
	memset(m, 0, sizeof(*m));
	rc = cbor_head(&c, 5U, &count);
	if (rc != 0 || count != 20U) {
		return -EINVAL;
	}

#define KEY_UINT(key, field) do { \
	if (cbor_key(&c, (key)) != 0 || cbor_uint(&c, &(field)) != 0) return -EINVAL; \
} while (0)
	KEY_UINT(1U, value);
	if (value != MBS_FIRMWARE_DELTA_PROTOCOL_VERSION || cbor_key(&c, 2U) != 0 ||
	    cbor_bytes(&c, 2U, m->campaign_id, sizeof(m->campaign_id),
		       sizeof(m->campaign_id), NULL) != 0) {
		return -EINVAL;
	}
	KEY_UINT(3U, value); m->role = (uint8_t)value;
	if (cbor_key(&c, 4U) != 0 || cbor_text(&c, m->board_id, sizeof(m->board_id)) != 0 ||
	    cbor_key(&c, 5U) != 0 || cbor_text(&c, m->soc_id, sizeof(m->soc_id)) != 0) {
		return -EINVAL;
	}
	KEY_UINT(6U, m->hardware_revision_min);
	KEY_UINT(7U, m->hardware_revision_max);
	KEY_UINT(8U, m->partition_abi);
	if (cbor_key(&c, 9U) != 0 || cbor_version(&c, &m->source_version) != 0 ||
	    cbor_key(&c, 10U) != 0 || cbor_version(&c, &m->target_version) != 0 ||
	    cbor_key(&c, 11U) != 0 ||
	    cbor_bytes(&c, 2U, m->source_hash, sizeof(m->source_hash),
		       sizeof(m->source_hash), NULL) != 0 ||
	    cbor_key(&c, 12U) != 0 ||
	    cbor_bytes(&c, 2U, m->target_hash, sizeof(m->target_hash),
		       sizeof(m->target_hash), NULL) != 0 ||
	    cbor_key(&c, 13U) != 0 ||
	    cbor_bytes(&c, 2U, m->patch_hash, sizeof(m->patch_hash),
		       sizeof(m->patch_hash), NULL) != 0) {
		return -EINVAL;
	}
	KEY_UINT(14U, m->patch_size);
	KEY_UINT(15U, m->chunk_size);
	KEY_UINT(16U, m->image_key_id);
	KEY_UINT(17U, m->manifest_key_id);
	KEY_UINT(18U, m->security_counter);
	if (cbor_key(&c, 19U) != 0 || c.pos >= c.size || c.data[c.pos++] != 0xf4U) {
		return -EINVAL;
	}
	KEY_UINT(20U, m->patch_format);
#undef KEY_UINT
	return c.pos == c.size ? 0 : -EINVAL;
}

static int version_compare(const struct mbs_firmware_version *a,
			   const struct mbs_firmware_version *b)
{
	const uint32_t av[] = {a->major, a->minor, a->patch, a->build};
	const uint32_t bv[] = {b->major, b->minor, b->patch, b->build};

	for (size_t i = 0; i < ARRAY_SIZE(av); i++) {
		if (av[i] != bv[i]) {
			return av[i] > bv[i] ? 1 : -1;
		}
	}
	return 0;
}

int mbs_firmware_hash(const uint8_t *data, size_t size,
		       uint8_t hash[MBS_FIRMWARE_HASH_SIZE])
{
	psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
	size_t output_size = 0U;
	psa_status_t status;

	if ((data == NULL && size != 0U) || hash == NULL) {
		return -EINVAL;
	}
	status = psa_hash_setup(&op, PSA_ALG_SHA_256);
	if (status == PSA_SUCCESS) {
		status = psa_hash_update(&op, data, size);
	}
	if (status == PSA_SUCCESS) {
		status = psa_hash_finish(&op, hash, MBS_FIRMWARE_HASH_SIZE, &output_size);
	}
	psa_hash_abort(&op);
	return status == PSA_SUCCESS && output_size == MBS_FIRMWARE_HASH_SIZE ? 0 : -EIO;
}

int mbs_firmware_platform_ed25519_verify(
	const uint8_t public_key[32], const uint8_t signature[64],
	const uint8_t *message, size_t message_size)
{
	return crypto_ed25519_check(signature, public_key, message, message_size) == 0 ? 0 : -EKEYREJECTED;
}

int mbs_firmware_manifest_admit(const uint8_t *encoded, size_t encoded_size,
				 const uint8_t signature[MBS_FIRMWARE_SIGNATURE_SIZE],
				 struct mbs_firmware_manifest *manifest,
				 uint8_t transfer_id[MBS_FIRMWARE_TRANSFER_ID_SIZE],
				 enum mbs_firmware_result *result)
{
	const struct mbs_firmware_version running = {
		.major = APP_VERSION_MAJOR, .minor = APP_VERSION_MINOR,
		.patch = APP_PATCHLEVEL, .build = APP_TWEAK,
	};
	uint8_t public_key[32];
	int rc;

	if (result == NULL) {
		return -EINVAL;
	}
	*result = MBS_FIRMWARE_RESULT_INVALID;
	rc = manifest_decode(encoded, encoded_size, manifest);
	if (rc != 0) {
		return rc;
	}
	if (hex2bin(CONFIG_MBS_FIRMWARE_MANIFEST_PUBLIC_KEY_HEX,
		    strlen(CONFIG_MBS_FIRMWARE_MANIFEST_PUBLIC_KEY_HEX),
		    public_key, sizeof(public_key)) != sizeof(public_key)) {
		*result = MBS_FIRMWARE_RESULT_INTERNAL;
		return -EINVAL;
	}
	rc = mbs_firmware_platform_ed25519_verify(public_key, signature,
						 encoded, encoded_size);
	memset(public_key, 0, sizeof(public_key));
	if (rc != 0) {
		*result = MBS_FIRMWARE_RESULT_BAD_SIGNATURE;
		return rc;
	}
	if (manifest->role != CONFIG_MBS_FIRMWARE_ROLE ||
	    strcmp(manifest->board_id, CONFIG_MBS_FIRMWARE_BOARD_ID) != 0 ||
	    strcmp(manifest->soc_id, CONFIG_MBS_FIRMWARE_SOC_ID) != 0 ||
	    CONFIG_MBS_FIRMWARE_HARDWARE_REVISION < manifest->hardware_revision_min ||
	    CONFIG_MBS_FIRMWARE_HARDWARE_REVISION > manifest->hardware_revision_max ||
	    manifest->partition_abi != MBS_FIRMWARE_DELTA_PARTITION_ABI) {
		*result = MBS_FIRMWARE_RESULT_WRONG_TARGET;
		return -EXDEV;
	}
	if (version_compare(&manifest->source_version, &running) != 0) {
		*result = MBS_FIRMWARE_RESULT_WRONG_SOURCE;
		return -ESTALE;
	}
	if (version_compare(&manifest->target_version, &manifest->source_version) <= 0 ||
	    manifest->security_counter < CONFIG_MBS_FIRMWARE_SECURITY_COUNTER) {
		*result = MBS_FIRMWARE_RESULT_DOWNGRADE;
		return -EPERM;
	}
	if (manifest->patch_size <= 88U || manifest->patch_size > MBS_FIRMWARE_PATCH_MAX_SIZE ||
	    manifest->chunk_size != MBS_FIRMWARE_CHUNK_SIZE ||
	    manifest->patch_format != 1U ||
	    manifest->image_key_id != CONFIG_MBS_FIRMWARE_IMAGE_KEY_ID ||
	    manifest->manifest_key_id != CONFIG_MBS_FIRMWARE_MANIFEST_KEY_ID) {
		*result = MBS_FIRMWARE_RESULT_UNSUPPORTED;
		return -ENOTSUP;
	}
	rc = mbs_firmware_hash(encoded, encoded_size, transfer_id);
	if (rc == 0) {
		*result = MBS_FIRMWARE_RESULT_OK;
	}
	return rc;
}
