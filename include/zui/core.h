/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ZUI core lifecycle API
 */

#ifndef MESHBUS_INCLUDE_ZUI_CORE_H_
#define MESHBUS_INCLUDE_ZUI_CORE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zui_host;

struct zui_version {
	uint16_t major;
	uint16_t minor;
	uint16_t patch;
};

struct zui_runtime_stats {
	bool heap_stats_available;
	size_t heap_free_bytes;
	size_t heap_allocated_bytes;
	size_t heap_max_allocated_bytes;
};

int zui_init(void);
int zui_deinit(void);
struct zui_host *zui_get_default_host(void);
struct zui_version zui_get_version(void);
int zui_get_runtime_stats(struct zui_runtime_stats *stats);

#ifdef __cplusplus
}
#endif

#endif /* MESHBUS_INCLUDE_ZUI_CORE_H_ */
