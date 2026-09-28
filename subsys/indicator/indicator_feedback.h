/* SPDX-FileCopyrightText: 2026 FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MESHBUS_INDICATOR_FEEDBACK_INTERNAL_H_
#define MESHBUS_INDICATOR_FEEDBACK_INTERNAL_H_
#include <stdbool.h>
void mbs_indicator_feedback_configure(bool enabled, bool messages, bool system);
void mbs_indicator_feedback_cancel(void);
#endif
