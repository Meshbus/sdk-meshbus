/* SPDX-FileCopyrightText: 2026 FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <indicator/indicator.h>
#include <power/power.h>

static const struct device *gpio = DEVICE_DT_GET(DT_NODELABEL(test_gpio));
static void expect_light(bool on)
{
	zassert_equal(gpio_emul_output_get(gpio, 0), on, "unexpected light output");
}
static void configure(bool master, bool messages, bool system, bool heartbeat)
{
	mbs_indicator_config cfg;
	zassert_ok(mbs_indicator_config_get(&cfg));
	cfg.light_enabled = master;
	cfg.light_feedback.message_enabled = messages;
	cfg.light_feedback.system_enabled = system;
	cfg.light_feedback.heartbeat_enabled = heartbeat;
	zassert_ok(mbs_indicator_config_set(&cfg));
	k_sleep(K_MSEC(20));
}
static void battery(uint8_t soc, bool charging)
{
	struct mbs_power_fuel_gauge_data_event event = {
		.voltage_mv = 3500, .soc_percent = soc, .charging = charging,
	};
	zassert_ok(zbus_chan_pub(&mbs_power_fuel_gauge_data_chan, &event, K_MSEC(20)));
	k_sleep(K_MSEC(20));
}
static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	configure(false, false, false, false);
	mbs_indicator_startup_complete(true);
	battery(80, false);
	k_sleep(K_MSEC(30));
	configure(true, true, true, false);
	expect_light(false);
}
ZTEST(indicator_feedback, test_receive_pattern_and_burst_merge)
{
	zassert_ok(mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_RECEIVED));
	k_sleep(K_MSEC(20));
	expect_light(true);
	for (int i = 0; i < 3; i++) {
		zassert_ok(mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_RECEIVED));
	}
	k_sleep(K_MSEC(90));
	expect_light(false);
	k_sleep(K_MSEC(110));
	expect_light(true);
	k_sleep(K_MSEC(240));
	expect_light(false);
	k_sleep(K_MSEC(600));
	expect_light(false);
}
ZTEST(indicator_feedback, test_preemption_does_not_resume_ack)
{
	zassert_ok(mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_ACK));
	k_sleep(K_MSEC(30));
	expect_light(true);
	zassert_ok(mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_MESSAGE_FAILED));
	k_sleep(K_MSEC(110));
	expect_light(false); /* Failure off phase; the 400ms ACK was interrupted. */
	k_sleep(K_MSEC(600));
	expect_light(false);
}
ZTEST(indicator_feedback, test_category_and_master_switches)
{
	configure(true, false, true, false);
	zassert_ok(mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_ACK));
	k_sleep(K_MSEC(30));
	expect_light(false);
	zassert_ok(mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_SYSTEM_SUCCESS));
	k_sleep(K_MSEC(30));
	expect_light(true);
	configure(false, true, true, true);
	expect_light(false);
	zassert_ok(mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_MESSAGE_FAILED));
	k_sleep(K_MSEC(500));
	expect_light(false);
	configure(true, true, false, false);
	expect_light(false); /* Disabled events are never replayed. */
	zassert_ok(mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_SYSTEM_FAILED));
	k_sleep(K_MSEC(30));
	expect_light(false);
}
ZTEST(indicator_feedback, test_low_battery_hysteresis_and_charging)
{
	battery(15, false);
	expect_light(true);
	battery(18, false);
	expect_light(true);
	battery(20, false);
	expect_light(false);
	battery(14, true);
	expect_light(false);
	k_sleep(K_MSEC(1100));
	battery(14, false);
	expect_light(true);
	battery(14, true);
	expect_light(false);
}
ZTEST(indicator_feedback, test_fault_allows_messages_between_reminders)
{
	mbs_indicator_startup_complete(false);
	k_sleep(K_MSEC(20));
	expect_light(true);
	zassert_ok(mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_ACK));
	k_sleep(K_MSEC(1250));
	expect_light(true); /* The pending ACK follows the finite fault pattern. */
	k_sleep(K_MSEC(450));
	expect_light(false);
}
ZTEST(indicator_feedback, test_heartbeat_recovers_without_catchup)
{
	configure(true, true, true, true);
	zassert_ok(mbs_indicator_light_idle(100, 1000));
	zassert_ok(mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_ACCEPTED));
	k_sleep(K_MSEC(150));
	expect_light(false);
	k_sleep(K_MSEC(1040));
	expect_light(true);
	k_sleep(K_MSEC(150));
	expect_light(false);
}
ZTEST(indicator_feedback, test_equal_priority_waits_without_interrupting)
{
	zassert_ok(mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_ACK));
	k_sleep(K_MSEC(20));
	zassert_ok(mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_RECEIVED));
	k_sleep(K_MSEC(130));
	expect_light(true); /* ACK stays on; an interrupted receive would be off. */
	k_sleep(K_MSEC(300));
	expect_light(true); /* The receive pattern follows the ACK. */
	k_sleep(K_MSEC(100));
	expect_light(false);
	k_sleep(K_MSEC(100));
	expect_light(true);
	k_sleep(K_MSEC(200));
	expect_light(false);
}
ZTEST(indicator_feedback, test_expired_pending_result_is_not_replayed)
{
	mbs_indicator_startup_complete(false);
	k_sleep(K_MSEC(20));
	zassert_ok(mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_ACCEPTED));
	zassert_ok(mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_UNCONFIRMED));
	k_sleep(K_MSEC(2180));
	/* A fresh high priority event keeps the original acceptance waiting. */
	zassert_ok(mbs_indicator_feedback_submit(MBS_INDICATOR_FEEDBACK_UNCONFIRMED));
	k_sleep(K_MSEC(1430));
	expect_light(false); /* Acceptance would be on here without the 3s expiry. */
}

ZTEST_SUITE(indicator_feedback, NULL, NULL, before, NULL, NULL);
