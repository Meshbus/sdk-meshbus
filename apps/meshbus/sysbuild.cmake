# SPDX-FileCopyrightText: FoBE Studio
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

if(SB_CONFIG_BOOTLOADER_MCUBOOT AND NOT SB_CONFIG_BOOT_SIGNATURE_TYPE_NONE)
  # Match native sysbuild's per-key whitespace handling and the CMake variable
  # expansion used by both MCUboot and the application's signing step.
  string(REPLACE "," ";" meshbus_image_keys "${SB_CONFIG_BOOT_SIGNATURE_KEY_FILE}")
  foreach(meshbus_image_key IN LISTS meshbus_image_keys)
    string(STRIP "${meshbus_image_key}" meshbus_image_key)
    if("${meshbus_image_key}" STREQUAL "")
      continue()
    endif()
    string(CONFIGURE "${meshbus_image_key}" meshbus_image_key)
    file(REAL_PATH "${meshbus_image_key}" meshbus_image_key)
    foreach(meshbus_example_key root-rsa-2048.pem root-ec-p256.pem root-ed25519.pem)
      file(REAL_PATH "${ZEPHYR_MCUBOOT_MODULE_DIR}/${meshbus_example_key}" meshbus_example_path)
      if("${meshbus_image_key}" STREQUAL "${meshbus_example_path}")
        message(FATAL_ERROR "Meshbus authentication requires a caller-owned key, not an MCUboot example key")
      endif()
    endforeach()
  endforeach()
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
