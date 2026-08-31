/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/meshbus/message.h>
#include <zephyr/meshbus/time.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

#include "services/messages_cache.h"

static uint64_t time_ms = 1704067200000ULL;
static bool time_valid = true;
static int time_error;
static struct k_thread receiver_thread;
static K_THREAD_STACK_DEFINE(receiver_stack, 1536);

bool __wrap_meshbus_time_realtime_is_valid(void)
{
	return time_valid;
}

int __wrap_meshbus_time_timestamp_ms_get(uint64_t *timestamp)
{
	*timestamp = time_ms;
	return time_error;
}

static struct meshbus_message_response_event received_event(void)
{
	return (struct meshbus_message_response_event){
		.type = meshbus_MessageContent_MessageType_RECEIVE_NODE,
		.route = meshbus_MessageContent_MessageRoute_ROUTE_DIRECT,
		.target = {1, 2, 3, 4},
		.payload_len = 5U,
		.payload = {'h', 'e', 'l', 'l', 'o'},
		.sender_name = "Sender",
		.sender_timestamp = 42U,
	};
}

static void receive_message(void)
{
	struct meshbus_message_response_event event = received_event();

	zassert_ok(zbus_chan_pub(&meshbus_message_response_chan, &event, K_SECONDS(1)));
}

static struct desktop_messages_cache_entry copy_entry(uint32_t position)
{
	struct desktop_messages_cache_entry entry;

	zassert_true(desktop_messages_cache_copy_received(position, &entry));
	zassert_not_equal(entry.entry_id, 0U);
	return entry;
}

static void *cache_setup(void)
{
	zassert_equal(desktop_messages_cache_received_count(), 0U);
	zassert_equal(desktop_messages_cache_unread_received_count(), 0U);
	return NULL;
}

static void cache_before(void *fixture)
{
	ARG_UNUSED(fixture);
	time_ms = 1704067200000ULL;
	time_valid = true;
	time_error = 0;

	/* Isolate tests through normal bounded-cache eviction, not a reset hook. */
	for (size_t i = 0; i < CONFIG_MESHBUS_DESKTOP_MESSAGE_CACHE_COUNT; i++) {
		receive_message();
	}
	for (size_t i = 0; i < CONFIG_MESHBUS_DESKTOP_MESSAGE_CACHE_COUNT; i++) {
		struct desktop_messages_cache_entry entry = copy_entry(i);

		desktop_messages_cache_mark_read(entry.entry_id);
	}
	zassert_equal(desktop_messages_cache_unread_received_count(), 0U);
}

ZTEST(desktop_messages_cache, test_identical_messages_keep_distinct_ids)
{
	struct desktop_messages_cache_entry first;
	struct desktop_messages_cache_entry second;

	receive_message();
	first = copy_entry(0);
	receive_message();
	second = copy_entry(0);
	zassert_true(second.entry_id > first.entry_id);
	zassert_equal(copy_entry(1).entry_id, first.entry_id);
	zassert_mem_equal(first.message.payload.bytes, second.message.payload.bytes, 5U);
	zassert_equal(second.message.sender_timestamp, 42U);
	zassert_equal(desktop_messages_cache_unread_received_count(), 2U);
}

ZTEST(desktop_messages_cache, test_clock_changes_do_not_change_identity)
{
	struct desktop_messages_cache_entry before = copy_entry(0);
	struct desktop_messages_cache_entry forward;
	struct desktop_messages_cache_entry backward;
	struct desktop_messages_cache_entry fallback;

	time_ms += 86400000ULL;
	receive_message();
	forward = copy_entry(0);
	time_ms -= 172800000ULL;
	receive_message();
	backward = copy_entry(0);
	time_valid = false;
	time_error = -EIO;
	receive_message();
	fallback = copy_entry(0);
	zassert_true(before.entry_id < forward.entry_id);
	zassert_true(forward.entry_id < backward.entry_id);
	zassert_true(backward.entry_id < fallback.entry_id);
	zassert_true(forward.timestamp_realtime);
	zassert_true(backward.timestamp_realtime);
	zassert_false(fallback.timestamp_realtime);
	zassert_not_equal(fallback.message.timestamp, 0U);
}

ZTEST(desktop_messages_cache, test_marking_preserves_other_ids_and_is_idempotent)
{
	struct desktop_messages_cache_entry selected;
	struct desktop_messages_cache_entry unread;
	uint32_t sequence;

	receive_message();
	selected = copy_entry(0);
	receive_message();
	zassert_equal(copy_entry(1).entry_id, selected.entry_id);
	desktop_messages_cache_mark_read(selected.entry_id);
	zassert_false(copy_entry(1).unread);
	zassert_true(copy_entry(0).unread);
	sequence = desktop_messages_cache_update_seq();
	desktop_messages_cache_mark_read(selected.entry_id);
	desktop_messages_cache_mark_read(0U);
	zassert_equal(desktop_messages_cache_update_seq(), sequence);
	desktop_messages_cache_mark_unread(selected.entry_id);
	zassert_equal(desktop_messages_cache_unread_received_count(), 2U);
	zassert_true(desktop_messages_cache_copy_unread_received(1U, &unread));
	zassert_equal(unread.entry_id, selected.entry_id);
}

static void receive_until_evicted(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	for (size_t i = 0; i < CONFIG_MESHBUS_DESKTOP_MESSAGE_CACHE_COUNT; i++) {
		receive_message();
	}
}

ZTEST(desktop_messages_cache, test_eviction_cannot_redirect_an_old_ui_id)
{
	struct desktop_messages_cache_entry selected = copy_entry(0);
	uint32_t sequence;
	k_tid_t tid;

	/* The UI snapshot outlives arrivals from a different execution context. */
	tid = k_thread_create(&receiver_thread, receiver_stack,
			      K_THREAD_STACK_SIZEOF(receiver_stack), receive_until_evicted,
			      NULL, NULL, NULL, K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
	zassert_ok(k_thread_join(tid, K_SECONDS(1)));
	zassert_equal(desktop_messages_cache_received_count(),
		      CONFIG_MESHBUS_DESKTOP_MESSAGE_CACHE_COUNT);
	sequence = desktop_messages_cache_update_seq();
	desktop_messages_cache_mark_read(selected.entry_id);
	desktop_messages_cache_mark_unread(selected.entry_id);
	zassert_equal(desktop_messages_cache_update_seq(), sequence);
	zassert_equal(desktop_messages_cache_unread_received_count(),
		      CONFIG_MESHBUS_DESKTOP_MESSAGE_CACHE_COUNT);
	zassert_true(copy_entry(CONFIG_MESHBUS_DESKTOP_MESSAGE_CACHE_COUNT - 1U).entry_id >
		     selected.entry_id);
	zassert_mem_equal(selected.message.payload.bytes, "hello", 5U);
}

ZTEST(desktop_messages_cache, test_invalid_events_leave_cache_unchanged)
{
	struct meshbus_message_response_event event = received_event();
	uint32_t sequence = desktop_messages_cache_update_seq();
	uint64_t latest_id = copy_entry(0).entry_id;

	event.payload_len = 0U;
	zassert_true(zbus_chan_pub(&meshbus_message_response_chan, &event, K_SECONDS(1)) < 0);
	event = received_event();
	event.route = (meshbus_message_route)UINT8_MAX;
	zassert_true(zbus_chan_pub(&meshbus_message_response_chan, &event, K_SECONDS(1)) < 0);
	zassert_equal(desktop_messages_cache_update_seq(), sequence);
	zassert_equal(copy_entry(0).entry_id, latest_id);
	zassert_equal(desktop_messages_cache_unread_received_count(), 0U);
}

ZTEST_SUITE(desktop_messages_cache, NULL, cache_setup, cache_before, NULL, NULL);
