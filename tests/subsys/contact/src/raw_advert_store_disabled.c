// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <contact/contact.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

#include "mbs_settings_internal.h"

#define TEST_CONTACT_SETTINGS_SUBTREE "meshbus/contact"
#define TEST_CONTACT_SETTINGS_KEY_ADVERT_RAW "advert_raw"

MBS_SETTINGS_INDEXED_RAW_SCHEMA_DEFINE(contact_advert_raw_blob_schema,
				      TEST_CONTACT_SETTINGS_SUBTREE,
				      TEST_CONTACT_SETTINGS_KEY_ADVERT_RAW);

static void fill_bytes(uint8_t *buf, size_t len, uint8_t seed)
{
	for (size_t i = 0; i < len; i++) {
		buf[i] = (uint8_t)(seed + i);
	}
}

static void build_contact(mbs_contact *contact, uint8_t seed, const char *name)
{
	*contact = (mbs_contact)meshbus_Contact_init_zero;
	contact->role = MBS_CONTACT_ROLE_CHAT;
	contact->public_key.size = MBS_CONTACT_PUBLIC_KEY_SIZE;
	fill_bytes(contact->public_key.bytes, contact->public_key.size, seed);
	strncpy(contact->name, name, sizeof(contact->name) - 1U);
	contact->name[sizeof(contact->name) - 1U] = '\0';
}

static int find_contact_index_by_prefix(const uint8_t *public_key_prefix, size_t *index_out)
{
	if (public_key_prefix == NULL || index_out == NULL) {
		return -EINVAL;
	}

	for (size_t i = 0U; i < mbs_contact_store_size(); i++) {
		mbs_contact contact = meshbus_Contact_init_zero;
		int rc = mbs_contact_get(i, &contact);

		if (rc == -ENOENT) {
			continue;
		}
		if (rc != 0) {
			return rc;
		}
		if (memcmp(contact.public_key.bytes, public_key_prefix,
			   CONFIG_MBS_CONTACT_PREFIX_BYTES) == 0) {
			*index_out = i;
			return 0;
		}
	}

	return -ENOENT;
}

static void assert_raw_missing(size_t slot)
{
	uint8_t buffer[MBS_CONTACT_ADVERT_RAW_MAX_LEN];
	size_t len = 0U;
	int rc;

	rc = mbs_settings_indexed_raw_load(&contact_advert_raw_blob_schema, slot,
					  buffer, sizeof(buffer), &len);
	if (rc == 0) {
		zassert_equal(len, 0U, "raw advert should be empty: slot=%u len=%u",
			      (unsigned int)slot, (unsigned int)len);
		return;
	}
	zassert_equal(rc, -ENOENT, "raw advert should be absent: slot=%u rc=%d",
		      (unsigned int)slot, rc);
}

static void delete_raw(size_t slot)
{
	(void)mbs_settings_indexed_raw_delete(&contact_advert_raw_blob_schema, slot);
}

static void save_raw(size_t slot, const uint8_t *raw, size_t raw_len)
{
	int rc;

	rc = mbs_settings_indexed_raw_save(&contact_advert_raw_blob_schema, slot, raw, raw_len);
	zassert_ok(rc, "raw advert save failed: slot=%u rc=%d", (unsigned int)slot, rc);
}

static void assert_raw_present(size_t slot, const uint8_t *expected, size_t expected_len)
{
	uint8_t buffer[MBS_CONTACT_ADVERT_RAW_MAX_LEN];
	size_t len = 0U;
	int rc;

	rc = mbs_settings_indexed_raw_load(&contact_advert_raw_blob_schema, slot,
					  buffer, sizeof(buffer), &len);
	zassert_ok(rc, "raw advert should remain: slot=%u rc=%d", (unsigned int)slot, rc);
	zassert_equal(len, expected_len, "raw advert length mismatch");
	zassert_mem_equal(buffer, expected, expected_len, "raw advert payload mismatch");
}

ZTEST(mbs_contact_contract_raw_store_disabled, test_advert_updates_contact_without_raw_save)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	mbs_contact got = meshbus_Contact_init_zero;
	mbs_contact_response_advert_event advert = {0};
	uint8_t prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES];
	size_t slot = 0U;
	static const uint8_t raw[] = {0x11, 0x22, 0x33};
	int rc;

	build_contact(&contact, 0x31, "raw_disabled");
	memcpy(prefix, contact.public_key.bytes, sizeof(prefix));

	rc = mbs_contact_set(prefix, &contact);
	zassert_ok(rc, "contact set failed: %d", rc);
	rc = find_contact_index_by_prefix(prefix, &slot);
	zassert_ok(rc, "contact slot lookup failed: %d", rc);
	delete_raw(slot);

	memcpy(advert.public_key, contact.public_key.bytes, sizeof(advert.public_key));
	strncpy(advert.name, "raw_disabled_new", sizeof(advert.name) - 1U);
	advert.name[sizeof(advert.name) - 1U] = '\0';
	advert.role = MBS_CONTACT_ROLE_CHAT;
	advert.raw_advert_len = sizeof(raw);
	memcpy(advert.raw_advert, raw, sizeof(raw));

	rc = zbus_chan_pub(&mbs_contact_advert_response_chan, &advert, K_NO_WAIT);
	zassert_ok(rc, "advert response publish failed: %d", rc);
	k_sleep(K_MSEC(20));

	rc = mbs_contact_find_by_prefix(prefix, &got);
	zassert_ok(rc, "contact should remain readable: %d", rc);
	zassert_true(strcmp(got.name, "raw_disabled_new") == 0, "contact name should update");
	assert_raw_missing(slot);

	rc = mbs_contact_share_request(prefix);
	zassert_equal(rc, -ENODATA, "raw-disabled contact advert request mismatch: %d", rc);

	rc = mbs_contact_reset(prefix);
	zassert_ok(rc, "contact reset failed: %d", rc);
}

ZTEST(mbs_contact_contract_raw_store_disabled, test_contact_reset_preserves_existing_raw)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	uint8_t prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES];
	size_t slot = 0U;
	static const uint8_t raw[] = {0xa1, 0xb2, 0xc3, 0xd4};
	int rc;

	build_contact(&contact, 0x51, "raw_preserve");
	memcpy(prefix, contact.public_key.bytes, sizeof(prefix));

	rc = mbs_contact_set(prefix, &contact);
	zassert_ok(rc, "contact set failed: %d", rc);
	rc = find_contact_index_by_prefix(prefix, &slot);
	zassert_ok(rc, "contact slot lookup failed: %d", rc);

	save_raw(slot, raw, sizeof(raw));
	rc = mbs_contact_reset(prefix);
	zassert_ok(rc, "contact reset failed: %d", rc);

	rc = mbs_contact_find_by_prefix(prefix, &contact);
	zassert_equal(rc, -ENOENT, "contact should be deleted: %d", rc);
	assert_raw_present(slot, raw, sizeof(raw));
}

ZTEST_SUITE(mbs_contact_contract_raw_store_disabled, NULL, NULL, NULL, NULL, NULL);
