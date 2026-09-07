// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/meshbus/contact.h>
#include <zephyr/meshbus/management.h>
#include <zephyr/meshbus/meshcore.h>
#include <zephyr/meshbus/radio.h>
#include <zephyr/settings/settings.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/ztest.h>

#define TEST_PARTITION_NODE DT_NODELABEL(storage_partition)
#define STAGE_PARTITION_NODE DT_NODELABEL(management_test_stage_partition)
#define PRODUCT_PARTITION_NODE DT_NODELABEL(product_storage_partition)
#define SLOT0_PARTITION_NODE DT_NODELABEL(slot0_partition)
#define SLOT1_PARTITION_NODE DT_NODELABEL(slot1_partition)
#define RRAM_NODE DT_MEM_FROM_PARTITION(TEST_PARTITION_NODE)

/* C2 real-ZMS contention and reboot service-DUT coverage. */

#define PARTITION_START(node_id) DT_REG_ADDR(node_id)
#define PARTITION_END(node_id) (DT_REG_ADDR(node_id) + DT_REG_SIZE(node_id))
#define TEST_STAGE_MAGIC 0x4d424d47U
#define TEST_STAGE_PREPARED 1U
#define TEST_STAGE_VERIFY 2U
#define TEST_ITERATIONS 6U
#define TEST_WRITER_STACK_SIZE 4096U
#define FINAL_LATITUDE (200000 + (TEST_ITERATIONS - 1U))
#define FINAL_SECRET_FIRST (0x40U + (TEST_ITERATIONS - 1U))

enum settings_domain {
  DOMAIN_CONTACT = BIT(0),
  DOMAIN_MANAGEMENT = BIT(1),
  DOMAIN_MESHCORE = BIT(2),
};

struct test_stage_record {
  uint32_t magic;
  uint32_t phase;
  uint32_t inverse;
  uint32_t contact_writes;
  uint32_t management_writes;
  uint32_t meshcore_writes;
  int32_t latitude;
  uint8_t public_key[MESHBUS_MESHCORE_PUBLIC_KEY_SIZE];
  uint8_t secret_first;
  uint8_t reserved[3];
};

enum writer_kind {
  WRITER_CONTACT,
  WRITER_MANAGEMENT,
  WRITER_MESHCORE,
};

struct writer_task {
  enum writer_kind kind;
  struct k_sem done;
  int rc;
  uint32_t elapsed_us;
};

static struct test_stage_record test_stage;
static int test_storage_prepare_rc;
static atomic_t gate_mask;
static atomic_t gate_claimed;
static atomic_t gate_arrived;
static atomic_t gate_timeout;
static atomic_t gate_active;
static atomic_t gate_max_active;
static atomic_t contact_write_count;
static atomic_t management_write_count;
static atomic_t meshcore_write_count;
static K_SEM_DEFINE(gate_release, 0, 2);
static K_SEM_DEFINE(stress_start, 0, 3);
static K_SEM_DEFINE(stress_ready, 0, 3);
static K_THREAD_STACK_DEFINE(writer_stack_a, TEST_WRITER_STACK_SIZE);
static K_THREAD_STACK_DEFINE(writer_stack_b, TEST_WRITER_STACK_SIZE);
static K_THREAD_STACK_DEFINE(writer_stack_c, TEST_WRITER_STACK_SIZE);
static struct k_thread writer_thread_a;
static struct k_thread writer_thread_b;
static struct k_thread writer_thread_c;

BUILD_ASSERT(!IS_ENABLED(CONFIG_FLASH_SIMULATOR),
             "C2 scenario must use real RRAM");
BUILD_ASSERT(IS_ENABLED(CONFIG_MESHBUS_RADIO_DEFAULT_RECEIVE_ONLY),
             "service-DUT scenario must never enable TX");
BUILD_ASSERT(CONFIG_MESHBUS_MESHCORE_DEFAULT_ROLE ==
                 MESHBUS_MESHCORE_ROLE_REPEATER,
             "service-DUT scenario must default to repeater role");
BUILD_ASSERT(DT_NODE_HAS_STATUS(TEST_PARTITION_NODE, okay),
             "test storage missing");
BUILD_ASSERT(DT_NODE_HAS_STATUS(STAGE_PARTITION_NODE, okay),
             "test stage missing");
BUILD_ASSERT(DT_NODE_HAS_STATUS(PRODUCT_PARTITION_NODE, okay),
             "product storage guard missing");
BUILD_ASSERT(DT_REG_ADDR(TEST_PARTITION_NODE) == 0x164000,
             "wrong test storage base");
BUILD_ASSERT(DT_REG_SIZE(TEST_PARTITION_NODE) == 0xf000,
             "wrong test storage size");
BUILD_ASSERT(DT_REG_ADDR(STAGE_PARTITION_NODE) == 0x173000, "wrong stage base");
BUILD_ASSERT(DT_REG_SIZE(STAGE_PARTITION_NODE) == 0x1000, "wrong stage size");
BUILD_ASSERT(DT_REG_ADDR(PRODUCT_PARTITION_NODE) == 0x174000,
             "wrong product storage base");
BUILD_ASSERT(DT_REG_SIZE(PRODUCT_PARTITION_NODE) == 0x9000,
             "wrong product storage size");
BUILD_ASSERT(DT_PROP(RRAM_NODE, erase_block_size) == 4096,
             "unexpected erase alignment");
BUILD_ASSERT(DT_PROP(RRAM_NODE, write_block_size) == 16,
             "unexpected write alignment");
BUILD_ASSERT((sizeof(struct test_stage_record) %
              DT_PROP(RRAM_NODE, write_block_size)) == 0,
             "test stage record is not write aligned");
BUILD_ASSERT(PARTITION_END(SLOT0_PARTITION_NODE) <=
                 PARTITION_START(SLOT1_PARTITION_NODE),
             "slot0 overlaps slot1");
BUILD_ASSERT(PARTITION_END(SLOT1_PARTITION_NODE) <=
                 PARTITION_START(TEST_PARTITION_NODE),
             "slot1 overlaps test storage");
BUILD_ASSERT(PARTITION_END(TEST_PARTITION_NODE) <=
                 PARTITION_START(STAGE_PARTITION_NODE),
             "test storage overlaps stage");
BUILD_ASSERT(PARTITION_END(STAGE_PARTITION_NODE) <=
                 PARTITION_START(PRODUCT_PARTITION_NODE),
             "stage overlaps product storage");
BUILD_ASSERT(PARTITION_END(PRODUCT_PARTITION_NODE) <= DT_REG_SIZE(RRAM_NODE),
             "product storage exceeds CPUAPP RRAM");

extern int __real_settings_save_one(const char *name, const void *value,
                                    size_t val_len);

static enum settings_domain settings_domain_get(const char *name) {
  if (name != NULL && strncmp(name, "meshbus/contact/", 16U) == 0) {
    return DOMAIN_CONTACT;
  }
  if (name != NULL && strncmp(name, "meshbus/management/", 19U) == 0) {
    return DOMAIN_MANAGEMENT;
  }
  if (name != NULL && strncmp(name, "meshbus/meshcore/", 17U) == 0) {
    return DOMAIN_MESHCORE;
  }
  return 0;
}

static void atomic_max_update(atomic_t *target, atomic_val_t value) {
  atomic_val_t current = atomic_get(target);

  while (current < value && !atomic_cas(target, current, value)) {
    current = atomic_get(target);
  }
}

int __wrap_settings_save_one(const char *name, const void *value,
                             size_t val_len) {
  enum settings_domain domain = settings_domain_get(name);
  atomic_val_t active;
  bool gate_participant = false;

  if (domain == DOMAIN_CONTACT) {
    atomic_inc(&contact_write_count);
  } else if (domain == DOMAIN_MANAGEMENT) {
    atomic_inc(&management_write_count);
  } else if (domain == DOMAIN_MESHCORE) {
    atomic_inc(&meshcore_write_count);
  }

  if (domain != 0 && (atomic_get(&gate_mask) & domain) != 0) {
    atomic_val_t previous = atomic_or(&gate_claimed, domain);

    if ((previous & domain) == 0) {
      atomic_val_t arrived = atomic_inc(&gate_arrived) + 1;

      active = atomic_inc(&gate_active) + 1;
      atomic_max_update(&gate_max_active, active);
      gate_participant = true;
      if (arrived == 2) {
        k_sem_give(&gate_release);
        k_sem_give(&gate_release);
      }
      if (k_sem_take(&gate_release, K_SECONDS(2)) != 0) {
        atomic_set(&gate_timeout, 1);
      }
    }
  }

  if (!gate_participant) {
    active = atomic_inc(&gate_active) + 1;
    atomic_max_update(&gate_max_active, active);
  }
  int rc = __real_settings_save_one(name, value, val_len);
  atomic_dec(&gate_active);
  return rc;
}

static void sem_drain(struct k_sem *sem) {
  while (k_sem_take(sem, K_NO_WAIT) == 0) {
  }
}

static void gate_arm(atomic_val_t mask) {
  sem_drain(&gate_release);
  atomic_set(&gate_claimed, 0);
  atomic_set(&gate_arrived, 0);
  atomic_set(&gate_timeout, 0);
  atomic_set(&gate_active, 0);
  atomic_set(&gate_max_active, 0);
  atomic_set(&gate_mask, mask);
}

static void gate_disarm(void) {
  atomic_set(&gate_mask, 0);
  sem_drain(&gate_release);
}

static bool stage_valid(const struct test_stage_record *stage) {
  return stage->magic == TEST_STAGE_MAGIC &&
         (stage->phase == TEST_STAGE_PREPARED ||
          stage->phase == TEST_STAGE_VERIFY) &&
         stage->inverse == ~stage->phase;
}

static int stage_write(uint32_t phase) {
  const struct flash_area *area = NULL;
  int rc = flash_area_open(DT_FIXED_PARTITION_ID(STAGE_PARTITION_NODE), &area);

  if (rc == 0) {
    rc = flash_area_flatten(area, 0, area->fa_size);
  }
  if (rc == 0) {
    test_stage.magic = TEST_STAGE_MAGIC;
    test_stage.phase = phase;
    test_stage.inverse = ~phase;
    rc = flash_area_write(area, 0, &test_stage, sizeof(test_stage));
  }
  if (area != NULL) {
    flash_area_close(area);
  }
  return rc;
}

static int partition_flatten(int partition_id) {
  const struct flash_area *area = NULL;
  int rc = flash_area_open(partition_id, &area);

  if (rc == 0) {
    rc = flash_area_flatten(area, 0, area->fa_size);
    flash_area_close(area);
  }
  return rc;
}

static int management_test_storage_prepare(void) {
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

  memset(&test_stage, 0, sizeof(test_stage));
  rc = partition_flatten(DT_FIXED_PARTITION_ID(TEST_PARTITION_NODE));
  if (rc == 0) {
    rc = stage_write(TEST_STAGE_PREPARED);
  }
  test_storage_prepare_rc = rc;
  return 0;
}

SYS_INIT(management_test_storage_prepare, POST_KERNEL, 99);

static void contact_key_build(uint8_t key[MESHBUS_CONTACT_PUBLIC_KEY_SIZE]) {
  for (size_t i = 0U; i < MESHBUS_CONTACT_PUBLIC_KEY_SIZE; i++) {
    key[i] = (uint8_t)(0x30U + i);
  }
}

static int contact_alias_set(const char *alias) {
  uint8_t key[MESHBUS_CONTACT_PUBLIC_KEY_SIZE];
  meshbus_contact contact = meshbus_Contact_init_zero;
  int rc;

  contact_key_build(key);
  rc = meshbus_contact_find_by_key(key, &contact);
  if (rc != 0) {
    return rc;
  }
  strncpy(contact.alias, alias, sizeof(contact.alias) - 1U);
  contact.alias[sizeof(contact.alias) - 1U] = '\0';
  return meshbus_contact_set(key, &contact);
}

static void writer_entry(void *arg1, void *arg2, void *arg3) {
  struct writer_task *task = arg1;
  uint64_t started;

  ARG_UNUSED(arg2);
  ARG_UNUSED(arg3);
  k_sem_give(&stress_ready);
  (void)k_sem_take(&stress_start, K_FOREVER);
  started = k_cycle_get_64();
  task->rc = 0;

  for (size_t i = 0U; i < TEST_ITERATIONS && task->rc == 0; i++) {
    if (task->kind == WRITER_CONTACT) {
      char alias[16];

      (void)snprintf(alias, sizeof(alias), "contact-%u", (unsigned int)i);
      task->rc = contact_alias_set(alias);
    } else if (task->kind == WRITER_MANAGEMENT) {
      meshbus_management_config cfg = meshbus_ManagementConfig_init_zero;

      task->rc = meshbus_management_config_get(&cfg);
      if (task->rc == 0) {
        cfg.secret.size = MESHBUS_MANAGEMENT_SECRET_MIN_LEN;
        memset(cfg.secret.bytes, 0x70, cfg.secret.size);
        cfg.secret.bytes[0] = (uint8_t)(0x40U + i);
        task->rc = meshbus_management_config_set(&cfg);
      }
      memset(&cfg, 0, sizeof(cfg));
    } else {
      meshbus_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;

      task->rc = meshbus_meshcore_config_get(&cfg);
      if (task->rc == 0) {
        cfg.latitude = 200000 + (int32_t)i;
        task->rc = meshbus_meshcore_config_set(&cfg);
      }
    }
    k_sleep(K_MSEC(CONFIG_MESHBUS_SETTINGS_PERSISTENCE_DELAY + 5));
  }

  task->elapsed_us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_64() - started);
  k_sem_give(&task->done);
}

static void writer_start(struct k_thread *thread, k_thread_stack_t *stack,
                         size_t stack_size, struct writer_task *task,
                         enum writer_kind kind) {
  task->kind = kind;
  task->rc = -EINPROGRESS;
  task->elapsed_us = 0U;
  k_sem_init(&task->done, 0, 1);
  (void)k_thread_create(thread, stack, stack_size, writer_entry, task, NULL,
                        NULL, K_PRIO_PREEMPT(2), 0, K_NO_WAIT);
}

static void writer_finish(struct k_thread *thread, struct writer_task *task) {
  zassert_ok(k_sem_take(&task->done, K_SECONDS(3)), "writer timed out");
  zassert_ok(k_thread_join(thread, K_SECONDS(1)), "writer did not exit");
  zassert_ok(task->rc, "writer failed: kind=%u rc=%d", (unsigned int)task->kind,
             task->rc);
}

static void deterministic_overlap_round(enum settings_domain peer_domain,
                                        const char *alias) {
  static struct writer_task contact_task;
  meshbus_management_config management_cfg = meshbus_ManagementConfig_init_zero;
  meshbus_meshcore_config meshcore_cfg = meshbus_MeshcoreConfig_init_zero;

  sem_drain(&stress_start);
  sem_drain(&stress_ready);
  gate_arm(DOMAIN_CONTACT | peer_domain);
  writer_start(&writer_thread_a, writer_stack_a,
               K_THREAD_STACK_SIZEOF(writer_stack_a), &contact_task,
               WRITER_CONTACT);
  zassert_ok(k_sem_take(&stress_ready, K_SECONDS(1)),
             "contact writer did not start");
  k_sem_give(&stress_start);

  if (peer_domain == DOMAIN_MANAGEMENT) {
    zassert_ok(meshbus_management_config_get(&management_cfg));
    management_cfg.secret.size = MESHBUS_MANAGEMENT_SECRET_MIN_LEN;
    memset(management_cfg.secret.bytes, 0x61, management_cfg.secret.size);
    zassert_ok(meshbus_management_config_set(&management_cfg));
    memset(&management_cfg, 0, sizeof(management_cfg));
  } else {
    zassert_ok(meshbus_meshcore_config_get(&meshcore_cfg));
    meshcore_cfg.longitude++;
    zassert_ok(meshbus_meshcore_config_set(&meshcore_cfg));
  }

  writer_finish(&writer_thread_a, &contact_task);
  k_sleep(K_MSEC(CONFIG_MESHBUS_SETTINGS_PERSISTENCE_DELAY + 50));
  gate_disarm();
  zassert_equal(atomic_get(&gate_arrived), 2,
                "two settings domains did not meet");
  zassert_equal(atomic_get(&gate_timeout), 0,
                "settings overlap barrier timed out");
  zassert_true(atomic_get(&gate_max_active) >= 2,
               "settings calls did not overlap");
  zassert_ok(contact_alias_set(alias), "round alias finalize failed");
}

static void first_boot_exercise(void) {
  static struct writer_task contact_task;
  static struct writer_task management_task;
  static struct writer_task meshcore_task;
  uint8_t key[MESHBUS_CONTACT_PUBLIC_KEY_SIZE];
  meshbus_meshcore_config meshcore_cfg = meshbus_MeshcoreConfig_init_zero;
  meshbus_management_config management_cfg = meshbus_ManagementConfig_init_zero;

  k_sleep(K_MSEC(CONFIG_MESHBUS_SETTINGS_PERSISTENCE_DELAY + 100));
  contact_key_build(key);
  zassert_ok(meshbus_contact_insert(key, "combined-contact",
                                    MESHBUS_CONTACT_ROLE_CHAT),
             "contact insert failed");
  atomic_set(&contact_write_count, 0);
  atomic_set(&management_write_count, 0);
  atomic_set(&meshcore_write_count, 0);

  deterministic_overlap_round(DOMAIN_MANAGEMENT, "management-overlap");
  deterministic_overlap_round(DOMAIN_MESHCORE, "meshcore-overlap");

  sem_drain(&stress_start);
  sem_drain(&stress_ready);
  writer_start(&writer_thread_a, writer_stack_a,
               K_THREAD_STACK_SIZEOF(writer_stack_a), &contact_task,
               WRITER_CONTACT);
  writer_start(&writer_thread_b, writer_stack_b,
               K_THREAD_STACK_SIZEOF(writer_stack_b), &management_task,
               WRITER_MANAGEMENT);
  writer_start(&writer_thread_c, writer_stack_c,
               K_THREAD_STACK_SIZEOF(writer_stack_c), &meshcore_task,
               WRITER_MESHCORE);
  for (size_t i = 0U; i < 3U; i++) {
    zassert_ok(k_sem_take(&stress_ready, K_SECONDS(1)),
               "stress writer not ready");
  }
  for (size_t i = 0U; i < 3U; i++) {
    k_sem_give(&stress_start);
  }
  writer_finish(&writer_thread_a, &contact_task);
  writer_finish(&writer_thread_b, &management_task);
  writer_finish(&writer_thread_c, &meshcore_task);
  k_sleep(K_MSEC(CONFIG_MESHBUS_SETTINGS_PERSISTENCE_DELAY + 100));

  zassert_ok(meshbus_meshcore_config_get(&meshcore_cfg));
  zassert_ok(meshbus_management_config_get(&management_cfg));
  zassert_equal(meshcore_cfg.latitude, FINAL_LATITUDE,
                "MeshCore final value lost");
  zassert_equal(management_cfg.secret.size, MESHBUS_MANAGEMENT_SECRET_MIN_LEN,
                "Management secret size lost");
  zassert_equal(management_cfg.secret.bytes[0], FINAL_SECRET_FIRST,
                "Management final value lost");
  zassert_true(atomic_get(&contact_write_count) > 0,
               "no Contact writes reached ZMS");
  zassert_true(atomic_get(&management_write_count) > 0,
               "no Management writes reached ZMS");
  zassert_true(atomic_get(&meshcore_write_count) > 0,
               "no MeshCore writes reached ZMS");

  test_stage.contact_writes = (uint32_t)atomic_get(&contact_write_count);
  test_stage.management_writes = (uint32_t)atomic_get(&management_write_count);
  test_stage.meshcore_writes = (uint32_t)atomic_get(&meshcore_write_count);
  test_stage.latitude = meshcore_cfg.latitude;
  memcpy(test_stage.public_key, meshcore_cfg.public_key.bytes,
         sizeof(test_stage.public_key));
  test_stage.secret_first = management_cfg.secret.bytes[0];
  memset(&management_cfg, 0, sizeof(management_cfg));
  zassert_ok(stage_write(TEST_STAGE_VERIFY), "verify stage write failed");
  printk("MB_MANAGEMENT_DUT_READY phase=reboot contact_writes=%u "
         "management_writes=%u "
         "meshcore_writes=%u contact_us=%u management_us=%u meshcore_us=%u\n",
         test_stage.contact_writes, test_stage.management_writes,
         test_stage.meshcore_writes, contact_task.elapsed_us,
         management_task.elapsed_us, meshcore_task.elapsed_us);
  sys_reboot(SYS_REBOOT_WARM);
  zassert_unreachable("software reboot returned");
}

static void second_boot_verify_and_cleanup(void) {
  uint8_t key[MESHBUS_CONTACT_PUBLIC_KEY_SIZE];
  meshbus_contact contact = meshbus_Contact_init_zero;
  meshbus_management_config management_cfg = meshbus_ManagementConfig_init_zero;
  meshbus_meshcore_config meshcore_cfg = meshbus_MeshcoreConfig_init_zero;
  meshbus_radio_config radio_cfg = meshbus_RadioConfig_init_zero;

  contact_key_build(key);
  zassert_ok(meshbus_contact_find_by_key(key, &contact),
             "Contact did not survive reboot");
  zassert_str_equal(contact.alias, "contact-5",
                    "Contact final alias did not survive reboot");
  zassert_ok(meshbus_management_config_get(&management_cfg));
  zassert_equal(management_cfg.secret.size, MESHBUS_MANAGEMENT_SECRET_MIN_LEN,
                "Management secret size did not survive reboot");
  zassert_equal(management_cfg.secret.bytes[0], test_stage.secret_first,
                "Management secret did not survive reboot");
  zassert_ok(meshbus_meshcore_config_get(&meshcore_cfg));
  zassert_equal(meshcore_cfg.latitude, test_stage.latitude,
                "MeshCore config did not survive reboot");
  zassert_mem_equal(meshcore_cfg.public_key.bytes, test_stage.public_key,
                    sizeof(test_stage.public_key),
                    "MeshCore identity changed across reboot");
  zassert_equal(meshbus_meshcore_active_role_get(),
                MESHBUS_MESHCORE_ROLE_REPEATER,
                "MeshCore role changed across reboot");
  zassert_ok(meshbus_radio_config_get(&radio_cfg));
  zassert_true(radio_cfg.receive_only, "Radio is not receive-only");

  radio_cfg.enabled = false;
  zassert_ok(meshbus_radio_config_set(&radio_cfg),
             "final radio disable failed");
  k_sleep(K_MSEC(CONFIG_MESHBUS_SETTINGS_PERSISTENCE_DELAY + 50));
  zassert_ok(partition_flatten(DT_FIXED_PARTITION_ID(TEST_PARTITION_NODE)),
             "test storage cleanup failed");
  zassert_ok(partition_flatten(DT_FIXED_PARTITION_ID(STAGE_PARTITION_NODE)),
             "stage cleanup failed");
  memset(&management_cfg, 0, sizeof(management_cfg));
  printk("MB_MANAGEMENT_DUT_RESULT pass domains=3 overlap=2 reboot=1 identity=1 "
         "receive_only=1 cleanup=1\n");
}

ZTEST(meshbus_management_service_dut,
      test_combined_settings_contention_and_reboot) {
  zassert_ok(test_storage_prepare_rc, "test storage prepare failed: %d",
             test_storage_prepare_rc);
  if (test_stage.phase == TEST_STAGE_VERIFY) {
    second_boot_verify_and_cleanup();
    return;
  }
  first_boot_exercise();
}

ZTEST_SUITE(meshbus_management_service_dut, NULL, NULL, NULL, NULL, NULL);
