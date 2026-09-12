/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT fobe_test_power_domain

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/pm/device.h>

static int fake_power_domain_pm_action(const struct device *dev, enum pm_device_action action)
{
	ARG_UNUSED(dev);

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
	case PM_DEVICE_ACTION_SUSPEND:
	case PM_DEVICE_ACTION_TURN_ON:
	case PM_DEVICE_ACTION_TURN_OFF:
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int fake_power_domain_init(const struct device *dev)
{
	return pm_device_driver_init(dev, fake_power_domain_pm_action);
}

#define FAKE_POWER_DOMAIN_DEFINE(inst)                                                             \
	PM_DEVICE_DT_INST_DEFINE(inst, fake_power_domain_pm_action);                               \
	DEVICE_DT_INST_DEFINE(inst, fake_power_domain_init, PM_DEVICE_DT_INST_GET(inst), NULL,     \
			      NULL, POST_KERNEL, CONFIG_POWER_DOMAIN_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(FAKE_POWER_DOMAIN_DEFINE)
