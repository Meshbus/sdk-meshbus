/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/atomic.h>
#include <indicator/indicator.h>

static atomic_t output_period;
static int test_pwm_set(const struct device *dev, uint32_t channel,
                        uint32_t period, uint32_t pulse, pwm_flags_t flags)
{
    ARG_UNUSED(dev); ARG_UNUSED(channel); ARG_UNUSED(flags);
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

static void enable_buzzer(bool enabled)
{
    mbs_indicator_config config;
    zassert_ok(mbs_indicator_config_get(&config));
    config.buzzer_enabled = enabled;
    config.has_buzzer_feedback = true;
    config.buzzer_feedback.system_enabled = true;
    zassert_ok(mbs_indicator_config_set(&config));
}
ZTEST(indicator_playback, test_owned_lifetime_preemption_and_end)
{
    static struct indicator_buzzer_note old[] = {{440, 40}};
    static struct indicator_buzzer_note held[] = {{660, 0}};
    static struct indicator_buzzer_note sequence[] = {{440, 20}, {880, 20}};
    struct indicator_buzzer_melody melody = {old, ARRAY_SIZE(old)};
    uint32_t first, second;
    enable_buzzer(false);
    k_sleep(K_MSEC(250)); /* Let the normal startup notification be filtered. */
    enable_buzzer(true);
    zassert_true(mbs_indicator_buzzer_is_ready());
    for (unsigned i=0; i<10; ++i) {
        melody = (struct indicator_buzzer_melody){old, ARRAY_SIZE(old)};
        zassert_ok(mbs_indicator_buzzer_play_owned(INDICATOR_SOURCE_SYSTEM, &melody, &first));
        zassert_true(mbs_indicator_buzzer_playing(first));
        k_sleep(K_MSEC(10));
        melody = (struct indicator_buzzer_melody){held, ARRAY_SIZE(held)};
        zassert_ok(mbs_indicator_buzzer_play_owned(INDICATOR_SOURCE_SYSTEM, &melody, &second));
        zassert_not_equal(first, second);
        zassert_false(mbs_indicator_buzzer_playing(first));
        mbs_indicator_buzzer_stop_owned(first);
        k_sleep(K_MSEC(60)); // The old deadline must not finish a replacement held note.
        zassert_true(mbs_indicator_buzzer_playing(second));
        zassert_not_equal(atomic_get(&output_period), 0);
        mbs_indicator_buzzer_stop_owned(second);
        zassert_false(mbs_indicator_buzzer_playing(second));
        zassert_equal(atomic_get(&output_period), 0);
    }
    melody = (struct indicator_buzzer_melody){sequence, ARRAY_SIZE(sequence)};
    zassert_ok(mbs_indicator_buzzer_play_owned(INDICATOR_SOURCE_SYSTEM, &melody, &first));
    zassert_true(mbs_indicator_buzzer_playing(first));
    k_sleep(K_MSEC(100));
    zassert_false(mbs_indicator_buzzer_playing(first));
    zassert_equal(atomic_get(&output_period), 0);
    enable_buzzer(false);
    zassert_equal(mbs_indicator_buzzer_play_owned(INDICATOR_SOURCE_SYSTEM, &melody, &first), -EACCES);
    zassert_equal(first, 0);
    zassert_false(mbs_indicator_buzzer_playing(0));
}
ZTEST(indicator_playback, test_repeat_retains_token_and_preemption_stops_it)
{
    static const struct indicator_buzzer_note notes[] = {{440, 10}, {660, 10}};
    static const struct indicator_buzzer_note held[] = {{880, 0}};
    struct indicator_buzzer_melody melody = {notes, ARRAY_SIZE(notes)};
    uint32_t token, replacement;
    enable_buzzer(true);
    zassert_ok(mbs_indicator_buzzer_play_owned_repeat(INDICATOR_SOURCE_SYSTEM, &melody, &token));
    k_sleep(K_MSEC(150));
    zassert_true(mbs_indicator_buzzer_playing(token));
    melody = (struct indicator_buzzer_melody){held, ARRAY_SIZE(held)};
    zassert_equal(mbs_indicator_buzzer_play_owned_repeat(INDICATOR_SOURCE_SYSTEM, &melody, &replacement), -EINVAL);
    zassert_equal(replacement, 0);
    zassert_true(mbs_indicator_buzzer_playing(token));
    zassert_ok(mbs_indicator_buzzer_play_owned(INDICATOR_SOURCE_SYSTEM, &melody, &replacement));
    mbs_indicator_buzzer_stop_owned(token);
    k_sleep(K_MSEC(100));
    zassert_false(mbs_indicator_buzzer_playing(token));
    zassert_true(mbs_indicator_buzzer_playing(replacement));
    mbs_indicator_buzzer_stop_owned(replacement);
    zassert_equal(atomic_get(&output_period), 0);
}
ZTEST_SUITE(indicator_playback, NULL, NULL, NULL, NULL, NULL);
