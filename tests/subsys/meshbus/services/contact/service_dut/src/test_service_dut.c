// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/meshbus/contact.h>
#include <zephyr/settings/settings.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

#define TEST_PARTITION_NODE    DT_NODELABEL(storage_partition)
#define STAGE_PARTITION_NODE   DT_NODELABEL(contact_test_stage_partition)
#define PRODUCT_PARTITION_NODE DT_NODELABEL(product_storage_partition)
#define SLOT0_PARTITION_NODE   DT_NODELABEL(slot0_partition)
#define SLOT1_PARTITION_NODE   DT_NODELABEL(slot1_partition)
#define RRAM_NODE              DT_MEM_FROM_PARTITION(TEST_PARTITION_NODE)

#define PARTITION_START(node_id) DT_REG_ADDR(node_id)
#define PARTITION_END(node_id)   (DT_REG_ADDR(node_id) + DT_REG_SIZE(node_id))
#define TEST_STAGE_MAGIC         0x4d424354U
#define TEST_STAGE_PREPARED      1U
#define TEST_STAGE_VERIFY        2U
#define TEST_MUTATION_LIMIT      22U
#define TEST_WRITER_STACK_SIZE   4096U

struct test_stage_record {
	uint32_t magic;
	uint32_t phase;
	uint32_t inverse;
	uint32_t reserved;
};

static struct test_stage_record test_stage;
static int test_storage_prepare_rc;

enum contact_writer_operation {
	CONTACT_WRITER_SET,
	CONTACT_WRITER_PUBLISH,
};

struct contact_writer_task {
	enum contact_writer_operation operation;
	meshbus_contact contact;
	const struct zbus_channel *chan;
	const void *event;
	struct k_sem *start;
	struct k_sem done;
	int rc;
	uint32_t elapsed_us;
};

static K_THREAD_STACK_DEFINE(contact_writer_stack_a, TEST_WRITER_STACK_SIZE);
static K_THREAD_STACK_DEFINE(contact_writer_stack_b, TEST_WRITER_STACK_SIZE);
static struct k_thread contact_writer_thread_a;
static struct k_thread contact_writer_thread_b;
static K_SEM_DEFINE(contact_parallel_start, 0, 2);
static K_SEM_DEFINE(contact_parallel_ready, 0, 2);
static K_SEM_DEFINE(contact_save_gate_entered, 0, 1);
static K_SEM_DEFINE(contact_save_gate_release, 0, 1);
static atomic_t contact_save_gate_armed = ATOMIC_INIT(0);
static atomic_t contact_save_gate_claimed = ATOMIC_INIT(0);
static atomic_t contact_save_call_count = ATOMIC_INIT(0);

BUILD_ASSERT(!IS_ENABLED(CONFIG_FLASH_SIMULATOR), "C2 scenario must use real RRAM");
BUILD_ASSERT(DT_NODE_HAS_STATUS(TEST_PARTITION_NODE, okay), "test storage missing");
BUILD_ASSERT(DT_NODE_HAS_STATUS(STAGE_PARTITION_NODE, okay), "test stage missing");
BUILD_ASSERT(DT_NODE_HAS_STATUS(PRODUCT_PARTITION_NODE, okay), "product storage guard missing");
BUILD_ASSERT(DT_REG_ADDR(TEST_PARTITION_NODE) == 0x164000, "wrong test storage base");
BUILD_ASSERT(DT_REG_SIZE(TEST_PARTITION_NODE) == 0xf000, "wrong test storage size");
BUILD_ASSERT(DT_REG_ADDR(STAGE_PARTITION_NODE) == 0x173000, "wrong stage base");
BUILD_ASSERT(DT_REG_SIZE(STAGE_PARTITION_NODE) == 0x1000, "wrong stage size");
BUILD_ASSERT(DT_REG_ADDR(PRODUCT_PARTITION_NODE) == 0x174000, "wrong product storage base");
BUILD_ASSERT(DT_REG_SIZE(PRODUCT_PARTITION_NODE) == 0x9000, "wrong product storage size");
BUILD_ASSERT(DT_PROP(RRAM_NODE, erase_block_size) == 4096, "unexpected erase alignment");
BUILD_ASSERT(DT_PROP(RRAM_NODE, write_block_size) == 16, "unexpected write alignment");
BUILD_ASSERT((sizeof(struct test_stage_record) % DT_PROP(RRAM_NODE, write_block_size)) == 0,
	     "test stage record is not write aligned");
BUILD_ASSERT((DT_REG_ADDR(TEST_PARTITION_NODE) % DT_PROP(RRAM_NODE, erase_block_size)) == 0,
	     "test storage base is not erase aligned");
BUILD_ASSERT((DT_REG_SIZE(TEST_PARTITION_NODE) % DT_PROP(RRAM_NODE, erase_block_size)) == 0,
	     "test storage size is not erase aligned");
BUILD_ASSERT(PARTITION_END(SLOT0_PARTITION_NODE) <= PARTITION_START(SLOT1_PARTITION_NODE),
	     "slot0 overlaps slot1");
BUILD_ASSERT(PARTITION_END(SLOT1_PARTITION_NODE) <= PARTITION_START(TEST_PARTITION_NODE),
	     "slot1 overlaps test storage");
BUILD_ASSERT(PARTITION_END(TEST_PARTITION_NODE) <= PARTITION_START(STAGE_PARTITION_NODE),
	     "test storage overlaps stage");
BUILD_ASSERT(PARTITION_END(STAGE_PARTITION_NODE) <= PARTITION_START(PRODUCT_PARTITION_NODE),
	     "stage overlaps product storage");
BUILD_ASSERT(PARTITION_END(PRODUCT_PARTITION_NODE) <= DT_REG_SIZE(RRAM_NODE),
	     "product storage exceeds CPUAPP RRAM");

static uint32_t elapsed_us(uint64_t start_cycles)
{
	return (uint32_t)k_cyc_to_us_floor64(k_cycle_get_64() - start_cycles);
}

extern int __real_settings_save_one(const char *name, const void *value, size_t val_len);

int __wrap_settings_save_one(const char *name, const void *value, size_t val_len)
{
	static const char contact_prefix[] = "meshbus/contact/";
	bool is_contact_record =
		name != NULL && strncmp(name, contact_prefix, sizeof(contact_prefix) - 1U) == 0;

	if (is_contact_record && atomic_get(&contact_save_gate_armed) != 0) {
		atomic_inc(&contact_save_call_count);
		if (atomic_cas(&contact_save_gate_claimed, 0, 1)) {
			k_sem_give(&contact_save_gate_entered);
			(void)k_sem_take(&contact_save_gate_release, K_FOREVER);
		}
	}

	return __real_settings_save_one(name, value, val_len);
}

static void sem_drain(struct k_sem *sem)
{
	while (k_sem_take(sem, K_NO_WAIT) == 0) {
	}
}

static void contact_save_gate_disarm(void)
{
	atomic_clear(&contact_save_gate_armed);
	atomic_clear(&contact_save_gate_claimed);
	atomic_clear(&contact_save_call_count);
	sem_drain(&contact_save_gate_entered);
	sem_drain(&contact_save_gate_release);
}

static void contact_save_gate_arm(void)
{
	contact_save_gate_disarm();
	atomic_set(&contact_save_gate_armed, 1);
}

static void contact_writer_entry(void *arg1, void *arg2, void *arg3)
{
	struct contact_writer_task *task = arg1;
	uint64_t started;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	if (task->start != NULL) {
		k_sem_give(&contact_parallel_ready);
		(void)k_sem_take(task->start, K_FOREVER);
	}
	started = k_cycle_get_64();
	if (task->operation == CONTACT_WRITER_SET) {
		task->rc = meshbus_contact_set(task->contact.public_key.bytes, &task->contact);
	} else {
		task->rc = zbus_chan_pub(task->chan, task->event, K_FOREVER);
	}
	task->elapsed_us = elapsed_us(started);
	k_sem_give(&task->done);
}

static void contact_writer_init(struct contact_writer_task *task,
				enum contact_writer_operation operation, struct k_sem *start)
{
	memset(task, 0, sizeof(*task));
	task->operation = operation;
	task->start = start;
	task->rc = -EINPROGRESS;
	k_sem_init(&task->done, 0, 1);
}

static void contact_writer_start(struct k_thread *thread, k_thread_stack_t *stack,
				 size_t stack_size, struct contact_writer_task *task)
{
	(void)k_thread_create(thread, stack, stack_size, contact_writer_entry, task, NULL, NULL,
			      K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
}

static void contact_writer_finish(struct k_thread *thread, struct contact_writer_task *task,
				  bool done_already)
{
	if (!done_already) {
		zassert_ok(k_sem_take(&task->done, K_SECONDS(1)), "contact writer did not finish");
	}
	zassert_ok(k_thread_join(thread, K_SECONDS(1)), "contact writer thread did not exit");
}

static bool stage_valid(const struct test_stage_record *stage)
{
	return stage->magic == TEST_STAGE_MAGIC &&
	       (stage->phase == TEST_STAGE_PREPARED || stage->phase == TEST_STAGE_VERIFY) &&
	       stage->inverse == ~stage->phase;
}

static int stage_write(uint32_t phase)
{
	const struct flash_area *area = NULL;
	struct test_stage_record next = {
		.magic = TEST_STAGE_MAGIC,
		.phase = phase,
		.inverse = ~phase,
	};
	int rc = flash_area_open(DT_FIXED_PARTITION_ID(STAGE_PARTITION_NODE), &area);

	if (rc == 0) {
		rc = flash_area_flatten(area, 0, area->fa_size);
	}
	if (rc == 0) {
		rc = flash_area_write(area, 0, &next, sizeof(next));
	}
	if (rc == 0) {
		test_stage = next;
	}
	if (area != NULL) {
		flash_area_close(area);
	}
	return rc;
}

static int stage_erase(void)
{
	const struct flash_area *area;
	int rc = flash_area_open(DT_FIXED_PARTITION_ID(STAGE_PARTITION_NODE), &area);

	if (rc == 0) {
		rc = flash_area_flatten(area, 0, area->fa_size);
		flash_area_close(area);
	}
	return rc;
}

static int contact_test_storage_prepare(void)
{
	const struct flash_area *area = NULL;
	int rc = flash_area_open(DT_FIXED_PARTITION_ID(STAGE_PARTITION_NODE), &area);

	if (rc == 0) {
		rc = flash_area_read(area, 0, &test_stage, sizeof(test_stage));
		flash_area_close(area);
	}
	if (rc != 0) {
		test_storage_prepare_rc = rc;
		return 0;
	}
	if (stage_valid(&test_stage)) {
		return 0;
	}

	rc = flash_area_open(DT_FIXED_PARTITION_ID(TEST_PARTITION_NODE), &area);
	if (rc == 0) {
		rc = flash_area_flatten(area, 0, area->fa_size);
		flash_area_close(area);
	}
	if (rc == 0) {
		rc = stage_write(TEST_STAGE_PREPARED);
	}
	test_storage_prepare_rc = rc;
	return 0;
}

SYS_INIT(contact_test_storage_prepare, POST_KERNEL, 99);

static void build_key(size_t index, uint8_t key[MESHBUS_CONTACT_PUBLIC_KEY_SIZE])
{
	for (size_t i = 0U; i < MESHBUS_CONTACT_PUBLIC_KEY_SIZE; i++) {
		key[i] = (uint8_t)(0x10U + (index * 0x20U) + i);
	}
}

static void verify_contact(size_t index, const char *name)
{
	uint8_t key[MESHBUS_CONTACT_PUBLIC_KEY_SIZE];
	meshbus_contact contact = meshbus_Contact_init_zero;

	build_key(index, key);
	zassert_ok(meshbus_contact_find_by_key(key, &contact), "contact %u missing",
		   (unsigned int)index);
	zassert_true(strcmp(contact.name, name) == 0, "contact name mismatch: %s", contact.name);
}

static uint32_t exercise_parallel_contact_updates(void)
{
	static struct contact_writer_task ble_writer;
	static struct contact_writer_task meshcore_writer;
	static struct contact_writer_task natural_a;
	static struct contact_writer_task natural_b;
	static meshbus_contact_response_path_event path_event;
	static meshbus_contact got;
	uint8_t key[MESHBUS_CONTACT_PUBLIC_KEY_SIZE];
	uint64_t started;
	uint32_t natural_total_us;
	atomic_val_t calls_while_blocked;
	bool meshcore_finished_early = false;
	bool meshcore_started = false;
	int gate_rc;
	int rc;

	printk("MB_CONTACT_HW_READY phase=parallel contacts=%u\n",
	       (unsigned int)meshbus_contact_store_count());
	memset(&path_event, 0, sizeof(path_event));
	memset(&got, 0, sizeof(got));
	build_key(0U, key);
	contact_writer_init(&ble_writer, CONTACT_WRITER_SET, NULL);
	zassert_ok(meshbus_contact_find_by_key(key, &ble_writer.contact),
		   "BLE-side contact load failed");
	strncpy(ble_writer.contact.alias, "ble-gated", sizeof(ble_writer.contact.alias) - 1U);
	ble_writer.contact.alias[sizeof(ble_writer.contact.alias) - 1U] = '\0';

	memcpy(path_event.key_prefix, key, sizeof(path_event.key_prefix));
	path_event.timestamp = 4242U;
	path_event.has_out_path = true;
	path_event.out_path_len = 2U;
	path_event.path_hash_size = 1U;
	path_event.out_path[0] = 0x41U;
	path_event.out_path[1] = 0x42U;
	contact_writer_init(&meshcore_writer, CONTACT_WRITER_PUBLISH, NULL);
	meshcore_writer.chan = &meshbus_contact_path_response_chan;
	meshcore_writer.event = &path_event;

	contact_save_gate_arm();
	contact_writer_start(&contact_writer_thread_a, contact_writer_stack_a,
			     K_THREAD_STACK_SIZEOF(contact_writer_stack_a), &ble_writer);
	gate_rc = k_sem_take(&contact_save_gate_entered, K_SECONDS(1));
	if (gate_rc == 0) {
		meshcore_started = true;
		contact_writer_start(&contact_writer_thread_b, contact_writer_stack_b,
				     K_THREAD_STACK_SIZEOF(contact_writer_stack_b),
				     &meshcore_writer);
		k_sleep(K_MSEC(20));
		meshcore_finished_early = k_sem_take(&meshcore_writer.done, K_NO_WAIT) == 0;
	}
	calls_while_blocked = atomic_get(&contact_save_call_count);
	k_sem_give(&contact_save_gate_release);
	contact_writer_finish(&contact_writer_thread_a, &ble_writer, false);
	if (meshcore_started) {
		contact_writer_finish(&contact_writer_thread_b, &meshcore_writer,
				      meshcore_finished_early);
	}
	contact_save_gate_disarm();

	zassert_ok(gate_rc, "BLE-side writer did not reach real ZMS");
	zassert_false(meshcore_finished_early,
		      "MeshCore-side update bypassed the Contact writer transaction");
	zassert_equal(calls_while_blocked, 1,
		      "more than one Contact write entered the gated transaction: %ld",
		      (long)calls_while_blocked);
	zassert_ok(ble_writer.rc, "BLE-side writer failed: %d", ble_writer.rc);
	zassert_ok(meshcore_writer.rc, "MeshCore-side writer failed: %d", meshcore_writer.rc);
	zassert_ok(meshbus_contact_find_by_key(key, &got), "gated contact missing");
	zassert_true(strcmp(got.alias, "ble-gated") == 0, "BLE alias update was lost");
	zassert_equal(got.out_path.size, path_event.out_path_len, "MeshCore path update was lost");
	zassert_mem_equal(got.out_path.bytes, path_event.out_path, path_event.out_path_len,
			  "MeshCore path bytes were lost");
	zassert_equal(got.last_seen_timestamp, path_event.timestamp,
		      "MeshCore timestamp update was lost");
	printk("MB_CONTACT_HW_MEASUREMENT phase=gated_parallel blocked_ms=20 "
	       "ble_total_us=%u meshcore_total_us=%u zms_entries=%ld\n",
	       ble_writer.elapsed_us, meshcore_writer.elapsed_us, (long)calls_while_blocked);

	sem_drain(&contact_parallel_start);
	sem_drain(&contact_parallel_ready);
	contact_writer_init(&natural_a, CONTACT_WRITER_SET, &contact_parallel_start);
	contact_writer_init(&natural_b, CONTACT_WRITER_SET, &contact_parallel_start);
	build_key(1U, key);
	zassert_ok(meshbus_contact_find_by_key(key, &natural_a.contact),
		   "natural contact A load failed");
	strncpy(natural_a.contact.alias, "natural-a", sizeof(natural_a.contact.alias) - 1U);
	natural_a.contact.alias[sizeof(natural_a.contact.alias) - 1U] = '\0';
	build_key(2U, key);
	zassert_ok(meshbus_contact_find_by_key(key, &natural_b.contact),
		   "natural contact B load failed");
	strncpy(natural_b.contact.alias, "natural-b", sizeof(natural_b.contact.alias) - 1U);
	natural_b.contact.alias[sizeof(natural_b.contact.alias) - 1U] = '\0';
	contact_writer_start(&contact_writer_thread_a, contact_writer_stack_a,
			     K_THREAD_STACK_SIZEOF(contact_writer_stack_a), &natural_a);
	contact_writer_start(&contact_writer_thread_b, contact_writer_stack_b,
			     K_THREAD_STACK_SIZEOF(contact_writer_stack_b), &natural_b);
	zassert_ok(k_sem_take(&contact_parallel_ready, K_SECONDS(1)), "writer A not ready");
	zassert_ok(k_sem_take(&contact_parallel_ready, K_SECONDS(1)), "writer B not ready");
	started = k_cycle_get_64();
	k_sem_give(&contact_parallel_start);
	k_sem_give(&contact_parallel_start);
	contact_writer_finish(&contact_writer_thread_a, &natural_a, false);
	contact_writer_finish(&contact_writer_thread_b, &natural_b, false);
	natural_total_us = elapsed_us(started);
	zassert_ok(natural_a.rc, "natural contact writer A failed: %d", natural_a.rc);
	zassert_ok(natural_b.rc, "natural contact writer B failed: %d", natural_b.rc);
	build_key(1U, key);
	rc = meshbus_contact_find_by_key(key, &got);
	zassert_ok(rc, "natural contact A missing: %d", rc);
	zassert_true(strcmp(got.alias, "natural-a") == 0, "natural alias A was lost");
	build_key(2U, key);
	rc = meshbus_contact_find_by_key(key, &got);
	zassert_ok(rc, "natural contact B missing: %d", rc);
	zassert_true(strcmp(got.alias, "natural-b") == 0, "natural alias B was lost");
	printk("MB_CONTACT_HW_MEASUREMENT phase=natural_parallel a_us=%u b_us=%u "
	       "total_us=%u over_100ms=%u\n",
	       natural_a.elapsed_us, natural_b.elapsed_us, natural_total_us,
	       (natural_a.elapsed_us > 100000U || natural_b.elapsed_us > 100000U) ? 1U : 0U);

	return 4U;
}

ZTEST(meshbus_contact_service_dut, test_real_zms_reboot_capacity_and_cleanup)
{
	static const uint8_t malformed[] = {0xff, 0xff, 0xff};
	uint8_t key[MESHBUS_CONTACT_PUBLIC_KEY_SIZE];
	uint32_t mutations = test_stage.phase == TEST_STAGE_VERIFY ? 3U : 0U;

	zassert_ok(test_storage_prepare_rc, "test storage preparation failed: %d",
		   test_storage_prepare_rc);
	zassert_true(stage_valid(&test_stage), "test stage marker is invalid");

	if (test_stage.phase == TEST_STAGE_PREPARED) {
		zassert_equal(meshbus_contact_store_count(), 0U,
			      "prepared contact storage is not empty");
		for (size_t i = 0U; i < 3U; i++) {
			build_key(i, key);
			zassert_ok(meshbus_contact_insert(
					   key,
					   i == 0U ? "persist-a"
						   : (i == 1U ? "persist-b" : "persist-c"),
					   MESHBUS_CONTACT_ROLE_CHAT),
				   "first-boot insert failed: %u", (unsigned int)i);
			mutations++;
		}
		zassert_ok(stage_write(TEST_STAGE_VERIFY), "verify stage write failed");
		printk("MB_CONTACT_HW_READY phase=persisted mutations=%u\n", mutations);
		sys_reboot(SYS_REBOOT_WARM);
		zassert_unreachable("software reboot returned");
	}

	zassert_equal(meshbus_contact_store_count(), 3U, "unexpected restored contact count");
	verify_contact(0U, "persist-a");
	verify_contact(1U, "persist-b");
	verify_contact(2U, "persist-c");

	for (size_t i = 3U; i < CONFIG_MESHBUS_CONTACT_MAX_CONTACTS; i++) {
		build_key(i, key);
		zassert_ok(meshbus_contact_insert(key, "fill", MESHBUS_CONTACT_ROLE_CHAT),
			   "capacity fill failed: %u", (unsigned int)i);
		mutations++;
	}
	zassert_equal(meshbus_contact_store_count(), CONFIG_MESHBUS_CONTACT_MAX_CONTACTS,
		      "contact store did not reach capacity");
	mutations += exercise_parallel_contact_updates();

	for (size_t i = 0U; i < CONFIG_MESHBUS_CONTACT_MAX_CONTACTS; i++) {
		build_key(i, key);
		zassert_ok(meshbus_contact_reset(key), "cleanup failed: %u", (unsigned int)i);
		mutations++;
	}
	zassert_equal(meshbus_contact_store_count(), 0U, "cleanup left visible contacts");

	zassert_ok(settings_save_one("meshbus/contact/0", malformed, sizeof(malformed)),
		   "malformed record seed failed");
	mutations++;
	zassert_ok(settings_load_subtree("meshbus/contact"), "malformed reload failed");
	zassert_equal(meshbus_contact_store_count(), 0U, "malformed contact became visible");
	zassert_ok(settings_delete("meshbus/contact/0"), "malformed cleanup failed");
	mutations++;

	zassert_true(mutations <= TEST_MUTATION_LIMIT, "mutation bound exceeded: %u", mutations);
	zassert_ok(stage_erase(), "stage cleanup failed");
	printk("MB_CONTACT_HW_RESULT pass mutations=%u final_count=%u\n", mutations,
	       (unsigned int)meshbus_contact_store_count());
}

ZTEST_SUITE(meshbus_contact_service_dut, NULL, NULL, NULL, NULL, NULL);
