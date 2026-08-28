/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file zui_log.h
 * @brief ZUI Framework Logging Utilities
 *
 * Provides centralized logging helpers for the ZUI framework.
 * These functions avoid code bloat from LOG_MODULE_DECLARE in macros.
 */

#ifndef ZUI_LOG_H_
#define ZUI_LOG_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Log a warning for array full condition
 *
 * Used by fixed-capacity array macros to avoid inline LOG_MODULE_DECLARE.
 *
 * @param array_name Name of the array type
 * @param operation Operation that failed ("push_new" or "push_back")
 */
void zui_array_log_full(const char *array_name, const char *operation);

/**
 * @brief Log a warning for array allocation failure
 *
 * Used by dynamic array macros to avoid inline LOG_MODULE_DECLARE.
 *
 * @param array_name Name of the array type
 * @param operation Operation that failed ("push_new" or "push_back")
 */
void zui_array_log_alloc_failed(const char *array_name, const char *operation);

#ifdef __cplusplus
}
#endif

#endif /* ZUI_LOG_H_ */
