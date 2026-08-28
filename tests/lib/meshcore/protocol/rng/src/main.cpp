// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "Utils.h"
#include "meshcore_test_runtime.h"

extern "C" {
#include "meshcore_rng.h"
}

class ScriptedReferenceRng : public mesh::RNG {
public:
	ScriptedReferenceRng(const uint8_t *bytes, size_t len) : bytes_(bytes), len_(len)
	{
	}

	void random(uint8_t *dest, size_t sz) override
	{
		for (size_t i = 0; i < sz; i++) {
			dest[i] = bytes_[idx_ % len_];
			idx_++;
		}
	}

private:
	const uint8_t *bytes_;
	size_t len_;
	size_t idx_ = 0U;
};

static void expect_next_int_matches_reference(const uint8_t *bytes, size_t len,
					      uint32_t min, uint32_t max, int repeats)
{
	ScriptedReferenceRng reference(bytes, len);

	meshcore_hal_test_rng_set_bytes(bytes, len);
	for (int i = 0; i < repeats; i++) {
		uint32_t expected = reference.nextInt(min, max);
		uint32_t actual = meshcore_rng_next_int(min, max);

		zassert_equal(expected, actual,
			      "next_int mismatch: min=%u max=%u iter=%d", min, max, i);
	}
	meshcore_hal_test_rng_clear();
}

ZTEST(meshcore_rng_tdd, test_next_int_matches_reference_for_multiple_ranges)
{
	static const uint8_t seq1[] = { 0x00, 0x01, 0x02, 0x03 };
	static const uint8_t seq2[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34 };
	static const struct {
		uint32_t min;
		uint32_t max;
	} ranges[] = {
		{ 0U, 1U },
		{ 0U, 5U },
		{ 1U, 4U },
		{ 5U, 17U },
		{ 100U, 1000U },
	};

	for (size_t i = 0; i < ARRAY_SIZE(ranges); i++) {
		expect_next_int_matches_reference(seq1, sizeof(seq1), ranges[i].min,
					      ranges[i].max, 8);
		expect_next_int_matches_reference(seq2, sizeof(seq2), ranges[i].min,
					      ranges[i].max, 8);
	}
}

ZTEST(meshcore_rng_tdd, test_next_int_matches_reference_across_repeated_calls)
{
	static const uint8_t seq[] = { 0x11, 0x22, 0x33, 0x44, 0x55 };

	expect_next_int_matches_reference(seq, sizeof(seq), 7U, 29U, 16);
}

ZTEST_SUITE(meshcore_rng_tdd, NULL, NULL, NULL, NULL, NULL);
