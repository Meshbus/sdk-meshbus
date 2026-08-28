// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "Utils.h"

extern "C" {
#include "meshcore_test_runtime.h"
#include "meshcore_utils.h"
}

class CaptureStream : public Stream {
public:
	void print(char c) override
	{
		zassert_true(len_ < sizeof(buf_) - 1U, "printHex capture buffer overflow");
		buf_[len_++] = c;
		buf_[len_] = '\0';
	}

	const char *c_str() const
	{
		return buf_;
	}

private:
	char buf_[32] = { 0 };
	size_t len_ = 0U;
};

static void expect_sha256_single_matches_reference(const uint8_t *msg, int msg_len,
						   size_t hash_len)
{
	uint8_t expected[32] = { 0 };
	uint8_t actual[32] = { 0 };

	mesh::Utils::sha256(expected, hash_len, msg, msg_len);
	meshcore_utils_sha256(actual, hash_len, msg, msg_len);

	zassert_mem_equal(expected, actual, hash_len, "single-fragment SHA256 mismatch");
}

static void expect_sha256_two_fragments_matches_reference(const uint8_t *frag1,
							  int frag1_len,
							  const uint8_t *frag2,
							  int frag2_len,
							  size_t hash_len)
{
	uint8_t expected[32] = { 0 };
	uint8_t actual[32] = { 0 };
	uint8_t combined[32] = { 0 };
	uint8_t expected_combined[32] = { 0 };

	zassert_true((size_t)(frag1_len + frag2_len) <= sizeof(combined),
		     "combined test fixture too small");

	memcpy(combined, frag1, (size_t)frag1_len);
	memcpy(&combined[frag1_len], frag2, (size_t)frag2_len);

	mesh::Utils::sha256(expected, hash_len, frag1, frag1_len, frag2, frag2_len);
	mesh::Utils::sha256(expected_combined, hash_len, combined, frag1_len + frag2_len);
	meshcore_utils_sha256_two_fragments(actual, hash_len, frag1, frag1_len, frag2,
					    frag2_len);

	zassert_mem_equal(expected_combined, expected, hash_len,
			  "reference two-fragment SHA256 differs from combined input");
	zassert_mem_equal(expected, actual, hash_len, "two-fragment SHA256 mismatch");
}

static void expect_hex_roundtrip_matches_reference(const uint8_t *raw, size_t raw_len,
						   const char *expected_hex)
{
	char expected[32] = { 0 };
	char actual[32] = { 0 };
	uint8_t expected_out[16] = { 0 };
	uint8_t actual_out[16] = { 0 };

	zassert_true((raw_len * 2U + 1U) <= sizeof(actual), "hex fixture too large");
	zassert_true(raw_len <= sizeof(actual_out), "raw fixture too large");

	mesh::Utils::toHex(expected, raw, raw_len);
	meshcore_utils_to_hex(actual, raw, raw_len);

	zassert_equal(0, strcmp(expected_hex, expected), "reference hex mismatch");
	zassert_equal(0, strcmp(expected, actual), "hex string mismatch");
	zassert_equal('\0', actual[raw_len * 2U], "hex output is not null-terminated");

	zassert_true(mesh::Utils::fromHex(expected_out, (int)raw_len, actual),
		     "reference fromHex failed");
	zassert_true(meshcore_utils_from_hex(actual_out, (int)raw_len, actual),
		     "C from_hex failed");
	zassert_mem_equal(expected_out, actual_out, raw_len, "hex roundtrip mismatch");
	zassert_mem_equal(raw, actual_out, raw_len, "hex did not roundtrip to original bytes");
}

static void expect_print_hex_reference_matches_c_to_hex(const uint8_t *raw, size_t raw_len)
{
	CaptureStream stream;
	char actual[32] = { 0 };

	zassert_true((raw_len * 2U + 1U) <= sizeof(actual), "hex fixture too large");

	mesh::Utils::printHex(stream, raw, raw_len);
	meshcore_utils_to_hex(actual, raw, raw_len);

	zassert_equal(0, strcmp(stream.c_str(), actual),
		      "reference printHex differs from C to_hex");
}

static void expect_from_hex_matches_reference(const char *hex, int dest_size)
{
	uint8_t expected[16] = { 0xA5 };
	uint8_t actual[16] = { 0xA5 };
	bool expected_ok;
	bool actual_ok;

	zassert_true(dest_size >= 0 && dest_size <= (int)sizeof(actual), "bad fixture size");

	expected_ok = mesh::Utils::fromHex(expected, dest_size, hex);
	actual_ok = meshcore_utils_from_hex(actual, dest_size, hex);

	zassert_equal(expected_ok, actual_ok, "from_hex result mismatch for %s", hex);
	zassert_mem_equal(expected, actual, (size_t)dest_size, "from_hex output mismatch");
}

static void expect_parse_matches_reference(const char *input, int max_num, char separator)
{
	char expected_text[32] = { 0 };
	char actual_text[32] = { 0 };
	const char *expected_parts[6] = { 0 };
	const char *actual_parts[6] = { 0 };
	int expected_count;
	int actual_count;

	zassert_true(strlen(input) < sizeof(expected_text), "parse fixture too large");
	zassert_true(max_num >= 0 && max_num <= (int)ARRAY_SIZE(actual_parts), "bad max_num");

	strcpy(expected_text, input);
	strcpy(actual_text, input);

	expected_count = mesh::Utils::parseTextParts(expected_text, expected_parts, max_num,
						     separator);
	actual_count = meshcore_utils_parse_text_parts(actual_text, actual_parts, max_num,
						       separator);

	zassert_equal(expected_count, actual_count, "parse part count mismatch for %s", input);
	zassert_mem_equal(expected_text, actual_text, sizeof(expected_text),
			  "parse mutated text mismatch for %s", input);

	for (int i = 0; i < actual_count; i++) {
		ptrdiff_t expected_offset = expected_parts[i] - expected_text;
		ptrdiff_t actual_offset = actual_parts[i] - actual_text;

		zassert_equal(expected_offset, actual_offset, "part %d offset mismatch", i);
		zassert_equal(0, strcmp(expected_parts[i], actual_parts[i]),
			      "part %d content mismatch", i);
	}
}

static void fill_sequence(uint8_t *dest, size_t len, uint8_t first)
{
	for (size_t i = 0; i < len; i++) {
		dest[i] = (uint8_t)(first + i);
	}
}

static void expect_encrypt_decrypt_matches_reference(const uint8_t *plain, int plain_len)
{
	uint8_t shared_secret[PUB_KEY_SIZE];
	uint8_t expected_cipher[64] = { 0 };
	uint8_t actual_cipher[64] = { 0 };
	uint8_t expected_plain[64] = { 0 };
	uint8_t actual_plain[64] = { 0 };
	int expected_cipher_len;
	int actual_cipher_len;
	int expected_plain_len;
	int actual_plain_len;

	zassert_true(plain_len >= 0 && plain_len <= 32, "bad encrypt fixture length");
	fill_sequence(shared_secret, sizeof(shared_secret), 0x10);

	expected_cipher_len = mesh::Utils::encrypt(shared_secret, expected_cipher, plain, plain_len);
	actual_cipher_len = meshcore_utils_encrypt(shared_secret, actual_cipher, plain, plain_len);

	zassert_equal(expected_cipher_len, actual_cipher_len, "encrypt length mismatch");
	zassert_mem_equal(expected_cipher, actual_cipher, (size_t)expected_cipher_len,
			  "ciphertext mismatch");

	expected_plain_len = mesh::Utils::decrypt(shared_secret, expected_plain, expected_cipher,
						  expected_cipher_len);
	actual_plain_len = meshcore_utils_decrypt(shared_secret, actual_plain, actual_cipher,
						  actual_cipher_len);

	zassert_equal(expected_plain_len, actual_plain_len, "decrypt length mismatch");
	zassert_mem_equal(expected_plain, actual_plain, (size_t)expected_plain_len,
			  "decrypted plaintext mismatch");
	zassert_mem_equal(plain, actual_plain, (size_t)plain_len,
			  "decrypted plaintext prefix mismatch");
}

static void expect_encrypt_then_mac_matches_reference(const uint8_t *plain, int plain_len)
{
	uint8_t shared_secret[PUB_KEY_SIZE];
	uint8_t expected_packet[64] = { 0 };
	uint8_t actual_packet[64] = { 0 };
	uint8_t expected_plain[64] = { 0 };
	uint8_t actual_plain[64] = { 0 };
	int expected_packet_len;
	int actual_packet_len;
	int expected_plain_len;
	int actual_plain_len;

	zassert_true(plain_len >= 0 && plain_len <= 32, "bad MAC fixture length");
	fill_sequence(shared_secret, sizeof(shared_secret), 0xA0);

	expected_packet_len = mesh::Utils::encryptThenMAC(shared_secret, expected_packet, plain,
							  plain_len);
	actual_packet_len = meshcore_utils_encrypt_then_mac(shared_secret, actual_packet, plain,
							   plain_len);

	zassert_equal(expected_packet_len, actual_packet_len, "encryptThenMAC length mismatch");
	zassert_mem_equal(expected_packet, actual_packet, (size_t)expected_packet_len,
			  "encryptThenMAC output mismatch");

	expected_plain_len = mesh::Utils::MACThenDecrypt(shared_secret, expected_plain,
							 expected_packet, expected_packet_len);
	actual_plain_len = meshcore_utils_mac_then_decrypt(shared_secret, actual_plain,
							  actual_packet, actual_packet_len);

	zassert_equal(expected_plain_len, actual_plain_len, "MACThenDecrypt length mismatch");
	zassert_mem_equal(expected_plain, actual_plain, (size_t)expected_plain_len,
			  "MACThenDecrypt plaintext mismatch");
	zassert_mem_equal(plain, actual_plain, (size_t)plain_len,
			  "MACThenDecrypt plaintext prefix mismatch");
}

static void expect_mac_then_decrypt_rejects(const uint8_t *shared_secret,
					    const uint8_t *packet, int packet_len)
{
	uint8_t expected_plain[64] = { 0 };
	uint8_t actual_plain[64] = { 0 };

	zassert_equal(0, mesh::Utils::MACThenDecrypt(shared_secret, expected_plain, packet,
						     packet_len),
		      "reference should reject tampered packet");
	zassert_equal(0, meshcore_utils_mac_then_decrypt(shared_secret, actual_plain, packet,
							 packet_len),
		      "C implementation should reject tampered packet");
}

ZTEST(meshcore_utils_tdd, test_sha256_single_fragment_matches_reference)
{
	static const uint8_t empty[] = "";
	static const uint8_t ascii[] = "zephyr_meshcore";
	static const uint8_t binary[] = { 0x00, 0x10, 0xFF, 0x20, 0x00, 0x30 };
	static const size_t hash_lens[] = { 1, 8, 16, 32 };

	for (size_t i = 0; i < ARRAY_SIZE(hash_lens); i++) {
		expect_sha256_single_matches_reference(empty, 0, hash_lens[i]);
		expect_sha256_single_matches_reference(ascii, (int)strlen((const char *)ascii),
						       hash_lens[i]);
		expect_sha256_single_matches_reference(binary, (int)sizeof(binary),
						       hash_lens[i]);
	}
}

ZTEST(meshcore_utils_tdd, test_sha256_two_fragments_matches_reference)
{
	static const uint8_t empty[] = "";
	static const uint8_t frag1[] = "zephyr_";
	static const uint8_t frag2[] = "rtos";
	static const uint8_t binary1[] = { 0x00, 0x42, 0x7E };
	static const uint8_t binary2[] = { 0xFF, 0x00, 0x11, 0x22 };
	static const size_t hash_lens[] = { 1, 8, 16, 32 };

	for (size_t i = 0; i < ARRAY_SIZE(hash_lens); i++) {
		expect_sha256_two_fragments_matches_reference(frag1, (int)(sizeof(frag1) - 1),
							      frag2,
							      (int)(sizeof(frag2) - 1),
							      hash_lens[i]);
		expect_sha256_two_fragments_matches_reference(empty, 0, frag2,
							      (int)(sizeof(frag2) - 1),
							      hash_lens[i]);
		expect_sha256_two_fragments_matches_reference(binary1, (int)sizeof(binary1),
							      binary2, (int)sizeof(binary2),
							      hash_lens[i]);
	}
}

ZTEST(meshcore_utils_tdd, test_hex_matches_reference)
{
	static const uint8_t raw[] = { 0x00, 0xAB, 0xCD, 0xEF, 0x10 };
	uint8_t expected_out[sizeof(raw)] = { 0 };
	uint8_t actual_out[sizeof(raw)] = { 0 };

	expect_hex_roundtrip_matches_reference(raw, sizeof(raw), "00ABCDEF10");
	expect_print_hex_reference_matches_c_to_hex(raw, sizeof(raw));
	expect_from_hex_matches_reference("00abcdef10", sizeof(raw));
	expect_from_hex_matches_reference("00AbCdEf10", sizeof(raw));

	zassert_false(mesh::Utils::fromHex(expected_out, (int)sizeof(expected_out), "00ABCDEF"),
		      "reference fromHex should reject length mismatch");
	zassert_false(meshcore_utils_from_hex(actual_out, (int)sizeof(actual_out), "00ABCDEF"),
		      "from_hex should reject length mismatch");
	zassert_true(mesh::Utils::fromHex(expected_out, 2, "00AG"),
		     "reference fromHex should preserve its permissive conversion");
	zassert_true(meshcore_utils_from_hex(actual_out, 2, "00AG"),
		     "C from_hex should match the reference permissive conversion");
	zassert_mem_equal(expected_out, actual_out, 2,
			  "C from_hex invalid-nibble mapping mismatch");
}

ZTEST(meshcore_utils_tdd, test_is_hex_char_matches_reference)
{
	static const char valid[] = "0123456789ABCDEFabcdef";
	static const char invalid[] = { 'G', '/', ':', '@', '\0' };

	for (size_t i = 0; i < strlen(valid); i++) {
		zassert_equal(mesh::Utils::isHexChar(valid[i]),
			      meshcore_utils_is_hex_char(valid[i]),
			      "valid hex char mismatch for %c", valid[i]);
		zassert_true(meshcore_utils_is_hex_char(valid[i]),
			     "expected true for valid hex char %c", valid[i]);
	}

	for (size_t i = 0; i < ARRAY_SIZE(invalid); i++) {
		zassert_equal(mesh::Utils::isHexChar(invalid[i]),
			      meshcore_utils_is_hex_char(invalid[i]),
			      "invalid hex char mismatch");
		zassert_false(meshcore_utils_is_hex_char(invalid[i]),
			      "expected false for invalid hex char");
	}
}

ZTEST(meshcore_utils_tdd, test_parse_text_parts_matches_reference)
{
	expect_parse_matches_reference("a,b,,c", 4, ',');
	expect_parse_matches_reference(",a,b", 4, ',');
	expect_parse_matches_reference("a,b,", 4, ',');
	expect_parse_matches_reference("a,b,c,d", 2, ',');
	expect_parse_matches_reference("a,b,c", 0, ',');
	expect_parse_matches_reference("a|b||c", 4, '|');
}

ZTEST(meshcore_utils_tdd, test_encrypt_decrypt_matches_reference)
{
	static const uint8_t empty[] = "";
	static const uint8_t short_plain[] = { 0x01, 0x02, 0x00, 0xFF, 0x42 };
	static const uint8_t block_plain[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	};
	static const uint8_t partial_plain[] = {
		'M', 'e', 's', 'h', 'C', 'o', 'r', 'e', 0x00, 0x7E,
		0x80, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x01,
	};

	expect_encrypt_decrypt_matches_reference(empty, 0);
	expect_encrypt_decrypt_matches_reference(short_plain, (int)sizeof(short_plain));
	expect_encrypt_decrypt_matches_reference(block_plain, (int)sizeof(block_plain));
	expect_encrypt_decrypt_matches_reference(partial_plain, (int)sizeof(partial_plain));
}

ZTEST(meshcore_utils_tdd, test_decrypt_rejects_non_block_aligned_ciphertext)
{
	uint8_t shared_secret[PUB_KEY_SIZE];
	uint8_t cipher[17];
	uint8_t plain[32] = { 0 };

	fill_sequence(shared_secret, sizeof(shared_secret), 0x44);
	fill_sequence(cipher, sizeof(cipher), 0x20);

	zassert_equal(0, meshcore_utils_decrypt(shared_secret, plain, cipher, 1),
		      "decrypt should reject a short non-block-aligned ciphertext");
	zassert_equal(0, meshcore_utils_decrypt(shared_secret, plain, cipher, 15),
		      "decrypt should reject a partial block");
	zassert_equal(0, meshcore_utils_decrypt(shared_secret, plain, cipher, 17),
		      "decrypt should reject a block plus trailing byte");
}

ZTEST(meshcore_utils_tdd, test_encrypt_then_mac_matches_reference)
{
	static const uint8_t empty[] = "";
	static const uint8_t short_plain[] = { 0x5A, 0x00, 0xC3, 0x7E, 0x11 };
	static const uint8_t block_plain[] = {
		0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
		0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
	};

	expect_encrypt_then_mac_matches_reference(empty, 0);
	expect_encrypt_then_mac_matches_reference(short_plain, (int)sizeof(short_plain));
	expect_encrypt_then_mac_matches_reference(block_plain, (int)sizeof(block_plain));
}

ZTEST(meshcore_utils_tdd, test_mac_then_decrypt_rejects_bad_mac)
{
	uint8_t shared_secret[PUB_KEY_SIZE];
	uint8_t packet[64] = { 0 };
	static const uint8_t plain[] = { 'b', 'a', 'd', '-', 'm', 'a', 'c' };
	int packet_len;

	fill_sequence(shared_secret, sizeof(shared_secret), 0x33);
	packet_len = meshcore_utils_encrypt_then_mac(shared_secret, packet, plain,
						     (int)sizeof(plain));
	zassert_true(packet_len > CIPHER_MAC_SIZE, "failed to create test packet");

	packet[0] ^= 0x01;
	expect_mac_then_decrypt_rejects(shared_secret, packet, packet_len);

	packet[0] ^= 0x01;
	packet[CIPHER_MAC_SIZE] ^= 0x01;
	expect_mac_then_decrypt_rejects(shared_secret, packet, packet_len);
}

ZTEST(meshcore_utils_tdd, test_encrypt_then_mac_reports_negative_on_crypto_failures)
{
	uint8_t shared_secret[PUB_KEY_SIZE];
	uint8_t packet[64] = { 0 };
	static const uint8_t plain[] = { 0x31, 0x32, 0x33, 0x34 };

	fill_sequence(shared_secret, sizeof(shared_secret), 0x61);

	meshcore_hal_test_crypto_set_encrypt_fail(true);
	zassert_true(meshcore_utils_encrypt(shared_secret, packet, plain,
					    (int)sizeof(plain)) < 0,
		     "encrypt should return negative on forced AES failure");
	zassert_true(meshcore_utils_encrypt_then_mac(shared_secret, packet, plain,
						     (int)sizeof(plain)) < 0,
		     "encrypt_then_mac should return negative on forced AES failure");
	meshcore_hal_test_crypto_set_encrypt_fail(false);

	meshcore_hal_test_crypto_set_hmac_fail(true);
	zassert_true(meshcore_utils_encrypt_then_mac(shared_secret, packet, plain,
						     (int)sizeof(plain)) < 0,
		     "encrypt_then_mac should return negative on forced HMAC failure");
	meshcore_hal_test_crypto_set_hmac_fail(false);
}

ZTEST_SUITE(meshcore_utils_tdd, NULL, NULL, NULL, NULL, NULL);
