/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <channel/channel.h>
#include <contact/contact.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>
#include <zephyr/ztest.h>

#define PERF_CONTACT_COUNT       64U
#define PERF_CHANNEL_COUNT_64 64U
#define PERF_CHANNEL_COUNT_128 128U
#define PERF_ITERATIONS       3U

#if defined(CONFIG_ZMS_LOOKUP_CACHE)
#define PERF_STORAGE_PROFILE "cached"
#else
#define PERF_STORAGE_PROFILE "release-uncached"
#endif

static uint8_t contact_keys[PERF_CONTACT_COUNT][MBS_CONTACT_PUBLIC_KEY_SIZE];
static uint8_t contact_prefixes[PERF_CONTACT_COUNT][CONFIG_MBS_CONTACT_PREFIX_BYTES];
static uint8_t channel_hashes[PERF_CHANNEL_COUNT_128];

static uint32_t elapsed_us(uint64_t start_cycles)
{
	return (uint32_t)k_cyc_to_us_floor64(k_cycle_get_64() - start_cycles);
}

static void fill_contact(mbs_contact *contact, size_t idx)
{
	zassert_not_null(contact, "contact is NULL");

	*contact = (mbs_contact)meshbus_Contact_init_zero;
	contact->public_key.size = MBS_CONTACT_PUBLIC_KEY_SIZE;
	for (size_t i = 0; i < MBS_CONTACT_PUBLIC_KEY_SIZE; i++) {
		contact->public_key.bytes[i] = (uint8_t)(0x30U + idx + (i * 13U));
	}
	contact->public_key.bytes[0] = (uint8_t)idx;
	contact->public_key.bytes[1] = (uint8_t)(0xa0U + idx);
	contact->public_key.bytes[2] = (uint8_t)(0x50U ^ idx);
	contact->public_key.bytes[3] = (uint8_t)(0xc0U + idx);
	contact->role = MBS_CONTACT_ROLE_CHAT;
	contact->path_hash_size = 1U;
	contact->is_neighbor = true;
	contact->last_seen_timestamp = (uint32_t)(1000U + idx);
	(void)snprintk(contact->name, sizeof(contact->name), "contact_%02u", (unsigned int)idx);
	(void)snprintk(contact->alias, sizeof(contact->alias), "alias_%02u", (unsigned int)idx);
}

static void fill_secret(uint8_t *secret, size_t secret_len, size_t idx)
{
	zassert_not_null(secret, "secret is NULL");

	for (size_t i = 0; i < secret_len; i++) {
		secret[i] = (uint8_t)(0x80U + idx + (i * 7U));
	}
	secret[0] = (uint8_t)idx;
	secret[1] = (uint8_t)(0x5aU ^ idx);
}

static void seed_contacts(void)
{
	for (size_t i = 0; i < PERF_CONTACT_COUNT; i++) {
		mbs_contact contact;

		fill_contact(&contact, i);
		memcpy(contact_keys[i], contact.public_key.bytes, sizeof(contact_keys[i]));
		memcpy(contact_prefixes[i], contact.public_key.bytes, sizeof(contact_prefixes[i]));
		zassert_ok(mbs_contact_set(contact.public_key.bytes, &contact),
			   "contact set failed: idx=%u", (unsigned int)i);
	}

	zassert_equal(mbs_contact_store_count(), PERF_CONTACT_COUNT,
		      "contact store count mismatch");
}

static void seed_channels(size_t count)
{
	for (size_t i = 0; i < count; i++) {
		uint8_t secret[MBS_CHANNEL_SECRET_DEFAULT_LEN];
		mbs_channel channel = meshbus_Channel_init_zero;
		char name[MBS_CHANNEL_NAME_MAX_LEN];

		if (i < mbs_channel_store_count()) {
			continue;
		}

		fill_secret(secret, sizeof(secret), i);
		(void)snprintk(name, sizeof(name), "chan_%03u", (unsigned int)i);
		zassert_ok(mbs_channel_set(i, secret, sizeof(secret), name),
			   "channel set failed: idx=%u", (unsigned int)i);
		zassert_ok(mbs_channel_get(i, &channel), "channel get failed: idx=%u",
			   (unsigned int)i);
		zassert_equal(channel.hash.size, 1U, "channel hash size mismatch");
		channel_hashes[i] = channel.hash.bytes[0];
	}

	zassert_equal(mbs_channel_store_count(), count, "channel store count mismatch");
}

static uint32_t measure_settings_reload_us(void)
{
	uint64_t start = k_cycle_get_64();

	zassert_ok(settings_load_subtree("meshbus/channel"), "channel reload failed");
	zassert_ok(settings_load_subtree("meshbus/contact"), "contact reload failed");
	return elapsed_us(start);
}

static uint32_t measure_channel_get_us(size_t count)
{
	mbs_channel channel = meshbus_Channel_init_zero;
	uint64_t start = k_cycle_get_64();

	for (size_t i = 0; i < count; i++) {
		zassert_ok(mbs_channel_get(i, &channel), "channel get failed: idx=%u",
			   (unsigned int)i);
	}

	return elapsed_us(start);
}

static uint32_t measure_channel_find_us(size_t count)
{
	mbs_channel channel = meshbus_Channel_init_zero;
	uint64_t start = k_cycle_get_64();

	for (size_t i = 0; i < count; i++) {
		size_t slot_id = 0U;

		zassert_ok(mbs_channel_next_by_hash(&channel_hashes[i], 0U, &slot_id,
							&channel),
			   "channel find failed: idx=%u", (unsigned int)i);
		zassert_equal(channel.hash.size, 1U, "channel hash size mismatch");
		zassert_equal(channel.hash.bytes[0], channel_hashes[i],
			      "channel find returned unexpected hash");
		zassert_true(slot_id < count, "channel find returned unexpected slot");
	}

	return elapsed_us(start);
}

static uint32_t measure_contact_get_us(void)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	uint64_t start = k_cycle_get_64();

	for (size_t i = 0; i < PERF_CONTACT_COUNT; i++) {
		zassert_ok(mbs_contact_get(i, &contact), "contact get failed: idx=%u",
			   (unsigned int)i);
	}

	return elapsed_us(start);
}

static uint32_t measure_contact_find_prefix_us(void)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	uint64_t start = k_cycle_get_64();

	for (size_t i = 0; i < PERF_CONTACT_COUNT; i++) {
		zassert_ok(mbs_contact_find_by_prefix(contact_prefixes[i], &contact),
			   "contact find by prefix failed: idx=%u", (unsigned int)i);
	}

	return elapsed_us(start);
}

static uint32_t measure_contact_find_key_us(void)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	uint64_t start = k_cycle_get_64();

	for (size_t i = 0; i < PERF_CONTACT_COUNT; i++) {
		zassert_ok(mbs_contact_find_by_key(contact_keys[i], &contact),
			   "contact find by key failed: idx=%u", (unsigned int)i);
	}

	return elapsed_us(start);
}

static void run_perf_scenario(size_t channel_count)
{
	uint32_t reload_us = 0U;
	uint32_t channel_get_us = 0U;
	uint32_t channel_find_us = 0U;
	uint32_t contact_get_us = 0U;
	uint32_t contact_find_prefix_us = 0U;
	uint32_t contact_find_key_us = 0U;

	seed_channels(channel_count);

	for (size_t i = 0; i < PERF_ITERATIONS; i++) {
		reload_us += measure_settings_reload_us();
		channel_get_us += measure_channel_get_us(channel_count);
		channel_find_us += measure_channel_find_us(channel_count);
		contact_get_us += measure_contact_get_us();
		contact_find_prefix_us += measure_contact_find_prefix_us();
		contact_find_key_us += measure_contact_find_key_us();
	}

	printk("MBS_SETTINGS_PERF platform=%s backend=zms_blob storage_profile=%s "
	       "contacts=%u channels=%u iterations=%u "
	       "reload_avg_us=%u channel_get_round_avg_us=%u channel_get_call_avg_us=%u "
	       "channel_find_round_avg_us=%u channel_find_call_avg_us=%u "
	       "contact_get_round_avg_us=%u contact_get_call_avg_us=%u "
	       "contact_find_prefix_round_avg_us=%u contact_find_prefix_call_avg_us=%u "
	       "contact_find_key_round_avg_us=%u contact_find_key_call_avg_us=%u\n",
	       CONFIG_BOARD_TARGET, PERF_STORAGE_PROFILE,
	       (unsigned int)PERF_CONTACT_COUNT, (unsigned int)channel_count,
	       (unsigned int)PERF_ITERATIONS, (unsigned int)(reload_us / PERF_ITERATIONS),
	       (unsigned int)(channel_get_us / PERF_ITERATIONS),
	       (unsigned int)(channel_get_us / (PERF_ITERATIONS * channel_count)),
	       (unsigned int)(channel_find_us / PERF_ITERATIONS),
	       (unsigned int)(channel_find_us / (PERF_ITERATIONS * channel_count)),
	       (unsigned int)(contact_get_us / PERF_ITERATIONS),
	       (unsigned int)(contact_get_us / (PERF_ITERATIONS * PERF_CONTACT_COUNT)),
	       (unsigned int)(contact_find_prefix_us / PERF_ITERATIONS),
	       (unsigned int)(contact_find_prefix_us / (PERF_ITERATIONS * PERF_CONTACT_COUNT)),
	       (unsigned int)(contact_find_key_us / PERF_ITERATIONS),
	       (unsigned int)(contact_find_key_us / (PERF_ITERATIONS * PERF_CONTACT_COUNT)));
}

ZTEST(mbs_settings_performance, test_zms_blob_public_api_read_performance)
{
	seed_contacts();
	run_perf_scenario(PERF_CHANNEL_COUNT_64);
	run_perf_scenario(PERF_CHANNEL_COUNT_128);

	mbs_contact contact_match = meshbus_Contact_init_zero;
	uint8_t contact_hash = contact_keys[0][0];
	size_t contact_slot = 0U;

	zassert_ok(mbs_contact_next_by_hash(&contact_hash, 0U, &contact_slot, &contact_match),
		   "contact next by hash failed");
	zassert_mem_equal(contact_match.public_key.bytes, contact_keys[0], MBS_CONTACT_PUBLIC_KEY_SIZE,
			  "contact hash lookup returned unexpected contact");
}

ZTEST_SUITE(mbs_settings_performance, NULL, NULL, NULL, NULL, NULL);
