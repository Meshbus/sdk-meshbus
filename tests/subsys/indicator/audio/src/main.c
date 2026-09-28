/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/atomic.h>
#include <indicator/indicator.h>

static atomic_t output_period;
static atomic_t tones;
static int test_pwm_set(const struct device *dev, uint32_t channel,
                        uint32_t period, uint32_t pulse, pwm_flags_t flags)
{
    ARG_UNUSED(dev); ARG_UNUSED(channel); ARG_UNUSED(flags);
    if (pulse) { atomic_inc(&tones); }
    atomic_set(&output_period, pulse ? period : 0); return 0;
}
static int test_pwm_cycles(const struct device *dev, uint32_t channel, uint64_t *cycles)
{
    ARG_UNUSED(dev); ARG_UNUSED(channel); *cycles = 1000000; return 0;
}
static DEVICE_API(pwm, test_pwm_api) = {
    .set_cycles = test_pwm_set, .get_cycles_per_sec = test_pwm_cycles,
};
DEVICE_DT_DEFINE(DT_NODELABEL(test_pwm), NULL, NULL, NULL, NULL,
                 POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &test_pwm_api);

#include <power/power.h>
static mbs_indicator_config cfg;
static void apply(void)
{
	zassert_ok(mbs_indicator_config_set(&cfg));
	k_sleep(K_MSEC(25));
}
static void battery(uint8_t soc, bool charging)
{
	struct mbs_power_fuel_gauge_data_event event = {
		.voltage_mv = 3500, .soc_percent = soc, .charging = charging,
	};
	zassert_ok(zbus_chan_pub(&mbs_power_fuel_gauge_data_chan, &event, K_MSEC(20)));
	k_sleep(K_MSEC(25));
}
static void before(void *unused)
{
	ARG_UNUSED(unused);
	zassert_ok(mbs_indicator_config_get(&cfg));
	cfg.buzzer_enabled = false;
	cfg.light_enabled = false;
	apply();
	mbs_indicator_startup_complete(true);
	battery(80, false);
	cfg.buzzer_enabled = true;
	cfg.buzzer_feedback.direct_message_enabled = true;
	cfg.buzzer_feedback.channel_message_enabled = true;
	cfg.buzzer_feedback.system_enabled = true;
	cfg.buzzer_feedback.input_enabled = true;
	apply();
	atomic_clear(&tones);
}
static void submit(enum mbs_indicator_feedback event)
{
	zassert_ok(mbs_indicator_feedback_submit(event));
	k_sleep(K_MSEC(25));
}
ZTEST(indicator_audio, test_received_types_and_merge)
{
	submit(MBS_INDICATOR_FEEDBACK_RECEIVED);
	submit(MBS_INDICATOR_FEEDBACK_RECEIVED);
	k_sleep(K_MSEC(400));
	zassert_equal(atomic_get(&tones), 3);
	submit(MBS_INDICATOR_FEEDBACK_RECEIVED_CHANNEL);
	k_sleep(K_MSEC(150));
	zassert_equal(atomic_get(&tones), 4);
}
ZTEST(indicator_audio, test_accepted_silent_ack_follows_system)
{
	cfg.buzzer_feedback.system_enabled = false;
	apply();
	submit(MBS_INDICATOR_FEEDBACK_ACCEPTED);
	submit(MBS_INDICATOR_FEEDBACK_ACK);
	zassert_equal(atomic_get(&tones), 0);
	cfg.buzzer_feedback.system_enabled = true;
	apply();
	submit(MBS_INDICATOR_FEEDBACK_ACK);
	k_sleep(K_MSEC(300));
	zassert_equal(atomic_get(&tones), 2);
}
K_SEM_DEFINE(queue_delay_done, 0, 1);
static void delay_system_queue(struct k_work *work)
{
	ARG_UNUSED(work);
	k_sleep(K_MSEC(300));
	k_sem_give(&queue_delay_done);
}
K_WORK_DEFINE(queue_delay_work, delay_system_queue);

ZTEST(indicator_audio, test_queue_delay_preserves_notes_before_next_feedback)
{
	submit(MBS_INDICATOR_FEEDBACK_RECEIVED);
	submit(MBS_INDICATOR_FEEDBACK_SYSTEM_SUCCESS);
	k_sem_reset(&queue_delay_done);
	zassert_true(k_work_submit(&queue_delay_work) >= 0);
	zassert_ok(k_sem_take(&queue_delay_done, K_SECONDS(1)));
	/* RX still needs two notes, then the four-note pairing success melody. */
	k_sleep(K_MSEC(900));
	zassert_equal(atomic_get(&tones), 7, "queue delay truncated a melody");
	zassert_equal(atomic_get(&output_period), 0);
}

ZTEST(indicator_audio, test_failure_preempts_and_drops_received_tail)
{
	submit(MBS_INDICATOR_FEEDBACK_RECEIVED);
	submit(MBS_INDICATOR_FEEDBACK_MESSAGE_FAILED);
	k_sleep(K_MSEC(600));
	zassert_equal(atomic_get(&tones), 4); /* First RX note plus three failure notes. */
	zassert_equal(atomic_get(&output_period), 0);
}
ZTEST(indicator_audio, test_unconfirmed_and_system_switch)
{
	submit(MBS_INDICATOR_FEEDBACK_UNCONFIRMED);
	zassert_within(atomic_get(&output_period), 1012, 2); /* 988 Hz */
	k_sleep(K_MSEC(700));
	zassert_equal(atomic_get(&tones), 2);
	cfg.buzzer_feedback.system_enabled = false;
	apply();
	submit(MBS_INDICATOR_FEEDBACK_MESSAGE_FAILED);
	zassert_equal(atomic_get(&tones), 2);
}
ZTEST(indicator_audio, test_input_is_independent_and_cannot_interrupt_result)
{
	static const struct indicator_buzzer_note note[] = {{1000, 30}};
	const struct indicator_buzzer_melody melody = {note, 1};
	cfg.buzzer_feedback.system_enabled = false;
	apply();
	zassert_ok(mbs_indicator_buzzer_play(INDICATOR_SOURCE_INPUT, &melody));
	k_sleep(K_MSEC(60));
	cfg.buzzer_feedback.input_enabled = false;
	apply();
	zassert_equal(mbs_indicator_buzzer_play(INDICATOR_SOURCE_INPUT, &melody), -EACCES);
	cfg.buzzer_feedback.input_enabled = true;
	cfg.buzzer_feedback.system_enabled = true;
	apply();
	submit(MBS_INDICATOR_FEEDBACK_MESSAGE_FAILED);
	zassert_equal(mbs_indicator_buzzer_play(INDICATOR_SOURCE_INPUT, &melody), -EBUSY);
}
ZTEST(indicator_audio, test_startup_is_explicit_and_category_disable_stops)
{
	k_sleep(K_MSEC(300));
	zassert_equal(atomic_get(&tones), 0);
	mbs_indicator_startup_complete(true);
	k_sleep(K_MSEC(25));
	zassert_not_equal(atomic_get(&output_period), 0);
	cfg.buzzer_feedback.system_enabled = false;
	apply();
	zassert_equal(atomic_get(&output_period), 0);
	k_sleep(K_MSEC(250));
	zassert_equal(atomic_get(&tones), 1);
}
ZTEST(indicator_audio, test_condition_cooldown_survives_flapping)
{
	battery(15, false);
	zassert_not_equal(atomic_get(&output_period), 0);
	battery(14, true);
	zassert_equal(atomic_get(&output_period), 0);
	k_sleep(K_MSEC(1100));
	atomic_clear(&tones);
	battery(14, false);
	k_sleep(K_MSEC(550));
	zassert_equal(atomic_get(&tones), 0);
	battery(80, false);
	mbs_indicator_startup_complete(false);
	k_sleep(K_MSEC(800));
	zassert_equal(atomic_get(&tones), 3);
	cfg.buzzer_feedback.system_enabled = false;
	apply();
	mbs_indicator_startup_complete(true);
	k_sleep(K_MSEC(1100));
	cfg.buzzer_feedback.system_enabled = true;
	apply();
	atomic_clear(&tones);
	mbs_indicator_startup_complete(false);
	k_sleep(K_MSEC(800));
	zassert_equal(atomic_get(&tones), 0);
}
ZTEST(indicator_audio, test_equal_priority_waits_and_master_drops_pending)
{
	submit(MBS_INDICATOR_FEEDBACK_RECEIVED);
	submit(MBS_INDICATOR_FEEDBACK_SYSTEM_SUCCESS);
	k_sleep(K_MSEC(900));
	zassert_equal(atomic_get(&tones), 7);
	zassert_equal(atomic_get(&output_period), 0);
	k_sleep(K_MSEC(1100));
	submit(MBS_INDICATOR_FEEDBACK_RECEIVED);
	submit(MBS_INDICATOR_FEEDBACK_SYSTEM_SUCCESS);
	cfg.buzzer_enabled = false;
	apply();
	zassert_equal(atomic_get(&output_period), 0);
	atomic_clear(&tones);
	cfg.buzzer_enabled = true;
	apply();
	k_sleep(K_MSEC(500));
	zassert_equal(atomic_get(&tones), 0);
}
ZTEST_SUITE(indicator_audio, NULL, NULL, before, NULL, NULL);
