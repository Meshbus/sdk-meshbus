/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/init.h>
#include <channel/channel.h>
#include <contact/contact.h>
#include <message/message.h>
#include <zephyr/sys/reboot.h>
#include <meshcore/meshcore.h>
#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

#if defined(TEST_REQUIRE_RUNTIME)
#include "meshcore/platform.h"
#include <radio/radio.h>
static bool fake_tx;
static unsigned int fail_init;
static bool fail_submit;
static bool hold_init;
static bool hold_deinit;
static bool check_reentrant;
static bool boot_identity_captured;
static uint8_t boot_public_key[32];
K_SEM_DEFINE(deinit_entered, 0, 1);
K_SEM_DEFINE(deinit_release, 0, 1);
K_SEM_DEFINE(init_entered, 0, 1);
K_SEM_DEFINE(init_release, 0, 1);

int __real_k_work_submit_to_queue(struct k_work_q *queue, struct k_work *work);
int __wrap_k_work_submit_to_queue(struct k_work_q *queue, struct k_work *work)
{
	if (fail_submit) {
		fail_submit = false;
		return -EIO;
	}
	return __real_k_work_submit_to_queue(queue, work);
}

static atomic_t init_failures;
static atomic_t tx_completions;
static atomic_t rx_injections;
static atomic_t advert_requests;

int __real_zbus_chan_pub(const struct zbus_channel *chan, const void *msg, k_timeout_t timeout);
int __wrap_zbus_chan_pub(const struct zbus_channel *chan, const void *msg, k_timeout_t timeout)
{
	if (fake_tx && chan == &mbs_radio_publish_chan) {
		return 0; /* Hold one accepted radio send until the test publishes completion. */
	}
	return __real_zbus_chan_pub(chan, msg, timeout);
}

int __real_meshcore_radio_rx_inject(const uint8_t *data, size_t len,
	int16_t rssi, int8_t snr, uint32_t now);
int __wrap_meshcore_radio_rx_inject(const uint8_t *data, size_t len,
	int16_t rssi, int8_t snr, uint32_t now)
{
	atomic_inc(&rx_injections);
	return __real_meshcore_radio_rx_inject(data, len, rssi, snr, now);
}

int __real_meshcore_node_advert_request(bool flood);
int __wrap_meshcore_node_advert_request(bool flood)
{
	if (check_reentrant) {
		mbs_meshcore_config before, candidate, after;

		check_reentrant = false;
		zassert_ok(mbs_meshcore_config_get(&before));
		candidate = before;
		candidate.role = MBS_MESHCORE_ROLE_ROOM;
		zassert_equal(mbs_meshcore_config_set(&candidate), -EWOULDBLOCK);
		zassert_ok(mbs_meshcore_config_get(&after));
		zassert_mem_equal(&before, &after, sizeof(after));
		zassert_true(mbs_meshcore_runtime_is_ready());
	}
	atomic_inc(&advert_requests);
	return __real_meshcore_node_advert_request(flood);
}

void __real_meshcore_deinit(void);
void __wrap_meshcore_deinit(void)
{
	if (hold_deinit) {
		hold_deinit = false;
		k_sem_give(&deinit_entered);
		zassert_ok(k_sem_take(&deinit_release, K_SECONDS(2)));
	}
	__real_meshcore_deinit();
}

int __real_meshcore_init(void);
int __wrap_meshcore_init(void)
{
	if (!boot_identity_captured) {
		meshcore_common_node_identity_t identity;

		zassert_ok(meshcore_platform_node_identity_get(&identity));
		memcpy(boot_public_key, identity.public_key, sizeof(boot_public_key));
		boot_identity_captured = true;
	}
	if (hold_init) {
		hold_init = false;
		k_sem_give(&init_entered);
		if (k_sem_take(&init_release, K_SECONDS(2)) != 0) {
			return -ETIMEDOUT;
		}
	}
	if (fail_init) {
		fail_init--;
		atomic_inc(&init_failures);
		return -EIO;
	}
	return __real_meshcore_init();
}

int __real_meshcore_radio_tx_done(uint32_t now, bool success);
int __wrap_meshcore_radio_tx_done(uint32_t now, bool success)
{
	atomic_inc(&tx_completions);
	return __real_meshcore_radio_tx_done(now, success);
}

BUILD_ASSERT(IS_ENABLED(CONFIG_MBS_MESHCORE_RUNTIME),
	     "Runtime fixture must include the engine");
#endif

BUILD_ASSERT(DT_NODE_HAS_PROP(DT_NODELABEL(sim_flash_controller), memory_region),
	     "Lifecycle fixture requires flash retained over real emulated resets");

/* The flash simulator and this cursor survive real emulated MCU resets. */
static struct {
	uint32_t magic;
	uint32_t step;
	uint8_t public_key[32];
} retained __noinit;
static atomic_t save_attempts;
#if defined(CONFIG_MBS_CONTACT)
static const uint8_t peer_key[32] = {0x73};
#endif

int __real_settings_save_one(const char *name, const void *value, size_t len);
int __wrap_settings_save_one(const char *name, const void *value, size_t len)
{
	if (strcmp(name, "meshbus/meshcore/config") == 0) {
		atomic_inc(&save_attempts);
	}
	return __real_settings_save_one(name, value, len);
}

int __real_settings_delete(const char *name);
int __wrap_settings_delete(const char *name)
{
	if (strcmp(name, "meshbus/meshcore/config") == 0) {
		atomic_inc(&save_attempts);
	}
	return __real_settings_delete(name);
}

static int prepare_flash(void)
{
	if (retained.magic != 0x726f6c65) {
		const struct device *flash = DEVICE_DT_GET(DT_NODELABEL(sim_flash_controller));
		int rc = flash_erase(flash, 0, 0x10000);

		if (rc != 0) {
			return rc;
		}
		retained.step = 0;
		retained.magic = 0x726f6c65;
	}
	return 0;
}
SYS_INIT(prepare_flash, POST_KERNEL, 99);

#if defined(CONFIG_MBS_CHANNEL)
/* Every directed transition between the four roles, once. */
static const uint8_t roles[] = {1, 2, 1, 3, 1, 4, 2, 3, 2, 4, 3, 4, 1};
#else
static const uint8_t roles[] = {2, 3, 2, 4, 3, 4, 2};
#endif

static void check_role(uint8_t role)
{
	mbs_meshcore_config cfg;

	zassert_ok(mbs_meshcore_config_get(&cfg));
	zassert_equal(cfg.role, role);
	zassert_equal(mbs_meshcore_active_role_get(), role);
	zassert_equal(mbs_meshcore_runtime_is_ready(),
		      IS_ENABLED(CONFIG_MBS_MESHCORE_RUNTIME));
#if defined(TEST_REQUIRE_RUNTIME)
	meshcore_common_node_runtime_policy_t policy;
	meshcore_common_cli_event_t cli = {
		.public_key = {0x73},
		.sender_timestamp = 1U,
		.route = MESHCORE_COMMON_MESSAGE_ROUTE_DIRECT,
		.text_len = 6U,
		.text = "reboot",
	};
	char reply[16] = "unchanged";
	mbs_meshcore_config after;

	zassert_ok(meshcore_platform_node_runtime_policy_get(&policy));
	zassert_equal(policy.tx_delay_factor, cfg.tx_delay_factor);
	zassert_equal(policy.direct_tx_delay_factor, cfg.direct_tx_delay_factor);
	cli.type = MESHCORE_COMMON_CLI_COMMAND;
	zassert_equal(meshcore_platform_cli_receive(&cli, reply, sizeof(reply) - 1U),
		      -ENOTSUP);
	cli.type = MESHCORE_COMMON_CLI_DATA;
	zassert_equal(meshcore_platform_cli_receive(&cli,
		role == MBS_MESHCORE_ROLE_CHAT ? NULL : reply,
		role == MBS_MESHCORE_ROLE_CHAT ? 0U : sizeof(reply) - 1U), -ENOTSUP);
	zassert_str_equal(reply, "unchanged");
	zassert_ok(mbs_meshcore_config_get(&after));
	zassert_mem_equal(&cfg, &after, sizeof(cfg));
#endif
}

static void wait_persistence(void)
{
	k_sleep(K_MSEC(CONFIG_MBS_SETTINGS_PERSISTENCE_DELAY + 100));
}

#if defined(TEST_REQUIRE_RUNTIME)
K_THREAD_STACK_DEFINE(update_stack, 4096);
static struct k_thread update_thread;
static int update_result;
static atomic_t update_done;

static void update_entry(void *candidate, void *unused1, void *unused2)
{
	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);
	update_result = candidate == NULL ? mbs_meshcore_config_reset() :
		mbs_meshcore_config_set(candidate);
	atomic_set(&update_done, 1);
}

static void start_update(mbs_meshcore_config *candidate)
{
	atomic_clear(&update_done);
	k_thread_create(&update_thread, update_stack, K_THREAD_STACK_SIZEOF(update_stack),
			update_entry, candidate, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
}

static void finish_update(void)
{
	zassert_ok(k_thread_join(&update_thread, K_SECONDS(2)));
	zassert_equal(atomic_get(&update_done), 1);
	zassert_ok(update_result);
}

static void check_uncommitted(const mbs_meshcore_config *before)
{
	mbs_meshcore_config cfg;

	zassert_ok(mbs_meshcore_config_get(&cfg));
	zassert_mem_equal(&cfg, before, sizeof(cfg));
}

static void check_blocked_update(mbs_meshcore_config *before)
{
	zassert_equal(atomic_get(&update_done), 0);
	zassert_false(mbs_meshcore_runtime_is_ready());
	check_uncommitted(before);
	zassert_equal(mbs_meshcore_config_set(before), -EBUSY);
	zassert_equal(mbs_meshcore_config_reset(), -EBUSY);
	zassert_equal(mbs_meshcore_advert_request(false), -EBUSY);
}

static void check_failed_update(bool reset, bool submission, unsigned int failures)
{
	mbs_meshcore_config before, candidate;
	atomic_val_t writes;

	wait_persistence();
	zassert_ok(mbs_meshcore_config_get(&before));
	candidate = before;
	candidate.role = before.role == roles[0] ? roles[1] : roles[0];
	writes = atomic_get(&save_attempts);
	fail_submit = submission;
	fail_init = failures;
	zassert_equal(reset ? mbs_meshcore_config_reset() :
		      mbs_meshcore_config_set(&candidate), -EIO);
	check_uncommitted(&before);
	zassert_equal(mbs_meshcore_active_role_get(), before.role);
	zassert_equal(mbs_meshcore_runtime_is_ready(), failures < 2);
	wait_persistence();
	zassert_equal(atomic_get(&save_attempts), writes,
		      "A failed candidate must not schedule a settings write");
	if (failures >= 2) {
		/* The committed configuration is also the recovery target after double failure. */
		zassert_ok(mbs_meshcore_config_set(&before));
	}
	check_role(before.role);
}
#endif

static void check_preserved_tables(void)
{
#if defined(CONFIG_MBS_CHANNEL)
	mbs_channel channel;
	mbs_contact contact;
	uint8_t secret[16] = {0x42};

	zassert_ok(mbs_channel_get(0, &channel));
	zassert_str_equal(channel.name, "custom");
	zassert_equal(channel.secret.size, sizeof(secret));
	zassert_mem_equal(channel.secret.bytes, secret, sizeof(secret));
	zassert_ok(mbs_contact_find_by_key(peer_key, &contact));
#endif
}

static void reset_live(void)
{
	mbs_meshcore_config before, cfg;
#if defined(TEST_REQUIRE_RUNTIME)
	meshcore_common_node_identity_t identity;

	/* Populate the host cache, then hold the old engine before deinitialization. */
	zassert_ok(meshcore_platform_node_identity_get(&identity));
#endif
	zassert_ok(mbs_meshcore_config_get(&before));
#if defined(TEST_REQUIRE_RUNTIME)
	hold_deinit = true;
	start_update(NULL);
	zassert_ok(k_sem_take(&deinit_entered, K_SECONDS(1)));
	check_blocked_update(&before);
	zassert_ok(meshcore_platform_node_identity_get(&identity));
	zassert_mem_equal(identity.public_key, before.public_key.bytes, sizeof(identity.public_key));
	k_sem_give(&deinit_release);
	finish_update();
#else
	zassert_ok(mbs_meshcore_config_reset());
#endif
	check_role(roles[0]);
	zassert_ok(mbs_meshcore_config_get(&cfg));
	zassert_equal(cfg.role, roles[0]);
	zassert_equal(cfg.latitude, 0);
	zassert_false(cfg.client_repeat);
	zassert_equal(cfg.path_hash_size, 1);
	check_preserved_tables();
#if defined(TEST_REQUIRE_RUNTIME)
	zassert_equal(cfg.public_key.size, MBS_MESHCORE_PUBLIC_KEY_SIZE);
	zassert_equal(cfg.private_key.size, MBS_MESHCORE_PRIVATE_KEY_SIZE);
	zassert_not_equal(memcmp(cfg.public_key.bytes, before.public_key.bytes,
				MBS_MESHCORE_PUBLIC_KEY_SIZE), 0);
	zassert_not_equal(cfg.name[0], '\0');
	zassert_equal(cfg.flood_max, 64);
	zassert_equal(cfg.advert_interval, 60);
	zassert_ok(meshcore_platform_node_identity_get(&identity));
	zassert_mem_equal(identity.public_key, cfg.public_key.bytes, sizeof(identity.public_key));
	memcpy(retained.public_key, cfg.public_key.bytes, sizeof(retained.public_key));
#endif
	/* Cross the old automatic-reboot deadline with the test cursor unchanged. */
	k_sleep(K_SECONDS(1));
}

ZTEST(role_lifecycle, test_live_role_and_persistence)
{
	mbs_meshcore_config cfg;
#if defined(TEST_REQUIRE_RUNTIME)
	mbs_meshcore_config saved;
#endif

	zassert_ok(mbs_meshcore_config_get(&cfg));

#if defined(TEST_REQUIRE_RUNTIME)
	/* The first engine initialization must see the eventual receive identity,
	 * including on blank storage, before any outgoing request can refresh it.
	 */
	zassert_true(boot_identity_captured);
	zassert_equal(cfg.public_key.size, sizeof(boot_public_key));
	zassert_mem_equal(boot_public_key, cfg.public_key.bytes, sizeof(boot_public_key),
			  "Engine started before the local identity was ready");
#endif
	if (retained.step == 1) {
#if defined(TEST_REQUIRE_RUNTIME)
		uint8_t consumed_seed[32];

		/* Test entropy repeats after reboot; skip the first-boot identity seed. */
		meshcore_platform_rng_random(consumed_seed, sizeof(consumed_seed));
#endif
		/* An explicit reboot, not the role setter, verifies durable settings. */
		zassert_equal(cfg.role, roles[ARRAY_SIZE(roles) - 2]);
		zassert_str_equal(cfg.name, "persistent identity");
		retained.step = 2;
		reset_live(); /* Restore the default role as well as the identity. */
		reset_live(); /* Same-role identity replacement must also stop the engine. */
#if defined(TEST_REQUIRE_RUNTIME)
		check_failed_update(true, true, 0);
		check_failed_update(true, false, 1);
		check_failed_update(true, false, 2);
#endif
		wait_persistence();
		retained.step = 3;
		sys_reboot(SYS_REBOOT_COLD);
	}
	zassert_not_equal(retained.step, 2, "Configuration reset rebooted the device");
	if (retained.step == 3) {
		zassert_equal(cfg.role, roles[0]);
		check_role(cfg.role);
		check_preserved_tables();
#if defined(TEST_REQUIRE_RUNTIME)
		zassert_mem_equal(cfg.public_key.bytes, retained.public_key, sizeof(retained.public_key));
#endif
		return;
	}
	zassert_equal(retained.step, 0, "Unexpected reboot during live role changes");
	zassert_equal(cfg.role, roles[0]);
	zassert_equal(cfg.tx_delay_factor, 0.5f);
	zassert_equal(cfg.direct_tx_delay_factor, 0.2f);
	check_role(cfg.role);
	cfg.role = 0;
	zassert_equal(mbs_meshcore_config_set(&cfg), -EINVAL);
	cfg.role = 5;
	zassert_equal(mbs_meshcore_config_set(&cfg), -EINVAL);
#if !defined(CONFIG_MBS_CHANNEL)
	cfg.role = MBS_MESHCORE_ROLE_CHAT;
	zassert_equal(mbs_meshcore_config_set(&cfg), -ENOTSUP);
#else
	uint8_t secret[16] = {0x42};
	mbs_channel channel;
	mbs_contact contact;

#if defined(TEST_REQUIRE_RUNTIME)
	meshcore_common_channel_view_t match;
	uint8_t zero_secret[16] = {0};
	uint8_t empty_hash = 0U;

	zassert_ok(mbs_channel_reset(0));
	zassert_equal(meshcore_platform_channel_search_by_hash(&empty_hash, &match, 1), 0);
	zassert_ok(mbs_channel_set(0, zero_secret, sizeof(zero_secret), "zero secret"));
	zassert_ok(mbs_channel_get(0, &channel));
	zassert_equal(meshcore_platform_channel_search_by_hash(channel.hash.bytes, &match, 1),
		      1, "A configured all-zero secret must remain searchable");
	zassert_mem_equal(match.secret, zero_secret, sizeof(zero_secret));
#endif
	zassert_ok(mbs_channel_set(0, secret, sizeof(secret), "custom"));
	zassert_ok(mbs_contact_insert(peer_key, "retained peer", MBS_CONTACT_ROLE_CHAT));
#endif
#if defined(TEST_REQUIRE_RUNTIME)
	static const uint8_t packet[] = {0x42};
	struct mbs_radio_tx_done_event done = {.status = 0};

	struct mbs_radio_state_event radio_enabled = {.enabled = true};

	struct mbs_radio_receive_event rx = {.len = 1, .data = {0x42}};

	/* Radio is paused by the unavailable backend: these stay queued until switching. */
	zassert_ok(mbs_meshcore_advert_request(false));
	zassert_ok(zbus_chan_pub(&mbs_radio_receive_chan, &rx, K_MSEC(100)));
	fake_tx = true;
	zassert_equal(meshcore_platform_radio_packet_send(packet, sizeof(packet)), 1);
#endif
	for (size_t i = 1; i < ARRAY_SIZE(roles); i++) {
		zassert_ok(mbs_meshcore_config_get(&cfg));
#if defined(TEST_REQUIRE_RUNTIME)
		saved = cfg;
#endif
		cfg.role = roles[i];
		/* Delay factors apply to CHAT as well as REPEATER.
		 * Exercise explicit zero and non-default values across live roles.
		 */
		cfg.tx_delay_factor = i % 2U == 0U ? 0.0f : 0.75f;
		cfg.direct_tx_delay_factor = i % 2U == 0U ? 0.0f : 0.25f;
		strcpy(cfg.name, "persistent identity");
#if defined(TEST_REQUIRE_RUNTIME)
		if (i == 1) {
			fail_submit = true;
			zassert_equal(mbs_meshcore_config_set(&cfg), -EIO);
			check_uncommitted(&saved);
			check_role(saved.role);
			hold_init = true;
			start_update(&cfg);
			zassert_ok(k_sem_take(&init_entered, K_SECONDS(1)));
			check_blocked_update(&saved);
			k_sem_give(&init_release);
			finish_update();
		} else
#endif
		{
			zassert_ok(mbs_meshcore_config_set(&cfg));
		}
		check_role(cfg.role);
#if defined(TEST_REQUIRE_RUNTIME)
		meshcore_common_node_identity_t identity;

		zassert_ok(meshcore_platform_node_identity_get(&identity));
		zassert_equal((unsigned int)identity.role, (unsigned int)cfg.role);
#endif
#if defined(CONFIG_MBS_CHANNEL)
		zassert_ok(mbs_channel_get(0, &channel));
		zassert_str_equal(channel.name, "custom");
		zassert_ok(mbs_contact_find_by_key(peer_key, &contact));
#endif
	}
#if defined(TEST_REQUIRE_RUNTIME)
	zassert_ok(zbus_chan_pub(&mbs_radio_state_chan, &radio_enabled, K_MSEC(100)));
	k_sleep(K_MSEC(20));
	zassert_equal(atomic_get(&rx_injections), 0);
	zassert_equal(atomic_get(&advert_requests), 0);
	/* Old TX occupies hardware across every switch; its completion cannot enter the new engine. */
	zassert_equal(meshcore_platform_radio_packet_send(packet, sizeof(packet)), 0);
	zassert_ok(zbus_chan_pub(&mbs_radio_tx_done_chan, &done, K_MSEC(100)));
	k_sleep(K_MSEC(20));
	zassert_equal(atomic_get(&tx_completions), 0);
	zassert_equal(meshcore_platform_radio_packet_send(packet, sizeof(packet)), 1);
	zassert_ok(zbus_chan_pub(&mbs_radio_tx_done_chan, &done, K_MSEC(100)));
	k_sleep(K_MSEC(20));
	zassert_equal(atomic_get(&tx_completions), 1);
	fake_tx = false;
	zassert_ok(zbus_chan_pub(&mbs_radio_receive_chan, &rx, K_MSEC(100)));
	check_reentrant = true;
	zassert_ok(mbs_meshcore_advert_request(false));
	k_sleep(K_MSEC(20));
	zassert_equal(atomic_get(&rx_injections), 1);
	zassert_equal(atomic_get(&advert_requests), 1);
	zassert_false(check_reentrant);


	check_failed_update(false, true, 0);
	check_failed_update(false, false, 1);
	check_failed_update(false, false, 2);
#endif
	/* Survive the former 750 ms automatic reboot deadline. */
	k_sleep(K_SECONDS(1));
	cfg.role = roles[ARRAY_SIZE(roles) - 2];
	zassert_ok(mbs_meshcore_config_set(&cfg));
	check_role(cfg.role);
	wait_persistence();
#if defined(TEST_REQUIRE_RUNTIME)
	/* A rejected role must not replace the last successful setting across reboot. */
	check_failed_update(false, false, 1);
#endif
	retained.step = 1;
	sys_reboot(SYS_REBOOT_COLD);
}
ZTEST_SUITE(role_lifecycle, NULL, NULL, NULL, NULL, NULL);
