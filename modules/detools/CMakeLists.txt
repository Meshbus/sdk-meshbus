# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_DETOOLS)
  set(DETOOLS_DIR ${ZEPHYR_DETOOLS_MODULE_DIR})

  zephyr_library()
  zephyr_library_sources(
    ${DETOOLS_DIR}/c/detools.c
  )
  zephyr_include_directories(
    ${DETOOLS_DIR}/c
  )
  zephyr_compile_definitions(
    DETOOLS_CONFIG_FILE_IO=0
    DETOOLS_CONFIG_COMPRESSION_NONE=0
    DETOOLS_CONFIG_COMPRESSION_LZMA=0
    DETOOLS_CONFIG_COMPRESSION_CRLE=1
    DETOOLS_CONFIG_COMPRESSION_HEATSHRINK=1
  )
endif()
