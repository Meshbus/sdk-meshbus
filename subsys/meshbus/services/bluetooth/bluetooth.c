/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_types.h>
#if defined(CONFIG_BT_CTS)
#include <zephyr/bluetooth/services/cts.h>
#endif
#if defined(CONFIG_BT_BAS)
#include <zephyr/bluetooth/services/bas.h>
#endif
#if defined(CONFIG_MESHBUS_MESHCORE_COMPANION_BLUETOOTH)
#include <zephyr/bluetooth/services/nus.h>
#endif
#include <zephyr/meshbus/bluetooth.h>
#include <zephyr/meshbus/notify.h>
#include <zephyr/meshbus/power.h>

#include "common/settings.h"

LOG_MODULE_REGISTER(meshbus_bluetooth, CONFIG_MESHBUS_BLUETOOTH_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* ZBus Channels                                                              */
/* -------------------------------------------------------------------------- */

static bool bluetooth_pairing_event_validator(const void *msg, size_t msg_size)
{
	if (msg == NULL) {
		return false;
	}
	if (msg_size != sizeof(struct meshbus_bluetooth_pairing_event)) {
		return false;
	}

	const struct meshbus_bluetooth_pairing_event *ev =
		(const struct meshbus_bluetooth_pairing_event *)msg;

	return ev->passkey <= 999999U;
}

static bool bluetooth_state_event_validator(const void *msg, size_t msg_size)
{
	if (msg == NULL) {
		return false;
	}
	if (msg_size != sizeof(struct meshbus_bluetooth_state_event)) {
		return false;
	}

	const struct meshbus_bluetooth_state_event *ev =
		(const struct meshbus_bluetooth_state_event *)msg;

	return (uint32_t)ev->state <= (uint32_t)MESHBUS_BLUETOOTH_STATE_CONNECTED;
}

ZBUS_CHAN_DEFINE(meshbus_bluetooth_pairing_chan, struct meshbus_bluetooth_pairing_event,
		 bluetooth_pairing_event_validator, /* validator */
		 NULL,                              /* user_data */
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(meshbus_bluetooth_state_chan, struct meshbus_bluetooth_state_event,
		 bluetooth_state_event_validator, /* validator */
		 NULL,                            /* user_data */
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

/* -------------------------------------------------------------------------- */
/* Statistics                                                                 */
/* -------------------------------------------------------------------------- */

#ifdef CONFIG_MESHBUS_BLUETOOTH_STATS
STATS_SECT_DECL(meshbus_bluetooth_stats) meshbus_bluetooth_stats;
static struct k_spinlock bluetooth_stats_lock;

#define BLUETOOTH_STATS_INC(_field)                                                           \
	do {                                                                                   \
		k_spinlock_key_t key = k_spin_lock(&bluetooth_stats_lock);                      \
		STATS_INC(meshbus_bluetooth_stats, _field);                                     \
		k_spin_unlock(&bluetooth_stats_lock, key);                                      \
	} while (false)
#else
#define BLUETOOTH_STATS_INC(_field) do { } while (false)
#endif

/* -------------------------------------------------------------------------- */
/* Defaults And State                                                         */
/* -------------------------------------------------------------------------- */

#if defined(CONFIG_BT_CTLR_TX_PWR_DBM)
#define MESHBUS_BT_ADV_TX_POWER_DBM CONFIG_BT_CTLR_TX_PWR_DBM
#else
#define MESHBUS_BT_ADV_TX_POWER_DBM BT_GAP_TX_POWER_INVALID
#endif

static const int8_t adv_tx_power_dbm = (int8_t)MESHBUS_BT_ADV_TX_POWER_DBM;

#if defined(CONFIG_MESHBUS_MESHCORE_COMPANION_BLUETOOTH)
static const uint8_t nus_service_uuid[] = { BT_UUID_NUS_SRV_VAL };
#endif

static const uint8_t adv_flags = (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR);

#define MESHBUS_BLUETOOTH_NOTIFY_MSGQ_DEPTH 4U
#define MESHBUS_BLUETOOTH_NAME_SUFFIX_BYTES 4U
#define MESHBUS_BLUETOOTH_NAME_SUFFIX_LEN (MESHBUS_BLUETOOTH_NAME_SUFFIX_BYTES * 2U)
#define MESHBUS_BLUETOOTH_NAME_COMPANION_PREFIX "MeshCore-"
#define MESHBUS_BLUETOOTH_NAME_MESHBUS_PREFIX "Meshbus "

#if defined(CONFIG_MESHBUS_BLUETOOTH_GATT_NOTIFY)
#define MESHBUS_BLUETOOTH_NOTIFY_SERVICE_UUID_VALUE \
	BT_UUID_128_ENCODE(0x9d8e9f10, 0x6137, 0x4c6a, 0x9a6a, 0x9f3ad4c00100)
#define MESHBUS_BLUETOOTH_NOTIFY_CHARACTERISTIC_UUID_VALUE \
	BT_UUID_128_ENCODE(0x9d8e9f10, 0x6137, 0x4c6a, 0x9a6a, 0x9f3ad4c00101)
#endif

#define MESHBUS_BLUETOOTH_CONFIG_DEFAULTS                                                          \
	{                                                                                          \
		.enabled = IS_ENABLED(CONFIG_MESHBUS_BLUETOOTH_DEFAULT_ENABLED),                   \
		.passkey_mode = (meshbus_bluetooth_passkey_mode)                                   \
			CONFIG_MESHBUS_BLUETOOTH_DEFAULT_PASSKEY_MODE,                             \
		.fixed_passkey = CONFIG_MESHBUS_BLUETOOTH_DEFAULT_FIXED_PASSKEY,                   \
		.meshcore_companion_enabled =                                                  \
			IS_ENABLED(CONFIG_MESHBUS_MESHCORE_COMPANION_BLUETOOTH),                   \
	}

/* Persisted configuration (protected by settings_mutex). */
static meshbus_bluetooth_config bluetooth_cfg = MESHBUS_BLUETOOTH_CONFIG_DEFAULTS;

/* Lock-free mirrors for callback / hot paths. */
static atomic_t bt_enabled_flag = ATOMIC_INIT(IS_ENABLED(CONFIG_MESHBUS_BLUETOOTH_DEFAULT_ENABLED));
static atomic_t bt_passkey_mode_atomic = ATOMIC_INIT(CONFIG_MESHBUS_BLUETOOTH_DEFAULT_PASSKEY_MODE);
static atomic_t bt_fixed_passkey_atomic =
	ATOMIC_INIT(CONFIG_MESHBUS_BLUETOOTH_DEFAULT_FIXED_PASSKEY);
static atomic_t bt_meshcore_companion_enabled_flag =
	ATOMIC_INIT(IS_ENABLED(CONFIG_MESHBUS_MESHCORE_COMPANION_BLUETOOTH));

/* Single-connection policy. */
static atomic_t conn_active_flag = ATOMIC_INIT(0);

/* Published Bluetooth state (deduplicate ZBus publishes). */
static atomic_t bt_state_atomic = ATOMIC_INIT(MESHBUS_BLUETOOTH_STATE_DISABLED);

#if defined(CONFIG_MESHBUS_BLUETOOTH_GATT_NOTIFY)
static atomic_t notify_ccc_enabled_flag = ATOMIC_INIT(0);
static struct k_work bluetooth_notify_work;
K_MSGQ_DEFINE(bluetooth_notify_msgq, sizeof(meshbus_notify_event),
	      MESHBUS_BLUETOOTH_NOTIFY_MSGQ_DEPTH, 4);
#endif

/*
 * Lock ordering:
 * - settings_mutex (persisted configuration) outermost
 * - state_mutex (connection/runtime state) inner
 *
 * Other mutexes (bt_stack_mutex/cb_mutex/cts_mutex) are independent and must not
 * be held while taking settings_mutex/state_mutex.
 */

/* Runtime connection state (protected by state_mutex). */
static K_MUTEX_DEFINE(state_mutex);
static struct bt_conn *active_conn;
static bt_addr_le_t active_peer_identity;
static bool active_peer_identity_valid;

/* Serialize settings_handler_apply() to avoid concurrent enable/disable races. */
static K_MUTEX_DEFINE(apply_mutex);

/* Settings state (protected by settings_mutex). */
static K_MUTEX_DEFINE(settings_mutex);
static bool settings_initial_apply = false;
static struct k_work_delayable settings_persistence_work;
static meshbus_bluetooth_config settings_load_cfg = MESHBUS_BLUETOOTH_CONFIG_DEFAULTS;
static struct mb_settings_blob_load_state settings_load_state;

/* Advertising refresh work. */
static struct k_work_delayable advertise_work;

/* Serialize bt_enable()/bt_disable() to keep stack lifecycle predictable. */
static K_MUTEX_DEFINE(bt_stack_mutex);

/* Deferred security upgrade (avoid heavy work in conn callbacks). */
static struct k_work_delayable security_work;

/* Serialize auth callback registration (must be done once, after stack is ready). */
static K_MUTEX_DEFINE(cb_mutex);
static bool auth_cb_registered;
static bool auth_info_cb_registered;

struct bluetooth_evt_pending {
	bool have_state;
	enum meshbus_bluetooth_state state;

	bool have_passkey;
	uint32_t passkey;

	bool do_disconnect;
	struct bt_conn *disconnect_conn;
	uint8_t disconnect_reason;

	bool do_unpair;
	bt_addr_le_t unpair_addr;
	bool unpair_addr_valid;
};

static struct bluetooth_evt_pending evt_pending;
static struct k_work evt_work;
static struct k_spinlock evt_lock;

/* -------------------------------------------------------------------------- */
/* Declarations                                                               */
/* -------------------------------------------------------------------------- */

static void adv_refresh_submit(void);
static void adv_refresh_submit_delayed(k_timeout_t delay);
static int bluetooth_auth_register_once(void);
static bool any_le_connection_connected(void);
static bool bluetooth_is_enabled(void);
static bool bluetooth_config_meshcore_companion_enabled(const meshbus_bluetooth_config *cfg);
static void bluetooth_config_normalize(meshbus_bluetooth_config *cfg);
static int bluetooth_device_name_apply(bool companion_enabled);
#if defined(CONFIG_MESHBUS_BLUETOOTH_GATT_NOTIFY)
static void bluetooth_notify_work_handler(struct k_work *work);
static void bluetooth_notify_listener_cb(const struct zbus_channel *chan);
#endif

/* -------------------------------------------------------------------------- */
/* Validation And Helpers                                                     */
/* -------------------------------------------------------------------------- */

static bool bluetooth_config_meshcore_companion_enabled(const meshbus_bluetooth_config *cfg)
{
	if (cfg == NULL) {
		return false;
	}

	if (!IS_ENABLED(CONFIG_MESHBUS_MESHCORE_COMPANION_BLUETOOTH)) {
		return false;
	}

	return cfg->meshcore_companion_enabled;
}

static void bluetooth_config_normalize(meshbus_bluetooth_config *cfg)
{
	if (cfg == NULL) {
		return;
	}

	if (!IS_ENABLED(CONFIG_MESHBUS_MESHCORE_COMPANION_BLUETOOTH)) {
		cfg->meshcore_companion_enabled = false;
	}
}

static void bluetooth_device_id_suffix(char *suffix, size_t suffix_size)
{
	uint8_t id[MESHBUS_BLUETOOTH_NAME_SUFFIX_BYTES] = {0};

	if (suffix == NULL || suffix_size <= MESHBUS_BLUETOOTH_NAME_SUFFIX_LEN) {
		return;
	}

	if (IS_ENABLED(CONFIG_HWINFO)) {
		uint8_t full_id[16] = {0};
		ssize_t id_len = hwinfo_get_device_id(full_id, sizeof(full_id));

		if (id_len >= (ssize_t)sizeof(id)) {
			memcpy(id, full_id, sizeof(id));
		}
	}

	(void)snprintk(suffix, suffix_size, "%02X%02X%02X%02X", id[0], id[1], id[2], id[3]);
}

static int bluetooth_device_name_build(bool companion_enabled, char *name, size_t name_size)
{
	char suffix[MESHBUS_BLUETOOTH_NAME_SUFFIX_LEN + 1U] = {0};
	const char *prefix = companion_enabled ? MESHBUS_BLUETOOTH_NAME_COMPANION_PREFIX :
						     MESHBUS_BLUETOOTH_NAME_MESHBUS_PREFIX;
	int len;

	if (name == NULL || name_size == 0U) {
		return -EINVAL;
	}

	bluetooth_device_id_suffix(suffix, sizeof(suffix));
	len = snprintk(name, name_size, "%s%s", prefix, suffix);
	if (len < 0 || (size_t)len >= name_size) {
		return -ENOMEM;
	}

	return 0;
}

static int bluetooth_device_name_apply(bool companion_enabled)
{
	char name[CONFIG_BT_DEVICE_NAME_MAX + 1U] = {0};
	const char *current;
	int rc;

	rc = bluetooth_device_name_build(companion_enabled, name, sizeof(name));
	if (rc != 0) {
		return rc;
	}

	current = bt_get_name();
	if (current != NULL && strcmp(current, name) == 0) {
		return 0;
	}

	rc = bt_set_name(name);
	if (rc != 0) {
		LOG_ERR("Bluetooth device name update failed: %d", rc);
		return rc;
	}

	LOG_INF("Bluetooth device name set to %s", name);
	return 0;
}

static int bluetooth_config_validate(const meshbus_bluetooth_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	if (cfg->passkey_mode > MESHBUS_BLUETOOTH_PASSKEY_MODE_FIXED) {
		LOG_ERR("Invalid passkey_mode: %u", (uint32_t)cfg->passkey_mode);
		return -EINVAL;
	}

	if (cfg->fixed_passkey > 999999U) {
		LOG_ERR("Invalid fixed_passkey: %u (valid: 0-999999)", cfg->fixed_passkey);
		return -EINVAL;
	}

	return 0;
}

static void runtime_flags_sync(const meshbus_bluetooth_config *cfg)
{
	atomic_set(&bt_enabled_flag, cfg->enabled ? 1 : 0);
	atomic_set(&bt_passkey_mode_atomic, (atomic_val_t)cfg->passkey_mode);
	atomic_set(&bt_fixed_passkey_atomic, (atomic_val_t)cfg->fixed_passkey);
	atomic_set(&bt_meshcore_companion_enabled_flag,
		   bluetooth_config_meshcore_companion_enabled(cfg) ? 1 : 0);
}

static bool bluetooth_is_enabled(void)
{
	return atomic_get(&bt_enabled_flag) != 0;
}

static meshbus_bluetooth_passkey_mode bluetooth_passkey_mode(void)
{
	atomic_val_t val = atomic_get(&bt_passkey_mode_atomic);

	if (val < (atomic_val_t)MESHBUS_BLUETOOTH_PASSKEY_MODE_RANDOM ||
	    val > (atomic_val_t)MESHBUS_BLUETOOTH_PASSKEY_MODE_FIXED) {
		return MESHBUS_BLUETOOTH_PASSKEY_MODE_RANDOM;
	}

	return (meshbus_bluetooth_passkey_mode)val;
}

static uint32_t bluetooth_fixed_passkey(void)
{
	atomic_val_t val = atomic_get(&bt_fixed_passkey_atomic);
	if (val < 0) {
		return 0U;
	}
	return (uint32_t)val;
}

static int bluetooth_stack_ensure_ready(void)
{
	bool bt_just_enabled = false;

	k_mutex_lock(&bt_stack_mutex, K_FOREVER);

	if (bt_is_ready()) {
		k_mutex_unlock(&bt_stack_mutex);
		return 0;
	}

	int rc = bt_enable(NULL);
	if (rc != 0 && rc != -EALREADY) {
		LOG_ERR("Bluetooth enable failed: %d", rc);
		k_mutex_unlock(&bt_stack_mutex);
		return rc;
	}

	bt_just_enabled = (rc == 0);
	k_mutex_unlock(&bt_stack_mutex);

	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		/* Load bond/keys after bt_enable(). */
		rc = settings_load_subtree("bt");
		if (rc != 0) {
			LOG_WRN("Bluetooth settings load failed: %d", rc);
		}
	}

	if (bt_just_enabled) {
		LOG_DBG("Bluetooth stack enabled");
	}

	return 0;
}

static void bluetooth_state_publish(enum meshbus_bluetooth_state state)
{
	atomic_val_t prev = atomic_get(&bt_state_atomic);
	if (prev == (atomic_val_t)state) {
		return;
	}

	atomic_set(&bt_state_atomic, (atomic_val_t)state);

	struct meshbus_bluetooth_state_event ev = {
		.state = state,
	};

	int rc = zbus_chan_pub(&meshbus_bluetooth_state_chan, &ev, K_NO_WAIT);
	if (rc != 0) {
		LOG_DBG("State publish failed: %d", rc);
	}
}

static void bluetooth_pairing_publish(uint32_t passkey)
{
	struct meshbus_bluetooth_pairing_event ev = {
		.passkey = passkey,
	};

	int rc = zbus_chan_pub(&meshbus_bluetooth_pairing_chan, &ev, K_NO_WAIT);
	if (rc != 0) {
		LOG_DBG("Pairing publish failed: %d", rc);
	}
}

static void bluetooth_state_publish_from_connection(void)
{
	if (!bluetooth_is_enabled()) {
		bluetooth_state_publish(MESHBUS_BLUETOOTH_STATE_DISABLED);
		return;
	}

	if (atomic_get(&conn_active_flag) == 0) {
		bluetooth_state_publish(MESHBUS_BLUETOOTH_STATE_DISCONNECTED);
		return;
	}

	struct bt_conn *conn = NULL;

	k_mutex_lock(&state_mutex, K_FOREVER);
	if (active_conn != NULL) {
		conn = bt_conn_ref(active_conn);
	}
	k_mutex_unlock(&state_mutex);

	if (conn == NULL) {
		/*
		 * If our local active_conn pointer is stale but there is still an LE
		 * connection, keep the last published state instead of guessing.
		 */
		if (!any_le_connection_connected()) {
			bluetooth_state_publish(MESHBUS_BLUETOOTH_STATE_DISCONNECTED);
		}
		return;
	}

	bt_security_t level = bt_conn_get_security(conn);
	bt_conn_unref(conn);

	bluetooth_state_publish((level >= BT_SECURITY_L4) ? MESHBUS_BLUETOOTH_STATE_CONNECTED
							  : MESHBUS_BLUETOOTH_STATE_PAIRING);
}

/* -------------------------------------------------------------------------- */
/* Settings Schema And Apply                                                  */
/* -------------------------------------------------------------------------- */

#define MESHBUS_BLUETOOTH_SETTINGS_SUBTREE "meshbus/bluetooth"
#define MESHBUS_BLUETOOTH_SETTINGS_KEY_CONFIG "config"

MB_SETTINGS_BLOB_SCHEMA_DEFINE(bluetooth_settings_schema, MESHBUS_BLUETOOTH_SETTINGS_SUBTREE,
			       MESHBUS_BLUETOOTH_SETTINGS_KEY_CONFIG,
			       meshbus_BluetoothConfig, meshbus_bluetooth_config);

static int settings_handler_apply(const meshbus_bluetooth_config *cfg, bool persistence, bool force)
{
	meshbus_bluetooth_config next;
	int rc = 0;
	bool companion_changed = false;
	bool companion_enabled = false;

	if (cfg == NULL) {
		return -EINVAL;
	}

	memcpy(&next, cfg, sizeof(next));
	bluetooth_config_normalize(&next);
	cfg = &next;

	k_mutex_lock(&apply_mutex, K_FOREVER);

	rc = bluetooth_config_validate(cfg);
	if (rc != 0) {
		goto out;
	}

	bool unchanged;

	k_mutex_lock(&settings_mutex, K_FOREVER);

	if (!settings_initial_apply) {
		settings_initial_apply = true;
	}

	unchanged = (bluetooth_cfg.enabled == cfg->enabled) &&
		    (bluetooth_cfg.passkey_mode == cfg->passkey_mode) &&
		    (bluetooth_cfg.fixed_passkey == cfg->fixed_passkey) &&
		    (bluetooth_config_meshcore_companion_enabled(&bluetooth_cfg) ==
		     bluetooth_config_meshcore_companion_enabled(cfg));
	companion_changed =
		bluetooth_cfg.enabled && cfg->enabled &&
		(bluetooth_config_meshcore_companion_enabled(&bluetooth_cfg) !=
		 bluetooth_config_meshcore_companion_enabled(cfg));
	companion_enabled = bluetooth_config_meshcore_companion_enabled(cfg);
	if (!force && unchanged) {
		k_mutex_unlock(&settings_mutex);
		LOG_DBG("Settings unchanged, nothing to apply");
		goto out;
	}
	k_mutex_unlock(&settings_mutex);

	LOG_INF("Settings apply: enabled=%d passkey_mode=%u fixed_passkey_set=%d companion=%d",
		(int)cfg->enabled, (uint32_t)cfg->passkey_mode,
		cfg->passkey_mode == MESHBUS_BLUETOOTH_PASSKEY_MODE_FIXED ? 1 : 0,
		(int)companion_enabled);

	if (cfg->enabled) {
		rc = bluetooth_stack_ensure_ready();
		if (rc != 0) {
			goto out;
		}

		rc = bluetooth_auth_register_once();
		if (rc != 0) {
			goto out;
		}

		rc = bluetooth_device_name_apply(companion_enabled);
		if (rc != 0) {
			goto out;
		}

#if defined(CONFIG_BT_BAS)
		/* Provide a deterministic demo battery level for BAS (placeholder). */
		if (IS_ENABLED(CONFIG_MESHBUS_BLUETOOTH_GATT_BAS)) {
			int bas_rc = bt_bas_set_battery_level(
				(uint8_t)CONFIG_MESHBUS_BLUETOOTH_BATTERY_DEMO_LEVEL);
			if (bas_rc != 0) {
				LOG_WRN("BAS demo level set failed: %d", bas_rc);
			}
		}
#endif
	}

	k_mutex_lock(&settings_mutex, K_FOREVER);
	memcpy(&bluetooth_cfg, cfg, sizeof(bluetooth_cfg));
	runtime_flags_sync(cfg);
	k_mutex_unlock(&settings_mutex);

	if (!cfg->enabled) {
		/* Wait for in-flight work to finish to avoid racing with adv start/security. */
		struct k_work_sync sync;
		(void)k_work_cancel_delayable_sync(&advertise_work, &sync);
		(void)k_work_cancel_delayable_sync(&security_work, &sync);
		(void)bt_le_adv_stop();

		struct bt_conn *conn_to_disc = NULL;
		k_mutex_lock(&state_mutex, K_FOREVER);
		if (active_conn != NULL) {
			conn_to_disc = bt_conn_ref(active_conn);
		}
		k_mutex_unlock(&state_mutex);

		if (conn_to_disc != NULL) {
			(void)bt_conn_disconnect(conn_to_disc, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
			bt_conn_unref(conn_to_disc);
		}

		atomic_set(&conn_active_flag, 0);
#if defined(CONFIG_MESHBUS_BLUETOOTH_GATT_NOTIFY)
		atomic_set(&notify_ccc_enabled_flag, 0);
#endif
		bluetooth_state_publish(MESHBUS_BLUETOOTH_STATE_DISABLED);
	} else {
		adv_refresh_submit();
		bluetooth_state_publish_from_connection();

		if (companion_changed) {
			struct bt_conn *conn_to_disc = NULL;

			k_mutex_lock(&state_mutex, K_FOREVER);
			if (active_conn != NULL) {
				conn_to_disc = bt_conn_ref(active_conn);
			}
			k_mutex_unlock(&state_mutex);

			if (conn_to_disc != NULL) {
				(void)bt_conn_disconnect(conn_to_disc,
							 BT_HCI_ERR_REMOTE_USER_TERM_CONN);
				bt_conn_unref(conn_to_disc);
			}
		}
	}

	if (persistence) {
		k_work_reschedule(&settings_persistence_work,
				  K_MSEC(CONFIG_MESHBUS_SETTINGS_PERSISTENCE_DELAY));
	}

out:
	k_mutex_unlock(&apply_mutex);
	return rc;
}

MB_SETTINGS_BLOB_CONFIG_DEFINE(bluetooth_settings_schema, settings_mutex, settings_load_state,
			       settings_load_cfg, bluetooth_cfg, settings_initial_apply,
			       meshbus_bluetooth_config, meshbus_BluetoothConfig_size,
			       settings_handler_apply, "Bluetooth")

SETTINGS_STATIC_HANDLER_DEFINE(meshbus_bluetooth, MESHBUS_BLUETOOTH_SETTINGS_SUBTREE, NULL,
			       settings_handle_set, settings_handle_commit,
			       settings_handle_export);

/* -------------------------------------------------------------------------- */
/* Callbacks And Work                                                         */
/* -------------------------------------------------------------------------- */

#if defined(CONFIG_MESHBUS_BLUETOOTH_GATT_NOTIFY)
static void bluetooth_notify_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	atomic_set(&notify_ccc_enabled_flag, (value == BT_GATT_CCC_NOTIFY) ? 1 : 0);
	LOG_DBG("Meshbus notify CCC %s", value == BT_GATT_CCC_NOTIFY ? "enabled" : "disabled");
}

static struct bt_uuid_128 meshbus_bt_notify_service_uuid = BT_UUID_INIT_128(
	MESHBUS_BLUETOOTH_NOTIFY_SERVICE_UUID_VALUE);
static struct bt_uuid_128 meshbus_bt_notify_char_uuid = BT_UUID_INIT_128(
	MESHBUS_BLUETOOTH_NOTIFY_CHARACTERISTIC_UUID_VALUE);

BT_GATT_SERVICE_DEFINE(meshbus_bt_notify_svc,
	BT_GATT_PRIMARY_SERVICE(&meshbus_bt_notify_service_uuid),
	BT_GATT_CHARACTERISTIC(&meshbus_bt_notify_char_uuid.uuid,
			       BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(bluetooth_notify_ccc_changed,
		    BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN));

static struct bt_conn *bluetooth_notify_active_conn_ref(void)
{
	struct bt_conn *conn = NULL;

	k_mutex_lock(&state_mutex, K_FOREVER);
	if (active_conn != NULL) {
		conn = bt_conn_ref(active_conn);
	}
	k_mutex_unlock(&state_mutex);

	return conn;
}

static bool bluetooth_notify_ready(struct bt_conn *conn)
{
	if (!meshbus_bluetooth_connection_is_authorized(conn)) {
		return false;
	}
	if (atomic_get(&notify_ccc_enabled_flag) == 0) {
		return false;
	}

	return true;
}

static int bluetooth_notify_send_event(const meshbus_notify_event *event)
{
	struct bt_conn *conn = NULL;
	size_t budget;
	size_t mtu;
	int rc;

	if (event == NULL) {
		return -EINVAL;
	}

	BLUETOOTH_STATS_INC(notify_attempted);

	conn = bluetooth_notify_active_conn_ref();
	if (!bluetooth_notify_ready(conn)) {
		rc = -ENOTCONN;
		goto out;
	}

	mtu = bt_gatt_get_mtu(conn);
	budget = (mtu > 3U) ? (mtu - 3U) : 0U;
	if (budget == 0U) {
		rc = -EMSGSIZE;
		goto out;
	}
	if (event->payload_len == 0U || event->payload_len > budget) {
		rc = -EMSGSIZE;
		goto out;
	}

	/*
	 * Let Zephyr select the subscribed connection from the CCC table. Real
	 * iOS reconnects can leave the service-level active_conn pointer valid
	 * enough for connection state/MTU checks while conn-specific subscription
	 * lookup no longer matches the peer that owns the CCC subscription.
	 */
	rc = bt_gatt_notify(NULL, &meshbus_bt_notify_svc.attrs[2], event->payload,
			    event->payload_len);
	if (rc == 0) {
		BLUETOOTH_STATS_INC(notify_sent);
	}

out:
	if (rc != 0) {
		BLUETOOTH_STATS_INC(notify_dropped);
		LOG_DBG("Meshbus BLE notify drop: type=%u rc=%d", (unsigned int)event->type, rc);
	}
	if (conn != NULL) {
		bt_conn_unref(conn);
	}

	return rc;
}

static void bluetooth_notify_work_handler(struct k_work *work)
{
	meshbus_notify_event event = MESHBUS_NOTIFY_EVENT_INIT_ZERO;

	ARG_UNUSED(work);

	while (k_msgq_get(&bluetooth_notify_msgq, &event, K_NO_WAIT) == 0) {
		(void)bluetooth_notify_send_event(&event);
	}
}

static void bluetooth_notify_listener_cb(const struct zbus_channel *chan)
{
	const meshbus_notify_event *event;
	int rc;

	if (chan != &meshbus_notify_chan) {
		return;
	}

	event = zbus_chan_const_msg(chan);
	if (event == NULL) {
		return;
	}

	rc = k_msgq_put(&bluetooth_notify_msgq, event, K_NO_WAIT);
	if (rc != 0) {
		BLUETOOTH_STATS_INC(notify_dropped);
		LOG_DBG("Meshbus BLE notify queue full: rc=%d", rc);
		return;
	}

	rc = k_work_submit(&bluetooth_notify_work);
	if (rc < 0 && rc != -EBUSY) {
		BLUETOOTH_STATS_INC(notify_dropped);
		LOG_DBG("Meshbus BLE notify work submit failed: rc=%d", rc);
	}
}

ZBUS_LISTENER_DEFINE(meshbus_bluetooth_notify_listener, bluetooth_notify_listener_cb);
#endif

static void bluetooth_evt_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	struct bluetooth_evt_pending pending = {0};

	k_spinlock_key_t key = k_spin_lock(&evt_lock);
	pending = evt_pending;
	memset(&evt_pending, 0, sizeof(evt_pending));
	k_spin_unlock(&evt_lock, key);

	/* Bond repair first so the next pairing attempt won't be blocked by stale keys. */
	if (pending.do_unpair && pending.unpair_addr_valid) {
		int rc = bt_unpair(BT_ID_DEFAULT, &pending.unpair_addr);
		if (rc != 0) {
			LOG_WRN("Unpair failed: %d", rc);
		} else {
			LOG_INF("Bond removed (auto-repair)");
		}
	}

	if (pending.do_disconnect) {
		if (pending.disconnect_conn != NULL) {
			(void)bt_conn_disconnect(pending.disconnect_conn,
						 pending.disconnect_reason);
			bt_conn_unref(pending.disconnect_conn);
		}
	}

	if (pending.have_passkey) {
		bluetooth_pairing_publish(pending.passkey);
	}

	if (pending.have_state) {
		bluetooth_state_publish(pending.state);
	}
}

static void bluetooth_evt_request_state(enum meshbus_bluetooth_state state)
{
	k_spinlock_key_t key = k_spin_lock(&evt_lock);
	evt_pending.have_state = true;
	evt_pending.state = state;
	k_spin_unlock(&evt_lock, key);

	(void)k_work_submit(&evt_work);
}

static void bluetooth_evt_request_passkey(uint32_t passkey)
{
	k_spinlock_key_t key = k_spin_lock(&evt_lock);
	evt_pending.have_passkey = true;
	evt_pending.passkey = passkey;
	k_spin_unlock(&evt_lock, key);

	(void)k_work_submit(&evt_work);
}

static void bluetooth_evt_request_disconnect(struct bt_conn *conn, uint8_t reason)
{
	if (conn == NULL) {
		return;
	}

	struct bt_conn *ref = bt_conn_ref(conn);
	struct bt_conn *old = NULL;

	k_spinlock_key_t key = k_spin_lock(&evt_lock);
	evt_pending.do_disconnect = true;
	evt_pending.disconnect_reason = reason;
	old = evt_pending.disconnect_conn;
	evt_pending.disconnect_conn = ref;
	k_spin_unlock(&evt_lock, key);

	/* Drop any previous pending reference outside the spinlock. */
	if (old != NULL) {
		bt_conn_unref(old);
	}

	(void)k_work_submit(&evt_work);
}

static void bluetooth_evt_request_unpair(const bt_addr_le_t *addr)
{
	if (addr == NULL) {
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&evt_lock);
	evt_pending.do_unpair = true;
	evt_pending.unpair_addr = *addr;
	evt_pending.unpair_addr_valid = true;
	k_spin_unlock(&evt_lock, key);

	(void)k_work_submit(&evt_work);
}

#if defined(CONFIG_BT_CTS)
/*
 * Current Time Service (CTS) requires the application to register callbacks.
 * Without bt_cts_init(), CTS will dereference a NULL callback pointer when a
 * client reads/writes or enables notifications, causing a crash.
 *
 * Keep the implementation minimal and side-effect free: store the last time
 * written by a client and return it on reads/notifications.
 */
static K_MUTEX_DEFINE(cts_mutex);
static bool cts_initialized;

/* Default to a sane fixed timestamp (2026-01-01 00:00:00 Thu) until set by a peer. */
static struct bt_cts_time_format cts_time = {
	.year = 0, /* filled at init */
	.mon = 1,
	.mday = 1,
	.hours = 0,
	.min = 0,
	.sec = 0,
	.wday = 4, /* Monday=1 ... Sunday=7. 2026-01-01 is a Thursday. */
	.fractions256 = 0,
	.reason = BT_CTS_UPDATE_REASON_UNKNOWN,
};

static atomic_t cts_notify_enabled_flag = ATOMIC_INIT(0);

static void meshbus_cts_notification_changed(bool enabled)
{
	atomic_set(&cts_notify_enabled_flag, enabled ? 1 : 0);
	LOG_DBG("CTS notify %s", enabled ? "enabled" : "disabled");
}

static int meshbus_cts_time_write(struct bt_cts_time_format *new_time)
{
	if (new_time == NULL) {
		return -EINVAL;
	}

	/* Store raw CTS fields; year is little-endian on the wire and in the struct. */
	k_mutex_lock(&cts_mutex, K_FOREVER);
	cts_time = *new_time;
	k_mutex_unlock(&cts_mutex);

	LOG_DBG("CTS time updated by peer: %u-%02u-%02u %02u:%02u:%02u (wday=%u)",
		(uint32_t)sys_le16_to_cpu(new_time->year), new_time->mon, new_time->mday,
		new_time->hours, new_time->min, new_time->sec, new_time->wday);

	return 0;
}

static int meshbus_cts_fill_current_time(struct bt_cts_time_format *out_time)
{
	if (out_time == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&cts_mutex, K_FOREVER);
	*out_time = cts_time;
	k_mutex_unlock(&cts_mutex);

	return 0;
}

static const struct bt_cts_cb meshbus_cts_cb = {
	.notification_changed = meshbus_cts_notification_changed,
	.cts_time_write = meshbus_cts_time_write,
	.fill_current_cts_time = meshbus_cts_fill_current_time,
};

static int meshbus_cts_init_once(void)
{
	if (!IS_ENABLED(CONFIG_BT_CTS)) {
		return 0;
	}

	k_mutex_lock(&cts_mutex, K_FOREVER);
	bool already = cts_initialized;
	if (!already) {
		cts_time.year = sys_cpu_to_le16(2026);
	}
	k_mutex_unlock(&cts_mutex);

	if (already) {
		return 0;
	}

	int rc = bt_cts_init(&meshbus_cts_cb);
	if (rc != 0) {
		LOG_ERR("CTS init failed: %d", rc);
		return rc;
	}

	k_mutex_lock(&cts_mutex, K_FOREVER);
	cts_initialized = true;
	k_mutex_unlock(&cts_mutex);

	LOG_DBG("CTS initialized");
	return 0;
}
#endif /* CONFIG_BT_CTS */

struct conn_scan_ctx {
	bool connected;
};

static void conn_addr_to_str(struct bt_conn *conn, char *addr, size_t size)
{
	const bt_addr_le_t *dst = bt_conn_get_dst(conn);

	if (dst != NULL) {
		bt_addr_le_to_str(dst, addr, size);
		return;
	}

	if (size > 0U) {
		strncpy(addr, "<unknown>", size);
		addr[size - 1U] = '\0';
	}
}

static void conn_scan_cb(struct bt_conn *conn, void *data)
{
	struct conn_scan_ctx *ctx = (struct conn_scan_ctx *)data;
	struct bt_conn_info info;

	if (ctx == NULL || ctx->connected) {
		return;
	}

	if (bt_conn_get_info(conn, &info) != 0) {
		return;
	}

	if (info.type == BT_CONN_TYPE_LE && info.state == BT_CONN_STATE_CONNECTED) {
		ctx->connected = true;
	}
}

static bool any_le_connection_connected(void)
{
	struct conn_scan_ctx ctx = {0};

	bt_conn_foreach(BT_CONN_TYPE_LE, conn_scan_cb, &ctx);
	return ctx.connected;
}

/* -------------------------------------------------------------------------- */
/* Bluetooth Auth Callbacks                                                   */
/* -------------------------------------------------------------------------- */

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	conn_addr_to_str(conn, addr, sizeof(addr));
	LOG_INF("Passkey for %s: %06u", addr, passkey);

	/* Keep BT RX WQ stack usage low: defer ZBus publish/state updates. */
	bluetooth_evt_request_passkey((uint32_t)passkey);
	bluetooth_evt_request_state(MESHBUS_BLUETOOTH_STATE_PAIRING);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	conn_addr_to_str(conn, addr, sizeof(addr));
	LOG_WRN("Pairing cancelled: %s", addr);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	ARG_UNUSED(conn);
	LOG_INF("Pairing complete (bonded=%d)", (int)bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	LOG_WRN("Pairing failed (%d), disconnecting", reason);

	/* Defer disconnect out of BT RX WQ context. */
	bluetooth_evt_request_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
	bluetooth_evt_request_state(MESHBUS_BLUETOOTH_STATE_DISCONNECTED);
}

static uint32_t auth_app_passkey(struct bt_conn *conn)
{
	ARG_UNUSED(conn);

	if (bluetooth_passkey_mode() == MESHBUS_BLUETOOTH_PASSKEY_MODE_RANDOM) {
		return BT_PASSKEY_RAND;
	}

	return bluetooth_fixed_passkey();
}

static struct bt_conn_auth_cb auth_cb = {
	.passkey_display = auth_passkey_display,
	.passkey_entry = NULL,
	.cancel = auth_cancel,
#if defined(CONFIG_BT_APP_PASSKEY)
	.app_passkey = auth_app_passkey,
#endif
};

static struct bt_conn_auth_info_cb auth_cb_info = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

static int bluetooth_auth_register_once(void)
{
	int rc = 0;

	k_mutex_lock(&cb_mutex, K_FOREVER);

	if (!auth_cb_registered) {
		rc = bt_conn_auth_cb_register(&auth_cb);
		if (rc != 0) {
			LOG_ERR("Auth callback register failed: %d", rc);
			goto out;
		}
		auth_cb_registered = true;
	}

	if (!auth_info_cb_registered) {
		rc = bt_conn_auth_info_cb_register(&auth_cb_info);
		if (rc != 0) {
			LOG_ERR("Auth info callback register failed: %d", rc);
			goto out;
		}
		auth_info_cb_registered = true;
	}

out:
	k_mutex_unlock(&cb_mutex);
	return rc;
}

static void security_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!bluetooth_is_enabled()) {
		return;
	}

	struct bt_conn *conn = NULL;

	k_mutex_lock(&state_mutex, K_FOREVER);
	if (active_conn != NULL) {
		conn = bt_conn_ref(active_conn);
	}
	k_mutex_unlock(&state_mutex);

	if (conn == NULL) {
		return;
	}

	int rc = bt_conn_set_security(conn, BT_SECURITY_L4);
	if (rc == 0 || rc == -EALREADY) {
		/* ok */
	} else if (rc == -EBUSY) {
		/* Security is already being negotiated (e.g., peer initiated). */
		LOG_DBG("Security upgrade already in progress");
	} else {
		LOG_WRN("Failed to request BT_SECURITY_L4: %d", rc);
		(void)bt_conn_disconnect(conn, BT_HCI_ERR_INSUFFICIENT_SECURITY);
	}

	bt_conn_unref(conn);
}

static void bluetooth_adv_data_fill(struct bt_data *ad, size_t *ad_len, struct bt_data *sd,
				    size_t *sd_len)
{
	const char *name = bt_get_name();
#if defined(CONFIG_MESHBUS_MESHCORE_COMPANION_BLUETOOTH)
	bool companion_enabled = meshbus_bluetooth_meshcore_companion_enabled();
#endif

	*ad_len = 0U;
	*sd_len = 0U;

	ad[(*ad_len)++] = (struct bt_data)BT_DATA(BT_DATA_FLAGS, &adv_flags, sizeof(adv_flags));
	ad[(*ad_len)++] =
		(struct bt_data)BT_DATA(BT_DATA_TX_POWER, &adv_tx_power_dbm,
					sizeof(adv_tx_power_dbm));

#if defined(CONFIG_MESHBUS_MESHCORE_COMPANION_BLUETOOTH)
	if (companion_enabled) {
		ad[(*ad_len)++] = (struct bt_data)BT_DATA(BT_DATA_UUID128_ALL, nus_service_uuid,
							  sizeof(nus_service_uuid));
	}
#endif

	/*
	 * Some scanner apps only surface Tx Power if it appears in the scan response
	 * (or they only display scan-response fields by default). Duplicate the AD
	 * element here for compatibility.
	 */
	sd[(*sd_len)++] =
		(struct bt_data)BT_DATA(BT_DATA_TX_POWER, &adv_tx_power_dbm,
					sizeof(adv_tx_power_dbm));
	if (name != NULL && name[0] != '\0') {
		sd[(*sd_len)++] = (struct bt_data)BT_DATA(BT_DATA_NAME_COMPLETE, name,
							  strlen(name));
	}
}

static void advertise_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	struct bt_data adv_ad[3];
	struct bt_data adv_sd[2];
	size_t adv_ad_len;
	size_t adv_sd_len;

	if (!bluetooth_is_enabled()) {
		return;
	}

	int rc = bluetooth_stack_ensure_ready();
	if (rc != 0) {
		return;
	}

	/* If we're connected, keep advertising off. */
	if (atomic_get(&conn_active_flag) != 0) {
		return;
	}

	/* Convert milliseconds to BLE advertising units (0.625 ms). Clamp to spec range. */
	uint32_t min_ms = (uint32_t)CONFIG_MESHBUS_BLUETOOTH_ADV_INT_MIN_MS;
	uint32_t max_ms = (uint32_t)CONFIG_MESHBUS_BLUETOOTH_ADV_INT_MAX_MS;
	uint32_t min_units = (min_ms * 1000U + 312U) / 625U;
	uint32_t max_units = (max_ms * 1000U + 312U) / 625U;
	min_units = CLAMP(min_units, 0x0020U, 0x4000U);
	max_units = CLAMP(max_units, 0x0020U, 0x4000U);
	if (max_units < min_units) {
		max_units = min_units;
	}

	bluetooth_adv_data_fill(adv_ad, &adv_ad_len, adv_sd, &adv_sd_len);

	const struct bt_le_adv_param adv_param =
		BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONN, min_units, max_units, NULL);

	rc = bt_le_adv_start(&adv_param, adv_ad, adv_ad_len, adv_sd, adv_sd_len);
	if (rc == -EALREADY) {
		/*
		 * Another subsystem (or an earlier init stage) may have already started
		 * connectable advertising. Ensure our ADV/scan-response payload is applied
		 * so TX Power is present in the ADV data.
		 */
		rc = bt_le_adv_update_data(adv_ad, adv_ad_len, adv_sd, adv_sd_len);
		if (rc != 0) {
			LOG_WRN("Advertising update failed: %d", rc);
		}
		return;
	}

	if (rc != 0) {
		LOG_WRN("Advertising start failed: %d", rc);
		/* Transient failures can happen around teardown; retry gently. */
		if (rc == -EBUSY || rc == -EAGAIN) {
			adv_refresh_submit_delayed(K_MSEC(250));
		}
		return;
	}

	LOG_INF("Advertising started");
	bluetooth_state_publish(MESHBUS_BLUETOOTH_STATE_DISCONNECTED);
}

static void adv_refresh_submit(void)
{
	adv_refresh_submit_delayed(K_NO_WAIT);
}

static void adv_refresh_submit_delayed(k_timeout_t delay)
{
	(void)k_work_reschedule(&advertise_work, delay);
}

/* -------------------------------------------------------------------------- */
/* Connection Callbacks                                                       */
/* -------------------------------------------------------------------------- */

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	conn_addr_to_str(conn, addr, sizeof(addr));

	if (err) {
		LOG_WRN("Connect failed: %s err 0x%02x %s", addr, err, bt_hci_err_to_str(err));
		return;
	}

	if (!bluetooth_is_enabled()) {
		LOG_WRN("Rejecting connection while disabled: %s", addr);
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		return;
	}

	if (!atomic_cas(&conn_active_flag, 0, 1)) {
		LOG_WRN("Rejecting extra connection: %s", addr);
		bt_conn_disconnect(conn, BT_HCI_ERR_CONN_LIMIT_EXCEEDED);
		return;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);
	if (active_conn != NULL) {
		bt_conn_unref(active_conn);
		active_conn = NULL;
	}
	active_conn = bt_conn_ref(conn);
	active_peer_identity_valid = false;
	k_mutex_unlock(&state_mutex);

	LOG_INF("Connected: %s", addr);
#if defined(CONFIG_MESHBUS_BLUETOOTH_GATT_NOTIFY)
	atomic_set(&notify_ccc_enabled_flag, 0);
#endif
	bluetooth_evt_request_state(MESHBUS_BLUETOOTH_STATE_PAIRING);

	/* Let the host finish connection bookkeeping before starting SMP/LL procedures. */
	(void)k_work_reschedule(&security_work, K_MSEC(100));
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];
	bool was_active_conn = false;

	conn_addr_to_str(conn, addr, sizeof(addr));
	LOG_INF("Disconnected: %s reason 0x%02x %s", addr, reason, bt_hci_err_to_str(reason));

	k_mutex_lock(&state_mutex, K_FOREVER);
	if (active_conn != NULL && active_conn == conn) {
		bt_conn_unref(active_conn);
		active_conn = NULL;
		was_active_conn = true;
	}
	if (was_active_conn) {
		active_peer_identity_valid = false;
	}
	k_mutex_unlock(&state_mutex);

	if (was_active_conn) {
		(void)k_work_cancel_delayable(&security_work);
	}

	/*
	 * Ignore disconnect callbacks from rejected extra connections.
	 * They must not reset the active-connection state machine.
	 */
	if (!was_active_conn) {
		/*
		 * Defensive recovery: if state got stale but no LE connection is still
		 * active, clear the local state to avoid getting stuck after disconnect.
		 */
		if (atomic_get(&conn_active_flag) == 0) {
			return;
		}
		if (any_le_connection_connected()) {
			return;
		}

		k_mutex_lock(&state_mutex, K_FOREVER);
		if (active_conn != NULL) {
			bt_conn_unref(active_conn);
			active_conn = NULL;
		}
		k_mutex_unlock(&state_mutex);

		atomic_set(&conn_active_flag, 0);
		LOG_WRN("Recovered stale connection state after disconnect");

		if (bluetooth_is_enabled()) {
			bluetooth_evt_request_state(MESHBUS_BLUETOOTH_STATE_DISCONNECTED);
			adv_refresh_submit();
		} else {
			bluetooth_evt_request_state(MESHBUS_BLUETOOTH_STATE_DISABLED);
		}

		return;
	}

	atomic_set(&conn_active_flag, 0);
#if defined(CONFIG_MESHBUS_BLUETOOTH_GATT_NOTIFY)
	atomic_set(&notify_ccc_enabled_flag, 0);
#endif

	if (bluetooth_is_enabled()) {
		bluetooth_evt_request_state(MESHBUS_BLUETOOTH_STATE_DISCONNECTED);
		adv_refresh_submit();
	} else {
		bluetooth_evt_request_state(MESHBUS_BLUETOOTH_STATE_DISABLED);
	}
}

static void identity_resolved(struct bt_conn *conn, const bt_addr_le_t *rpa,
			      const bt_addr_le_t *identity)
{
	char addr_identity[BT_ADDR_LE_STR_LEN];
	char addr_rpa[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(identity, addr_identity, sizeof(addr_identity));
	bt_addr_le_to_str(rpa, addr_rpa, sizeof(addr_rpa));

	LOG_DBG("Identity resolved %s -> %s", addr_rpa, addr_identity);

	/* Cache identity for bond repair (e.g., when peer forgot keys). */
	k_mutex_lock(&state_mutex, K_FOREVER);
	if (active_conn == conn && identity != NULL) {
		active_peer_identity = *identity;
		active_peer_identity_valid = true;
	}
	k_mutex_unlock(&state_mutex);
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	conn_addr_to_str(conn, addr, sizeof(addr));

	if (!err) {
		LOG_INF("Security changed: %s level %u", addr, level);
		bluetooth_evt_request_state((level >= BT_SECURITY_L4)
						    ? MESHBUS_BLUETOOTH_STATE_CONNECTED
						    : MESHBUS_BLUETOOTH_STATE_PAIRING);
		return;
	}

	LOG_WRN("Security failed: %s level %u err %s(%d)", addr, level, bt_security_err_to_str(err),
		err);

	/* If the peer forgot bonding data, remove our stale bond and allow re-pairing. */
	if (err == BT_SECURITY_ERR_PIN_OR_KEY_MISSING) {
		bt_addr_le_t identity;
		bool have_identity = false;
		bt_addr_le_t dst;
		bool have_dst = false;

		k_mutex_lock(&state_mutex, K_FOREVER);
		if (active_conn == conn) {
			if (active_peer_identity_valid) {
				identity = active_peer_identity;
				have_identity = true;
			} else {
				const bt_addr_le_t *d = bt_conn_get_dst(conn);
				if (d != NULL) {
					dst = *d;
					have_dst = true;
				}
			}
		}
		k_mutex_unlock(&state_mutex);

		if (have_identity) {
			bluetooth_evt_request_unpair(&identity);
		} else if (have_dst) {
			bluetooth_evt_request_unpair(&dst);
		}
	}

	bluetooth_evt_request_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
	bluetooth_evt_request_state(MESHBUS_BLUETOOTH_STATE_DISCONNECTED);
}

BT_CONN_CB_DEFINE(meshbus_bluetooth_conn_cbs) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = adv_refresh_submit,
	.identity_resolved = identity_resolved,
	.security_changed = security_changed,
};

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

int meshbus_bluetooth_config_get(meshbus_bluetooth_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&settings_mutex, K_FOREVER);
	memcpy(cfg, &bluetooth_cfg, sizeof(*cfg));
	k_mutex_unlock(&settings_mutex);

	return 0;
}

int meshbus_bluetooth_config_set(const meshbus_bluetooth_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	return settings_handler_apply(cfg, true, false);
}

bool meshbus_bluetooth_meshcore_companion_enabled(void)
{
	if (!bluetooth_is_enabled()) {
		return false;
	}

	if (!IS_ENABLED(CONFIG_MESHBUS_MESHCORE_COMPANION_BLUETOOTH)) {
		return false;
	}

	return atomic_get(&bt_meshcore_companion_enabled_flag) != 0;
}

bool meshbus_bluetooth_connection_is_authorized(const struct bt_conn *conn)
{
	bool is_active;

	if (conn == NULL || !bluetooth_is_enabled() || atomic_get(&conn_active_flag) == 0) {
		return false;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);
	is_active = active_conn == conn;
	k_mutex_unlock(&state_mutex);

	return is_active && bt_conn_get_security(conn) >= BT_SECURITY_L4;
}

int meshbus_bluetooth_config_reset(void)
{
	struct k_work_sync sync;

	/* Prevent stale delayed writes from re-creating the key after reset. */
	(void)k_work_cancel_delayable_sync(&settings_persistence_work, &sync);

	meshbus_bluetooth_config cfg = MESHBUS_BLUETOOTH_CONFIG_DEFAULTS;
	int rc = settings_handler_apply(&cfg, false, true);
	if (rc != 0) {
		return rc;
	}

	rc = mb_settings_blob_delete(&bluetooth_settings_schema);
	if (rc != 0) {
		LOG_WRN("Failed to delete persisted settings: %d", rc);
	}

	LOG_INF("Bluetooth config reset to defaults");
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Power Callback                                                             */
/* -------------------------------------------------------------------------- */

static void meshbus_power_bluetooth_cb(enum meshbus_power_action action, void *user_data)
{
	ARG_UNUSED(user_data);

	if (action != MESHBUS_POWER_ACTION_SHUTDOWN && action != MESHBUS_POWER_ACTION_REBOOT) {
		return;
	}

	/* Quiesce delayed work to prevent re-enabling advertising during teardown. */
	(void)k_work_cancel_delayable(&advertise_work);
	(void)k_work_cancel_delayable(&security_work);
	(void)k_work_cancel_delayable(&settings_persistence_work);

	/* Ensure advertise_work_handler bails even if rescheduled by BT callbacks. */
	atomic_set(&bt_enabled_flag, 0);

	(void)bt_le_adv_stop();

	struct bt_conn *conn = NULL;

	k_mutex_lock(&state_mutex, K_FOREVER);
	if (active_conn != NULL) {
		conn = active_conn;
		active_conn = NULL;
		active_peer_identity_valid = false;
	}
	k_mutex_unlock(&state_mutex);

	if (conn != NULL) {
		(void)bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		bt_conn_unref(conn);
	}

	atomic_set(&conn_active_flag, 0);
#if defined(CONFIG_MESHBUS_BLUETOOTH_GATT_NOTIFY)
	atomic_set(&notify_ccc_enabled_flag, 0);
#endif

	LOG_INF("Bluetooth stopped");
}
MESHBUS_POWER_ACTION_CALLBACK_DEFINE(meshbus_power_bluetooth_cb, NULL);

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

static int meshbus_bluetooth_init(void)
{
	int rc;

#ifdef CONFIG_MESHBUS_BLUETOOTH_STATS
	rc = STATS_INIT_AND_REG(meshbus_bluetooth_stats, STATS_SIZE_32, "meshbus_bluetooth");
	if (rc != 0) {
		LOG_WRN("Failed to register Bluetooth stats: %d", rc);
	}
#endif

#if defined(CONFIG_BT_CTS)
	rc = meshbus_cts_init_once();
	if (rc != 0) {
		return rc;
	}
#endif

	k_work_init_delayable(&advertise_work, advertise_work_handler);
	k_work_init_delayable(&security_work, security_work_handler);
	k_work_init_delayable(&settings_persistence_work, settings_persistence_work_handler);
	k_work_init(&evt_work, bluetooth_evt_work_handler);
#if defined(CONFIG_MESHBUS_BLUETOOTH_GATT_NOTIFY)
	k_work_init(&bluetooth_notify_work, bluetooth_notify_work_handler);
	atomic_set(&notify_ccc_enabled_flag, 0);
	rc = zbus_chan_add_obs(&meshbus_notify_chan, &meshbus_bluetooth_notify_listener, K_NO_WAIT);
	if (rc != 0 && rc != -EALREADY && rc != -EEXIST) {
		return rc;
	}
#endif

	active_peer_identity_valid = false;

	rc = settings_load_subtree(MESHBUS_BLUETOOTH_SETTINGS_SUBTREE);
	if (rc != 0) {
		return rc;
	}

	if (!settings_initial_apply) {
		/* No persisted settings; apply defaults at boot without persisting. */
		rc = settings_handler_apply(&bluetooth_cfg, false, true);
		if (rc != 0) {
			return rc;
		}
	}

	return 0;
}

SYS_INIT(meshbus_bluetooth_init, APPLICATION, CONFIG_MESHBUS_BLUETOOTH_INIT_PRIORITY);
