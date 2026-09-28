/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "meshcore_qr.h"

#include <stddef.h>
#include <string.h>
#include <zephyr/sys/util.h>

#define QR_DATA_CODEWORDS 55
#define QR_ECC_CODEWORDS 15
#define QR_TOTAL_CODEWORDS (QR_DATA_CODEWORDS + QR_ECC_CODEWORDS)
#define QR_MAX_ALPHANUMERIC_CHARS 77
#define QR_MASK 0

static bool qr_get_bit(const uint8_t *bits, int x, int y)
{
	size_t bit = (size_t)y * MESHCORE_QR_SIZE + (size_t)x;

	return (bits[bit >> 3] & BIT(bit & 7U)) != 0U;
}

static void qr_set_bit(uint8_t *bits, int x, int y, bool dark)
{
	size_t bit;

	if (x < 0 || x >= MESHCORE_QR_SIZE || y < 0 || y >= MESHCORE_QR_SIZE) {
		return;
	}

	bit = (size_t)y * MESHCORE_QR_SIZE + (size_t)x;
	if (dark) {
		bits[bit >> 3] |= BIT(bit & 7U);
	} else {
		bits[bit >> 3] &= (uint8_t)~BIT(bit & 7U);
	}
}

static void qr_set_function(struct meshcore_qr_code *code, uint8_t *reserved, int x, int y,
			    bool dark)
{
	qr_set_bit(reserved, x, y, true);
	qr_set_bit(code->modules, x, y, dark);
}

static int qr_abs(int value)
{
	return value < 0 ? -value : value;
}

static int qr_alphanumeric_value(char ch)
{
	if (ch >= '0' && ch <= '9') {
		return ch - '0';
	}
	if (ch >= 'A' && ch <= 'Z') {
		return ch - 'A' + 10;
	}

	switch (ch) {
	case ' ':
		return 36;
	case '$':
		return 37;
	case '%':
		return 38;
	case '*':
		return 39;
	case '+':
		return 40;
	case '-':
		return 41;
	case '.':
		return 42;
	case '/':
		return 43;
	case ':':
		return 44;
	default:
		return -1;
	}
}

static bool qr_append_bits(uint8_t *data, int *bit_len, unsigned int value, int count)
{
	if (*bit_len + count > QR_DATA_CODEWORDS * 8) {
		return false;
	}

	for (int i = count - 1; i >= 0; i--) {
		if (((value >> i) & 1U) != 0U) {
			data[*bit_len >> 3] |= BIT(7 - (*bit_len & 7));
		}
		(*bit_len)++;
	}

	return true;
}

static bool qr_build_data(const char *text, uint8_t data[QR_DATA_CODEWORDS])
{
	size_t len;
	int bit_len = 0;
	size_t i = 0U;

	len = strlen(text);
	if (len > QR_MAX_ALPHANUMERIC_CHARS) {
		return false;
	}

	memset(data, 0, QR_DATA_CODEWORDS);
	if (!qr_append_bits(data, &bit_len, 0x2U, 4) ||
	    !qr_append_bits(data, &bit_len, (unsigned int)len, 9)) {
		return false;
	}

	while (i + 1U < len) {
		int a = qr_alphanumeric_value(text[i]);
		int b = qr_alphanumeric_value(text[i + 1U]);

		if (a < 0 || b < 0 ||
		    !qr_append_bits(data, &bit_len, (unsigned int)(a * 45 + b), 11)) {
			return false;
		}
		i += 2U;
	}

	if (i < len) {
		int a = qr_alphanumeric_value(text[i]);

		if (a < 0 || !qr_append_bits(data, &bit_len, (unsigned int)a, 6)) {
			return false;
		}
	}

	if (!qr_append_bits(data, &bit_len, 0U, MIN(4, QR_DATA_CODEWORDS * 8 - bit_len))) {
		return false;
	}
	while ((bit_len & 7) != 0) {
		if (!qr_append_bits(data, &bit_len, 0U, 1)) {
			return false;
		}
	}

	for (int idx = bit_len >> 3; idx < QR_DATA_CODEWORDS; idx++) {
		data[idx] = ((idx - (bit_len >> 3)) & 1) == 0 ? 0xEC : 0x11;
	}

	return true;
}

static uint8_t qr_gf_multiply(uint8_t x, uint8_t y)
{
	uint8_t z = 0U;

	for (int i = 7; i >= 0; i--) {
		z = (uint8_t)((z << 1) ^ (((z >> 7) & 1U) * 0x11DU));
		if (((y >> i) & 1U) != 0U) {
			z ^= x;
		}
	}

	return z;
}

static void qr_rs_generator(uint8_t generator[QR_ECC_CODEWORDS])
{
	uint8_t root = 1U;

	memset(generator, 0, QR_ECC_CODEWORDS);
	generator[QR_ECC_CODEWORDS - 1] = 1U;

	for (int i = 0; i < QR_ECC_CODEWORDS; i++) {
		for (int j = 0; j < QR_ECC_CODEWORDS; j++) {
			generator[j] = qr_gf_multiply(generator[j], root);
			if (j + 1 < QR_ECC_CODEWORDS) {
				generator[j] ^= generator[j + 1];
			}
		}
		root = qr_gf_multiply(root, 0x02U);
	}
}

static void qr_append_ecc(const uint8_t data[QR_DATA_CODEWORDS],
			  uint8_t codewords[QR_TOTAL_CODEWORDS])
{
	uint8_t generator[QR_ECC_CODEWORDS];
	uint8_t remainder[QR_ECC_CODEWORDS] = {0};

	qr_rs_generator(generator);
	memcpy(codewords, data, QR_DATA_CODEWORDS);

	for (int i = 0; i < QR_DATA_CODEWORDS; i++) {
		uint8_t factor = data[i] ^ remainder[0];

		memmove(&remainder[0], &remainder[1], QR_ECC_CODEWORDS - 1);
		remainder[QR_ECC_CODEWORDS - 1] = 0U;
		for (int j = 0; j < QR_ECC_CODEWORDS; j++) {
			remainder[j] ^= qr_gf_multiply(generator[j], factor);
		}
	}

	memcpy(&codewords[QR_DATA_CODEWORDS], remainder, QR_ECC_CODEWORDS);
}

static void qr_draw_finder(struct meshcore_qr_code *code, uint8_t *reserved, int left, int top)
{
	for (int dy = -1; dy <= 7; dy++) {
		for (int dx = -1; dx <= 7; dx++) {
			bool inner = dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4;
			bool border = dx == 0 || dx == 6 || dy == 0 || dy == 6;
			bool in_pattern = dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6;

			qr_set_function(code, reserved, left + dx, top + dy,
					in_pattern && (border || inner));
		}
	}
}

static void qr_draw_alignment(struct meshcore_qr_code *code, uint8_t *reserved, int center_x,
			      int center_y)
{
	for (int dy = -2; dy <= 2; dy++) {
		for (int dx = -2; dx <= 2; dx++) {
			int dist = MAX(qr_abs(dx), qr_abs(dy));

			qr_set_function(code, reserved, center_x + dx, center_y + dy,
					dist == 2 || dist == 0);
		}
	}
}

static void qr_draw_timing(struct meshcore_qr_code *code, uint8_t *reserved)
{
	for (int i = 0; i < MESHCORE_QR_SIZE; i++) {
		if (!qr_get_bit(reserved, i, 6)) {
			qr_set_function(code, reserved, i, 6, (i & 1) == 0);
		}
		if (!qr_get_bit(reserved, 6, i)) {
			qr_set_function(code, reserved, 6, i, (i & 1) == 0);
		}
	}
}

static void qr_reserve_format(struct meshcore_qr_code *code, uint8_t *reserved)
{
	for (int i = 0; i <= 5; i++) {
		qr_set_function(code, reserved, 8, i, false);
	}
	qr_set_function(code, reserved, 8, 7, false);
	qr_set_function(code, reserved, 8, 8, false);
	qr_set_function(code, reserved, 7, 8, false);
	for (int i = 9; i < 15; i++) {
		qr_set_function(code, reserved, 14 - i, 8, false);
	}

	for (int i = 0; i < 8; i++) {
		qr_set_function(code, reserved, MESHCORE_QR_SIZE - 1 - i, 8, false);
	}
	for (int i = 8; i < 15; i++) {
		qr_set_function(code, reserved, 8, MESHCORE_QR_SIZE - 15 + i, false);
	}
	qr_set_function(code, reserved, 8, MESHCORE_QR_SIZE - 8, true);
}

static void qr_draw_function_patterns(struct meshcore_qr_code *code, uint8_t *reserved)
{
	qr_draw_finder(code, reserved, 0, 0);
	qr_draw_finder(code, reserved, MESHCORE_QR_SIZE - 7, 0);
	qr_draw_finder(code, reserved, 0, MESHCORE_QR_SIZE - 7);
	qr_draw_alignment(code, reserved, 22, 22);
	qr_draw_timing(code, reserved);
	qr_reserve_format(code, reserved);
}

static bool qr_mask_invert(int x, int y)
{
	return ((x + y) & 1) == 0;
}

static void qr_draw_codewords(struct meshcore_qr_code *code, const uint8_t *reserved,
			      const uint8_t codewords[QR_TOTAL_CODEWORDS])
{
	int bit = 0;

	for (int right = MESHCORE_QR_SIZE - 1; right >= 1; right -= 2) {
		if (right == 6) {
			right = 5;
		}

		for (int vert = 0; vert < MESHCORE_QR_SIZE; vert++) {
			bool upward = ((right + 1) & 2) == 0;
			int y = upward ? MESHCORE_QR_SIZE - 1 - vert : vert;

			for (int j = 0; j < 2; j++) {
				int x = right - j;

				if (qr_get_bit(reserved, x, y)) {
					continue;
				}
				if (bit >= QR_TOTAL_CODEWORDS * 8) {
					return;
				}

				qr_set_bit(code->modules, x, y,
					   (codewords[bit >> 3] & BIT(7 - (bit & 7))) != 0U);
				bit++;
			}
		}
	}
}

static void qr_apply_mask(struct meshcore_qr_code *code, const uint8_t *reserved)
{
	for (int y = 0; y < MESHCORE_QR_SIZE; y++) {
		for (int x = 0; x < MESHCORE_QR_SIZE; x++) {
			if (qr_get_bit(reserved, x, y) || !qr_mask_invert(x, y)) {
				continue;
			}
			qr_set_bit(code->modules, x, y, !qr_get_bit(code->modules, x, y));
		}
	}
}

static bool qr_format_bit(int bits, int index)
{
	return ((bits >> index) & 1) != 0;
}

static void qr_draw_format(struct meshcore_qr_code *code)
{
	int data = (1 << 3) | QR_MASK;
	int rem = data;
	int bits;

	for (int i = 0; i < 10; i++) {
		rem = (rem << 1) ^ (((rem >> 9) & 1) * 0x537);
	}
	bits = ((data << 10) | rem) ^ 0x5412;

	for (int i = 0; i <= 5; i++) {
		qr_set_bit(code->modules, 8, i, qr_format_bit(bits, i));
	}
	qr_set_bit(code->modules, 8, 7, qr_format_bit(bits, 6));
	qr_set_bit(code->modules, 8, 8, qr_format_bit(bits, 7));
	qr_set_bit(code->modules, 7, 8, qr_format_bit(bits, 8));
	for (int i = 9; i < 15; i++) {
		qr_set_bit(code->modules, 14 - i, 8, qr_format_bit(bits, i));
	}

	for (int i = 0; i < 8; i++) {
		qr_set_bit(code->modules, MESHCORE_QR_SIZE - 1 - i, 8, qr_format_bit(bits, i));
	}
	for (int i = 8; i < 15; i++) {
		qr_set_bit(code->modules, 8, MESHCORE_QR_SIZE - 15 + i,
			   qr_format_bit(bits, i));
	}
	qr_set_bit(code->modules, 8, MESHCORE_QR_SIZE - 8, true);
}

bool meshcore_qr_encode_text(const char *text, struct meshcore_qr_code *out)
{
	uint8_t reserved[MESHCORE_QR_MODULE_BYTES] = {0};
	uint8_t data[QR_DATA_CODEWORDS];
	uint8_t codewords[QR_TOTAL_CODEWORDS];

	if (text == NULL || out == NULL) {
		return false;
	}
	if (!qr_build_data(text, data)) {
		return false;
	}

	memset(out, 0, sizeof(*out));
	out->size = MESHCORE_QR_SIZE;

	qr_append_ecc(data, codewords);
	qr_draw_function_patterns(out, reserved);
	qr_draw_codewords(out, reserved, codewords);
	qr_apply_mask(out, reserved);
	qr_draw_format(out);

	return true;
}

bool meshcore_qr_get_module(const struct meshcore_qr_code *code, int x, int y)
{
	if (code == NULL || code->size != MESHCORE_QR_SIZE || x < 0 || y < 0 ||
	    x >= code->size || y >= code->size) {
		return false;
	}

	return qr_get_bit(code->modules, x, y);
}
