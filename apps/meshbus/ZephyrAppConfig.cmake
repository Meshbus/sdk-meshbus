# SPDX-FileCopyrightText: FoBE Studio
# SPDX-License-Identifier: Apache-2.0

# zephyr_default invokes this hook after board resolution, before configuration_files.
macro(boards_init)
  include("${APPLICATION_SOURCE_DIR}/cmake/board_profile.cmake")
  meshbus_board_profile("${APPLICATION_SOURCE_DIR}" meshbus_profile)
  # Keep prj.conf and explicitly supplied configuration; the product profile is
  # an extra fragment, just as board-specific configuration was in the flat layout.
  list(PREPEND EXTRA_CONF_FILE "${meshbus_profile}")
  list(REMOVE_DUPLICATES EXTRA_CONF_FILE)
  cmake_path(REPLACE_EXTENSION meshbus_profile LAST_ONLY ".overlay"
             OUTPUT_VARIABLE meshbus_overlay)
  if(EXISTS "${meshbus_overlay}")
    list(PREPEND EXTRA_DTC_OVERLAY_FILE "${meshbus_overlay}")
    list(REMOVE_DUPLICATES EXTRA_DTC_OVERLAY_FILE)
  endif()
endmacro()
