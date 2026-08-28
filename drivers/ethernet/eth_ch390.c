/* WCH CH390 Ethernet Controller with SPI interface
 *
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT	wch_ch390

#include <zephyr/logging/log.h>

#ifndef CONFIG_ETHERNET_LOG_LEVEL
#define CONFIG_ETHERNET_LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#endif

LOG_MODULE_REGISTER(eth_ch390, CONFIG_ETHERNET_LOG_LEVEL);

#include <string.h>
#include <errno.h>

#include <zephyr/net/ethernet.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/net/net_pkt.h>
#include <ethernet/eth_stats.h>
#include <zephyr/net/net_if.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/net/phy.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>

static const struct device *eth_ch390_get_phy(const struct device *dev,
					      struct net_if *iface);

/* CH390 Product ID */
#define CH390_VENDOR_ID			0x1C00
#define CH390_PRODUCT_ID		0x9151

/* CH390 PHY Address */
#define CH390_PHY_ADDR			1

/* CH390 Registers */
/* NCR - Network Control Register */
#define CH390_NCR			0x00
/* NSR - Network Status Register */
#define CH390_NSR			0x01
/* TCR - TX Control Register */
#define CH390_TCR			0x02
/* RCR - RX Control Register */
#define CH390_RCR			0x05
/* FCR - RX/TX Flow Control Register */
#define CH390_FCR			0x0A
/* WCR - Wakeup Control Register */
#define CH390_WCR			0x0F
/* EPCR - EEPROM & PHY Control Register */
#define CH390_EPCR			0x0B
/* EPAR - EEPROM & PHY Address Register */
#define CH390_EPAR			0x0C
/* EPDRL - EEPROM & PHY Low Byte Data Register */
#define CH390_EPDRL			0x0D
/* PAR - Physical Address Register */
#define CH390_PAR			0x10
/* MAR - Multicast Address Hash Table Register */
#define CH390_MAR			0x16
/* GPR - General Purpose Register */
#define CH390_GPR			0x1F
/* VIDL - Vendor ID Low Byte Register */
#define CH390_VIDL			0x28
/* PIDL - Product ID Low Byte Register */
#define CH390_PIDL			0x2A
/* TCR2 - TX Control Register 2 */
#define CH390_TCR2			0x2D
/* TCSCR - TX Checksum Control Register */
#define CH390_TCSCR			0x31
/* RCSCSR - RX Checksum Control Status Register */
#define CH390_RCSCSR			0x32
/* INTCR - INT Pin Control Register */
#define CH390_INTCR			0x39
/* INTCKCR - INT Pin Clock Output Control Register */
#define CH390_INTCKCR			0x54
/* MPTRCR - Memory Pointer Control Register */
#define CH390_MPTRCR			0x55
/* MRCMDX - Memory Data Pre-Fetch Read Command Without Address Increment Register */
#define CH390_MRCMDX			0x70
/* MRCMD - Memory Data Read Command With Address Increment Register */
#define CH390_MRCMD			0x72
/* MRRL - Memory Data Read Address Low Byte Register */
#define CH390_MRRL			0x74
/* MRRH - Memory Data Read Address High Byte Register */
#define CH390_MRRH			0x75
/* MWCMD - Memory Data Write Command With Address Increment Register */
#define CH390_MWCMD			0x78
/* TXPLL - TX Packet Length Low Byte Register */
#define CH390_TXPLL			0x7C
/* ISR - Interrupt Status Register */
#define CH390_ISR			0x7E
/* IMR - Interrupt Mask Register */
#define CH390_IMR			0x7F

/* 0x00 */
/* FDX - Duplex Mode of the Internal PHY */
#define CH390_NCR_FDX			BIT(3)
/* RST - Software Reset and Auto-Clear after 10us */
#define CH390_NCR_RST			BIT(0)

/* 0x01 */
/* SPEED - Speed of Internal PHY */
#define CH390_NSR_SPEED		BIT(7)
/* LINKST - Link Status of Internal PHY */
#define CH390_NSR_LINKST		BIT(6)
/* WAKEST - Wake event status (R/WC1) */
#define CH390_NSR_WAKEST		BIT(5)
/* TX2END - TX Packet Index II Complete Status */
#define CH390_NSR_TX2END		BIT(3)
/* TX1END - TX Packet Index I Complete Status */
#define CH390_NSR_TX1END		BIT(2)

/* 0x02 */
/* TXREQ - TX Request. Auto-Clear after Sending Completely */
#define CH390_TCR_TXREQ		BIT(0)

/* 0x05 */
/* DIS_CRC - Discard CRC Error Packet */
#define CH390_RCR_DIS_CRC		BIT(4)
/* ALL - Receive all multicast packets */
#define CH390_RCR_ALL			BIT(3)
/* PRMSC - Promiscuous Mode */
#define CH390_RCR_PRMSC		BIT(1)
/* RXEN - RX Enable */
#define CH390_RCR_RXEN			BIT(0)

/* 0x06 */
/* MF - Multicast Frame */
#define CH390_RSR_MF			BIT(6)
/* RX status error bits, excluding multicast indicator */
#define CH390_RSR_ERR_MASK		(BIT(7) | BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0))

/* 0x0B */
/* EPOS - EEPROM or PHY Operation Select */
#define CH390_EPCR_EPOS		BIT(3)
/* ERPRR - EEPROM Read or PHY Register Read Command */
#define CH390_EPCR_ERPRR		BIT(2)
/* ERPRW - EEPROM Write or PHY Register Write Command */
#define CH390_EPCR_ERPRW		BIT(1)
/* ERRE - EEPROM Access Status or PHY Access Status */
#define CH390_EPCR_ERRE		BIT(0)

/* 0x0C */
/* PHY Address bit shift */
#define CH390_EPAR_PHY_ADR_SHIFT	6

/* 0x16 + 7 */
/* Bit 7 = Enable all broadcast packets */
#define CH390_MAR_7_BCAST_EN		0x80

/* 0x1F */
#define CH390_GPR_PHY_ON		0x00
#define CH390_GPR_PHY_OFF		0x01

/* 0x39 */
#define CH390_INTCR_POL_HIGH		0x00
#define CH390_INTCR_POL_LOW		0x01

/* 0x2D */
#define CH390_TCR2_RLCP			BIT(6)

/* 0x31 */
#define CH390_TCSCR_IPV6TCPCSE		BIT(4)
#define CH390_TCSCR_IPV6UDPCSE		BIT(3)
#define CH390_TCSCR_UDPCSE		BIT(2)
#define CH390_TCSCR_TCPCSE		BIT(1)
#define CH390_TCSCR_IPCSE		BIT(0)

/* 0x0A */
#define CH390_FCR_FLOW_ENABLE		0x39

/* PHY registers */
#define CH390_PHY_BMCR			0x00
#define CH390_PHY_ANAR			0x04
#define CH390_PHY_ANLPAR		0x05

/* BMCR bits */
#define CH390_PHY_BMCR_SPEED100		BIT(13)
#define CH390_PHY_BMCR_ANENABLE		BIT(12)
#define CH390_PHY_BMCR_ANRESTART	BIT(9)
#define CH390_PHY_BMCR_FULLDPLX		BIT(8)

/* Auto-negotiation advertisement bits */
#define CH390_PHY_ANAR_SELECTOR_8023	0x0001
#define CH390_PHY_ANAR_10_HALF		BIT(5)
#define CH390_PHY_ANAR_10_FULL		BIT(6)
#define CH390_PHY_ANAR_100_HALF		BIT(7)
#define CH390_PHY_ANAR_100_FULL		BIT(8)
#define CH390_PHY_ANAR_PAUSE		BIT(10)

/* 0x55 */
#define CH390_MPTRCR_RST_RX		BIT(0)

/* 0x7E */
/* LNKCHG - Link Status Change */
#define CH390_ISR_LNKCHG		BIT(5)
/* ROO - RX Overflow Counter Overflow */
#define CH390_ISR_ROO			BIT(3)
/* ROS - RX Overflow */
#define CH390_ISR_ROS			BIT(2)
/* PT - Packet Transmitted */
#define CH390_ISR_PT			BIT(1)
/* PR - Packet Received */
#define CH390_ISR_PR			BIT(0)
#define CH390_ISR_CLR_STATUS		(CH390_ISR_LNKCHG | CH390_ISR_ROO | CH390_ISR_ROS | \
					 CH390_ISR_PT | CH390_ISR_PR)

/* 0x7F */
/* PAR - Pointer Auto-Return Mode */
#define CH390_IMR_PAR			BIT(7)
/* LNKCHGI - Enable Link Status Change Interrupt */
#define CH390_IMR_LNKCHGI		BIT(5)
/* PTI - Enable Packet Transmitted Interrupt */
#define CH390_IMR_PTI			BIT(1)
/* PRI - Enable Packet Received Interrupt */
#define CH390_IMR_PRI			BIT(0)

#define CH390_PKT_READY			0x01
#define CH390_PKT_ERR_MASK		0xFE
#define CH390_ETH_CRC_LEN		4U
#define CH390_MAX_RX_FRAME_SIZE		(NET_ETH_MAX_FRAME_SIZE + CH390_ETH_CRC_LEN)
#define CH390_RX_MEM_START		0x0C00
#define CH390_RX_MEM_END		0x3FFF
#define CH390_RX_MEM_SIZE		(CH390_RX_MEM_END - CH390_RX_MEM_START + 1)
#define CH390_RX_MEM_START_HI		(CH390_RX_MEM_START >> 8)
#define CH390_RUNTIME_IMR		(CH390_IMR_PAR | CH390_IMR_PRI | CH390_IMR_LNKCHGI)

/* SPI Read Opcode */
#define CH390_SPI_RD			0x00
/* SPI Write Opcode */
#define CH390_SPI_WR			0x80

/* Max time to wait for EEPROM or PHY access completion */
#define CH390_EPCR_POLL_TIMEOUT	K_MSEC(10)
/* Delay between EPCR status polls */
#define CH390_EPCR_POLL_INTERVAL	K_USEC(100)
/* Max time to wait for TX request bit to clear */
#define CH390_TCR_POLL_TIMEOUT		K_USEC(1000)
/* Hardware reset pulse width */
#define CH390_HW_RESET_PULSE_MS		2
/* Delay after reset release */
#define CH390_HW_RESET_SETTLE_MS	4
/* Soft-reset completion timeout */
#define CH390_SOFT_RESET_TIMEOUT_MS	100
/* Delay between soft-reset status polls */
#define CH390_SOFT_RESET_POLL_MS	10
/* Post soft-reset settle time */
#define CH390_POST_RESET_SETTLE_MS	10
/* Datasheet recommends link-change handling after status settles */
#define CH390_LINK_STABLE_DELAY_MS	65
#define CH390_PHY_RESTART_WAIT_MS	5
#define CH390_TX_CHKSUM_MASK		(CH390_TCSCR_IPCSE | CH390_TCSCR_TCPCSE | \
					 CH390_TCSCR_UDPCSE | CH390_TCSCR_IPV6TCPCSE | \
					 CH390_TCSCR_IPV6UDPCSE)

struct eth_ch390_config {
	struct net_eth_mac_config mac_cfg;
	struct gpio_dt_spec gpio_int;
	struct gpio_dt_spec gpio_reset;
	const struct device *phy_dev;
	struct spi_dt_spec spi;
};

struct eth_ch390_data {
	K_KERNEL_STACK_MEMBER(rx_thread_stack, CONFIG_ETH_CH390_RX_THREAD_STACK_SIZE);
	uint8_t tx_buf[NET_ETH_MAX_FRAME_SIZE];
	uint8_t rx_buf[NET_ETH_MAX_FRAME_SIZE];
	uint8_t multicast_hash[8];
	uint8_t multicast_refcnt[64];
	struct gpio_callback gpio_cb;
	struct phy_link_state state;
	struct k_thread rx_thread;
	const struct device *dev;
	struct k_mutex spi_lock;
	struct k_sem int_event;
	struct net_if *iface;
	phy_callback_t phy_cb;
	void *phy_cb_user_data;
	uint8_t mac_addr[6];
	bool allmulti_enabled;
	bool promisc_enabled;
	bool flow_ctrl_enabled;
	bool autoneg_enabled;
#if defined(CONFIG_NET_STATISTICS_ETHERNET)
	struct net_stats_eth stats;
#endif
};

struct eth_ch390_rxhdr {
	uint8_t flag;
	uint8_t status;
	uint8_t len[2];
};

enum eth_ch390_rx_result {
	CH390_RX_RESULT_NO_PKT,
	CH390_RX_RESULT_FRAME_OK,
	CH390_RX_RESULT_FRAME_DROPPED_CONTINUE,
	CH390_RX_RESULT_FATAL_IO,
	CH390_RX_RESULT_FATAL_RESTART,
};

static void eth_ch390_update_link_status(const struct device *dev);
static int eth_ch390_restart_rx_path(const struct device *dev);
static int eth_ch390_apply_flow_control_policy(const struct device *dev,
					      bool link_up, enum phy_link_speed speed);
static int eth_ch390_phy_read(const struct device *dev, uint16_t reg_addr, uint32_t *data);
static int eth_ch390_phy_write(const struct device *dev, uint16_t reg_addr, uint32_t data);

static int eth_ch390_gpio_irq_set_enabled(const struct device *dev, bool enabled)
{
	const struct eth_ch390_config *config = dev->config;

	return gpio_pin_interrupt_configure_dt(&config->gpio_int,
					       enabled ? GPIO_INT_EDGE_TO_ACTIVE :
							 GPIO_INT_DISABLE);
}

static int eth_ch390_spi_read(const struct device *dev, uint8_t reg,
			       uint8_t *data, size_t len)
{
	const struct eth_ch390_config *config = dev->config;

	uint8_t cmd = reg | CH390_SPI_RD;
	struct spi_buf txb = {
		.buf = &cmd,
		.len = 1,
	};
	struct spi_buf_set txbs = {
		.buffers = &txb,
		.count = 1,
	};

	struct spi_buf rxb[2] = {
		{
			.buf = NULL,
			.len = 1,
		},
		{
			.buf = data,
			.len = len,
		},
	};
	struct spi_buf_set rxbs = {
		.buffers = rxb,
		.count = ARRAY_SIZE(rxb),
	};

	return spi_transceive_dt(&config->spi, &txbs, &rxbs);
}

static int eth_ch390_spi_write(const struct device *dev, uint8_t reg,
				uint8_t *data, size_t len)
{
	const struct eth_ch390_config *config = dev->config;

	uint8_t cmd = reg | CH390_SPI_WR;
	struct spi_buf txb[2] = {
		{
			.buf = &cmd,
			.len = 1,
		},
		{
			.buf = data,
			.len = len,
		},
	};
	struct spi_buf_set txbs = {
		.buffers = txb,
		.count = ARRAY_SIZE(txb),
	};

	return spi_write_dt(&config->spi, &txbs);
}

static inline int eth_ch390_spi_read_mem(const struct device *dev, uint8_t reg,
					  uint8_t *data, size_t len)
{
	return eth_ch390_spi_read(dev, reg, data, len);
}

static inline int eth_ch390_spi_write_mem(const struct device *dev, uint8_t reg,
					   uint8_t *data, size_t len)
{
	return eth_ch390_spi_write(dev, reg, data, len);
}

static inline int eth_ch390_spi_read_reg(const struct device *dev, uint8_t reg, uint8_t *data)
{
	return eth_ch390_spi_read(dev, reg, data, 1);
}

static inline int eth_ch390_spi_write_reg(const struct device *dev, uint8_t reg, uint8_t data)
{
	return eth_ch390_spi_write(dev, reg, &data, 1);
}

static inline int eth_ch390_spi_read_regs(const struct device *dev, uint8_t reg,
					   uint8_t *data, size_t len)
{
	int ret = 0;

	for (size_t i = 0; i < len; i++) {
		ret = eth_ch390_spi_read_reg(dev, (uint8_t)(reg + i), &data[i]);
		if (ret < 0) {
			break;
		}
	}

	return ret;
}

static inline int eth_ch390_spi_write_regs(const struct device *dev, uint8_t reg,
					    uint8_t *data, size_t len)
{
	int ret = 0;

	for (size_t i = 0; i < len; i++) {
		ret = eth_ch390_spi_write_reg(dev, (uint8_t)(reg + i), data[i]);
		if (ret < 0) {
			break;
		}
	}

	return ret;
}

static inline uint32_t eth_ch390_reverse_bits_u32(uint32_t value)
{
	uint32_t reversed = 0U;

	for (size_t i = 0U; i < 32U; i++) {
		reversed <<= 1;
		reversed |= (value & 0x1U);
		value >>= 1;
	}

	return reversed;
}

static uint32_t eth_ch390_crc32_ieee(const uint8_t *data, size_t len)
{
	uint32_t crc = 0xFFFFFFFFU;

	for (size_t i = 0U; i < len; i++) {
		crc ^= data[i];

		for (int bit = 0; bit < 8; bit++) {
			if ((crc & 0x1U) != 0U) {
				crc = (crc >> 1) ^ 0xEDB88320U;
			} else {
				crc >>= 1;
			}
		}
	}

	return ~crc;
}

static void eth_ch390_notify_phy_link_change(const struct device *dev)
{
	struct eth_ch390_data *data = dev->data;
	const struct device *phy_dev = eth_ch390_get_phy(dev, data->iface);
	struct phy_link_state state;

	if (data->phy_cb == NULL) {
		return;
	}

	state = data->state;
	data->phy_cb(phy_dev, &state, data->phy_cb_user_data);
}

static int eth_ch390_apply_multicast_hash(const struct device *dev)
{
	struct eth_ch390_data *data = dev->data;
	uint8_t mar[sizeof(data->multicast_hash)];

	memcpy(mar, data->multicast_hash, sizeof(mar));
	mar[7] |= CH390_MAR_7_BCAST_EN;

	return eth_ch390_spi_write_regs(dev, CH390_MAR, mar, sizeof(mar));
}

static int eth_ch390_apply_rx_mode(const struct device *dev)
{
	struct eth_ch390_data *data = dev->data;
	uint8_t rcr;
	int ret;

	ret = eth_ch390_spi_read_reg(dev, CH390_RCR, &rcr);
	if (ret < 0) {
		return ret;
	}

	rcr &= ~(CH390_RCR_PRMSC | CH390_RCR_ALL);
	if (data->promisc_enabled) {
		rcr |= CH390_RCR_PRMSC;
	}
	if (data->allmulti_enabled) {
		rcr |= CH390_RCR_ALL;
	}

	return eth_ch390_spi_write_reg(dev, CH390_RCR, rcr);
}

static int eth_ch390_set_multicast_filter(const struct device *dev,
					   const struct ethernet_filter *filter)
{
	struct eth_ch390_data *data = dev->data;
	uint32_t crc = eth_ch390_reverse_bits_u32(
		eth_ch390_crc32_ieee(filter->mac_address.addr, sizeof(filter->mac_address.addr)));
	uint8_t hash_index = (crc >> 26) & 0x3FU;
	uint8_t byte_index = hash_index / 8U;
	uint8_t bit_mask = BIT(hash_index % 8U);
	uint8_t old_refcnt = data->multicast_refcnt[hash_index];
	uint8_t old_hash = data->multicast_hash[byte_index];
	int ret;

	if (filter->set) {
		if (data->multicast_refcnt[hash_index] == UINT8_MAX) {
			return -EOVERFLOW;
		}

		data->multicast_refcnt[hash_index]++;
		if (data->multicast_refcnt[hash_index] == 1U) {
			data->multicast_hash[byte_index] |= bit_mask;
		}
	} else {
		if (data->multicast_refcnt[hash_index] == 0U) {
			return -ENOENT;
		}

		data->multicast_refcnt[hash_index]--;
		if (data->multicast_refcnt[hash_index] == 0U) {
			data->multicast_hash[byte_index] &= (uint8_t)~bit_mask;
		}
	}

	ret = eth_ch390_apply_multicast_hash(dev);
	if (ret < 0) {
		data->multicast_refcnt[hash_index] = old_refcnt;
		data->multicast_hash[byte_index] = old_hash;
	}

	return ret;
}

static int eth_ch390_read_pkt_ready(const struct device *dev, uint8_t *ready)
{
	int ret;

	ret = eth_ch390_spi_read_reg(dev, CH390_MRCMDX, ready);
	if (ret < 0) {
		return ret;
	}

	/* MRCMDX is prefetch based; second read returns current status. */
	return eth_ch390_spi_read_reg(dev, CH390_MRCMDX, ready);
}

static int eth_ch390_reset_rx_pointer(const struct device *dev)
{
	return eth_ch390_spi_write_reg(dev, CH390_MPTRCR, CH390_MPTRCR_RST_RX);
}

static int eth_ch390_drop_frame(const struct device *dev, uint16_t length)
{
	uint8_t ptr[2];
	uint16_t addr;
	int ret;

	ret = eth_ch390_spi_read_regs(dev, CH390_MRRL, ptr, sizeof(ptr));
	if (ret < 0) {
		return ret;
	}

	addr = sys_get_le16(ptr);
	if ((addr < CH390_RX_MEM_START) || (addr > CH390_RX_MEM_END)) {
		LOG_WRN_RATELIMIT("%s: invalid RX pointer 0x%04x", dev->name, addr);
		return -ERANGE;
	}

	addr += length;

	while (addr > CH390_RX_MEM_END) {
		addr -= CH390_RX_MEM_SIZE;
	}

	sys_put_le16(addr, ptr);

	return eth_ch390_spi_write_regs(dev, CH390_MRRL, ptr, sizeof(ptr));
}

static int eth_ch390_restart_rx_path(const struct device *dev)
{
	uint8_t rcr;
	uint8_t ready;
	int ret;

	ret = eth_ch390_spi_write_reg(dev, CH390_IMR, 0x00);
	if (ret < 0) {
		return ret;
	}

	ret = eth_ch390_spi_read_reg(dev, CH390_RCR, &rcr);
	if (ret < 0) {
		return ret;
	}

	ret = eth_ch390_spi_write_reg(dev, CH390_RCR, rcr & ~CH390_RCR_RXEN);
	if (ret < 0) {
		return ret;
	}

	ret = eth_ch390_reset_rx_pointer(dev);
	if (ret < 0) {
		return ret;
	}

	ret = eth_ch390_spi_write_reg(dev, CH390_MRRH, CH390_RX_MEM_START_HI);
	if (ret < 0) {
		return ret;
	}

	ret = eth_ch390_spi_write_reg(dev, CH390_ISR, CH390_ISR_CLR_STATUS);
	if (ret < 0) {
		return ret;
	}

	ret = eth_ch390_spi_write_reg(dev, CH390_IMR, CH390_RUNTIME_IMR);
	if (ret < 0) {
		return ret;
	}

	ret = eth_ch390_spi_write_reg(dev, CH390_RCR, rcr | CH390_RCR_RXEN);
	if (ret < 0) {
		return ret;
	}

	(void)eth_ch390_read_pkt_ready(dev, &ready);

	return 0;
}

static int eth_ch390_check_id(const struct device *dev)
{
	uint16_t id;
	int ret;

	/* Read vendor ID */
	ret = eth_ch390_spi_read_regs(dev, CH390_VIDL, (void *)&id, 2);
	if (ret < 0) {
		return ret;
	}

	if (sys_le16_to_cpu(id) != CH390_VENDOR_ID) {
		LOG_ERR("%s: Found vendor ID %04x, expected %04x", dev->name, id, CH390_VENDOR_ID);
		return -ENODEV;
	}

	/* Read product ID */
	ret = eth_ch390_spi_read_regs(dev, CH390_PIDL, (void *)&id, 2);
	if (ret < 0) {
		return ret;
	}

	if (sys_le16_to_cpu(id) != CH390_PRODUCT_ID) {
		LOG_ERR("%s: Found product ID %04x, expected %04x",
			dev->name, id, CH390_PRODUCT_ID);
		return -ENODEV;
	}

	LOG_INF("%s: vendor/product ID verified", dev->name);

	return ret;
}

static int eth_ch390_hw_reset(const struct device *dev)
{
	const struct eth_ch390_config *config = dev->config;
	int ret;

	ret = gpio_pin_set_dt(&config->gpio_reset, 1);
	if (ret < 0) {
		return ret;
	}

	k_msleep(CH390_HW_RESET_PULSE_MS);

	ret = gpio_pin_set_dt(&config->gpio_reset, 0);
	if (ret < 0) {
		return ret;
	}

	k_msleep(CH390_HW_RESET_SETTLE_MS);

	return 0;
}

static int eth_ch390_epcr_poll(const struct device *dev, k_timeout_t timeout)
{
	k_timepoint_t timepoint = sys_timepoint_calc(timeout);
	uint8_t epcr;
	int ret;

	do {
		ret = eth_ch390_spi_read_reg(dev, CH390_EPCR, &epcr);
		if (ret || ((epcr & CH390_EPCR_ERRE) == 0)) {
			return ret;
		}
		k_sleep(CH390_EPCR_POLL_INTERVAL);
	} while (!sys_timepoint_expired(timepoint));

	return -ETIMEDOUT;
}

static int eth_ch390_tcr_poll(const struct device *dev, k_timeout_t timeout)
{
	k_timepoint_t timepoint = sys_timepoint_calc(timeout);
	uint8_t tcr;
	int ret;

	do {
		ret = eth_ch390_spi_read_reg(dev, CH390_TCR, &tcr);
		if (ret < 0) {
			return ret;
		}

		if ((tcr & CH390_TCR_TXREQ) == 0U) {
			return 0;
		}
	} while (!sys_timepoint_expired(timepoint));

	return -ETIMEDOUT;
}

static int eth_ch390_setup_defaults(const struct device *dev)
{
	int ret;

	ret = eth_ch390_spi_write_reg(dev, CH390_NCR, 0x00);
	if (ret < 0) {
		return ret;
	}

	ret = eth_ch390_spi_write_reg(dev, CH390_WCR, 0x00);
	if (ret < 0) {
		return ret;
	}

	ret = eth_ch390_spi_write_reg(dev, CH390_TCR, 0x00);
	if (ret < 0) {
		return ret;
	}

	ret = eth_ch390_spi_write_reg(dev, CH390_RCR, CH390_RCR_DIS_CRC);
	if (ret < 0) {
		return ret;
	}

	ret = eth_ch390_spi_write_reg(dev, CH390_TCR2, CH390_TCR2_RLCP);
	if (ret < 0) {
		return ret;
	}

	ret = eth_ch390_spi_write_reg(dev, CH390_TCSCR, CH390_TX_CHKSUM_MASK);
	if (ret < 0) {
		return ret;
	}

	ret = eth_ch390_spi_write_reg(dev, CH390_RCSCSR, 0x00);
	if (ret < 0) {
		return ret;
	}

	ret = eth_ch390_spi_write_reg(dev, CH390_FCR, 0x00);
	if (ret < 0) {
		return ret;
	}

	ret = eth_ch390_apply_multicast_hash(dev);
	if (ret < 0) {
		return ret;
	}

	ret = eth_ch390_spi_write_reg(dev, CH390_INTCKCR, 0x00);
	if (ret < 0) {
		return ret;
	}

	return eth_ch390_spi_write_reg(dev, CH390_NSR,
				       CH390_NSR_WAKEST | CH390_NSR_TX2END |
				       CH390_NSR_TX1END);
}

static int eth_ch390_hw_start(const struct device *dev, struct net_if *iface)
{
	ARG_UNUSED(iface);

	const struct eth_ch390_config *config = dev->config;
	struct eth_ch390_data *data = dev->data;
	int tries = CH390_SOFT_RESET_TIMEOUT_MS / CH390_SOFT_RESET_POLL_MS;
	uint8_t ncr;
	int ret;

	LOG_INF("%s: Starting hardware", dev->name);

	ret = eth_ch390_gpio_irq_set_enabled(dev, false);
	if (ret < 0) {
		return ret;
	}

	k_mutex_lock(&data->spi_lock, K_FOREVER);

	/*
	 * Force PHY on before MAC setup. CH390 bring-up is more reliable when
	 * we do not rely on the previous GPR state.
	 */
	ret = eth_ch390_spi_write_reg(dev, CH390_GPR, CH390_GPR_PHY_ON);
	if (ret < 0) {
		goto out_unlock;
	}

	/* MAC/PHY registers are not accessible for ~1 ms after PHY on. */
	k_msleep(1);

	/* Software Reset and Auto-Clear after 10 us */
	ret = eth_ch390_spi_write_reg(dev, CH390_NCR, CH390_NCR_RST);
	if (ret < 0) {
		goto out_unlock;
	}

	for (int i = 0; i < tries; i++) {
		ret = eth_ch390_spi_read_reg(dev, CH390_NCR, &ncr);
		if (ret < 0) {
			goto out_unlock;
		}

		if ((ncr & CH390_NCR_RST) == 0U) {
			break;
		}

		k_msleep(CH390_SOFT_RESET_POLL_MS);
	}

	if ((ncr & CH390_NCR_RST) != 0U) {
		ret = -ETIMEDOUT;
		goto out_unlock;
	}

	/* Re-assert PHY power after soft reset. */
	ret = eth_ch390_spi_write_reg(dev, CH390_GPR, CH390_GPR_PHY_ON);
	if (ret < 0) {
		goto out_unlock;
	}

	k_msleep(CH390_POST_RESET_SETTLE_MS);

	/* Set gpio pin polarity based on gpio dt flags */
	ret = eth_ch390_spi_write_reg(dev, CH390_INTCR,
				       (config->gpio_int.dt_flags & GPIO_ACTIVE_LOW) > 0 ?
				       CH390_INTCR_POL_LOW : CH390_INTCR_POL_HIGH);
	if (ret < 0) {
		goto out_unlock;
	}

	ret = eth_ch390_setup_defaults(dev);
	if (ret < 0) {
		goto out_unlock;
	}

	ret = eth_ch390_apply_rx_mode(dev);
	if (ret < 0) {
		goto out_unlock;
	}

	ret = eth_ch390_restart_rx_path(dev);
	if (ret < 0) {
		goto out_unlock;
	}

	ret = eth_ch390_spi_write_reg(dev, CH390_ISR, CH390_ISR_CLR_STATUS);
	if (ret < 0) {
		goto out_unlock;
	}

	k_sem_reset(&data->int_event);
	k_mutex_unlock(&data->spi_lock);

	ret = eth_ch390_gpio_irq_set_enabled(dev, true);
	if (ret < 0) {
		return ret;
	}

	/*
	 * CH390 may report transient link-change right after reset. Take one
	 * delayed link sample so carrier state can be established even if no
	 * later edge arrives.
	 */
	k_msleep(CH390_LINK_STABLE_DELAY_MS);
	eth_ch390_update_link_status(dev);

	return 0;

out_unlock:
	k_mutex_unlock(&data->spi_lock);
	return ret;
}

static int eth_ch390_hw_stop(const struct device *dev, struct net_if *iface)
{
	ARG_UNUSED(iface);

	struct eth_ch390_data *data = dev->data;
	int ret;

	ret = eth_ch390_gpio_irq_set_enabled(dev, false);
	if (ret < 0) {
		return ret;
	}

	k_mutex_lock(&data->spi_lock, K_FOREVER);

	/* Mask device interrupts and stop RX before changing flow-control or PHY state. */
	ret = eth_ch390_spi_write_reg(dev, CH390_IMR, 0x00);
	if (ret < 0) {
		goto out_unlock;
	}

	ret = eth_ch390_spi_write_reg(dev, CH390_RCR, 0x00);
	if (ret < 0) {
		goto out_unlock;
	}

	ret = eth_ch390_apply_flow_control_policy(dev, false, 0U);
	if (ret < 0) {
		LOG_WRN_RATELIMIT("%s: failed to disable flow control (%d)", dev->name, ret);
	}
	data->flow_ctrl_enabled = false;

	/* Power off the internal phy */
	ret = eth_ch390_spi_write_reg(dev, CH390_GPR, CH390_GPR_PHY_OFF);
	if (ret < 0) {
		goto out_unlock;
	}

	ret = eth_ch390_spi_write_reg(dev, CH390_ISR, CH390_ISR_CLR_STATUS);

out_unlock:
	k_sem_reset(&data->int_event);
	k_mutex_unlock(&data->spi_lock);
	return ret;
}

static int eth_ch390_tx(const struct device *dev, struct net_pkt *pkt)
{
	size_t len = net_pkt_get_len(pkt);
	struct eth_ch390_data *data = dev->data;
	uint16_t len16;
	uint8_t tcr;
	int ret;

	if (len > sizeof(data->tx_buf)) {
		ret = -EMSGSIZE;
		goto out_update_errors_tx;
	}

	/* Read TX data from net_pkt */
	if (net_pkt_read(pkt, data->tx_buf, len)) {
		ret = -EIO;
		goto out_update_errors_tx;
	}

	k_mutex_lock(&data->spi_lock, K_FOREVER);

	ret = eth_ch390_tcr_poll(dev, CH390_TCR_POLL_TIMEOUT);
	if (ret < 0) {
		goto out_spi_unlock;
	}

	/* Write TX data to TX SRAM */
	ret = eth_ch390_spi_write_mem(dev, CH390_MWCMD, data->tx_buf, len);
	if (ret < 0) {
		goto out_spi_unlock;
	}

	/* Write TX data length */
	len16 = sys_cpu_to_le16((uint16_t)len);
	ret = eth_ch390_spi_write_regs(dev, CH390_TXPLL, (void *)&len16, 2);
	if (ret < 0) {
		goto out_spi_unlock;
	}

	/* TX request */
	ret = eth_ch390_spi_read_reg(dev, CH390_TCR, &tcr);
	if (ret < 0) {
		goto out_spi_unlock;
	}

	ret = eth_ch390_spi_write_reg(dev, CH390_TCR, tcr | CH390_TCR_TXREQ);
	if (ret < 0) {
		goto out_spi_unlock;
	}

	k_mutex_unlock(&data->spi_lock);

	/* Update ethernet statistics */
	eth_stats_update_bytes_tx(data->iface, len);
	eth_stats_update_pkts_tx(data->iface);
	if (net_eth_is_addr_broadcast(&NET_ETH_HDR(pkt)->dst)) {
		eth_stats_update_broadcast_tx(data->iface);
	} else if (net_eth_is_addr_multicast(&NET_ETH_HDR(pkt)->dst)) {
		eth_stats_update_multicast_tx(data->iface);
	}  else {
		/* Unicast frame */
	}

	return 0;

out_spi_unlock:
	k_mutex_unlock(&data->spi_lock);
out_update_errors_tx:
	eth_stats_update_errors_tx(data->iface);
	return ret;
}

static enum eth_ch390_rx_result eth_ch390_receive_one(const struct device *dev)
{
	struct eth_ch390_data *data = dev->data;
	struct eth_ch390_rxhdr rxhdr;
	struct net_pkt *pkt;
	uint8_t ready;
	uint16_t raw_len;
	uint16_t frame_len;
	uint8_t crc[CH390_ETH_CRC_LEN];
	int ret;

	ret = eth_ch390_read_pkt_ready(dev, &ready);
	if (ret < 0) {
		return CH390_RX_RESULT_FATAL_IO;
	}

	if ((ready & CH390_PKT_ERR_MASK) != 0U) {
		eth_stats_update_errors_rx(data->iface);
		LOG_WRN_RATELIMIT("%s: invalid RX ready status 0x%02x", dev->name, ready);
		return CH390_RX_RESULT_FATAL_RESTART;
	}

	if ((ready & CH390_PKT_READY) == 0U) {
		return CH390_RX_RESULT_NO_PKT;
	}

	ret = eth_ch390_spi_read_mem(dev, CH390_MRCMD, (uint8_t *)&rxhdr, sizeof(rxhdr));
	if (ret < 0) {
		return CH390_RX_RESULT_FATAL_IO;
	}

	raw_len = sys_get_le16(rxhdr.len);
	if (raw_len < CH390_ETH_CRC_LEN) {
		eth_stats_update_errors_rx(data->iface);
		LOG_WRN_RATELIMIT("%s: invalid frame length %u", dev->name, raw_len);
		return CH390_RX_RESULT_FATAL_RESTART;
	}

	if ((rxhdr.status & CH390_RSR_ERR_MASK) != 0U) {
		eth_stats_update_errors_rx(data->iface);
		LOG_WRN_RATELIMIT("%s: dropping RX frame with status 0x%02x", dev->name, rxhdr.status);
		ret = eth_ch390_drop_frame(dev, raw_len);
		if (ret == -ERANGE) {
			return CH390_RX_RESULT_FATAL_RESTART;
		}
		return (ret < 0) ? CH390_RX_RESULT_FATAL_IO : CH390_RX_RESULT_FRAME_DROPPED_CONTINUE;
	}

	if (raw_len > CH390_MAX_RX_FRAME_SIZE) {
		eth_stats_update_errors_rx(data->iface);
		LOG_WRN_RATELIMIT("%s: RX frame too large (%u)", dev->name, raw_len);
		return CH390_RX_RESULT_FATAL_RESTART;
	}

	frame_len = raw_len - CH390_ETH_CRC_LEN;
	pkt = net_pkt_rx_alloc_with_buffer(data->iface, frame_len, NET_AF_UNSPEC, 0,
					   K_MSEC(CONFIG_ETH_CH390_TIMEOUT));
	if (!pkt) {
		eth_stats_update_errors_rx(data->iface);
		ret = eth_ch390_drop_frame(dev, raw_len);
		if (ret == -ERANGE) {
			return CH390_RX_RESULT_FATAL_RESTART;
		}
		return (ret < 0) ? CH390_RX_RESULT_FATAL_IO : CH390_RX_RESULT_FRAME_DROPPED_CONTINUE;
	}

	ret = eth_ch390_spi_read_mem(dev, CH390_MRCMD, data->rx_buf, frame_len);
	if (ret < 0) {
		net_pkt_unref(pkt);
		return CH390_RX_RESULT_FATAL_IO;
	}

	ret = eth_ch390_spi_read_mem(dev, CH390_MRCMD, crc, sizeof(crc));
	if (ret < 0) {
		net_pkt_unref(pkt);
		return CH390_RX_RESULT_FATAL_IO;
	}

	ret = net_pkt_write(pkt, data->rx_buf, frame_len);
	if (ret < 0) {
		net_pkt_unref(pkt);
		return CH390_RX_RESULT_FATAL_IO;
	}

	ret = net_recv_data(data->iface, pkt);
	if (ret < 0) {
		net_pkt_unref(pkt);
		eth_stats_update_errors_rx(data->iface);
		return CH390_RX_RESULT_FRAME_DROPPED_CONTINUE;
	}

	eth_stats_update_bytes_rx(data->iface, frame_len);
	eth_stats_update_pkts_rx(data->iface);
	if (frame_len >= sizeof(struct net_eth_hdr)) {
		struct net_eth_addr *dst = (struct net_eth_addr *)data->rx_buf;

		if (net_eth_is_addr_broadcast(dst)) {
			eth_stats_update_broadcast_rx(data->iface);
		} else if (net_eth_is_addr_multicast(dst)) {
			eth_stats_update_multicast_rx(data->iface);
		}
	}

	return CH390_RX_RESULT_FRAME_OK;
}

static int eth_ch390_rx(const struct device *dev)
{
	struct eth_ch390_data *data = dev->data;
	enum eth_ch390_rx_result rx_ret;
	int ret;

	k_mutex_lock(&data->spi_lock, K_FOREVER);

	while (true) {
		rx_ret = eth_ch390_receive_one(dev);

		if (rx_ret == CH390_RX_RESULT_FRAME_OK ||
		    rx_ret == CH390_RX_RESULT_FRAME_DROPPED_CONTINUE) {
			continue;
		}

		if (rx_ret == CH390_RX_RESULT_FATAL_RESTART) {
			LOG_WRN_RATELIMIT("%s: restart RX path", dev->name);
			if (eth_ch390_restart_rx_path(dev) < 0) {
				LOG_WRN_RATELIMIT("%s: RX restart failed", dev->name);
				ret = -EIO;
				goto out_update_errors_rx;
			}
			break;
		}

		if (rx_ret == CH390_RX_RESULT_FATAL_IO) {
			ret = -EIO;
			goto out_update_errors_rx;
		}

		break;
	}

	ret = 0;
	goto out_spi_unlock;

out_update_errors_rx:
	eth_stats_update_errors_rx(data->iface);
out_spi_unlock:
	k_mutex_unlock(&data->spi_lock);
	return ret;
}

static int eth_ch390_update_pause_advertisement(const struct device *dev)
{
	uint16_t anar;
	uint32_t anar_val;
	int ret;

	ret = eth_ch390_phy_read(dev, CH390_PHY_ANAR, &anar_val);
	if (ret < 0) {
		return ret;
	}
	anar = (uint16_t)anar_val;

	anar |= CH390_PHY_ANAR_SELECTOR_8023;
	if (IS_ENABLED(CONFIG_ETH_CH390_FLOW_CONTROL)) {
		anar |= CH390_PHY_ANAR_PAUSE;
	} else {
		anar &= ~CH390_PHY_ANAR_PAUSE;
	}

	return eth_ch390_phy_write(dev, CH390_PHY_ANAR, anar);
}

static int eth_ch390_apply_flow_control_policy(const struct device *dev,
						bool link_up,
						enum phy_link_speed speed)
{
	struct eth_ch390_data *data = dev->data;
	bool enable = false;
	uint16_t anar;
	uint16_t anlpar;
	uint32_t phy_val;
	int ret;

	if (IS_ENABLED(CONFIG_ETH_CH390_FLOW_CONTROL) && data->autoneg_enabled &&
	    link_up && PHY_LINK_IS_FULL_DUPLEX(speed)) {
		ret = eth_ch390_phy_read(dev, CH390_PHY_ANAR, &phy_val);
		if (ret < 0) {
			return ret;
		}
		anar = (uint16_t)phy_val;

		ret = eth_ch390_phy_read(dev, CH390_PHY_ANLPAR, &phy_val);
		if (ret < 0) {
			return ret;
		}
		anlpar = (uint16_t)phy_val;

		enable = ((anar & CH390_PHY_ANAR_PAUSE) != 0U) &&
			 ((anlpar & CH390_PHY_ANAR_PAUSE) != 0U);
	}

	if (enable == data->flow_ctrl_enabled) {
		return 0;
	}

	ret = eth_ch390_spi_write_reg(dev, CH390_FCR, enable ? CH390_FCR_FLOW_ENABLE : 0x00);
	if (ret < 0) {
		return ret;
	}

	data->flow_ctrl_enabled = enable;
	return 0;
}

static void eth_ch390_apply_link_state(const struct device *dev,
					const struct phy_link_state *new_state)
{
	struct eth_ch390_data *data = dev->data;
	struct phy_link_state old_state = data->state;

	data->state = *new_state;

	if (data->iface != NULL) {
		if (!old_state.is_up && new_state->is_up) {
			LOG_INF("%s: Link up", dev->name);
			net_eth_carrier_on(data->iface);
		} else if (old_state.is_up && !new_state->is_up) {
			LOG_INF("%s: Link down", dev->name);
			net_eth_carrier_off(data->iface);
		}
	}

	if (new_state->is_up && old_state.speed != new_state->speed) {
		LOG_INF("%s: Link speed %s Mb, %s duplex", dev->name,
			PHY_LINK_IS_SPEED_100M(new_state->speed) ? "100" : "10",
			PHY_LINK_IS_FULL_DUPLEX(new_state->speed) ? "full" : "half");
	}

	if (old_state.is_up != new_state->is_up || old_state.speed != new_state->speed) {
		eth_ch390_notify_phy_link_change(dev);
	}
}

static void eth_ch390_update_link_status(const struct device *dev)
{
	struct eth_ch390_data *data = dev->data;
	struct phy_link_state new_state = { 0 };
	enum phy_link_speed speed = 0U;
	uint8_t nsr;
	uint8_t ncr;
	int ret;

	k_mutex_lock(&data->spi_lock, K_FOREVER);

	if (eth_ch390_spi_read_reg(dev, CH390_NSR, &nsr) < 0) {
		k_mutex_unlock(&data->spi_lock);
		return;
	}

	if (eth_ch390_spi_read_reg(dev, CH390_NCR, &ncr) < 0) {
		k_mutex_unlock(&data->spi_lock);
		return;
	}

	if ((nsr & CH390_NSR_LINKST) > 0) {
		if ((nsr & CH390_NSR_SPEED) > 0) {
			speed = ((ncr & CH390_NCR_FDX) > 0) ? LINK_FULL_10BASE :
							       LINK_HALF_10BASE;
		} else {
			speed = ((ncr & CH390_NCR_FDX) > 0) ? LINK_FULL_100BASE :
							       LINK_HALF_100BASE;
		}

		new_state.is_up = true;
		new_state.speed = speed;
	}

	ret = eth_ch390_apply_flow_control_policy(dev, new_state.is_up, new_state.speed);
	k_mutex_unlock(&data->spi_lock);
	if (ret < 0) {
		LOG_WRN_RATELIMIT("%s: flow control policy update failed (%d)", dev->name, ret);
	}

	eth_ch390_apply_link_state(dev, &new_state);
}

static void eth_ch390_rx_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const struct eth_ch390_config *config;
	struct eth_ch390_data *data;
	struct device *dev;
	uint8_t isr = 0;

	dev = p1;
	config = dev->config;
	data = dev->data;

	while (true) {
		int gpio_level;

		k_sem_take(&data->int_event, K_FOREVER);

		do {
			bool rx_restarted = false;
			int ret;

			k_mutex_lock(&data->spi_lock, K_FOREVER);
			ret = eth_ch390_spi_read_reg(dev, CH390_ISR, &isr);
			if (ret == 0 && isr != 0U) {
				ret = eth_ch390_spi_write_reg(dev, CH390_ISR, isr);
			}
			k_mutex_unlock(&data->spi_lock);
			if (ret < 0 || isr == 0U) {
				break;
			}

			if ((isr & (CH390_ISR_ROO | CH390_ISR_ROS)) != 0U) {
				eth_stats_update_errors_rx(data->iface);
				LOG_WRN_RATELIMIT("%s: RX overflow (ISR=0x%02x)", dev->name, isr);
				k_mutex_lock(&data->spi_lock, K_FOREVER);
				ret = eth_ch390_restart_rx_path(dev);
				k_mutex_unlock(&data->spi_lock);
				if (ret == 0) {
					rx_restarted = true;
				} else {
					LOG_WRN_RATELIMIT("%s: RX restart failed (%d)", dev->name, ret);
				}
			}

			if ((isr & CH390_ISR_PR) > 0 && !rx_restarted) {
				(void)eth_ch390_rx(dev);
				LOG_DBG("%s: Packet Received", dev->name);
			}

			if ((isr & CH390_ISR_LNKCHG) > 0) {
				eth_ch390_update_link_status(dev);
				LOG_DBG("%s: Link changed", dev->name);
			}

			gpio_level = gpio_pin_get_dt(&config->gpio_int);
			if (gpio_level < 0) {
				break;
			}
		} while (gpio_level > 0);
	}
}

static void eth_ch390_iface_init(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	struct eth_ch390_data *data = dev->data;

	net_if_set_link_addr(iface, data->mac_addr, sizeof(data->mac_addr), NET_LINK_ETHERNET);

	data->iface = iface;

	ethernet_init(iface);

	/* Do not start the interface until PHY link is up */
	net_if_carrier_off(iface);

	k_thread_create(&data->rx_thread, data->rx_thread_stack,
			CONFIG_ETH_CH390_RX_THREAD_STACK_SIZE,
			eth_ch390_rx_thread, (void *)dev, NULL, NULL,
			K_PRIO_COOP(CONFIG_ETH_CH390_RX_THREAD_PRIO),
			0, K_NO_WAIT);
	k_thread_name_set(&data->rx_thread, "eth_ch390");
}

static enum ethernet_hw_caps eth_ch390_get_capabilities(const struct device *dev,
							 struct net_if *iface)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(iface);

	return ETHERNET_LINK_10BASE | ETHERNET_LINK_100BASE
		| ETHERNET_HW_FILTERING
		| ETHERNET_HW_TX_CHKSUM_OFFLOAD
#if defined(CONFIG_NET_PROMISCUOUS_MODE)
		| ETHERNET_PROMISC_MODE
#endif
	;
}

static int eth_ch390_set_config_mac_address(const struct device *dev,
					     const struct ethernet_config *config)
{
	struct eth_ch390_data *data = dev->data;
	int ret;

	memcpy(data->mac_addr, config->mac_address.addr, sizeof(data->mac_addr));

	ret = eth_ch390_spi_write_regs(dev, CH390_PAR, data->mac_addr, sizeof(data->mac_addr));
	if (ret < 0) {
		return ret;
	}

	if (data->iface != NULL) {
		net_if_set_link_addr(data->iface, data->mac_addr, sizeof(data->mac_addr),
				     NET_LINK_ETHERNET);
	}

	LOG_INF("%s: MAC set to %02x:%02x:%02x:%02x:%02x:%02x", dev->name,
		data->mac_addr[0], data->mac_addr[1], data->mac_addr[2], data->mac_addr[3],
		data->mac_addr[4], data->mac_addr[5]);

	return 0;
}

static int eth_ch390_set_config_promisc(const struct device *dev,
					 const struct ethernet_config *config)
{
	struct eth_ch390_data *data = dev->data;
	bool old_promisc;
	int ret;

	if (!IS_ENABLED(CONFIG_NET_PROMISCUOUS_MODE)) {
		return -ENOTSUP;
	}

	if (data->promisc_enabled == config->promisc_mode) {
		return -EALREADY;
	}

	old_promisc = data->promisc_enabled;
	data->promisc_enabled = config->promisc_mode;
	ret = eth_ch390_apply_rx_mode(dev);
	if (ret < 0) {
		data->promisc_enabled = old_promisc;
	}

	return ret;
}

static int eth_ch390_set_config_filter(const struct device *dev,
					const struct ethernet_config *config)
{
	if (config->filter.type != ETHERNET_FILTER_TYPE_DST_MAC_ADDRESS) {
		return -ENOTSUP;
	}

	if (net_eth_is_addr_broadcast((struct net_eth_addr *)&config->filter.mac_address)) {
		return -ENOTSUP;
	}

	if (!net_eth_is_addr_multicast((struct net_eth_addr *)&config->filter.mac_address)) {
		return -ENOTSUP;
	}

	return eth_ch390_set_multicast_filter(dev, &config->filter);
}

static int eth_ch390_set_config(const struct device *dev,
				 struct net_if *iface,
				 enum ethernet_config_type type,
				 const struct ethernet_config *config)
{
	ARG_UNUSED(iface);

	struct eth_ch390_data *data = dev->data;
	int ret;

	if (config == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&data->spi_lock, K_FOREVER);

	switch (type) {
	case ETHERNET_CONFIG_TYPE_MAC_ADDRESS:
		ret = eth_ch390_set_config_mac_address(dev, config);
		break;
	case ETHERNET_CONFIG_TYPE_PROMISC_MODE:
		ret = eth_ch390_set_config_promisc(dev, config);
		break;
	case ETHERNET_CONFIG_TYPE_FILTER:
		ret = eth_ch390_set_config_filter(dev, config);
		break;
	default:
		ret = -ENOTSUP;
		break;
	}

	k_mutex_unlock(&data->spi_lock);
	return ret;
}

static int eth_ch390_get_config(const struct device *dev, struct net_if *iface,
				 enum ethernet_config_type type,
				 struct ethernet_config *config)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(iface);

	switch (type) {
	case ETHERNET_CONFIG_TYPE_RX_CHECKSUM_SUPPORT:
		config->chksum_support = ETHERNET_CHECKSUM_SUPPORT_NONE;
		return 0;
	case ETHERNET_CONFIG_TYPE_TX_CHECKSUM_SUPPORT:
		config->chksum_support = ETHERNET_CHECKSUM_SUPPORT_IPV4_HEADER |
					 ETHERNET_CHECKSUM_SUPPORT_IPV6_HEADER |
					 ETHERNET_CHECKSUM_SUPPORT_TCP |
					 ETHERNET_CHECKSUM_SUPPORT_UDP;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static const struct device *eth_ch390_get_phy(const struct device *dev,
					      struct net_if *iface)
{
	ARG_UNUSED(iface);

	const struct eth_ch390_config *config = dev->config;

	return config->phy_dev;
}

#if defined(CONFIG_NET_STATISTICS_ETHERNET)
static struct net_stats_eth *eth_ch390_get_stats(const struct device *dev,
						 struct net_if *iface)
{
	ARG_UNUSED(iface);

	struct eth_ch390_data *data = dev->data;

	return &data->stats;
}
#endif

static const struct ethernet_api eth_ch390_api = {
	.iface_api.init = eth_ch390_iface_init,
#if defined(CONFIG_NET_STATISTICS_ETHERNET)
	.get_stats = eth_ch390_get_stats,
#endif
	.get_capabilities = eth_ch390_get_capabilities,
	.set_config = eth_ch390_set_config,
	.get_config = eth_ch390_get_config,
	.start = eth_ch390_hw_start,
	.stop = eth_ch390_hw_stop,
	.get_phy = eth_ch390_get_phy,
	.send = eth_ch390_tx,
};

static int eth_ch390_get_link_state(const struct device *dev,
				     struct phy_link_state *state)
{
	struct eth_ch390_data *const data = dev->data;

	state->speed = data->state.speed;
	state->is_up = data->state.is_up;

	return 0;
}

static int eth_ch390_phy_link_cb_set(const struct device *dev, phy_callback_t cb, void *user_data)
{
	struct eth_ch390_data *data = dev->data;

	data->phy_cb = cb;
	data->phy_cb_user_data = user_data;

	if (cb != NULL) {
		struct phy_link_state state = data->state;

		cb(dev, &state, user_data);
	}

	return 0;
}

static int eth_ch390_phy_cfg_link(const struct device *dev, enum phy_link_speed adv_speeds,
				   enum phy_cfg_link_flag flags)
{
	struct eth_ch390_data *data = dev->data;
	const enum phy_link_speed supported_speeds = LINK_HALF_10BASE | LINK_FULL_10BASE |
						      LINK_HALF_100BASE | LINK_FULL_100BASE;
	uint32_t speeds_mask = (uint32_t)adv_speeds;
	uint32_t supported_mask = (uint32_t)supported_speeds;
	uint32_t one_speed_mask;
	uint16_t bmcr;
	uint16_t new_bmcr;
	uint16_t anar;
	uint32_t phy_val;
	int ret;

	if ((flags & ~PHY_FLAG_AUTO_NEGOTIATION_DISABLED) != 0U) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->spi_lock, K_FOREVER);

	if ((flags & PHY_FLAG_AUTO_NEGOTIATION_DISABLED) != 0U) {
		if ((speeds_mask & ~supported_mask) != 0U) {
			ret = -ENOTSUP;
			goto out_unlock;
		}

		one_speed_mask = speeds_mask & supported_mask;
		if (one_speed_mask == 0U || ((one_speed_mask & (one_speed_mask - 1U)) != 0U)) {
			ret = -ENOTSUP;
			goto out_unlock;
		}

		new_bmcr = 0U;
		if ((one_speed_mask & (LINK_HALF_100BASE | LINK_FULL_100BASE)) != 0U) {
			new_bmcr |= CH390_PHY_BMCR_SPEED100;
		}
		if ((one_speed_mask & (LINK_FULL_10BASE | LINK_FULL_100BASE)) != 0U) {
			new_bmcr |= CH390_PHY_BMCR_FULLDPLX;
		}

		ret = eth_ch390_phy_read(dev, CH390_PHY_BMCR, &phy_val);
		if (ret < 0) {
			goto out_unlock;
		}
		bmcr = (uint16_t)phy_val;

		if ((bmcr & (CH390_PHY_BMCR_ANENABLE | CH390_PHY_BMCR_SPEED100 |
			     CH390_PHY_BMCR_FULLDPLX)) == new_bmcr &&
		    data->autoneg_enabled == false) {
			ret = -EALREADY;
			goto out_unlock;
		}

		ret = eth_ch390_phy_write(dev, CH390_PHY_BMCR, new_bmcr);
		if (ret < 0) {
			goto out_unlock;
		}

		data->autoneg_enabled = false;
		(void)eth_ch390_apply_flow_control_policy(dev, false, 0U);
		ret = 0;
		goto out_unlock_with_refresh;
	}

	if (adv_speeds == 0U) {
		adv_speeds = supported_speeds;
		speeds_mask = supported_mask;
	}

	if ((speeds_mask & ~supported_mask) != 0U) {
		ret = -ENOTSUP;
		goto out_unlock;
	}

	anar = CH390_PHY_ANAR_SELECTOR_8023;
	if ((adv_speeds & LINK_HALF_10BASE) != 0U) {
		anar |= CH390_PHY_ANAR_10_HALF;
	}
	if ((adv_speeds & LINK_FULL_10BASE) != 0U) {
		anar |= CH390_PHY_ANAR_10_FULL;
	}
	if ((adv_speeds & LINK_HALF_100BASE) != 0U) {
		anar |= CH390_PHY_ANAR_100_HALF;
	}
	if ((adv_speeds & LINK_FULL_100BASE) != 0U) {
		anar |= CH390_PHY_ANAR_100_FULL;
	}
	if (IS_ENABLED(CONFIG_ETH_CH390_FLOW_CONTROL)) {
		anar |= CH390_PHY_ANAR_PAUSE;
	}

	ret = eth_ch390_phy_write(dev, CH390_PHY_ANAR, anar);
	if (ret < 0) {
		goto out_unlock;
	}

	new_bmcr = CH390_PHY_BMCR_ANENABLE | CH390_PHY_BMCR_ANRESTART;
	ret = eth_ch390_phy_write(dev, CH390_PHY_BMCR, new_bmcr);
	if (ret < 0) {
		goto out_unlock;
	}

	data->autoneg_enabled = true;
	ret = 0;

out_unlock_with_refresh:
	k_mutex_unlock(&data->spi_lock);
	k_msleep(CH390_PHY_RESTART_WAIT_MS);
	eth_ch390_update_link_status(dev);
	return ret;

out_unlock:
	k_mutex_unlock(&data->spi_lock);
	return ret;
}

static int eth_ch390_phy_read(const struct device *dev, uint16_t reg_addr, uint32_t *data)
{
	uint16_t phy_data;
	int ret;

	ret = eth_ch390_spi_write_reg(dev, CH390_EPAR, (uint8_t)(reg_addr & 0x1F) |
				       (CH390_PHY_ADDR << CH390_EPAR_PHY_ADR_SHIFT));
	if (ret < 0) {
		return ret;
	}

	ret = eth_ch390_spi_write_reg(dev, CH390_EPCR, CH390_EPCR_ERPRR | CH390_EPCR_EPOS);
	if (ret < 0) {
		return ret;
	}

	ret = eth_ch390_epcr_poll(dev, CH390_EPCR_POLL_TIMEOUT);
	if (ret < 0) {
		return ret;
	}

	ret = eth_ch390_spi_read_regs(dev, CH390_EPDRL, (void *)&phy_data, 2);
	if (ret) {
		return ret;
	}

	*data = sys_le16_to_cpu(phy_data);

	return 0;
}

static int eth_ch390_phy_write(const struct device *dev, uint16_t reg_addr, uint32_t data)
{
	uint16_t phy_data = sys_cpu_to_le16((uint16_t)data);
	int ret;

	ret = eth_ch390_spi_write_reg(dev, CH390_EPAR, (uint8_t)(reg_addr & 0x1F) |
				       (CH390_PHY_ADDR << CH390_EPAR_PHY_ADR_SHIFT));
	if (ret < 0) {
		return ret;
	}

	ret = eth_ch390_spi_write_regs(dev, CH390_EPDRL, (void *)&phy_data, 2);
	if (ret < 0) {
		return ret;
	}

	ret = eth_ch390_spi_write_reg(dev, CH390_EPCR, CH390_EPCR_ERPRW | CH390_EPCR_EPOS);
	if (ret < 0) {
		return ret;
	}

	return eth_ch390_epcr_poll(dev, K_MSEC(10));
}

static DEVICE_API(ethphy, ethphy_ch390_api) = {
	.get_link = eth_ch390_get_link_state,
	.cfg_link = eth_ch390_phy_cfg_link,
	.link_cb_set = eth_ch390_phy_link_cb_set,
	.read = eth_ch390_phy_read,
	.write = eth_ch390_phy_write,
};

static void eth_ch390_gpio_callback(const struct device *dev,
				     struct gpio_callback *cb,
				     uint32_t pins)
{
	struct eth_ch390_data *data = CONTAINER_OF(cb, struct eth_ch390_data, gpio_cb);

	ARG_UNUSED(dev);
	ARG_UNUSED(pins);

	k_sem_give(&data->int_event);
}

static int eth_ch390_set_mac_addr(const struct device *dev)
{
	const struct eth_ch390_config *config = dev->config;
	struct eth_ch390_data *data = dev->data;
	int ret;

	/* Set MAC address if it is statically defined or if random is set in device tree */
	ret = net_eth_mac_load(&config->mac_cfg, data->mac_addr);
	if (ret == 0) {
		/* Write the MAC address into device */
		return eth_ch390_spi_write_regs(dev, CH390_PAR, data->mac_addr,
						 sizeof(data->mac_addr));
	}

	/* Read the MAC address from CH390_PAR registers */
	ret = eth_ch390_spi_read_regs(dev, CH390_PAR, data->mac_addr, sizeof(data->mac_addr));
	if (ret < 0) {
		return ret;
	}

	/* Check if MAC address is not 00:00:00:00:00:00 */
	if (UNALIGNED_GET((uint32_t *)(data->mac_addr + 0)) == 0x0 &&
	    UNALIGNED_GET((uint16_t *)(data->mac_addr + 4)) == 0x0) {
		return -EINVAL;
	}

	/* Check if MAC address if not multicast address */
	if ((data->mac_addr[0] & 0x1) > 0) {
		return -EINVAL;
	}

	return 0;
}

static int eth_ch390_init(const struct device *dev)
{
	const struct eth_ch390_config *config = dev->config;
	struct eth_ch390_data *data = dev->data;
	int ret;

	data->dev = dev;
	data->promisc_enabled = false;
	data->allmulti_enabled = IS_ENABLED(CONFIG_ETH_CH390_ALLMULTI_DEFAULT);
	data->flow_ctrl_enabled = false;
	data->autoneg_enabled = true;
	memset(data->multicast_hash, 0, sizeof(data->multicast_hash));
	memset(data->multicast_refcnt, 0, sizeof(data->multicast_refcnt));

	k_sem_init(&data->int_event, 0, UINT_MAX);
	k_mutex_init(&data->spi_lock);

	if (!spi_is_ready_dt(&config->spi)) {
		LOG_ERR("%s: SPI master port %s is not ready", dev->name, config->spi.bus->name);
		return -EINVAL;
	}

	if (!gpio_is_ready_dt(&config->gpio_int)) {
		LOG_ERR("%s: GPIO port %s is not ready", dev->name, config->gpio_int.port->name);
		return -EINVAL;
	}

	if (!gpio_is_ready_dt(&config->gpio_reset)) {
		LOG_ERR("%s: GPIO port %s is not ready", dev->name, config->gpio_reset.port->name);
		return -EINVAL;
	}

	ret = gpio_pin_configure_dt(&config->gpio_int, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("%s: Unable to configure GPIO pin (%d)", dev->name, ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&config->gpio_reset, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("%s: Unable to configure reset pin (%d)", dev->name, ret);
		return ret;
	}

	gpio_init_callback(&data->gpio_cb, eth_ch390_gpio_callback, BIT(config->gpio_int.pin));

	ret = gpio_add_callback(config->gpio_int.port, &data->gpio_cb);
	if (ret < 0) {
		LOG_ERR("%s: Unable to add GPIO callback (%d)", dev->name, ret);
		return ret;
	}

	ret = eth_ch390_gpio_irq_set_enabled(dev, false);
	if (ret < 0) {
		LOG_ERR("%s: Unable to disable GPIO interrupt (%d)", dev->name, ret);
		return ret;
	}

	k_mutex_lock(&data->spi_lock, K_FOREVER);

	ret = eth_ch390_hw_reset(dev);
	if (ret < 0) {
		LOG_ERR("%s: Unable to perform hardware reset (%d)", dev->name, ret);
		goto out_spi_unlock;
	}

	ret = eth_ch390_check_id(dev);
	if (ret < 0) {
		goto out_spi_unlock;
	}

	ret = eth_ch390_update_pause_advertisement(dev);
	if (ret < 0) {
		goto out_spi_unlock;
	}

	ret = eth_ch390_set_mac_addr(dev);
	if (ret < 0) {
		LOG_ERR("%s: Unable to initialize MAC address (%d)", dev->name, ret);
		goto out_spi_unlock;
	}

	k_mutex_unlock(&data->spi_lock);

	LOG_INF("%s: Device initialized", dev->name);

	return 0;

out_spi_unlock:
	k_mutex_unlock(&data->spi_lock);
	return ret;
}

#define ETH_CH390_INIT(inst)									\
	DEVICE_DECLARE(eth_ch390_phy_##inst);							\
												\
	static struct eth_ch390_data eth_ch390_data_##inst;					\
												\
	static const struct eth_ch390_config eth_ch390_config_##inst = {			\
		.mac_cfg = NET_ETH_MAC_DT_INST_CONFIG_INIT(inst),				\
		.gpio_int = GPIO_DT_SPEC_INST_GET(inst, int_gpios),				\
		.gpio_reset = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),			\
		.phy_dev = DEVICE_GET(eth_ch390_phy_##inst),					\
		.spi = SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8)),				\
	};											\
												\
	ETH_NET_DEVICE_DT_INST_DEFINE(inst, eth_ch390_init, NULL, &eth_ch390_data_##inst,	\
				      &eth_ch390_config_##inst, CONFIG_ETH_INIT_PRIORITY,	\
				      &eth_ch390_api, NET_ETH_MTU);				\
												\
	DEVICE_DEFINE(eth_ch390_phy_##inst, DEVICE_DT_NAME(DT_DRV_INST(inst)) "_phy",		\
		      NULL, NULL, &eth_ch390_data_##inst, &eth_ch390_config_##inst,		\
		      POST_KERNEL, CONFIG_ETH_INIT_PRIORITY, &ethphy_ch390_api);

DT_INST_FOREACH_STATUS_OKAY(ETH_CH390_INIT)
