// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#ifndef FOBE_TESTS_LIB_MESHCORE_MODULE_DISPATCHER_MESH_SRC_TEST_TARGET_ENV_H_
#define FOBE_TESTS_LIB_MESHCORE_MODULE_DISPATCHER_MESH_SRC_TEST_TARGET_ENV_H_

#include "test_support.h"

namespace meshcore_dispatcher_mesh_tdd {

struct TargetEnv {
	MeshScript script;
	struct meshcore_packet_queue_manager manager;
	struct meshcore_tables tables;
	struct meshcore_mesh mesh;

	TargetEnv();
	~TargetEnv();

	void sync_script_to_hal();
	void sync_script_from_hal();
};

void target_mesh_loop(TargetEnv &env);
JointSnapshot capture_target_snapshot(TargetEnv &env);

} // namespace meshcore_dispatcher_mesh_tdd

#endif /* FOBE_TESTS_LIB_MESHCORE_MODULE_DISPATCHER_MESH_SRC_TEST_TARGET_ENV_H_ */
