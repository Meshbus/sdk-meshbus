/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_ZUI_SCREEN_INTERNAL_H_
#define ZEPHYR_SUBSYS_ZUI_SCREEN_INTERNAL_H_

#include <zephyr/zui/screen.h>

typedef void (*zui_screen_invalidate_cb)(struct zui_screen *screen, void *user_data);

int zui_screen_set_invalidate_callback(struct zui_screen *screen,
				       zui_screen_invalidate_cb callback,
				       void *user_data);
int zui_screen_poll_tick(struct zui_screen *screen, uint32_t now_ms);
int32_t zui_screen_next_tick_timeout_ms(const struct zui_screen *screen, uint32_t now_ms);

#endif /* ZEPHYR_SUBSYS_ZUI_SCREEN_INTERNAL_H_ */
