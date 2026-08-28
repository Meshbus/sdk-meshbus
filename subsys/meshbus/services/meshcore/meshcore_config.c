/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <float.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <zephyr/meshbus/meshcore.h>
#include <zephyr/meshbus/power.h>

#include "common/settings.h"
#include "meshbus/meshcore.pb.h"
#include "meshcore_prvi.h"

LOG_MODULE_REGISTER(meshbus_meshcore_config, CONFIG_MESHBUS_MESHCORE_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* ZBus Channels                                                              */
/* -------------------------------------------------------------------------- */

static bool meshcore_node_discover_request_validator(const void *msg, size_t msg_size);
static bool meshcore_trace_request_validator(const void *msg, size_t msg_size);
static bool meshcore_trace_response_validator(const void *msg, size_t msg_size);

ZBUS_CHAN_DEFINE(meshbus_meshcore_config_reset_chan, meshbus_meshcore_config_reset_event,
		 NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(meshbus_meshcore_advert_request_chan, meshbus_meshcore_advert_request_event,
		 NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(meshbus_meshcore_node_discover_request_chan,
		 meshbus_meshcore_node_discover_request_event,
		 meshcore_node_discover_request_validator, NULL,
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(meshbus_meshcore_trace_request_chan,
		 meshbus_meshcore_trace_request_event,
		 meshcore_trace_request_validator, NULL,
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(meshbus_meshcore_trace_response_chan,
		 meshbus_meshcore_trace_response_event,
		 meshcore_trace_response_validator, NULL,
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

/* -------------------------------------------------------------------------- */
/* Defaults And State                                                         */
/* -------------------------------------------------------------------------- */

#define MESHBUS_MESHCORE_ROLE_MIN MESHBUS_MESHCORE_ROLE_CHAT
#define MESHBUS_MESHCORE_ROLE_MAX MESHBUS_MESHCORE_ROLE_SENSOR
#define MESHBUS_MESHCORE_FIRMWARE_ROLE_VALUE \
	((meshbus_meshcore_role)CONFIG_MESHBUS_MESHCORE_FIRMWARE_ROLE)
#define MESHBUS_MESHCORE_SETTINGS_SUBTREE "meshbus/meshcore"
#define MESHBUS_MESHCORE_SETTINGS_KEY_CONFIG "config"
#define MESHBUS_MESHCORE_CONTACT_ADD_FILTER_MASK \
	(MESHBUS_MESHCORE_CONTACT_ADD_FILTER_OVERWRITE_OLDEST | \
	 MESHBUS_MESHCORE_CONTACT_ADD_FILTER_CHAT | \
	 MESHBUS_MESHCORE_CONTACT_ADD_FILTER_REPEATER | \
	 MESHBUS_MESHCORE_CONTACT_ADD_FILTER_ROOM | \
	 MESHBUS_MESHCORE_CONTACT_ADD_FILTER_SENSOR | \
	 MESHBUS_MESHCORE_CONTACT_ADD_FILTER_MANUAL_MODE)

#if defined(CONFIG_MESHBUS_MESHCORE_ROLE_REPEATER)
#define MESHBUS_MESHCORE_DEFAULT_ADD_CONTACT_CONFIG \
	(MESHBUS_MESHCORE_CONTACT_ADD_FILTER_OVERWRITE_OLDEST | \
	 MESHBUS_MESHCORE_CONTACT_ADD_FILTER_MANUAL_MODE)
#else
#define MESHBUS_MESHCORE_DEFAULT_ADD_CONTACT_CONFIG \
	(MESHBUS_MESHCORE_CONTACT_ADD_FILTER_CHAT | \
	 MESHBUS_MESHCORE_CONTACT_ADD_FILTER_REPEATER | \
	 MESHBUS_MESHCORE_CONTACT_ADD_FILTER_ROOM | \
	 MESHBUS_MESHCORE_CONTACT_ADD_FILTER_SENSOR | \
	 MESHBUS_MESHCORE_CONTACT_ADD_FILTER_MANUAL_MODE)
#endif

#define MESHBUS_MESHCORE_CONFIG_DEFAULTS \
	{ \
		.path_hash_size = 1U, \
		.loop_detect = MESHBUS_MESHCORE_LOOP_DETECT_OFF, \
		.client_repeat = false, \
		.latitude = 0, \
		.longitude = 0, \
		.advert_position = false, \
		.add_contact_config = MESHBUS_MESHCORE_DEFAULT_ADD_CONTACT_CONFIG, \
		.tx_delay_factor = 0.5f, \
		.direct_tx_delay_factor = 0.2f, \
		.telemetry_mode_base = MESHBUS_MESHCORE_TELEMETRY_MODE_ALL, \
		.telemetry_mode_locat = MESHBUS_MESHCORE_TELEMETRY_MODE_ALL, \
		.telemetry_mode_environment = MESHBUS_MESHCORE_TELEMETRY_MODE_FLAGS, \
	}

BUILD_ASSERT(CONFIG_MESHBUS_MESHCORE_FIRMWARE_ROLE >= MESHBUS_MESHCORE_ROLE_MIN &&
		     CONFIG_MESHBUS_MESHCORE_FIRMWARE_ROLE <= MESHBUS_MESHCORE_ROLE_MAX,
	     "CONFIG_MESHBUS_MESHCORE_FIRMWARE_ROLE must be a valid MeshCore role");

static meshbus_meshcore_config meshcore_cfg = MESHBUS_MESHCORE_CONFIG_DEFAULTS;

static K_MUTEX_DEFINE(meshbus_meshcore_settings_mutex);
static K_MUTEX_DEFINE(meshbus_meshcore_persistence_mutex);
static bool settings_initial_apply;
static struct k_work_delayable settings_persistence_work;
static meshbus_meshcore_config settings_load_cfg = MESHBUS_MESHCORE_CONFIG_DEFAULTS;
static struct mb_settings_blob_load_state settings_load_state;
static atomic_t shutting_down = ATOMIC_INIT(0);
static atomic_t async_request_tag_counter = ATOMIC_INIT(1);

/* -------------------------------------------------------------------------- */
/* Settings Helpers And Schema                                                */
/* -------------------------------------------------------------------------- */

MB_SETTINGS_BLOB_SCHEMA_DEFINE(meshcore_config_settings_schema,
			       MESHBUS_MESHCORE_SETTINGS_SUBTREE,
			       MESHBUS_MESHCORE_SETTINGS_KEY_CONFIG,
			       meshbus_MeshcoreConfig, meshbus_meshcore_config);

/* -------------------------------------------------------------------------- */
/* Validation And Helpers                                                     */
/* -------------------------------------------------------------------------- */

static bool meshcore_config_telemetry_mode_valid(meshbus_MeshcoreConfig_TelemetryMode mode)
{
	return mode >= meshbus_MeshcoreConfig_TelemetryMode_TELEMETRY_DENY &&
	       mode <= meshbus_MeshcoreConfig_TelemetryMode_TELEMETRY_ALLOW_ALL;
}

static int meshcore_config_validate(const meshbus_meshcore_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	if (cfg->public_key.size != 0U &&
	    cfg->public_key.size != MESHBUS_MESHCORE_PUBLIC_KEY_SIZE) {
		LOG_ERR("Invalid public_key size: %u", (unsigned int)cfg->public_key.size);
		return -EINVAL;
	}

	if (cfg->private_key.size != 0U &&
	    cfg->private_key.size != MESHBUS_MESHCORE_PRIVATE_KEY_SIZE) {
		LOG_ERR("Invalid private_key size: %u", (unsigned int)cfg->private_key.size);
		return -EINVAL;
	}
	if (cfg->path_hash_size == 0U ||
	    cfg->path_hash_size > MESHBUS_MESHCORE_PATH_HASH_SIZE_MAX) {
		LOG_ERR("Invalid path_hash_size: %u", (unsigned int)cfg->path_hash_size);
		return -EINVAL;
	}
	if (cfg->loop_detect < MESHBUS_MESHCORE_LOOP_DETECT_OFF ||
	    cfg->loop_detect > MESHBUS_MESHCORE_LOOP_DETECT_STRICT) {
		LOG_ERR("Invalid loop_detect: %u", (unsigned int)cfg->loop_detect);
		return -EINVAL;
	}
	if ((cfg->add_contact_config & ~MESHBUS_MESHCORE_CONTACT_ADD_FILTER_MASK) != 0U) {
		LOG_ERR("Invalid add_contact_config: 0x%02x",
			(unsigned int)cfg->add_contact_config);
		return -EINVAL;
	}
	if (!meshcore_config_telemetry_mode_valid(cfg->telemetry_mode_base)) {
		LOG_ERR("Invalid telemetry_mode_base: %u",
			(unsigned int)cfg->telemetry_mode_base);
		return -EINVAL;
	}
	if (!meshcore_config_telemetry_mode_valid(cfg->telemetry_mode_locat)) {
		LOG_ERR("Invalid telemetry_mode_locat: %u",
			(unsigned int)cfg->telemetry_mode_locat);
		return -EINVAL;
	}
	if (!meshcore_config_telemetry_mode_valid(cfg->telemetry_mode_environment)) {
		LOG_ERR("Invalid telemetry_mode_environment: %u",
			(unsigned int)cfg->telemetry_mode_environment);
		return -EINVAL;
	}

	if (!(cfg->tx_delay_factor == cfg->tx_delay_factor) ||
	    cfg->tx_delay_factor < 0.0f || cfg->tx_delay_factor > FLT_MAX) {
		LOG_ERR("Invalid tx_delay_factor");
		return -EINVAL;
	}

	if (!(cfg->direct_tx_delay_factor == cfg->direct_tx_delay_factor) ||
	    cfg->direct_tx_delay_factor < 0.0f ||
	    cfg->direct_tx_delay_factor > FLT_MAX) {
		LOG_ERR("Invalid direct_tx_delay_factor");
		return -EINVAL;
	}

	return 0;
}

static bool meshcore_config_has_change(const meshbus_meshcore_config *a,
				       const meshbus_meshcore_config *b)
{
	if (a == NULL || b == NULL) {
		return true;
	}

	if (memcmp(a->name, b->name, sizeof(a->name)) != 0) {
		return true;
	}

	if (a->latitude != b->latitude || a->longitude != b->longitude ||
	    a->disable_fwd != b->disable_fwd || a->flood_max != b->flood_max ||
	    a->multi_acks != b->multi_acks || a->advert_interval != b->advert_interval ||
	    a->flood_advert_interval != b->flood_advert_interval ||
	    a->advert_position != b->advert_position ||
	    a->add_contact_config != b->add_contact_config ||
	    a->client_repeat != b->client_repeat ||
	    a->add_contact_hops_limit != b->add_contact_hops_limit ||
	    a->path_hash_size != b->path_hash_size || a->loop_detect != b->loop_detect ||
	    a->telemetry_mode_base != b->telemetry_mode_base ||
	    a->telemetry_mode_locat != b->telemetry_mode_locat ||
	    a->telemetry_mode_environment != b->telemetry_mode_environment) {
		return true;
	}

	if (a->tx_delay_factor != b->tx_delay_factor ||
	    a->direct_tx_delay_factor != b->direct_tx_delay_factor) {
		return true;
	}

	if (a->public_key.size != b->public_key.size ||
	    memcmp(a->public_key.bytes, b->public_key.bytes, a->public_key.size) != 0) {
		return true;
	}

	if (a->private_key.size != b->private_key.size ||
	    memcmp(a->private_key.bytes, b->private_key.bytes, a->private_key.size) != 0) {
		return true;
	}

	return false;
}

static bool meshcore_node_discover_request_validator(const void *msg, size_t msg_size)
{
	const meshbus_meshcore_node_discover_request_event *event = msg;

	if (event == NULL || msg_size != sizeof(*event)) {
		return false;
	}

	return event->filter != 0U &&
	       (event->filter & ~MESHBUS_MESHCORE_DISCOVER_FILTER_ALL) == 0U;
}

static bool meshcore_trace_request_validator(const void *msg, size_t msg_size)
{
	const meshbus_meshcore_trace_request_event *event = msg;
	uint8_t hop_count;

	if (event == NULL || msg_size != sizeof(*event) ||
	    event->path_len == 0U ||
	    event->path_len > MESHBUS_MESHCORE_PATH_MAX_LEN ||
	    event->path_hash_size == 0U ||
	    event->path_hash_size > MESHBUS_MESHCORE_PATH_HASH_SIZE_MAX ||
	    (event->path_len % event->path_hash_size) != 0U) {
		return false;
	}

	hop_count = event->path_len / event->path_hash_size;
	return (hop_count % 2U) != 0U;
}

static bool meshcore_trace_response_validator(const void *msg, size_t msg_size)
{
	const meshbus_meshcore_trace_response_event *event = msg;

	if (event == NULL || msg_size != sizeof(*event) || event->tag == 0U) {
		return false;
	}

	return event->out_path_snr_count <= ARRAY_SIZE(event->out_path_snr) &&
	       event->return_path_snr_count <= ARRAY_SIZE(event->return_path_snr);
}

static uint32_t async_request_tag_next(void)
{
	uint32_t tag = (uint32_t)atomic_inc(&async_request_tag_counter);

	return tag == 0U ? (uint32_t)atomic_inc(&async_request_tag_counter) : tag;
}

/* -------------------------------------------------------------------------- */
/* Settings Apply                                                             */
/* -------------------------------------------------------------------------- */

static int settings_handler_apply(const meshbus_meshcore_config *cfg, bool persistence,
				  bool force)
{
	meshbus_meshcore_config normalized_cfg = *cfg;

	if (normalized_cfg.path_hash_size <= 0U) {
		normalized_cfg.path_hash_size = 1U;
	}

	int rc = meshcore_config_validate(&normalized_cfg);
	if (rc != 0) {
		return rc;
	}

	k_mutex_lock(&meshbus_meshcore_settings_mutex, K_FOREVER);
	if (persistence && atomic_get(&shutting_down) != 0) {
		k_mutex_unlock(&meshbus_meshcore_settings_mutex);
		return -ESHUTDOWN;
	}

	if (!force && !meshcore_config_has_change(&meshcore_cfg, &normalized_cfg)) {
		if (!settings_initial_apply) {
			settings_initial_apply = true;
		}
		k_mutex_unlock(&meshbus_meshcore_settings_mutex);
		LOG_DBG("MeshCore settings unchanged, nothing to apply");
		return 0;
	}

	memcpy(&meshcore_cfg, &normalized_cfg, sizeof(meshcore_cfg));
	meshcore_cfg.name[sizeof(meshcore_cfg.name) - 1U] = '\0';
	settings_initial_apply = true;

	meshbus_meshcore_config applied_cfg = meshcore_cfg;

	k_mutex_unlock(&meshbus_meshcore_settings_mutex);

	LOG_INF("MeshCore config applied: firmware_role=%u name=%s path_hash_size=%u "
		"loop_detect=%u client_repeat=%d advert=%d add_contact_config=0x%02x",
		(unsigned int)MESHBUS_MESHCORE_FIRMWARE_ROLE_VALUE, applied_cfg.name,
		(unsigned int)applied_cfg.path_hash_size,
		(unsigned int)applied_cfg.loop_detect, applied_cfg.client_repeat,
		applied_cfg.advert_position, (unsigned int)applied_cfg.add_contact_config);

	if (persistence) {
		k_work_reschedule(&settings_persistence_work,
				  K_MSEC(CONFIG_MESHBUS_SETTINGS_PERSISTENCE_DELAY));
	}

	return 0;
}

static int settings_handle_set(const char *name, size_t len, settings_read_cb read_cb,
			       void *cb_arg)
{
	uint8_t config_buffer[MB_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_MeshcoreConfig_size)];

	if (name == NULL || read_cb == NULL) {
		return -EINVAL;
	}

	return mb_settings_blob_handle_set_with_buffer(
		&meshcore_config_settings_schema, &meshbus_meshcore_settings_mutex,
		&settings_load_state, &settings_load_cfg, name, len, read_cb, cb_arg,
		config_buffer, sizeof(config_buffer));
}

static int settings_handle_commit(void)
{
	meshbus_meshcore_config cfg;
	bool force;
	int rc;

	if (!mb_settings_blob_commit_prepare(
		    &meshcore_config_settings_schema, &meshbus_meshcore_settings_mutex,
		    &settings_load_state, &settings_load_cfg, &cfg,
		    &settings_initial_apply, &force)) {
		return 0;
	}

	rc = settings_handler_apply(&cfg, false, force);
	if (rc != 0) {
		LOG_WRN("Ignoring invalid persisted MeshCore config: %d", rc);
		return 0;
	}

	return 0;
}

static int settings_handle_export(int (*export_func)(const char *name, const void *val,
						      size_t val_len))
{
	meshbus_meshcore_config snapshot;
	uint8_t config_buffer[MB_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_MeshcoreConfig_size)];

	if (export_func == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&meshbus_meshcore_settings_mutex, K_FOREVER);
	snapshot = meshcore_cfg;
	k_mutex_unlock(&meshbus_meshcore_settings_mutex);

	return mb_settings_blob_export_with_buffer(&meshcore_config_settings_schema, &snapshot,
						   config_buffer, sizeof(config_buffer),
						   export_func);
}

SETTINGS_STATIC_HANDLER_DEFINE(meshbus_meshcore, MESHBUS_MESHCORE_SETTINGS_SUBTREE, NULL,
			       settings_handle_set, settings_handle_commit,
			       settings_handle_export);

/* -------------------------------------------------------------------------- */
/* Callbacks And Work                                                         */
/* -------------------------------------------------------------------------- */

static int meshcore_config_persist_now(bool stop_if_shutting_down)
{
	uint8_t buffer[MB_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_MeshcoreConfig_size)];
	int rc;

	/* Lock order: persistence mutex -> settings mutex (inside the blob helper). */
	k_mutex_lock(&meshbus_meshcore_persistence_mutex, K_FOREVER);
	if (stop_if_shutting_down && atomic_get(&shutting_down) != 0) {
		k_mutex_unlock(&meshbus_meshcore_persistence_mutex);
		return -ESHUTDOWN;
	}

	rc = mb_settings_blob_save_locked_with_buffer(
		&meshcore_config_settings_schema, &meshbus_meshcore_settings_mutex,
		&meshcore_cfg, buffer, sizeof(buffer));
	k_mutex_unlock(&meshbus_meshcore_persistence_mutex);

	return rc;
}

static void settings_persistence_work_handler(struct k_work *work)
{
	int rc;

	ARG_UNUSED(work);

	rc = meshcore_config_persist_now(true);
	if (rc == -ESHUTDOWN) {
		return;
	}
	if (rc != 0) {
		LOG_WRN("Settings persistence failed: %d", rc);
	} else {
		LOG_DBG("Settings persistence complete");
	}
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

int meshbus_meshcore_config_set(meshbus_meshcore_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	meshbus_meshcore_config new_cfg = *cfg;
	new_cfg.name[sizeof(new_cfg.name) - 1U] = '\0';

	if (new_cfg.name[0] == '\0' &&
	    new_cfg.public_key.size == MESHBUS_MESHCORE_PUBLIC_KEY_SIZE) {
		for (size_t i = 0; i < CONFIG_MESHBUS_MESHCORE_NAME_PUBKEY_PREFIX_BYTES; i++) {
			(void)snprintk(&new_cfg.name[i * 2U],
				       sizeof(new_cfg.name) - (i * 2U), "%02X",
				       new_cfg.public_key.bytes[i]);
		}
		new_cfg.name[CONFIG_MESHBUS_MESHCORE_NAME_PUBKEY_PREFIX_BYTES * 2U] = '\0';
	}

	LOG_DBG("Set MeshCore config request: firmware_role=%u name=%s",
		(unsigned int)MESHBUS_MESHCORE_FIRMWARE_ROLE_VALUE, new_cfg.name);

	return settings_handler_apply(&new_cfg, true, false);
}

int meshbus_meshcore_config_get(meshbus_meshcore_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&meshbus_meshcore_settings_mutex, K_FOREVER);
	memcpy(cfg, &meshcore_cfg, sizeof(*cfg));
	k_mutex_unlock(&meshbus_meshcore_settings_mutex);

	return 0;
}

meshbus_meshcore_role meshbus_meshcore_firmware_role_get(void)
{
	return MESHBUS_MESHCORE_FIRMWARE_ROLE_VALUE;
}

int meshbus_meshcore_config_reset(void)
{
	struct k_work_sync sync;
	meshbus_meshcore_config_reset_event event = {0};
	int pub_rc;
	int rc;

	(void)k_work_cancel_delayable_sync(&settings_persistence_work, &sync);
	k_mutex_lock(&meshbus_meshcore_persistence_mutex, K_FOREVER);
	if (atomic_get(&shutting_down) != 0) {
		k_mutex_unlock(&meshbus_meshcore_persistence_mutex);
		return -ESHUTDOWN;
	}

	meshbus_meshcore_config cfg = MESHBUS_MESHCORE_CONFIG_DEFAULTS;
	rc = settings_handler_apply(&cfg, false, true);

	if (rc != 0) {
		k_mutex_unlock(&meshbus_meshcore_persistence_mutex);
		return rc;
	}

	rc = mb_settings_blob_delete(&meshcore_config_settings_schema);
	k_mutex_unlock(&meshbus_meshcore_persistence_mutex);
	if (rc != 0) {
		LOG_ERR("Failed to delete persisted MeshCore config: %d", rc);
		return rc;
	}

	pub_rc = zbus_chan_pub(&meshbus_meshcore_config_reset_chan, &event, K_NO_WAIT);
	if (pub_rc != 0) {
		LOG_WRN("MeshCore config reset event publish failed: %d", pub_rc);
	}

	LOG_INF("MeshCore config reset to defaults");
	return 0;
}

int meshbus_meshcore_advert_request(bool flood)
{
	meshbus_meshcore_advert_request_event event = {.flood = flood};

	if (atomic_get(&shutting_down) != 0) {
		return -ESHUTDOWN;
	}

	int rc = meshbus_meshcore_request_publish_accepted(
		&meshbus_meshcore_advert_request_chan, &event);
	if (rc != 0) {
		LOG_WRN("MeshCore advert request not accepted: flood=%d rc=%d",
			flood ? 1 : 0, rc);
		return rc;
	}

	LOG_DBG("MeshCore advert request accepted: flood=%d", flood ? 1 : 0);
	return 0;
}

int meshbus_meshcore_node_discover_request(uint8_t filter, uint32_t since, uint32_t *out_tag)
{
	meshbus_meshcore_node_discover_request_event event = {0};
	int rc;

	if (out_tag != NULL) {
		*out_tag = 0U;
	}
	if (filter == 0U || (filter & ~MESHBUS_MESHCORE_DISCOVER_FILTER_ALL) != 0U) {
		return -EINVAL;
	}
	if (atomic_get(&shutting_down) != 0) {
		return -ESHUTDOWN;
	}

	event.filter = filter;
	event.since = since;
	event.tag = async_request_tag_next();

	rc = meshbus_meshcore_request_publish_accepted(
		&meshbus_meshcore_node_discover_request_chan, &event);
	if (rc != 0) {
		LOG_WRN("MeshCore node-discover not accepted: filter=0x%02x rc=%d",
			 (unsigned int)filter, rc);
		return rc;
	}

	if (out_tag != NULL) {
		*out_tag = event.tag;
	}
	LOG_DBG("MeshCore node-discover request accepted: filter=0x%02x tag=%u",
		(unsigned int)filter, (unsigned int)event.tag);
	return 0;
}

int meshbus_meshcore_trace_request(const uint8_t *path, uint8_t path_len,
				   uint8_t path_hash_size, uint32_t *out_tag)
{
	meshbus_meshcore_trace_request_event event = {0};
	int rc;

	if (out_tag != NULL) {
		*out_tag = 0U;
	}
	if (path == NULL || path_len == 0U || path_len > sizeof(event.path) ||
	    path_hash_size == 0U ||
	    path_hash_size > MESHBUS_MESHCORE_PATH_HASH_SIZE_MAX ||
	    (path_len % path_hash_size) != 0U ||
	    ((path_len / path_hash_size) % 2U) == 0U) {
		return -EINVAL;
	}
	if (atomic_get(&shutting_down) != 0) {
		return -ESHUTDOWN;
	}

	memcpy(event.path, path, path_len);
	event.path_len = path_len;
	event.path_hash_size = path_hash_size;
	event.tag = async_request_tag_next();

	rc = meshbus_meshcore_request_publish_accepted(
		&meshbus_meshcore_trace_request_chan, &event);
	if (rc != 0) {
		LOG_WRN("MeshCore trace request not accepted: tag=%u rc=%d",
			(unsigned int)event.tag, rc);
		return rc;
	}

	if (out_tag != NULL) {
		*out_tag = event.tag;
	}
	LOG_DBG("MeshCore trace accepted: path_len=%u hash_size=%u tag=%u",
		(unsigned int)path_len, (unsigned int)path_hash_size,
		(unsigned int)event.tag);
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Power Callback                                                             */
/* -------------------------------------------------------------------------- */

static void meshbus_power_meshcore_config_cb(enum meshbus_power_action action, void *user_data)
{
	int rc;

	ARG_UNUSED(user_data);

	if (action != MESHBUS_POWER_ACTION_SHUTDOWN && action != MESHBUS_POWER_ACTION_REBOOT) {
		return;
	}

	k_mutex_lock(&meshbus_meshcore_settings_mutex, K_FOREVER);
	atomic_set(&shutting_down, 1);
	k_mutex_unlock(&meshbus_meshcore_settings_mutex);

	/* The system workqueue may be the shutdown caller, so do not enqueue behind it. */
	(void)k_work_cancel_delayable(&settings_persistence_work);
	rc = meshcore_config_persist_now(false);
	if (rc != 0) {
		LOG_ERR("MeshCore config shutdown persistence failed: action=%u rc=%d",
			(unsigned int)action, rc);
	} else {
		LOG_INF("MeshCore config persisted before power action: %u",
			(unsigned int)action);
	}

	LOG_INF("MeshCore config service stopped: action=%u", (unsigned int)action);
}
MESHBUS_POWER_ACTION_CALLBACK_DEFINE(meshbus_power_meshcore_config_cb, NULL);

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

static int meshbus_meshcore_config_init(void)
{
	int rc;

	LOG_DBG("Initializing MeshCore config service");

#ifdef CONFIG_MESHBUS_MESHCORE_STATS
	rc = STATS_INIT_AND_REG(meshbus_meshcore_stats, STATS_SIZE_32, "meshbus_meshcore");
	if (rc != 0) {
		LOG_WRN("Failed to register MeshCore stats: %d", rc);
	}
#endif

	k_work_init_delayable(&settings_persistence_work, settings_persistence_work_handler);

	k_mutex_lock(&meshbus_meshcore_settings_mutex, K_FOREVER);
	atomic_clear(&shutting_down);
	mb_settings_blob_load_state_reset(&settings_load_state, &settings_load_cfg,
					  sizeof(settings_load_cfg));
	k_mutex_unlock(&meshbus_meshcore_settings_mutex);

	rc = settings_load_subtree(MESHBUS_MESHCORE_SETTINGS_SUBTREE);
	if (rc != 0) {
		LOG_WRN("Failed to load MeshCore settings: %d", rc);
	}

	if (!settings_initial_apply) {
		meshbus_meshcore_config defaults = MESHBUS_MESHCORE_CONFIG_DEFAULTS;

		rc = settings_handler_apply(&defaults, false, true);
		if (rc != 0) {
			LOG_ERR("Failed to apply default MeshCore config: %d", rc);
			return rc;
		}
	}

	LOG_INF("MeshCore config ready");
	return 0;
}

SYS_INIT(meshbus_meshcore_config_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
