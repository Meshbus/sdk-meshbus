/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (c) 2026 FoBE Studio
 */

#define DT_DRV_COMPAT zephyr_bt_hci_test

#include <errno.h>

#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/device.h>
#include <zephyr/drivers/bluetooth.h>
#include <zephyr/net_buf.h>

struct fake_bt_hci_data {
	struct bt_hci_driver_data common;
};

static int fake_bt_hci_open(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static int fake_bt_hci_send(const struct device *dev, struct net_buf *buf)
{
	ARG_UNUSED(dev);
	net_buf_unref(buf);
	return -ENOTSUP;
}

static DEVICE_API(bt_hci, fake_bt_hci_api) = {
	.open = fake_bt_hci_open,
	.close = NULL,
	.send = fake_bt_hci_send,
};

#define FAKE_BT_HCI_DEFINE(inst)                                                                  \
	static struct fake_bt_hci_data fake_bt_hci_data_##inst;                                   \
	DEVICE_DT_INST_DEFINE(inst, NULL, NULL, &fake_bt_hci_data_##inst, NULL, POST_KERNEL,      \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &fake_bt_hci_api)

DT_INST_FOREACH_STATUS_OKAY(FAKE_BT_HCI_DEFINE)
