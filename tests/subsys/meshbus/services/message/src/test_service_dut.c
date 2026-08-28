// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/meshbus/contact.h>
#include <zephyr/meshbus/message.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

/* Matches the Contact dependency supplied by test_api.c's linker wrapper. */
static const uint8_t test_public_key[MESHBUS_CONTACT_PUBLIC_KEY_SIZE] = {
	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
	0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
	0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
	0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
};

static K_SEM_DEFINE(node_request_sem, 0, 4);

static void node_request_listener_cb(const struct zbus_channel *chan)
{
	if (chan == &meshbus_message_send_to_node_request_chan) {
		k_sem_give(&node_request_sem);
	}
}

ZBUS_LISTENER_DEFINE(node_request_listener, node_request_listener_cb);
ZBUS_CHAN_ADD_OBS(meshbus_message_send_to_node_request_chan, node_request_listener, 0);

static void sys_workq_drain(void)
{
	int rc = k_work_queue_drain(&k_sys_work_q, false);

	zassert_true(rc >= 0, "system workqueue drain failed: %d", rc);
}

static void message_queue_drain(void)
{
	for (;;) {
		meshbus_message_content message = meshbus_MessageContent_init_zero;
		int rc = meshbus_message_next(&message);

		if (rc == -ENOENT) {
			return;
		}
		zassert_ok(rc, "message queue drain failed: %d", rc);
	}
}

static int publish_ack(uint8_t attempt)
{
	struct meshbus_message_ack_response_event event = {0};

	memcpy(event.target, test_public_key, sizeof(event.target));
	event.attempt = attempt;
	return zbus_chan_pub(&meshbus_message_ack_response_chan, &event, K_MSEC(100));
}

static void test_before(void *fixture)
{
	ARG_UNUSED(fixture);

	sys_workq_drain();
	message_queue_drain();
	while (k_sem_take(&node_request_sem, K_NO_WAIT) == 0) {
	}
}

ZTEST(meshbus_message_service_dut, test_pending_attempt_timeout_and_ack_recovery)
{
	static const uint8_t payload[] = "message-c2";
	const uint8_t attempt = 37U;
	uint64_t ack_token = 0U;
	int64_t started = k_uptime_get();
	int rc;

	rc = meshbus_message_send_to_node(test_public_key, payload, sizeof(payload) - 1U,
					  false, attempt, &ack_token);
	zassert_ok(rc, "initial node send failed: %d", rc);
	zassert_not_equal(ack_token, 0U, "initial ACK token missing");
	zassert_ok(k_sem_take(&node_request_sem, K_MSEC(100)), "initial request missing");

	rc = meshbus_message_send_to_node(test_public_key, payload, sizeof(payload) - 1U,
					  false, attempt, NULL);
	zassert_equal(rc, -EALREADY, "duplicate pending attempt accepted: %d", rc);

	k_sleep(K_MSEC(CONFIG_MESHBUS_CONTACT_REQUEST_TIMEOUT_MS + 100U));
	rc = meshbus_message_send_to_node(test_public_key, payload, sizeof(payload) - 1U,
					  false, attempt, &ack_token);
	zassert_ok(rc, "attempt was not released after timeout: %d", rc);
	zassert_ok(k_sem_take(&node_request_sem, K_MSEC(100)), "post-timeout request missing");
	zassert_true(k_uptime_get() - started >= CONFIG_MESHBUS_CONTACT_REQUEST_TIMEOUT_MS,
		     "attempt released before configured timeout");

	zassert_ok(publish_ack(attempt), "ACK publish failed");
	sys_workq_drain();
	rc = meshbus_message_send_to_node(test_public_key, payload, sizeof(payload) - 1U,
					  false, attempt, &ack_token);
	zassert_ok(rc, "attempt was not released after ACK: %d", rc);
	zassert_ok(k_sem_take(&node_request_sem, K_MSEC(100)), "post-ACK request missing");
	zassert_ok(publish_ack(attempt), "final ACK publish failed");
	sys_workq_drain();

	printk("MB_MESSAGE_SERVICE_DUT_TIMEOUT pass timeout_ms=%u ack_recovery=1\n",
	       CONFIG_MESHBUS_CONTACT_REQUEST_TIMEOUT_MS);
}

ZTEST_SUITE(meshbus_message_service_dut, NULL, NULL, test_before, NULL, NULL);
