// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#ifndef FOBE_TESTS_LIB_MESHCORE_MODULE_DISPATCHER_MESH_SRC_PACKET_BRIDGE_H_
#define FOBE_TESTS_LIB_MESHCORE_MODULE_DISPATCHER_MESH_SRC_PACKET_BRIDGE_H_

#include <zephyr/ztest.h>

extern "C" {
#include "meshcore_clock.h"
#include "meshcore_mesh.h"
#include "meshcore_packet.h"
#include "meshcore_packet_manager.h"
#include "meshcore_rng.h"
#include "meshcore_tables.h"
#include "meshcore_test_runtime.h"
#include "meshcore_utils.h"
#include "meshcore/types.h"
}

#undef PH_ROUTE_MASK
#undef PH_TYPE_SHIFT
#undef PH_TYPE_MASK
#undef PH_VER_SHIFT
#undef PH_VER_MASK
#undef ROUTE_TYPE_TRANSPORT_FLOOD
#undef ROUTE_TYPE_FLOOD
#undef ROUTE_TYPE_DIRECT
#undef ROUTE_TYPE_TRANSPORT_DIRECT
#undef PAYLOAD_TYPE_REQ
#undef PAYLOAD_TYPE_RESPONSE
#undef PAYLOAD_TYPE_TXT_MSG
#undef PAYLOAD_TYPE_ACK
#undef PAYLOAD_TYPE_ADVERT
#undef PAYLOAD_TYPE_GRP_TXT
#undef PAYLOAD_TYPE_GRP_DATA
#undef PAYLOAD_TYPE_ANON_REQ
#undef PAYLOAD_TYPE_PATH
#undef PAYLOAD_TYPE_TRACE
#undef PAYLOAD_TYPE_MULTIPART
#undef PAYLOAD_TYPE_CONTROL
#undef PAYLOAD_TYPE_RAW_CUSTOM
#undef PAYLOAD_VER_1
#undef PAYLOAD_VER_2
#undef PAYLOAD_VER_3
#undef PAYLOAD_VER_4

#include "Mesh.h"
#include "Packet.h"
#include "SimpleMeshTables.h"
#include "StaticPoolPacketManager.h"

#endif /* FOBE_TESTS_LIB_MESHCORE_MODULE_DISPATCHER_MESH_SRC_PACKET_BRIDGE_H_ */
