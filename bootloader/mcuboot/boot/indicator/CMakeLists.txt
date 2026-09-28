# SPDX-FileCopyrightText: FoBE Studio
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MCUBOOT_INDICATOR)
	zephyr_library()
	zephyr_library_sources(src/status_indication.c)
	if(CONFIG_MCUBOOT_SERIAL)
		target_link_options(app PUBLIC
			-Wl,--wrap=console_read
			-Wl,--wrap=console_write
		)
	endif()
endif()
