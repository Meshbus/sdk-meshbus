# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

zephyr_library_sources(settings.c time.c)
zephyr_library_sources_ifdef(CONFIG_MESHBUS_SHELL shell.c)
zephyr_library_sources_ifdef(CONFIG_MCUMGR mgmt.c)
