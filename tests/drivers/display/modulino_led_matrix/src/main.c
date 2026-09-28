/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/drivers/display.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/ztest.h>

#define MATRIX_NODE  DT_NODELABEL(matrix)
#define GRAY_NODE    DT_NODELABEL(matrix_gray)
#define WRONG_NODE   DT_NODELABEL(matrix_wrong_id)
#define UNKNOWN_NODE DT_NODELABEL(matrix_unknown_mode)

/* Model the externally observable protocol from upstream main.c/matrix.c.
 * Do not include driver-private definitions: wire expectations live here.
 */
struct matrix_emulator {
	uint8_t identity;
	bool gray;
	bool unknown_mode;
	uint8_t frame[48];
	size_t frame_len;
	size_t command_len;
	unsigned int reads;
	unsigned int commands;
	unsigned int frames;
	int64_t ready_at;
	bool fail_read;
	bool fail_command;
	bool fail_frame;
	bool ignore_command;
	bool fail_after_command;
};

static struct matrix_emulator mono = {.identity = 0x72};
static struct matrix_emulator gray = {.identity = 0x72, .gray = true};
static struct matrix_emulator wrong = {.identity = 0x7c};
static struct matrix_emulator unknown = {.identity = 0x72, .unknown_mode = true};
static const struct device *const matrix = DEVICE_DT_GET(MATRIX_NODE);

static int matrix_transfer(const struct emul *target, struct i2c_msg *msgs, int count, int addr)
{
	struct matrix_emulator *state = target->data;
	struct i2c_msg *msg = &msgs[0];

	if (count != 1 || (msg->flags & I2C_MSG_STOP) == 0 || addr != target->bus.i2c->addr ||
	    k_uptime_get() < state->ready_at) {
		return -EIO;
	}
	if (msg->flags & I2C_MSG_READ) {
		state->reads++;
		if (state->fail_read || msg->len != 4) {
			return -EIO;
		}
		msg->buf[0] = state->identity;
		memcpy(msg->buf + 1, state->unknown_mode ? "???" : state->gray ? "GS4" : "MON", 3);
		return 0;
	}

	if (msg->len != (state->gray ? 48 : 12)) {
		return -EMSGSIZE;
	}
	/* These must never be sent by a display driver. */
	zassert_not_equal(memcmp(msg->buf, "CF", 2), 0, "flash configuration command emitted");
	zassert_not_equal(memcmp(msg->buf, "DIE", 3), 0, "bootloader command emitted");

	if (memcmp(msg->buf, "MON", 3) == 0 || memcmp(msg->buf, "GS4", 3) == 0) {
		state->commands++;
		state->command_len = msg->len;
		for (size_t i = 3; i < msg->len; i++) {
			zassert_equal(msg->buf[i], 0, "mode command must be zero-padded");
		}
		if (state->fail_command) {
			return -EIO;
		}
		if (!state->ignore_command) {
			state->gray = msg->buf[0] == 'G';
		}
		state->fail_read = state->fail_after_command;
	} else {
		if (state->fail_frame) {
			return -EIO;
		}
		memcpy(state->frame, msg->buf, msg->len);
		state->frame_len = msg->len;
		state->frames++;
	}
	state->ready_at = k_uptime_get() + 1;
	return 0;
}

static int matrix_emul_init(const struct emul *target, const struct device *parent)
{
	ARG_UNUSED(target);
	ARG_UNUSED(parent);
	return 0;
}

static const struct i2c_emul_api matrix_emul_api = {.transfer = matrix_transfer};
EMUL_DT_DEFINE(MATRIX_NODE, matrix_emul_init, &mono, NULL, &matrix_emul_api, NULL);
EMUL_DT_DEFINE(GRAY_NODE, matrix_emul_init, &gray, NULL, &matrix_emul_api, NULL);
EMUL_DT_DEFINE(WRONG_NODE, matrix_emul_init, &wrong, NULL, &matrix_emul_api, NULL);
EMUL_DT_DEFINE(UNKNOWN_NODE, matrix_emul_init, &unknown, NULL, &matrix_emul_api, NULL);

static const struct display_buffer_descriptor full_gray = {
	.width = 12,
	.height = 8,
	.pitch = 12,
	.buf_size = 48,
};

static const struct display_buffer_descriptor full = {
	.width = 12,
	.height = 8,
	.pitch = 12,
	.buf_size = 12,
};

static void *matrix_setup(void)
{
	static const uint8_t black[12];

	zassert_true(device_is_ready(matrix));
	zassert_true(device_is_ready(DEVICE_DT_GET(GRAY_NODE)));
	zassert_false(device_is_ready(DEVICE_DT_GET(WRONG_NODE)));
	zassert_false(device_is_ready(DEVICE_DT_GET(UNKNOWN_NODE)));
	zassert_equal(wrong.frames + wrong.commands, 0);
	zassert_equal(unknown.frames + unknown.commands, 0);
	zassert_equal(mono.commands, 0);
	zassert_equal(mono.frame_len, 12);
	zassert_mem_equal(mono.frame, black, 12);
	zassert_equal(gray.commands, 1);
	zassert_equal(gray.command_len, 48, "warm-start GS4 must use 48-byte MON command");
	zassert_false(gray.gray);
	zassert_equal(gray.frame_len, 12);
	return NULL;
}

static void matrix_before(void *fixture)
{
	static const uint8_t black[48];

	ARG_UNUSED(fixture);
	mono.fail_read = false;
	mono.fail_command = false;
	mono.fail_frame = false;
	mono.ignore_command = false;
	mono.fail_after_command = false;
	mono.unknown_mode = false;
	mono.identity = 0x72;
	zassert_ok(display_blanking_on(matrix));
	zassert_ok(display_write(matrix, 0, 0, &full_gray, black));
	zassert_ok(display_set_pixel_format(matrix, PIXEL_FORMAT_MONO01));
	zassert_ok(display_blanking_off(matrix));
	mono.reads = 0;
	mono.commands = 0;
	mono.frames = 0;
}

ZTEST(matrix, test_capabilities)
{
	struct display_capabilities caps;

	display_get_capabilities(matrix, &caps);
	zassert_equal(caps.x_resolution, 12);
	zassert_equal(caps.y_resolution, 8);
	zassert_equal(caps.supported_pixel_formats, PIXEL_FORMAT_MONO01 | PIXEL_FORMAT_L_4);
	zassert_equal(caps.current_pixel_format, PIXEL_FORMAT_MONO01);
	zassert_equal(caps.screen_info, SCREEN_INFO_MONO_VTILED);
	zassert_equal(caps.current_orientation, DISPLAY_ORIENTATION_NORMAL);
	zassert_equal(display_set_pixel_format(matrix, PIXEL_FORMAT_RGB_888), -ENOTSUP);
	zassert_equal(mono.reads + mono.frames + mono.commands, 0);
}

ZTEST(matrix, test_mono_corners_and_partial_write)
{
	const uint8_t corners[12] = {0x81, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x81};
	const uint8_t patch[] = {0x05, 0x02, 0xff, 0xff};
	const struct display_buffer_descriptor desc = {
		.width = 2,
		.height = 3,
		.pitch = 4,
		.buf_size = sizeof(patch),
	};
	uint8_t expected[12];

	zassert_ok(display_write(matrix, 0, 0, &full, corners));
	zassert_equal(mono.frame_len, 12);
	zassert_mem_equal(mono.frame, corners, 12);
	zassert_ok(display_write(matrix, 4, 2, &desc, patch));
	memcpy(expected, corners, 12);
	expected[4] = 0x14;
	expected[5] = 0x08;
	zassert_mem_equal(mono.frame, expected, 12);
	zassert_equal(mono.commands, 0);
}

ZTEST(matrix, test_grayscale_packing_and_mode_switch)
{
	uint8_t ramp[48];
	struct display_capabilities caps;

	for (size_t i = 0; i < sizeof(ramp); i++) {
		ramp[i] = ((i * 2 % 16) << 4) | ((i * 2 + 1) % 16);
	}
	zassert_ok(display_set_pixel_format(matrix, PIXEL_FORMAT_L_4));
	zassert_true(mono.gray);
	zassert_equal(mono.command_len, 12);
	zassert_ok(display_write(matrix, 0, 0, &full_gray, ramp));
	zassert_equal(mono.frame_len, 48);
	zassert_mem_equal(mono.frame, ramp, 48);
	display_get_capabilities(matrix, &caps);
	zassert_equal(caps.current_pixel_format, PIXEL_FORMAT_L_4);
	zassert_equal(caps.screen_info, 0);
	zassert_ok(display_set_pixel_format(matrix, PIXEL_FORMAT_MONO01));
	zassert_false(mono.gray);
	zassert_equal(mono.command_len, 48);
	/* Nonzero grayscale pixels stay lit when displayed as monochrome. */
	zassert_equal(mono.frame[0], 0xee);
	zassert_equal(mono.frame[1], 0xff);
}

ZTEST(matrix, test_gray_odd_pitch_and_partial_nibbles)
{
	const uint8_t patch[] = {0x12, 0x3f, 0xff, 0x45, 0x6f};
	const struct display_buffer_descriptor desc = {
		.width = 3,
		.height = 2,
		.pitch = 5,
		.buf_size = sizeof(patch),
	};
	uint8_t expected[48] = {0};

	zassert_ok(display_set_pixel_format(matrix, PIXEL_FORMAT_L_4));
	zassert_ok(display_write(matrix, 3, 2, &desc, patch));
	expected[13] = 0x01;
	expected[14] = 0x23;
	expected[19] = 0x04;
	expected[20] = 0x56;
	zassert_mem_equal(mono.frame, expected, 48);
}

ZTEST(matrix, test_blanking_caches_and_restores)
{
	const uint8_t image[12] = {1, 2, 4, 8, 16, 32, 64, 128, 0, 0, 0, 0};
	const uint8_t black[12] = {0};
	unsigned int frames;
	unsigned int reads;

	zassert_ok(display_blanking_on(matrix));
	zassert_mem_equal(mono.frame, black, 12);
	frames = mono.frames;
	reads = mono.reads;
	zassert_ok(display_write(matrix, 0, 0, &full, image));
	zassert_equal(mono.frames, frames);
	zassert_equal(mono.reads, reads);
	zassert_ok(display_blanking_off(matrix));
	zassert_mem_equal(mono.frame, image, 12);
}

ZTEST(matrix, test_invalid_descriptors_do_not_touch_bus)
{
	const uint8_t buf[48] = {0};
	struct display_buffer_descriptor desc = full;

	zassert_equal(display_write(matrix, 0, 0, NULL, buf), -EINVAL);
	zassert_equal(display_write(matrix, 0, 0, &desc, NULL), -EINVAL);
	zassert_equal(display_write(matrix, 12, 0, &desc, buf), -EINVAL);
	zassert_equal(display_write(matrix, 0, 8, &desc, buf), -EINVAL);
	zassert_equal(display_write(matrix, UINT16_MAX, UINT16_MAX, &desc, buf), -EINVAL);
	zassert_equal(display_write(matrix, 1, 0, &desc, buf), -EINVAL);
	zassert_equal(display_write(matrix, 0, 1, &desc, buf), -EINVAL);
	desc.width = 0;
	zassert_equal(display_write(matrix, 0, 0, &desc, buf), -EINVAL);
	desc = full;
	desc.height = 0;
	zassert_equal(display_write(matrix, 0, 0, &desc, buf), -EINVAL);
	desc = full;
	desc.pitch = 11;
	zassert_equal(display_write(matrix, 0, 0, &desc, buf), -EINVAL);
	desc = full;
	desc.buf_size = 11;
	zassert_equal(display_write(matrix, 0, 0, &desc, buf), -EINVAL);
	zassert_equal(mono.reads + mono.frames + mono.commands, 0);

	zassert_ok(display_set_pixel_format(matrix, PIXEL_FORMAT_L_4));
	unsigned int reads = mono.reads;

	desc.buf_size = 47;
	zassert_equal(display_write(matrix, 0, 0, &desc, buf), -EINVAL);
	zassert_equal(mono.reads, reads);
}

ZTEST(matrix, test_reserved_prefixes_rejected_in_both_formats)
{
	const char *const prefixes[] = {"CF", "DIE", "MON", "GS4"};
	const enum display_pixel_format formats[] = {PIXEL_FORMAT_MONO01, PIXEL_FORMAT_L_4};
	uint8_t buf[48];

	for (size_t f = 0; f < ARRAY_SIZE(formats); f++) {
		zassert_ok(display_set_pixel_format(matrix, formats[f]));
		unsigned int reads = mono.reads;

		for (size_t i = 0; i < ARRAY_SIZE(prefixes); i++) {
			memset(buf, 0, sizeof(buf));
			memcpy(buf, prefixes[i], strlen(prefixes[i]));
			zassert_equal(display_write(matrix, 0, 0, &full_gray, buf), -EINVAL);
		}
		zassert_equal(mono.reads, reads);
	}
}

ZTEST(matrix, test_partial_write_cannot_assemble_reserved_prefix)
{
	uint8_t buf[12] = {'C', 0};
	const uint8_t patch = 'F';
	const struct display_buffer_descriptor desc = {
		.width = 1,
		.height = 8,
		.pitch = 1,
		.buf_size = 1,
	};

	zassert_ok(display_write(matrix, 0, 0, &full, buf));
	unsigned int reads = mono.reads;

	zassert_equal(display_write(matrix, 1, 0, &desc, &patch), -EINVAL);
	zassert_equal(mono.reads, reads);
	zassert_ok(display_blanking_off(matrix));
	zassert_mem_equal(mono.frame, buf, 12);
}

ZTEST(matrix, test_failed_frame_does_not_commit_pixels)
{
	uint8_t buf[12] = {1};
	const uint8_t patch = 4;
	const struct display_buffer_descriptor desc = {
		.width = 1,
		.height = 8,
		.pitch = 1,
		.buf_size = 1,
	};

	zassert_ok(display_write(matrix, 0, 0, &full, buf));
	buf[0] = 2;
	mono.fail_frame = true;
	zassert_equal(display_write(matrix, 0, 0, &full, buf), -EIO);
	mono.fail_frame = false;
	zassert_ok(display_write(matrix, 1, 0, &desc, &patch));
	buf[0] = 1;
	buf[1] = 4;
	zassert_mem_equal(mono.frame, buf, 12);
}

ZTEST(matrix, test_read_errors_and_identity_are_propagated)
{
	const uint8_t black[12] = {0};

	mono.fail_read = true;
	zassert_equal(display_write(matrix, 0, 0, &full, black), -EIO);
	mono.fail_read = false;
	mono.identity = 0x7c;
	zassert_equal(display_write(matrix, 0, 0, &full, black), -ENODEV);
	mono.identity = 0x72;
	mono.unknown_mode = true;
	zassert_equal(display_write(matrix, 0, 0, &full, black), -ENOTSUP);
	zassert_equal(mono.frames + mono.commands, 0);
}

ZTEST(matrix, test_failed_mode_change_recovers_from_device_state)
{
	struct display_capabilities caps;

	mono.fail_after_command = true;
	zassert_equal(display_set_pixel_format(matrix, PIXEL_FORMAT_L_4), -EIO);
	zassert_true(mono.gray);
	display_get_capabilities(matrix, &caps);
	zassert_equal(caps.current_pixel_format, PIXEL_FORMAT_MONO01);
	mono.fail_after_command = false;
	mono.fail_read = false;
	zassert_ok(display_blanking_off(matrix));
	zassert_false(mono.gray);
	zassert_equal(mono.command_len, 48);
}

ZTEST(matrix, test_mode_command_error_and_unconfirmed_switch)
{
	struct display_capabilities caps;

	mono.fail_command = true;
	zassert_equal(display_set_pixel_format(matrix, PIXEL_FORMAT_L_4), -EIO);
	mono.fail_command = false;
	mono.ignore_command = true;
	zassert_equal(display_set_pixel_format(matrix, PIXEL_FORMAT_L_4), -EIO);
	display_get_capabilities(matrix, &caps);
	zassert_equal(caps.current_pixel_format, PIXEL_FORMAT_MONO01);
	zassert_equal(mono.frames, 0);
}

ZTEST(matrix, test_device_reset_mode_is_rediscovered)
{
	const uint8_t black[48] = {0};

	zassert_ok(display_set_pixel_format(matrix, PIXEL_FORMAT_L_4));
	mono.gray = false; /* Peripheral rebooted independently of the host. */
	zassert_ok(display_write(matrix, 0, 0, &full_gray, black));
	zassert_true(mono.gray);
	zassert_equal(mono.command_len, 12);
	zassert_equal(mono.frame_len, 48);
}

ZTEST(matrix, test_failed_blanking_preserves_visible_write_behavior)
{
	const uint8_t image[12] = {1};

	mono.fail_frame = true;
	zassert_equal(display_blanking_on(matrix), -EIO);
	mono.fail_frame = false;
	zassert_ok(display_write(matrix, 0, 0, &full, image));
	zassert_mem_equal(mono.frame, image, 12);
	zassert_equal(mono.frames, 1);
}

ZTEST_SUITE(matrix, NULL, matrix_setup, matrix_before, NULL, NULL);
