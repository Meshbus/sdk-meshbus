# SPDX-License-Identifier: Apache-2.0

# u8g2 is huge; keep it behind CONFIG_U8G2.

zephyr_library()

zephyr_library_sources(
	u8g2_display.c
	u8g2_bitmap.c
	u8g2_box.c
	u8g2_buffer.c
	u8g2_circle.c
	u8g2_d_memory.c
	u8g2_font.c
	u8g2_fonts.c
	u8g2_hvline.c
	u8g2_intersection.c
	u8g2_line.c
	u8g2_ll_hvline.c
	u8g2_polygon.c
	u8g2_setup.c

	u8x8_8x8.c
	u8x8_byte.c
	u8x8_cad.c
	u8x8_display.c
	u8x8_setup.c
)
