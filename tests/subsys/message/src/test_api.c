/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <pb_decode.h>

#include <zephyr/kernel.h>
#include <channel/channel.h>
#include <message/message.h>
#include <meshcore/meshcore.h>
#include <notify/notify.h>
#include <contact/contact.h>
#include <clock/timestamp.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

#define TEST_NODE_NAME "contract_node"
#define TEST_CONTACT_NAME "contact_a"
#define TEST_CHANNEL_PAYLOAD_MAX_LEN \
	(CONFIG_MBS_MESSAGE_TX_MAX_LEN - (sizeof(TEST_NODE_NAME) - 1U) - 2U)

static const uint8_t local_public_key[MBS_CONTACT_PUBLIC_KEY_SIZE] = {
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a,
	0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
	0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
};

static const uint8_t contact_public_key[MBS_CONTACT_PUBLIC_KEY_SIZE] = {
	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa,
	0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5,
	0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
};

static const uint8_t channel_secret[MBS_CHANNEL_SECRET_DEFAULT_LEN] = {
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
	0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
};

static bool test_prefix_match(const uint8_t *prefix, size_t prefix_len,
			      const uint8_t *full, size_t full_len)
{
	if (prefix == NULL || full == NULL || prefix_len == 0U || full_len < prefix_len) {
		return false;
	}

	return memcmp(prefix, full, prefix_len) == 0;
}

int __wrap_mbs_meshcore_config_get(mbs_meshcore_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	*cfg = (mbs_meshcore_config)meshbus_MeshcoreConfig_init_zero;
	cfg->public_key.size = sizeof(local_public_key);
	memcpy(cfg->public_key.bytes, local_public_key, sizeof(local_public_key));
	(void)snprintk(cfg->name, sizeof(cfg->name), "%s", TEST_NODE_NAME);
	return 0;
}

int __wrap_mbs_contact_find_by_prefix(const uint8_t *public_key_prefix, mbs_contact *out)
{
	if (public_key_prefix == NULL || out == NULL) {
		return -EINVAL;
	}

	if (!test_prefix_match(public_key_prefix, CONFIG_MBS_CONTACT_PREFIX_BYTES,
			       contact_public_key, sizeof(contact_public_key))) {
		return -ENOENT;
	}

	*out = (mbs_contact)meshbus_Contact_init_zero;
	out->role = MBS_CONTACT_ROLE_CHAT;
	out->public_key.size = sizeof(contact_public_key);
	memcpy(out->public_key.bytes, contact_public_key, sizeof(contact_public_key));
	(void)snprintk(out->name, sizeof(out->name), "%s", TEST_CONTACT_NAME);
	return 0;
}

int __wrap_mbs_channel_get(size_t index, mbs_channel *channel)
{
	if (channel == NULL) {
		return -EINVAL;
	}
	if (index != 0U) {
		return -ENOENT;
	}

	*channel = (mbs_channel)meshbus_Channel_init_zero;
	channel->secret.size = sizeof(channel_secret);
	memcpy(channel->secret.bytes, channel_secret, sizeof(channel_secret));
	channel->hash.size = 1U;
	channel->hash.bytes[0] = 0x5a;
	(void)snprintk(channel->name, sizeof(channel->name), "contract");
	return 0;
}

K_SEM_DEFINE(node_request_sem, 0, 8);
K_SEM_DEFINE(channel_request_sem, 0, 8);
K_SEM_DEFINE(message_notify_sem, 0, 16);
K_SEM_DEFINE(message_ack_sem, 0, 16);

static struct mbs_message_send_to_node_request_event last_node_request;
static struct mbs_message_send_to_channel_request_event last_channel_request;
static mbs_notify_event last_notify;
static struct mbs_message_ack_response_event last_ack_response;

static void message_send_to_node_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_message_send_to_node_request_event *event =
		zbus_chan_const_msg(chan);

	if (chan != &mbs_message_send_to_node_request_chan || event == NULL) {
		return;
	}

	last_node_request = *event;
	k_sem_give(&node_request_sem);
}

static void message_send_to_channel_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_message_send_to_channel_request_event *event =
		zbus_chan_const_msg(chan);

	if (chan != &mbs_message_send_to_channel_request_chan || event == NULL) {
		return;
	}

	last_channel_request = *event;
	k_sem_give(&channel_request_sem);
}

ZBUS_LISTENER_DEFINE(message_send_to_node_listener, message_send_to_node_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_message_send_to_node_request_chan, message_send_to_node_listener, 0);

ZBUS_LISTENER_DEFINE(message_send_to_channel_listener, message_send_to_channel_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_message_send_to_channel_request_chan,
		  message_send_to_channel_listener, 0);

static void message_notify_listener_cb(const struct zbus_channel *chan)
{
	const mbs_notify_event *event = zbus_chan_const_msg(chan);

	if (chan != &mbs_notify_chan || event == NULL) {
		return;
	}

	last_notify = *event;
	k_sem_give(&message_notify_sem);
}

ZBUS_LISTENER_DEFINE(message_notify_listener, message_notify_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_notify_chan, message_notify_listener, 0);

static void message_ack_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_message_ack_response_event *event =
		zbus_chan_const_msg(chan);

	if (chan != &mbs_message_ack_response_chan || event == NULL) {
		return;
	}

	last_ack_response = *event;
	k_sem_give(&message_ack_sem);
}

ZBUS_LISTENER_DEFINE(message_ack_listener, message_ack_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_message_ack_response_chan, message_ack_listener, 0);

static void sem_drain(struct k_sem *sem)
{
	while (k_sem_take(sem, K_NO_WAIT) == 0) {
	}
}

static void message_notify_drain(void)
{
	memset(&last_notify, 0, sizeof(last_notify));
	sem_drain(&message_notify_sem);
}

static void message_ack_drain(void)
{
	memset(&last_ack_response, 0, sizeof(last_ack_response));
	sem_drain(&message_ack_sem);
}

static void test_realtime_reset(void)
{
	const struct timespec invalid_realtime = {
		.tv_sec = 1,
		.tv_nsec = 0,
	};

	zassert_ok(sys_clock_settime(SYS_CLOCK_REALTIME, &invalid_realtime));
}

static void message_queue_drain(void)
{
	for (;;) {
		mbs_message_content message = meshbus_MessageContent_init_zero;
		int rc = mbs_message_next(&message);

		if (rc == -ENOENT) {
			return;
		}
		zassert_ok(rc, "mbs_message_next failed while draining: %d", rc);
	}
}

static void sys_workq_drain(void)
{
	int rc = k_work_queue_drain(&k_sys_work_q, false);

	zassert_true(rc >= 0, "sys workqueue drain failed: %d", rc);
}

static int decode_notify_event(const mbs_notify_event *event, mbs_notify *payload)
{
	pb_istream_t stream;

	if (event == NULL || payload == NULL || event->payload_len == 0U ||
	    event->payload_len > MBS_NOTIFY_PAYLOAD_MAX_LEN) {
		return -EINVAL;
	}

	*payload = (mbs_notify)meshbus_Notify_init_zero;
	stream = pb_istream_from_buffer(event->payload, event->payload_len);
	if (!pb_decode(&stream, meshbus_Notify_fields, payload)) {
		return -EINVAL;
	}

	return 0;
}

static int wait_message_notify_event(
	mbs_notify_message_event event,
	mbs_notify *out, int32_t timeout_ms)
{
	int64_t deadline = k_uptime_get() + timeout_ms;

	while (k_uptime_get() <= deadline) {
		int64_t remain_ms = deadline - k_uptime_get();
		k_timeout_t timeout = remain_ms > 0 ? K_MSEC(remain_ms) : K_NO_WAIT;
		mbs_notify payload = meshbus_Notify_init_zero;
		int rc = k_sem_take(&message_notify_sem, timeout);

		if (rc != 0) {
			return rc;
		}
		if (last_notify.type != MBS_NOTIFY_TYPE_MESSAGES_CHANGED) {
			timeout = K_NO_WAIT;
			continue;
		}
		rc = decode_notify_event(&last_notify, &payload);
		if (rc != 0) {
			timeout = K_NO_WAIT;
			continue;
		}
		if (payload.payload_variant.message.event == event) {
			if (out != NULL) {
				*out = payload;
			}
			return 0;
		}
	}

	return -ETIMEDOUT;
}

static int message_publish_response(mbs_message_type type, mbs_message_route route,
				    const char *sender_name, const char *payload,
				    uint64_t sender_timestamp)
{
	struct mbs_message_response_event event = { 0 };
	size_t payload_len;

	if (sender_name == NULL || payload == NULL || sender_timestamp == 0U) {
		return -EINVAL;
	}

	payload_len = strlen(payload);
	if (payload_len == 0U || payload_len > sizeof(event.payload)) {
		return -EINVAL;
	}

	event.type = type;
	event.route = route;
	memcpy(event.target,
	       (type == meshbus_MessageContent_MessageType_RECEIVE_CHANNEL) ?
		       channel_secret :
		       contact_public_key,
	       sizeof(event.target));
	(void)snprintk(event.sender_name, sizeof(event.sender_name), "%s", sender_name);
	event.payload_len = (uint16_t)payload_len;
	memcpy(event.payload, payload, payload_len);
	event.sender_timestamp = sender_timestamp;
	event.has_rx_snr = true;
	event.rx_snr = 7.25f;

	return zbus_chan_pub(&mbs_message_response_chan, &event, K_NO_WAIT);
}

static int message_publish_ack_response(const uint8_t *target, uint8_t attempt)
{
	struct mbs_message_ack_response_event event = { 0 };

	if (target == NULL) {
		return -EINVAL;
	}

	memcpy(event.target, target, sizeof(event.target));
	event.attempt = attempt;
	return zbus_chan_pub(&mbs_message_ack_response_chan, &event, K_NO_WAIT);
}

static void message_release_pending_attempt(uint8_t attempt)
{
	mbs_notify notify = meshbus_Notify_init_zero;
	int rc;

	message_notify_drain();
	rc = message_publish_ack_response(contact_public_key, attempt);
	zassert_ok(rc, "ACK publish failed for attempt=%u: %d", (unsigned int)attempt, rc);
	rc = wait_message_notify_event(MBS_NOTIFY_MESSAGE_EVENT_ACK, &notify, 300);
	zassert_ok(rc, "ACK notify missing for attempt=%u: %d", (unsigned int)attempt, rc);
	zassert_true(notify.payload_variant.message.has_ack_token,
		     "ACK notify should include ack_token");
}

static void *suite_setup(void)
{
	test_realtime_reset();
	return NULL;
}

static void test_before(void *fixture)
{
	ARG_UNUSED(fixture);

	sys_workq_drain();
	test_realtime_reset();
	sem_drain(&node_request_sem);
	sem_drain(&channel_request_sem);
	message_queue_drain();
	message_notify_drain();
	message_ack_drain();
	memset(&last_node_request, 0, sizeof(last_node_request));
	memset(&last_channel_request, 0, sizeof(last_channel_request));
}

ZTEST(mbs_message_contract, test_message_invalid_inputs)
{
	mbs_message_content message = meshbus_MessageContent_init_zero;
	const uint8_t payload[] = "ok";
	int rc;

	rc = mbs_message_next(NULL);
	zassert_equal(rc, -EINVAL, "mbs_message_next(NULL) rc=%d", rc);

	rc = mbs_message_next(&message);
	zassert_equal(rc, -ENOENT, "mbs_message_next(empty) rc=%d", rc);

	rc = mbs_message_send_to_node(NULL, payload, sizeof(payload) - 1U, false, 1U, NULL);
	zassert_equal(rc, -EINVAL, "send_to_node(NULL prefix) rc=%d", rc);

	rc = mbs_message_send_to_node(contact_public_key, payload, 0U, false, 1U, NULL);
	zassert_equal(rc, -EINVAL, "send_to_node(zero payload) rc=%d", rc);

	rc = mbs_message_send_to_channel(CONFIG_MBS_CHANNEL_MAX_CHANNELS, payload,
					     sizeof(payload) - 1U);
	zassert_equal(rc, -ENOENT, "send_to_channel(invalid index) rc=%d", rc);
}

ZTEST(mbs_message_contract, test_node_extended_attempt_payload_boundary)
{
	uint8_t payload[160];
	uint64_t ack_token = UINT64_MAX;
	int rc;

	memset(payload, 'x', sizeof(payload));
	for (size_t len = 159U; len <= sizeof(payload); ++len) {
		rc = mbs_message_send_to_node(contact_public_key, payload, len, false, 4U,
					      &ack_token);
		zassert_equal(rc, -EMSGSIZE, "oversized extended attempt accepted: %d", rc);
		zassert_equal(ack_token, 0U, "rejected send reserved an ACK token");
		zassert_not_equal(k_sem_take(&node_request_sem, K_NO_WAIT), 0,
				  "rejected send published a request");
	}

	rc = mbs_message_send_to_node(contact_public_key, payload, 158U, false, 4U, &ack_token);
	zassert_ok(rc, "158-byte extended attempt rejected: %d", rc);
	zassert_ok(k_sem_take(&node_request_sem, K_MSEC(100)), "missing request");
	zassert_equal(last_node_request.payload_len, 158U, "payload was truncated");
	message_release_pending_attempt(4U);

	rc = mbs_message_send_to_node(contact_public_key, payload, sizeof(payload), false, 3U,
				      &ack_token);
	zassert_ok(rc, "160-byte legacy attempt rejected: %d", rc);
	zassert_ok(k_sem_take(&node_request_sem, K_MSEC(100)), "missing request");
	zassert_equal(last_node_request.payload_len, sizeof(payload), "payload was truncated");
	message_release_pending_attempt(3U);
}

ZTEST(mbs_message_contract, test_send_to_node_request_does_not_queue_local_message)
{
	uint8_t contact_prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES];
	uint64_t ack_token = 0U;
	const uint8_t attempt = 11U;
	int rc;

	memcpy(contact_prefix, contact_public_key, sizeof(contact_prefix));
	rc = mbs_message_send_to_node(contact_prefix, (const uint8_t *)"hello-node",
					  strlen("hello-node"), false, attempt, &ack_token);
	zassert_ok(rc, "send_to_node failed: %d", rc);
	zassert_true(ack_token != 0U, "ack_token should be assigned");

	rc = k_sem_take(&node_request_sem, K_MSEC(100));
	zassert_ok(rc, "node send request not published");
	zassert_equal(last_node_request.attempt, attempt, "attempt mismatch");
	zassert_equal(last_node_request.payload_len, strlen("hello-node"), "payload_len mismatch");
	zassert_mem_equal(last_node_request.payload, "hello-node", strlen("hello-node"),
			  "payload mismatch");
	rc = k_sem_take(&message_notify_sem, K_MSEC(50));
	zassert_true(rc != 0, "send_to_node should not emit notify");

	rc = mbs_message_next(&(mbs_message_content)meshbus_MessageContent_init_zero);
	zassert_equal(rc, -ENOENT, "send_to_node should not enqueue local message rc=%d", rc);

	message_release_pending_attempt(attempt);
}

ZTEST(mbs_message_contract, test_send_to_channel_request_does_not_queue_local_message)
{
	int rc = mbs_message_send_to_channel(0U, (const uint8_t *)"hello-chan",
						 strlen("hello-chan"));

	zassert_ok(rc, "send_to_channel failed: %d", rc);
	rc = k_sem_take(&channel_request_sem, K_MSEC(100));
	zassert_ok(rc, "channel send request not published");
	zassert_equal(last_channel_request.channel_index, 0U, "channel index mismatch");
	zassert_equal(last_channel_request.payload_len, strlen("hello-chan"),
		      "channel payload_len mismatch");
	zassert_mem_equal(last_channel_request.payload, "hello-chan", strlen("hello-chan"),
			  "channel payload mismatch");
	rc = k_sem_take(&message_notify_sem, K_MSEC(50));
	zassert_true(rc != 0, "send_to_channel should not emit notify");

	rc = mbs_message_next(&(mbs_message_content)meshbus_MessageContent_init_zero);
	zassert_equal(rc, -ENOENT, "send_to_channel should not enqueue local message rc=%d", rc);
}

ZTEST(mbs_message_contract, test_channel_payload_limit_accounts_for_sender_prefix)
{
	uint8_t payload[TEST_CHANNEL_PAYLOAD_MAX_LEN + 1U];
	int rc;

	memset(payload, 'x', sizeof(payload));
	rc = mbs_message_send_to_channel(0U, payload,
					     TEST_CHANNEL_PAYLOAD_MAX_LEN);
	zassert_ok(rc, "maximum channel payload rejected: %d", rc);
	zassert_ok(k_sem_take(&channel_request_sem, K_MSEC(100)),
		   "maximum channel payload was not published");
	zassert_equal(last_channel_request.payload_len,
		      TEST_CHANNEL_PAYLOAD_MAX_LEN,
		      "maximum channel payload length mismatch");

	rc = mbs_message_send_to_channel(0U, payload,
					     TEST_CHANNEL_PAYLOAD_MAX_LEN + 1U);
	zassert_equal(rc, -EMSGSIZE, "oversize channel payload rc=%d", rc);
	zassert_not_equal(k_sem_take(&channel_request_sem, K_MSEC(50)), 0,
			  "oversize channel payload must not be published");
}

ZTEST(mbs_message_contract, test_response_fifo_next_and_read_delete)
{
	const uint64_t target_unix_ms = 1893456789000ULL;
	const struct timespec target_time = {
		.tv_sec = (time_t)(target_unix_ms / MSEC_PER_SEC),
		.tv_nsec = (long)((target_unix_ms % MSEC_PER_SEC) * NSEC_PER_MSEC),
	};
	mbs_message_content first = meshbus_MessageContent_init_zero;
	mbs_message_content second = meshbus_MessageContent_init_zero;
	mbs_notify notify = meshbus_Notify_init_zero;
	int rc;

	zassert_ok(sys_clock_settime(SYS_CLOCK_REALTIME, &target_time));
	zassert_true(mbs_clock_realtime_is_valid(), "test realtime should be valid");

	rc = message_publish_response(meshbus_MessageContent_MessageType_RECEIVE_NODE,
				      meshbus_MessageContent_MessageRoute_ROUTE_DIRECT,
				      "contact-one", "first", 101U);
	zassert_ok(rc, "first response publish failed: %d", rc);
	rc = message_publish_response(meshbus_MessageContent_MessageType_RECEIVE_CHANNEL,
				      meshbus_MessageContent_MessageRoute_ROUTE_FLOOD,
				      "contact-two", "second", 202U);
	zassert_ok(rc, "second response publish failed: %d", rc);

	rc = wait_message_notify_event(MBS_NOTIFY_MESSAGE_EVENT_RECV, &notify, 300);
	zassert_ok(rc, "first RECV notify missing: %d", rc);
	zassert_false(notify.payload_variant.message.has_ack_token,
		      "RECV notify should not include ack_token");
	rc = wait_message_notify_event(MBS_NOTIFY_MESSAGE_EVENT_RECV, &notify, 300);
	zassert_ok(rc, "second RECV notify missing: %d", rc);
	zassert_false(notify.payload_variant.message.has_ack_token,
		      "second RECV notify should not include ack_token");

	rc = mbs_message_next(&first);
	zassert_ok(rc, "first mbs_message_next failed: %d", rc);
	zassert_equal(first.type, meshbus_MessageContent_MessageType_RECEIVE_NODE, "type mismatch");
	zassert_equal(first.route, meshbus_MessageContent_MessageRoute_ROUTE_DIRECT,
		      "route mismatch");
	zassert_true(strcmp(first.sender_name, "contact-one") == 0, "sender_name mismatch");
	zassert_equal(first.sender_timestamp, 101U, "sender timestamp mismatch");
	zassert_equal(first.payload.size, strlen("first"), "payload size mismatch");
	zassert_mem_equal(first.payload.bytes, "first", strlen("first"), "payload mismatch");
	zassert_true(first.timestamp >= target_unix_ms,
		     "first local timestamp should use synchronized realtime");
	zassert_true(first.timestamp < target_unix_ms + 10000U,
		     "first local timestamp drift too large");
	rc = k_sem_take(&message_notify_sem, K_MSEC(50));
	zassert_true(rc != 0, "mbs_message_next should not emit READ notify");

	rc = mbs_message_next(&second);
	zassert_ok(rc, "second mbs_message_next failed: %d", rc);
	zassert_equal(second.type, meshbus_MessageContent_MessageType_RECEIVE_CHANNEL,
		      "type mismatch");
	zassert_equal(second.route, meshbus_MessageContent_MessageRoute_ROUTE_FLOOD,
		      "route mismatch");
	zassert_true(strcmp(second.sender_name, "contact-two") == 0, "sender_name mismatch");
	zassert_equal(second.sender_timestamp, 202U, "sender timestamp mismatch");
	zassert_equal(second.payload.size, strlen("second"), "payload size mismatch");
	zassert_mem_equal(second.payload.bytes, "second", strlen("second"), "payload mismatch");
	zassert_true(second.timestamp > first.timestamp, "local timestamps should be monotonic");
	rc = k_sem_take(&message_notify_sem, K_MSEC(50));
	zassert_true(rc != 0, "mbs_message_next should not emit READ notify");

	rc = mbs_message_next(&second);
	zassert_equal(rc, -ENOENT, "queue should be empty after reads rc=%d", rc);
}

ZTEST(mbs_message_contract, test_fifo_overflow_drops_oldest_message)
{
	char payload[16];
	int rc;

	for (int i = 0; i < CONFIG_MBS_MESSAGE_MAX_STORE_COUNT + 1; i++) {
		(void)snprintk(payload, sizeof(payload), "rx-%d", i);
		rc = message_publish_response(meshbus_MessageContent_MessageType_RECEIVE_CHANNEL,
					      meshbus_MessageContent_MessageRoute_ROUTE_FLOOD,
					      "contact-rx", payload, (uint64_t)(1000 + i));
		zassert_ok(rc, "publish response #%d failed: %d", i, rc);
	}
	sys_workq_drain();

	for (int i = 1; i < CONFIG_MBS_MESSAGE_MAX_STORE_COUNT + 1; i++) {
		mbs_message_content message = meshbus_MessageContent_init_zero;

		rc = mbs_message_next(&message);
		zassert_ok(rc, "mbs_message_next #%d failed: %d", i, rc);
		(void)snprintk(payload, sizeof(payload), "rx-%d", i);
		zassert_equal(message.payload.size, strlen(payload), "payload size mismatch");
		zassert_mem_equal(message.payload.bytes, payload, strlen(payload),
				  "payload mismatch at index %d", i);
	}

	rc = mbs_message_next(&(mbs_message_content)meshbus_MessageContent_init_zero);
	zassert_equal(rc, -ENOENT, "queue should be empty after draining rc=%d", rc);
}

ZTEST(mbs_message_contract, test_message_response_event_validator_rejects_invalid_payload)
{
	struct mbs_message_response_event event = { 0 };
	int rc;

	event.type = meshbus_MessageContent_MessageType_RECEIVE_NODE;
	memcpy(event.target, contact_public_key, sizeof(event.target));
	(void)snprintk(event.sender_name, sizeof(event.sender_name), "%s", TEST_CONTACT_NAME);

	rc = zbus_chan_pub(&mbs_message_response_chan, &event, K_NO_WAIT);
	zassert_true(rc < 0, "response event should reject zero payload rc=%d", rc);
}

ZTEST(mbs_message_contract, test_message_response_event_validator_accepts_unspecified_route)
{
	mbs_message_content message = meshbus_MessageContent_init_zero;
	int rc;

	rc = message_publish_response(meshbus_MessageContent_MessageType_RECEIVE_NODE,
				      meshbus_MessageContent_MessageRoute_ROUTE_UNSPECIFIED,
				      "contact-unspecified", "route", 303U);
	zassert_ok(rc, "unspecified route response publish failed: %d", rc);
	rc = mbs_message_next(&message);
	zassert_ok(rc, "mbs_message_next failed: %d", rc);
	zassert_equal(message.type, meshbus_MessageContent_MessageType_RECEIVE_NODE,
		      "type mismatch");
	zassert_equal(message.route, meshbus_MessageContent_MessageRoute_ROUTE_UNSPECIFIED,
		      "route mismatch");
}

ZTEST(mbs_message_contract, test_message_response_event_validator_rejects_invalid_route)
{
	struct mbs_message_response_event event = { 0 };
	int rc;

	event.type = meshbus_MessageContent_MessageType_RECEIVE_NODE;
	event.route = (mbs_message_route)99;
	memcpy(event.target, contact_public_key, sizeof(event.target));
	(void)snprintk(event.sender_name, sizeof(event.sender_name), "%s", TEST_CONTACT_NAME);
	event.payload_len = strlen("bad-route");
	memcpy(event.payload, "bad-route", event.payload_len);

	rc = zbus_chan_pub(&mbs_message_response_chan, &event, K_NO_WAIT);
	zassert_true(rc < 0, "response event should reject invalid route rc=%d", rc);
}

ZTEST(mbs_message_contract, test_ack_token_assigned_and_attempt_reusable_after_ack)
{
	uint8_t contact_prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES];
	mbs_notify notify = meshbus_Notify_init_zero;
	uint64_t ack_token = 0U;
	uint64_t duplicate_ack_token = 123U;
	const uint8_t attempt = 12U;
	int rc;

	memcpy(contact_prefix, contact_public_key, sizeof(contact_prefix));

	rc = mbs_message_send_to_node(contact_prefix, (const uint8_t *)"ack-me",
					  strlen("ack-me"), false, attempt, &ack_token);
	zassert_ok(rc, "send failed: %d", rc);
	zassert_true(ack_token != 0U, "ack_token should be assigned");
	rc = k_sem_take(&node_request_sem, K_MSEC(100));
	zassert_ok(rc, "node request not published");
	zassert_equal(last_node_request.attempt, attempt, "attempt mismatch");

	rc = mbs_message_send_to_node(contact_prefix, (const uint8_t *)"dup",
					  strlen("dup"), false, attempt, &duplicate_ack_token);
	zassert_equal(rc, -EALREADY, "pending duplicate attempt should be rejected rc=%d", rc);
	zassert_equal(duplicate_ack_token, 0U, "failed send should clear ack_token");

	rc = message_publish_ack_response(contact_public_key, attempt);
	zassert_ok(rc, "ACK publish failed: %d", rc);
	rc = wait_message_notify_event(MBS_NOTIFY_MESSAGE_EVENT_ACK, &notify, 300);
	zassert_ok(rc, "ACK notify missing: %d", rc);
	zassert_true(notify.payload_variant.message.has_ack_token,
		     "ACK notify should include ack_token");
	zassert_equal(notify.payload_variant.message.ack_token, ack_token,
		      "ACK notify ack_token mismatch");
	rc = k_sem_take(&message_ack_sem, K_MSEC(100));
	zassert_ok(rc, "ACK event missing");
	zassert_equal(last_ack_response.attempt, attempt, "ACK attempt mismatch");

	rc = mbs_message_send_to_node(contact_prefix, (const uint8_t *)"reuse",
					  strlen("reuse"), false, attempt, &ack_token);
	zassert_ok(rc, "attempt should be reusable after ACK: %d", rc);
	zassert_true(ack_token != 0U, "reused send should receive ack_token");

	message_release_pending_attempt(attempt);
}

ZTEST(mbs_message_contract, test_ack_pending_table_uses_configured_capacity)
{
	uint8_t contact_prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES];
	uint64_t ack_token = 0U;
	uint64_t overflow_ack_token = 123U;
	const uint8_t high_attempt = 250U;
	int rc;

	memcpy(contact_prefix, contact_public_key, sizeof(contact_prefix));

	for (uint8_t attempt = 0U; attempt < CONFIG_MBS_MESSAGE_PENDING_SEND_COUNT;
	     attempt++) {
		ack_token = 0U;
		rc = mbs_message_send_to_node(contact_prefix, (const uint8_t *)"fill",
						  strlen("fill"), false, attempt, &ack_token);
		zassert_ok(rc, "pending table fill failed at attempt=%u: %d",
			   (unsigned int)attempt, rc);
		zassert_true(ack_token != 0U, "fill send should receive ack_token");
		rc = k_sem_take(&node_request_sem, K_MSEC(100));
		zassert_ok(rc, "node request missing at attempt=%u", (unsigned int)attempt);
	}

	rc = mbs_message_send_to_node(contact_prefix, (const uint8_t *)"full",
					  strlen("full"), false, high_attempt,
					  &overflow_ack_token);
	zassert_equal(rc, -ENOSPC, "full pending table should reject newest rc=%d", rc);
	zassert_equal(overflow_ack_token, 0U, "failed send should clear ack_token");

	message_release_pending_attempt(0U);

	ack_token = 0U;
	rc = mbs_message_send_to_node(contact_prefix, (const uint8_t *)"high",
					  strlen("high"), false, high_attempt, &ack_token);
	zassert_ok(rc, "high attempt should be accepted after free slot: %d", rc);
	zassert_true(ack_token != 0U, "high attempt send should receive ack_token");
	rc = k_sem_take(&node_request_sem, K_MSEC(100));
	zassert_ok(rc, "high attempt node request missing");
	zassert_equal(last_node_request.attempt, high_attempt, "high attempt mismatch");

	message_release_pending_attempt(high_attempt);
	for (uint8_t attempt = 1U; attempt < CONFIG_MBS_MESSAGE_PENDING_SEND_COUNT;
	     attempt++) {
		message_release_pending_attempt(attempt);
	}
}

ZTEST_SUITE(mbs_message_contract, NULL, suite_setup, test_before, NULL, NULL);
