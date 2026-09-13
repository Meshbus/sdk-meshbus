# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20.0)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../toolchain.cmake)
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

project(arduboy3d CXX)

if(NOT DEFINED MESHBUS_ARDUBOY_SDK_DIR)
    set(MESHBUS_ARDUBOY_SDK_DIR $ENV{MESHBUS_ARDUBOY_SDK_DIR})
endif()
if(NOT MESHBUS_ARDUBOY_SDK_DIR)
    get_filename_component(MESHBUS_ARDUBOY_SDK_DIR
        ${PROJECT_SOURCE_DIR}/../../../../../../../../modules/lib/arduboy
        ABSOLUTE
    )
endif()
include(${MESHBUS_ARDUBOY_SDK_DIR}/meshbus_arduboy_llext.cmake)

file(GLOB_RECURSE ARDUBOY3D_UPSTREAM CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/upstream/*
)

file(GLOB_RECURSE ARDUBOY3D_PORT_HEADERS CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/src/*.h
)

file(GLOB ARDUBOY3D_CPP CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/upstream/*.cpp)
meshbus_arduboy_llext_add_app(${PROJECT_NAME}
    RUNTIME COMPATIBILITY
    SOURCES
        ${ARDUBOY3D_CPP}
        ${PROJECT_SOURCE_DIR}/src/sketch.cpp
        ${PROJECT_SOURCE_DIR}/src/arduboy3d_app.cpp
    DEFINES
        ARDUBOY3D_ZEPHYR=1
    FLAGS
        -fsigned-char
        -Wno-int-to-pointer-cast
        -Wno-parentheses
        -Wno-unused-function
        -Wno-unused-variable
    INCLUDES
        ${LLEXT_EDK_INSTALL_DIR}/include/meshbus/include
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/upstream
    DEPENDS
        ${ARDUBOY3D_PORT_HEADERS}
        ${ARDUBOY3D_UPSTREAM}
)
