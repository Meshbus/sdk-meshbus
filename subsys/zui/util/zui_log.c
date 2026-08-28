/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file zui_log.c
 * @brief ZUI Framework Logging Module Registration
 *
 * This file registers the central logging module for the ZUI framework.
 * Other ZUI files should use LOG_MODULE_DECLARE(zui) to reference this module.
 *
 * Also provides helper functions for logging from macro-generated code
 * to avoid code bloat from multiple LOG_MODULE_DECLARE instances.
 */

#include <zephyr/logging/log.h>

#include "zui_log.h"

LOG_MODULE_REGISTER(zui, CONFIG_ZUI_LOG_LEVEL);

void zui_array_log_full(const char *array_name, const char *operation)
{
	LOG_WRN("%s: array full, %s dropped", array_name, operation);
}

void zui_array_log_alloc_failed(const char *array_name, const char *operation)
{
	LOG_WRN("%s: allocation failed in %s", array_name, operation);
}
