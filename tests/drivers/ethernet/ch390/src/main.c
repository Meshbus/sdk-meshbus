/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/spi_emul.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#define CH_NODE DT_NODELABEL(ch390)

#define CH390_NCR 0x00
#define CH390_TCR 0x02
#define CH390_RCR 0x05
#define CH390_EPCR 0x0B
#define CH390_EPAR 0x0C
#define CH390_EPDRL 0x0D
#define CH390_PAR 0x10
#define CH390_GPR 0x1F
#define CH390_VIDL 0x28
#define CH390_PIDL 0x2A
#define CH390_MRCMDX 0x70
#define CH390_MWCMD 0x78
#define CH390_TXPLL 0x7C
#define CH390_IMR 0x7F

#define CH390_NCR_RST BIT(0)
#define CH390_EPCR_EPOS BIT(3)
#define CH390_EPCR_ERPRR BIT(2)
#define CH390_EPCR_ERPRW BIT(1)
#define CH390_IMR_PAR BIT(7)
#define CH390_IMR_LNKCHGI BIT(5)
#define CH390_IMR_PRI BIT(0)
#define CH390_RUNTIME_IMR (CH390_IMR_PAR | CH390_IMR_LNKCHGI | CH390_IMR_PRI)
#define CH390_PHY_ANAR 0x04

struct ch390_emul_data {
	uint8_t regs[0x80];
	uint16_t phy_regs[32];
	size_t tx_mem_len;
	uint16_t tx_len;
	uint8_t tx_request_count;
	uint8_t imr_writes[8];
	uint8_t imr_write_count;
};

static struct ch390_emul_data ch390_emul_data;
static const struct device *const ch390_dev = DEVICE_DT_GET(CH_NODE);
static const uint8_t expected_mac[] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

static void ch390_emul_reset(struct ch390_emul_data *data)
{
	memset(data, 0, sizeof(*data));
	sys_put_le16(0x1c00, &data->regs[CH390_VIDL]);
	sys_put_le16(0x9151, &data->regs[CH390_PIDL]);
	data->phy_regs[CH390_PHY_ANAR] = 0x0001;
}

static void ch390_emul_write_reg(struct ch390_emul_data *data, uint8_t reg, uint8_t value)
{
	if (reg == CH390_NCR && (value & CH390_NCR_RST) != 0U) {
		data->regs[reg] = value & (uint8_t)~CH390_NCR_RST;
		return;
	}

	if (reg == CH390_EPCR) {
		uint8_t phy_reg = data->regs[CH390_EPAR] & 0x1fU;

		if ((value & CH390_EPCR_EPOS) != 0U && (value & CH390_EPCR_ERPRR) != 0U) {
			sys_put_le16(data->phy_regs[phy_reg], &data->regs[CH390_EPDRL]);
		} else if ((value & CH390_EPCR_EPOS) != 0U &&
			   (value & CH390_EPCR_ERPRW) != 0U) {
			data->phy_regs[phy_reg] = sys_get_le16(&data->regs[CH390_EPDRL]);
		}
		data->regs[reg] = 0;
		return;
	}

	if (reg == CH390_TCR && (value & BIT(0)) != 0U) {
		data->tx_request_count++;
		data->regs[reg] = 0;
		return;
	}

	if (reg == CH390_IMR && data->imr_write_count < ARRAY_SIZE(data->imr_writes)) {
		data->imr_writes[data->imr_write_count++] = value;
	}

	data->regs[reg] = value;
}

static int ch390_emul_write(const struct spi_buf_set *tx_bufs, struct ch390_emul_data *data)
{
	const uint8_t *cmd_buf;
	const uint8_t *payload;
	uint8_t reg;
	size_t len;

	if (tx_bufs == NULL || tx_bufs->count != 2U ||
	    tx_bufs->buffers[0].len != 1U || tx_bufs->buffers[1].buf == NULL) {
		return -EIO;
	}

	cmd_buf = tx_bufs->buffers[0].buf;
	payload = tx_bufs->buffers[1].buf;
	reg = cmd_buf[0] & 0x7fU;
	len = tx_bufs->buffers[1].len;

	if (reg == CH390_MWCMD) {
		data->tx_mem_len = len;
		return 0;
	}

	for (size_t i = 0; i < len; i++) {
		ch390_emul_write_reg(data, (uint8_t)(reg + i), payload[i]);
	}

	data->tx_len = sys_get_le16(&data->regs[CH390_TXPLL]);
	return 0;
}

static int ch390_emul_read(const struct spi_buf_set *tx_bufs,
			   const struct spi_buf_set *rx_bufs,
			   struct ch390_emul_data *data)
{
	const uint8_t *cmd_buf;
	uint8_t *payload;
	uint8_t reg;
	size_t len;

	if (tx_bufs == NULL || rx_bufs == NULL ||
	    tx_bufs->count != 1U || tx_bufs->buffers[0].len != 1U ||
	    rx_bufs->count != 2U || rx_bufs->buffers[1].buf == NULL) {
		return -EIO;
	}

	cmd_buf = tx_bufs->buffers[0].buf;
	payload = rx_bufs->buffers[1].buf;
	reg = cmd_buf[0] & 0x7fU;
	len = rx_bufs->buffers[1].len;

	for (size_t i = 0; i < len; i++) {
		payload[i] = data->regs[reg + i];
	}

	if (reg == CH390_MRCMDX && len > 0U) {
		payload[0] = 0;
	}

	return 0;
}

static int ch390_emul_io(const struct emul *target,
			 const struct spi_config *config,
			 const struct spi_buf_set *tx_bufs,
			 const struct spi_buf_set *rx_bufs)
{
	struct ch390_emul_data *data = target->data;

	ARG_UNUSED(config);

	if (rx_bufs != NULL) {
		return ch390_emul_read(tx_bufs, rx_bufs, data);
	}

	return ch390_emul_write(tx_bufs, data);
}

static int ch390_emul_init(const struct emul *target, const struct device *parent)
{
	ARG_UNUSED(parent);

	ch390_emul_reset(target->data);
	return 0;
}

static const struct spi_emul_api ch390_emul_api = {
	.io = ch390_emul_io,
};

EMUL_DT_DEFINE(CH_NODE, ch390_emul_init, &ch390_emul_data, NULL, &ch390_emul_api, NULL);

static struct net_pkt *make_pkt(struct net_if *iface, size_t len)
{
	static uint8_t bytes[NET_ETH_MAX_FRAME_SIZE + 1U];
	struct net_pkt *pkt;

	for (size_t i = 0; i < len; i++) {
		bytes[i] = (uint8_t)i;
	}
	memset(bytes, 0xff, sizeof(struct net_eth_addr));
	bytes[6] = 0x02;

	pkt = net_pkt_alloc_with_buffer(iface, len, NET_AF_UNSPEC, 0, K_NO_WAIT);
	zassert_not_null(pkt);
	zassert_ok(net_pkt_write(pkt, bytes, len));
	net_pkt_cursor_init(pkt);
	return pkt;
}

static void *ch390_setup(void)
{
	zassert_true(device_is_ready(ch390_dev));
	zassert_mem_equal(&ch390_emul_data.regs[CH390_PAR], expected_mac, sizeof(expected_mac));

	return NULL;
}

static void ch390_before(void *fixture)
{
	ARG_UNUSED(fixture);

	ch390_emul_data.tx_mem_len = 0;
	ch390_emul_data.tx_len = 0;
	ch390_emul_data.tx_request_count = 0;
	ch390_emul_data.imr_write_count = 0;
}

ZTEST(ch390, test_start_stop_masks_device_interrupts)
{
	const struct ethernet_api *api = ch390_dev->api;

	zassert_ok(api->start(ch390_dev));
	zassert_equal(ch390_emul_data.regs[CH390_IMR], CH390_RUNTIME_IMR);
	zassert_true(ch390_emul_data.imr_write_count >= 2U);
	zassert_equal(ch390_emul_data.imr_writes[0], 0);
	zassert_equal(ch390_emul_data.imr_writes[ch390_emul_data.imr_write_count - 1U],
		      CH390_RUNTIME_IMR);

	zassert_ok(api->stop(ch390_dev));
	zassert_equal(ch390_emul_data.regs[CH390_IMR], 0);
	zassert_equal(ch390_emul_data.regs[CH390_RCR], 0);
	zassert_equal(ch390_emul_data.regs[CH390_GPR], 1);
}

ZTEST(ch390, test_tx_oversized_is_rejected_before_spi_write)
{
	const struct ethernet_api *api = ch390_dev->api;
	struct net_pkt *pkt;

	pkt = make_pkt(NULL, NET_ETH_MAX_FRAME_SIZE + 1U);

	zassert_equal(api->send(ch390_dev, pkt), -EMSGSIZE);
	zassert_equal(ch390_emul_data.tx_mem_len, 0);
	zassert_equal(ch390_emul_data.tx_len, 0);
	net_pkt_unref(pkt);
}

ZTEST(ch390, test_tx_writes_checked_length)
{
	const struct ethernet_api *api = ch390_dev->api;
	struct net_pkt *pkt;
	size_t len = sizeof(struct net_eth_hdr) + 32U;

	zassert_ok(api->start(ch390_dev));
	pkt = make_pkt(NULL, len);

	zassert_ok(api->send(ch390_dev, pkt));
	zassert_equal(ch390_emul_data.tx_mem_len, len);
	zassert_equal(ch390_emul_data.tx_len, len);
	zassert_equal(ch390_emul_data.tx_request_count, 1);
	net_pkt_unref(pkt);
}

ZTEST_SUITE(ch390, NULL, ch390_setup, ch390_before, NULL, NULL);
