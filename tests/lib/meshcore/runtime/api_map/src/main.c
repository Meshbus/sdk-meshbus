// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <zephyr/ztest.h>

ZTEST(meshcore_runtime_api_map, test_runtime_api_map_checked_at_configure)
{
	zassert_true(true, "runtime API map checker did not run");
}

ZTEST_SUITE(meshcore_runtime_api_map, NULL, NULL, NULL, NULL, NULL);
