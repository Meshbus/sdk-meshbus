/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt_defines.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#include <meshcore/meshcore.h>
#include <management/management.h>

#include "mbs_settings_internal.h"
#include "management_priv.h"

LOG_MODULE_REGISTER(mbs_management, CONFIG_MBS_MANAGEMENT_LOG_LEVEL);

static bool management_smp_response_validator(const void *msg, size_t msg_size);

ZBUS_CHAN_DEFINE(mbs_management_smp_response_chan,
		 mbs_management_smp_response_event,
		 management_smp_response_validator, NULL,
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

static K_MUTEX_DEFINE(management_settings_mutex);
static mbs_management_config management_cfg = meshbus_ManagementConfig_init_zero;
static mbs_management_config settings_load_cfg = meshbus_ManagementConfig_init_zero;
static struct mbs_settings_blob_load_state settings_load_state;
static bool settings_initial_apply;
static struct k_work_delayable settings_persistence_work;

MBS_SETTINGS_BLOB_SCHEMA_DEFINE(management_config_settings_schema,
			       MBS_MANAGEMENT_SETTINGS_SUBTREE,
			       MBS_MANAGEMENT_SETTINGS_KEY_CONFIG,
			       meshbus_ManagementConfig, mbs_management_config);

static bool management_smp_response_validator(const void *msg, size_t msg_size)
{
	const mbs_management_smp_response_event *event = msg;

	if (event == NULL || msg_size != sizeof(*event)) {
		return false;
	}
	if (event->tag == 0U ||
	    event->response_len > mbs_management_smp_effective_max_len_get()) {
		return false;
	}
	if ((event->status == 0 && event->response_len < MGMT_HDR_SIZE) ||
	    (event->status != 0 && event->response_len != 0U)) {
		return false;
	}

	return true;
}

size_t mbs_management_smp_effective_max_len_get(void)
{
	return MANAGEMENT_SMP_EFFECTIVE_MAX_LEN;
}

static int management_config_validate(const mbs_management_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	if (cfg->secret.size != 0U &&
	    !management_password_is_valid(cfg->secret.bytes, cfg->secret.size)) {
		LOG_ERR("Invalid management password: size=%u",
			(unsigned int)cfg->secret.size);
		return -EINVAL;
	}

	return 0;
}

static bool management_config_has_change(const mbs_management_config *a,
					  const mbs_management_config *b)
{
	if (a == NULL || b == NULL) {
		return true;
	}

	return a->secret.size != b->secret.size ||
	       memcmp(a->secret.bytes, b->secret.bytes,
		      a->secret.size) != 0;
}

static int settings_handler_apply(const mbs_management_config *cfg,
				  bool persistence, bool force)
{
	mbs_management_config normalized_cfg;
	bool secret_changed;
	int rc;

	if (cfg == NULL) {
		return -EINVAL;
	}

	normalized_cfg = *cfg;
	rc = management_config_validate(&normalized_cfg);

	if (rc != 0) {
		return rc;
	}

	k_mutex_lock(&management_settings_mutex, K_FOREVER);

	if (!force && !management_config_has_change(&management_cfg, &normalized_cfg)) {
		if (!settings_initial_apply) {
			settings_initial_apply = true;
		}
		k_mutex_unlock(&management_settings_mutex);
		LOG_DBG("Management settings unchanged, nothing to apply");
		return 0;
	}

	secret_changed =
		management_cfg.secret.size != normalized_cfg.secret.size ||
		memcmp(management_cfg.secret.bytes,
		       normalized_cfg.secret.bytes,
		       management_cfg.secret.size) != 0;
	memcpy(&management_cfg, &normalized_cfg, sizeof(management_cfg));
	settings_initial_apply = true;

	k_mutex_unlock(&management_settings_mutex);

	if (persistence) {
		k_work_reschedule(&settings_persistence_work,
				  K_MSEC(CONFIG_MBS_SETTINGS_PERSISTENCE_DELAY));
	}

	if (secret_changed) {
		management_sessions_clear();
	}

	return 0;
}

static int settings_handle_set(const char *name, size_t len,
			       settings_read_cb read_cb, void *cb_arg)
{
	uint8_t config_buffer[
		MBS_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_ManagementConfig_size)];

	if (name == NULL || read_cb == NULL) {
		return -EINVAL;
	}

	return mbs_settings_blob_handle_set_with_buffer(
		&management_config_settings_schema, &management_settings_mutex,
		&settings_load_state, &settings_load_cfg, name, len, read_cb, cb_arg,
		config_buffer, sizeof(config_buffer));
}

static int settings_handle_commit(void)
{
	mbs_management_config cfg;
	bool force;
	int rc;

	if (!mbs_settings_blob_commit_prepare(
		    &management_config_settings_schema, &management_settings_mutex,
		    &settings_load_state, &settings_load_cfg, &cfg,
		    &settings_initial_apply, &force)) {
		return 0;
	}

	rc = settings_handler_apply(&cfg, false, force);
	if (rc != 0) {
		LOG_WRN("Ignoring invalid persisted management config: %d", rc);
		return 0;
	}

	return 0;
}

static int settings_handle_export(int (*export_func)(const char *name, const void *val,
						      size_t val_len))
{
	mbs_management_config snapshot;
	uint8_t config_buffer[
		MBS_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_ManagementConfig_size)];

	if (export_func == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&management_settings_mutex, K_FOREVER);
	snapshot = management_cfg;
	k_mutex_unlock(&management_settings_mutex);

	return mbs_settings_blob_export_with_buffer(&management_config_settings_schema,
						   &snapshot, config_buffer,
						   sizeof(config_buffer), export_func);
}

SETTINGS_STATIC_HANDLER_DEFINE(mbs_management, MBS_MANAGEMENT_SETTINGS_SUBTREE,
			       NULL, settings_handle_set, settings_handle_commit,
			       settings_handle_export);

MBS_SETTINGS_BLOB_CONFIG_DEFINE_PERSISTENCE_WORK(management_config_settings_schema,
						management_settings_mutex,
						management_cfg,
						meshbus_ManagementConfig_size,
						LOG_WRN)

int mbs_management_config_get(mbs_management_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&management_settings_mutex, K_FOREVER);
	memcpy(cfg, &management_cfg, sizeof(*cfg));
	k_mutex_unlock(&management_settings_mutex);

	return 0;
}

int mbs_management_config_set(const mbs_management_config *cfg)
{
	mbs_management_config new_cfg;

	if (cfg == NULL) {
		return -EINVAL;
	}

	new_cfg = *cfg;
	if (new_cfg.secret.size == 0U) {
		k_mutex_lock(&management_settings_mutex, K_FOREVER);
		new_cfg.secret = management_cfg.secret;
		k_mutex_unlock(&management_settings_mutex);
	}

	return settings_handler_apply(&new_cfg, true, false);
}

int mbs_management_config_reset(void)
{
	struct k_work_sync sync;
	mbs_management_config cfg = meshbus_ManagementConfig_init_zero;
	int rc;

	(void)k_work_cancel_delayable_sync(&settings_persistence_work, &sync);

	rc = settings_handler_apply(&cfg, false, true);
	if (rc != 0) {
		return rc;
	}

	rc = mbs_settings_blob_delete(&management_config_settings_schema);
	if (rc != 0) {
		LOG_ERR("Failed to delete persisted management config: %d", rc);
		return rc;
	}

	LOG_INF("Management config reset to defaults");
	return 0;
}

#if !defined(CONFIG_MBS_MANAGEMENT_ENDPOINT)
void management_sessions_clear(void)
{
#if defined(CONFIG_MBS_MANAGEMENT_OPERATOR)
	management_smp_session_clear();
#endif
}
#endif

#if !defined(CONFIG_MBS_MANAGEMENT_OPERATOR)
int mbs_management_smp_request(
	const mbs_management_smp_request_event *request, uint32_t *out_tag)
{
	ARG_UNUSED(request);

	if (out_tag != NULL) {
		*out_tag = 0U;
	}

	return -ENOTSUP;
}

int mbs_management_smp_request_with_secret(
	const mbs_management_smp_request_event *request,
	const uint8_t *secret, size_t secret_len, uint32_t *out_tag)
{
	ARG_UNUSED(request);
	ARG_UNUSED(secret);
	ARG_UNUSED(secret_len);

	if (out_tag != NULL) {
		*out_tag = 0U;
	}

	return -ENOTSUP;
}

int mbs_management_secret_set_request(
	const mbs_management_secret_set_request_event *request, uint32_t *out_tag)
{
	ARG_UNUSED(request);

	if (out_tag != NULL) {
		*out_tag = 0U;
	}

	return -ENOTSUP;
}
#endif

static void meshcore_anon_data_response_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_meshcore_anon_data_response_event *event;

	event = (const struct mbs_meshcore_anon_data_response_event *)
		zbus_chan_const_msg(chan);
	if (event == NULL || event->payload_len == 0U) {
		return;
	}

	LOG_DBG("Management anon frame received: sender=%02x%02x%02x%02x len=%u",
		event->public_key[0], event->public_key[1], event->public_key[2],
		event->public_key[3], (unsigned int)event->payload_len);

#if defined(CONFIG_MBS_MANAGEMENT_OPERATOR)
	management_smp_anon_data_response_handle(event);
#endif
#if defined(CONFIG_MBS_MANAGEMENT_ENDPOINT)
	management_session_anon_data_response_handle(event);
#endif
}

ZBUS_LISTENER_DEFINE(mbs_management_meshcore_anon_data_response_listener,
		     meshcore_anon_data_response_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_meshcore_anon_data_response_chan,
		  mbs_management_meshcore_anon_data_response_listener, 4);

static int management_init(void)
{
	int rc;

	LOG_INF("Initializing Meshbus management");

	k_work_init_delayable(&settings_persistence_work,
			      settings_persistence_work_handler);
#if defined(CONFIG_MBS_MANAGEMENT_ENDPOINT)
	management_session_init();
#endif
#if defined(CONFIG_MBS_MANAGEMENT_OPERATOR)
	management_smp_init();
#endif

	k_mutex_lock(&management_settings_mutex, K_FOREVER);
	mbs_settings_blob_load_state_reset(&settings_load_state, &settings_load_cfg,
					  sizeof(settings_load_cfg));
	k_mutex_unlock(&management_settings_mutex);

	rc = settings_load_subtree(MBS_MANAGEMENT_SETTINGS_SUBTREE);
	if (rc != 0) {
		LOG_WRN("Failed to load management settings: %d", rc);
	}

	if (!settings_initial_apply) {
		mbs_management_config defaults = meshbus_ManagementConfig_init_zero;

		rc = settings_handler_apply(&defaults, false, true);
		if (rc != 0) {
			LOG_ERR("Failed to apply default management config: %d", rc);
			return rc;
		}
	}

	LOG_INF("Meshbus management ready");
	return 0;
}

SYS_INIT(management_init, APPLICATION, CONFIG_MBS_MANAGEMENT_INIT_PRIORITY);
