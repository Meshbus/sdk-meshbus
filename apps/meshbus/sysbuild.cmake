# SPDX-License-Identifier: Apache-2.0

include("${APP_DIR}/cmake/board_profile.cmake")
meshbus_board_profile("${APP_DIR}" meshbus_profile)
get_filename_component(meshbus_profile_dir "${meshbus_profile}" DIRECTORY)

if(SB_CONFIG_BOOTLOADER_MCUBOOT AND
   NOT SB_CONFIG_BOOT_SIGNATURE_TYPE_NONE AND
   "${SB_CONFIG_BOOT_SIGNATURE_KEY_FILE}" STREQUAL "")
  message(FATAL_ERROR
    "Meshbus requires an explicit image key. Pass "
    "'-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"/absolute/path/to/key.pem\"'")
endif()

zephyr_file(CONF_FILES "${meshbus_profile_dir}"
  KCONF mcuboot_board_conf
  SUFFIX mcuboot
)
zephyr_file(CONF_FILES "${meshbus_profile_dir}"
  DTS mcuboot_board_overlay
  SUFFIX mcuboot
)

list(APPEND mcuboot_EXTRA_CONF_FILE ${mcuboot_board_conf})
list(REMOVE_DUPLICATES mcuboot_EXTRA_CONF_FILE)
set(mcuboot_EXTRA_CONF_FILE "${mcuboot_EXTRA_CONF_FILE}"
  CACHE INTERNAL "Meshbus device-specific MCUboot configuration")

list(APPEND mcuboot_EXTRA_DTC_OVERLAY_FILE ${mcuboot_board_overlay})
list(REMOVE_DUPLICATES mcuboot_EXTRA_DTC_OVERLAY_FILE)
set(mcuboot_EXTRA_DTC_OVERLAY_FILE "${mcuboot_EXTRA_DTC_OVERLAY_FILE}"
  CACHE INTERNAL "Meshbus device-specific MCUboot devicetree overlays")
