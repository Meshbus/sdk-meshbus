/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/linker/iterable_sections.h>

#if defined(CONFIG_MESHBUS_POWER)
ITERABLE_SECTION_RAM(meshbus_power_action_callback, Z_LINK_ITERABLE_SUBALIGN)
#endif