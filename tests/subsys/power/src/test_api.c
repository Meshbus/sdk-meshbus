/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <setjmp.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#if DT_HAS_CHOSEN(meshbus_fuel_gauge)
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#endif
#include <power/power.h>
#if IS_ENABLED(CONFIG_MBS_MESHCORE)
#include <meshcore/meshcore.h>
#endif
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

#if DT_HAS_CHOSEN(meshbus_fuel_gauge)
#define ADC_NODE       DT_NODELABEL(test_adc)
#define GPIO_NODE      DT_NODELABEL(test_gpio)
#define CHARGER_NODE   DT_NODELABEL(test_charger)
#define ADC_CHANNEL_ID 0U
#define CHARGING_PIN   DT_GPIO_PIN(CHARGER_NODE, charging_gpios)
#define ONLINE_PIN     DT_GPIO_PIN(CHARGER_NODE, online_gpios)
#define POWER_TEST_SAMPLE_INTERVAL_MS CONFIG_MBS_POWER_SAMPLE_INTERVAL
#define POWER_TEST_LOW_TIMEOUT_S      2U
#define POWER_TEST_LOW_RESET_TIMEOUT_S 4U
#define POWER_TEST_LOW_SAMPLE_COUNT   6U

static const struct device *const adc_dev = DEVICE_DT_GET(ADC_NODE);
static const struct device *const gpio_dev = DEVICE_DT_GET(GPIO_NODE);

struct test_adc_state {
	uint32_t voltage_mv;
	uint32_t read_count;
	int rc;
};

static struct test_adc_state adc_state;
static K_SEM_DEFINE(power_event_sem, 0, 1);

static void power_event_listener_cb(const struct zbus_channel *chan)
{
	ARG_UNUSED(chan);

	k_sem_give(&power_event_sem);
}

ZBUS_LISTENER_DEFINE(power_event_listener, power_event_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_power_fuel_gauge_data_chan, power_event_listener, 3);
#endif

static enum mbs_power_action last_action;
static uint32_t action_count;
static uint32_t poweroff_count;
static uint32_t reboot_count;
static int last_reboot_type;
static jmp_buf power_jump_env;
static bool power_jump_armed;

#if IS_ENABLED(CONFIG_MBS_MESHCORE)
#define MESHCORE_SETTINGS_CONFIG_KEY "meshbus/meshcore/config"

static K_SEM_DEFINE(meshcore_shutdown_done_sem, 0, 1);
static uint32_t meshcore_settings_save_count;
static int meshcore_shutdown_jump_code;
static bool meshcore_save_before_poweroff;
static bool meshcore_settings_save_fail;
#endif

int __real_settings_save_one(const char *name, const void *value, size_t value_len);

int __wrap_settings_save_one(const char *name, const void *value, size_t value_len)
{
#if IS_ENABLED(CONFIG_MBS_MESHCORE)
	if (name != NULL && strcmp(name, MESHCORE_SETTINGS_CONFIG_KEY) == 0) {
		meshcore_settings_save_count++;
		meshcore_save_before_poweroff = poweroff_count == 0U;
		if (meshcore_settings_save_fail) {
			return -EIO;
		}

		return 0;
	}
#endif

	return __real_settings_save_one(name, value, value_len);
}

void __wrap_sys_poweroff(void)
{
	poweroff_count++;
	if (power_jump_armed) {
		longjmp(power_jump_env, 1);
	}
}

void __wrap_sys_reboot(int type)
{
	reboot_count++;
	last_reboot_type = type;
	if (power_jump_armed) {
		longjmp(power_jump_env, 2);
	}
}

#if IS_ENABLED(CONFIG_MBS_MESHCORE)
static void meshcore_shutdown_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	meshcore_shutdown_jump_code = setjmp(power_jump_env);
	if (meshcore_shutdown_jump_code == 0) {
		power_jump_armed = true;
		(void)mbs_power_shutdown();
		zassert_unreachable("mbs_power_shutdown should reach sys_poweroff");
	}
	power_jump_armed = false;
	k_sem_give(&meshcore_shutdown_done_sem);
}
K_WORK_DEFINE(meshcore_shutdown_work, meshcore_shutdown_work_handler);

static void meshcore_shutdown_run(void)
{
	k_sem_reset(&meshcore_shutdown_done_sem);
	zassert_equal(k_work_submit(&meshcore_shutdown_work), 1,
		      "shutdown work was not submitted");
	zassert_ok(k_sem_take(&meshcore_shutdown_done_sem, K_SECONDS(2)),
		   "shutdown work did not reach sys_poweroff");
}
#endif

static void power_action_cb(enum mbs_power_action action, void *user_data)
{
	ARG_UNUSED(user_data);
	last_action = action;
	action_count++;
}
MBS_POWER_ACTION_CALLBACK_DEFINE(power_action_cb, NULL);

#if DT_HAS_CHOSEN(meshbus_fuel_gauge)
static int test_adc_value_func(const struct device *dev, unsigned int chan, void *data,
			       uint32_t *result)
{
	struct test_adc_state *state = data;

	ARG_UNUSED(dev);
	ARG_UNUSED(chan);

	state->read_count++;
	if (state->rc != 0) {
		return state->rc;
	}

	*result = state->voltage_mv;
	return 0;
}

static void test_wait_fuel_gauge_cache_expire(void)
{
	k_sleep(K_MSEC(CONFIG_FUEL_GAUGE_COMPOSITE_DATA_VALIDITY_MS + 5));
}

static void test_set_adc_voltage(uint32_t voltage_mv)
{
	adc_state.voltage_mv = voltage_mv;
	adc_state.rc = 0;
	test_wait_fuel_gauge_cache_expire();
}

static void test_set_adc_error(int rc)
{
	adc_state.rc = rc;
	test_wait_fuel_gauge_cache_expire();
}

static void test_set_charging(bool charging)
{
	zassert_ok(gpio_emul_input_set(gpio_dev, CHARGING_PIN, charging ? 1 : 0));
	zassert_ok(gpio_emul_input_set(gpio_dev, ONLINE_PIN, charging ? 1 : 0));
}

static void test_wait_for_next_power_event(k_timeout_t timeout)
{
	k_sem_reset(&power_event_sem);
	zassert_ok(k_sem_take(&power_event_sem, timeout), "Timed out waiting for power event");
}

static void test_reset_fuel_gauge_state(void)
{
	zassert_true(device_is_ready(adc_dev), "ADC emulator is not ready");
	zassert_true(device_is_ready(gpio_dev), "GPIO emulator is not ready");

	adc_state.voltage_mv = 3900U;
	adc_state.read_count = 0U;
	adc_state.rc = 0;
	k_sem_reset(&power_event_sem);
	zassert_ok(
		adc_emul_value_func_set(adc_dev, ADC_CHANNEL_ID, test_adc_value_func, &adc_state));
	test_set_charging(false);
	test_wait_fuel_gauge_cache_expire();
}

static bool test_event_matches_voltage(const struct mbs_power_fuel_gauge_data_event *event,
				       uint32_t voltage_mv)
{
	return event->voltage_mv >= voltage_mv - 20U && event->voltage_mv <= voltage_mv + 20U;
}

static void test_wait_for_voltage_event(uint32_t voltage_mv)
{
	struct mbs_power_fuel_gauge_data_event event = {0};

	for (uint8_t i = 0U; i < 4U; i++) {
		zassert_ok(zbus_chan_read(&mbs_power_fuel_gauge_data_chan, &event,
					  K_MSEC(POWER_TEST_SAMPLE_INTERVAL_MS + 250)));
		if (test_event_matches_voltage(&event, voltage_mv)) {
			return;
		}
		k_sleep(K_MSEC(POWER_TEST_SAMPLE_INTERVAL_MS + 100U));
	}

	zassert_unreachable("Timed out waiting for expected fuel-gauge voltage");
}

static void test_wait_without_poweroff(uint32_t duration_ms)
{
	uint32_t before = poweroff_count;

	k_sleep(K_MSEC(duration_ms));
	zassert_equal(poweroff_count, before);
}

static void test_set_low_voltage_config(uint32_t timeout_s)
{
	mbs_power_config cfg = meshbus_PowerConfig_init_zero;

	cfg.low_voltage_shutdown_timeout = timeout_s;
	zassert_ok(mbs_power_config_set(&cfg));
}
#endif

static mbs_power_config valid_power_config(void)
{
	mbs_power_config cfg = meshbus_PowerConfig_init_zero;

	cfg.low_voltage_shutdown_timeout = 10;
	cfg.losing_power_shutdown_timeout = 20;
	cfg.no_connection_shutdown_timeout = 30;
	return cfg;
}

static void power_before(void *fixture)
{
	ARG_UNUSED(fixture);
	zassert_ok(mbs_power_config_reset());
	last_action = 0;
	action_count = 0;
	poweroff_count = 0;
	reboot_count = 0;
	last_reboot_type = -1;
	power_jump_armed = false;
#if IS_ENABLED(CONFIG_MBS_MESHCORE)
	meshcore_settings_save_count = 0U;
	meshcore_shutdown_jump_code = 0;
	meshcore_save_before_poweroff = false;
	meshcore_settings_save_fail = false;
	k_sem_reset(&meshcore_shutdown_done_sem);
#endif
#if DT_HAS_CHOSEN(meshbus_fuel_gauge)
	test_reset_fuel_gauge_state();
#endif
}

ZTEST(mbs_power_contract, test_config_defaults_set_get_reset_and_validation)
{
	mbs_power_config cfg = valid_power_config();
	mbs_power_config got;

	zassert_equal(mbs_power_config_get(NULL), -EINVAL);
	zassert_ok(mbs_power_config_get(&got));
	zassert_equal(got.low_voltage_shutdown_timeout, 0);
	zassert_equal(got.losing_power_shutdown_timeout, 0);
	zassert_equal(got.no_connection_shutdown_timeout, 0);

	zassert_ok(mbs_power_config_set(&cfg));
	zassert_ok(mbs_power_config_get(&got));
	zassert_equal(got.low_voltage_shutdown_timeout, cfg.low_voltage_shutdown_timeout);

	cfg.low_voltage_shutdown_timeout = 301;
	zassert_equal(mbs_power_config_set(&cfg), -EINVAL);

	cfg = valid_power_config();
	cfg.losing_power_shutdown_timeout = 301;
	zassert_equal(mbs_power_config_set(&cfg), -EINVAL);

	cfg = valid_power_config();
	cfg.no_connection_shutdown_timeout = 301;
	zassert_equal(mbs_power_config_set(&cfg), -EINVAL);
}

#if !DT_HAS_CHOSEN(meshbus_fuel_gauge)
ZTEST(mbs_power_contract, test_no_fuel_gauge_and_public_channel_contract)
{
	struct mbs_power_fuel_gauge_data_event event = {
		.voltage_mv = 3700,
		.soc_percent = 50,
		.temperature_dk = 2981,
		.charging = true,
		.online = true,
	};
	uint16_t voltage;
	uint8_t soc;
	uint16_t temperature;

	zassert_false(mbs_power_is_charging());
	zassert_false(mbs_power_is_online());
	zassert_equal(mbs_power_fuel_gauge_get(&voltage, &soc, &temperature), -ENODEV);
	zassert_ok(zbus_chan_pub(&mbs_power_fuel_gauge_data_chan, &event, K_NO_WAIT));
	zassert_ok(zbus_chan_read(&mbs_power_fuel_gauge_data_chan, &event, K_NO_WAIT));
	zassert_equal(event.voltage_mv, 3700);
	zassert_true(event.charging);
}
#endif

#if DT_HAS_CHOSEN(meshbus_fuel_gauge)
ZTEST(mbs_power_contract, test_fuel_gauge_get_bootstraps_then_uses_cache)
{
	uint32_t first_read_count;
	uint16_t first_voltage;
	uint16_t voltage;
	uint8_t soc;
	uint16_t temperature;

	test_set_adc_voltage(3900U);
	test_wait_for_voltage_event(3900U);
	zassert_ok(mbs_power_fuel_gauge_get(&voltage, &soc, &temperature));
	zassert_within(voltage, 3900U, 20U);
	zassert_true(soc > 0U, "3900 mV should map to non-zero SOC");
	first_voltage = voltage;
	first_read_count = adc_state.read_count;

	test_set_adc_voltage(4200U);
	zassert_ok(mbs_power_fuel_gauge_get(&voltage, &soc, &temperature));
	zassert_equal(voltage, first_voltage, "second getter should return cached voltage");
	zassert_equal(adc_state.read_count, first_read_count,
		      "second getter should not read hardware");
}

ZTEST(mbs_power_contract, test_periodic_work_publishes_fuel_gauge_event)
{
	struct mbs_power_fuel_gauge_data_event event = {0};

	test_set_adc_voltage(4010U);

	for (uint8_t i = 0U; i < 20U; i++) {
		zassert_ok(zbus_chan_read(&mbs_power_fuel_gauge_data_chan, &event,
					  K_MSEC(POWER_TEST_SAMPLE_INTERVAL_MS + 250)));
		if (event.voltage_mv >= 3990U && event.voltage_mv <= 4030U) {
			break;
		}
		k_sleep(K_MSEC(POWER_TEST_SAMPLE_INTERVAL_MS + 100U));
	}

	zassert_within(event.voltage_mv, 4010U, 20U);
	zassert_true(event.soc_percent > 0U);
}

ZTEST(mbs_power_contract, test_charger_notification_publishes_without_periodic_delay)
{
	struct mbs_power_fuel_gauge_data_event event = {0};

	test_set_charging(false);
	test_set_adc_voltage(3900U);
	test_wait_for_next_power_event(K_MSEC(POWER_TEST_SAMPLE_INTERVAL_MS + 250U));

	k_sem_reset(&power_event_sem);
	test_set_adc_voltage(4010U);
	test_set_charging(true);
	zassert_ok(k_sem_take(&power_event_sem, K_MSEC(250)),
		   "Charger notification did not publish before the periodic interval");
	zassert_ok(zbus_chan_read(&mbs_power_fuel_gauge_data_chan, &event, K_NO_WAIT));
	zassert_true(event.charging);
	zassert_true(event.online);
	zassert_true(test_event_matches_voltage(&event, 4010U));

	k_sem_reset(&power_event_sem);
	test_set_charging(false);
	zassert_ok(k_sem_take(&power_event_sem, K_MSEC(250)),
		   "Charger removal did not publish before the periodic interval");
	zassert_ok(zbus_chan_read(&mbs_power_fuel_gauge_data_chan, &event, K_NO_WAIT));
	zassert_false(event.charging);
	zassert_false(event.online);
}

ZTEST(mbs_power_contract,
      test_low_voltage_state_resets_on_recovery_charging_invalid_and_disable)
{
	mbs_power_config cfg = meshbus_PowerConfig_init_zero;

	test_set_low_voltage_config(POWER_TEST_LOW_RESET_TIMEOUT_S);
	test_set_charging(false);
	test_set_adc_voltage(2500U);
	test_wait_without_poweroff((POWER_TEST_LOW_SAMPLE_COUNT - 1U) *
				   POWER_TEST_SAMPLE_INTERVAL_MS +
				   (POWER_TEST_LOW_RESET_TIMEOUT_S * MSEC_PER_SEC / 2U));

	test_set_adc_voltage(3900U);
	test_wait_without_poweroff(POWER_TEST_SAMPLE_INTERVAL_MS +
				   POWER_TEST_LOW_RESET_TIMEOUT_S * MSEC_PER_SEC + 500U);

	test_set_adc_voltage(2500U);
	test_wait_without_poweroff((POWER_TEST_LOW_SAMPLE_COUNT - 1U) *
				   POWER_TEST_SAMPLE_INTERVAL_MS +
				   (POWER_TEST_LOW_RESET_TIMEOUT_S * MSEC_PER_SEC / 2U));
	test_set_charging(true);
	test_wait_without_poweroff(POWER_TEST_SAMPLE_INTERVAL_MS +
				   POWER_TEST_LOW_RESET_TIMEOUT_S * MSEC_PER_SEC + 500U);
	test_set_charging(false);

	test_wait_without_poweroff((POWER_TEST_LOW_SAMPLE_COUNT - 1U) *
				   POWER_TEST_SAMPLE_INTERVAL_MS +
				   (POWER_TEST_LOW_RESET_TIMEOUT_S * MSEC_PER_SEC / 2U));
	test_set_adc_error(-EIO);
	test_wait_without_poweroff(POWER_TEST_SAMPLE_INTERVAL_MS +
				   POWER_TEST_LOW_RESET_TIMEOUT_S * MSEC_PER_SEC + 500U);
	test_set_adc_voltage(2500U);

	k_sleep(K_MSEC(POWER_TEST_LOW_SAMPLE_COUNT * POWER_TEST_SAMPLE_INTERVAL_MS + 250U));
	zassert_ok(mbs_power_config_set(&cfg));
	test_wait_without_poweroff(POWER_TEST_LOW_RESET_TIMEOUT_S * MSEC_PER_SEC + 500U);
}

ZTEST(mbs_power_contract, test_zz_low_voltage_shutdown_requires_six_samples)
{
	test_set_low_voltage_config(POWER_TEST_LOW_TIMEOUT_S);
	test_set_charging(false);
	test_set_adc_voltage(2500U);

	test_wait_without_poweroff((POWER_TEST_LOW_SAMPLE_COUNT - 1U) *
				   POWER_TEST_SAMPLE_INTERVAL_MS +
				   (POWER_TEST_LOW_TIMEOUT_S * MSEC_PER_SEC / 2U));
	k_sleep(K_MSEC(POWER_TEST_SAMPLE_INTERVAL_MS + POWER_TEST_LOW_TIMEOUT_S * MSEC_PER_SEC +
		       500U));
	zassert_true(poweroff_count > 0U);
}
#endif

#if !DT_HAS_CHOSEN(meshbus_fuel_gauge) && !IS_ENABLED(CONFIG_MBS_MESHCORE)
ZTEST(mbs_power_contract, test_shutdown_and_reboot_publish_action_callbacks)
{
	int jump_code;

	power_jump_armed = true;
	jump_code = setjmp(power_jump_env);
	if (jump_code == 0) {
		(void)mbs_power_shutdown();
		zassert_unreachable("mbs_power_shutdown should reach sys_poweroff");
	}
	power_jump_armed = false;
	zassert_equal(jump_code, 1);
	zassert_equal(action_count, 1);
	zassert_equal(last_action, MBS_POWER_ACTION_SHUTDOWN);
	zassert_equal(poweroff_count, 1);
	zassert_equal(reboot_count, 0);

	action_count = 0;
	poweroff_count = 0;
	reboot_count = 0;
	last_reboot_type = -1;

	power_jump_armed = true;
	jump_code = setjmp(power_jump_env);
	if (jump_code == 0) {
		mbs_power_reboot();
		zassert_unreachable("mbs_power_reboot should reach sys_reboot");
	}
	power_jump_armed = false;
	zassert_equal(jump_code, 2);
	zassert_equal(action_count, 1);
	zassert_equal(last_action, MBS_POWER_ACTION_REBOOT);
	zassert_equal(reboot_count, 1);
	zassert_equal(last_reboot_type, SYS_REBOOT_COLD);
}
#endif

#if IS_ENABLED(CONFIG_MBS_MESHCORE)
ZTEST(mbs_power_contract, test_meshcore_persistence_finishes_before_workqueue_shutdown)
{
	mbs_meshcore_config cfg;

	zassert_ok(mbs_meshcore_config_get(&cfg));
	cfg.advert_position = !cfg.advert_position;
	zassert_ok(mbs_meshcore_config_set(&cfg));

	meshcore_shutdown_run();
	zassert_equal(meshcore_shutdown_jump_code, 1,
		      "shutdown did not reach sys_poweroff");
	zassert_equal(poweroff_count, 1U);
	zassert_equal(meshcore_settings_save_count, 1U,
		      "MeshCore config was not persisted during shutdown");
	zassert_true(meshcore_save_before_poweroff,
		     "MeshCore config persisted after sys_poweroff");
	zassert_equal(mbs_meshcore_config_set(&cfg), -ESHUTDOWN,
		      "config set was accepted after shutdown started");
	zassert_equal(mbs_meshcore_config_reset(), -ESHUTDOWN,
		      "config reset was accepted after shutdown started");

	meshcore_settings_save_count = 0U;
	meshcore_save_before_poweroff = false;
	meshcore_settings_save_fail = true;
	poweroff_count = 0U;
	meshcore_shutdown_run();
	zassert_equal(meshcore_shutdown_jump_code, 1,
		      "failed persistence must still continue to sys_poweroff");
	zassert_equal(poweroff_count, 1U);
	zassert_equal(meshcore_settings_save_count, 1U,
		      "shutdown did not attempt failing MeshCore persistence");
	zassert_true(meshcore_save_before_poweroff,
		     "failed MeshCore persistence was attempted after sys_poweroff");
}
#endif

ZTEST_SUITE(mbs_power_contract, NULL, NULL, power_before, NULL, NULL);
