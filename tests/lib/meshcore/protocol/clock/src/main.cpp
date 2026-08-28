// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "MeshCore.h"
#include "meshcore_test_runtime.h"

extern "C" {
#include "meshcore_clock.h"
}

class ScriptedReferenceRtc : public mesh::RTCClock {
public:
	ScriptedReferenceRtc(const uint32_t *values, size_t len)
		: values_(values), len_(len)
	{
	}

	uint32_t getCurrentTime() override
	{
		size_t idx = index_ < len_ ? index_ : len_ - 1U;

		index_++;
		return values_[idx];
	}

	void setCurrentTime(uint32_t time) override
	{
		(void)time;
	}

private:
	const uint32_t *values_;
	size_t len_;
	size_t index_ = 0U;
};

static void expect_unique_time_matches_reference(const uint32_t *values, size_t len)
{
	ScriptedReferenceRtc reference(values, len);
	struct meshcore_rtc_clock_state actual_state;

	meshcore_rtc_clock_state_init(&actual_state);
	for (size_t i = 0; i < len; i++) {
		uint32_t expected = reference.getCurrentTimeUnique();

		meshcore_hal_test_rtc_set_current_time(values[i]);
		uint32_t actual = meshcore_clock_rtc_get_current_time_unique(&actual_state);
		zassert_equal(expected, actual, "unique time mismatch at idx %d", (int)i);
	}
}

ZTEST(meshcore_clock_tdd, test_rtc_get_current_time_unique_matches_reference)
{
	static const uint32_t increasing[] = { 1700000000U, 1700000001U, 1700000002U };
	static const uint32_t repeated[] = { 1700000010U, 1700000010U, 1700000010U };
	static const uint32_t backwards[] = { 1700000050U, 1700000048U, 1700000048U, 1700000047U };
	static const uint32_t mixed[] = {
		1700000100U, 1700000100U, 1700000101U, 1700000099U, 1700000102U,
	};

	expect_unique_time_matches_reference(increasing, ARRAY_SIZE(increasing));
	expect_unique_time_matches_reference(repeated, ARRAY_SIZE(repeated));
	expect_unique_time_matches_reference(backwards, ARRAY_SIZE(backwards));
	expect_unique_time_matches_reference(mixed, ARRAY_SIZE(mixed));
}

ZTEST_SUITE(meshcore_clock_tdd, NULL, NULL, NULL, NULL, NULL);
