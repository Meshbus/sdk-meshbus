# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MESHBUS_RADIO)
        zephyr_library_sources(
                radio.c
        )

        zephyr_library_sources_ifdef(CONFIG_MCUMGR
                mgmt.c
        )

        zephyr_library_sources_ifdef(CONFIG_MESHBUS_RADIO_SHELL
                shell.c
        )
endif()
