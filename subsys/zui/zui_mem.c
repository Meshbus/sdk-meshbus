/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file zui_mem.c
 * @brief ZUI Framework Memory Management Implementation
 *
 * Modeled after LVGL's lvgl_mem.c for Zephyr.
 */

#include "zui_mem.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zui);

#if !IS_ENABLED(CONFIG_ZUI_USE_SYSTEM_HEAP)
static char __aligned(8) zui_heap_mem[ZUI_HEAP_SIZE];
static struct sys_heap zui_heap;
static struct k_spinlock zui_heap_lock;
#endif

void *zui_malloc(size_t size)
{
#if IS_ENABLED(CONFIG_ZUI_USE_SYSTEM_HEAP)
	return k_malloc(size);
#else
	/* Dedicated ZUI heap */
	k_spinlock_key_t key;
	void *ret;

	key = k_spin_lock(&zui_heap_lock);
	ret = sys_heap_alloc(&zui_heap, size);
	k_spin_unlock(&zui_heap_lock, key);

	return ret;
#endif
}

void *zui_realloc(void *ptr, size_t size)
{
#if IS_ENABLED(CONFIG_ZUI_USE_SYSTEM_HEAP)
	return k_realloc(ptr, size);
#else
	/* Dedicated ZUI heap */
	k_spinlock_key_t key;
	void *ret;

	key = k_spin_lock(&zui_heap_lock);
	ret = sys_heap_realloc(&zui_heap, ptr, size);
	k_spin_unlock(&zui_heap_lock, key);

	return ret;
#endif
}

void zui_free(void *ptr)
{
	if (ptr == NULL) {
		return;
	}

#if IS_ENABLED(CONFIG_ZUI_USE_SYSTEM_HEAP)
	k_free(ptr);
#else
	/* Dedicated ZUI heap */
	k_spinlock_key_t key;

	key = k_spin_lock(&zui_heap_lock);
	sys_heap_free(&zui_heap, ptr);
	k_spin_unlock(&zui_heap_lock, key);
#endif
}

void zui_print_heap_info(bool dump_chunks)
{
#if IS_ENABLED(CONFIG_ZUI_USE_SYSTEM_HEAP)
	ARG_UNUSED(dump_chunks);
	LOG_WRN_ONCE("ZUI heap info not available when using system heap");
#else
#ifdef CONFIG_SYS_HEAP_INFO
	k_spinlock_key_t key;

	key = k_spin_lock(&zui_heap_lock);
	sys_heap_print_info(&zui_heap, dump_chunks);
	k_spin_unlock(&zui_heap_lock, key);
#else
	ARG_UNUSED(dump_chunks);
	LOG_WRN_ONCE("Enable CONFIG_SYS_HEAP_INFO for heap info dump");
#endif
#endif
}

void zui_heap_stats(struct sys_memory_stats *stats)
{
	if (stats == NULL) {
		return;
	}

#if IS_ENABLED(CONFIG_ZUI_USE_SYSTEM_HEAP)
	ARG_UNUSED(stats);
	LOG_WRN_ONCE("ZUI heap stats not available when using system heap");
#else
#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
	k_spinlock_key_t key;

	key = k_spin_lock(&zui_heap_lock);
	sys_heap_runtime_stats_get(&zui_heap, stats);
	k_spin_unlock(&zui_heap_lock, key);
#else
	ARG_UNUSED(stats);
	LOG_WRN_ONCE("Enable CONFIG_SYS_HEAP_RUNTIME_STATS for heap statistics");
#endif
#endif
}

void zui_heap_init(void)
{
#if IS_ENABLED(CONFIG_ZUI_USE_SYSTEM_HEAP)
	LOG_INF("ZUI allocations use system heap (k_malloc)");
#else
	sys_heap_init(&zui_heap, zui_heap_mem, ZUI_HEAP_SIZE);
	LOG_INF("ZUI heap initialized: %u bytes", ZUI_HEAP_SIZE);
#endif
}

static int zui_mem_sys_init(void)
{
	zui_heap_init();
	return 0;
}

SYS_INIT(zui_mem_sys_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
