// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <string.h>

#include <pb_decode.h>

#include <zephyr/device.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/meshbus/contact.h>
#include <zephyr/meshbus/meshcore.h>
#include <zephyr/meshbus/notify.h>
#include <zephyr/sys/base64.h>
#include <zephyr/sys/crc.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

#define SERIAL_B64_MAX_LEN \
	(((MESHBUS_NOTIFY_PAYLOAD_MAX_LEN + 2U) / 3U) * 4U)
#define SERIAL_FRAME_FIXED_OVERHEAD \
	(1U + sizeof("MBN1 ") - 1U + 4U + 1U + 1U + 4U + 1U)
#define SERIAL_CAPTURE_MAX_LEN (SERIAL_FRAME_FIXED_OVERHEAD + SERIAL_B64_MAX_LEN + 1U)

static const struct device *const test_uart =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_uart_mcumgr));
static uint8_t serial_capture[SERIAL_CAPTURE_MAX_LEN];
static size_t serial_capture_len;

int meshbus_notify_serial_write_frame(const uint8_t *payload, size_t len);

static void fill_prefix(pb_bytes_array_t *dst, uint8_t seed)
{
	zassert_not_null(dst, "dst");
	dst->size = CONFIG_MESHBUS_CONTACT_PREFIX_BYTES;
	for (size_t i = 0; i < dst->size; i++) {
		dst->bytes[i] = (uint8_t)(seed + i);
	}
}

static void fill_public_key(pb_bytes_array_t *dst, size_t size, uint8_t seed)
{
	zassert_not_null(dst, "dst");
	dst->size = size;
	for (size_t i = 0; i < dst->size; i++) {
		dst->bytes[i] = (uint8_t)(seed + i);
	}
}

static void fill_channel_prefix(pb_bytes_array_t *dst, uint8_t seed)
{
	zassert_not_null(dst, "dst");
	dst->size = 3U;
	for (size_t i = 0; i < dst->size; i++) {
		dst->bytes[i] = (uint8_t)(seed + i);
	}
}

static int read_notify(meshbus_notify_event *event)
{
	return zbus_chan_read(&meshbus_notify_chan, event, K_NO_WAIT);
}

static int read_notify_decoded(meshbus_notify_event *event, meshbus_notify *payload)
{
	pb_istream_t stream;
	int rc;

	rc = read_notify(event);
	if (rc != 0) {
		return rc;
	}
	if (payload == NULL || event->payload_len == 0U ||
	    event->payload_len > MESHBUS_NOTIFY_PAYLOAD_MAX_LEN) {
		return -EINVAL;
	}

	*payload = (meshbus_notify)meshbus_Notify_init_zero;
	stream = pb_istream_from_buffer(event->payload, event->payload_len);
	if (!pb_decode(&stream, meshbus_Notify_fields, payload)) {
		return -EINVAL;
	}

	return 0;
}

static void serial_capture_reset(void)
{
	(void)uart_emul_flush_tx_data(test_uart);
	(void)uart_emul_flush_rx_data(test_uart);
	memset(serial_capture, 0, sizeof(serial_capture));
	serial_capture_len = 0U;
}

static void serial_capture_quiesce(void)
{
	uint8_t scratch[SERIAL_CAPTURE_MAX_LEN];

	for (int i = 0; i < 50; i++) {
		(void)uart_emul_get_tx_data(test_uart, scratch, sizeof(scratch));
		k_msleep(1);
	}
}

static bool serial_capture_wait_for_newline(void)
{
	for (int i = 0; i < 50; i++) {
		if (serial_capture_len >= sizeof(serial_capture)) {
			return false;
		}
		serial_capture_len += uart_emul_get_tx_data(
			test_uart, &serial_capture[serial_capture_len],
			sizeof(serial_capture) - serial_capture_len);
		if (serial_capture_len > 0U &&
		    serial_capture[serial_capture_len - 1U] == '\n') {
			return true;
		}
		k_msleep(1);
	}

	return false;
}

static int hex_digit(uint8_t ch)
{
	if (ch >= '0' && ch <= '9') {
		return ch - '0';
	}
	if (ch >= 'A' && ch <= 'F') {
		return ch - 'A' + 10;
	}
	if (ch >= 'a' && ch <= 'f') {
		return ch - 'a' + 10;
	}

	return -EINVAL;
}

static int parse_hex(const uint8_t *src, size_t len, uint32_t *out)
{
	uint32_t value = 0U;
	int digit;

	zassert_not_null(src, "src");
	zassert_not_null(out, "out");

	for (size_t i = 0U; i < len; i++) {
		digit = hex_digit(src[i]);
		if (digit < 0) {
			return digit;
		}
		value = (value << 4) | (uint32_t)digit;
	}

	*out = value;
	return 0;
}

ZTEST(meshbus_notify_contract, test_publish_rejects_null_and_invalid_shape)
{
	meshbus_notify payload = (meshbus_notify)meshbus_Notify_init_zero;
	int rc;

	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_NODES_CHANGED, NULL);
	zassert_equal(rc, -EINVAL, "publish(NULL) rc=%d", rc);

	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_NODE_TELEMETRY;
	fill_prefix((pb_bytes_array_t *)&payload.payload_variant.node_telemetry.public_key_prefix,
		    0x10);
	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_NODES_CHANGED, &payload);
	zassert_equal(rc, -EINVAL, "mismatched type/tag should fail rc=%d", rc);

	payload = (meshbus_notify)meshbus_Notify_init_zero;
	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_MESSAGE;
	payload.payload_variant.message.event = MESHBUS_NOTIFY_MESSAGE_EVENT_ACK;
	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_MESSAGES_CHANGED, &payload);
	zassert_equal(rc, -EINVAL, "ACK without ack_token should fail rc=%d", rc);

	payload = (meshbus_notify)meshbus_Notify_init_zero;
	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_NODE_ADVERT;
	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_NODE_ADVERT, &payload);
	zassert_equal(rc, -EINVAL, "node advert without public_key should fail rc=%d", rc);

	fill_public_key((pb_bytes_array_t *)&payload.payload_variant.node_advert.public_key,
			ARRAY_SIZE(payload.payload_variant.node_advert.public_key.bytes), 0x20);
	payload.payload_variant.node_advert.has_out_path = true;
	payload.payload_variant.node_advert.path_hash_size = 2U;
	payload.payload_variant.node_advert.out_path.size = 3U;
	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_NODE_ADVERT, &payload);
	zassert_equal(rc, -EINVAL, "node advert with partial hop should fail rc=%d", rc);

	payload = (meshbus_notify)meshbus_Notify_init_zero;
	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_NODE_DISCOVER;
	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_NODE_DISCOVER, &payload);
	zassert_equal(rc, -EINVAL, "node discover without public_key should fail rc=%d", rc);
}

ZTEST(meshbus_notify_contract, test_publish_rejects_optional_bytes_without_presence)
{
	meshbus_notify payload = (meshbus_notify)meshbus_Notify_init_zero;
	int rc;

	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_NODE;
	fill_prefix((pb_bytes_array_t *)&payload.payload_variant.node.public_key_prefix, 0x20);
	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_NODES_CHANGED, &payload);
	zassert_equal(rc, -EINVAL, "node prefix without has flag should fail rc=%d", rc);

	payload = (meshbus_notify)meshbus_Notify_init_zero;
	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_NODE;
	payload.payload_variant.node.has_public_key_prefix = true;
	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_NODES_CHANGED, &payload);
	zassert_equal(rc, -EINVAL, "empty node prefix with has flag should fail rc=%d", rc);

	payload = (meshbus_notify)meshbus_Notify_init_zero;
	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_CHANNEL;
	fill_channel_prefix(
		(pb_bytes_array_t *)&payload.payload_variant.channel.secret_prefix, 0x50);
	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_CHANNELS_CHANGED, &payload);
	zassert_equal(rc, -EINVAL, "channel prefix without has flag should fail rc=%d", rc);

	payload = (meshbus_notify)meshbus_Notify_init_zero;
	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_CHANNEL;
	payload.payload_variant.channel.has_secret_prefix = true;
	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_CHANNELS_CHANGED, &payload);
	zassert_equal(rc, -EINVAL, "empty channel prefix with has flag should fail rc=%d", rc);
}

ZTEST(meshbus_notify_contract, test_publish_accepts_nodes_changed_with_optional_prefix)
{
	meshbus_notify payload = (meshbus_notify)meshbus_Notify_init_zero;
	meshbus_notify_event got_event = MESHBUS_NOTIFY_EVENT_INIT_ZERO;
	meshbus_notify got = meshbus_Notify_init_zero;
	int rc;

	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_NODE;

	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_NODES_CHANGED, &payload);
	zassert_ok(rc, "prefix-less nodes_changed should succeed: %d", rc);

	rc = read_notify_decoded(&got_event, &got);
	zassert_ok(rc, "zbus read failed: %d", rc);
	zassert_equal(got_event.type, MESHBUS_NOTIFY_TYPE_NODES_CHANGED, "type mismatch");
	zassert_equal(got.payload_variant.node.public_key_prefix.size, 0U,
		      "prefix should stay absent");

	payload = (meshbus_notify)meshbus_Notify_init_zero;
	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_NODE;
	payload.payload_variant.node.has_public_key_prefix = true;
	fill_prefix((pb_bytes_array_t *)&payload.payload_variant.node.public_key_prefix, 0x20);

	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_NODES_CHANGED, &payload);
	zassert_ok(rc, "prefixed nodes_changed should succeed: %d", rc);

	rc = read_notify_decoded(&got_event, &got);
	zassert_ok(rc, "zbus read failed: %d", rc);
	zassert_equal(got.payload_variant.node.public_key_prefix.size,
		      CONFIG_MESHBUS_CONTACT_PREFIX_BYTES, "prefix size mismatch");
}

ZTEST(meshbus_notify_contract, test_publish_accepts_channels_changed_with_optional_prefix)
{
	meshbus_notify payload = (meshbus_notify)meshbus_Notify_init_zero;
	meshbus_notify_event got_event = MESHBUS_NOTIFY_EVENT_INIT_ZERO;
	meshbus_notify got = meshbus_Notify_init_zero;
	int rc;

	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_CHANNEL;

	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_CHANNELS_CHANGED, &payload);
	zassert_ok(rc, "prefix-less channels_changed should succeed: %d", rc);

	rc = read_notify_decoded(&got_event, &got);
	zassert_ok(rc, "zbus read failed: %d", rc);
	zassert_equal(got_event.type, MESHBUS_NOTIFY_TYPE_CHANNELS_CHANGED, "type mismatch");
	zassert_equal(got.payload_variant.channel.secret_prefix.size, 0U,
		      "prefix should stay absent");

	payload = (meshbus_notify)meshbus_Notify_init_zero;
	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_CHANNEL;
	payload.payload_variant.channel.has_secret_prefix = true;
	fill_channel_prefix(
		(pb_bytes_array_t *)&payload.payload_variant.channel.secret_prefix, 0x50);

	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_CHANNELS_CHANGED, &payload);
	zassert_ok(rc, "prefixed channels_changed should succeed: %d", rc);

	rc = read_notify_decoded(&got_event, &got);
	zassert_ok(rc, "zbus read failed: %d", rc);
	zassert_equal(got.payload_variant.channel.secret_prefix.size, 3U,
		      "prefix size mismatch");
	zassert_mem_equal(got.payload_variant.channel.secret_prefix.bytes,
			  payload.payload_variant.channel.secret_prefix.bytes, 3U,
			  "prefix mismatch");
}

ZTEST(meshbus_notify_contract, test_publish_requires_route_result_prefix)
{
	meshbus_notify payload = (meshbus_notify)meshbus_Notify_init_zero;
	meshbus_notify_event got_event = MESHBUS_NOTIFY_EVENT_INIT_ZERO;
	meshbus_notify got = meshbus_Notify_init_zero;
	pb_bytes_array_t *prefix;
	int rc;

	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_NODE_DISCOVER_PATH;
	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_NODE_DISCOVER_PATH, &payload);
	zassert_equal(rc, -EINVAL, "discover without prefix should fail rc=%d", rc);

	prefix = (pb_bytes_array_t *)
			 &payload.payload_variant.node_discover_path.public_key_prefix;
	fill_prefix(prefix, 0x30);
	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_NODE_DISCOVER_PATH, &payload);
	zassert_ok(rc, "discover with prefix should succeed: %d", rc);
	rc = read_notify_decoded(&got_event, &got);
	zassert_ok(rc, "zbus read failed: %d", rc);
	zassert_equal(got.payload_variant.node_discover_path.public_key_prefix.size,
		      CONFIG_MESHBUS_CONTACT_PREFIX_BYTES, "discover prefix size mismatch");

	payload = (meshbus_notify)meshbus_Notify_init_zero;
	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_NODE_TRACE_PATH;
	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_NODE_TRACE_PATH, &payload);
	zassert_equal(rc, -EINVAL, "trace without prefix should fail rc=%d", rc);

	prefix = (pb_bytes_array_t *)
			 &payload.payload_variant.node_trace_path.public_key_prefix;
	fill_prefix(prefix, 0x40);
	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_NODE_TRACE_PATH, &payload);
	zassert_ok(rc, "trace with prefix should succeed: %d", rc);
	rc = read_notify_decoded(&got_event, &got);
	zassert_ok(rc, "zbus read failed: %d", rc);
	zassert_equal(got.payload_variant.node_trace_path.public_key_prefix.size,
		      CONFIG_MESHBUS_CONTACT_PREFIX_BYTES, "trace prefix size mismatch");
}

ZTEST(meshbus_notify_contract, test_publish_accepts_message_recv_without_payload_details)
{
	meshbus_notify payload = (meshbus_notify)meshbus_Notify_init_zero;
	meshbus_notify_event got_event = MESHBUS_NOTIFY_EVENT_INIT_ZERO;
	meshbus_notify got = meshbus_Notify_init_zero;
	int rc;

	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_MESSAGE;
	payload.payload_variant.message.event = MESHBUS_NOTIFY_MESSAGE_EVENT_RECV;

	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_MESSAGES_CHANGED, &payload);
	zassert_ok(rc, "message RECV publish failed: %d", rc);

	rc = read_notify_decoded(&got_event, &got);
	zassert_ok(rc, "zbus read failed: %d", rc);
	zassert_equal(got.payload_variant.message.event, MESHBUS_NOTIFY_MESSAGE_EVENT_RECV,
		      "message event mismatch");
	zassert_false(got.payload_variant.message.has_ack_token,
		      "RECV should not include ack_token");
}

ZTEST(meshbus_notify_contract, test_publish_accepts_message_ack_with_ack_token_only)
{
	meshbus_notify payload = (meshbus_notify)meshbus_Notify_init_zero;
	meshbus_notify_event got_event = MESHBUS_NOTIFY_EVENT_INIT_ZERO;
	meshbus_notify got = meshbus_Notify_init_zero;
	int rc;

	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_MESSAGE;
	payload.payload_variant.message.event = MESHBUS_NOTIFY_MESSAGE_EVENT_ACK;
	payload.payload_variant.message.has_ack_token = true;
	payload.payload_variant.message.ack_token = 0x1234ULL;

	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_MESSAGES_CHANGED, &payload);
	zassert_ok(rc, "message ACK publish failed: %d", rc);

	rc = read_notify_decoded(&got_event, &got);
	zassert_ok(rc, "zbus read failed: %d", rc);
	zassert_true(got.payload_variant.message.has_ack_token,
		     "ACK should include ack_token");
	zassert_equal(got.payload_variant.message.ack_token, 0x1234ULL,
		      "ack_token mismatch");
}

ZTEST(meshbus_notify_contract, test_publish_accepts_node_advert)
{
	meshbus_notify payload = (meshbus_notify)meshbus_Notify_init_zero;
	meshbus_notify_event got_event = MESHBUS_NOTIFY_EVENT_INIT_ZERO;
	meshbus_notify got = meshbus_Notify_init_zero;
	static const uint8_t out_path[] = {0x81, 0x82, 0x83, 0x84};
	int rc;

	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_NODE_ADVERT;
	fill_public_key((pb_bytes_array_t *)&payload.payload_variant.node_advert.public_key,
			ARRAY_SIZE(payload.payload_variant.node_advert.public_key.bytes), 0x60);
	payload.payload_variant.node_advert.response_snr = -8;
	payload.payload_variant.node_advert.has_position = true;
	payload.payload_variant.node_advert.latitude = 1234567;
	payload.payload_variant.node_advert.longitude = -7654321;
	payload.payload_variant.node_advert.has_out_path = true;
	payload.payload_variant.node_advert.path_hash_size = 2U;
	payload.payload_variant.node_advert.out_path.size = sizeof(out_path);
	memcpy(payload.payload_variant.node_advert.out_path.bytes, out_path, sizeof(out_path));

	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_NODE_ADVERT, &payload);
	zassert_ok(rc, "node advert publish failed: %d", rc);

	rc = read_notify_decoded(&got_event, &got);
	zassert_ok(rc, "zbus read failed: %d", rc);
	zassert_equal(got_event.type, MESHBUS_NOTIFY_TYPE_NODE_ADVERT, "type mismatch");
	zassert_equal(got.which_payload_variant, MESHBUS_NOTIFY_TAG_NODE_ADVERT,
		      "tag mismatch");
	zassert_equal(got.payload_variant.node_advert.public_key.size,
		      ARRAY_SIZE(got.payload_variant.node_advert.public_key.bytes),
		      "public_key size mismatch");
	zassert_mem_equal(got.payload_variant.node_advert.public_key.bytes,
			  payload.payload_variant.node_advert.public_key.bytes,
			  ARRAY_SIZE(got.payload_variant.node_advert.public_key.bytes),
			  "public_key mismatch");
	zassert_equal(got.payload_variant.node_advert.response_snr, -8,
		      "response_snr mismatch");
	zassert_true(got.payload_variant.node_advert.has_position,
		     "has_position mismatch");
	zassert_equal(got.payload_variant.node_advert.latitude, 1234567,
		      "latitude mismatch");
	zassert_equal(got.payload_variant.node_advert.longitude, -7654321,
		      "longitude mismatch");
	zassert_true(got.payload_variant.node_advert.has_out_path,
		     "has_out_path mismatch");
	zassert_equal(got.payload_variant.node_advert.path_hash_size, 2U,
		      "path_hash_size mismatch");
	zassert_equal(got.payload_variant.node_advert.out_path.size, sizeof(out_path),
		      "out_path size mismatch");
	zassert_mem_equal(got.payload_variant.node_advert.out_path.bytes, out_path,
			  sizeof(out_path), "out_path mismatch");
}

ZTEST(meshbus_notify_contract, test_publish_accepts_node_discover)
{
	meshbus_notify payload = (meshbus_notify)meshbus_Notify_init_zero;
	meshbus_notify_event got_event = MESHBUS_NOTIFY_EVENT_INIT_ZERO;
	meshbus_notify got = meshbus_Notify_init_zero;
	static const uint8_t path[] = {0x31, 0x32, 0x33};
	int rc;

	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_NODE_DISCOVER;
	payload.payload_variant.node_discover.tag = 0x11223344U;
	fill_public_key((pb_bytes_array_t *)&payload.payload_variant.node_discover.public_key,
			ARRAY_SIZE(payload.payload_variant.node_discover.public_key.bytes), 0x70);
	payload.payload_variant.node_discover.role = MESHBUS_CONTACT_ROLE_SENSOR;
	payload.payload_variant.node_discover.path.size = sizeof(path);
	memcpy(payload.payload_variant.node_discover.path.bytes, path, sizeof(path));
	payload.payload_variant.node_discover.uplink_snr = -9;
	payload.payload_variant.node_discover.downlink_snr = 12;

	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_NODE_DISCOVER, &payload);
	zassert_ok(rc, "node discover publish failed: %d", rc);

	rc = read_notify_decoded(&got_event, &got);
	zassert_ok(rc, "zbus read failed: %d", rc);
	zassert_equal(got_event.type, MESHBUS_NOTIFY_TYPE_NODE_DISCOVER, "type mismatch");
	zassert_equal(got.which_payload_variant, MESHBUS_NOTIFY_TAG_NODE_DISCOVER,
		      "tag mismatch");
	zassert_equal(got.payload_variant.node_discover.tag, 0x11223344U,
		      "request tag mismatch");
	zassert_equal(got.payload_variant.node_discover.public_key.size,
		      ARRAY_SIZE(got.payload_variant.node_discover.public_key.bytes),
		      "public_key size mismatch");
	zassert_equal(got.payload_variant.node_discover.role, MESHBUS_CONTACT_ROLE_SENSOR,
		      "role mismatch");
	zassert_equal(got.payload_variant.node_discover.path.size, sizeof(path),
		      "path size mismatch");
	zassert_mem_equal(got.payload_variant.node_discover.path.bytes, path,
			  sizeof(path), "path mismatch");
	zassert_equal(got.payload_variant.node_discover.uplink_snr, -9,
		      "uplink snr mismatch");
	zassert_equal(got.payload_variant.node_discover.downlink_snr, 12,
		      "downlink snr mismatch");
}

ZTEST(meshbus_notify_contract, test_meshcore_trace_response_publishes_meshcore_notify)
{
	meshbus_meshcore_trace_response_event event = {
		.tag = 0x55667788U,
		.timestamp = 1234U,
		.state = 1U,
		.out_path_snr_count = 2U,
		.out_path_snr = { -2, 3 },
		.return_path_snr_count = 1U,
		.return_path_snr = { 4 },
	};
	meshbus_notify_event got_event = MESHBUS_NOTIFY_EVENT_INIT_ZERO;
	meshbus_notify got = meshbus_Notify_init_zero;
	int rc;

	rc = zbus_chan_pub(&meshbus_meshcore_trace_response_chan, &event, K_NO_WAIT);
	zassert_ok(rc, "meshcore trace response publish failed: %d", rc);

	rc = read_notify_decoded(&got_event, &got);
	zassert_ok(rc, "zbus read failed: %d", rc);
	zassert_equal(got_event.type, MESHBUS_NOTIFY_TYPE_MESHCORE_TRACE, "type mismatch");
	zassert_equal(got.which_payload_variant, MESHBUS_NOTIFY_TAG_MESHCORE_TRACE,
		      "tag mismatch");
	zassert_equal(got.payload_variant.meshcore_trace.tag, event.tag,
		      "request tag mismatch");
	zassert_equal(got.payload_variant.meshcore_trace.state, event.state,
		      "state mismatch");
	zassert_equal(got.payload_variant.meshcore_trace.out_path_snr_count, 2U,
		      "out snr count mismatch");
	zassert_equal(got.payload_variant.meshcore_trace.out_path_snr[0], -8,
		      "out snr[0] mismatch");
	zassert_equal(got.payload_variant.meshcore_trace.out_path_snr[1], 12,
		      "out snr[1] mismatch");
	zassert_equal(got.payload_variant.meshcore_trace.return_path_snr_count, 1U,
		      "return snr count mismatch");
	zassert_equal(got.payload_variant.meshcore_trace.return_path_snr[0], 16,
		      "return snr[0] mismatch");
}

ZTEST(meshbus_notify_contract, test_meshcore_trace_response_validator_rejects_invalid_counts)
{
	meshbus_meshcore_trace_response_event event = {
		.tag = 0x11223344U,
		.out_path_snr_count = MESHBUS_MESHCORE_PATH_MAX_LEN + 1U,
	};
	int rc;

	rc = zbus_chan_pub(&meshbus_meshcore_trace_response_chan, &event, K_NO_WAIT);
	zassert_true(rc < 0, "oversized out_path_snr_count should fail: %d", rc);

	event.out_path_snr_count = 0U;
	event.return_path_snr_count = MESHBUS_MESHCORE_PATH_MAX_LEN + 1U;
	rc = zbus_chan_pub(&meshbus_meshcore_trace_response_chan, &event, K_NO_WAIT);
	zassert_true(rc < 0, "oversized return_path_snr_count should fail: %d", rc);

	event.return_path_snr_count = 0U;
	event.tag = 0U;
	rc = zbus_chan_pub(&meshbus_meshcore_trace_response_chan, &event, K_NO_WAIT);
	zassert_true(rc < 0, "zero tag should fail: %d", rc);
}

ZTEST(meshbus_notify_contract, test_serial_sideband_publishes_notify_payload)
{
	meshbus_notify payload = (meshbus_notify)meshbus_Notify_init_zero;
	meshbus_Notify decoded = meshbus_Notify_init_zero;
	size_t payload_start = 11U;
	size_t payload_end = 0U;
	size_t payload_b64_len;
	size_t payload_len = 0U;
	uint8_t encoded_payload[MESHBUS_NOTIFY_PAYLOAD_MAX_LEN];
	pb_istream_t stream;
	uint32_t declared_len;
	uint32_t parsed_crc;
	uint16_t expected_crc;
	int rc;

	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_MESSAGE;
	payload.payload_variant.message.event = MESHBUS_NOTIFY_MESSAGE_EVENT_ACK;
	payload.payload_variant.message.has_ack_token = true;
	payload.payload_variant.message.ack_token = 0x1234ULL;

	serial_capture_quiesce();
	serial_capture_reset();

	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_MESSAGES_CHANGED, &payload);
	zassert_ok(rc, "message ACK publish failed: %d", rc);

	zassert_true(serial_capture_wait_for_newline(), "serial output timed out");
	zassert_true(serial_capture_len > payload_start,
		     "frame too short: %zu", serial_capture_len);
	zassert_equal(serial_capture[0], 0x1e, "prefix mismatch");
	zassert_mem_equal(&serial_capture[1], "MBN1", 4U, "magic mismatch");
	zassert_equal(serial_capture[5], ' ', "magic separator mismatch");
	zassert_equal(serial_capture[10], ' ', "length separator mismatch");
	zassert_equal(serial_capture[serial_capture_len - 1U], '\n',
		      "newline missing");

	rc = parse_hex(&serial_capture[6], 4U, &declared_len);
	zassert_ok(rc, "payload length parse failed: %d", rc);

	for (size_t i = payload_start; i < serial_capture_len; i++) {
		if (serial_capture[i] == ' ') {
			payload_end = i;
			break;
		}
	}
	zassert_true(payload_end > payload_start, "payload delimiter missing");
	zassert_equal(serial_capture_len - payload_end, 6U,
		      "CRC tail length mismatch");

	rc = parse_hex(&serial_capture[payload_end + 1U], 4U, &parsed_crc);
	zassert_ok(rc, "CRC parse failed: %d", rc);
	expected_crc = crc16_itu_t(0x0000, &serial_capture[1], payload_end - 1U);
	zassert_equal(parsed_crc, expected_crc, "CRC mismatch");

	payload_b64_len = payload_end - payload_start;
	rc = base64_decode(encoded_payload, sizeof(encoded_payload), &payload_len,
			   &serial_capture[payload_start], payload_b64_len);
	zassert_ok(rc, "base64 decode failed: %d", rc);
	zassert_equal(payload_len, declared_len, "declared length mismatch");

	stream = pb_istream_from_buffer(encoded_payload, payload_len);
	zassert_true(pb_decode(&stream, meshbus_Notify_fields, &decoded),
		     "protobuf decode failed");
	zassert_equal(decoded.which_payload_variant, MESHBUS_NOTIFY_TAG_MESSAGE,
		      "decoded tag mismatch");
	zassert_equal(decoded.payload_variant.message.event,
		      MESHBUS_NOTIFY_MESSAGE_EVENT_ACK, "decoded event mismatch");
	zassert_true(decoded.payload_variant.message.has_ack_token,
		     "decoded ACK token missing");
	zassert_equal(decoded.payload_variant.message.ack_token, 0x1234ULL,
		      "decoded ACK token mismatch");
}

ZTEST(meshbus_notify_contract, test_serial_frame_accepts_max_configured_payload)
{
	uint8_t payload[MESHBUS_NOTIFY_PAYLOAD_MAX_LEN];
	size_t payload_start = 11U;
	size_t payload_end = 0U;
	uint32_t declared_len;
	uint32_t parsed_crc;
	uint16_t expected_crc;
	int rc;

	for (size_t i = 0U; i < sizeof(payload); i++) {
		payload[i] = (uint8_t)i;
	}

	serial_capture_quiesce();
	serial_capture_reset();

	rc = meshbus_notify_serial_write_frame(payload, sizeof(payload));
	zassert_ok(rc, "max payload frame write failed: %d", rc);

	zassert_true(serial_capture_wait_for_newline(), "serial output timed out");
	zassert_equal(serial_capture[0], 0x1e, "prefix mismatch");
	zassert_mem_equal(&serial_capture[1], "MBN1", 4U, "magic mismatch");
	zassert_equal(serial_capture[serial_capture_len - 1U], '\n',
		      "newline missing");

	rc = parse_hex(&serial_capture[6], 4U, &declared_len);
	zassert_ok(rc, "payload length parse failed: %d", rc);
	zassert_equal(declared_len, sizeof(payload), "declared length mismatch");

	for (size_t i = payload_start; i < serial_capture_len; i++) {
		if (serial_capture[i] == ' ') {
			payload_end = i;
			break;
		}
	}
	zassert_true(payload_end > payload_start, "payload delimiter missing");

	rc = parse_hex(&serial_capture[payload_end + 1U], 4U, &parsed_crc);
	zassert_ok(rc, "CRC parse failed: %d", rc);
	expected_crc = crc16_itu_t(0x0000, &serial_capture[1], payload_end - 1U);
	zassert_equal(parsed_crc, expected_crc, "CRC mismatch");
}

ZTEST_SUITE(meshbus_notify_contract, NULL, NULL, NULL, NULL, NULL);
