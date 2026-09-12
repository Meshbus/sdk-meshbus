/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zui/predictive.h>

#include "zui_predictive_internal.h"

LOG_MODULE_REGISTER(zui_predictive, CONFIG_ZUI_LOG_LEVEL);

#define ZUI_PREDICTIVE_MAX_SEQUENCE_LEN 10U

char zui_predictive_key_for_char(char ch)
{
	switch (tolower((unsigned char)ch)) {
	case 'a':
	case 'b':
	case 'c':
		return '2';
	case 'd':
	case 'e':
	case 'f':
		return '3';
	case 'g':
	case 'h':
	case 'i':
		return '4';
	case 'j':
	case 'k':
	case 'l':
		return '5';
	case 'm':
	case 'n':
	case 'o':
		return '6';
	case 'p':
	case 'q':
	case 'r':
	case 's':
		return '7';
	case 't':
	case 'u':
	case 'v':
		return '8';
	case 'w':
	case 'x':
	case 'y':
	case 'z':
		return '9';
	default:
		return '\0';
	}
}

int zui_predictive_sequence_for_word(const char *word, char *sequence, size_t sequence_size)
{
	size_t i;

	if (word == NULL || sequence == NULL || sequence_size == 0U) {
		return -EINVAL;
	}

	for (i = 0U; word[i] != '\0'; i++) {
		char key = zui_predictive_key_for_char(word[i]);

		if (key == '\0') {
			sequence[0] = '\0';
			return -EINVAL;
		}
		if (i + 1U >= sequence_size || i >= ZUI_PREDICTIVE_MAX_SEQUENCE_LEN) {
			sequence[0] = '\0';
			return -ENOSPC;
		}
		sequence[i] = key;
	}

	if (i == 0U) {
		sequence[0] = '\0';
		return -EINVAL;
	}

	sequence[i] = '\0';
	return 0;
}

static int zui_predictive_encode_sequence(const char *sequence, uint32_t *key, size_t *len)
{
	uint32_t encoded = 1U;
	size_t sequence_len = 0U;

	if (sequence == NULL || key == NULL) {
		return -EINVAL;
	}

	for (size_t i = 0U; sequence[i] != '\0'; i++) {
		char ch = sequence[i];

		if (ch < '2' || ch > '9') {
			return -EINVAL;
		}
		if (sequence_len >= ZUI_PREDICTIVE_MAX_SEQUENCE_LEN) {
			return -ENOSPC;
		}

		encoded = (encoded << 3) | (uint32_t)(ch - '2');
		sequence_len++;
	}

	if (sequence_len == 0U) {
		return -EINVAL;
	}

	*key = encoded;
	if (len != NULL) {
		*len = sequence_len;
	}
	return 0;
}

static size_t zui_predictive_lower_bound_key(uint32_t key)
{
	size_t lo = 0U;
	size_t hi = zui_predictive_dict_entry_count;

	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2U;
		uint32_t entry_key = zui_predictive_dict_keys[mid];

		if (entry_key < key) {
			lo = mid + 1U;
		} else {
			hi = mid;
		}
	}

	return lo;
}

static size_t zui_predictive_find_key(uint32_t key)
{
	size_t lo = zui_predictive_lower_bound_key(key);

	if (lo >= zui_predictive_dict_entry_count || zui_predictive_dict_keys[lo] != key) {
		return SIZE_MAX;
	}

	return lo;
}

static size_t zui_predictive_static_candidate_count_by_index(size_t entry_index)
{
	const char *cursor;
	const char *end;
	size_t count = 0U;

	if (entry_index >= zui_predictive_dict_entry_count) {
		return 0U;
	}

	cursor = &zui_predictive_dict_candidate_blob
			 [zui_predictive_dict_candidate_starts[entry_index]];
	end = &zui_predictive_dict_candidate_blob
		      [zui_predictive_dict_candidate_starts[entry_index + 1U]];

	while (cursor < end) {
		size_t len = strlen(cursor);

		if (len == 0U || cursor + len > end) {
			break;
		}
		count++;
		cursor += len + 1U;
	}

	return count;
}

static const char *zui_predictive_static_candidate_by_index(size_t entry_index, size_t index)
{
	const char *cursor;
	const char *end;
	size_t current = 0U;

	if (entry_index >= zui_predictive_dict_entry_count) {
		return NULL;
	}

	cursor = &zui_predictive_dict_candidate_blob
			 [zui_predictive_dict_candidate_starts[entry_index]];
	end = &zui_predictive_dict_candidate_blob
		      [zui_predictive_dict_candidate_starts[entry_index + 1U]];

	while (cursor < end) {
		size_t len = strlen(cursor);

		if (len == 0U || cursor + len > end) {
			break;
		}
		if (current == index) {
			return cursor;
		}
		current++;
		cursor += len + 1U;
	}

	return NULL;
}

static size_t zui_predictive_static_candidate_count(const char *sequence)
{
	uint32_t key;
	size_t entry_index;

	if (zui_predictive_encode_sequence(sequence, &key, NULL) != 0) {
		return 0U;
	}

	entry_index = zui_predictive_find_key(key);
	return zui_predictive_static_candidate_count_by_index(entry_index);
}

static const char *zui_predictive_static_candidate(const char *sequence, size_t index)
{
	uint32_t key;
	size_t entry_index;

	if (zui_predictive_encode_sequence(sequence, &key, NULL) != 0) {
		return NULL;
	}

	entry_index = zui_predictive_find_key(key);
	return zui_predictive_static_candidate_by_index(entry_index, index);
}

static bool zui_predictive_static_has_sequence_prefix(const char *sequence)
{
	uint32_t key;
	size_t prefix_len;

	if (zui_predictive_encode_sequence(sequence, &key, &prefix_len) != 0) {
		return false;
	}

	for (size_t len = prefix_len; len <= ZUI_PREDICTIVE_MAX_SEQUENCE_LEN; len++) {
		uint8_t shift = (uint8_t)(3U * (len - prefix_len));
		uint32_t range_start = key << shift;
		uint32_t range_end = ((key + 1U) << shift) - 1U;
		size_t index = zui_predictive_lower_bound_key(range_start);

		if (index < zui_predictive_dict_entry_count &&
		    zui_predictive_dict_keys[index] <= range_end) {
			return true;
		}
	}

	return false;
}

size_t zui_predictive_candidate_count(const char *sequence)
{
	return zui_predictive_static_candidate_count(sequence);
}

const char *zui_predictive_candidate(const char *sequence, size_t index)
{
	return zui_predictive_static_candidate(sequence, index);
}

bool zui_predictive_has_sequence_prefix(const char *sequence)
{
	return zui_predictive_static_has_sequence_prefix(sequence);
}
