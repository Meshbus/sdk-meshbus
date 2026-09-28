/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MBS_SERVICES_INDICATOR_LIGHT_INTERNAL_H_
#define MBS_SERVICES_INDICATOR_LIGHT_INTERNAL_H_

#include <zephyr/kernel.h>
#include <indicator/indicator.h>

#if DT_HAS_CHOSEN(meshbus_indicator_light_active)
#define MBS_INDICATOR_LIGHT_DEVICE_EXIST 1
#else
#define MBS_INDICATOR_LIGHT_DEVICE_EXIST 0
#endif

#if IS_ENABLED(CONFIG_MBS_INDICATOR_LIGHT) && IS_ENABLED(MBS_INDICATOR_LIGHT_DEVICE_EXIST)

bool indicator_light_is_ready(void);
int mbs_indicator_light_init(void);
bool indicator_light_supports_rgb(void);
void indicator_light_set_idle_color(uint8_t r, uint8_t g, uint8_t b);
void indicator_light_set_idle(uint32_t on_duration_ms, uint32_t off_duration_ms);
void indicator_light_set_idle_enabled(bool enable);
void indicator_light_set_enabled(bool enable);
void indicator_light_set_color(uint8_t r, uint8_t g, uint8_t b);
int indicator_light_play(uint32_t on_duration_ms, uint32_t off_duration_ms, uint8_t count);
void indicator_light_stop(void);

#else /* Stubs when light module is disabled */

static inline bool indicator_light_is_ready(void)
{
	return false;
}
static inline int mbs_indicator_light_init(void)
{
	return 0;
}
static inline bool indicator_light_supports_rgb(void)
{
	return false;
}
static inline void indicator_light_set_idle_color(uint8_t r, uint8_t g, uint8_t b)
{
	ARG_UNUSED(r);
	ARG_UNUSED(g);
	ARG_UNUSED(b);
}
static inline void indicator_light_set_idle(uint32_t on_ms, uint32_t off_ms)
{
	ARG_UNUSED(on_ms);
	ARG_UNUSED(off_ms);
}
static inline void indicator_light_set_idle_enabled(bool enable)
{
	ARG_UNUSED(enable);
}
static inline void indicator_light_set_enabled(bool enable)
{
	ARG_UNUSED(enable);
}
static inline void indicator_light_set_color(uint8_t r, uint8_t g, uint8_t b)
{
	ARG_UNUSED(r);
	ARG_UNUSED(g);
	ARG_UNUSED(b);
}
static inline int indicator_light_play(uint32_t on_ms, uint32_t off_ms, uint8_t count)
{
	ARG_UNUSED(on_ms);
	ARG_UNUSED(off_ms);
	ARG_UNUSED(count);
	return -ENODEV;
}
static inline void indicator_light_stop(void)
{
}

#endif /* CONFIG_MBS_INDICATOR_LIGHT */

#endif /* MBS_SERVICES_INDICATOR_LIGHT_INTERNAL_H_ */
