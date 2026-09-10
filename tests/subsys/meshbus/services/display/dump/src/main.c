/* Copyright (c) 2026 FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <zephyr/display/u8g2.h>
#include <zephyr/display/u8g2_snapshot.h>
#include <zephyr/meshbus/display.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

#define WIDTH 128U
#define HEIGHT 64U
#define FRAME_SIZE (WIDTH * HEIGHT / 8U)
#define GROUP meshbus_DisplayMgmtGroupId_DISPLAY_MGMT_GROUP_ID_MESHBUS_DISPLAY
#define DUMP meshbus_DisplayMgmtCommandId_DISPLAY_MGMT_COMMAND_ID_DUMP

/* Only the panel is substituted. The renderer, snapshot service, registered
 * handler and wire codecs are real. This does not exercise SMP transport.
 */
static u8g2_t renderer;
static uint16_t panel_width = WIDTH;
static bool fail_write;
static bool pause_write;
static K_SEM_DEFINE(write_started, 0, 1);
static K_SEM_DEFINE(write_continue, 0, 1);
static K_THREAD_STACK_DEFINE(render_stack, 2048);
static struct k_thread render_thread;

static int blank(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static int write_frame(const struct device *dev, uint16_t x, uint16_t y,
		       const struct display_buffer_descriptor *desc, const void *buf)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(x);
	ARG_UNUSED(desc);
	ARG_UNUSED(buf);
	if (pause_write && y == 8U) {
		k_sem_give(&write_started);
		zassert_ok(k_sem_take(&write_continue, K_SECONDS(2)));
	}
	return fail_write ? -EIO : 0;
}

static void caps(const struct device *dev, struct display_capabilities *out)
{
	ARG_UNUSED(dev);
	*out = (struct display_capabilities){
		.x_resolution = panel_width, .y_resolution = HEIGHT,
		.supported_pixel_formats = PIXEL_FORMAT_MONO01,
		.current_pixel_format = PIXEL_FORMAT_MONO01,
		.screen_info = SCREEN_INFO_MONO_VTILED,
	};
}

static int format(const struct device *dev, enum display_pixel_format fmt)
{
	ARG_UNUSED(dev);
	return fmt == PIXEL_FORMAT_MONO01 ? 0 : -ENOTSUP;
}

static DEVICE_API(display, panel_api) = {
	.blanking_on = blank, .blanking_off = blank, .write = write_frame,
	.get_capabilities = caps, .set_pixel_format = format,
};
DEVICE_DT_DEFINE(DT_NODELABEL(test_display), NULL, NULL, NULL, NULL,
		 POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY, &panel_api);

static void draw(uint8_t byte)
{
	memset(u8g2_GetBufferPtr(&renderer), byte, FRAME_SIZE);
	u8g2_SendBuffer(&renderer);
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	fail_write = false;
	pause_write = false;
	panel_width = WIDTH;
	u8g2_deinit();
	zassert_ok(u8g2_set_output_invert(false));
	zassert_ok(u8g2_init(&renderer, DEVICE_DT_GET(DT_NODELABEL(test_display)), U8G2_R0));
	draw(0xA5);
}

static void after(void *fixture)
{
	ARG_UNUSED(fixture);
	u8g2_deinit();
}

ZTEST(display_dump, test_copy_capacity_and_unsubmitted_pixels)
{
	struct u8g2_snapshot_info info;
	uint8_t bytes[FRAME_SIZE];
	memset(bytes, 0x33, sizeof(bytes));
	zassert_equal(u8g2_snapshot_copy(bytes, 1, &info), -ENOSPC);
	zassert_equal(bytes[0], 0x33);
	zassert_equal(u8g2_snapshot_copy(NULL, 1, &info), -EINVAL);
	zassert_equal(u8g2_snapshot_copy(bytes, sizeof(bytes), NULL), -EINVAL);
	zassert_ok(u8g2_snapshot_copy(NULL, 0, &info));
	zassert_equal(info.len, FRAME_SIZE);
	memset(u8g2_GetBufferPtr(&renderer), 0xCC, FRAME_SIZE);
	zassert_ok(u8g2_snapshot_copy(bytes, sizeof(bytes), &info));
	for (size_t i = 0; i < sizeof(bytes); i++) {
		zassert_equal(bytes[i], 0xA5);
	}
}

ZTEST(display_dump, test_chunks_survive_render_teardown_and_failed_capture)
{
	struct meshbus_display_dump_chunk chunk;
	zassert_ok(meshbus_display_dump_read(0, 0, 0, &chunk));
	uint32_t id = chunk.snapshot_id;
	zassert_not_equal(id, 0);
	zassert_equal(chunk.data_len, 256);
	zassert_equal(chunk.total_size, FRAME_SIZE);
	zassert_equal(chunk.width, WIDTH);
	zassert_equal(chunk.height, HEIGHT);
	zassert_equal(chunk.format, meshbus_DisplayDumpFormat_DISPLAY_DUMP_FORMAT_SSD1306_PAGE);
	draw(0xCC);
	u8g2_deinit();
	for (uint32_t offset = 0; offset < FRAME_SIZE; offset += 256) {
		zassert_ok(meshbus_display_dump_read(id, offset, 256, &chunk));
		zassert_equal(chunk.offset, offset);
		for (size_t i = 0; i < chunk.data_len; i++) {
			zassert_equal(chunk.data[i], 0xA5);
		}
	}
	zassert_equal(meshbus_display_dump_read(0, 0, 0, &chunk), -ENODEV);
	zassert_ok(meshbus_display_dump_read(id, FRAME_SIZE - 7, 256, &chunk));
	zassert_equal(chunk.data_len, 7);
}

ZTEST(display_dump, test_ids_and_argument_boundaries)
{
	struct meshbus_display_dump_chunk chunk;
	zassert_equal(meshbus_display_dump_read(0, 0, 0, NULL), -EINVAL);
	zassert_equal(meshbus_display_dump_read(0, 1, 0, &chunk), -EINVAL);
	zassert_equal(meshbus_display_dump_read(0, 0, 257, &chunk), -EINVAL);
	zassert_ok(meshbus_display_dump_read(0, 0, 1, &chunk));
	uint32_t old_id = chunk.snapshot_id;
	zassert_equal(chunk.data_len, 1);
	zassert_ok(meshbus_display_dump_read(0, 0, 0, &chunk));
	uint32_t id = chunk.snapshot_id;
	zassert_not_equal(old_id, id);
	zassert_equal(meshbus_display_dump_read(old_id, 0, 0, &chunk), -ENOENT);
	zassert_equal(meshbus_display_dump_read(id, FRAME_SIZE, 0, &chunk), -EINVAL);
	zassert_equal(meshbus_display_dump_read(id, UINT32_MAX, 0, &chunk), -EINVAL);
	zassert_equal(meshbus_display_dump_read(id, 0, UINT32_MAX, &chunk), -EINVAL);
}

ZTEST(display_dump, test_failed_frame_does_not_replace_published_frame)
{
	uint8_t bytes[FRAME_SIZE];
	struct u8g2_snapshot_info info;
	fail_write = true;
	draw(0xCC);
	zassert_ok(u8g2_snapshot_copy(bytes, sizeof(bytes), &info));
	zassert_equal(bytes[0], 0xA5);
	fail_write = false;
	draw(0xCC);
	zassert_ok(u8g2_snapshot_copy(bytes, sizeof(bytes), &info));
	zassert_equal(bytes[0], 0xCC);
}

static void render(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	draw(0xCC);
}

ZTEST(display_dump, test_capture_during_flush)
{
	uint8_t bytes[FRAME_SIZE];
	struct u8g2_snapshot_info info;
	pause_write = true;
	k_thread_create(&render_thread, render_stack, K_THREAD_STACK_SIZEOF(render_stack),
			render, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	zassert_ok(k_sem_take(&write_started, K_SECONDS(2)));
	zassert_ok(u8g2_snapshot_copy(bytes, sizeof(bytes), &info));
	for (size_t i = 0; i < sizeof(bytes); i++) {
		zassert_equal(bytes[i], 0xA5);
	}
	k_sem_give(&write_continue);
	zassert_ok(k_thread_join(&render_thread, K_SECONDS(2)));
	zassert_ok(u8g2_snapshot_copy(bytes, sizeof(bytes), &info));
	for (size_t i = 0; i < sizeof(bytes); i++) {
		zassert_equal(bytes[i], 0xCC);
	}
}

ZTEST(display_dump, test_orientation_and_inversion)
{
	struct meshbus_display_dump_chunk chunk;
	zassert_ok(u8g2_init(&renderer, DEVICE_DT_GET(DT_NODELABEL(test_display)), U8G2_R1));
	zassert_ok(u8g2_set_output_invert(true));
	draw(0x80);
	zassert_ok(meshbus_display_dump_read(0, 0, 0, &chunk));
	zassert_equal(chunk.orientation,
		      meshbus_DisplayDumpOrientation_DISPLAY_DUMP_ORIENTATION_VERTICAL_FLIP);
	zassert_true(chunk.inverted);
	zassert_equal(chunk.data[0], 0x80);
}

static int wire_call(const meshbus_DisplayDumpRequest *req, meshbus_DisplayDumpResponse *rsp,
		     bool malformed)
{
	struct cbor_nb_reader reader = {.nb = smp_packet_alloc()};
	struct cbor_nb_writer writer = {.nb = smp_packet_alloc()};
	struct smp_streamer streamer = {.reader = &reader, .writer = &writer};
	zassert_not_null(reader.nb);
	zassert_not_null(writer.nb);
	if (req != NULL || malformed) {
		uint8_t data[32] = {0xFF};
		pb_ostream_t out = pb_ostream_from_buffer(data, sizeof(data));
		if (!malformed) {
			zassert_true(pb_encode(&out, meshbus_DisplayDumpRequest_fields, req));
		}
		ZCBOR_STATE_E(enc, 3, reader.nb->data, net_buf_tailroom(reader.nb), 0);
		zassert_true(zcbor_map_start_encode(enc, 1));
		zassert_true(zcbor_tstr_put_lit(enc, "data"));
		zassert_true(zcbor_bstr_encode_ptr(enc, data, malformed ? 1 : out.bytes_written));
		zassert_true(zcbor_map_end_encode(enc, 1));
		net_buf_add(reader.nb, enc->payload_mut - reader.nb->data);
	}
	zcbor_new_encode_state(writer.zs, ARRAY_SIZE(writer.zs), writer.nb->data,
			       net_buf_tailroom(writer.nb), 0);
	zassert_true(zcbor_map_start_encode(writer.zs, 1));
	const struct mgmt_handler *handler = mgmt_find_handler(GROUP, DUMP);
	zassert_not_null(handler);
	zassert_is_null(handler->mh_write);
	int rc = handler->mh_read(&streamer);
	if (rc == 0) {
		zassert_true(zcbor_map_end_encode(writer.zs, 1));
		net_buf_add(writer.nb, writer.zs->payload_mut - writer.nb->data);
		zassert_true(writer.nb->len + 8 <= 612);
		struct zcbor_string data;
		ZCBOR_STATE_D(dec, 3, writer.nb->data, writer.nb->len, 1, 0);
		zassert_true(zcbor_map_start_decode(dec));
		zassert_true(zcbor_tstr_expect_lit(dec, "data"));
		zassert_true(zcbor_bstr_decode(dec, &data));
		zassert_true(zcbor_map_end_decode(dec));
		pb_istream_t in = pb_istream_from_buffer(data.value, data.len);
		memset(rsp, 0, sizeof(*rsp));
		zassert_true(pb_decode(&in, meshbus_DisplayDumpResponse_fields, rsp));
		zassert_equal(in.bytes_left, 0);
	}
	smp_packet_free(reader.nb);
	smp_packet_free(writer.nb);
	return rc;
}

ZTEST(display_dump, test_mcumgr_chunks_and_errors)
{
	meshbus_DisplayDumpResponse rsp;
	zassert_ok(wire_call(NULL, &rsp, false));
	zassert_equal(rsp.data.size, 256);
	zassert_equal(rsp.data.bytes[0], 0xA5);
	meshbus_DisplayDumpRequest req = {.snapshot_id = rsp.snapshot_id, .offset = 1000};
	draw(0xCC);
	zassert_ok(wire_call(&req, &rsp, false));
	zassert_equal(rsp.data.size, 24);
	zassert_equal(rsp.data.bytes[0], 0xA5);
	req.offset = FRAME_SIZE;
	zassert_equal(wire_call(&req, &rsp, false), MGMT_ERR_EINVAL);
	req.offset = 0;
	req.snapshot_id = UINT32_MAX;
	zassert_equal(wire_call(&req, &rsp, false), MGMT_ERR_ENOENT);
	zassert_equal(wire_call(NULL, &rsp, true), MGMT_ERR_EINVAL);
	u8g2_deinit();
	zassert_equal(wire_call(NULL, &rsp, false), MGMT_ERR_EBADSTATE);
}

ZTEST(display_dump, test_oversize_capture_preserves_previous_snapshot)
{
	struct meshbus_display_dump_chunk chunk;
	meshbus_DisplayDumpResponse rsp;
	zassert_ok(meshbus_display_dump_read(0, 0, 0, &chunk));
	uint32_t id = chunk.snapshot_id;
	panel_width = WIDTH * 2;
	zassert_ok(u8g2_init(&renderer, DEVICE_DT_GET(DT_NODELABEL(test_display)), U8G2_R0));
	zassert_equal(meshbus_display_dump_read(0, 0, 0, &chunk), -ENOSPC);
	zassert_equal(wire_call(NULL, &rsp, false), MGMT_ERR_EMSGSIZE);
	zassert_ok(meshbus_display_dump_read(id, 0, 0, &chunk));
	zassert_equal(chunk.data[0], 0xA5);
}

ZTEST(display_dump, test_shell_shares_source_and_preserves_frozen_id)
{
	struct meshbus_display_dump_chunk chunk;
	zassert_ok(meshbus_display_dump_read(0, 0, 0, &chunk));
	uint32_t id = chunk.snapshot_id;
	draw(0xCC);
	const struct shell *sh = shell_backend_dummy_get_ptr();
	for (int tries = 0; tries < 100 && !shell_ready(sh); tries++) {
		k_msleep(1);
	}
	zassert_true(shell_ready(sh), "dummy shell did not initialize");
	shell_backend_dummy_clear_output(sh);
	zassert_ok(shell_execute_cmd(sh, "zui dump"));
	size_t len;
	const char *output = shell_backend_dummy_get_output(sh, &len);
	zassert_true(len > 2048, "captured %u bytes: %s", (unsigned)len, output);
	zassert_not_null(strstr(output, "DUMP_DISP 128 64 BUF 1024 FMT SSD1306_PAGE ORI 0"));
	zassert_not_null(strstr(output, "DUMP cccccccc"));
	zassert_not_null(strstr(output, "DUMP_END"));
	zassert_ok(meshbus_display_dump_read(id, 0, 0, &chunk));
	zassert_equal(chunk.data[0], 0xA5);
}

ZTEST_SUITE(display_dump, NULL, NULL, before, after, NULL);
