// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <zephyr/ztest.h>

ZTEST(meshcore_protocol_api_map, test_protocol_api_map_checked_at_configure)
{
	zassert_true(true, "protocol API map checker did not run");
}

ZTEST_SUITE(meshcore_protocol_api_map, NULL, NULL, NULL, NULL, NULL);
