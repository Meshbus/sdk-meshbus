# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MBS_FIRMWARE OR CONFIG_MBS_FIRMWARE_IMAGE_IDENTITY)
	zephyr_library_named(mbs_firmware)
	zephyr_library_sources(image.c)
endif()

if(CONFIG_MBS_FIRMWARE)
	zephyr_library_add_dependencies(mbs_proto)
	zephyr_library_include_directories(
		${CMAKE_CURRENT_SOURCE_DIR}/../mgmt
	)
if(CONFIG_BT AND CONFIG_SOC_FLASH_NRF_RADIO_SYNC_NONE)
    message(FATAL_ERROR
        "Meshbus Firmware with Bluetooth requires radio-synchronized flash operations")
endif()

if(CONFIG_BT AND CONFIG_SOC_FLASH_NRF_RRAM AND
   CONFIG_SOC_FLASH_NRF_RADIO_SYNC_TICKER AND
   CONFIG_NRF_RRAM_WRITE_BUFFER_SIZE LESS 4)
    message(FATAL_ERROR
        "Meshbus Firmware with Bluetooth ticker sync requires an RRAM write buffer of at least 4 words")
endif()

	zephyr_library_sources(firmware.c manifest.c mgmt.c)
	zephyr_library_sources_ifdef(CONFIG_MBS_FIRMWARE_MCUMGR_IMAGE
		img_mgmt.c)
	if(CONFIG_MBS_FIRMWARE_MCUMGR_IMAGE)
		zephyr_library_link_libraries(MCUBOOT_BOOTUTIL)
	endif()
endif()
