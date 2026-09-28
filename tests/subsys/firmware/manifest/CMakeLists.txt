# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mbs_firmware_manifest_test)

target_sources(app PRIVATE
  src/main.c
  ${ZEPHYR_MESHBUS_MODULE_DIR}/subsys/firmware/manifest.c
)
target_compile_definitions(app PRIVATE
  CONFIG_MBS_FIRMWARE_ROLE=1
  CONFIG_MBS_FIRMWARE_BOARD_ID="unit_board"
  CONFIG_MBS_FIRMWARE_SOC_ID="nrf54l15"
  CONFIG_MBS_FIRMWARE_HARDWARE_REVISION=1
  CONFIG_MBS_FIRMWARE_IMAGE_KEY_ID=1
  CONFIG_MBS_FIRMWARE_MANIFEST_KEY_ID=1
  CONFIG_MBS_FIRMWARE_SECURITY_COUNTER=1
  CONFIG_MBS_FIRMWARE_MANIFEST_PUBLIC_KEY_HEX="d4b31ba49a3add3f825d10ca7f31b50b0de87f37ccc49f1a403a5c1320ffb4e0"
)
