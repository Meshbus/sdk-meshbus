# SPDX-FileCopyrightText: FoBE Studio
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MCUBOOT_BOOT_BLUETOOTH)
	zephyr_library()
	zephyr_library_sources(src/ble_transport.c)
	target_link_options(app PUBLIC
		-Wl,--wrap=boot_serial_start
	)
	if(CONFIG_BOOT_SERIAL_WAIT_FOR_DFU)
		target_link_options(app PUBLIC
			-Wl,--wrap=boot_serial_check_start
		)
	endif()
endif()
