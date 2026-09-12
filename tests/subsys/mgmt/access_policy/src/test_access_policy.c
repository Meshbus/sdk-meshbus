// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <stdio.h>

#include "mbs_mgmt_internal.h"

/* Focused external access-policy coverage for Meshbus MCUmgr hooks. */

static const char *const mbs_settings_subtrees[] = {
	"meshbus/bluetooth",
	"meshbus/channel",
	"meshbus/clock",
	"meshbus/display",
	"meshbus/gnss",
	"meshbus/indicator",
	"meshbus/llext",
	"meshbus/meshcore",
	"meshbus/contact",
	"meshbus/power",
	"meshbus/radio",
	"meshbus/telemetry",
};

ZTEST(mbs_management_access_policy, test_settings_access_allows_mbs_service_subtrees)
{
	for (size_t i = 0; i < ARRAY_SIZE(mbs_settings_subtrees); i++) {
		char child_key[64];
		const char *subtree = mbs_settings_subtrees[i];

		zassert_true(mbs_mgmt_settings_name_allowed(subtree), "%s denied", subtree);

		zassert_true(snprintf(child_key, sizeof(child_key), "%s/config/1", subtree) <
			     sizeof(child_key));
		zassert_true(mbs_mgmt_settings_name_allowed(child_key), "%s denied", child_key);

		zassert_true(mbs_mgmt_settings_access_allowed(SETTINGS_ACCESS_READ, child_key),
			     "read denied for %s", child_key);
		zassert_true(mbs_mgmt_settings_access_allowed(SETTINGS_ACCESS_WRITE, child_key),
			     "write denied for %s", child_key);
		zassert_true(mbs_mgmt_settings_access_allowed(SETTINGS_ACCESS_DELETE, child_key),
			     "delete denied for %s", child_key);
		zassert_true(mbs_mgmt_settings_access_allowed(SETTINGS_ACCESS_SAVE, subtree),
			     "save denied for %s", subtree);
	}
}

ZTEST(mbs_management_access_policy, test_settings_access_rejects_non_mbs_or_ambiguous_names)
{
	zassert_false(mbs_mgmt_settings_name_allowed(NULL));
	zassert_false(mbs_mgmt_settings_name_allowed(""));
	zassert_false(mbs_mgmt_settings_name_allowed("bt"));
	zassert_false(mbs_mgmt_settings_name_allowed("meshbus"));
	zassert_false(mbs_mgmt_settings_name_allowed("meshbus/radio_extra"));
	zassert_false(mbs_mgmt_settings_name_allowed("meshbus/radioextra/config/1"));

	zassert_false(mbs_mgmt_settings_access_allowed(SETTINGS_ACCESS_READ, NULL));
	zassert_false(mbs_mgmt_settings_access_allowed(SETTINGS_ACCESS_WRITE, NULL));
	zassert_false(mbs_mgmt_settings_access_allowed(SETTINGS_ACCESS_DELETE, NULL));
	zassert_false(mbs_mgmt_settings_access_allowed(SETTINGS_ACCESS_SAVE, NULL));
	zassert_false(mbs_mgmt_settings_access_allowed((enum settings_mgmt_access_types)99,
						      "meshbus/radio"));
}

ZTEST(mbs_management_access_policy, test_settings_access_allows_only_nameless_control_ops)
{
	zassert_true(mbs_mgmt_settings_access_allowed(SETTINGS_ACCESS_COMMIT, NULL));
	zassert_true(mbs_mgmt_settings_access_allowed(SETTINGS_ACCESS_LOAD, NULL));

	zassert_false(mbs_mgmt_settings_access_allowed(SETTINGS_ACCESS_COMMIT, "meshbus/radio"));
	zassert_false(mbs_mgmt_settings_access_allowed(SETTINGS_ACCESS_LOAD, "meshbus/radio"));
}

ZTEST(mbs_management_access_policy, test_fs_hook_is_audit_only_for_known_access_types)
{
	zassert_true(mbs_mgmt_fs_access_type_audited(FS_MGMT_FILE_ACCESS_READ));
	zassert_true(mbs_mgmt_fs_access_type_audited(FS_MGMT_FILE_ACCESS_WRITE));
	zassert_true(mbs_mgmt_fs_access_type_audited(FS_MGMT_FILE_ACCESS_STATUS));
	zassert_true(mbs_mgmt_fs_access_type_audited(FS_MGMT_FILE_ACCESS_HASH_CHECKSUM));
	zassert_false(mbs_mgmt_fs_access_type_audited((enum fs_mgmt_file_access_types)99));
}

ZTEST_SUITE(mbs_management_access_policy, NULL, NULL, NULL, NULL, NULL);
