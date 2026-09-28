/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MESHCORE_QR_SIZE 29
#define MESHCORE_QR_MODULE_BYTES ((MESHCORE_QR_SIZE * MESHCORE_QR_SIZE + 7) / 8)

struct meshcore_qr_code {
	uint8_t modules[MESHCORE_QR_MODULE_BYTES];
	int size;
};

bool meshcore_qr_encode_text(const char *text, struct meshcore_qr_code *out);
bool meshcore_qr_get_module(const struct meshcore_qr_code *code, int x, int y);

#ifdef __cplusplus
}
#endif
