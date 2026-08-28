// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "Identity.h"
#include "Utils.h"

extern "C" {
#include "meshcore_identity.h"

void meshcore_hal_test_rng_set_bytes(const uint8_t *src, size_t len);
void meshcore_hal_test_rng_clear(void);
}

class FixedRNG : public mesh::RNG {
public:
	FixedRNG(const uint8_t *src, size_t len) : data_(src), len_(len), idx_(0U)
	{
	}

	void random(uint8_t *dest, size_t sz) override
	{
		for (size_t i = 0; i < sz; i++) {
			dest[i] = data_[idx_ % len_];
			idx_++;
		}
	}

private:
	const uint8_t *data_;
	size_t len_;
	size_t idx_;
};

static void make_reference_local_identity(mesh::LocalIdentity *dest,
					  const uint8_t *rng_data, size_t rng_len)
{
	FixedRNG rng(rng_data, rng_len);
	mesh::LocalIdentity identity(&rng);

	*dest = identity;
}

static void serialize_actual_local_identity(
	const struct meshcore_local_identity *identity, uint8_t dest[PRV_KEY_SIZE + PUB_KEY_SIZE])
{
	memcpy(dest, identity->prv_key, PRV_KEY_SIZE);
	memcpy(dest + PRV_KEY_SIZE, identity->identity.pub_key, PUB_KEY_SIZE);
}

static void expect_local_identity_matches_reference(
	mesh::LocalIdentity &expected,
	const struct meshcore_local_identity *actual)
{
	uint8_t expected_bytes[PRV_KEY_SIZE + PUB_KEY_SIZE] = { 0 };
	uint8_t actual_bytes[PRV_KEY_SIZE + PUB_KEY_SIZE] = { 0 };
	size_t expected_len;

	expected_len = expected.writeTo(expected_bytes, sizeof(expected_bytes));
	zassert_equal(sizeof(expected_bytes), expected_len,
		      "reference local identity serialization length mismatch");

	serialize_actual_local_identity(actual, actual_bytes);
	zassert_mem_equal(expected_bytes, actual_bytes, sizeof(expected_bytes),
			  "local identity state mismatch");
}

static void expect_identity_matches_reference(const mesh::Identity &expected,
					      const struct meshcore_identity *actual)
{
	zassert_mem_equal(expected.pub_key, actual->pub_key, PUB_KEY_SIZE,
			  "identity public key mismatch");
}

static void build_hex(char dest[], size_t dest_size, const uint8_t *src, size_t src_len)
{
	zassert_true(dest_size >= (src_len * 2U + 1U), "hex buffer too small");
	mesh::Utils::toHex(dest, src, src_len);
}

static void expect_identity_copy_hash_matches_reference(const uint8_t *pub_key)
{
	mesh::Identity expected(pub_key);
	struct meshcore_identity actual;
	uint8_t expected_hash[MAX_HASH_SIZE] = { 0 };
	uint8_t actual_hash[MAX_HASH_SIZE] = { 0 };
	int expected_len;
	int actual_len;

	meshcore_identity_init_from_pub_key(&actual, pub_key);

	expected_len = expected.copyHashTo(expected_hash);
	actual_len = meshcore_identity_copy_hash_to(&actual, actual_hash);

	zassert_equal(expected_len, actual_len, "copyHashTo length mismatch");
	zassert_mem_equal(expected_hash, actual_hash, (size_t)expected_len,
			  "copyHashTo bytes mismatch");
}

static void expect_identity_copy_hash_by_len_matches_reference(const uint8_t *pub_key,
							       uint8_t len)
{
	mesh::Identity expected(pub_key);
	struct meshcore_identity actual;
	uint8_t expected_hash[MAX_HASH_SIZE] = { 0 };
	uint8_t actual_hash[MAX_HASH_SIZE] = { 0 };
	int expected_len;
	int actual_len;

	meshcore_identity_init_from_pub_key(&actual, pub_key);

	expected_len = expected.copyHashTo(expected_hash, len);
	actual_len = meshcore_identity_copy_hash_by_len(&actual, actual_hash, len);

	zassert_equal(expected_len, actual_len, "copyHashByLen length mismatch");
	zassert_mem_equal(expected_hash, actual_hash, (size_t)len,
			  "copyHashByLen bytes mismatch");
}

static void expect_identity_hash_match_matches_reference(const uint8_t *pub_key,
							 const uint8_t *hash)
{
	mesh::Identity expected(pub_key);
	struct meshcore_identity actual;
	bool expected_ok;
	bool actual_ok;

	meshcore_identity_init_from_pub_key(&actual, pub_key);

	expected_ok = expected.isHashMatch(hash);
	actual_ok = meshcore_identity_is_hash_match(&actual, hash);
	zassert_equal(expected_ok, actual_ok, "isHashMatch mismatch");
}

static void expect_identity_hash_match_by_len_matches_reference(const uint8_t *pub_key,
								const uint8_t *hash,
								uint8_t len)
{
	mesh::Identity expected(pub_key);
	struct meshcore_identity actual;
	bool expected_ok;
	bool actual_ok;

	meshcore_identity_init_from_pub_key(&actual, pub_key);

	expected_ok = expected.isHashMatch(hash, len);
	actual_ok = meshcore_identity_is_hash_match_by_len(&actual, hash, len);
	zassert_equal(expected_ok, actual_ok, "isHashMatchByLen mismatch");
}

static void expect_local_identity_write_to_matches_reference(mesh::LocalIdentity &expected,
							     const struct meshcore_local_identity *actual,
							     size_t max_len)
{
	uint8_t expected_buf[PRV_KEY_SIZE + PUB_KEY_SIZE + 4];
	uint8_t actual_buf[PRV_KEY_SIZE + PUB_KEY_SIZE + 4];
	size_t expected_len;
	size_t actual_len;

	memset(expected_buf, 0xA5, sizeof(expected_buf));
	memset(actual_buf, 0xA5, sizeof(actual_buf));

	expected_len = expected.writeTo(expected_buf, max_len);
	actual_len = meshcore_local_identity_write_to(actual, actual_buf, max_len);

	zassert_equal(expected_len, actual_len, "write_to length mismatch");
	zassert_mem_equal(expected_buf, actual_buf, sizeof(expected_buf),
			  "write_to buffer mismatch");
}

static void expect_local_identity_read_from_matches_reference(const uint8_t *src, size_t len)
{
	mesh::LocalIdentity expected;
	struct meshcore_local_identity actual;

	expected.readFrom(src, len);
	meshcore_local_identity_init(&actual);
	meshcore_local_identity_read_from(&actual, src, len);

	expect_local_identity_matches_reference(expected, &actual);
}

static const uint8_t identity_rng_bytes[SEED_SIZE] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
};

static const uint8_t identity_rng_bytes_alt[SEED_SIZE] = {
	0xF0, 0xE1, 0xD2, 0xC3, 0xB4, 0xA5, 0x96, 0x87,
	0x78, 0x69, 0x5A, 0x4B, 0x3C, 0x2D, 0x1E, 0x0F,
	0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88,
	0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
};

ZTEST(meshcore_identity_tdd, test_identity_init_variants_match_reference)
{
	mesh::LocalIdentity reference_local;
	mesh::Identity expected_default;
	mesh::Identity expected_hex;
	mesh::Identity expected_pub;
	struct meshcore_identity actual = {};
	char pub_hex[PUB_KEY_SIZE * 2 + 1] = { 0 };

	make_reference_local_identity(&reference_local, identity_rng_bytes,
				      sizeof(identity_rng_bytes));
	build_hex(pub_hex, sizeof(pub_hex), reference_local.pub_key, PUB_KEY_SIZE);

	meshcore_identity_init(&actual);
	expect_identity_matches_reference(expected_default, &actual);

	expected_hex = mesh::Identity(pub_hex);
	zassert_true(meshcore_identity_init_from_pub_hex(&actual, pub_hex),
		     "init_from_pub_hex should succeed");
	expect_identity_matches_reference(expected_hex, &actual);

	expected_pub = mesh::Identity(reference_local.pub_key);
	meshcore_identity_init_from_pub_key(&actual, reference_local.pub_key);
	expect_identity_matches_reference(expected_pub, &actual);
}

ZTEST(meshcore_identity_tdd, test_identity_hash_helpers_match_reference)
{
	mesh::LocalIdentity reference_local;
	uint8_t default_hash[PATH_HASH_SIZE] = { 0 };
	uint8_t short_hash[4] = { 0 };
	uint8_t wrong_hash[4] = { 0 };

	make_reference_local_identity(&reference_local, identity_rng_bytes,
				      sizeof(identity_rng_bytes));

	expect_identity_copy_hash_matches_reference(reference_local.pub_key);
	expect_identity_copy_hash_by_len_matches_reference(reference_local.pub_key, 4U);

	memcpy(default_hash, reference_local.pub_key, sizeof(default_hash));
	memcpy(short_hash, reference_local.pub_key, sizeof(short_hash));
	memcpy(wrong_hash, reference_local.pub_key, sizeof(wrong_hash));
	wrong_hash[3] ^= 0x5A;

	expect_identity_hash_match_matches_reference(reference_local.pub_key, default_hash);
	expect_identity_hash_match_by_len_matches_reference(reference_local.pub_key,
							    short_hash, 4U);
	expect_identity_hash_match_by_len_matches_reference(reference_local.pub_key,
							    wrong_hash, 4U);
}

ZTEST(meshcore_identity_tdd, test_identity_match_helpers_match_reference)
{
	mesh::LocalIdentity ref_a;
	mesh::LocalIdentity ref_b;
	mesh::Identity expected_a_same;
	mesh::Identity expected_a_copy;
	mesh::Identity expected_b;
	struct meshcore_identity actual_a;
	struct meshcore_identity actual_a_copy;
	struct meshcore_identity actual_b;

	make_reference_local_identity(&ref_a, identity_rng_bytes, sizeof(identity_rng_bytes));
	make_reference_local_identity(&ref_b, identity_rng_bytes_alt,
				      sizeof(identity_rng_bytes_alt));

	expected_a_same = mesh::Identity(ref_a.pub_key);
	expected_a_copy = mesh::Identity(ref_a.pub_key);
	expected_b = mesh::Identity(ref_b.pub_key);
	meshcore_identity_init_from_pub_key(&actual_a, ref_a.pub_key);
	meshcore_identity_init_from_pub_key(&actual_a_copy, ref_a.pub_key);
	meshcore_identity_init_from_pub_key(&actual_b, ref_b.pub_key);

	zassert_equal(expected_a_same.matches(expected_a_copy),
		      meshcore_identity_matches(&actual_a, &actual_a_copy),
		      "matches mismatch for same identities");
	zassert_equal(expected_a_same.matches(expected_b),
		      meshcore_identity_matches(&actual_a, &actual_b),
		      "matches mismatch for different identities");
	zassert_equal(expected_a_same.matches(ref_a.pub_key),
		      meshcore_identity_matches_by_pub_key(&actual_a, ref_a.pub_key),
		      "matches_by_pub_key mismatch for same identity");
	zassert_equal(expected_a_same.matches(ref_b.pub_key),
		      meshcore_identity_matches_by_pub_key(&actual_a, ref_b.pub_key),
		      "matches_by_pub_key mismatch for different identity");
}

ZTEST(meshcore_identity_tdd, test_local_identity_init_variants_match_reference)
{
	mesh::LocalIdentity expected_default;
	mesh::LocalIdentity expected_hex;
	mesh::LocalIdentity expected_bytes;
	mesh::LocalIdentity reference_local;
	struct meshcore_local_identity actual = {};
	uint8_t full_layout[PRV_KEY_SIZE + PUB_KEY_SIZE] = { 0 };
	char prv_hex[PRV_KEY_SIZE * 2 + 1] = { 0 };
	char pub_hex[PUB_KEY_SIZE * 2 + 1] = { 0 };

	make_reference_local_identity(&reference_local, identity_rng_bytes,
				      sizeof(identity_rng_bytes));
	reference_local.writeTo(full_layout, sizeof(full_layout));
	build_hex(prv_hex, sizeof(prv_hex), full_layout, PRV_KEY_SIZE);
	build_hex(pub_hex, sizeof(pub_hex), reference_local.pub_key, PUB_KEY_SIZE);

	meshcore_local_identity_init(&actual);
	expect_local_identity_matches_reference(expected_default, &actual);

	expected_hex = mesh::LocalIdentity(prv_hex, pub_hex);
	zassert_true(meshcore_local_identity_init_from_hex(&actual, prv_hex, pub_hex),
		     "init_from_hex should succeed");
	expect_local_identity_matches_reference(expected_hex, &actual);

	expected_bytes.readFrom(full_layout, sizeof(full_layout));
	meshcore_local_identity_init_from_bytes(&actual, full_layout,
						 full_layout + PRV_KEY_SIZE);
	expect_local_identity_matches_reference(expected_bytes, &actual);
}

ZTEST(meshcore_identity_tdd, test_local_identity_generate_matches_reference)
{
	mesh::LocalIdentity expected;
	struct meshcore_local_identity actual;

	make_reference_local_identity(&expected, identity_rng_bytes, sizeof(identity_rng_bytes));

	meshcore_hal_test_rng_set_bytes(identity_rng_bytes, sizeof(identity_rng_bytes));
	meshcore_local_identity_generate(&actual);
	meshcore_hal_test_rng_clear();

	expect_local_identity_matches_reference(expected, &actual);
}

ZTEST(meshcore_identity_tdd, test_local_identity_sign_and_verify_match_reference)
{
	mesh::LocalIdentity expected_local;
	struct meshcore_local_identity actual_local;
	mesh::Identity expected_identity;
	struct meshcore_identity actual_identity;
	const uint8_t message[] = "meshcore-identity";
	const uint8_t tampered_message[] = "meshcore-identitz";
	uint8_t full_layout[PRV_KEY_SIZE + PUB_KEY_SIZE] = { 0 };
	uint8_t expected_sig[SIGNATURE_SIZE] = { 0 };
	uint8_t actual_sig[SIGNATURE_SIZE] = { 0 };
	uint8_t tampered_sig[SIGNATURE_SIZE] = { 0 };
	bool expected_ok;
	bool actual_ok;

	make_reference_local_identity(&expected_local, identity_rng_bytes, sizeof(identity_rng_bytes));
	expected_identity = mesh::Identity(expected_local.pub_key);
	expected_local.writeTo(full_layout, sizeof(full_layout));
	meshcore_local_identity_init_from_bytes(&actual_local, full_layout,
						 full_layout + PRV_KEY_SIZE);

	meshcore_identity_init_from_pub_key(&actual_identity, expected_local.pub_key);

	expected_local.sign(expected_sig, message, sizeof(message) - 1);
	meshcore_local_identity_sign(&actual_local, actual_sig, message,
				     sizeof(message) - 1);

	zassert_mem_equal(expected_sig, actual_sig, sizeof(expected_sig),
			  "signature mismatch");

	expected_ok = expected_identity.verify(expected_sig, message, sizeof(message) - 1);
	actual_ok = meshcore_identity_verify(&actual_identity, actual_sig, message,
					     sizeof(message) - 1);
	zassert_equal(expected_ok, actual_ok, "verify result mismatch");

	memcpy(tampered_sig, actual_sig, sizeof(tampered_sig));
	tampered_sig[0] ^= 0x01;
	expected_ok = expected_identity.verify(tampered_sig, message, sizeof(message) - 1);
	actual_ok = meshcore_identity_verify(&actual_identity, tampered_sig, message,
					     sizeof(message) - 1);
	zassert_equal(expected_ok, actual_ok, "tampered signature verify mismatch");

	expected_ok = expected_identity.verify(expected_sig, tampered_message,
					       sizeof(tampered_message) - 1);
	actual_ok = meshcore_identity_verify(&actual_identity, actual_sig, tampered_message,
					     sizeof(tampered_message) - 1);
	zassert_equal(expected_ok, actual_ok, "tampered message verify mismatch");
}

ZTEST(meshcore_identity_tdd, test_local_identity_write_to_matches_reference)
{
	mesh::LocalIdentity expected;
	struct meshcore_local_identity actual;
	uint8_t full_layout[PRV_KEY_SIZE + PUB_KEY_SIZE] = { 0 };

	make_reference_local_identity(&expected, identity_rng_bytes, sizeof(identity_rng_bytes));
	expected.writeTo(full_layout, sizeof(full_layout));
	meshcore_local_identity_init_from_bytes(&actual, full_layout,
						 full_layout + PRV_KEY_SIZE);

	expect_local_identity_write_to_matches_reference(expected, &actual, 0U);
	expect_local_identity_write_to_matches_reference(expected, &actual, PRV_KEY_SIZE - 1U);
	expect_local_identity_write_to_matches_reference(expected, &actual, PRV_KEY_SIZE);
	expect_local_identity_write_to_matches_reference(expected, &actual,
							 PRV_KEY_SIZE + 8U);
	expect_local_identity_write_to_matches_reference(expected, &actual,
							 PRV_KEY_SIZE + PUB_KEY_SIZE);
}

ZTEST(meshcore_identity_tdd, test_local_identity_read_from_matches_reference)
{
	mesh::LocalIdentity reference_local;
	uint8_t private_only[PRV_KEY_SIZE] = { 0 };
	uint8_t full_layout[PRV_KEY_SIZE + PUB_KEY_SIZE] = { 0 };
	uint8_t invalid_layout[PRV_KEY_SIZE - 1U] = { 0 };

	make_reference_local_identity(&reference_local, identity_rng_bytes,
				      sizeof(identity_rng_bytes));
	reference_local.writeTo(private_only, sizeof(private_only));
	reference_local.writeTo(full_layout, sizeof(full_layout));

	expect_local_identity_read_from_matches_reference(private_only, sizeof(private_only));
	expect_local_identity_read_from_matches_reference(full_layout, sizeof(full_layout));
	expect_local_identity_read_from_matches_reference(invalid_layout,
							  sizeof(invalid_layout));
}

ZTEST(meshcore_identity_tdd, test_local_identity_shared_secret_matches_reference)
{
	mesh::LocalIdentity ref_a;
	mesh::LocalIdentity ref_b;
	struct meshcore_local_identity actual_a;
	struct meshcore_local_identity actual_b;
	uint8_t expected_ab[PUB_KEY_SIZE] = { 0 };
	uint8_t expected_ba[PUB_KEY_SIZE] = { 0 };
	uint8_t actual_ab[PUB_KEY_SIZE] = { 0 };
	uint8_t actual_ba[PUB_KEY_SIZE] = { 0 };
	uint8_t layout_a[PRV_KEY_SIZE + PUB_KEY_SIZE] = { 0 };
	uint8_t layout_b[PRV_KEY_SIZE + PUB_KEY_SIZE] = { 0 };

	make_reference_local_identity(&ref_a, identity_rng_bytes, sizeof(identity_rng_bytes));
	make_reference_local_identity(&ref_b, identity_rng_bytes_alt,
				      sizeof(identity_rng_bytes_alt));

	ref_a.writeTo(layout_a, sizeof(layout_a));
	ref_b.writeTo(layout_b, sizeof(layout_b));
	meshcore_local_identity_init_from_bytes(&actual_a, layout_a, layout_a + PRV_KEY_SIZE);
	meshcore_local_identity_init_from_bytes(&actual_b, layout_b, layout_b + PRV_KEY_SIZE);

	ref_a.calcSharedSecret(expected_ab, ref_b.pub_key);
	ref_b.calcSharedSecret(expected_ba, ref_a.pub_key);
	meshcore_local_identity_calc_shared_secret(&actual_a, actual_ab,
						    actual_b.identity.pub_key);
	meshcore_local_identity_calc_shared_secret(&actual_b, actual_ba,
						    actual_a.identity.pub_key);

	zassert_mem_equal(expected_ab, expected_ba, sizeof(expected_ab),
			  "reference shared secret mismatch");
	zassert_mem_equal(expected_ab, actual_ab, sizeof(expected_ab),
			  "shared secret mismatch for A->B");
	zassert_mem_equal(expected_ba, actual_ba, sizeof(expected_ba),
			  "shared secret mismatch for B->A");
}

ZTEST(meshcore_identity_tdd, test_local_identity_validate_private_key_matches_reference)
{
	mesh::LocalIdentity reference_local;
	uint8_t private_key[PRV_KEY_SIZE] = { 0 };
	uint8_t zero_key[PRV_KEY_SIZE] = { 0 };
	bool expected_ok;
	bool actual_ok;

	make_reference_local_identity(&reference_local, identity_rng_bytes,
				      sizeof(identity_rng_bytes));
	reference_local.writeTo(private_key, sizeof(private_key));

	expected_ok = mesh::LocalIdentity::validatePrivateKey(private_key);
	actual_ok = meshcore_local_identity_validate_private_key(private_key);
	zassert_true(expected_ok, "reference generated private key should validate");
	zassert_equal(expected_ok, actual_ok,
		      "generated private key validation mismatch");

	expected_ok = mesh::LocalIdentity::validatePrivateKey(zero_key);
	actual_ok = meshcore_local_identity_validate_private_key(zero_key);
	zassert_equal(expected_ok, actual_ok, "zero private key validation mismatch");
	zassert_false(meshcore_local_identity_validate_private_key(nullptr),
		      "NULL private key should not validate");
}

ZTEST_SUITE(meshcore_identity_tdd, NULL, NULL, NULL, NULL, NULL);
