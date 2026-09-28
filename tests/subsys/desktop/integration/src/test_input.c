/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

/* Desktop Input-to-ZUI delivery integration coverage. */

#include "desktop_private.h"

#include <errno.h>
#include <stddef.h>

#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <input/input.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

#define MAX_SUBMIT_ATTEMPTS 8

ZBUS_CHAN_DEFINE(mbs_input_raw_event_chan, struct mbs_input_event, NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));
ZBUS_CHAN_DEFINE(mbs_input_action_chan, struct mbs_input_act_event, NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

static struct zui_desktop test_desktop;
static struct zui_input_event attempts[MAX_SUBMIT_ATTEMPTS];
static int submit_results[MAX_SUBMIT_ATTEMPTS];
static size_t attempt_count;
static size_t result_count;
static int keypad_result = -ENOTSUP;
static int32_t keypad_value;

struct zui_desktop *zui_desktop_get_instance(void)
{
	return &test_desktop;
}

int zui_desktop_submit_input(struct zui_desktop *desktop, const struct zui_input_event *event)
{
	int ret = 0;

	zassert_equal(desktop, &test_desktop, "unexpected Desktop instance");
	zassert_not_null(event, "missing input event");
	zassert_true(attempt_count < ARRAY_SIZE(attempts), "too many submit attempts");

	attempts[attempt_count] = *event;
	if (attempt_count < result_count) {
		ret = submit_results[attempt_count];
	}
	attempt_count++;

	return ret;
}

int zui_input_keypad_value_from_zephyr(uint16_t code, int32_t *out)
{
	ARG_UNUSED(code);
	if (keypad_result == 0 && out != NULL) {
		*out = keypad_value;
	}
	return keypad_result;
}

static void script_submit_results(const int *results, size_t count)
{
	zassert_true(count <= ARRAY_SIZE(submit_results), "submit script too long");
	memset(submit_results, 0, sizeof(submit_results));
	if (results != NULL && count > 0U) {
		memcpy(submit_results, results, count * sizeof(results[0]));
	}
	result_count = count;
	attempt_count = 0U;
}

static void publish_raw(uint16_t code, int32_t value)
{
	const struct mbs_input_event event = {
		.type = INPUT_EV_KEY,
		.code = code,
		.value = value,
	};

	zassert_ok(zbus_chan_pub(&mbs_input_raw_event_chan, &event, K_NO_WAIT),
		   "raw input publish failed");
}

static void publish_action(uint16_t code, uint8_t action)
{
	const struct mbs_input_act_event event = {
		.type = INPUT_EV_KEY,
		.code = code,
		.action = action,
	};

	zassert_ok(zbus_chan_pub(&mbs_input_action_chan, &event, K_NO_WAIT),
		   "action input publish failed");
}

static void publish_scroll(uint8_t action)
{
	const struct mbs_input_act_event event = {
		.type = INPUT_EV_REL,
		.action = action,
	};

	zassert_ok(zbus_chan_pub(&mbs_input_action_chan, &event, K_NO_WAIT),
		   "scroll input publish failed");
}

static void clear_pressed_state(void)
{
	static const uint16_t codes[] = {
		INPUT_BTN_BACK,
		INPUT_BTN_SELECT,
		INPUT_KEY_UP,
		INPUT_KEY_DOWN,
		INPUT_KEY_LEFT,
		INPUT_KEY_RIGHT,
	};

	script_submit_results(NULL, 0U);
	for (size_t i = 0U; i < ARRAY_SIZE(codes); i++) {
		publish_raw(codes[i], 0);
	}
	script_submit_results(NULL, 0U);
}

static void *desktop_input_setup(void)
{
	test_desktop.host = (struct zui_host *)&test_desktop;
	return NULL;
}

static void desktop_input_before(void *fixture)
{
	ARG_UNUSED(fixture);
	clear_pressed_state();
	keypad_result = -ENOTSUP;
	keypad_value = 0;
}

ZTEST(desktop_input_delivery, test_raw_press_repeat_release_order_and_sequence)
{
	script_submit_results(NULL, 0U);
	publish_raw(INPUT_KEY_UP, 1);
	publish_raw(INPUT_KEY_UP, 2);
	publish_raw(INPUT_KEY_UP, 0);

	zassert_equal(attempt_count, 3U, "raw input delivery count mismatch");
	zassert_equal(attempts[0].code, ZUI_INPUT_CODE_UP, "press code mismatch");
	zassert_equal(attempts[0].action, ZUI_INPUT_ACTION_PRESS, "missing press");
	zassert_equal(attempts[1].action, ZUI_INPUT_ACTION_CLICK, "repeat was not a click");
	zassert_equal(attempts[2].action, ZUI_INPUT_ACTION_RELEASE, "missing release");
	zassert_equal(attempts[1].sequence, attempts[0].sequence + 1U,
		      "repeat sequence was not monotonic");
	zassert_equal(attempts[2].sequence, attempts[1].sequence + 1U,
		      "release sequence was not monotonic");
	zassert_equal(desktop_input_pressed_mask_get(), 0U, "release left pressed state set");
}

ZTEST(desktop_input_delivery, test_short_and_long_actions_preserve_order)
{
	script_submit_results(NULL, 0U);
	publish_action(INPUT_BTN_BACK, INPUT_ACT_KEY_SHORT);

	zassert_equal(attempt_count, 3U, "short action delivery count mismatch");
	zassert_equal(attempts[0].code, ZUI_INPUT_CODE_BACK, "short action code mismatch");
	zassert_equal(attempts[0].action, ZUI_INPUT_ACTION_PRESS, "short press missing");
	zassert_equal(attempts[1].action, ZUI_INPUT_ACTION_CLICK, "short click missing");
	zassert_equal(attempts[2].action, ZUI_INPUT_ACTION_RELEASE, "short release missing");

	script_submit_results(NULL, 0U);
	publish_action(INPUT_BTN_SELECT, INPUT_ACT_KEY_LONG);
	zassert_equal(attempt_count, 3U, "long action delivery count mismatch");
	zassert_equal(attempts[1].action, ZUI_INPUT_ACTION_LONG_PRESS, "long action mismatch");
	zassert_equal(desktop_input_pressed_mask_get(), 0U,
		      "synthetic action left pressed state set");
}

ZTEST(desktop_input_delivery, test_keypad_action_is_single_value_event)
{
	keypad_result = 0;
	keypad_value = 7;
	script_submit_results(NULL, 0U);
	publish_action(0x1234U, INPUT_ACT_KEY_SHORT);

	zassert_equal(attempt_count, 1U, "keypad action was expanded");
	zassert_equal(attempts[0].code, ZUI_INPUT_CODE_KEYPAD, "keypad code mismatch");
	zassert_equal(attempts[0].action, ZUI_INPUT_ACTION_CLICK, "keypad action mismatch");
	zassert_equal(attempts[0].value, 7, "keypad value mismatch");
}

ZTEST(desktop_input_delivery, test_unknown_and_unpaired_events_are_ignored)
{
	const struct mbs_input_event non_key = {
		.type = INPUT_EV_REL,
		.code = 0x7777U,
		.value = 1,
	};
	const struct mbs_input_act_event bad_action = {
		.type = INPUT_EV_KEY,
		.code = INPUT_BTN_SELECT,
		.action = UINT8_MAX,
	};

	script_submit_results(NULL, 0U);
	publish_raw(0x7777U, 1);
	publish_raw(INPUT_KEY_LEFT, 0);
	zassert_ok(zbus_chan_pub(&mbs_input_raw_event_chan, &non_key, K_NO_WAIT));
	zassert_ok(zbus_chan_pub(&mbs_input_action_chan, &bad_action, K_NO_WAIT));

	zassert_equal(attempt_count, 0U, "invalid input reached Desktop");
	zassert_equal(desktop_input_pressed_mask_get(), 0U, "invalid input changed state");
}

ZTEST(desktop_input_delivery, test_failed_raw_press_does_not_commit_pressed_state)
{
	const int results[] = {-ENOSPC};
	struct desktop_input_drop_counts before;
	struct desktop_input_drop_counts after;

	desktop_input_drop_counts_get(&before);
	script_submit_results(results, ARRAY_SIZE(results));
	publish_raw(INPUT_BTN_SELECT, 1);
	desktop_input_drop_counts_get(&after);

	zassert_equal(attempt_count, 1U, "press was not submitted exactly once");
	zassert_equal(desktop_input_pressed_mask_get(), 0U,
		      "failed press changed the committed pressed state");
	zassert_equal(after.press, before.press + 1U, "press drop was not counted");
	zassert_equal(after.action, before.action, "press changed action drop count");
	zassert_equal(after.release, before.release, "press changed release drop count");
}

ZTEST(desktop_input_delivery, test_failed_raw_release_preserves_pressed_state)
{
	const uint32_t select_mask = BIT(ZUI_INPUT_CODE_SELECT);
	const int results[] = {0, -ENOSPC};
	struct desktop_input_drop_counts before;
	struct desktop_input_drop_counts after;

	desktop_input_drop_counts_get(&before);
	script_submit_results(results, ARRAY_SIZE(results));
	publish_raw(INPUT_BTN_SELECT, 1);
	publish_raw(INPUT_BTN_SELECT, 0);
	desktop_input_drop_counts_get(&after);

	zassert_equal(attempt_count, 2U, "press/release submit count mismatch");
	zassert_equal(desktop_input_pressed_mask_get(), select_mask,
		      "failed release cleared the committed pressed state");
	zassert_equal(after.release, before.release + 1U, "release drop was not counted");
}

ZTEST(desktop_input_delivery, test_later_raw_release_recovers_after_drop)
{
	const int results[] = {0, -ENOSPC, 0};
	struct desktop_input_drop_counts before;
	struct desktop_input_drop_counts after;

	desktop_input_drop_counts_get(&before);
	script_submit_results(results, ARRAY_SIZE(results));
	publish_raw(INPUT_BTN_SELECT, 1);
	publish_raw(INPUT_BTN_SELECT, 0);
	publish_raw(INPUT_BTN_SELECT, 0);
	desktop_input_drop_counts_get(&after);

	zassert_equal(attempt_count, 3U, "release recovery submit count mismatch");
	zassert_equal(attempts[1].action, ZUI_INPUT_ACTION_RELEASE,
		      "first recovery event was not release");
	zassert_equal(attempts[2].action, ZUI_INPUT_ACTION_RELEASE,
		      "second recovery event was not release");
	zassert_equal(desktop_input_pressed_mask_get(), 0U,
		      "successful later release did not recover pressed state");
	zassert_equal(after.release, before.release + 1U,
		      "release recovery counted the successful retry as dropped");
}

ZTEST(desktop_input_delivery, test_failed_synthetic_press_aborts_action_sequence)
{
	const int results[] = {-ENOSPC};
	struct desktop_input_drop_counts before;
	struct desktop_input_drop_counts after;

	desktop_input_drop_counts_get(&before);
	script_submit_results(results, ARRAY_SIZE(results));
	publish_action(INPUT_BTN_SELECT, INPUT_ACT_KEY_SHORT);
	desktop_input_drop_counts_get(&after);

	zassert_equal(attempt_count, 1U,
		      "action sequence continued after its synthetic press failed");
	zassert_equal(attempts[0].action, ZUI_INPUT_ACTION_PRESS,
		      "first synthetic event was not press");
	zassert_equal(desktop_input_pressed_mask_get(), 0U,
		      "failed synthetic press changed pressed state");
	zassert_equal(after.press, before.press + 1U,
		      "synthetic press drop was not counted");
}

ZTEST(desktop_input_delivery, test_failed_scroll_press_aborts_action_sequence)
{
	const int results[] = {-ENOSPC};

	script_submit_results(results, ARRAY_SIZE(results));
	publish_scroll(INPUT_ACT_SCROLL_CW);

	zassert_equal(attempt_count, 1U,
		      "scroll sequence continued after its synthetic press failed");
	zassert_equal(attempts[0].action, ZUI_INPUT_ACTION_PRESS,
		      "first scroll event was not press");
}

ZTEST(desktop_input_delivery, test_action_failure_still_attempts_synthetic_release)
{
	const int results[] = {0, -ENOSPC, 0};
	struct desktop_input_drop_counts before;
	struct desktop_input_drop_counts after;

	desktop_input_drop_counts_get(&before);
	script_submit_results(results, ARRAY_SIZE(results));
	publish_action(INPUT_BTN_SELECT, INPUT_ACT_KEY_SHORT);
	desktop_input_drop_counts_get(&after);

	zassert_equal(attempt_count, 3U, "synthetic release was not attempted");
	zassert_equal(attempts[0].action, ZUI_INPUT_ACTION_PRESS, "missing press");
	zassert_equal(attempts[1].action, ZUI_INPUT_ACTION_CLICK, "missing action");
	zassert_equal(attempts[2].action, ZUI_INPUT_ACTION_RELEASE, "missing release");
	zassert_equal(desktop_input_pressed_mask_get(), 0U,
		      "successful synthetic release did not close pressed state");
	zassert_equal(after.action, before.action + 1U, "action drop was not counted");
	zassert_equal(after.release, before.release,
		      "successful recovery release was counted as dropped");
}

ZTEST(desktop_input_delivery, test_scroll_action_failure_still_attempts_release)
{
	const int results[] = {0, -ENOSPC, 0};

	script_submit_results(results, ARRAY_SIZE(results));
	publish_scroll(INPUT_ACT_SCROLL_CCW);

	zassert_equal(attempt_count, 3U, "scroll release was not attempted");
	zassert_equal(attempts[0].action, ZUI_INPUT_ACTION_PRESS, "missing scroll press");
	zassert_equal(attempts[1].action, ZUI_INPUT_ACTION_CLICK, "missing scroll action");
	zassert_equal(attempts[2].action, ZUI_INPUT_ACTION_RELEASE, "missing scroll release");
}

ZTEST_SUITE(desktop_input_delivery, NULL, desktop_input_setup,
	    desktop_input_before, NULL, NULL);
