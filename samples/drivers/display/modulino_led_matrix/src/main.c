/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define WIDTH  12U
#define HEIGHT 8U
#define PIXELS (WIDTH * HEIGHT)

static const struct device *const matrix = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static unsigned int checks;

#define REQUIRE(expr)                                                                              \
	do {                                                                                       \
		int result_ = (expr);                                                              \
		if (result_ != 0) {                                                                \
			printk("MODULINO_SAMPLE_FAIL line=%d rc=%d: %s\n", __LINE__, result_,      \
			       #expr);                                                             \
			return result_;                                                            \
		}                                                                                  \
	} while (0)

static int expect(const char *label, int actual, int wanted)
{
	if (actual != wanted) {
		printk("MODULINO_SAMPLE_FAIL %s: actual=%d expected=%d\n", label, actual, wanted);
		return -EINVAL;
	}
	checks++;
	return 0;
}

#define EXPECT(label, expr, wanted) REQUIRE(expect(label, (expr), wanted))

static struct display_buffer_descriptor full_desc(enum display_pixel_format format)
{
	return (struct display_buffer_descriptor){
		.width = WIDTH,
		.height = HEIGHT,
		.pitch = WIDTH,
		.buf_size = format == PIXEL_FORMAT_L_4 ? 48 : 12,
	};
}

static int write_full(enum display_pixel_format format, const uint8_t *frame)
{
	struct display_buffer_descriptor desc = full_desc(format);

	return display_write(matrix, 0, 0, &desc, frame);
}

/* Clear in the old format first, so the previous scene cannot make a format
 * conversion collide with a reserved firmware command prefix.
 */
static int prepare(enum display_pixel_format format)
{
	struct display_capabilities caps;
	const uint8_t black[48] = {0};

	display_get_capabilities(matrix, &caps);
	REQUIRE(display_blanking_on(matrix));
	REQUIRE(write_full(caps.current_pixel_format, black));
	REQUIRE(display_set_pixel_format(matrix, format));
	REQUIRE(display_blanking_off(matrix));
	return 0;
}

static int check_capabilities(enum display_pixel_format format)
{
	struct display_capabilities caps;

	display_get_capabilities(matrix, &caps);
	EXPECT("width", caps.x_resolution, WIDTH);
	EXPECT("height", caps.y_resolution, HEIGHT);
	EXPECT("formats", caps.supported_pixel_formats, PIXEL_FORMAT_MONO01 | PIXEL_FORMAT_L_4);
	EXPECT("current format", caps.current_pixel_format, format);
	EXPECT("layout", caps.screen_info,
	       format == PIXEL_FORMAT_MONO01 ? SCREEN_INFO_MONO_VTILED : 0);
	EXPECT("orientation", caps.current_orientation, DISPLAY_ORIENTATION_NORMAL);
	EXPECT("events", caps.supported_events, 0);
	return 0;
}

static int check_input_validation(enum display_pixel_format format)
{
	const uint8_t pixels[48] = {0};
	struct display_buffer_descriptor full = full_desc(format);
	struct display_buffer_descriptor bad = full;

	EXPECT("null descriptor", display_write(matrix, 0, 0, NULL, pixels), -EINVAL);
	EXPECT("null buffer", display_write(matrix, 0, 0, &full, NULL), -EINVAL);
	EXPECT("x out of bounds", display_write(matrix, WIDTH, 0, &full, pixels), -EINVAL);
	EXPECT("y out of bounds", display_write(matrix, 0, HEIGHT, &full, pixels), -EINVAL);
	EXPECT("x overflow", display_write(matrix, UINT16_MAX, 0, &full, pixels), -EINVAL);
	EXPECT("rectangle width", display_write(matrix, 1, 0, &full, pixels), -EINVAL);
	EXPECT("rectangle height", display_write(matrix, 0, 1, &full, pixels), -EINVAL);
	bad.width = 0;
	EXPECT("zero width", display_write(matrix, 0, 0, &bad, pixels), -EINVAL);
	bad = full;
	bad.height = 0;
	EXPECT("zero height", display_write(matrix, 0, 0, &bad, pixels), -EINVAL);
	bad = full;
	bad.pitch = WIDTH - 1;
	EXPECT("short pitch", display_write(matrix, 0, 0, &bad, pixels), -EINVAL);
	bad = full;
	bad.buf_size--;
	EXPECT("short buffer", display_write(matrix, 0, 0, &bad, pixels), -EINVAL);
	return 0;
}

static int check_reserved_prefixes(enum display_pixel_format format)
{
	const char *const reserved[] = {"CF", "DIE", "MON", "GS4"};
	uint8_t frame[48];

	for (int blank = 0; blank < 2; blank++) {
		if (blank) {
			REQUIRE(display_blanking_on(matrix));
		}
		for (size_t i = 0; i < ARRAY_SIZE(reserved); i++) {
			memset(frame, 0, sizeof(frame));
			memcpy(frame, reserved[i], strlen(reserved[i]));
			/* Only use the driver's checked API; never send raw I2C here. */
			EXPECT("reserved prefix", write_full(format, frame), -EINVAL);
		}
	}
	REQUIRE(display_blanking_off(matrix));
	return 0;
}

static int api_checks(void)
{
	const enum display_pixel_format formats[] = {PIXEL_FORMAT_MONO01, PIXEL_FORMAT_L_4};
	uint8_t buf[48] = {0};
	struct display_buffer_descriptor desc = full_desc(PIXEL_FORMAT_MONO01);

	checks = 0;
	for (size_t i = 0; i < ARRAY_SIZE(formats); i++) {
		REQUIRE(prepare(formats[i]));
		REQUIRE(check_capabilities(formats[i]));
		REQUIRE(check_input_validation(formats[i]));
		REQUIRE(check_reserved_prefixes(formats[i]));
		REQUIRE(write_full(formats[i], buf));
	}
	EXPECT("unsupported format", display_set_pixel_format(matrix, PIXEL_FORMAT_RGB_888),
	       -ENOTSUP);
	EXPECT("readback", display_read(matrix, 0, 0, &desc, buf), -ENOSYS);
	EXPECT("clear callback", display_clear(matrix), -ENOSYS);
	EXPECT("global brightness", display_set_brightness(matrix, 128), -ENOSYS);
	EXPECT("global contrast", display_set_contrast(matrix, 128), -ENOSYS);
	EXPECT("rotation", display_set_orientation(matrix, DISPLAY_ORIENTATION_ROTATED_90),
	       -ENOSYS);
	EXPECT("direct framebuffer", display_get_framebuffer(matrix) == NULL, true);
	REQUIRE(prepare(PIXEL_FORMAT_MONO01));

	/* A valid first column plus a partial update must not assemble "CF". */
	buf[0] = 'C';
	REQUIRE(write_full(PIXEL_FORMAT_MONO01, buf));
	desc = (struct display_buffer_descriptor){
		.width = 1,
		.height = 8,
		.pitch = 1,
		.buf_size = 1,
	};
	const uint8_t suffix = 'F';

	EXPECT("partial reserved prefix", display_write(matrix, 1, 0, &desc, &suffix), -EINVAL);
	REQUIRE(prepare(PIXEL_FORMAT_MONO01));
	printk("MODULINO_SAMPLE_API_PASS checks=%u (return values; no pixel readback)\n", checks);
	return 0;
}

static int mono_pattern(bool checker)
{
	uint8_t frame[12] = {0};

	REQUIRE(prepare(PIXEL_FORMAT_MONO01));
	if (checker) {
		for (size_t x = 0; x < WIDTH; x++) {
			frame[x] = (x % 2) ? 0x55 : 0xaa;
		}
	} else {
		/* TL: 1 dot; TR: 2 dots; BL: 3 dots; BR: 2x2 square. */
		frame[0] = 0x81;
		frame[1] = 0x80;
		frame[2] = 0x80;
		frame[10] = 0xc1;
		frame[11] = 0xc1;
	}
	return write_full(PIXEL_FORMAT_MONO01, frame);
}

static int mono_partial(void)
{
	uint8_t frame[12];
	const uint8_t patch[] = {0x09, 0x06, 0x09, 0xff, 0xff};
	struct display_buffer_descriptor desc = {
		.width = 3,
		.height = 4,
		.pitch = 5,
		.buf_size = sizeof(patch),
		.frame_incomplete = true,
	};

	REQUIRE(prepare(PIXEL_FORMAT_MONO01));
	memset(frame, 0x81, sizeof(frame));
	frame[0] = 0xff;
	frame[11] = 0xff;
	REQUIRE(write_full(PIXEL_FORMAT_MONO01, frame));
	REQUIRE(display_write(matrix, 4, 2, &desc, patch));
	/* frame_incomplete does not defer the visible update in this driver. */
	return 0;
}

static int gray_levels(void)
{
	uint8_t frame[48];

	REQUIRE(prepare(PIXEL_FORMAT_L_4));
	for (size_t i = 0; i < sizeof(frame); i++) {
		frame[i] = ((2 * i % 16) << 4) | ((2 * i + 1) % 16);
	}
	return write_full(PIXEL_FORMAT_L_4, frame);
}

static int gray_partial(void)
{
	const uint8_t patch[] = {0x12, 0x3f, 0xff, 0x45, 0x6f};
	const struct display_buffer_descriptor desc = {
		.width = 3,
		.height = 2,
		.pitch = 5,
		.buf_size = sizeof(patch),
	};
	uint8_t frame[48] = {0};

	REQUIRE(prepare(PIXEL_FORMAT_L_4));
	for (size_t y = 0; y < HEIGHT; y++) {
		for (size_t x = 0; x < WIDTH; x++) {
			if (y == 0 || y == HEIGHT - 1 || x == 0 || x == WIDTH - 1) {
				size_t index = y * WIDTH + x;

				frame[index / 2] |= (index % 2) ? 0x0f : 0xf0;
			}
		}
	}
	REQUIRE(write_full(PIXEL_FORMAT_L_4, frame));
	return display_write(matrix, 3, 2, &desc, patch);
}

static int walk_pixels(void)
{
	uint8_t frame[12];

	REQUIRE(prepare(PIXEL_FORMAT_MONO01));
	for (size_t i = 0; i < PIXELS; i++) {
		memset(frame, 0, sizeof(frame));
		frame[i % WIDTH] = BIT(i / WIDTH);
		REQUIRE(write_full(PIXEL_FORMAT_MONO01, frame));
		k_msleep(30);
	}
	return 0;
}

static int blank_restore(void)
{
	uint8_t checker[12];

	REQUIRE(mono_pattern(false));
	printk("VISUAL blank: corners visible\n");
	k_msleep(CONFIG_SAMPLE_MATRIX_SCENE_MS);
	REQUIRE(display_blanking_on(matrix));
	for (size_t i = 0; i < WIDTH; i++) {
		checker[i] = (i % 2) ? 0x55 : 0xaa;
	}
	REQUIRE(write_full(PIXEL_FORMAT_MONO01, checker));
	printk("VISUAL blank: must remain dark after cached checker write\n");
	k_msleep(CONFIG_SAMPLE_MATRIX_SCENE_MS);
	REQUIRE(display_blanking_off(matrix));
	printk("VISUAL blank: checker restored\n");
	return 0;
}

static int convert_formats(void)
{
	REQUIRE(gray_levels());
	printk("VISUAL convert: grayscale\n");
	k_msleep(CONFIG_SAMPLE_MATRIX_SCENE_MS);
	REQUIRE(display_set_pixel_format(matrix, PIXEL_FORMAT_MONO01));
	REQUIRE(check_capabilities(PIXEL_FORMAT_MONO01));
	printk("VISUAL convert: all nonzero gray pixels now fully lit\n");
	k_msleep(CONFIG_SAMPLE_MATRIX_SCENE_MS);
	REQUIRE(display_set_pixel_format(matrix, PIXEL_FORMAT_L_4));
	REQUIRE(check_capabilities(PIXEL_FORMAT_L_4));
	printk("VISUAL convert: original grayscale restored\n");
	return 0;
}

/* Five columns per glyph, bit zero at the top; row seven stays blank. */
static const struct {
	char character;
	uint8_t columns[5];
} font[] = {
	{'F', {0x7f, 0x09, 0x09, 0x09, 0x01}}, {'O', {0x3e, 0x41, 0x41, 0x41, 0x3e}},
	{'B', {0x7f, 0x49, 0x49, 0x49, 0x36}}, {'E', {0x7f, 0x49, 0x49, 0x49, 0x41}},
	{'D', {0x7f, 0x41, 0x41, 0x22, 0x1c}}, {'V', {0x1f, 0x20, 0x40, 0x20, 0x1f}},
	{'K', {0x7f, 0x08, 0x14, 0x22, 0x41}}, {'I', {0x00, 0x41, 0x7f, 0x41, 0x00}},
	{'T', {0x01, 0x01, 0x7f, 0x01, 0x01}}, {'S', {0x26, 0x49, 0x49, 0x49, 0x32}},
	{'P', {0x7f, 0x09, 0x09, 0x09, 0x06}}, {'3', {0x22, 0x41, 0x49, 0x49, 0x36}},
	{'2', {0x42, 0x61, 0x51, 0x49, 0x46}}, {'C', {0x3e, 0x41, 0x41, 0x41, 0x22}},
	{'6', {0x3c, 0x4a, 0x49, 0x49, 0x30}},
};

static uint8_t text_column(char character, unsigned int column)
{
	if (column < 5) {
		for (size_t i = 0; i < ARRAY_SIZE(font); i++) {
			if (font[i].character == character) {
				return font[i].columns[column];
			}
		}
	}
	/* Spaces and the inter-character column are blank. */
	return 0;
}

static int scroll_text(void)
{
	static const char text[] = "FOBE DEVKIT ESP32C6";
	const int text_width = (sizeof(text) - 1) * 6 - 1;
	uint8_t frame[12];
	unsigned int frames = 0;

	REQUIRE(prepare(PIXEL_FORMAT_MONO01));
	/* Start and finish off-screen, including a fully blank frame at each end. */
	for (int origin = WIDTH; origin >= -text_width; origin--) {
		for (int x = 0; x < WIDTH; x++) {
			int column = x - origin;

			frame[x] = column < 0 || column >= text_width
					   ? 0
					   : text_column(text[column / 6], column % 6);
		}
		REQUIRE(write_full(PIXEL_FORMAT_MONO01, frame));
		frames++;
		k_msleep(CONFIG_SAMPLE_MATRIX_SCROLL_MS);
	}
	printk("MODULINO_SAMPLE_SCROLL_COMPLETE text=%s frames=%u\n", text, frames);
	return 0;
}

static void wave_frame(uint8_t *frame, enum display_pixel_format format, unsigned int phase)
{
	memset(frame, 0, 48);
	for (size_t y = 0; y < HEIGHT; y++) {
		for (size_t x = 0; x < WIDTH; x++) {
			unsigned int ramp = (x * 2 + y * 3 + phase) % 32;
			uint8_t level = ramp < 16 ? ramp : 31 - ramp;

			if (format == PIXEL_FORMAT_L_4) {
				size_t pixel = y * WIDTH + x;

				frame[pixel / 2] |= level << ((pixel % 2) ? 0 : 4);
			} else if (level >= 8) {
				frame[x] |= BIT(y);
			}
		}
	}
}

static int measure_refresh(enum display_pixel_format format)
{
	const char *name = format == PIXEL_FORMAT_L_4 ? "L_4" : "MONO01";
	uint8_t frame[48];
	uint32_t frames = 0;
	uint32_t min_us = UINT32_MAX;
	uint32_t max_us = 0;
	uint64_t sum_us = 0;
	int64_t start_ms;
	int64_t elapsed_ms;
	uint64_t fps_milli;

	REQUIRE(prepare(format));
	printk("VISUAL refresh: format=%s full_frame_bytes=%u duration_ms=%d unpaced=1\n", name,
	       format == PIXEL_FORMAT_L_4 ? 48U : 12U, CONFIG_SAMPLE_MATRIX_REFRESH_MS);
	start_ms = k_uptime_get();
	do {
		uint32_t start_cycles;
		uint32_t call_us;

		/* Advance the wave on every submitted frame; all 32 phases in both
		 * formats avoid the firmware's reserved command prefixes.
		 */
		wave_frame(frame, format, frames % 32);
		start_cycles = k_cycle_get_32();
		REQUIRE(write_full(format, frame));
		/* Unsigned subtraction handles a counter wrap during this call. */
		call_us = k_cyc_to_us_floor64((uint32_t)(k_cycle_get_32() - start_cycles));
		min_us = MIN(min_us, call_us);
		max_us = MAX(max_us, call_us);
		sum_us += call_us;
		frames++;
		elapsed_ms = k_uptime_get() - start_ms;
		/* No application sleep or per-frame logging: only rendering and
		 * Display API work, including the driver's required settling delay.
		 */
	} while (elapsed_ms < CONFIG_SAMPLE_MATRIX_REFRESH_MS);

	fps_milli = (uint64_t)frames * 1000000U / elapsed_ms;
	printk("MODULINO_SAMPLE_REFRESH_PASS format=%s frames=%u elapsed_ms=%lld "
	       "fps=%u.%03u write_us_avg=%u write_us_min=%u write_us_max=%u "
	       "host_updates_only=1\n",
	       name, frames, (long long)elapsed_ms, (unsigned int)(fps_milli / 1000),
	       (unsigned int)(fps_milli % 1000), (unsigned int)(sum_us / frames), min_us, max_us);
	return 0;
}

static int max_refresh(void)
{
	REQUIRE(measure_refresh(PIXEL_FORMAT_MONO01));
	REQUIRE(measure_refresh(PIXEL_FORMAT_L_4));
	return 0;
}

struct scene {
	const char *name;
	const char *expected;
	int (*show)(void);
};

static int corners(void)
{
	return mono_pattern(false);
}

static int checker(void)
{
	return mono_pattern(true);
}

static const struct scene scenes[] = {
	{"scroll", "FOBE DEVKIT ESP32C6 moves right to left in a 5x7 font", scroll_text},
	{"refresh", "unpaced moving diagonal waves: monochrome then grayscale", max_refresh},
	{"corners", "TL=1 TR=2 BL=3 BR=2x2 dots", corners},
	{"checker", "alternating lit/dark pixels across all 12x8 pixels", checker},
	{"mono_partial", "bright border with a 3x4 pattern at x=4 y=2", mono_partial},
	{"gray", "levels 0..15 in row-major order, repeated six times", gray_levels},
	{"gray_partial", "bright border, levels 1 2 3 / 4 5 6 at x=3 y=2", gray_partial},
	{"walk", "single dot walks all 96 positions from top-left to bottom-right", walk_pixels},
	{"blank", "corners, dark while caching checker, then checker", blank_restore},
	{"convert", "grayscale, monochrome, original grayscale", convert_formats},
};

static int demo(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(scenes); i++) {
		printk("VISUAL scene=%s expected=%s\n", scenes[i].name, scenes[i].expected);
		REQUIRE(scenes[i].show());
		k_msleep(CONFIG_SAMPLE_MATRIX_SCENE_MS);
	}
	printk("MODULINO_SAMPLE_SEQUENCE_COMPLETE scenes=%u visual_confirmation=required\n",
	       (unsigned int)ARRAY_SIZE(scenes));
	return 0;
}

int main(void)
{
	int ret;
	unsigned int cycle = 0;

	printk("MODULINO_SAMPLE_START board=%s device=%s\n", CONFIG_BOARD_TARGET, matrix->name);
	if (!device_is_ready(matrix)) {
		printk("MODULINO_SAMPLE_FAIL device not ready: check I2C power/wiring/firmware\n");
		return -ENODEV;
	}
	/* Allow the host to reconnect the native USB console after flashing. */
	k_msleep(3000);
	ret = api_checks();
	if (ret != 0) {
		return ret;
	}

	while (true) {
		printk("MODULINO_SAMPLE_CYCLE_BEGIN cycle=%u\n", ++cycle);
		ret = demo();
		if (ret != 0) {
			return ret;
		}
		printk("MODULINO_SAMPLE_CYCLE_COMPLETE cycle=%u\n", cycle);
	}

	return 0;
}
