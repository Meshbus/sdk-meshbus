# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_HEATSHRINK)
  set(HEATSHRINK_DIR ${ZEPHYR_HEATSHRINK_MODULE_DIR})

  zephyr_library()
  zephyr_library_sources(
    ${HEATSHRINK_DIR}/heatshrink_decoder.c
  )
  zephyr_include_directories(
    ${HEATSHRINK_DIR}
  )

  # Use static allocation mode with Flipper Zero compatible parameters
  zephyr_compile_definitions(
    HEATSHRINK_DYNAMIC_ALLOC=0
  )
endif()
