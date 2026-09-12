/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/zbus/zbus.h>

#include <power/power.h>

#if IS_ENABLED(CONFIG_MBS_INDICATOR_INPUT_FEEDBACK)
#include <zephyr/dt-bindings/input/input-event-codes.h>
#if IS_ENABLED(CONFIG_MBS_DESKTOP)
#include <desktop/desktop.h>
#endif
#include <input/input.h>
#endif
#if IS_ENABLED(CONFIG_MBS_INDICATOR_MESSAGE_FEEDBACK)
#include <message/message.h>
#endif

#include <stddef.h>
#include <stdlib.h>

#include "mbs_settings_internal.h"
#include "indicator_buzzer.h"
#include "indicator_buzzer_tone.h"
#include "indicator_light.h"

LOG_MODULE_REGISTER(mbs_indicator, CONFIG_MBS_INDICATOR_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* ZBus Channels                                                              */
/* -------------------------------------------------------------------------- */

static bool indicator_light_play_validator(const void *msg, size_t msg_size);
static bool indicator_buzzer_play_validator(const void *msg, size_t msg_size);
static void indicator_light_play_listener_cb(const struct zbus_channel *chan);
static void indicator_buzzer_play_listener_cb(const struct zbus_channel *chan);
#if IS_ENABLED(CONFIG_MBS_INDICATOR_INPUT_FEEDBACK)
static void indicator_input_action_listener_cb(const struct zbus_channel *chan);
static void indicator_input_feedback_work_handler(struct k_work *work);
#endif
#if IS_ENABLED(CONFIG_MBS_INDICATOR_MESSAGE_FEEDBACK)
static void indicator_message_response_listener_cb(const struct zbus_channel *chan);
static void indicator_message_feedback_work_handler(struct k_work *work);
#endif

ZBUS_CHAN_DEFINE(mbs_indicator_light_play_chan,
		 struct mbs_indicator_light_play_event,
		 indicator_light_play_validator, NULL,
		 ZBUS_OBSERVERS(mbs_indicator_light_play_listener), ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_indicator_buzzer_play_chan,
		 struct mbs_indicator_buzzer_play_event,
		 indicator_buzzer_play_validator, NULL,
		 ZBUS_OBSERVERS(mbs_indicator_buzzer_play_listener), ZBUS_MSG_INIT(0));

ZBUS_LISTENER_DEFINE(mbs_indicator_light_play_listener, indicator_light_play_listener_cb);
ZBUS_LISTENER_DEFINE(mbs_indicator_buzzer_play_listener, indicator_buzzer_play_listener_cb);
#if IS_ENABLED(CONFIG_MBS_INDICATOR_INPUT_FEEDBACK)
ZBUS_LISTENER_DEFINE(mbs_indicator_input_action_listener,
		     indicator_input_action_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_input_action_chan, mbs_indicator_input_action_listener, 2);
#endif
#if IS_ENABLED(CONFIG_MBS_INDICATOR_MESSAGE_FEEDBACK)
ZBUS_LISTENER_DEFINE(mbs_indicator_message_response_listener,
		     indicator_message_response_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_message_response_chan, mbs_indicator_message_response_listener, 2);
#endif

/* -------------------------------------------------------------------------- */
/* Defaults And State                                                         */
/* -------------------------------------------------------------------------- */
#define MBS_INDICATOR_SETTINGS_SUBTREE    "meshbus/indicator"
#define MBS_INDICATOR_SETTINGS_KEY_CONFIG "config"

#define MBS_INDICATOR_CONFIG_DEFAULTS                                                          \
		{                                                                                          \
			.light_enabled = true,                                                             \
			.buzzer_enabled = true,                                                            \
			.has_light_feedback = true,                                                        \
			.light_feedback = {                                                                \
				.heartbeat_enabled =                                                       \
					CONFIG_MBS_INDICATOR_DEFAULT_LIGHT_HEARTBEAT,                  \
			},                                                                                 \
			.has_buzzer_feedback = true,                                                       \
			.buzzer_feedback = {                                                               \
			.direct_message_enabled =                                                  \
				CONFIG_MBS_INDICATOR_DEFAULT_BUZZER_DIRECT_MESSAGE,            \
			.channel_message_enabled =                                                 \
				CONFIG_MBS_INDICATOR_DEFAULT_BUZZER_CHANNEL_MESSAGE,           \
			.system_enabled = CONFIG_MBS_INDICATOR_DEFAULT_BUZZER_SYSTEM,          \
		},                                                                                 \
	}

static mbs_indicator_config indicator_cfg = MBS_INDICATOR_CONFIG_DEFAULTS;
static K_MUTEX_DEFINE(settings_apply_mutex);
static K_MUTEX_DEFINE(settings_mutex);
static bool settings_initial_apply;
static struct k_work_delayable settings_persistence_work;
static mbs_indicator_config settings_load_cfg = MBS_INDICATOR_CONFIG_DEFAULTS;
static struct mbs_settings_blob_load_state settings_load_state;
/* Warn once when buzzer hardware is unavailable to avoid log spam. */
static atomic_t buzzer_unavailable_warned = ATOMIC_INIT(0);
static K_MUTEX_DEFINE(zbus_buzzer_mutex);
static struct indicator_buzzer_note zbus_buzzer_note;
static const struct indicator_buzzer_melody zbus_buzzer_melody = {
	.notes = &zbus_buzzer_note,
	.length = 1,
};
#if IS_ENABLED(CONFIG_MBS_INDICATOR_MESSAGE_FEEDBACK)
static struct k_spinlock message_feedback_lock;
static bool message_feedback_work_active;
static bool message_feedback_pending;
static bool message_feedback_direct_pending;
#endif
#if IS_ENABLED(CONFIG_MBS_INDICATOR_INPUT_FEEDBACK)
static struct k_spinlock input_feedback_lock;
static const struct indicator_buzzer_melody *input_feedback_pending_melody;
K_WORK_DEFINE(input_feedback_work, indicator_input_feedback_work_handler);
#endif
#if IS_ENABLED(CONFIG_MBS_INDICATOR_BUZZER)
static struct k_work_delayable startup_buzzer_work;
#endif
#if IS_ENABLED(CONFIG_MBS_INDICATOR_MESSAGE_FEEDBACK)
K_WORK_DEFINE(message_feedback_work, indicator_message_feedback_work_handler);
#endif

MBS_SETTINGS_BLOB_SCHEMA_DEFINE(indicator_settings_schema, MBS_INDICATOR_SETTINGS_SUBTREE,
			       MBS_INDICATOR_SETTINGS_KEY_CONFIG,
			       meshbus_IndicatorConfig, mbs_indicator_config);

/* Optional: power-domain device used to keep buzzer supply stable while enabled. */
#if DT_HAS_CHOSEN(meshbus_indicator_buzzer_power)
#define INDICATOR_BUZZER_POWER_NODE DT_CHOSEN(meshbus_indicator_buzzer_power)
static const struct device *const buzzer_power = DEVICE_DT_GET(INDICATOR_BUZZER_POWER_NODE);
static K_MUTEX_DEFINE(buzzer_power_mutex);
static bool buzzer_power_held;
#define INDICATOR_BUZZER_HAS_POWER 1
#else
#define INDICATOR_BUZZER_HAS_POWER 0
#endif

/* Optional: power-domain device used to keep light supply stable while enabled. */
#if DT_HAS_CHOSEN(meshbus_indicator_light_power)
#define INDICATOR_LIGHT_POWER_NODE DT_CHOSEN(meshbus_indicator_light_power)
static const struct device *const light_power = DEVICE_DT_GET(INDICATOR_LIGHT_POWER_NODE);
static K_MUTEX_DEFINE(light_power_mutex);
static bool light_power_held;
#define INDICATOR_LIGHT_HAS_POWER 1
#else
#define INDICATOR_LIGHT_HAS_POWER 0
#endif

/* -------------------------------------------------------------------------- */
/* Runtime PM And Hardware Apply                                              */
/* -------------------------------------------------------------------------- */
static void indicator_buzzer_power_apply(bool enabled)
{
#if INDICATOR_BUZZER_HAS_POWER && IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME)
	bool want_on = enabled;

	/* If there's no buzzer device, don't keep its (shared) power-domain on. */
	if (want_on && !indicator_buzzer_is_ready()) {
		want_on = false;
	}

	k_mutex_lock(&buzzer_power_mutex, K_FOREVER);

	if (want_on) {
		if (!buzzer_power_held) {
			if (device_is_ready(buzzer_power)) {
				/* Best-effort: ensure runtime PM is enabled so get() resumes the
				 * domain. */
				if (!pm_device_runtime_is_enabled(buzzer_power)) {
					(void)pm_device_runtime_enable(buzzer_power);
				}

				if (pm_device_runtime_get(buzzer_power) == 0) {
					buzzer_power_held = true;
				}
			}
		}
	} else {
		if (buzzer_power_held) {
			(void)pm_device_runtime_put(buzzer_power);
			buzzer_power_held = false;
		}
	}

	k_mutex_unlock(&buzzer_power_mutex);
#else
	ARG_UNUSED(enabled);
#endif
}

static void indicator_light_power_apply(bool enabled)
{
#if INDICATOR_LIGHT_HAS_POWER && IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME)
	bool want_on = enabled;

	/* If there's no light device, don't keep its (shared) power-domain on. */
	if (want_on && !indicator_light_is_ready()) {
		want_on = false;
	}

	k_mutex_lock(&light_power_mutex, K_FOREVER);

	if (want_on) {
		if (!light_power_held) {
			if (device_is_ready(light_power)) {
				/* Best-effort: ensure runtime PM is enabled so get() resumes the
				 * domain. */
				if (!pm_device_runtime_is_enabled(light_power)) {
					(void)pm_device_runtime_enable(light_power);
				}

				if (pm_device_runtime_get(light_power) == 0) {
					light_power_held = true;
				}
			}
		}
	} else {
		if (light_power_held) {
			(void)pm_device_runtime_put(light_power);
			light_power_held = false;
		}
	}

	k_mutex_unlock(&light_power_mutex);
#else
	ARG_UNUSED(enabled);
#endif
}

static void indicator_light_apply_cfg(const mbs_indicator_config *cfg)
{
	if (cfg == NULL) {
		return;
	}

	if (!cfg->light_enabled) {
		indicator_light_set_idle_enabled(false);
		indicator_light_set_enabled(false);
		return;
	}

	indicator_light_set_enabled(true);
	indicator_light_set_idle_enabled(cfg->has_light_feedback &&
					 cfg->light_feedback.heartbeat_enabled);
}

/* -------------------------------------------------------------------------- */
/* Validation And Helpers                                                     */
/* -------------------------------------------------------------------------- */
static bool indicator_buzzer_source_allowed(enum indicator_buzzer_source source)
{
	k_mutex_lock(&settings_mutex, K_FOREVER);
	bool enabled = indicator_cfg.buzzer_enabled;
	bool has_feedback = indicator_cfg.has_buzzer_feedback;
	mbs_indicator_buzzer_feedback feedback = indicator_cfg.buzzer_feedback;
	k_mutex_unlock(&settings_mutex);

	if (!enabled || !has_feedback) {
		return false;
	}

	switch (source) {
	case INDICATOR_SOURCE_SYSTEM:
		return feedback.system_enabled;
	case INDICATOR_SOURCE_DIRECT_MSG:
		return feedback.direct_message_enabled;
	case INDICATOR_SOURCE_CHANNEL_MSG:
		return feedback.channel_message_enabled;
	default:
		return false;
	}
}

static bool indicator_buzzer_available(void)
{
	if (indicator_buzzer_is_ready()) {
		return true;
	}

	if (atomic_cas(&buzzer_unavailable_warned, 0, 1)) {
		LOG_WRN("Indicator buzzer unavailable (missing hardware or disabled)");
	}

	return false;
}

static bool indicator_buzzer_feedback_equal(const mbs_indicator_config *lhs,
					    const mbs_indicator_config *rhs)
{
	return lhs->has_buzzer_feedback == rhs->has_buzzer_feedback &&
	       lhs->buzzer_feedback.direct_message_enabled ==
		       rhs->buzzer_feedback.direct_message_enabled &&
	       lhs->buzzer_feedback.channel_message_enabled ==
		       rhs->buzzer_feedback.channel_message_enabled &&
	       lhs->buzzer_feedback.system_enabled == rhs->buzzer_feedback.system_enabled;
}

static bool indicator_light_feedback_equal(const mbs_indicator_config *lhs,
					   const mbs_indicator_config *rhs)
{
	return lhs->has_light_feedback == rhs->has_light_feedback &&
	       lhs->light_feedback.heartbeat_enabled == rhs->light_feedback.heartbeat_enabled;
}

#if IS_ENABLED(CONFIG_MBS_INDICATOR_INPUT_FEEDBACK)
static bool indicator_input_is_ok_code(uint16_t code)
{
	return code == INPUT_BTN_SELECT || code == INPUT_KEY_ENTER || code == INPUT_KEY_KPENTER ||
	       code == INPUT_KEY_KPDOT;
}

static bool indicator_input_is_back_code(uint16_t code)
{
	if (code == INPUT_BTN_BACK || code == INPUT_KEY_ESC || code == INPUT_KEY_BACKSPACE ||
	    code == INPUT_KEY_KPASTERISK) {
		return true;
	}

#if defined(INPUT_KEY_BACK)
	if (code == INPUT_KEY_BACK) {
		return true;
	}
#endif

	return false;
}

static const struct indicator_buzzer_melody *indicator_input_key_melody(uint16_t code,
									uint8_t action)
{
	bool is_ok = indicator_input_is_ok_code(code);
	bool is_back = indicator_input_is_back_code(code);

	switch (action) {
	case INPUT_ACT_KEY_SHORT:
		if (is_ok) {
			return &indicator_buzzer_input_ok_short_tone;
		}
		if (is_back) {
			return &indicator_buzzer_input_back_short_tone;
		}
		return &indicator_buzzer_input_short_tone;
	case INPUT_ACT_KEY_LONG:
		if (is_ok) {
			return &indicator_buzzer_input_ok_long_tone;
		}
		if (is_back) {
			return &indicator_buzzer_input_back_long_tone;
		}
		return &indicator_buzzer_input_long_tone;
	default:
		return NULL;
	}
}
#endif

static bool indicator_light_play_validator(const void *msg, size_t msg_size)
{
	return msg != NULL && msg_size == sizeof(struct mbs_indicator_light_play_event);
}

static bool indicator_buzzer_play_validator(const void *msg, size_t msg_size)
{
	const struct mbs_indicator_buzzer_play_event *event = msg;

	if (event == NULL || msg_size != sizeof(*event)) {
		return false;
	}

	if (event->duration_ms == 0U) {
		return false;
	}

	return event->source >= INDICATOR_SOURCE_SYSTEM &&
	       event->source <= INDICATOR_SOURCE_CHANNEL_MSG;
}

static void indicator_light_play_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_indicator_light_play_event *event;
	int rc;

	if (chan != &mbs_indicator_light_play_chan) {
		return;
	}

	event = zbus_chan_const_msg(chan);
	if (event == NULL) {
		return;
	}

	rc = mbs_indicator_light_play(event->on_duration_ms, event->off_duration_ms,
					  event->count);
	if (rc != 0) {
		LOG_WRN("Indicator light zbus request failed: %d", rc);
	}
}

static void indicator_buzzer_play_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_indicator_buzzer_play_event *event;
	int rc;

	if (chan != &mbs_indicator_buzzer_play_chan) {
		return;
	}

	event = zbus_chan_const_msg(chan);
	if (event == NULL) {
		return;
	}

	k_mutex_lock(&zbus_buzzer_mutex, K_FOREVER);
	zbus_buzzer_note.freq_hz = event->freq_hz;
	zbus_buzzer_note.duration_ms = event->duration_ms;
	rc = mbs_indicator_buzzer_play(event->source, &zbus_buzzer_melody);
	k_mutex_unlock(&zbus_buzzer_mutex);

	if (rc != 0) {
		LOG_WRN("Indicator buzzer zbus request failed: %d", rc);
	}
}

#if IS_ENABLED(CONFIG_MBS_INDICATOR_INPUT_FEEDBACK)
static bool indicator_input_feedback_suppressed(void)
{
#if IS_ENABLED(CONFIG_MBS_DESKTOP)
	return mbs_desktop_external_app_is_active();
#else
	return false;
#endif
}

static void indicator_input_feedback_work_handler(struct k_work *work)
{
	const struct indicator_buzzer_melody *melody;
	k_spinlock_key_t key;
	int rc;

	ARG_UNUSED(work);

	key = k_spin_lock(&input_feedback_lock);
	melody = input_feedback_pending_melody;
	input_feedback_pending_melody = NULL;
	k_spin_unlock(&input_feedback_lock, key);

	if (melody == NULL) {
		return;
	}

	rc = mbs_indicator_buzzer_play(INDICATOR_SOURCE_SYSTEM, melody);
	if (rc != 0 && rc != -EACCES) {
		LOG_DBG("Indicator input feedback skipped: %d", rc);
	}
}

static void indicator_input_action_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_input_act_event *event = zbus_chan_const_msg(chan);
	const struct indicator_buzzer_melody *melody = NULL;
	k_spinlock_key_t key;

	if (chan != &mbs_input_action_chan || event == NULL) {
		return;
	}

	if (indicator_input_feedback_suppressed()) {
		return;
	}

	switch (event->action) {
	case INPUT_ACT_KEY_SHORT:
		if (event->type != INPUT_EV_KEY) {
			return;
		}
		melody = indicator_input_key_melody(event->code, event->action);
		break;
	case INPUT_ACT_SCROLL_CW:
	case INPUT_ACT_SCROLL_CCW:
		if (event->type != INPUT_EV_REL) {
			return;
		}
		melody = &indicator_buzzer_input_short_tone;
		break;
	case INPUT_ACT_KEY_LONG:
		if (event->type != INPUT_EV_KEY) {
			return;
		}
		melody = indicator_input_key_melody(event->code, event->action);
		break;
	default:
		return;
	}

	if (melody == NULL) {
		return;
	}

	key = k_spin_lock(&input_feedback_lock);
	input_feedback_pending_melody = melody;
	k_spin_unlock(&input_feedback_lock, key);

	(void)k_work_submit(&input_feedback_work);
}
#endif

#if IS_ENABLED(CONFIG_MBS_INDICATOR_MESSAGE_FEEDBACK)
static bool indicator_message_response_source(
	const struct mbs_message_response_event *event,
	enum indicator_buzzer_source *source)
{
	if (event == NULL || source == NULL) {
		return false;
	}

	switch (event->type) {
	case meshbus_MessageContent_MessageType_RECEIVE_NODE:
		*source = INDICATOR_SOURCE_DIRECT_MSG;
		return true;
	case meshbus_MessageContent_MessageType_RECEIVE_CHANNEL:
		*source = INDICATOR_SOURCE_CHANNEL_MSG;
		return true;
	default:
		return false;
	}
}

static void indicator_message_response_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_message_response_event *event;
	enum indicator_buzzer_source source;
	k_spinlock_key_t key;
	bool submit_work = false;
	int rc;

	if (chan != &mbs_message_response_chan) {
		return;
	}

	event = zbus_chan_const_msg(chan);
	if (!indicator_message_response_source(event, &source)) {
		return;
	}

	key = k_spin_lock(&message_feedback_lock);
	message_feedback_pending = true;
	if (source == INDICATOR_SOURCE_DIRECT_MSG) {
		message_feedback_direct_pending = true;
	}
	if (!message_feedback_work_active) {
		message_feedback_work_active = true;
		submit_work = true;
	}
	k_spin_unlock(&message_feedback_lock, key);

	if (!submit_work) {
		return;
	}

	rc = k_work_submit(&message_feedback_work);
	if (rc < 0 && rc != -EBUSY) {
		key = k_spin_lock(&message_feedback_lock);
		message_feedback_work_active = false;
		k_spin_unlock(&message_feedback_lock, key);
		LOG_WRN("Schedule indicator message feedback failed: rc=%d", rc);
	}
}

static void indicator_message_feedback_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	for (;;) {
		enum indicator_buzzer_source source;
		k_spinlock_key_t key;
		int rc;

		key = k_spin_lock(&message_feedback_lock);
		if (!message_feedback_pending) {
			message_feedback_work_active = false;
			k_spin_unlock(&message_feedback_lock, key);
			return;
		}

		source = message_feedback_direct_pending ? INDICATOR_SOURCE_DIRECT_MSG :
							   INDICATOR_SOURCE_CHANNEL_MSG;
		message_feedback_pending = false;
		message_feedback_direct_pending = false;
		k_spin_unlock(&message_feedback_lock, key);

		rc = mbs_indicator_buzzer_play(source, &indicator_buzzer_message_feedback_tone);
		if (rc != 0 && rc != -EACCES) {
			LOG_DBG("Indicator message feedback skipped: rc=%d", rc);
		}
	}
}
#endif

#if IS_ENABLED(CONFIG_MBS_INDICATOR_BUZZER)
static void startup_buzzer_work_handler(struct k_work *work)
{
	int rc;

	ARG_UNUSED(work);

	rc = mbs_indicator_buzzer_play(INDICATOR_SOURCE_SYSTEM,
					   &indicator_buzzer_startup_tone);
	if (rc != 0 && rc != -EACCES) {
		LOG_DBG("Indicator startup feedback skipped: %d", rc);
	}
}
#endif

static int indicator_config_validate(const mbs_indicator_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	if (!cfg->has_light_feedback) {
		LOG_ERR("Missing light feedback config");
		return -EINVAL;
	}

	if (!cfg->has_buzzer_feedback) {
		LOG_ERR("Missing buzzer feedback config");
		return -EINVAL;
	}

	return 0;
}

/* -------------------------------------------------------------------------- */
/* Settings Schema And Apply                                                  */
/* -------------------------------------------------------------------------- */
static int settings_handler_apply(const mbs_indicator_config *cfg, bool persistence, bool force)
{
	mbs_indicator_config prev_cfg;
	bool prev_buzzer_enabled;
	bool prev_light_enabled;

	if (cfg == NULL) {
		return -EINVAL;
	}

	int rc = indicator_config_validate(cfg);
	if (rc != 0) {
		return rc;
	}

	k_mutex_lock(&settings_apply_mutex, K_FOREVER);
	k_mutex_lock(&settings_mutex, K_FOREVER);
	prev_cfg = indicator_cfg;
	prev_buzzer_enabled = prev_cfg.buzzer_enabled;
	prev_light_enabled = prev_cfg.light_enabled;

	/* Check if there are any changes (skip if force is set) */
	if (!force && prev_cfg.buzzer_enabled == cfg->buzzer_enabled &&
	    prev_cfg.light_enabled == cfg->light_enabled &&
	    indicator_light_feedback_equal(&prev_cfg, cfg) &&
	    indicator_buzzer_feedback_equal(&prev_cfg, cfg)) {
		settings_initial_apply = true;
		k_mutex_unlock(&settings_mutex);
		k_mutex_unlock(&settings_apply_mutex);
		LOG_DBG("Settings unchanged, nothing to apply");
		return 0;
	}
	k_mutex_unlock(&settings_mutex);

	LOG_INF("Settings apply: light_enabled=%d light_heartbeat=%d buzzer_enabled=%d "
		"buzzer_dm=%d buzzer_channel=%d buzzer_system=%d",
		(int)cfg->light_enabled, (int)cfg->light_feedback.heartbeat_enabled,
		(int)cfg->buzzer_enabled,
		(int)cfg->buzzer_feedback.direct_message_enabled,
		(int)cfg->buzzer_feedback.channel_message_enabled,
		(int)cfg->buzzer_feedback.system_enabled);

	/* Keep light supply stable while enabled (optional).
	 * Acquire before enabling; release only after we've turned the light off.
	 */
	if (cfg->light_enabled) {
		indicator_light_power_apply(true);
	}

	/* Apply light configuration (may touch hardware). */
	indicator_light_apply_cfg(cfg);

	/* If light just got disabled, release its power-domain after we've turned it off. */
	if (prev_light_enabled && !cfg->light_enabled) {
		indicator_light_power_apply(false);
	}

	/* If buzzer just got disabled, stop any in-flight melody before cutting power. */
	if (prev_buzzer_enabled && !cfg->buzzer_enabled) {
		if (indicator_buzzer_is_ready()) {
			indicator_buzzer_stop();
		}
	}

	/* Keep buzzer supply stable while buzzer is enabled (optional). */
	indicator_buzzer_power_apply(cfg->buzzer_enabled);

	k_mutex_lock(&settings_mutex, K_FOREVER);
	/* Copy configuration after hardware/runtime side effects have completed. */
	memcpy(&indicator_cfg, cfg, sizeof(mbs_indicator_config));
	settings_initial_apply = true;
	k_mutex_unlock(&settings_mutex);

	if (persistence) {
		k_work_reschedule(&settings_persistence_work,
				  K_MSEC(CONFIG_MBS_SETTINGS_PERSISTENCE_DELAY));
	}

	k_mutex_unlock(&settings_apply_mutex);
	return 0;
}

MBS_SETTINGS_BLOB_CONFIG_DEFINE(indicator_settings_schema, settings_mutex, settings_load_state,
			       settings_load_cfg, indicator_cfg, settings_initial_apply,
			       mbs_indicator_config, meshbus_IndicatorConfig_size,
			       settings_handler_apply, "indicator")

SETTINGS_STATIC_HANDLER_DEFINE(mbs_indicator, MBS_INDICATOR_SETTINGS_SUBTREE, NULL,
			       settings_handle_set, settings_handle_commit, settings_handle_export);

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */
int mbs_indicator_config_get(mbs_indicator_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&settings_mutex, K_FOREVER);
	memcpy(cfg, &indicator_cfg, sizeof(*cfg));
	k_mutex_unlock(&settings_mutex);

	return 0;
}

int mbs_indicator_config_set(const mbs_indicator_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}
	return settings_handler_apply(cfg, true, false);
}

int mbs_indicator_config_reset(void)
{
	mbs_indicator_config cfg = MBS_INDICATOR_CONFIG_DEFAULTS;
	struct k_work_sync sync;
	int rc;

	(void)k_work_cancel_delayable_sync(&settings_persistence_work, &sync);

	rc = settings_handler_apply(&cfg, false, true);
	if (rc != 0) {
		return rc;
	}

	rc = mbs_settings_blob_delete(&indicator_settings_schema);
	if (rc != 0) {
		LOG_ERR("Failed to delete persisted settings: %d", rc);
		return rc;
	}

	return 0;
}

int mbs_indicator_light_idle_color(uint8_t r, uint8_t g, uint8_t b)
{
	if (!indicator_light_supports_rgb()) {
		return 0; /* Silently ignore for non-RGB hardware */
	}

	indicator_light_set_idle_color(r, g, b);
	return 0;
}

int mbs_indicator_light_idle(uint32_t on_duration_ms, uint32_t off_duration_ms)
{
	indicator_light_set_idle(on_duration_ms, off_duration_ms);
	return 0;
}

int mbs_indicator_light_color(uint8_t r, uint8_t g, uint8_t b)
{
	if (!indicator_light_supports_rgb()) {
		return 0; /* Silently ignore for non-RGB hardware */
	}

	indicator_light_set_color(r, g, b);
	return 0;
}

int mbs_indicator_light_play(uint32_t on_duration_ms, uint32_t off_duration_ms, uint8_t count)
{
	return indicator_light_play(on_duration_ms, off_duration_ms, count);
}

int mbs_indicator_light_stop(void)
{
	indicator_light_stop();
	return 0;
}

int mbs_indicator_buzzer_play(enum indicator_buzzer_source source,
				  const struct indicator_buzzer_melody *melody)
{
	if (melody == NULL || melody->notes == NULL || melody->length == 0) {
		return -EINVAL;
	}

	if (!indicator_buzzer_source_allowed(source)) {
		LOG_DBG("Buzzer request filtered: source=%d", source);
		return -EACCES;
	}

	if (!indicator_buzzer_available()) {
		return 0;
	}

	return indicator_buzzer_play(melody);
}

int mbs_indicator_buzzer_rtttl(const char *rtttl_string)
{
	if (rtttl_string == NULL || *rtttl_string == '\0') {
		return -EINVAL;
	}

	/* RTTTL is typically used for system/notification tones, use SYSTEM source */
	if (!indicator_buzzer_source_allowed(INDICATOR_SOURCE_SYSTEM)) {
		LOG_DBG("Buzzer RTTTL filtered");
		return -EACCES;
	}

	if (!indicator_buzzer_available()) {
		return 0;
	}

	return indicator_buzzer_play_rtttl(rtttl_string);
}

void mbs_indicator_buzzer_stop(void)
{
	if (!indicator_buzzer_available()) {
		return;
	}

	indicator_buzzer_stop();
}

bool mbs_indicator_light_is_ready(void)
{
	return indicator_light_is_ready();
}

bool mbs_indicator_buzzer_is_ready(void)
{
	return indicator_buzzer_is_ready();
}

/* -------------------------------------------------------------------------- */
/* Power Callback                                                             */
/* -------------------------------------------------------------------------- */
static void mbs_power_indicator_cb(enum mbs_power_action event, void *user_data)
{
	ARG_UNUSED(user_data);

	if (event != MBS_POWER_ACTION_SHUTDOWN && event != MBS_POWER_ACTION_REBOOT) {
		return;
	}

#if IS_ENABLED(CONFIG_MBS_INDICATOR_BUZZER)
	(void)k_work_cancel_delayable(&startup_buzzer_work);
#endif

	if (indicator_buzzer_source_allowed(INDICATOR_SOURCE_SYSTEM)) {
		indicator_buzzer_play_sync(&indicator_buzzer_shutdown_tone, K_SECONDS(3));
	}

	/* Best-effort: stop activity and release any held power-domain references. */
	(void)k_work_cancel_delayable(&settings_persistence_work);

	indicator_buzzer_stop();
	indicator_light_stop();

	indicator_buzzer_power_apply(false);
	indicator_light_power_apply(false);

	LOG_INF("Indicator stopped");
}
MBS_POWER_ACTION_CALLBACK_DEFINE(mbs_power_indicator_cb, NULL);

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */
static int mbs_indicator_init(void)
{
	int rc;

	/* Initialize light and buzzer modules */
#if IS_ENABLED(CONFIG_MBS_INDICATOR_LIGHT)
	mbs_indicator_light_init();
#endif

#if IS_ENABLED(CONFIG_MBS_INDICATOR_BUZZER)
	mbs_indicator_buzzer_init();
#endif

	/* Initialize work items */
	k_work_init_delayable(&settings_persistence_work, settings_persistence_work_handler);
#if IS_ENABLED(CONFIG_MBS_INDICATOR_BUZZER)
	k_work_init_delayable(&startup_buzzer_work, startup_buzzer_work_handler);
#endif

	/* Load settings */
	rc = settings_load_subtree(MBS_INDICATOR_SETTINGS_SUBTREE);
	if (rc != 0) {
		return rc;
	}
	if (!settings_initial_apply) {
		rc = settings_handler_apply(&indicator_cfg, false, true);
		if (rc != 0) {
			return rc;
		}
	}

#if IS_ENABLED(CONFIG_MBS_INDICATOR_BUZZER)
	(void)k_work_schedule(&startup_buzzer_work, K_MSEC(200));
#endif

	return 0;
}

SYS_INIT(mbs_indicator_init, APPLICATION, CONFIG_MBS_INDICATOR_INIT_PRIORITY);
