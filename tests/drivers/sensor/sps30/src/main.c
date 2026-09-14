/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/ztest.h>

static const uint8_t frame[] = {0x3f, 0xa0, 0x56, 0x0, 0x0, 0x81, 0x40, 0x20, 0x8e, 0x0, 0x0, 0x81,
				0x40, 0x98, 0x88, 0x0, 0x0, 0x81, 0x41, 0x22, 0x18, 0x0, 0x0, 0x81,
				0x40, 0xa0, 0xf4, 0x0, 0x0, 0x81, 0x40, 0xc0, 0x4f, 0x0, 0x0, 0x81,
				0x40, 0xe0, 0xc9, 0x0, 0x0, 0x81, 0x41, 0x0,  0xfc, 0x0, 0x0, 0x81,
				0x41, 0x10, 0xbf, 0x0, 0x0, 0x81, 0x3f, 0x0,  0xaa, 0x0, 0x0, 0x81};
static const struct device *const sensor = DEVICE_DT_GET(DT_NODELABEL(pm));
static uint16_t selected;
static bool ready = true;
static bool corrupt;
static bool fail;
static bool bad_ready;
static bool invalid_float;
static unsigned int reads;

static int transfer(const struct emul *target, struct i2c_msg *msgs, int count, int addr)
{
	ARG_UNUSED(target);
	zassert_equal(count, 1);
	zassert_equal(addr, 0x69);
	zassert_true(msgs[0].flags & I2C_MSG_STOP);
	if (fail) {
		return -EIO;
	}
	struct i2c_msg *m = &msgs[0];
	if (!(m->flags & I2C_MSG_READ)) {
		zassert_true(m->len == 2 || m->len == 5);
		selected = (m->buf[0] << 8) | m->buf[1];
		if (selected == 0x0010) {
			const uint8_t start[] = {0, 0x10, 3, 0, 0xac};
			zassert_equal(m->len, sizeof(start));
			zassert_mem_equal(m->buf, start, sizeof(start));
		}
		return 0;
	}
	reads++;
	if (selected == 0xd100 || selected == 0x0202) {
		zassert_equal(m->len, 3);
		/* CRC fixtures: 00 00 -> 81; 00 01 -> b0; 01 00 -> 75. */
		const uint8_t yes[] = {0, 1, 0xb0};
		const uint8_t no[] = {0, 0, 0x81};
		const uint8_t version[] = {1, 0, 0x75};
		memcpy(m->buf, selected == 0xd100 ? version : ready ? yes : no, 3);
		if (bad_ready) {
			m->buf[2] ^= 1;
		}
	} else {
		zassert_equal(selected, 0x0300);
		zassert_equal(m->len, sizeof(frame));
		memcpy(m->buf, frame, sizeof(frame));
		if (corrupt) {
			m->buf[59] ^= 1;
		}
		if (invalid_float) {
			/* +Inf (7f80 0000), including valid independent CRC bytes. */
			m->buf[0] = 0x7f;
			m->buf[1] = 0x80;
			m->buf[2] = 0x59;
			m->buf[3] = 0;
			m->buf[4] = 0;
			m->buf[5] = 0x81;
		}
	}
	return 0;
}
static int init(const struct emul *e, const struct device *parent)
{
	ARG_UNUSED(e);
	ARG_UNUSED(parent);
	return 0;
}
static const struct i2c_emul_api api = {.transfer = transfer};
EMUL_DT_DEFINE(DT_NODELABEL(pm), init, NULL, NULL, &api, NULL);

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	ready = true;
	corrupt = false;
	fail = false;
	bad_ready = false;
	invalid_float = false;
	reads = 0;
	zassert_true(device_is_ready(sensor));
}
ZTEST(sps30, test_mass_mapping)
{
	struct sensor_value v;
	zassert_ok(sensor_sample_fetch(sensor));
	zassert_ok(sensor_channel_get(sensor, SENSOR_CHAN_PM_1_0, &v));
	zassert_equal(v.val1, 1);
	zassert_equal(v.val2, 250000);
	zassert_ok(sensor_channel_get(sensor, SENSOR_CHAN_PM_2_5, &v));
	zassert_equal(v.val1, 2);
	zassert_equal(v.val2, 500000);
	zassert_ok(sensor_channel_get(sensor, SENSOR_CHAN_PM_10, &v));
	zassert_equal(v.val1, 10);
	zassert_equal(v.val2, 125000);
}
ZTEST(sps30, test_not_ready)
{
	ready = false;
	zassert_equal(sensor_sample_fetch(sensor), -EAGAIN);
	zassert_equal(reads, 1);
}
ZTEST(sps30, test_crc_and_rollback)
{
	struct sensor_value v;
	zassert_ok(sensor_sample_fetch(sensor));
	corrupt = true;
	zassert_equal(sensor_sample_fetch(sensor), -EBADMSG);
	zassert_ok(sensor_channel_get(sensor, SENSOR_CHAN_PM_10, &v));
	zassert_equal(v.val1, 10);
	zassert_equal(v.val2, 125000);
	corrupt = false;
	bad_ready = true;
	zassert_equal(sensor_sample_fetch(sensor), -EBADMSG);
}
ZTEST(sps30, test_nonfinite)
{
	invalid_float = true;
	zassert_equal(sensor_sample_fetch(sensor), -EBADMSG);
}
ZTEST(sps30, test_bus_error_and_unsupported)
{
	struct sensor_value v;
	fail = true;
	zassert_equal(sensor_sample_fetch(sensor), -EIO);
	zassert_equal(sensor_sample_fetch_chan(sensor, SENSOR_CHAN_HUMIDITY), -ENOTSUP);
	zassert_equal(sensor_channel_get(sensor, SENSOR_CHAN_HUMIDITY, &v), -ENOTSUP);
}
ZTEST_SUITE(sps30, NULL, NULL, before, NULL, NULL);
