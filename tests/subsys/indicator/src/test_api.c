/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <string.h>

#include <pb_decode.h>

#include "meshbus/indicator.pb.h"

#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <indicator/indicator.h>
#include <input/input.h>
#include <message/message.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <zephyr/zbus/zbus.h>

#define INDICATOR_RTTTL_MAX_LENGTH 480U

BUILD_ASSERT(sizeof(((meshbus_IndicatorBuzzerRtttlRequest *)0)->rtttl) ==
		     INDICATOR_RTTTL_MAX_LENGTH + 1U,
	     "RTTTL request must reserve one byte for the string terminator");

struct indicator_buzzer_test_capture {
	uint32_t play_count;
	uint8_t last_melody_len;
	uint16_t last_first_freq_hz;
	uint16_t last_first_duration_ms;
	uint16_t last_last_freq_hz;
	uint16_t last_last_duration_ms;
};

static K_MUTEX_DEFINE(test_buzzer_capture_mutex);
static struct indicator_buzzer_test_capture test_buzzer_capture;

bool __wrap_indicator_buzzer_is_ready(void)
{
	return true;
}

int __wrap_indicator_buzzer_play(const struct indicator_buzzer_melody *melody)
{
	if (melody == NULL || melody->notes == NULL || melody->length == 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&test_buzzer_capture_mutex, K_FOREVER);
	test_buzzer_capture.play_count++;
	test_buzzer_capture.last_melody_len = melody->length;
	test_buzzer_capture.last_first_freq_hz = melody->notes[0].freq_hz;
	test_buzzer_capture.last_first_duration_ms = melody->notes[0].duration_ms;
	test_buzzer_capture.last_last_freq_hz = melody->notes[melody->length - 1U].freq_hz;
	test_buzzer_capture.last_last_duration_ms =
		melody->notes[melody->length - 1U].duration_ms;
	k_mutex_unlock(&test_buzzer_capture_mutex);

	return 0;
}

int __wrap_indicator_buzzer_play_owned(const struct indicator_buzzer_melody *melody,
				       uint32_t *token)
{
	*token = 1;
	return __wrap_indicator_buzzer_play(melody);
}

int __wrap_indicator_buzzer_play_rtttl(const char *rtttl_string)
{
	return (rtttl_string == NULL || *rtttl_string == '\0') ? -EINVAL : 0;
}

static void indicator_test_buzzer_capture_reset(void)
{
	k_mutex_lock(&test_buzzer_capture_mutex, K_FOREVER);
	memset(&test_buzzer_capture, 0, sizeof(test_buzzer_capture));
	k_mutex_unlock(&test_buzzer_capture_mutex);
}

static void indicator_test_buzzer_capture_get(struct indicator_buzzer_test_capture *capture)
{
	k_mutex_lock(&test_buzzer_capture_mutex, K_FOREVER);
	*capture = test_buzzer_capture;
	k_mutex_unlock(&test_buzzer_capture_mutex);
}

static const struct indicator_buzzer_note test_notes[] = {
	{ .freq_hz = 440, .duration_ms = 20 },
};

static mbs_indicator_config valid_indicator_config(void)
{
	mbs_indicator_config cfg = meshbus_IndicatorConfig_init_zero;

	cfg.light_enabled = true;
	cfg.buzzer_enabled = true;
	cfg.has_light_feedback = true;
	cfg.light_feedback.heartbeat_enabled = true;
	cfg.has_buzzer_feedback = true;
	cfg.buzzer_feedback.direct_message_enabled = true;
	cfg.buzzer_feedback.channel_message_enabled = true;
	cfg.buzzer_feedback.system_enabled = true;
	cfg.buzzer_feedback.input_enabled = true;
	return cfg;
}

static void *indicator_suite_setup(void)
{
	zassert_ok(mbs_indicator_config_reset());
	k_msleep(250);
	indicator_test_buzzer_capture_reset();
	return NULL;
}

static void indicator_before(void *fixture)
{
	ARG_UNUSED(fixture);
	mbs_indicator_config cfg = valid_indicator_config();
	cfg.buzzer_enabled = false;
	zassert_ok(mbs_indicator_config_set(&cfg));
	k_msleep(20);
	zassert_ok(mbs_indicator_config_reset());
	indicator_test_buzzer_capture_reset();
}

static int indicator_test_buzzer_wait_count(uint32_t expected,
					    struct indicator_buzzer_test_capture *out)
{
	for (int i = 0; i < 30; i++) {
		struct indicator_buzzer_test_capture capture;

		indicator_test_buzzer_capture_get(&capture);
		if (capture.play_count >= expected) {
			if (out != NULL) {
				*out = capture;
			}
			return 0;
		}
		k_msleep(10);
	}

	if (out != NULL) {
		indicator_test_buzzer_capture_get(out);
	}
	return -ETIMEDOUT;
}

static int indicator_publish_message_response(mbs_message_type type)
{
	struct mbs_message_response_event event = {
		.type = type,
		.route = meshbus_MessageContent_MessageRoute_ROUTE_DIRECT,
		.payload_len = 5U,
		.sender_timestamp = 123U,
	};

	memset(event.target, 0xaa, sizeof(event.target));
	memcpy(event.payload, "hello", event.payload_len);
	(void)snprintk(event.sender_name, sizeof(event.sender_name), "peer");

	return zbus_chan_pub(&mbs_message_response_chan, &event, K_MSEC(100));
}

static int indicator_publish_input_action(uint16_t code, uint8_t action)
{
	struct mbs_input_act_event event = {
		.type = INPUT_EV_KEY,
		.code = code,
		.action = action,
	};

	return zbus_chan_pub(&mbs_input_action_chan, &event, K_MSEC(100));
}

ZTEST(mbs_indicator_contract, test_config_defaults_set_get_reset_and_validation)
{
	mbs_indicator_config cfg = valid_indicator_config();
	mbs_indicator_config got;

	zassert_equal(mbs_indicator_config_get(NULL), -EINVAL);
	zassert_ok(mbs_indicator_config_get(&got));
	zassert_true(got.has_light_feedback);
	zassert_equal(got.light_feedback.heartbeat_enabled,
		      CONFIG_MBS_INDICATOR_DEFAULT_LIGHT_HEARTBEAT);
	zassert_true(got.has_buzzer_feedback);
	zassert_equal(got.buzzer_feedback.direct_message_enabled,
		      CONFIG_MBS_INDICATOR_DEFAULT_BUZZER_DIRECT_MESSAGE);
	zassert_equal(got.buzzer_feedback.channel_message_enabled,
		      CONFIG_MBS_INDICATOR_DEFAULT_BUZZER_CHANNEL_MESSAGE);
	zassert_equal(got.buzzer_feedback.system_enabled,
		      CONFIG_MBS_INDICATOR_DEFAULT_BUZZER_SYSTEM);

	zassert_ok(mbs_indicator_config_set(&cfg));
	zassert_ok(mbs_indicator_config_get(&got));
	zassert_true(got.light_enabled);
	zassert_true(got.buzzer_enabled);

	cfg.has_light_feedback = false;
	zassert_equal(mbs_indicator_config_set(&cfg), -EINVAL);

	cfg = valid_indicator_config();
	cfg.has_buzzer_feedback = false;
	zassert_equal(mbs_indicator_config_set(&cfg), -EINVAL);
}

ZTEST(mbs_indicator_contract, test_fake_buzzer_readiness_and_light_api_contract)
{
	zassert_false(mbs_indicator_light_is_ready());
	zassert_true(mbs_indicator_buzzer_is_ready());

	zassert_ok(mbs_indicator_light_idle_color(1, 2, 3));
	zassert_ok(mbs_indicator_light_idle(10, 20));
	zassert_ok(mbs_indicator_light_color(4, 5, 6));
	zassert_equal(mbs_indicator_light_play(10, 20, 1), -ENODEV);
	zassert_ok(mbs_indicator_light_stop());
}

ZTEST(mbs_indicator_contract, test_buzzer_validation_and_no_hardware_contract)
{
	mbs_indicator_config cfg = valid_indicator_config();
	struct indicator_buzzer_melody melody = {
		.notes = test_notes,
		.length = ARRAY_SIZE(test_notes),
	};

	zassert_equal(mbs_indicator_buzzer_play(INDICATOR_SOURCE_SYSTEM, NULL), -EINVAL);
	zassert_equal(mbs_indicator_buzzer_play(INDICATOR_SOURCE_SYSTEM,
						   &(struct indicator_buzzer_melody){ 0 }),
		      -EINVAL);
	zassert_equal(mbs_indicator_buzzer_rtttl(NULL), -EINVAL);
	zassert_equal(mbs_indicator_buzzer_rtttl(""), -EINVAL);

	zassert_ok(mbs_indicator_buzzer_play(INDICATOR_SOURCE_SYSTEM, &melody));
	zassert_ok(mbs_indicator_buzzer_rtttl("ok:d=4,o=5,b=120:c"));

	cfg.buzzer_feedback.system_enabled = false;
	zassert_ok(mbs_indicator_config_set(&cfg));
	zassert_equal(mbs_indicator_buzzer_play(INDICATOR_SOURCE_SYSTEM, &melody), -EACCES);
	zassert_equal(mbs_indicator_buzzer_rtttl("ok:d=4,o=5,b=120:c"), -EACCES);
	mbs_indicator_buzzer_stop();
}

ZTEST(mbs_indicator_contract, test_rtttl_protobuf_accepts_480_characters_only)
{
	uint8_t encoded[1U + 2U + INDICATOR_RTTTL_MAX_LENGTH + 1U];
	meshbus_IndicatorBuzzerRtttlRequest req =
		meshbus_IndicatorBuzzerRtttlRequest_init_zero;
	pb_istream_t stream;

	/* Field 1, length-delimited string with a two-byte protobuf length. */
	encoded[0] = 0x0a;
	encoded[1] = 0xe0;
	encoded[2] = 0x03;
	memset(&encoded[3], 'a', INDICATOR_RTTTL_MAX_LENGTH);
	stream = pb_istream_from_buffer(encoded, 3U + INDICATOR_RTTTL_MAX_LENGTH);
	zassert_true(pb_decode(&stream, meshbus_IndicatorBuzzerRtttlRequest_fields, &req),
		     "480-character RTTTL decode failed: %s", PB_GET_ERROR(&stream));
	zassert_equal(strlen(req.rtttl), INDICATOR_RTTTL_MAX_LENGTH);

	req = (meshbus_IndicatorBuzzerRtttlRequest)
		meshbus_IndicatorBuzzerRtttlRequest_init_zero;
	encoded[1] = 0xe1;
	memset(&encoded[3], 'b', INDICATOR_RTTTL_MAX_LENGTH + 1U);
	stream = pb_istream_from_buffer(encoded, sizeof(encoded));
	zassert_false(pb_decode(&stream, meshbus_IndicatorBuzzerRtttlRequest_fields, &req),
		      "481-character RTTTL should exceed the schema capacity");
}

ZTEST(mbs_indicator_contract, test_message_feedback_distinguishes_melody_with_source_policy)
{
	struct indicator_buzzer_test_capture direct_capture;
	struct indicator_buzzer_test_capture channel_capture;
	int rc;

	rc = indicator_publish_message_response(meshbus_MessageContent_MessageType_RECEIVE_NODE);
	zassert_ok(rc, "node message response publish failed: %d", rc);
	rc = indicator_test_buzzer_wait_count(1U, &direct_capture);
	zassert_ok(rc, "direct message feedback missing: %d", rc);

	indicator_test_buzzer_capture_reset();

	rc = indicator_publish_message_response(meshbus_MessageContent_MessageType_RECEIVE_CHANNEL);
	zassert_ok(rc, "channel message response publish failed: %d", rc);
	rc = indicator_test_buzzer_wait_count(1U, &channel_capture);
	zassert_ok(rc, "channel message feedback missing: %d", rc);
	zassert_equal(channel_capture.last_melody_len, 1U);
	zassert_equal(direct_capture.last_melody_len, 3U);
	zassert_equal(channel_capture.last_first_freq_hz, direct_capture.last_first_freq_hz);
	zassert_equal(channel_capture.last_first_duration_ms,
		      direct_capture.last_first_duration_ms);
}

ZTEST(mbs_indicator_contract, test_message_feedback_respects_message_category_switches)
{
	mbs_indicator_config cfg = valid_indicator_config();
	struct indicator_buzzer_test_capture capture;
	int rc;

	cfg.buzzer_feedback.channel_message_enabled = false;
	zassert_ok(mbs_indicator_config_set(&cfg));

	rc = indicator_publish_message_response(meshbus_MessageContent_MessageType_RECEIVE_CHANNEL);
	zassert_ok(rc, "channel message response publish failed: %d", rc);
	k_msleep(100);
	indicator_test_buzzer_capture_get(&capture);
	zassert_equal(capture.play_count, 0U,
		      "channel message should be filtered in direct-message-only mode");

	rc = indicator_publish_message_response(meshbus_MessageContent_MessageType_RECEIVE_NODE);
	zassert_ok(rc, "node message response publish failed: %d", rc);
	rc = indicator_test_buzzer_wait_count(1U, &capture);
	zassert_ok(rc, "direct message feedback missing: %d", rc);

	indicator_test_buzzer_capture_reset();
	cfg.buzzer_feedback.direct_message_enabled = false;
	cfg.buzzer_feedback.channel_message_enabled = true;
	zassert_ok(mbs_indicator_config_set(&cfg));

	rc = indicator_publish_message_response(meshbus_MessageContent_MessageType_RECEIVE_NODE);
	zassert_ok(rc, "node message response publish failed: %d", rc);
	k_msleep(100);
	indicator_test_buzzer_capture_get(&capture);
	zassert_equal(capture.play_count, 0U,
		      "direct message should be filtered when direct-message feedback is off");

	rc = indicator_publish_message_response(meshbus_MessageContent_MessageType_RECEIVE_CHANNEL);
	zassert_ok(rc, "channel message response publish failed: %d", rc);
	rc = indicator_test_buzzer_wait_count(1U, &capture);
	zassert_ok(rc, "channel message feedback missing: %d", rc);
}

ZTEST(mbs_indicator_contract, test_input_feedback_classifies_t9_dot_and_star)
{
	struct indicator_buzzer_test_capture capture;
	int rc;

	rc = indicator_publish_input_action(INPUT_KEY_KPDOT, INPUT_ACT_KEY_LONG);
	zassert_ok(rc, "KPDOT input action publish failed: %d", rc);
	rc = indicator_test_buzzer_wait_count(1U, &capture);
	zassert_ok(rc, "KPDOT input feedback missing: %d", rc);
	zassert_equal(capture.last_melody_len, 5U);
	zassert_equal(capture.last_first_duration_ms, 45U);
	zassert_equal(capture.last_last_freq_hz, 2000U);
	zassert_equal(capture.last_last_duration_ms, 45U);

	indicator_test_buzzer_capture_reset();

	rc = indicator_publish_input_action(INPUT_KEY_KPASTERISK, INPUT_ACT_KEY_LONG);
	zassert_ok(rc, "KPASTERISK input action publish failed: %d", rc);
	rc = indicator_test_buzzer_wait_count(1U, &capture);
	zassert_ok(rc, "KPASTERISK input feedback missing: %d", rc);
	zassert_equal(capture.last_melody_len, 5U);
	zassert_equal(capture.last_first_duration_ms, 45U);
	zassert_equal(capture.last_last_freq_hz, 1000U);
	zassert_equal(capture.last_last_duration_ms, 45U);
}

ZTEST(mbs_indicator_contract, test_system_preference_filters_all_send_result_sounds)
{
	mbs_indicator_config cfg;
	zassert_ok(mbs_indicator_config_get(&cfg));
	cfg.buzzer_feedback.system_enabled = false;
	zassert_ok(mbs_indicator_config_set(&cfg));
	struct mbs_message_send_result_event event = {.ack_token = 42};
	const enum mbs_message_send_result outcomes[] = {
		MBS_MESSAGE_SEND_ACCEPTED, MBS_MESSAGE_SEND_CONFIRMED,
		MBS_MESSAGE_SEND_UNCONFIRMED, MBS_MESSAGE_SEND_FAILED,
	};
	struct indicator_buzzer_test_capture capture;
	for (size_t i = 0; i < ARRAY_SIZE(outcomes); i++) {
		event.result = outcomes[i];
		zassert_ok(zbus_chan_pub(&mbs_message_send_result_chan, &event, K_MSEC(50)));
	}
	k_msleep(100);
	indicator_test_buzzer_capture_get(&capture);
	zassert_equal(capture.play_count, 0);
	cfg.buzzer_feedback.system_enabled = true;
	zassert_ok(mbs_indicator_config_set(&cfg));
	zassert_ok(zbus_chan_pub(&mbs_message_send_result_chan, &event, K_MSEC(50)));
	zassert_ok(indicator_test_buzzer_wait_count(1U, &capture));
	zassert_equal(capture.last_melody_len, 5);
}

ZTEST_SUITE(mbs_indicator_contract, NULL, indicator_suite_setup, indicator_before, NULL, NULL);
