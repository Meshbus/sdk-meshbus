# Copyright (c) 2025 FoBE Projects
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_CHARGER_COMPOSITE charger_composite.c)
