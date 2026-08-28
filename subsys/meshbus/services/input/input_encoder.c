/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "input.h"
#include "zephyr/meshbus/input.h"

LOG_MODULE_REGISTER(meshbus_input_encoder, CONFIG_MESHBUS_INPUT_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* Devices                                                                    */
/* -------------------------------------------------------------------------- */
#if DT_HAS_CHOSEN(meshbus_input_encoder)
static const struct device *const encoder_dev = DEVICE_DT_GET(DT_CHOSEN(meshbus_input_encoder));
#else
static const struct device *const encoder_dev = NULL;
#endif

#if defined(CONFIG_MESHBUS_INPUT_ENCODER) && DT_HAS_CHOSEN(meshbus_input_encoder)

/* -------------------------------------------------------------------------- */
/* Defaults And State                                                         */
/* -------------------------------------------------------------------------- */
struct input_encoder_state {
	int32_t acc; /**< Accumulated rotation value */
};
static struct input_encoder_state encoder_state;

/* -------------------------------------------------------------------------- */
/* Validation And Helpers                                                     */
/* -------------------------------------------------------------------------- */
static void input_encoder_event_handler(struct input_event *evt)
{
	if (evt->type != INPUT_EV_REL) {
		return;
	}

	int32_t step = CONFIG_MESHBUS_INPUT_ENCODER_STEP_THRESHOLD;

	if (step < 1) {
		step = 1;
	}

	if (evt->value == 0) {
		return;
	}

	/* Publish raw event for immediate UI feedback */
	LOG_DBG("Publishing raw input event: type=%u code=0x%04x value=%d", evt->type, evt->code,
		evt->value);
	meshbus_input_key_event_publish(evt->type, evt->code, evt->value);

	encoder_state.acc += evt->value;

	/* Publish CW events */
	while (encoder_state.acc >= step) {
		encoder_state.acc -= step;
		LOG_DBG("Encoder 0x%04x scroll CW", evt->code);
		meshbus_input_action_event_publish(INPUT_EV_REL, evt->code, INPUT_ACT_SCROLL_CW);
	}

	/* Publish CCW events */
	while (encoder_state.acc <= -step) {
		encoder_state.acc += step;
		LOG_DBG("Encoder 0x%04x scroll CCW", evt->code);
		meshbus_input_action_event_publish(INPUT_EV_REL, evt->code, INPUT_ACT_SCROLL_CCW);
	}
}

static void input_encoder_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);
	input_encoder_event_handler(evt);
}

INPUT_CALLBACK_DEFINE_NAMED(DEVICE_DT_GET(DT_CHOSEN(meshbus_input_encoder)), input_encoder_cb, NULL,
			    meshbus_encoder);

#endif /* CONFIG_MESHBUS_INPUT_ENCODER && DT_HAS_CHOSEN */

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */
static int meshbus_input_encoder_init(void)
{
#if defined(CONFIG_MESHBUS_INPUT_ENCODER) && DT_HAS_CHOSEN(meshbus_input_encoder)
	if (encoder_dev == NULL) {
		LOG_WRN("No encoder device configured");
	} else if (!device_is_ready(encoder_dev)) {
		LOG_ERR("Encoder device not ready");
	} else {
		LOG_INF("Encoder input initialized");
	}
#endif

	return 0;
}

SYS_INIT(meshbus_input_encoder_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
