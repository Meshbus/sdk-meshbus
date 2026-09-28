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

#include <meshcore/meshcore.h>
#include <power/power.h>

#include "mbs_settings_internal.h"
#include "meshbus/meshcore.pb.h"
#include "meshcore_prvi.h"
#if defined(CONFIG_MBS_MESHCORE_RUNTIME)
#include "meshcore_identity.h"
#endif

LOG_MODULE_REGISTER(mbs_meshcore_config, CONFIG_MBS_MESHCORE_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* ZBus Channels                                                              */
/* -------------------------------------------------------------------------- */

static bool meshcore_node_discover_request_validator(const void *msg, size_t msg_size);
static bool meshcore_trace_request_validator(const void *msg, size_t msg_size);
static bool meshcore_trace_response_validator(const void *msg, size_t msg_size);

ZBUS_CHAN_DEFINE(mbs_meshcore_advert_request_chan, mbs_meshcore_advert_request_event,
		 NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_meshcore_node_discover_request_chan,
		 mbs_meshcore_node_discover_request_event,
		 meshcore_node_discover_request_validator, NULL,
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_meshcore_trace_request_chan,
		 mbs_meshcore_trace_request_event,
		 meshcore_trace_request_validator, NULL,
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_meshcore_trace_response_chan,
		 mbs_meshcore_trace_response_event,
		 meshcore_trace_response_validator, NULL,
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

/* -------------------------------------------------------------------------- */
/* Defaults And State                                                         */
/* -------------------------------------------------------------------------- */

#define MBS_MESHCORE_ROLE_MIN MBS_MESHCORE_ROLE_CHAT
#define MBS_MESHCORE_ROLE_MAX MBS_MESHCORE_ROLE_SENSOR
#define MBS_MESHCORE_DEFAULT_ROLE_VALUE \
	((mbs_meshcore_role)CONFIG_MBS_MESHCORE_DEFAULT_ROLE)
#define MBS_MESHCORE_SETTINGS_SUBTREE "meshbus/meshcore"
#define MBS_MESHCORE_SETTINGS_KEY_CONFIG "config"
#define MBS_MESHCORE_CONTACT_ADD_FILTER_MASK \
	(MBS_MESHCORE_CONTACT_ADD_FILTER_OVERWRITE_OLDEST | \
	 MBS_MESHCORE_CONTACT_ADD_FILTER_CHAT | \
	 MBS_MESHCORE_CONTACT_ADD_FILTER_REPEATER | \
	 MBS_MESHCORE_CONTACT_ADD_FILTER_ROOM | \
	 MBS_MESHCORE_CONTACT_ADD_FILTER_SENSOR | \
	 MBS_MESHCORE_CONTACT_ADD_FILTER_MANUAL_MODE)

#define MBS_MESHCORE_DEFAULT_ADD_CONTACT_CONFIG \
	(MBS_MESHCORE_CONTACT_ADD_FILTER_CHAT | \
	 MBS_MESHCORE_CONTACT_ADD_FILTER_REPEATER | \
	 MBS_MESHCORE_CONTACT_ADD_FILTER_ROOM | \
	 MBS_MESHCORE_CONTACT_ADD_FILTER_SENSOR | \
	 MBS_MESHCORE_CONTACT_ADD_FILTER_MANUAL_MODE)


#define MBS_MESHCORE_CONFIG_DEFAULTS \
	{ \
		.role = MBS_MESHCORE_DEFAULT_ROLE_VALUE, \
		.path_hash_size = 1U, \
		.loop_detect = MBS_MESHCORE_LOOP_DETECT_OFF, \
		.client_repeat = false, \
		.latitude = 0, \
		.longitude = 0, \
		.advert_position = false, \
		.add_contact_config = MBS_MESHCORE_DEFAULT_ADD_CONTACT_CONFIG, \
		.tx_delay_factor = 0.5f, \
		.direct_tx_delay_factor = 0.2f, \
		.telemetry_mode_base = MBS_MESHCORE_TELEMETRY_MODE_ALL, \
		.telemetry_mode_locat = MBS_MESHCORE_TELEMETRY_MODE_ALL, \
		.telemetry_mode_environment = MBS_MESHCORE_TELEMETRY_MODE_FLAGS, \
	}

BUILD_ASSERT(CONFIG_MBS_MESHCORE_DEFAULT_ROLE >= MBS_MESHCORE_ROLE_MIN &&
		     CONFIG_MBS_MESHCORE_DEFAULT_ROLE <= MBS_MESHCORE_ROLE_MAX,
	     "CONFIG_MBS_MESHCORE_DEFAULT_ROLE must be a valid MeshCore role");
BUILD_ASSERT(CONFIG_MBS_MESHCORE_DEFAULT_ROLE != MBS_MESHCORE_ROLE_CHAT ||
	     (IS_ENABLED(CONFIG_MBS_CONTACT) && IS_ENABLED(CONFIG_MBS_CHANNEL) &&
	      IS_ENABLED(CONFIG_MBS_MESSAGE) &&
	      (!IS_ENABLED(CONFIG_MBS_MESHCORE_RUNTIME) ||
	       IS_ENABLED(CONFIG_MBS_MESHCORE_CLIENT))),
	     "Default CHAT requires compiled Contact, Channel, and Message integration");

static mbs_meshcore_config meshcore_cfg = MBS_MESHCORE_CONFIG_DEFAULTS;

static K_MUTEX_DEFINE(mbs_meshcore_settings_mutex);
static K_MUTEX_DEFINE(mbs_meshcore_persistence_mutex);
static bool settings_initial_apply;
static struct k_work_delayable settings_persistence_work;
static mbs_meshcore_config settings_load_cfg = MBS_MESHCORE_CONFIG_DEFAULTS;
static struct mbs_settings_blob_load_state settings_load_state;
static atomic_t shutting_down = ATOMIC_INIT(0);
static atomic_t active_role = ATOMIC_INIT(CONFIG_MBS_MESHCORE_DEFAULT_ROLE);
static bool config_ready;
static atomic_t activation_pending;
static atomic_t activation_failed;
static atomic_t config_update_busy;
static const mbs_meshcore_config *activation_cfg;

bool mbs_meshcore_activation_pending(void)
{
	return atomic_get(&activation_pending) != 0;
}

/* Only the engine sees a candidate; public getters and persistence see committed settings. */
int mbs_meshcore_active_config_get(mbs_meshcore_config *cfg)
{
	k_mutex_lock(&mbs_meshcore_settings_mutex, K_FOREVER);
	*cfg = activation_cfg != NULL ? *activation_cfg : meshcore_cfg;
	k_mutex_unlock(&mbs_meshcore_settings_mutex);
	return 0;
}

void mbs_meshcore_config_activate(const mbs_meshcore_config *cfg)
{
	k_mutex_lock(&mbs_meshcore_settings_mutex, K_FOREVER);
	activation_cfg = cfg;
	atomic_set(&active_role, cfg != NULL ? cfg->role : meshcore_cfg.role);
	k_mutex_unlock(&mbs_meshcore_settings_mutex);
}

void mbs_meshcore_activation_complete(int recovery_result)
{
	mbs_meshcore_config_activate(NULL);
	atomic_set(&activation_failed, recovery_result != 0);
	atomic_clear(&activation_pending);
}

#if defined(CONFIG_MBS_MESHCORE_RUNTIME)
/* Shared by first boot and reset; prepare a candidate without publishing it. */
void mbs_meshcore_config_init_identity(mbs_meshcore_config *cfg)
{
	struct meshcore_local_identity identity;

	meshcore_local_identity_generate(&identity);
	memcpy(cfg->public_key.bytes, identity.identity.pub_key, MESHCORE_PUBLIC_KEY_SIZE);
	memcpy(cfg->private_key.bytes, identity.prv_key, MESHCORE_PRIVATE_KEY_SIZE);
	cfg->public_key.size = MESHCORE_PUBLIC_KEY_SIZE;
	cfg->private_key.size = MESHCORE_PRIVATE_KEY_SIZE;
	cfg->disable_fwd = false;
	cfg->flood_max = 64U;
	cfg->client_repeat = false;
	cfg->tx_delay_factor = 0.5f;
	cfg->direct_tx_delay_factor = 0.2f;
	cfg->advert_interval = 60U;
	cfg->flood_advert_interval = 60U * 60U;
	cfg->loop_detect = MBS_MESHCORE_LOOP_DETECT_OFF;
}
#endif

static uint32_t meshcore_supported_roles(void)
{
	uint32_t roles = BIT(MBS_MESHCORE_ROLE_REPEATER) |
			 BIT(MBS_MESHCORE_ROLE_ROOM) | BIT(MBS_MESHCORE_ROLE_SENSOR);

	if (IS_ENABLED(CONFIG_MBS_CONTACT) && IS_ENABLED(CONFIG_MBS_CHANNEL) &&
	    IS_ENABLED(CONFIG_MBS_MESSAGE) &&
	    (!IS_ENABLED(CONFIG_MBS_MESHCORE_RUNTIME) ||
	     IS_ENABLED(CONFIG_MBS_MESHCORE_CLIENT))) {
		roles |= BIT(MBS_MESHCORE_ROLE_CHAT);
	}
	return roles;
}

static atomic_t async_request_tag_counter = ATOMIC_INIT(1);

/* -------------------------------------------------------------------------- */
/* Settings Helpers And Schema                                                */
/* -------------------------------------------------------------------------- */

MBS_SETTINGS_BLOB_SCHEMA_DEFINE(meshcore_config_settings_schema,
			       MBS_MESHCORE_SETTINGS_SUBTREE,
			       MBS_MESHCORE_SETTINGS_KEY_CONFIG,
			       meshbus_MeshcoreConfig, mbs_meshcore_config);

/* -------------------------------------------------------------------------- */
/* Validation And Helpers                                                     */
/* -------------------------------------------------------------------------- */

static bool meshcore_config_telemetry_mode_valid(meshbus_MeshcoreConfig_TelemetryMode mode)
{
	return mode >= meshbus_MeshcoreConfig_TelemetryMode_TELEMETRY_DENY &&
	       mode <= meshbus_MeshcoreConfig_TelemetryMode_TELEMETRY_ALLOW_ALL;
}

static int meshcore_config_validate(const mbs_meshcore_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	if (cfg->role < MBS_MESHCORE_ROLE_MIN || cfg->role > MBS_MESHCORE_ROLE_MAX) {
		return -EINVAL;
	}
	if ((meshcore_supported_roles() & BIT(cfg->role)) == 0U) {
		return -ENOTSUP;
	}

	if (cfg->public_key.size != 0U &&
	    cfg->public_key.size != MBS_MESHCORE_PUBLIC_KEY_SIZE) {
		LOG_ERR("Invalid public_key size: %u", (unsigned int)cfg->public_key.size);
		return -EINVAL;
	}

	if (cfg->private_key.size != 0U &&
	    cfg->private_key.size != MBS_MESHCORE_PRIVATE_KEY_SIZE) {
		LOG_ERR("Invalid private_key size: %u", (unsigned int)cfg->private_key.size);
		return -EINVAL;
	}
	if (cfg->path_hash_size == 0U ||
	    cfg->path_hash_size > MBS_MESHCORE_PATH_HASH_SIZE_MAX) {
		LOG_ERR("Invalid path_hash_size: %u", (unsigned int)cfg->path_hash_size);
		return -EINVAL;
	}
	if (cfg->loop_detect < MBS_MESHCORE_LOOP_DETECT_OFF ||
	    cfg->loop_detect > MBS_MESHCORE_LOOP_DETECT_STRICT) {
		LOG_ERR("Invalid loop_detect: %u", (unsigned int)cfg->loop_detect);
		return -EINVAL;
	}
	if ((cfg->add_contact_config & ~MBS_MESHCORE_CONTACT_ADD_FILTER_MASK) != 0U) {
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

static bool meshcore_config_has_change(const mbs_meshcore_config *a,
				       const mbs_meshcore_config *b)
{
	if (a == NULL || b == NULL) {
		return true;
	}

	if (memcmp(a->name, b->name, sizeof(a->name)) != 0) {
		return true;
	}

	if (a->role != b->role || a->latitude != b->latitude || a->longitude != b->longitude ||
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
	const mbs_meshcore_node_discover_request_event *event = msg;

	if (event == NULL || msg_size != sizeof(*event)) {
		return false;
	}

	return event->filter != 0U &&
	       (event->filter & ~MBS_MESHCORE_DISCOVER_FILTER_ALL) == 0U;
}

static bool meshcore_trace_request_validator(const void *msg, size_t msg_size)
{
	const mbs_meshcore_trace_request_event *event = msg;
	uint8_t hop_count;

	if (event == NULL || msg_size != sizeof(*event) ||
	    event->path_len == 0U ||
	    event->path_len > MBS_MESHCORE_PATH_MAX_LEN ||
	    event->path_hash_size == 0U ||
	    event->path_hash_size > MBS_MESHCORE_PATH_HASH_SIZE_MAX ||
	    (event->path_len % event->path_hash_size) != 0U) {
		return false;
	}

	hop_count = event->path_len / event->path_hash_size;
	return (hop_count % 2U) != 0U;
}

static bool meshcore_trace_response_validator(const void *msg, size_t msg_size)
{
	const mbs_meshcore_trace_response_event *event = msg;

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

static int settings_handler_apply(const mbs_meshcore_config *cfg, bool persistence,
				  bool force)
{
	mbs_meshcore_config normalized_cfg = *cfg;

	if (normalized_cfg.path_hash_size <= 0U) {
		normalized_cfg.path_hash_size = 1U;
	}

	int rc = meshcore_config_validate(&normalized_cfg);
	if (rc != 0) {
		return rc;
	}

	k_mutex_lock(&mbs_meshcore_settings_mutex, K_FOREVER);
	if (persistence && atomic_get(&shutting_down) != 0) {
		k_mutex_unlock(&mbs_meshcore_settings_mutex);
		return -ESHUTDOWN;
	}

	if (!force && !meshcore_config_has_change(&meshcore_cfg, &normalized_cfg)) {
		if (!settings_initial_apply) {
			settings_initial_apply = true;
		}
		k_mutex_unlock(&mbs_meshcore_settings_mutex);
		LOG_DBG("MeshCore settings unchanged, nothing to apply");
		return 0;
	}

	memcpy(&meshcore_cfg, &normalized_cfg, sizeof(meshcore_cfg));
	meshcore_cfg.name[sizeof(meshcore_cfg.name) - 1U] = '\0';
	atomic_set(&active_role, meshcore_cfg.role);
	settings_initial_apply = true;

	mbs_meshcore_config applied_cfg = meshcore_cfg;

	k_mutex_unlock(&mbs_meshcore_settings_mutex);

	LOG_INF("MeshCore config applied: configured_role=%u name=%s path_hash_size=%u "
		"loop_detect=%u client_repeat=%d advert=%d add_contact_config=0x%02x",
		(unsigned int)applied_cfg.role, applied_cfg.name,
		(unsigned int)applied_cfg.path_hash_size,
		(unsigned int)applied_cfg.loop_detect, applied_cfg.client_repeat,
		applied_cfg.advert_position, (unsigned int)applied_cfg.add_contact_config);

	if (persistence) {
		k_work_reschedule(&settings_persistence_work,
				  K_MSEC(CONFIG_MBS_SETTINGS_PERSISTENCE_DELAY));
	}

	return 0;
}

static int settings_handle_set(const char *name, size_t len, settings_read_cb read_cb,
			       void *cb_arg)
{
	uint8_t config_buffer[MBS_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_MeshcoreConfig_size)];

	if (name == NULL || read_cb == NULL) {
		return -EINVAL;
	}

	return mbs_settings_blob_handle_set_with_buffer(
		&meshcore_config_settings_schema, &mbs_meshcore_settings_mutex,
		&settings_load_state, &settings_load_cfg, name, len, read_cb, cb_arg,
		config_buffer, sizeof(config_buffer));
}

static int settings_handle_commit(void)
{
	mbs_meshcore_config cfg;
	bool force;
	int rc;

	if (config_ready) {
		return 0;
	}

	if (!mbs_settings_blob_commit_prepare(
		    &meshcore_config_settings_schema, &mbs_meshcore_settings_mutex,
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
	mbs_meshcore_config snapshot;
	uint8_t config_buffer[MBS_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_MeshcoreConfig_size)];

	if (export_func == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&mbs_meshcore_settings_mutex, K_FOREVER);
	snapshot = meshcore_cfg;
	k_mutex_unlock(&mbs_meshcore_settings_mutex);

	return mbs_settings_blob_export_with_buffer(&meshcore_config_settings_schema, &snapshot,
						   config_buffer, sizeof(config_buffer),
						   export_func);
}

SETTINGS_STATIC_HANDLER_DEFINE(mbs_meshcore, MBS_MESHCORE_SETTINGS_SUBTREE, NULL,
			       settings_handle_set, settings_handle_commit,
			       settings_handle_export);

/* -------------------------------------------------------------------------- */
/* Callbacks And Work                                                         */
/* -------------------------------------------------------------------------- */

static int meshcore_config_persist_now(bool stop_if_shutting_down)
{
	uint8_t buffer[MBS_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_MeshcoreConfig_size)];
	int rc;

	/* Lock order: persistence mutex -> settings mutex (inside the blob helper). */
	k_mutex_lock(&mbs_meshcore_persistence_mutex, K_FOREVER);
	if (stop_if_shutting_down && atomic_get(&shutting_down) != 0) {
		k_mutex_unlock(&mbs_meshcore_persistence_mutex);
		return -ESHUTDOWN;
	}

	rc = mbs_settings_blob_save_locked_with_buffer(
		&meshcore_config_settings_schema, &mbs_meshcore_settings_mutex,
		&meshcore_cfg, buffer, sizeof(buffer));
	k_mutex_unlock(&mbs_meshcore_persistence_mutex);

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

/* Serialize commit with shutdown persistence, never hold this lock while waiting on the engine. */
int mbs_meshcore_config_commit(const mbs_meshcore_config *cfg, bool force)
{
	int rc;

	k_mutex_lock(&mbs_meshcore_persistence_mutex, K_FOREVER);
	rc = settings_handler_apply(cfg, true, force);
	k_mutex_unlock(&mbs_meshcore_persistence_mutex);
	return rc;
}

static int meshcore_config_update(const mbs_meshcore_config *candidate, bool reset)
{
	mbs_meshcore_config next = *candidate;
	bool restart;
	bool identity_changed;
	int rc;

	next.name[sizeof(next.name) - 1U] = '\0';
	if (next.path_hash_size == 0U) {
		next.path_hash_size = 1U;
	}
	rc = meshcore_config_validate(&next);
	if (rc != 0) {
		return rc;
	}
	/* Reject concurrent setters without blocking callbacks on the engine's queue. */
	if (!atomic_cas(&config_update_busy, 0, 1)) {
		return -EBUSY;
	}
	if (atomic_get(&shutting_down) != 0) {
		rc = -ESHUTDOWN;
		goto out;
	}
#if defined(CONFIG_MBS_MESHCORE_RUNTIME)
	if (reset) {
		mbs_meshcore_config_init_identity(&next);
	}
#endif
	if (next.name[0] == '\0' && next.public_key.size == MBS_MESHCORE_PUBLIC_KEY_SIZE) {
		for (size_t i = 0; i < CONFIG_MBS_MESHCORE_NAME_PUBKEY_PREFIX_BYTES; i++) {
			(void)snprintk(&next.name[i * 2U], sizeof(next.name) - (i * 2U),
				       "%02X", next.public_key.bytes[i]);
		}
		next.name[CONFIG_MBS_MESHCORE_NAME_PUBKEY_PREFIX_BYTES * 2U] = '\0';
	}
	k_mutex_lock(&mbs_meshcore_settings_mutex, K_FOREVER);
	identity_changed = memcmp(&next.public_key, &meshcore_cfg.public_key,
				 sizeof(next.public_key)) != 0 ||
		memcmp(&next.private_key, &meshcore_cfg.private_key, sizeof(next.private_key)) != 0;
	restart = config_ready && (reset || next.role != meshcore_cfg.role ||
		atomic_get(&activation_failed) != 0 ||
		(identity_changed && mbs_meshcore_runtime_is_ready()));
	k_mutex_unlock(&mbs_meshcore_settings_mutex);
#if defined(CONFIG_MBS_MESHCORE_RUNTIME)
	if (restart) {
		atomic_set(&activation_pending, 1);
		/* The call returns after commit or recovery; no state/persistence lock is held. */
		rc = mbs_meshcore_runtime_apply(&next);
	} else {
		rc = mbs_meshcore_config_commit(&next, reset);
	}
#else
	ARG_UNUSED(restart);
	rc = mbs_meshcore_config_commit(&next, reset);
#endif
out:
	atomic_clear(&config_update_busy);
	return rc;
}

int mbs_meshcore_config_set(mbs_meshcore_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}
	return meshcore_config_update(cfg, false);
}

int mbs_meshcore_config_get(mbs_meshcore_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&mbs_meshcore_settings_mutex, K_FOREVER);
	memcpy(cfg, &meshcore_cfg, sizeof(*cfg));
	k_mutex_unlock(&mbs_meshcore_settings_mutex);

	return 0;
}

mbs_meshcore_role mbs_meshcore_active_role_get(void)
{
	return (mbs_meshcore_role)atomic_get(&active_role);
}

int mbs_meshcore_config_reset(void)
{
	const mbs_meshcore_config defaults = MBS_MESHCORE_CONFIG_DEFAULTS;

	return meshcore_config_update(&defaults, true);
}

int mbs_meshcore_advert_request(bool flood)
{
	mbs_meshcore_advert_request_event event = {.flood = flood};

	if (atomic_get(&shutting_down) != 0) {
		return -ESHUTDOWN;
	}

	int rc = mbs_meshcore_request_publish_accepted(
		&mbs_meshcore_advert_request_chan, &event);
	if (rc != 0) {
		LOG_WRN("MeshCore advert request not accepted: flood=%d rc=%d",
			flood ? 1 : 0, rc);
		return rc;
	}

	LOG_DBG("MeshCore advert request accepted: flood=%d", flood ? 1 : 0);
	return 0;
}

int mbs_meshcore_node_discover_request(uint8_t filter, uint32_t since, uint32_t *out_tag)
{
	mbs_meshcore_node_discover_request_event event = {0};
	int rc;

	if (out_tag != NULL) {
		*out_tag = 0U;
	}
	if (filter == 0U || (filter & ~MBS_MESHCORE_DISCOVER_FILTER_ALL) != 0U) {
		return -EINVAL;
	}
	if (atomic_get(&shutting_down) != 0) {
		return -ESHUTDOWN;
	}

	event.filter = filter;
	event.since = since;
	event.tag = async_request_tag_next();

	rc = mbs_meshcore_request_publish_accepted(
		&mbs_meshcore_node_discover_request_chan, &event);
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

int mbs_meshcore_trace_request(const uint8_t *path, uint8_t path_len,
				   uint8_t path_hash_size, uint32_t *out_tag)
{
	mbs_meshcore_trace_request_event event = {0};
	int rc;

	if (out_tag != NULL) {
		*out_tag = 0U;
	}
	if (path == NULL || path_len == 0U || path_len > sizeof(event.path) ||
	    path_hash_size == 0U ||
	    path_hash_size > MBS_MESHCORE_PATH_HASH_SIZE_MAX ||
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

	rc = mbs_meshcore_request_publish_accepted(
		&mbs_meshcore_trace_request_chan, &event);
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

static void mbs_power_meshcore_config_cb(enum mbs_power_action action, void *user_data)
{
	int rc;

	ARG_UNUSED(user_data);

	if (action != MBS_POWER_ACTION_SHUTDOWN && action != MBS_POWER_ACTION_REBOOT) {
		return;
	}

	k_mutex_lock(&mbs_meshcore_settings_mutex, K_FOREVER);
	atomic_set(&shutting_down, 1);
	k_mutex_unlock(&mbs_meshcore_settings_mutex);

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
MBS_POWER_ACTION_CALLBACK_DEFINE(mbs_power_meshcore_config_cb, NULL);

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

static int mbs_meshcore_config_init(void)
{
	int rc;

	LOG_DBG("Initializing MeshCore config service");

#ifdef CONFIG_MBS_MESHCORE_STATS
	rc = STATS_INIT_AND_REG(mbs_meshcore_stats, STATS_SIZE_32, "mbs_meshcore");
	if (rc != 0) {
		LOG_WRN("Failed to register MeshCore stats: %d", rc);
	}
#endif

	k_work_init_delayable(&settings_persistence_work, settings_persistence_work_handler);

	k_mutex_lock(&mbs_meshcore_settings_mutex, K_FOREVER);
	atomic_clear(&shutting_down);
	mbs_settings_blob_load_state_reset(&settings_load_state, &settings_load_cfg,
					  sizeof(settings_load_cfg));
	k_mutex_unlock(&mbs_meshcore_settings_mutex);

	rc = settings_load_subtree(MBS_MESHCORE_SETTINGS_SUBTREE);
	if (rc != 0) {
		LOG_WRN("Failed to load MeshCore settings: %d", rc);
	}

	if (!settings_initial_apply) {
		mbs_meshcore_config defaults = MBS_MESHCORE_CONFIG_DEFAULTS;

		rc = settings_handler_apply(&defaults, false, true);
		if (rc != 0) {
			LOG_ERR("Failed to apply default MeshCore config: %d", rc);
			return rc;
		}
	}

	atomic_set(&active_role, meshcore_cfg.role);
	config_ready = true;
	LOG_INF("MeshCore config ready: active_role=%u", (unsigned int)meshcore_cfg.role);
	return 0;
}

SYS_INIT(mbs_meshcore_config_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
