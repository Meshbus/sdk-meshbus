/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FOBE_SUBSYS_MBS_SHELL_H_
#define FOBE_SUBSYS_MBS_SHELL_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/shell/shell.h>

#ifdef __cplusplus
extern "C" {
#endif

int mbs_shell_parse_u64_arg(const char *arg, uint64_t *out);
int mbs_shell_parse_u32_arg(const char *arg, uint32_t *out);
int mbs_shell_parse_u16_arg(const char *arg, uint16_t *out);
int mbs_shell_parse_u8_arg(const char *arg, uint8_t *out);
int mbs_shell_parse_i32_arg(const char *arg, int32_t *out);
int mbs_shell_parse_i16_arg(const char *arg, int16_t *out);
int mbs_shell_parse_bool_arg(const char *arg, bool *out);
int mbs_shell_parse_float_arg(const char *arg, float *out);
void mbs_shell_error(const struct shell *sh, int32_t err);
void mbs_shell_invalid(const struct shell *sh);

#ifdef __cplusplus
}
#endif

#endif /* FOBE_SUBSYS_MBS_SHELL_H_ */
