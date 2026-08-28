# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED ZEPHYR_MESHCORE_MODULE_DIR OR
   ZEPHYR_MESHCORE_MODULE_DIR STREQUAL "")
  message(FATAL_ERROR
    "MeshCore Zephyr module was not discovered. Ensure west.yml includes "
    "modules/lib/meshcore and run this test from a west build context."
  )
endif()

set(MESHCORE_MODULE_SOURCE_MANIFEST
  "${ZEPHYR_MESHCORE_MODULE_DIR}/cmake/meshcore_sources.cmake"
)

if(NOT EXISTS "${MESHCORE_MODULE_SOURCE_MANIFEST}")
  message(FATAL_ERROR
    "MeshCore source manifest not found: "
    "${MESHCORE_MODULE_SOURCE_MANIFEST}"
  )
endif()

include("${MESHCORE_MODULE_SOURCE_MANIFEST}")
