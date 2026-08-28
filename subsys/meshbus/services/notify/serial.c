/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/meshbus/notify.h>
#include <zephyr/sys/base64.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(meshbus_notify, CONFIG_MESHBUS_NOTIFY_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* Constants                                                                  */
/* -------------------------------------------------------------------------- */

#define NOTIFY_SERIAL_FRAME_PREFIX_BYTE 0x1e
#define NOTIFY_SERIAL_FRAME_MAGIC "MBN1"
#define NOTIFY_SERIAL_B64_MAX_LEN \
	(((MESHBUS_NOTIFY_PAYLOAD_MAX_LEN + 2U) / 3U) * 4U)
#define NOTIFY_SERIAL_FRAME_FIXED_OVERHEAD \
	(1U + sizeof("MBN1 ") - 1U + 4U + 1U + 1U + 4U + 1U)
#define NOTIFY_SERIAL_FRAME_MAX_LEN \
	(NOTIFY_SERIAL_FRAME_FIXED_OVERHEAD + NOTIFY_SERIAL_B64_MAX_LEN + 1U)

/* -------------------------------------------------------------------------- */
/* Devices                                                                    */
/* -------------------------------------------------------------------------- */

#if DT_HAS_CHOSEN(zephyr_uart_mcumgr)
static const struct device *const notify_serial_uart =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_uart_mcumgr));
#endif

/* -------------------------------------------------------------------------- */
/* Public Helpers                                                             */
/* -------------------------------------------------------------------------- */

bool meshbus_notify_serial_ready(void)
{
#if DT_HAS_CHOSEN(zephyr_uart_mcumgr)
	return device_is_ready(notify_serial_uart);
#else
	return false;
#endif
}

static int notify_serial_write(const uint8_t *data, size_t len)
{
#if DT_HAS_CHOSEN(zephyr_uart_mcumgr)
	if (len == 0U) {
		return 0;
	}
	if (data == NULL) {
		return -EINVAL;
	}
	if (!meshbus_notify_serial_ready()) {
		return -ENODEV;
	}

	/*
	 * Zephyr's stock UART MCUmgr transport does not expose its TX lock.
	 * Keep each sideband frame as one non-preemptible byte stream so another
	 * firmware thread cannot splice an SMP response into it.
	 */
	k_sched_lock();
	for (size_t i = 0U; i < len; i++) {
		uart_poll_out(notify_serial_uart, data[i]);
	}
	k_sched_unlock();

	return 0;
#else
	ARG_UNUSED(data);
	ARG_UNUSED(len);
	return -ENODEV;
#endif
}

int meshbus_notify_serial_write_frame(const uint8_t *payload, size_t len)
{
	uint8_t frame_buf[NOTIFY_SERIAL_FRAME_MAX_LEN];
	size_t b64_len = 0U;
	size_t crc_len;
	size_t frame_buf_len = sizeof(frame_buf);
	uint16_t crc;
	int header_len;
	int tail_len;
	size_t written;
	size_t off;
	int rc;

	if (payload == NULL && len != 0U) {
		return -EINVAL;
	}
	if (len == 0U) {
		return 0;
	}
	if (len > MESHBUS_NOTIFY_PAYLOAD_MAX_LEN) {
		return -EMSGSIZE;
	}

	frame_buf[0] = NOTIFY_SERIAL_FRAME_PREFIX_BYTE;
	header_len = snprintk((char *)&frame_buf[1], frame_buf_len - 1U,
			      "%s %04X ", NOTIFY_SERIAL_FRAME_MAGIC,
			      (unsigned int)len);
	if (header_len < 0) {
		return -EINVAL;
	}

	off = 1U + (size_t)header_len;
	if (off >= frame_buf_len) {
		return -ENOMEM;
	}

	rc = base64_encode(&frame_buf[off], frame_buf_len - off, &b64_len,
			   payload, len);
	if (rc != 0) {
		return rc;
	}

	crc_len = (off + b64_len) - 1U;
	crc = crc16_itu_t(0x0000, &frame_buf[1], crc_len);

	off += b64_len;
	if (off >= frame_buf_len) {
		return -ENOMEM;
	}

	tail_len = snprintk((char *)&frame_buf[off], frame_buf_len - off, " %04X\n",
			    (unsigned int)crc);
	if (tail_len < 0) {
		return -EINVAL;
	}
	if ((off + (size_t)tail_len) >= frame_buf_len) {
		return -ENOMEM;
	}

	written = off + (size_t)tail_len;
	return notify_serial_write(frame_buf, written);
}
