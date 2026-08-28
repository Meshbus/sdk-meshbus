/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ZUI transient toast feedback API
 */

#ifndef ZEPHYR_INCLUDE_ZUI_TOAST_H_
#define ZEPHYR_INCLUDE_ZUI_TOAST_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/zui/assets.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zui_host;

struct zui_toast_config {
	/** Strings are copied when shown; icon remains borrowed until the toast is dismissed. */
	const char *title;
	const char *text;
	const struct zui_icon *icon;
	uint32_t timeout_ms;
};

uint32_t zui_toast_show(struct zui_host *host, const struct zui_toast_config *config);
int zui_toast_dismiss(struct zui_host *host, uint32_t toast_id);
bool zui_toast_is_visible(const struct zui_host *host, uint32_t toast_id);
int zui_toast_dismiss_all(struct zui_host *host);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ZUI_TOAST_H_ */
