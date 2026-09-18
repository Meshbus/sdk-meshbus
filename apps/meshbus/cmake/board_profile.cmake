# SPDX-License-Identifier: Apache-2.0

# Called after Zephyr resolves BOARD and BOARD_QUALIFIERS, in APP and sysbuild.
function(meshbus_board_profile app_dir output)
  set(scanner "${app_dir}/../../scripts/board_profiles.py")
  set(arguments --boards "${app_dir}/boards" --zephyr-base "${ZEPHYR_BASE}"
                --target "${BOARD}/${BOARD_QUALIFIERS}")
  foreach(root IN LISTS BOARD_ROOT)
    list(APPEND arguments --board-root "${root}")
  endforeach()
  foreach(root IN LISTS SOC_ROOT)
    list(APPEND arguments --soc-root "${root}")
  endforeach()
  execute_process(COMMAND "${PYTHON_EXECUTABLE}" "${scanner}" ${arguments}
                  OUTPUT_VARIABLE profile OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_VARIABLE error RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot resolve Meshbus board profile: ${error}")
  endif()
  file(GLOB_RECURSE profile_inputs CONFIGURE_DEPENDS
       "${app_dir}/boards/*.conf" "${app_dir}/boards/*.overlay")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
               "${scanner}" ${profile_inputs})
  set(${output} "${profile}" PARENT_SCOPE)
endfunction()
