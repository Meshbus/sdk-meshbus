/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "input.h"

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>
#include <power/power.h>

LOG_MODULE_REGISTER(mbs_input, CONFIG_MBS_INPUT_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* ZBus Channels                                                              */
/* -------------------------------------------------------------------------- */
ZBUS_CHAN_DEFINE(mbs_input_raw_event_chan, struct mbs_input_event, NULL, NULL, /* user_data */
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_input_action_chan, struct mbs_input_act_event, NULL,
		 NULL, /* user_data */
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

/* -------------------------------------------------------------------------- */
/* Runtime PM And Hardware Apply                                              */
/* -------------------------------------------------------------------------- */
#if defined(CONFIG_PM_DEVICE) && defined(CONFIG_PM_DEVICE_RUNTIME)
struct mbs_input_pm_target {
	const char *name;
	const struct device *dev;
	const struct device *power_domain;
	bool dev_claimed;
	bool pd_claimed;
};

#if defined(CONFIG_MBS_INPUT_BUTTON) && DT_HAS_CHOSEN(meshbus_input_buttons)
#define INPUT_BUTTONS_NODE DT_CHOSEN(meshbus_input_buttons)
#if DT_NODE_HAS_PROP(INPUT_BUTTONS_NODE, power_domains)
static const struct device *const buttons_power_dev =
	DEVICE_DT_GET(DT_PHANDLE_BY_IDX(INPUT_BUTTONS_NODE, power_domains, 0));
#else
static const struct device *const buttons_power_dev = NULL;
#endif
#endif

#if defined(CONFIG_MBS_INPUT_ENCODER) && DT_HAS_CHOSEN(meshbus_input_encoder)
#define INPUT_ENCODER_NODE DT_CHOSEN(meshbus_input_encoder)
#if DT_NODE_HAS_PROP(INPUT_ENCODER_NODE, power_domains)
static const struct device *const encoder_power_dev =
	DEVICE_DT_GET(DT_PHANDLE_BY_IDX(INPUT_ENCODER_NODE, power_domains, 0));
#else
static const struct device *const encoder_power_dev = NULL;
#endif
#endif

#if defined(CONFIG_MBS_INPUT_KEYPAD) && DT_HAS_CHOSEN(meshbus_input_keypad)
#define INPUT_KEYPAD_NODE DT_CHOSEN(meshbus_input_keypad)
#if DT_NODE_HAS_PROP(INPUT_KEYPAD_NODE, power_domains)
static const struct device *const keypad_power_dev =
	DEVICE_DT_GET(DT_PHANDLE_BY_IDX(INPUT_KEYPAD_NODE, power_domains, 0));
#elif DT_NODE_EXISTS(DT_PARENT(INPUT_KEYPAD_NODE)) &&                                              \
	DT_NODE_HAS_PROP(DT_PARENT(INPUT_KEYPAD_NODE), power_domains)
static const struct device *const keypad_power_dev =
	DEVICE_DT_GET(DT_PHANDLE_BY_IDX(DT_PARENT(INPUT_KEYPAD_NODE), power_domains, 0));
#elif DT_NODE_EXISTS(DT_GPARENT(INPUT_KEYPAD_NODE)) &&                                             \
	DT_NODE_HAS_PROP(DT_GPARENT(INPUT_KEYPAD_NODE), power_domains)
static const struct device *const keypad_power_dev =
	DEVICE_DT_GET(DT_PHANDLE_BY_IDX(DT_GPARENT(INPUT_KEYPAD_NODE), power_domains, 0));
#else
static const struct device *const keypad_power_dev = NULL;
#endif
#endif

#if (defined(CONFIG_MBS_INPUT_BUTTON) && DT_HAS_CHOSEN(meshbus_input_buttons)) ||              \
	(defined(CONFIG_MBS_INPUT_ENCODER) && DT_HAS_CHOSEN(meshbus_input_encoder)) ||         \
	(defined(CONFIG_MBS_INPUT_KEYPAD) && DT_HAS_CHOSEN(meshbus_input_keypad))
#define MBS_INPUT_PM_HAS_TARGETS 1
#else
#define MBS_INPUT_PM_HAS_TARGETS 0
#endif

#if MBS_INPUT_PM_HAS_TARGETS
static struct mbs_input_pm_target input_pm_targets[] = {
#if defined(CONFIG_MBS_INPUT_BUTTON) && DT_HAS_CHOSEN(meshbus_input_buttons)
	{
		.name = "buttons",
		.dev = DEVICE_DT_GET(INPUT_BUTTONS_NODE),
		.power_domain = buttons_power_dev,
	},
#endif
#if defined(CONFIG_MBS_INPUT_ENCODER) && DT_HAS_CHOSEN(meshbus_input_encoder)
	{
		.name = "encoder",
		.dev = DEVICE_DT_GET(INPUT_ENCODER_NODE),
		.power_domain = encoder_power_dev,
	},
#endif
#if defined(CONFIG_MBS_INPUT_KEYPAD) && DT_HAS_CHOSEN(meshbus_input_keypad)
	{
		.name = "keypad",
		.dev = DEVICE_DT_GET(INPUT_KEYPAD_NODE),
		.power_domain = keypad_power_dev,
	},
#endif
};
#else
static struct mbs_input_pm_target input_pm_targets[] = {
	{
		.name = NULL,
		.dev = NULL,
		.power_domain = NULL,
	},
};
#endif

static int mbs_input_runtime_pm_claim(const struct device *dev, const char *name, bool *claimed)
{
	if ((dev == NULL) || (name == NULL) || (claimed == NULL)) {
		return 0;
	}

	if (*claimed) {
		return 0;
	}

	if (!device_is_ready(dev)) {
		LOG_WRN("Input %s device not ready", name);
		return -ENODEV;
	}

	if (!pm_device_runtime_is_enabled(dev)) {
		int en_rc = pm_device_runtime_enable(dev);

		if (en_rc != 0 && en_rc != -ENOTSUP && en_rc != -EBUSY) {
			LOG_WRN("Input %s runtime enable failed: %d", name, en_rc);
		}
	}

	int get_rc = pm_device_runtime_get(dev);
	if (get_rc == -ENOTSUP) {
		get_rc = 0;
	}
	if (get_rc != 0) {
		LOG_WRN("Input %s runtime get failed: %d", name, get_rc);
		return get_rc;
	}

	*claimed = true;

	return 0;
}

static int mbs_input_runtime_pm_release(const struct device *dev, const char *name,
					    bool *claimed)
{
	if ((dev == NULL) || (name == NULL) || (claimed == NULL)) {
		return 0;
	}

	if (!*claimed) {
		return 0;
	}

	int put_rc = pm_device_runtime_put(dev);
	if (put_rc == -ENOTSUP) {
		put_rc = 0;
	}
	if (put_rc != 0 && put_rc != -EALREADY) {
		LOG_WRN("Input %s runtime put failed (%s): %d", name, dev->name, put_rc);
		return put_rc;
	}

	*claimed = false;

	return 0;
}

static size_t mbs_input_pm_pd_owner_idx(size_t idx)
{
	const struct device *pd = input_pm_targets[idx].power_domain;

	if (pd == NULL) {
		return SIZE_MAX;
	}

	for (size_t i = 0; i < idx; i++) {
		if (input_pm_targets[i].power_domain == pd) {
			return i;
		}
	}

	return SIZE_MAX;
}

static size_t mbs_input_pm_claim_power_domains(void)
{
	size_t claimed = 0U;

	for (size_t i = 0; i < ARRAY_SIZE(input_pm_targets); i++) {
		size_t owner = mbs_input_pm_pd_owner_idx(i);
		if (owner != SIZE_MAX) {
			input_pm_targets[i].pd_claimed = input_pm_targets[owner].pd_claimed;
			continue;
		}

		const struct device *pd = input_pm_targets[i].power_domain;
		if (pd == NULL) {
			continue;
		}

		bool was_claimed = input_pm_targets[i].pd_claimed;
		(void)mbs_input_runtime_pm_claim(pd, input_pm_targets[i].name,
						     &input_pm_targets[i].pd_claimed);
		if (!was_claimed && input_pm_targets[i].pd_claimed) {
			claimed++;
		}
	}

	return claimed;
}

static size_t mbs_input_pm_claim_devices(void)
{
	size_t claimed = 0U;

	for (size_t i = 0; i < ARRAY_SIZE(input_pm_targets); i++) {
		bool was_claimed = input_pm_targets[i].dev_claimed;
		(void)mbs_input_runtime_pm_claim(input_pm_targets[i].dev,
						     input_pm_targets[i].name,
						     &input_pm_targets[i].dev_claimed);
		if (!was_claimed && input_pm_targets[i].dev_claimed) {
			claimed++;
		}
	}

	return claimed;
}

static size_t mbs_input_pm_release_power_domains(void)
{
	size_t released = 0U;

	for (size_t i = 0; i < ARRAY_SIZE(input_pm_targets); i++) {
		size_t owner = mbs_input_pm_pd_owner_idx(i);
		if (owner != SIZE_MAX) {
			continue;
		}

		const struct device *pd = input_pm_targets[i].power_domain;
		if (pd == NULL) {
			continue;
		}
		if (!input_pm_targets[i].pd_claimed) {
			continue;
		}

		bool was_claimed = input_pm_targets[i].pd_claimed;
		(void)mbs_input_runtime_pm_release(pd, input_pm_targets[i].name,
						       &input_pm_targets[i].pd_claimed);
		if (was_claimed && !input_pm_targets[i].pd_claimed) {
			/* Clear duplicates (all targets sharing the same PD). */
			for (size_t j = 0; j < ARRAY_SIZE(input_pm_targets); j++) {
				if (input_pm_targets[j].power_domain == pd) {
					input_pm_targets[j].pd_claimed = false;
				}
			}
			released++;
		}
	}

	return released;
}

static size_t mbs_input_pm_release_devices(void)
{
	size_t released = 0U;

	for (size_t i = 0; i < ARRAY_SIZE(input_pm_targets); i++) {
		bool was_claimed = input_pm_targets[i].dev_claimed;
		(void)mbs_input_runtime_pm_release(input_pm_targets[i].dev,
						       input_pm_targets[i].name,
						       &input_pm_targets[i].dev_claimed);
		if (was_claimed && !input_pm_targets[i].dev_claimed) {
			released++;
		}
	}

	return released;
}

static size_t mbs_input_pm_target_count(bool count_power_domain)
{
	size_t count = 0U;

	for (size_t i = 0; i < ARRAY_SIZE(input_pm_targets); i++) {
		if (count_power_domain) {
			if (input_pm_targets[i].power_domain != NULL &&
			    mbs_input_pm_pd_owner_idx(i) == SIZE_MAX) {
				count++;
			}
		} else if (input_pm_targets[i].dev != NULL) {
			count++;
		}
	}

	return count;
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

/* Ensure input power rails are ON before input drivers run POST_KERNEL init. */
#define MBS_INPUT_PD_BOOTSTRAP_INIT_PRIORITY 80
static int mbs_input_pd_bootstrap_init(void)
{
	size_t pd_claimed = mbs_input_pm_claim_power_domains();
	size_t pd_targets = mbs_input_pm_target_count(true);

	LOG_DBG("Input power-domain bootstrap: claimed=%u targets=%u", (unsigned int)pd_claimed,
		(unsigned int)pd_targets);

	return 0;
}

SYS_INIT(mbs_input_pd_bootstrap_init, POST_KERNEL, MBS_INPUT_PD_BOOTSTRAP_INIT_PRIORITY);
#endif

static int mbs_input_pm_init(void)
{
#if defined(CONFIG_PM_DEVICE) && defined(CONFIG_PM_DEVICE_RUNTIME)
	size_t pd_claimed = mbs_input_pm_claim_power_domains();
	size_t dev_claimed = mbs_input_pm_claim_devices();
	size_t pd_targets = mbs_input_pm_target_count(true);
	size_t dev_targets = mbs_input_pm_target_count(false);

	LOG_INF("Input runtime-PM init: devices=%u/%u power-domains=%u/%u",
		(unsigned int)dev_claimed, (unsigned int)dev_targets, (unsigned int)pd_claimed,
		(unsigned int)pd_targets);
#endif

	return 0;
}

SYS_INIT(mbs_input_pm_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/* -------------------------------------------------------------------------- */
/* Power Callback                                                             */
/* -------------------------------------------------------------------------- */
static void mbs_power_input_cb(enum mbs_power_action action, void *user_data)
{
	ARG_UNUSED(user_data);

	if (action != MBS_POWER_ACTION_SHUTDOWN && action != MBS_POWER_ACTION_REBOOT) {
		return;
	}

#if defined(CONFIG_PM_DEVICE) && defined(CONFIG_PM_DEVICE_RUNTIME)
	size_t dev_released = mbs_input_pm_release_devices();
	size_t pd_released = mbs_input_pm_release_power_domains();

	if (dev_released > 0U || pd_released > 0U) {
		LOG_DBG("Input runtime-PM released: devices=%u power-domains=%u",
			(unsigned int)dev_released, (unsigned int)pd_released);
	}
#endif

	LOG_INF("Input stopped");
}
MBS_POWER_ACTION_CALLBACK_DEFINE(mbs_power_input_cb, NULL);

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

int mbs_input_key_event_publish(uint8_t type, uint16_t code, int32_t value)
{
	struct mbs_input_event msg = {
		.type = type,
		.code = code,
		.value = value,
	};

	int ret = zbus_chan_pub(&mbs_input_raw_event_chan, &msg, K_NO_WAIT);
	LOG_DBG("Raw event publish: type=%u, code=0x%04x, value=%d", msg.type, msg.code, msg.value);
	return ret;
}

int mbs_input_action_event_publish(uint8_t type, uint16_t code, uint8_t action)
{
	struct mbs_input_act_event msg = {
		.type = type,
		.code = code,
		.action = action,
	};

	int ret = zbus_chan_pub(&mbs_input_action_chan, &msg, K_NO_WAIT);
	LOG_DBG("Action event publish: type=%u, code=0x%04x, action=%u", msg.type, msg.code,
		msg.action);
	return ret;
}
