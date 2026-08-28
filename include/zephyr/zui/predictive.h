/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ZUI 12-key predictive dictionary API
 */

#ifndef ZEPHYR_INCLUDE_ZUI_PREDICTIVE_H_
#define ZEPHYR_INCLUDE_ZUI_PREDICTIVE_H_

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

char zui_predictive_key_for_char(char ch);
int zui_predictive_sequence_for_word(const char *word, char *sequence, size_t sequence_size);
size_t zui_predictive_candidate_count(const char *sequence);
const char *zui_predictive_candidate(const char *sequence, size_t index);
bool zui_predictive_has_sequence_prefix(const char *sequence);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ZUI_PREDICTIVE_H_ */
