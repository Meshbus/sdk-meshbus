# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

# Include nanopb cmake module for protobuf generation
list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
include(nanopb)

zephyr_library_named(mbs_proto_lib)

# Generate protobuf sources - nanopb auto-discovers matching .options files
if(NOT DEFINED ZEPHYR_MESHBUS_PROTOBUFS_MODULE_DIR)
        message(FATAL_ERROR
                "Meshbus requires the west module 'meshbus-protobufs'. "
                "Run 'west update meshbus-protobufs' and reconfigure.")
endif()

set(MESHBUS_PROTO_ROOT ${ZEPHYR_MESHBUS_PROTOBUFS_MODULE_DIR})
set(MESHBUS_PROTO_DIR ${MESHBUS_PROTO_ROOT}/meshbus)
if(NOT EXISTS ${MESHBUS_PROTO_DIR})
        message(FATAL_ERROR
                "Meshbus protobuf schema directory not found: "
                "${MESHBUS_PROTO_DIR}")
endif()

set(NANOPB_GENERATE_CPP_STANDALONE OFF)
set(MESHBUS_PROTO_FILES
        ${MESHBUS_PROTO_DIR}/contact.proto
        ${MESHBUS_PROTO_DIR}/meshcore.proto
        ${MESHBUS_PROTO_DIR}/channel.proto
        ${MESHBUS_PROTO_DIR}/fs.proto
        ${MESHBUS_PROTO_DIR}/notify.proto
        ${MESHBUS_PROTO_DIR}/message.proto
        ${MESHBUS_PROTO_DIR}/power.proto
        ${MESHBUS_PROTO_DIR}/management.proto
        ${MESHBUS_PROTO_DIR}/firmware.proto
        ${MESHBUS_PROTO_DIR}/bluetooth.proto
        ${MESHBUS_PROTO_DIR}/display.proto
        ${MESHBUS_PROTO_DIR}/indicator.proto
        ${MESHBUS_PROTO_DIR}/clock.proto
        ${MESHBUS_PROTO_DIR}/desktop.proto
        ${MESHBUS_PROTO_DIR}/input.proto
        ${MESHBUS_PROTO_DIR}/llext.proto
        ${MESHBUS_PROTO_DIR}/radio.proto
        ${MESHBUS_PROTO_DIR}/telemetry.proto
        ${MESHBUS_PROTO_DIR}/gnss.proto
)

nanopb_generate_cpp(PROTO_SRCS PROTO_HDRS
        RELPATH ${MESHBUS_PROTO_ROOT}
        ${MESHBUS_PROTO_FILES}
)

# Add generated files to library
zephyr_library_sources(${PROTO_SRCS} ${PROTO_HDRS})

add_custom_target(mbs_proto DEPENDS ${PROTO_HDRS})
zephyr_library_add_dependencies(mbs_proto)

if(TARGET app)
        add_dependencies(app mbs_proto)
endif()

# Include generated dir so we can use #include "meshbus/<service>.pb.h"
zephyr_include_directories(${CMAKE_CURRENT_BINARY_DIR})
