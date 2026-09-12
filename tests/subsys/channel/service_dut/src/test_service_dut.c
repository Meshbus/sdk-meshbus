// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <channel/channel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#define TEST_PARTITION_NODE    DT_NODELABEL(storage_partition)
#define STAGE_PARTITION_NODE   DT_NODELABEL(channel_test_stage_partition)
#define PRODUCT_PARTITION_NODE DT_NODELABEL(product_storage_partition)
#define SLOT0_PARTITION_NODE   DT_NODELABEL(slot0_partition)
#define SLOT1_PARTITION_NODE   DT_NODELABEL(slot1_partition)
#define RRAM_NODE              DT_MEM_FROM_PARTITION(TEST_PARTITION_NODE)

#define PARTITION_START(node_id)  DT_REG_ADDR(node_id)
#define PARTITION_END(node_id)    (DT_REG_ADDR(node_id) + DT_REG_SIZE(node_id))
#define TEST_MUTATION_LIMIT       22U
#define TEST_WRITER_STACK_SIZE    2048U
#define TEST_BUSY_RETURN_LIMIT_US 100000U
#define TEST_STAGE_MAGIC          0x4d424348U
#define TEST_STAGE_VERSION        1U
#define TEST_STAGE_PREPARED       1U
#define TEST_STAGE_VERIFY         2U

struct test_stage_record {
	uint32_t magic;
	uint32_t version;
	uint32_t phase;
	uint32_t phase_inverse;
};

static struct test_stage_record test_stage;
static int test_storage_prepare_rc;

struct channel_writer_task {
	size_t index;
	const char *name;
	struct k_sem *start;
	struct k_sem done;
	int rc;
	uint32_t elapsed_us;
};

static K_THREAD_STACK_DEFINE(channel_writer_stack_a, TEST_WRITER_STACK_SIZE);
static K_THREAD_STACK_DEFINE(channel_writer_stack_b, TEST_WRITER_STACK_SIZE);
static struct k_thread channel_writer_thread_a;
static struct k_thread channel_writer_thread_b;
static K_SEM_DEFINE(channel_parallel_start, 0, 2);
static K_SEM_DEFINE(channel_parallel_ready, 0, 2);
static K_SEM_DEFINE(channel_save_gate_entered, 0, 1);
static K_SEM_DEFINE(channel_save_gate_release, 0, 1);
static atomic_t channel_save_gate_armed = ATOMIC_INIT(0);
static atomic_t channel_save_gate_claimed = ATOMIC_INIT(0);
static atomic_t channel_save_call_count = ATOMIC_INIT(0);

BUILD_ASSERT(!IS_ENABLED(CONFIG_FLASH_SIMULATOR), "C2 scenario must not use flash simulator");
BUILD_ASSERT(DT_NODE_HAS_STATUS(TEST_PARTITION_NODE, okay), "test storage partition missing");
BUILD_ASSERT(DT_NODE_HAS_STATUS(STAGE_PARTITION_NODE, okay), "test stage partition missing");
BUILD_ASSERT(DT_NODE_HAS_STATUS(PRODUCT_PARTITION_NODE, okay), "product storage guard missing");
BUILD_ASSERT(DT_REG_ADDR(TEST_PARTITION_NODE) == 0x164000, "wrong test storage base");
BUILD_ASSERT(DT_REG_SIZE(TEST_PARTITION_NODE) == 0xf000, "wrong test storage size");
BUILD_ASSERT(DT_REG_ADDR(STAGE_PARTITION_NODE) == 0x173000, "wrong test stage base");
BUILD_ASSERT(DT_REG_SIZE(STAGE_PARTITION_NODE) == 0x1000, "wrong test stage size");
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
	     "test storage overlaps test stage");
BUILD_ASSERT(PARTITION_END(STAGE_PARTITION_NODE) <= PARTITION_START(PRODUCT_PARTITION_NODE),
	     "test stage overlaps product storage");
BUILD_ASSERT(PARTITION_END(PRODUCT_PARTITION_NODE) <= DT_REG_SIZE(RRAM_NODE),
	     "product storage exceeds CPUAPP RRAM");

static const uint8_t
	secrets[CONFIG_MBS_CHANNEL_MAX_CHANNELS][MBS_CHANNEL_SECRET_DEFAULT_LEN] = {
		{0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
		 0x1e, 0x1f},
		{0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d,
		 0x2e, 0x2f},
		{0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d,
		 0x3e, 0x3f},
		{0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d,
		 0x4e, 0x4f},
		{0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d,
		 0x5e, 0x5f},
		{0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d,
		 0x6e, 0x6f},
		{0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d,
		 0x7e, 0x7f},
		{0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d,
		 0x8e, 0x8f},
};

static uint32_t elapsed_us(uint64_t start_cycles)
{
	return (uint32_t)k_cyc_to_us_floor64(k_cycle_get_64() - start_cycles);
}

extern int __real_settings_save_one(const char *name, const void *value, size_t val_len);

int __wrap_settings_save_one(const char *name, const void *value, size_t val_len)
{
	static const char channel_prefix[] = "meshbus/channel/";
	bool is_channel_record =
		name != NULL && strncmp(name, channel_prefix, sizeof(channel_prefix) - 1U) == 0;

	if (is_channel_record && atomic_get(&channel_save_gate_armed) != 0) {
		atomic_inc(&channel_save_call_count);
		if (atomic_cas(&channel_save_gate_claimed, 0, 1)) {
			k_sem_give(&channel_save_gate_entered);
			(void)k_sem_take(&channel_save_gate_release, K_FOREVER);
		}
	}

	return __real_settings_save_one(name, value, val_len);
}

static void sem_drain(struct k_sem *sem)
{
	while (k_sem_take(sem, K_NO_WAIT) == 0) {
	}
}

static void channel_save_gate_disarm(void)
{
	atomic_clear(&channel_save_gate_armed);
	atomic_clear(&channel_save_gate_claimed);
	atomic_clear(&channel_save_call_count);
	sem_drain(&channel_save_gate_entered);
	sem_drain(&channel_save_gate_release);
}

static void channel_save_gate_arm(void)
{
	channel_save_gate_disarm();
	atomic_set(&channel_save_gate_armed, 1);
}

static void channel_writer_entry(void *arg1, void *arg2, void *arg3)
{
	struct channel_writer_task *task = arg1;
	uint64_t started;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	if (task->start != NULL) {
		k_sem_give(&channel_parallel_ready);
		(void)k_sem_take(task->start, K_FOREVER);
	}
	started = k_cycle_get_64();
	task->rc = mbs_channel_set(task->index, secrets[task->index],
				       sizeof(secrets[task->index]), task->name);
	task->elapsed_us = elapsed_us(started);
	k_sem_give(&task->done);
}

static void channel_writer_init(struct channel_writer_task *task, size_t index, const char *name,
				struct k_sem *start)
{
	memset(task, 0, sizeof(*task));
	task->index = index;
	task->name = name;
	task->start = start;
	task->rc = -EINPROGRESS;
	k_sem_init(&task->done, 0, 1);
}

static void channel_writer_start(struct k_thread *thread, k_thread_stack_t *stack,
				 size_t stack_size, struct channel_writer_task *task)
{
	(void)k_thread_create(thread, stack, stack_size, channel_writer_entry, task, NULL, NULL,
			      K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
}

static void channel_writer_finish(struct k_thread *thread, struct channel_writer_task *task)
{
	zassert_ok(k_sem_take(&task->done, K_SECONDS(1)), "channel writer did not finish");
	zassert_ok(k_thread_join(thread, K_SECONDS(1)), "channel writer thread did not exit");
}

static void verify_channel(size_t index, const char *expected_name);

static uint32_t exercise_parallel_channel_updates(void)
{
	struct channel_writer_task gated_writer;
	struct channel_writer_task natural_a;
	struct channel_writer_task natural_b;
	uint64_t started;
	uint32_t mutations = 0U;
	uint32_t busy_us = 0U;
	uint32_t natural_total_us;
	atomic_val_t calls_while_blocked;
	int gate_rc;
	int busy_rc = -EINPROGRESS;

	channel_writer_init(&gated_writer, 0U, "gated-a", NULL);
	channel_save_gate_arm();
	channel_writer_start(&channel_writer_thread_a, channel_writer_stack_a,
			     K_THREAD_STACK_SIZEOF(channel_writer_stack_a), &gated_writer);
	gate_rc = k_sem_take(&channel_save_gate_entered, K_SECONDS(1));
	if (gate_rc == 0) {
		started = k_cycle_get_64();
		busy_rc = mbs_channel_set(1U, secrets[1], sizeof(secrets[1]), "gated-b");
		busy_us = elapsed_us(started);
	}
	calls_while_blocked = atomic_get(&channel_save_call_count);
	k_sem_give(&channel_save_gate_release);
	channel_writer_finish(&channel_writer_thread_a, &gated_writer);
	channel_save_gate_disarm();

	zassert_ok(gate_rc, "gated channel writer did not reach real ZMS");
	zassert_ok(gated_writer.rc, "gated channel writer failed: %d", gated_writer.rc);
	zassert_equal(busy_rc, -EBUSY, "contending channel write must return -EBUSY: %d", busy_rc);
	zassert_true(busy_us < TEST_BUSY_RETURN_LIMIT_US,
		     "contending channel write blocked too long: %u us", busy_us);
	zassert_equal(calls_while_blocked, 1,
		      "more than one channel write entered the gated transaction: %ld",
		      (long)calls_while_blocked);
	mutations++;
	started = k_cycle_get_64();
	zassert_ok(mbs_channel_set(1U, secrets[1], sizeof(secrets[1]), "gated-b"),
		   "contending channel retry failed");
	uint32_t retry_us = elapsed_us(started);
	mutations++;
	verify_channel(0U, "gated-a");
	verify_channel(1U, "gated-b");
	printk("MBS_CHANNEL_HW_MEASUREMENT phase=gated_parallel busy_return_us=%u "
	       "first_total_us=%u retry_total_us=%u zms_entries=%ld\n",
	       busy_us, gated_writer.elapsed_us, retry_us, (long)calls_while_blocked);

	sem_drain(&channel_parallel_start);
	sem_drain(&channel_parallel_ready);
	channel_writer_init(&natural_a, 0U, "natural-a", &channel_parallel_start);
	channel_writer_init(&natural_b, 1U, "natural-b", &channel_parallel_start);
	channel_writer_start(&channel_writer_thread_a, channel_writer_stack_a,
			     K_THREAD_STACK_SIZEOF(channel_writer_stack_a), &natural_a);
	channel_writer_start(&channel_writer_thread_b, channel_writer_stack_b,
			     K_THREAD_STACK_SIZEOF(channel_writer_stack_b), &natural_b);
	zassert_ok(k_sem_take(&channel_parallel_ready, K_SECONDS(1)), "writer A not ready");
	zassert_ok(k_sem_take(&channel_parallel_ready, K_SECONDS(1)), "writer B not ready");
	started = k_cycle_get_64();
	k_sem_give(&channel_parallel_start);
	k_sem_give(&channel_parallel_start);
	channel_writer_finish(&channel_writer_thread_a, &natural_a);
	channel_writer_finish(&channel_writer_thread_b, &natural_b);
	natural_total_us = elapsed_us(started);

	zassert_true(natural_a.rc == 0 || natural_a.rc == -EBUSY,
		     "natural writer A result mismatch: %d", natural_a.rc);
	zassert_true(natural_b.rc == 0 || natural_b.rc == -EBUSY,
		     "natural writer B result mismatch: %d", natural_b.rc);
	zassert_false(natural_a.rc == -EBUSY && natural_b.rc == -EBUSY,
		      "both natural channel writers reported busy");
	if (natural_a.rc == 0) {
		mutations++;
	} else {
		zassert_ok(mbs_channel_set(0U, secrets[0], sizeof(secrets[0]), "natural-a"),
			   "natural writer A retry failed");
		mutations++;
	}
	if (natural_b.rc == 0) {
		mutations++;
	} else {
		zassert_ok(mbs_channel_set(1U, secrets[1], sizeof(secrets[1]), "natural-b"),
			   "natural writer B retry failed");
		mutations++;
	}
	verify_channel(0U, "natural-a");
	verify_channel(1U, "natural-b");
	printk("MBS_CHANNEL_HW_MEASUREMENT phase=natural_parallel a_rc=%d a_us=%u "
	       "b_rc=%d b_us=%u total_us=%u\n",
	       natural_a.rc, natural_a.elapsed_us, natural_b.rc, natural_b.elapsed_us,
	       natural_total_us);

	return mutations;
}

static bool stage_valid(const struct test_stage_record *stage)
{
	return stage->magic == TEST_STAGE_MAGIC && stage->version == TEST_STAGE_VERSION &&
	       (stage->phase == TEST_STAGE_PREPARED || stage->phase == TEST_STAGE_VERIFY) &&
	       stage->phase_inverse == ~stage->phase;
}

static int stage_write(uint32_t phase)
{
	const struct flash_area *area;
	struct test_stage_record next = {
		.magic = TEST_STAGE_MAGIC,
		.version = TEST_STAGE_VERSION,
		.phase = phase,
		.phase_inverse = ~phase,
	};
	int rc;

	rc = flash_area_open(DT_FIXED_PARTITION_ID(STAGE_PARTITION_NODE), &area);
	if (rc != 0) {
		return rc;
	}
	rc = flash_area_flatten(area, 0, area->fa_size);
	if (rc == 0) {
		rc = flash_area_write(area, 0, &next, sizeof(next));
	}
	flash_area_close(area);
	if (rc == 0) {
		test_stage = next;
	}
	return rc;
}

static int stage_erase(void)
{
	const struct flash_area *area;
	int rc;

	rc = flash_area_open(DT_FIXED_PARTITION_ID(STAGE_PARTITION_NODE), &area);
	if (rc != 0) {
		return rc;
	}
	rc = flash_area_flatten(area, 0, area->fa_size);
	flash_area_close(area);
	return rc;
}

static int channel_test_storage_prepare(void)
{
	const struct flash_area *area;
	int rc;

	rc = flash_area_open(DT_FIXED_PARTITION_ID(STAGE_PARTITION_NODE), &area);
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

SYS_INIT(channel_test_storage_prepare, POST_KERNEL, 99);

static void assert_test_partition(void)
{
	const struct flash_area *area;
	int rc = flash_area_open(DT_FIXED_PARTITION_ID(TEST_PARTITION_NODE), &area);

	zassert_ok(rc, "test partition open failed: %d", rc);
	zassert_equal(area->fa_off, DT_REG_ADDR(TEST_PARTITION_NODE),
		      "test partition offset drift");
	zassert_equal(area->fa_size, DT_REG_SIZE(TEST_PARTITION_NODE), "test partition size drift");
	zassert_true(strcmp(DT_PROP(TEST_PARTITION_NODE, label), "meshbus-test-storage") == 0,
		     "settings selected unexpected partition");
	flash_area_close(area);
}

static void verify_channel(size_t index, const char *expected_name)
{
	mbs_channel channel = meshbus_Channel_init_zero;

	zassert_ok(mbs_channel_get(index, &channel), "channel %u missing", (unsigned int)index);
	zassert_equal(channel.secret.size, sizeof(secrets[index]), "secret size mismatch");
	zassert_mem_equal(channel.secret.bytes, secrets[index], sizeof(secrets[index]),
			  "secret mismatch");
	zassert_true(strcmp(channel.name, expected_name) == 0, "name mismatch: %s", channel.name);
}

ZTEST(mbs_channel_service_dut, test_real_zms_reboot_capacity_and_cleanup)
{
	static const uint8_t malformed[] = {0xff, 0xff, 0xff};
	char max_name[MBS_CHANNEL_NAME_MAX_LEN];
	uint64_t started;
	uint32_t mutations = test_stage.phase == TEST_STAGE_VERIFY ? 3U : 0U;
	int rc;

	zassert_ok(test_storage_prepare_rc, "test storage preparation failed: %d",
		   test_storage_prepare_rc);
	zassert_true(stage_valid(&test_stage), "test stage marker is invalid");
	assert_test_partition();
	memset(max_name, 'm', sizeof(max_name) - 1U);
	max_name[sizeof(max_name) - 1U] = '\0';

	if (test_stage.phase == TEST_STAGE_PREPARED) {
		zassert_equal(mbs_channel_store_count(), 0U,
			      "prepared test storage is not empty");
		started = k_cycle_get_64();
		for (size_t i = 0U; i < 3U; i++) {
			const char *name =
				(i == 2U) ? max_name : (i == 0U ? "persist-a" : "persist-b");

			zassert_ok(mbs_channel_set(i, secrets[i], sizeof(secrets[i]), name),
				   "first-boot set failed: %u", (unsigned int)i);
			mutations++;
		}
		printk("MBS_CHANNEL_HW_MEASUREMENT phase=first_boot_set count=3 "
		       "elapsed_us=%u\n",
		       (unsigned int)elapsed_us(started));
		zassert_ok(stage_write(TEST_STAGE_VERIFY), "verify stage write failed");
		printk("MBS_CHANNEL_HW_READY phase=persisted mutations=%u\n", mutations);
		sys_reboot(SYS_REBOOT_WARM);
		zassert_unreachable("software reboot returned");
	}

	started = k_cycle_get_64();
	zassert_equal(mbs_channel_store_count(), 3U, "unexpected restored channel count");
	verify_channel(0U, "persist-a");
	verify_channel(1U, "persist-b");
	verify_channel(2U, max_name);
	printk("MBS_CHANNEL_HW_MEASUREMENT phase=restore_get count=3 elapsed_us=%u\n",
	       (unsigned int)elapsed_us(started));

	rc = mbs_channel_set(3U, secrets[0], sizeof(secrets[0]), "duplicate");
	zassert_equal(rc, -EEXIST, "duplicate secret mismatch: %d", rc);
	for (size_t i = 3U; i < CONFIG_MBS_CHANNEL_MAX_CHANNELS; i++) {
		zassert_ok(mbs_channel_set(i, secrets[i], sizeof(secrets[i]), "fill"),
			   "capacity fill failed: %u", (unsigned int)i);
		mutations++;
	}
	zassert_equal(mbs_channel_store_count(), CONFIG_MBS_CHANNEL_MAX_CHANNELS,
		      "logical store did not reach capacity");
	zassert_equal(mbs_channel_set(CONFIG_MBS_CHANNEL_MAX_CHANNELS, secrets[0],
					  sizeof(secrets[0]), "overflow"),
		      -ENOENT, "out-of-range write mismatch");
	mutations += exercise_parallel_channel_updates();

	for (size_t i = 0U; i < CONFIG_MBS_CHANNEL_MAX_CHANNELS; i++) {
		zassert_ok(mbs_channel_reset(i), "cleanup failed: %u", (unsigned int)i);
		mutations++;
	}
	zassert_equal(mbs_channel_store_count(), 0U, "cleanup left visible records");

	zassert_ok(settings_save_one("meshbus/channel/0", malformed, sizeof(malformed)),
		   "malformed seed write failed");
	mutations++;
	zassert_ok(settings_load_subtree("meshbus/channel"), "malformed reload failed");
	zassert_equal(mbs_channel_store_count(), 0U, "malformed record became visible");
	zassert_ok(mbs_channel_reset(0U), "malformed cleanup failed");
	mutations++;

	zassert_true(mutations <= TEST_MUTATION_LIMIT, "mutation bound exceeded: %u", mutations);
	zassert_ok(stage_erase(), "stage cleanup failed");
	printk("MBS_CHANNEL_HW_RESULT pass mutations=%u final_count=%u\n", mutations,
	       (unsigned int)mbs_channel_store_count());
}

ZTEST_SUITE(mbs_channel_service_dut, NULL, NULL, NULL, NULL, NULL);
