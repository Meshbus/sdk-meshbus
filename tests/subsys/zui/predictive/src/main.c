// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <zephyr/zui/predictive.h>

#define HAS_FIXTURE_DICT (CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_DICT_SOURCE[0] != '\0')

ZTEST(zui_predictive, test_key_mapping_and_sequence)
{
	char sequence[8];

	zassert_equal(zui_predictive_key_for_char('a'), '2');
	zassert_equal(zui_predictive_key_for_char('C'), '2');
	zassert_equal(zui_predictive_key_for_char('s'), '7');
	zassert_equal(zui_predictive_key_for_char('z'), '9');
	zassert_equal(zui_predictive_key_for_char('1'), '\0');

	zassert_ok(zui_predictive_sequence_for_word("good", sequence, sizeof(sequence)));
	zassert_str_equal(sequence, "4663");
	zassert_equal(zui_predictive_sequence_for_word("go-od", sequence, sizeof(sequence)),
		      -EINVAL);
	zassert_equal(zui_predictive_sequence_for_word("toolongword", sequence,
						      sizeof(sequence)),
		      -ENOSPC);
}

ZTEST(zui_predictive, test_lookup_order_and_truncation)
{
	if (!HAS_FIXTURE_DICT) {
		zassert_equal(zui_predictive_candidate_count("46"), 0U);
		zassert_equal(zui_predictive_candidate_count("4663"), 0U);
		zassert_is_null(zui_predictive_candidate("4663", 0U));
		return;
	}

	if (CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_DICT_SIZE <= 64) {
		zassert_equal(zui_predictive_candidate_count("46"), 1U);
	} else {
		zassert_equal(zui_predictive_candidate_count("46"), 2U);
	}
	zassert_str_equal(zui_predictive_candidate("46", 0U), "go");
	if (CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_DICT_SIZE > 64) {
		zassert_str_equal(zui_predictive_candidate("46", 1U), "in");
	}
	zassert_equal(zui_predictive_candidate_count("4663"), 8U);
	zassert_str_equal(zui_predictive_candidate("4663", 0U), "good");
	zassert_str_equal(zui_predictive_candidate("4663", 1U), "home");
	zassert_str_equal(zui_predictive_candidate("4663", 7U), "inne");
	zassert_is_null(zui_predictive_candidate("4663", 8U));
}

ZTEST(zui_predictive, test_distinct_sequences_and_no_match)
{
	if (!HAS_FIXTURE_DICT) {
		zassert_equal(zui_predictive_candidate_count("228"), 0U);
		zassert_equal(zui_predictive_candidate_count("364"), 0U);
		zassert_equal(zui_predictive_candidate_count("9999"), 0U);
		zassert_false(zui_predictive_has_sequence_prefix("4"));
		return;
	}

	zassert_true(zui_predictive_has_sequence_prefix("4"));
	zassert_true(zui_predictive_has_sequence_prefix("46"));
	zassert_true(zui_predictive_has_sequence_prefix("466"));
	zassert_true(zui_predictive_has_sequence_prefix("4663"));
	if (CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_DICT_SIZE <= 64) {
		zassert_equal(zui_predictive_candidate_count("228"), 0U);
		zassert_equal(zui_predictive_candidate_count("364"), 0U);
		zassert_is_null(zui_predictive_candidate("364", 0U));
		return;
	}

	zassert_equal(zui_predictive_candidate_count("228"), 2U);
	zassert_str_equal(zui_predictive_candidate("228", 0U), "cat");
	zassert_str_equal(zui_predictive_candidate("228", 1U), "bat");

	zassert_equal(zui_predictive_candidate_count("364"), 1U);
	zassert_str_equal(zui_predictive_candidate("364", 0U), "dog");

	zassert_equal(zui_predictive_candidate_count("9999"), 0U);
	zassert_is_null(zui_predictive_candidate("9999", 0U));
	zassert_false(zui_predictive_has_sequence_prefix("9999"));
	zassert_equal(zui_predictive_candidate_count("10"), 0U);
}

ZTEST_SUITE(zui_predictive, NULL, NULL, NULL, NULL, NULL);
