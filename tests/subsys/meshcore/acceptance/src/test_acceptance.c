/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 FoBE Studio */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <meshcore/meshcore.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

#include "meshcore_prvi.h"

/* Integration coverage for private request-acceptance and recovery hooks. */

enum listener_mode {
	LISTENER_MODE_CAPACITY_ONE,
	LISTENER_MODE_BLOCK_FIRST,
};

static atomic_t listener_mode;
static atomic_t queue_occupied;
static atomic_t listener_active;
static atomic_t listener_max_active;
static atomic_t listener_calls;
static K_SEM_DEFINE(first_listener_entered, 0, 1);
static K_SEM_DEFINE(release_first_listener, 0, 1);

static void max_active_update(atomic_val_t active)
{
	atomic_val_t old;

	do {
		old = atomic_get(&listener_max_active);
		if (old >= active) {
			return;
		}
	} while (!atomic_cas(&listener_max_active, old, active));
}

static void request_listener_cb(const struct zbus_channel *chan)
{
	int rc = 0;

	if (atomic_get(&listener_mode) == LISTENER_MODE_CAPACITY_ONE) {
		if (!atomic_cas(&queue_occupied, 0, 1)) {
			rc = -ENOBUFS;
		}
	} else {
		atomic_val_t active = atomic_inc(&listener_active) + 1;
		atomic_val_t call = atomic_inc(&listener_calls);

		max_active_update(active);
		if (call == 0) {
			k_sem_give(&first_listener_entered);
			zassert_ok(k_sem_take(&release_first_listener,
					      K_SECONDS(1)));
		}
		(void)atomic_dec(&listener_active);
	}

	mbs_meshcore_request_acceptance_report(chan, rc);
}

ZBUS_LISTENER_DEFINE(meshcore_acceptance_raw_listener, request_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_meshcore_raw_data_send_request_chan,
		  meshcore_acceptance_raw_listener, 0);
ZBUS_LISTENER_DEFINE(meshcore_acceptance_control_listener, request_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_meshcore_control_data_send_request_chan,
		  meshcore_acceptance_control_listener, 0);

static void listener_reset(enum listener_mode mode)
{
	atomic_set(&listener_mode, mode);
	atomic_set(&queue_occupied, 0);
	atomic_set(&listener_active, 0);
	atomic_set(&listener_max_active, 0);
	atomic_set(&listener_calls, 0);
	k_sem_reset(&first_listener_entered);
	k_sem_reset(&release_first_listener);
}

ZTEST(mbs_meshcore_integration_request_acceptance,
      test_queue_full_and_recovery_are_reported)
{
	static const uint8_t payload[] = {0x01};
	uint32_t accepted_before;
	uint32_t queue_full_before;

	listener_reset(LISTENER_MODE_CAPACITY_ONE);
	accepted_before = mbs_meshcore_stats.requests_accepted;
	queue_full_before = mbs_meshcore_stats.requests_queue_full;

	zassert_ok(mbs_meshcore_raw_data_send(NULL, 0U, payload,
					 sizeof(payload)));
	zassert_equal(mbs_meshcore_raw_data_send(NULL, 0U, payload,
						    sizeof(payload)),
		      -ENOBUFS);
	atomic_set(&queue_occupied, 0);
	zassert_ok(mbs_meshcore_raw_data_send(NULL, 0U, payload,
					 sizeof(payload)));
	zassert_equal(mbs_meshcore_stats.requests_accepted,
		      accepted_before + 2U);
	zassert_equal(mbs_meshcore_stats.requests_queue_full,
		      queue_full_before + 1U);
}

ZTEST(mbs_meshcore_integration_request_acceptance,
      test_direct_zbus_drop_is_counted)
{
	struct mbs_meshcore_raw_data_send_request_event event = {
		.payload_len = 1U,
		.payload = {0x02},
	};
	uint32_t queue_full_before;

	listener_reset(LISTENER_MODE_CAPACITY_ONE);
	atomic_set(&queue_occupied, 1);
	queue_full_before = mbs_meshcore_stats.requests_queue_full;

	zassert_ok(zbus_chan_pub(&mbs_meshcore_raw_data_send_request_chan,
				&event, K_NO_WAIT));
	zassert_equal(mbs_meshcore_stats.requests_queue_full,
		      queue_full_before + 1U);
}

ZTEST(mbs_meshcore_integration_request_acceptance,
      test_missing_runtime_consumer_is_reported)
{
	static const uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE];
	static const uint8_t payload[] = {0x03};
	uint32_t no_consumer_before;

	listener_reset(LISTENER_MODE_CAPACITY_ONE);
	no_consumer_before = mbs_meshcore_stats.requests_no_consumer;

	zassert_equal(mbs_meshcore_anon_data_send(public_key, payload,
						     sizeof(payload)),
		      -ENODEV);
	zassert_equal(mbs_meshcore_stats.requests_no_consumer,
		      no_consumer_before + 1U);
}

ZTEST(mbs_meshcore_integration_request_acceptance,
      test_anonymous_receive_accepts_full_padded_payload)
{
	struct mbs_meshcore_anon_data_response_event event = {
		.route = MBS_MESHCORE_ROUTE_DIRECT,
		.public_key = {0x42U},
		.payload_len = MBS_MESHCORE_ANON_DATA_RECEIVED_MAX_LEN,
	};
	struct mbs_meshcore_anon_data_response_event response = {0};

	memset(event.payload, 0xa5, MBS_MESHCORE_ANON_DATA_PAYLOAD_MAX_LEN);
	zassert_ok(zbus_chan_pub(&mbs_meshcore_anon_data_response_chan,
				 &event, K_NO_WAIT));
	zassert_ok(zbus_chan_read(&mbs_meshcore_anon_data_response_chan,
				  &response, K_NO_WAIT));
	zassert_equal(response.route, event.route);
	zassert_equal(response.public_key[0], event.public_key[0]);
	zassert_equal(response.payload_len, sizeof(event.payload));
	zassert_mem_equal(response.payload, event.payload, sizeof(event.payload));
}

ZTEST(mbs_meshcore_integration_request_acceptance,
      test_anonymous_send_rejects_invalid_explicit_paths)
{
	static const uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE];
	static const uint8_t payload[] = {0x06};
	uint8_t overlong_path[MBS_MESHCORE_PATH_MAX_LEN] = {0};
	struct mbs_meshcore_anon_data_send_request_event event = {
		.public_key = {0x42U},
		.payload_len = 1U,
		.payload = {0x07U},
		.direct_only = true,
		.has_explicit_path = true,
		.path_byte_len = MBS_MESHCORE_PATH_MAX_LEN,
		.path_hash_size = 1U,
	};

	zassert_equal(mbs_meshcore_anon_data_send_via_path(
			      public_key, payload, sizeof(payload), overlong_path,
			      sizeof(overlong_path), 1U),
		      -EINVAL, "64 one-byte hops must be rejected");
	zassert_equal(mbs_meshcore_anon_data_send_via_path(
			      public_key, payload, sizeof(payload), NULL, 1U, 1U),
		      -EINVAL, "non-empty NULL path must be rejected");
	zassert_equal(zbus_chan_pub(&mbs_meshcore_anon_data_send_request_chan,
				      &event, K_NO_WAIT),
		      -ENOMSG, "channel validator accepted an overlong path");

	event.path_byte_len = 0U;
	event.path_hash_size = 1U;
	event.direct_only = false;
	zassert_equal(zbus_chan_pub(&mbs_meshcore_anon_data_send_request_chan,
				      &event, K_NO_WAIT),
		      -ENOMSG, "explicit path must require direct-only policy");
}

ZTEST(mbs_meshcore_integration_request_acceptance,
      test_submission_error_is_classified)
{
	static const uint8_t payload[] = {0x04};
	uint32_t submit_errors_before =
		mbs_meshcore_stats.requests_submit_errors;

	zassert_equal(mbs_meshcore_request_publish_accepted(NULL, payload),
		      -EINVAL);
	zassert_equal(mbs_meshcore_stats.requests_submit_errors,
		      submit_errors_before + 1U);
}

struct submit_thread_result {
	struct k_sem done;
	int rc;
};

static void raw_submit_thread(void *arg1, void *arg2, void *arg3)
{
	struct submit_thread_result *result = arg1;
	static const uint8_t payload[] = {0x05};

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	result->rc = mbs_meshcore_raw_data_send(NULL, 0U, payload,
					    sizeof(payload));
	k_sem_give(&result->done);
}

static void control_submit_thread(void *arg1, void *arg2, void *arg3)
{
	struct submit_thread_result *result = arg1;
	static const uint8_t payload[] = {0x80};

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	result->rc = mbs_meshcore_control_data_send(payload,
						 sizeof(payload));
	k_sem_give(&result->done);
}

K_THREAD_STACK_DEFINE(raw_submit_stack, 1024);
K_THREAD_STACK_DEFINE(control_submit_stack, 1024);
static struct k_thread raw_submit_thread_data;
static struct k_thread control_submit_thread_data;

ZTEST(mbs_meshcore_integration_request_acceptance,
      test_public_submissions_are_globally_serialized)
{
	struct submit_thread_result raw = {0};
	struct submit_thread_result control = {0};
	atomic_val_t max_active_while_blocked;

	listener_reset(LISTENER_MODE_BLOCK_FIRST);
	k_sem_init(&raw.done, 0, 1);
	k_sem_init(&control.done, 0, 1);

	k_thread_create(&raw_submit_thread_data, raw_submit_stack,
			K_THREAD_STACK_SIZEOF(raw_submit_stack),
			raw_submit_thread, &raw, NULL, NULL, K_PRIO_PREEMPT(1),
			0, K_NO_WAIT);
	zassert_ok(k_sem_take(&first_listener_entered, K_SECONDS(1)));
	k_thread_create(&control_submit_thread_data, control_submit_stack,
			K_THREAD_STACK_SIZEOF(control_submit_stack),
			control_submit_thread, &control, NULL, NULL,
			K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
	k_sleep(K_MSEC(20));
	max_active_while_blocked = atomic_get(&listener_max_active);

	k_sem_give(&release_first_listener);
	zassert_ok(k_sem_take(&raw.done, K_SECONDS(1)));
	zassert_ok(k_sem_take(&control.done, K_SECONDS(1)));
	zassert_ok(raw.rc);
	zassert_ok(control.rc);
	zassert_equal(max_active_while_blocked, 1);
	zassert_equal(atomic_get(&listener_max_active), 1);
}

ZTEST_SUITE(mbs_meshcore_integration_request_acceptance,
	    NULL, NULL, NULL, NULL, NULL);
