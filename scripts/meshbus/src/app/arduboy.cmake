cmake_minimum_required(VERSION 3.20)
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake)
set(CMAKE_CXX_COMPILER_FORCED TRUE)
project(@APP_ID@ CXX)
include(${MESHBUS_ARDUBOY_SDK_DIR}/meshbus_arduboy_llext.cmake)
meshbus_arduboy_llext_add_app(@APP_ID@ RUNTIME
    SOURCE ${PROJECT_SOURCE_DIR}/src/main.cpp
    SKETCH ${PROJECT_SOURCE_DIR}/src/sketch/game.ino)
