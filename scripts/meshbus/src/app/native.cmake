cmake_minimum_required(VERSION 3.20)
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake)
set(CMAKE_C_COMPILER_FORCED TRUE)
project(@APP_ID@ C)
include(${LLEXT_EDK_INSTALL_DIR}/cmake.cflags)
set(cflags)
foreach(flag IN LISTS LLEXT_CFLAGS)
    string(REPLACE "\\\"" "\"" flag "${flag}")
    list(APPEND cflags "${flag}")
endforeach()
add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/@APP_ID@.llext
    COMMAND ${CMAKE_C_COMPILER} ${cflags} -c
        ${PROJECT_SOURCE_DIR}/src/main.c -o ${PROJECT_BINARY_DIR}/@APP_ID@.llext
        -MMD -MF ${PROJECT_BINARY_DIR}/app.d
    DEPENDS ${PROJECT_SOURCE_DIR}/src/main.c
    DEPFILE ${PROJECT_BINARY_DIR}/app.d
    VERBATIM)
add_custom_target(app ALL DEPENDS ${PROJECT_BINARY_DIR}/@APP_ID@.llext)
