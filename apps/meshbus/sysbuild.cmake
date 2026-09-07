# SPDX-License-Identifier: Apache-2.0

set(c2_target "idea_mesh_tracker_c2/nrf54l15/cpuapp")

if("${BOARD}/${BOARD_QUALIFIERS}" STREQUAL "${c2_target}" AND
   "${SB_CONFIG_BOOT_SIGNATURE_KEY_FILE}" STREQUAL "")
  message(FATAL_ERROR
    "C2 requires an explicit Ed25519 image key. Pass "
    "'-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"/absolute/path/to/key.pem\"'")
endif()

if(SB_CONFIG_MESHBUS_C2_EXTERNAL_SIGNING)
  if(NOT "${BOARD}/${BOARD_QUALIFIERS}" STREQUAL "${c2_target}")
    message(FATAL_ERROR "External signing is supported only for the C2")
  endif()
  get_property(image_conf_scripts TARGET ${DEFAULT_IMAGE} PROPERTY IMAGE_CONF_SCRIPT)
  list(APPEND image_conf_scripts "${APP_DIR}/sysbuild/c2_external_signing.cmake")
  set_target_properties(${DEFAULT_IMAGE} PROPERTIES IMAGE_CONF_SCRIPT "${image_conf_scripts}")
endif()

zephyr_file(CONF_FILES "${APP_DIR}/boards"
  KCONF mcuboot_board_conf
  SUFFIX mcuboot
)
zephyr_file(CONF_FILES "${APP_DIR}/boards"
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
