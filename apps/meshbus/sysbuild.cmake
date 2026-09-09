# SPDX-License-Identifier: Apache-2.0

set(c2_target "idea_mesh_tracker_c2/nrf54l15/cpuapp")

if("${BOARD}/${BOARD_QUALIFIERS}" STREQUAL "${c2_target}" AND
   "${SB_CONFIG_BOOT_SIGNATURE_KEY_FILE}" STREQUAL "")
  message(FATAL_ERROR
    "C2 requires an explicit Ed25519 image key. Pass "
    "'-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"/absolute/path/to/key.pem\"'")
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
