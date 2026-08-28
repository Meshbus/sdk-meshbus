// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "case_helpers.h"

static void *meshcore_runtime_oracle_setup(void)
{
	reset_target_runtime_state();
	return NULL;
}

static void meshcore_runtime_oracle_before(void *fixture)
{
	ARG_UNUSED(fixture);
	reset_target_runtime_state();
}

ZTEST_SUITE(meshcore_runtime_oracle, NULL, meshcore_runtime_oracle_setup,
	    meshcore_runtime_oracle_before, NULL, NULL);
