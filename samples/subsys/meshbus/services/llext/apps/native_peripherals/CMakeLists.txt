# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../toolchain.cmake)
set(CMAKE_C_COMPILER_FORCED TRUE)
project(native_peripherals C)

if(NOT DEFINED LLEXT_EDK_INSTALL_DIR)
    set(LLEXT_EDK_INSTALL_DIR $ENV{LLEXT_EDK_INSTALL_DIR})
endif()
include(${LLEXT_EDK_INSTALL_DIR}/cmake.cflags)

add_custom_command(
    OUTPUT ${PROJECT_BINARY_DIR}/native_peripherals.llext
    DEPENDS ${PROJECT_SOURCE_DIR}/src/main.c
    COMMAND ${CMAKE_C_COMPILER} ${LLEXT_CFLAGS} -c
        -o ${PROJECT_BINARY_DIR}/native_peripherals.llext
        ${PROJECT_SOURCE_DIR}/src/main.c
)
add_custom_target(native_peripherals ALL
    DEPENDS ${PROJECT_BINARY_DIR}/native_peripherals.llext)
