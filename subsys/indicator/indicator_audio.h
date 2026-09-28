/* SPDX-FileCopyrightText: 2026 FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MESHBUS_INDICATOR_AUDIO_INTERNAL_H_
#define MESHBUS_INDICATOR_AUDIO_INTERNAL_H_
#include <indicator/indicator.h>
void mbs_indicator_audio_configure(const mbs_indicator_config *cfg);
void mbs_indicator_audio_submit(enum mbs_indicator_feedback event);
void mbs_indicator_audio_conditions(bool low_battery, bool fault);
bool mbs_indicator_audio_busy(void);
void mbs_indicator_audio_cancel(void);
#endif
