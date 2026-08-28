/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file zui_mem.h
 * @brief ZUI Framework Memory Management
 *
 * Provides sys_heap based dynamic memory allocation for ZUI framework.
 * Modeled after LVGL's memory management approach for Zephyr.
 */

#pragma once

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/*                          Heap Size Configuration                           */
/* -------------------------------------------------------------------------- */

#ifndef CONFIG_ZUI_HEAP_SIZE
#define ZUI_HEAP_SIZE		16384
#else
#define ZUI_HEAP_SIZE		CONFIG_ZUI_HEAP_SIZE
#endif

/* -------------------------------------------------------------------------- */
/*                          Memory API                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the ZUI heap
 *
 * Called automatically during system initialization.
 */
void zui_heap_init(void);

/**
 * @brief Allocate memory from ZUI heap
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 */
void *zui_malloc(size_t size);

/**
 * @brief Reallocate memory from ZUI heap
 * @param ptr Pointer to previously allocated memory (or NULL)
 * @param size New size in bytes
 * @return Pointer to reallocated memory, or NULL on failure
 */
void *zui_realloc(void *ptr, size_t size);

/**
 * @brief Free memory back to ZUI heap
 * @param ptr Pointer to memory to free (NULL is safe)
 */
void zui_free(void *ptr);

/**
 * @brief Print heap information to the log
 * @param dump_chunks If true, dump detailed chunk information
 */
void zui_print_heap_info(bool dump_chunks);

/**
 * @brief Get heap runtime statistics
 * @param stats Pointer to sys_memory_stats structure to fill
 *
 * Requires CONFIG_SYS_HEAP_RUNTIME_STATS to be enabled.
 */
void zui_heap_stats(struct sys_memory_stats *stats);

/**
 * @brief Allocate zero-initialized memory from ZUI heap
 * @param n Number of elements
 * @param size Size of each element
 * @return Pointer to allocated memory, or NULL on failure
 */
static inline void *zui_calloc(size_t n, size_t size)
{
	if (size != 0U && n > (SIZE_MAX / size)) {
		return NULL;
	}

	void *p = zui_malloc(n * size);

	if (p) {
		memset(p, 0, n * size);
	}
	return p;
}

#ifdef __cplusplus
}
#endif
