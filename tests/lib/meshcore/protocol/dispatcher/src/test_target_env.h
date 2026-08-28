// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#ifndef FOBE_TESTS_LIB_MESHCORE_MODULE_DISPATCHER_SRC_TARGET_ENV_H_
#define FOBE_TESTS_LIB_MESHCORE_MODULE_DISPATCHER_SRC_TARGET_ENV_H_

#include "test_support.h"

extern "C" {

/*
 * Minimal owner shim for dispatcher-only tests.
 * It exists solely because dispatcher now calls owner-only entry points.
 */
struct meshcore_mesh {
	meshcore_hal_test_dispatcher_script_t *script;
};

meshcore_dispatcher_action meshcore_mesh_on_recv_packet(
	struct meshcore_mesh *mesh, struct meshcore_packet *packet);
uint32_t meshcore_mesh_runtime_get_cad_fail_retry_delay(
	struct meshcore_mesh *mesh);

}

namespace meshcore_dispatcher_tdd {

struct TargetEnv {
	DispatcherInputScript script;
	struct meshcore_packet_queue_manager manager;
	struct meshcore_mesh owner_mesh;
	struct meshcore_dispatcher dispatcher;

	TargetEnv();
	~TargetEnv();

	void sync_driver_inputs();
};

void target_dispatcher_loop(TargetEnv &env);
DispatcherSnapshot capture_target_snapshot(TargetEnv &env);

} // namespace meshcore_dispatcher_tdd

#endif /* FOBE_TESTS_LIB_MESHCORE_MODULE_DISPATCHER_SRC_TARGET_ENV_H_ */
