# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20.0)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../toolchain.cmake)
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

project(castleboy CXX)

if(NOT DEFINED MESHBUS_ARDUBOY_SDK_DIR)
    set(MESHBUS_ARDUBOY_SDK_DIR $ENV{MESHBUS_ARDUBOY_SDK_DIR})
endif()
if(NOT MESHBUS_ARDUBOY_SDK_DIR)
    get_filename_component(MESHBUS_ARDUBOY_SDK_DIR
        ${PROJECT_SOURCE_DIR}/../../../../../../../../modules/lib/meshbus-arduboy
        ABSOLUTE
    )
endif()
include(${MESHBUS_ARDUBOY_SDK_DIR}/meshbus_arduboy_llext.cmake)

file(GLOB CASTLEBOY_UPSTREAM CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/upstream/*.cpp
    ${PROJECT_SOURCE_DIR}/upstream/*.h
    ${PROJECT_SOURCE_DIR}/upstream/*.ino
)

meshbus_arduboy_llext_add_app(${PROJECT_NAME}
    SOURCE
        ${PROJECT_SOURCE_DIR}/src/castleboy_app.cpp
    DEFINES
        CASTLEBOY_ZEPHYR=1
    INCLUDES
        ${LLEXT_EDK_INSTALL_DIR}/include/meshbus/include
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/upstream
    DEPENDS
        ${CASTLEBOY_UPSTREAM}
)
