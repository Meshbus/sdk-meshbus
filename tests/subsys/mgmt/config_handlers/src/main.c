/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <pb_decode.h>
#include <pb_encode.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <clock/clock.h>
#include <display/display.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/ztest.h>

#define CLOCK_GROUP meshbus_ClockMgmtGroupId_CLOCK_MGMT_GROUP_ID_MESHBUS_CLOCK
#define CLOCK_CONFIG meshbus_ClockMgmtCommandId_CLOCK_MGMT_COMMAND_ID_CONFIG
#define CLOCK_RESET meshbus_ClockMgmtCommandId_CLOCK_MGMT_COMMAND_ID_CONFIG_RESET
#define DISPLAY_GROUP meshbus_DisplayMgmtGroupId_DISPLAY_MGMT_GROUP_ID_MESHBUS_DISPLAY
#define DISPLAY_CONFIG meshbus_DisplayMgmtCommandId_DISPLAY_MGMT_COMMAND_ID_CONFIG
#define DISPLAY_RESET meshbus_DisplayMgmtCommandId_DISPLAY_MGMT_COMMAND_ID_CONFIG_RESET

/* This suite stops at the registered handler boundary, before SMP transport. */
static struct cbor_nb_reader reader;
static struct cbor_nb_writer writer;
static struct smp_streamer streamer = {.reader = &reader, .writer = &writer};
static mbs_clock_config clock_config;
static mbs_display_config display_config;
static int get_rc;
static int set_rc;
static int reset_rc;
static char calls[8];
static size_t call_count;

static void record_call(char operation)
{
	zassert_true(call_count < sizeof(calls) - 1);
	calls[call_count++] = operation;
}

int __wrap_mbs_clock_config_get(mbs_clock_config *cfg)
{
	record_call('G');
	*cfg = clock_config;
	return get_rc;
}

int __wrap_mbs_clock_config_set(const mbs_clock_config *cfg)
{
	record_call('S');
	clock_config = *cfg;
	/* A backend may normalize its stored value; SET must still echo the request. */
	clock_config.utc_offset_minutes = 0;
	return set_rc;
}

int __wrap_mbs_clock_config_reset(void)
{
	record_call('R');
	clock_config = (mbs_clock_config){.time_format = 1, .utc_offset_minutes = 60};
	return reset_rc;
}

int __wrap_mbs_display_config_get(mbs_display_config *cfg)
{
	record_call('G');
	*cfg = display_config;
	return get_rc;
}

int __wrap_mbs_display_config_set(const mbs_display_config *cfg)
{
	record_call('S');
	display_config = *cfg;
	display_config.brightness = 100;
	return set_rc;
}

int __wrap_mbs_display_config_reset(void)
{
	record_call('R');
	display_config = (mbs_display_config){.brightness = 80, .sleep_timeout = 30};
	return reset_rc;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	reader.nb = smp_packet_alloc();
	writer.nb = smp_packet_alloc();
	zassert_not_null(reader.nb);
	zassert_not_null(writer.nb);
	get_rc = 0;
	set_rc = 0;
	reset_rc = 0;
	call_count = 0;
	memset(calls, 0, sizeof(calls));
	clock_config = (mbs_clock_config){.time_format = 1, .utc_offset_minutes = -90};
	display_config = (mbs_display_config){
		.brightness = 42, .sleep_timeout = 123, .invert = true,
	};
}

static void after(void *fixture)
{
	ARG_UNUSED(fixture);
	smp_packet_free(reader.nb);
	smp_packet_free(writer.nb);
}

static void request_data(const uint8_t *data, size_t len)
{
	net_buf_reset(reader.nb);
	ZCBOR_STATE_E(zse, 3, reader.nb->data, net_buf_tailroom(reader.nb), 0);

	zassert_true(zcbor_map_start_encode(zse, 1));
	zassert_true(zcbor_tstr_put_lit(zse, "data"));
	zassert_true(zcbor_bstr_encode_ptr(zse, data, len));
	zassert_true(zcbor_map_end_encode(zse, 1));
	net_buf_add(reader.nb, zse->payload_mut - reader.nb->data);
}

static void request_proto(const pb_msgdesc_t *fields, const void *request)
{
	uint8_t data[64];
	pb_ostream_t stream = pb_ostream_from_buffer(data, sizeof(data));

	zassert_true(pb_encode(&stream, fields, request));
	request_data(data, stream.bytes_written);
}

static int invoke(uint16_t group, uint16_t command, bool write)
{
	const struct mgmt_handler *handler = mgmt_find_handler(group, command);
	mgmt_handler_fn function;
	int rc;

	zassert_not_null(handler, "service handler was not registered");
	function = write ? handler->mh_write : handler->mh_read;
	zassert_not_null(function);
	net_buf_reset(writer.nb);
	zcbor_new_encode_state(writer.zs, ARRAY_SIZE(writer.zs), writer.nb->data,
			       net_buf_tailroom(writer.nb), 0);
	zassert_true(zcbor_map_start_encode(writer.zs, 1));
	rc = function(&streamer);
	if (rc == 0) {
		zassert_true(zcbor_map_end_encode(writer.zs, 1));
		net_buf_add(writer.nb, writer.zs->payload_mut - writer.nb->data);
	}
	return rc;
}

static void response_proto(const pb_msgdesc_t *fields, void *response)
{
	struct zcbor_string data;
	ZCBOR_STATE_D(zsd, 3, writer.nb->data, writer.nb->len, 1, 0);

	zassert_true(zcbor_map_start_decode(zsd));
	zassert_true(zcbor_tstr_expect_lit(zsd, "data"));
	zassert_true(zcbor_bstr_decode(zsd, &data));
	zassert_true(zcbor_map_end_decode(zsd));
	zassert_equal(zsd->payload - writer.nb->data, writer.nb->len);
	pb_istream_t stream = pb_istream_from_buffer(data.value, data.len);

	zassert_true(pb_decode(&stream, fields, response));
	zassert_equal(stream.bytes_left, 0);
}

ZTEST(config_handlers, test_get_accepts_empty_request_and_empty_data)
{
	meshbus_ClockConfigGetResponse clock = meshbus_ClockConfigGetResponse_init_zero;
	meshbus_DisplayConfigGetResponse display = meshbus_DisplayConfigGetResponse_init_zero;

	zassert_ok(invoke(CLOCK_GROUP, CLOCK_CONFIG, false));
	response_proto(meshbus_ClockConfigGetResponse_fields, &clock);
	zassert_true(clock.has_config);
	zassert_equal(clock.config.time_format, 1);
	zassert_equal(clock.config.utc_offset_minutes, -90);
	request_data(NULL, 0);
	zassert_ok(invoke(DISPLAY_GROUP, DISPLAY_CONFIG, false));
	response_proto(meshbus_DisplayConfigGetResponse_fields, &display);
	zassert_true(display.has_config);
	zassert_equal(display.config.brightness, 42);
	zassert_equal(display.config.sleep_timeout, 123);
	zassert_true(display.config.invert);
	zassert_str_equal(calls, "GG");
}

ZTEST(config_handlers, test_clock_set_echoes_request_without_readback)
{
	meshbus_ClockConfigSetRequest request = {
		.has_config = true, .config = {.time_format = 1, .utc_offset_minutes = -330},
	};
	meshbus_ClockConfigSetResponse response = meshbus_ClockConfigSetResponse_init_zero;

	request_proto(meshbus_ClockConfigSetRequest_fields, &request);
	zassert_ok(invoke(CLOCK_GROUP, CLOCK_CONFIG, true));
	response_proto(meshbus_ClockConfigSetResponse_fields, &response);
	zassert_true(response.has_config);
	zassert_equal(response.config.time_format, request.config.time_format);
	zassert_equal(response.config.utc_offset_minutes, request.config.utc_offset_minutes);
	zassert_equal(clock_config.utc_offset_minutes, 0);
	zassert_str_equal(calls, "S");
}

ZTEST(config_handlers, test_display_set_echoes_request_without_readback)
{
	meshbus_DisplayConfigSetRequest request = {
		.has_config = true,
		.config = {.brightness = 27, .sleep_timeout = 4321, .invert = true},
	};
	meshbus_DisplayConfigSetResponse response = meshbus_DisplayConfigSetResponse_init_zero;

	request_proto(meshbus_DisplayConfigSetRequest_fields, &request);
	zassert_ok(invoke(DISPLAY_GROUP, DISPLAY_CONFIG, true));
	response_proto(meshbus_DisplayConfigSetResponse_fields, &response);
	zassert_true(response.has_config);
	zassert_equal(response.config.brightness, request.config.brightness);
	zassert_equal(response.config.sleep_timeout, request.config.sleep_timeout);
	zassert_equal(response.config.invert, request.config.invert);
	zassert_equal(display_config.brightness, 100);
	zassert_str_equal(calls, "S");
}

ZTEST(config_handlers, test_set_requires_payload_and_config_presence)
{
	/* A nonempty protobuf containing only an unknown field has no config presence. */
	const uint8_t absent_config[] = {0x10, 0x01};

	zassert_equal(invoke(CLOCK_GROUP, CLOCK_CONFIG, true), -EINVAL);
	request_data(NULL, 0);
	zassert_equal(invoke(DISPLAY_GROUP, DISPLAY_CONFIG, true), -EINVAL);
	request_data(absent_config, sizeof(absent_config));
	zassert_equal(invoke(CLOCK_GROUP, CLOCK_CONFIG, true), -EINVAL);
	zassert_equal(invoke(DISPLAY_GROUP, DISPLAY_CONFIG, true), -EINVAL);
	zassert_str_equal(calls, "");
}

ZTEST(config_handlers, test_rejects_invalid_cbor_and_protobuf_before_backend)
{
	const uint8_t wrong_data_type[] = {0xa1, 0x64, 'd', 'a', 't', 'a', 0x01};
	const uint8_t truncated_proto[] = {0x0a, 0x05, 0x01};

	net_buf_add_mem(reader.nb, wrong_data_type, sizeof(wrong_data_type));
	zassert_equal(invoke(CLOCK_GROUP, CLOCK_CONFIG, false), -EINVAL);
	zassert_equal(invoke(DISPLAY_GROUP, DISPLAY_RESET, true), -EINVAL);
	request_data(truncated_proto, sizeof(truncated_proto));
	zassert_equal(invoke(CLOCK_GROUP, CLOCK_CONFIG, true), -EINVAL);
	zassert_equal(invoke(DISPLAY_GROUP, DISPLAY_CONFIG, true), -EINVAL);
	request_data(NULL, 0);
	net_buf_add_u8(reader.nb, 0);
	zassert_equal(invoke(CLOCK_GROUP, CLOCK_RESET, true), -EINVAL);
	zassert_str_equal(calls, "");
}

ZTEST(config_handlers, test_reset_reads_result_after_reset)
{
	meshbus_ClockConfigResetResponse clock = meshbus_ClockConfigResetResponse_init_zero;
	meshbus_DisplayConfigResetResponse display = meshbus_DisplayConfigResetResponse_init_zero;

	zassert_ok(invoke(CLOCK_GROUP, CLOCK_RESET, true));
	response_proto(meshbus_ClockConfigResetResponse_fields, &clock);
	zassert_true(clock.has_config);
	zassert_equal(clock.config.time_format, 1);
	zassert_equal(clock.config.utc_offset_minutes, 60);
	request_data(NULL, 0);
	zassert_ok(invoke(DISPLAY_GROUP, DISPLAY_RESET, true));
	response_proto(meshbus_DisplayConfigResetResponse_fields, &display);
	zassert_true(display.has_config);
	zassert_equal(display.config.brightness, 80);
	zassert_equal(display.config.sleep_timeout, 30);
	zassert_false(display.config.invert);
	zassert_str_equal(calls, "RGRG");
}

ZTEST(config_handlers, test_get_preserves_backend_errno)
{
	get_rc = -EIO;
	zassert_equal(invoke(CLOCK_GROUP, CLOCK_CONFIG, false), -EIO);
	zassert_equal(invoke(DISPLAY_GROUP, DISPLAY_CONFIG, false), -EIO);
	zassert_str_equal(calls, "GG");
}

ZTEST(config_handlers, test_set_preserves_backend_errno)
{
	meshbus_DisplayConfigSetRequest request = {.has_config = true};

	request_proto(meshbus_DisplayConfigSetRequest_fields, &request);
	set_rc = -EBUSY;
	zassert_equal(invoke(DISPLAY_GROUP, DISPLAY_CONFIG, true), -EBUSY);
	zassert_str_equal(calls, "S");
}

ZTEST(config_handlers, test_reset_failure_stops_before_get)
{
	reset_rc = -EACCES;
	zassert_equal(invoke(CLOCK_GROUP, CLOCK_RESET, true), -EACCES);
	zassert_str_equal(calls, "R");
}

ZTEST(config_handlers, test_reset_preserves_readback_errno)
{
	get_rc = -ENODATA;
	zassert_equal(invoke(DISPLAY_GROUP, DISPLAY_RESET, true), -ENODATA);
	zassert_str_equal(calls, "RG");
}

ZTEST_SUITE(config_handlers, NULL, NULL, before, after, NULL);
