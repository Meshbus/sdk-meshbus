/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MBS_SERVICES_INDICATOR_BUZZER_INTERNAL_H_
#define MBS_SERVICES_INDICATOR_BUZZER_INTERNAL_H_

#include <zephyr/kernel.h>
#include <indicator/indicator.h>

#if DT_HAS_CHOSEN(meshbus_indicator_buzzer)
#define MBS_INDICATOR_BUZZER_DEVICE_EXIST 1
#else
#define MBS_INDICATOR_BUZZER_DEVICE_EXIST 0
#endif

#if IS_ENABLED(CONFIG_MBS_INDICATOR_BUZZER)

bool indicator_buzzer_is_ready(void);
int mbs_indicator_buzzer_init(void);
int indicator_buzzer_play(const struct indicator_buzzer_melody *melody);
int indicator_buzzer_play_rtttl(const char *rtttl_string);
void indicator_buzzer_stop(void);
int indicator_buzzer_play_sync(const struct indicator_buzzer_melody *melody, k_timeout_t timeout);

#else /* Stubs when buzzer module is disabled */

static inline bool indicator_buzzer_is_ready(void)
{
	return false;
}
static inline int mbs_indicator_buzzer_init(void)
{
	return 0;
}
static inline int indicator_buzzer_play(const struct indicator_buzzer_melody *melody)
{
	ARG_UNUSED(melody);
	return -ENODEV;
}
static inline int indicator_buzzer_play_rtttl(const char *rtttl_string)
{
	ARG_UNUSED(rtttl_string);
	return -ENODEV;
}
static inline void indicator_buzzer_stop(void)
{
}
static inline int indicator_buzzer_play_sync(const struct indicator_buzzer_melody *melody,
					     k_timeout_t timeout)
{
	ARG_UNUSED(melody);
	ARG_UNUSED(timeout);
	return -ENODEV;
}

#endif /* CONFIG_MBS_INDICATOR_BUZZER */

#endif /* MBS_SERVICES_INDICATOR_BUZZER_INTERNAL_H_ */
