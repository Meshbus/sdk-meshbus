# SPDX-FileCopyrightText: FoBE Studio
# SPDX-License-Identifier: Apache-2.0

zephyr_sources_ifdef(CONFIG_MCUBOOT_DELTA flash_delta.c)
zephyr_sources_ifdef(CONFIG_MCUBOOT_DELTA_SHELL flash_delta_shell.c)

zephyr_library_link_libraries_ifdef(CONFIG_MCUBOOT_DELTA MCUBOOT_BOOTUTIL)
