# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../toolchain.cmake)
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

project(cxx_hello CXX)

if(NOT DEFINED LLEXT_EDK_INSTALL_DIR)
    set(LLEXT_EDK_INSTALL_DIR $ENV{LLEXT_EDK_INSTALL_DIR})
endif()
include(${LLEXT_EDK_INSTALL_DIR}/cmake.cflags)

set(LLEXT_CXXFLAGS ${LLEXT_CFLAGS})
list(FILTER LLEXT_CXXFLAGS EXCLUDE REGEX "^-std=.*")
list(APPEND LLEXT_CXXFLAGS
    -std=gnu++17
    -fno-exceptions
    -fno-rtti
    -fno-threadsafe-statics
    -fno-use-cxa-atexit
    -fno-weak
    -fno-unwind-tables
    -fno-asynchronous-unwind-tables
    -c
)

add_custom_command(
    OUTPUT
        ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.llext
        ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.inc
    DEPENDS
        ${PROJECT_SOURCE_DIR}/src/main.cpp
    COMMAND ${CMAKE_CXX_COMPILER} ${LLEXT_CXXFLAGS}
        -o ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.llext
        ${PROJECT_SOURCE_DIR}/src/main.cpp
    COMMAND xxd -ip ${PROJECT_NAME}.llext
        ${PROJECT_NAME}.inc
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
)

add_custom_target(
    cxx_hello ALL
    DEPENDS
        ${PROJECT_BINARY_DIR}/cxx_hello.llext
        ${PROJECT_BINARY_DIR}/cxx_hello.inc
)
