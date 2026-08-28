// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <zephyr/ztest.h>

extern "C" {
#include "meshcore/platform.h"
#include "meshcore/runtime.h"
#include "meshcore_test_runtime.h"
}

ZTEST(meshcore_platform_contract, test_singleton_init_uses_platform_hooks)
{
	static const uint8_t public_key[MESHCORE_PUBLIC_KEY_SIZE] = { 0x42 };
	static const uint8_t private_key[MESHCORE_PRIVATE_KEY_SIZE] = { 0x24 };

	meshcore_hal_test_host_state_reset();
	meshcore_hal_test_timer_reset();
	meshcore_hal_test_node_identity_set("platform-contract",
					   MESHCORE_COMMON_NODE_ROLE_CHAT,
					   public_key, private_key);

	zassert_ok(meshcore_init(), "singleton init should use linked hooks");
	zassert_true(meshcore_hal_test_timer_arm_count_get() > 0U,
		     "init should schedule through platform timer hook");
	meshcore_deinit();
}

ZTEST(meshcore_platform_contract, test_platform_hooks_are_direct_symbols)
{
	uint8_t random[4] = {};

	meshcore_platform_rng_random(random, sizeof(random));
	zassert_equal(meshcore_platform_radio_packet_send(NULL, 0U), 0,
		      "test radio hook should reject empty sends");
}

ZTEST_SUITE(meshcore_platform_contract, NULL, NULL, NULL, NULL, NULL);
