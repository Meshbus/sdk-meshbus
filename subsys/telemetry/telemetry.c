/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/pm/device_runtime.h>
#include <telemetry/telemetry.h>
#include <power/power.h>

#include "mbs_settings_internal.h"
#include "telemetry_dt.h"

LOG_MODULE_REGISTER(mbs_telemetry, CONFIG_MBS_TELEMETRY_LOG_LEVEL);

static void __maybe_unused telemetry_sensor_value_snprint(char *buf, size_t len,
							  const struct sensor_value *v)
{
	/* Use fixed-point formatting to avoid artifacts like "0.-530010". */
	int64_t micro = sensor_value_to_micro(v);
	bool neg = (micro < 0);
	uint64_t abs_micro = (uint64_t)(neg ? -micro : micro);

	uint64_t ip = abs_micro / 1000000ULL;
	uint64_t fp = abs_micro % 1000000ULL;

	(void)snprintk(buf, len, "%s%llu.%06llu", neg ? "-" : "", (unsigned long long)ip,
		       (unsigned long long)fp);
}

static void __maybe_unused telemetry_sensor_values_snprint(char *buf, size_t len,
							   const struct sensor_value *vals,
							   size_t count)
{
	size_t off = 0U;

	if (len == 0U) {
		return;
	}

	off += (size_t)snprintk(buf + off, len - off, "(");
	for (size_t i = 0; i < count && off < len; i++) {
		char vbuf[24];

		telemetry_sensor_value_snprint(vbuf, sizeof(vbuf), &vals[i]);
		off += (size_t)snprintk(buf + off, len - off, "%s%s", (i == 0U) ? "" : ", ", vbuf);
	}
	(void)snprintk(buf + off, len - off, ")");
}

size_t mbs_telemetry_channel_value_count(enum sensor_channel chan)
{
	switch (chan) {
	case SENSOR_CHAN_ACCEL_XYZ:
	case SENSOR_CHAN_GYRO_XYZ:
	case SENSOR_CHAN_MAGN_XYZ:
	case SENSOR_CHAN_POS_DXYZ:
	case SENSOR_CHAN_GRAVITY_VECTOR:
	case SENSOR_CHAN_GBIAS_XYZ:
		return 3U;
	case SENSOR_CHAN_GAME_ROTATION_VECTOR:
		return 4U;
	default:
		return 1U;
	}
}

/* -------------------------------------------------------------------------- */
/* ZBus Channels                                                              */
/* -------------------------------------------------------------------------- */
static bool telemetry_data_event_validator(const void *msg, size_t msg_size);

ZBUS_CHAN_DEFINE(mbs_telemetry_data_chan, struct mbs_telemetry_data_event,
		 telemetry_data_event_validator,
		 NULL, /* user_data */
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

/* -------------------------------------------------------------------------- */
/* Devices                                                                    */
/* -------------------------------------------------------------------------- */
#if DT_HAS_CHOSEN(meshbus_telemetry)
#define TELEMETRY_NODE        DT_CHOSEN(meshbus_telemetry)
#define TELEMETRY_HAS_CHANNEL 1
#else
#define TELEMETRY_HAS_CHANNEL 0
#warning "meshbus,telemetry not defined in chosen node"
#endif

/* Telemetry channel entry (sensor + channel binding from DTS) */
struct mbs_telemetry_channel_entry {
	const struct device *sensor;       /**< Sensor device */
	const struct device *power_domain; /**< Power-domain device (optional) */
	enum sensor_channel chan;          /**< Channel type to read */
};

#if TELEMETRY_HAS_CHANNEL

/* Devicetree binding: child nodes which each describe a (sensor, channel) pair. */
#define TELEMETRY_CHILD_SENSOR_NODE(_child) DT_PHANDLE(_child, sensor)
#define TELEMETRY_CHILD_SENSOR_DEV(_child)  DEVICE_DT_GET(TELEMETRY_CHILD_SENSOR_NODE(_child))
#define TELEMETRY_CHILD_SENSOR_PD_DEV(_child)                                                      \
	COND_CODE_1(DT_NODE_HAS_PROP(TELEMETRY_CHILD_SENSOR_NODE(_child), power_domains),           \
		    (DEVICE_DT_GET_OR_NULL(DT_PHANDLE_BY_IDX(TELEMETRY_CHILD_SENSOR_NODE(_child),   \
							    power_domains, 0))),              \
		    (NULL))

#define TELEMETRY_CHILD_ENTRY(_child)                                                              \
	{                                                                                          \
		.sensor = TELEMETRY_CHILD_SENSOR_DEV(_child),                                      \
		.power_domain = TELEMETRY_CHILD_SENSOR_PD_DEV(_child),                             \
		.chan = (enum sensor_channel)DT_PROP(_child, channel),                             \
	},

#define TELEMETRY_CHILD_ASSERT_COMMON_CHANNEL(_child)                                      \
	BUILD_ASSERT(MBS_TELEMETRY_DT_CHANNEL_IS_COMMON(_child),                              \
		     "Telemetry only supports common Zephyr sensor channels");
DT_FOREACH_CHILD_STATUS_OKAY(TELEMETRY_NODE, TELEMETRY_CHILD_ASSERT_COMMON_CHANNEL)

/*
 * The electronic Compass owns both its public provider and, for the composite
 * driver, every physical source behind it. Reject overlap before the early
 * Telemetry power-domain hold can touch one of those devices.
 */
#if DT_HAS_CHOSEN(meshbus_compass)
#define TELEMETRY_COMPASS_NODE DT_CHOSEN(meshbus_compass)
#define TELEMETRY_CHILD_ASSERT_NOT_COMPASS(_child)                                          \
	BUILD_ASSERT(!MBS_TELEMETRY_DT_SENSOR_IS_COMPASS_OWNED(                               \
			     TELEMETRY_CHILD_SENSOR_NODE(_child), TELEMETRY_COMPASS_NODE),    \
		     "Telemetry cannot bind a Compass provider or its physical sources");
DT_FOREACH_CHILD_STATUS_OKAY(TELEMETRY_NODE, TELEMETRY_CHILD_ASSERT_NOT_COMPASS)
#endif /* DT_HAS_CHOSEN(meshbus_compass) */

static const struct mbs_telemetry_channel_entry telemetry_channels[] = {
	DT_FOREACH_CHILD_STATUS_OKAY(TELEMETRY_NODE, TELEMETRY_CHILD_ENTRY)};

#define TELEMETRY_CHANNEL_COUNT ARRAY_SIZE(telemetry_channels)

#else /* !TELEMETRY_HAS_CHANNEL */

static const struct mbs_telemetry_channel_entry telemetry_channels[] = {};
#define TELEMETRY_CHANNEL_COUNT 0

#endif /* TELEMETRY_HAS_CHANNEL */

BUILD_ASSERT(TELEMETRY_CHANNEL_COUNT <=
		     ARRAY_SIZE(((meshbus_TelemetryStatusResponse *)0)->channels),
	     "Telemetry devicetree channel count exceeds protobuf status capacity");

/* -------------------------------------------------------------------------- */
/* Defaults And State                                                         */
/* -------------------------------------------------------------------------- */
#define MBS_TELEMETRY_SETTINGS_SUBTREE "meshbus/telemetry"
#define MBS_TELEMETRY_SETTINGS_KEY_CONFIG "config"

#define MBS_TELEMETRY_CONFIG_DEFAULTS                                                          \
	{                                                                                          \
		.enabled = IS_ENABLED(CONFIG_MBS_TELEMETRY_DEFAULT_ENABLED),                  \
		.sample_interval = CONFIG_MBS_TELEMETRY_DEFAULT_SAMPLE_INTERVAL,              \
	}

static mbs_telemetry_config telemetry_cfg = MBS_TELEMETRY_CONFIG_DEFAULTS;
static struct k_work_delayable telemetry_sample_work;
static atomic_t telemetry_quiescing;

/* Runtime channel/provider mapping built from devicetree. */
#define TELEMETRY_LIST_MAX (TELEMETRY_CHANNEL_COUNT > 0 ? TELEMETRY_CHANNEL_COUNT : 1)

enum telemetry_warn_flags {
	TELEMETRY_WARN_NOT_READY_BIT = 0,
	TELEMETRY_WARN_FETCH_FAIL_BIT = 1,
	TELEMETRY_WARN_CHAN_FAIL_BIT = 2,
};

struct telemetry_provider {
	const struct device *dev;
	const struct device *power_domain;
	bool runtime_held;
	int runtime_error;
	atomic_t warn_flags;
};

struct telemetry_binding {
	enum sensor_channel chan;
	uint16_t provider_idx;
	atomic_t warn_flags;
};

static struct telemetry_provider telemetry_providers[TELEMETRY_LIST_MAX];
static size_t telemetry_provider_count;
static struct telemetry_binding telemetry_bindings[TELEMETRY_LIST_MAX];
static size_t telemetry_binding_count;

/* Serialize periodic sampling and synchronous channel reads owned by Telemetry. */
static K_MUTEX_DEFINE(telemetry_sensor_io_mutex);

/* Lifetime power-domain holds keep configured sensor rails powered until shutdown. */
#if defined(CONFIG_PM_DEVICE) && defined(CONFIG_PM_DEVICE_RUNTIME)
static K_MUTEX_DEFINE(telemetry_pm_mutex);

/* Power-domain list derived from sensor nodes' `power-domains` property. */
#define TELEMETRY_PD_LIST_MAX (TELEMETRY_CHANNEL_COUNT > 0 ? TELEMETRY_CHANNEL_COUNT : 1)
static const struct device *telemetry_pds[TELEMETRY_PD_LIST_MAX];
static size_t telemetry_pd_count;
static bool telemetry_pd_list_built;

static bool telemetry_pd_held[TELEMETRY_PD_LIST_MAX];

#endif

/* settings_mutex protects telemetry_cfg and settings_initial_apply. */
static K_MUTEX_DEFINE(settings_mutex);
static K_MUTEX_DEFINE(settings_apply_mutex);
/* Serializes public set/reset transactions across deferred Settings I/O. */
static K_MUTEX_DEFINE(settings_transaction_mutex);
static bool settings_initial_apply;
static bool settings_persistence_dirty;
static struct k_work_delayable settings_persistence_work;
static mbs_telemetry_config settings_load_cfg = MBS_TELEMETRY_CONFIG_DEFAULTS;
static struct mbs_settings_blob_load_state settings_load_state;

MBS_SETTINGS_BLOB_SCHEMA_DEFINE(telemetry_settings_schema, MBS_TELEMETRY_SETTINGS_SUBTREE,
			       MBS_TELEMETRY_SETTINGS_KEY_CONFIG,
			       meshbus_TelemetryConfig, mbs_telemetry_config);

/* -------------------------------------------------------------------------- */
/* Declarations                                                               */
/* -------------------------------------------------------------------------- */
static int settings_handler_apply(const mbs_telemetry_config *cfg, bool persistence,
				  bool force);
static void telemetry_sample_work_handler(struct k_work *work);

static void telemetry_runtime_map_build(void);

/* -------------------------------------------------------------------------- */
/* Validation And Helpers                                                     */
/* -------------------------------------------------------------------------- */
static int telemetry_sensor_runtime_get(const struct device *sensor, bool *held)
{
	*held = false;
	if (!device_is_ready(sensor)) {
		return -ENODEV;
	}

#if defined(CONFIG_PM_DEVICE) && defined(CONFIG_PM_DEVICE_RUNTIME)
	int rc;

	if (!pm_device_runtime_is_enabled(sensor)) {
		rc = pm_device_runtime_enable(sensor);
		if (rc != 0 && rc != -ENOTSUP && rc != -ENOSYS && rc != -EBUSY) {
			return rc;
		}
	}

	rc = pm_device_runtime_get(sensor);
	if (rc == -ENOTSUP || rc == -ENOSYS) {
		return 0;
	}
	if (rc != 0) {
		return rc;
	}

	*held = true;
#endif

	return 0;
}

static int telemetry_sensor_runtime_put(const struct device *sensor, bool held)
{
	if (!held) {
		return 0;
	}

#if defined(CONFIG_PM_DEVICE) && defined(CONFIG_PM_DEVICE_RUNTIME)
	int rc = pm_device_runtime_put(sensor);

	return (rc == -ENOTSUP || rc == -ENOSYS || rc == -EALREADY) ? 0 : rc;
#else
	ARG_UNUSED(sensor);
	return 0;
#endif
}

static int telemetry_provider_leases_acquire(void)
{
	int first_error = 0;

	for (size_t i = 0; i < telemetry_provider_count; i++) {
		struct telemetry_provider *provider = &telemetry_providers[i];
		int rc;

		provider->runtime_held = false;
		provider->runtime_error = 0;
		if (provider->dev == NULL) {
			continue;
		}

		rc = telemetry_sensor_runtime_get(provider->dev, &provider->runtime_held);
		if (rc != 0) {
			provider->runtime_error = rc;
			LOG_WRN("Telemetry provider %s could not be held active: %d",
				provider->dev->name, rc);
			if (first_error == 0) {
				first_error = rc;
			}
		}
	}

	return first_error;
}

static int telemetry_provider_leases_release(void)
{
	int first_error = 0;

	k_mutex_lock(&telemetry_sensor_io_mutex, K_FOREVER);
	for (size_t i = telemetry_provider_count; i > 0U; i--) {
		size_t index = i - 1U;
		struct telemetry_provider *provider = &telemetry_providers[index];
		int rc;

		if (provider->dev == NULL || !provider->runtime_held) {
			continue;
		}

		rc = telemetry_sensor_runtime_put(provider->dev, true);

		if (first_error == 0 && rc != 0) {
			first_error = rc;
		}
		if (rc == 0) {
			provider->runtime_held = false;
		}
	}
	k_mutex_unlock(&telemetry_sensor_io_mutex);

	return first_error;
}

#if defined(CONFIG_PM_DEVICE) && defined(CONFIG_PM_DEVICE_RUNTIME)
static void telemetry_pd_list_build_once(void)
{
	if (telemetry_pd_list_built) {
		return;
	}

	telemetry_pd_count = 0U;
	for (size_t i = 0; i < TELEMETRY_CHANNEL_COUNT; i++) {
		const struct device *pd = telemetry_channels[i].power_domain;

		if (pd == NULL) {
			continue;
		}

		bool dup = false;
		for (size_t j = 0; j < telemetry_pd_count; j++) {
			if (telemetry_pds[j] == pd) {
				dup = true;
				break;
			}
		}
		if (dup) {
			continue;
		}

		if (telemetry_pd_count < ARRAY_SIZE(telemetry_pds)) {
			telemetry_pds[telemetry_pd_count++] = pd;
		}
	}

	telemetry_pd_list_built = true;
}

static int telemetry_pd_claim_acquire(void)
{
	int first_error = 0;
	size_t claimed = 0U;

	k_mutex_lock(&telemetry_pm_mutex, K_FOREVER);

	telemetry_pd_list_build_once();

	for (size_t i = 0; i < telemetry_pd_count; i++) {
		const struct device *pd = telemetry_pds[i];

		if (telemetry_pd_held[i]) {
			continue;
		}

		if (!device_is_ready(pd)) {
			LOG_WRN("Telemetry power-domain %s not ready", pd->name);
			if (first_error == 0) {
				first_error = -ENODEV;
			}
			continue;
		}

		/* Enable counted runtime-PM references before taking the lifetime hold. */
		if (!pm_device_runtime_is_enabled(pd)) {
			int en_rc = pm_device_runtime_enable(pd);

			if (en_rc != 0 && en_rc != -ENOTSUP && en_rc != -ENOSYS &&
			    en_rc != -EBUSY) {
				LOG_WRN("Telemetry power-domain %s runtime enable failed: %d",
					pd->name, en_rc);
				if (first_error == 0) {
					first_error = en_rc;
				}
				continue;
			}
		}

		int get_rc = pm_device_runtime_get(pd);
		if (get_rc == -ENOTSUP || get_rc == -ENOSYS) {
			get_rc = 0;
		}
		if (get_rc != 0) {
			LOG_WRN("Telemetry power-domain %s runtime get failed: %d", pd->name,
				get_rc);
			if (first_error == 0) {
				first_error = get_rc;
			}
			continue;
		}

		/* Unsupported runtime PM is a stable always-on domain and needs no count. */
		telemetry_pd_held[i] = true;
		claimed++;
	}

	k_mutex_unlock(&telemetry_pm_mutex);

	if (claimed > 0U) {
		LOG_DBG("Telemetry power-domains held: %u", (unsigned int)claimed);
	}
	return first_error;
}

static int telemetry_pd_claim_release(void)
{
	int first_error = 0;
	size_t released = 0U;

	k_mutex_lock(&telemetry_pm_mutex, K_FOREVER);

	telemetry_pd_list_build_once();

	for (size_t i = telemetry_pd_count; i > 0U; i--) {
		size_t index = i - 1U;
		const struct device *pd = telemetry_pds[index];

		if (!telemetry_pd_held[index]) {
			continue;
		}

		int put_rc = pm_device_runtime_put(pd);
		if (put_rc == -ENOTSUP || put_rc == -ENOSYS || put_rc == -EALREADY) {
			put_rc = 0;
		}
		if (put_rc != 0) {
			LOG_WRN("Telemetry power-domain %s runtime put failed: %d", pd->name,
				put_rc);
			if (first_error == 0) {
				first_error = put_rc;
			}
			continue;
		}

		telemetry_pd_held[index] = false;
		released++;
	}

	k_mutex_unlock(&telemetry_pm_mutex);

	if (released > 0U) {
		LOG_DBG("Telemetry power-domains released: %u", (unsigned int)released);
	}
	return first_error;
}

#endif /* CONFIG_PM_DEVICE && CONFIG_PM_DEVICE_RUNTIME */

static size_t telemetry_provider_find(const struct device *dev)
{
	for (size_t i = 0; i < telemetry_provider_count; i++) {
		if (telemetry_providers[i].dev == dev) {
			return i;
		}
	}

	return SIZE_MAX;
}

static const struct telemetry_binding *telemetry_binding_find(enum sensor_channel chan)
{
	for (size_t i = 0; i < telemetry_binding_count; i++) {
		if (telemetry_bindings[i].chan == chan) {
			return &telemetry_bindings[i];
		}
	}

	return NULL;
}

static bool telemetry_data_event_validator(const void *msg, size_t msg_size)
{
	const struct mbs_telemetry_data_event *event = msg;
	size_t value_count;

	if (event == NULL || msg_size != sizeof(*event)) {
		return false;
	}
	if (event->chan < 0 || event->chan >= SENSOR_CHAN_ALL) {
		return false;
	}
	if (telemetry_binding_find(event->chan) == NULL) {
		return false;
	}

	value_count = mbs_telemetry_channel_value_count(event->chan);
	return value_count <= MBS_TELEMETRY_MAX_VALUES && event->value_count == value_count;
}

static size_t telemetry_binding_find_idx(enum sensor_channel chan)
{
	for (size_t i = 0; i < telemetry_binding_count; i++) {
		if (telemetry_bindings[i].chan == chan) {
			return i;
		}
	}

	return SIZE_MAX;
}

static void telemetry_runtime_map_build(void)
{
	telemetry_provider_count = 0U;
	telemetry_binding_count = 0U;

	memset(telemetry_providers, 0, sizeof(telemetry_providers));
	memset(telemetry_bindings, 0, sizeof(telemetry_bindings));
	LOG_DBG("Configured %zu telemetry DT entries:", TELEMETRY_CHANNEL_COUNT);
	for (size_t i = 0; i < TELEMETRY_CHANNEL_COUNT; i++) {
		const struct mbs_telemetry_channel_entry *dt_entry = &telemetry_channels[i];
		const struct device *dev = dt_entry->sensor;
		enum sensor_channel chan = dt_entry->chan;

		if (telemetry_binding_find_idx(chan) != SIZE_MAX) {
			LOG_WRN("Duplicate telemetry channel[%u] in DTS entries; keeping first",
				(unsigned int)chan);
			continue;
		}

		size_t p = telemetry_provider_find(dev);
		if (p == SIZE_MAX) {
			if (telemetry_provider_count >= ARRAY_SIZE(telemetry_providers)) {
				LOG_WRN("Telemetry provider list full; dropping channel[%u] (%s)",
					(unsigned int)chan, dev->name);
				continue;
			}

			p = telemetry_provider_count++;
			telemetry_providers[p] = (struct telemetry_provider){
				.dev = dev,
				.power_domain = dt_entry->power_domain,
				.runtime_held = false,
				.runtime_error = 0,
				.warn_flags = 0U,
			};
		} else if (telemetry_providers[p].power_domain != dt_entry->power_domain) {
			LOG_WRN("Telemetry provider %s has inconsistent power domains", dev->name);
			continue;
		}

		if (telemetry_binding_count >= ARRAY_SIZE(telemetry_bindings)) {
			LOG_WRN("Telemetry binding list full; dropping channel[%u] (%s)",
				(unsigned int)chan, dev->name);
			continue;
		}

		telemetry_bindings[telemetry_binding_count++] = (struct telemetry_binding){
			.chan = chan,
			.provider_idx = (uint16_t)p,
			.warn_flags = 0U,
		};

		LOG_DBG("  [%zu] channel[%u] - %s (%s)", i, (unsigned int)chan, dev->name,
			device_is_ready(dev) ? "ready" : "not ready");
		if (dt_entry->power_domain != NULL) {
			LOG_DBG("       power-domain: %s", dt_entry->power_domain->name);
		}
	}

	LOG_DBG("Telemetry providers: %zu, bindings: %zu", telemetry_provider_count,
		telemetry_binding_count);
}

static int telemetry_config_validate(const mbs_telemetry_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	if (cfg->sample_interval < CONFIG_MBS_TELEMETRY_MIN_SAMPLE_INTERVAL) {
		LOG_ERR("Invalid sample interval: %u ms (min: %u ms)", cfg->sample_interval,
			CONFIG_MBS_TELEMETRY_MIN_SAMPLE_INTERVAL);
		return -EINVAL;
	}

	return 0;
}

static void telemetry_config_commit(const mbs_telemetry_config *cfg)
{
	k_mutex_lock(&settings_mutex, K_FOREVER);
	telemetry_cfg = *cfg;
	settings_initial_apply = true;
	k_mutex_unlock(&settings_mutex);
}

/* -------------------------------------------------------------------------- */
/* Settings Schema And Apply                                                  */
/* -------------------------------------------------------------------------- */

static int settings_handler_apply(const mbs_telemetry_config *cfg, bool persistence, bool force)
{
	mbs_telemetry_config old_cfg;
	bool next_enabled;
	int rc;

	rc = telemetry_config_validate(cfg);
	if (rc != 0) {
		return rc;
	}
	if (atomic_get(&telemetry_quiescing) != 0) {
		return -ESHUTDOWN;
	}

	k_mutex_lock(&settings_apply_mutex, K_FOREVER);
	if (atomic_get(&telemetry_quiescing) != 0) {
		k_mutex_unlock(&settings_apply_mutex);
		return -ESHUTDOWN;
	}
	k_mutex_lock(&settings_mutex, K_FOREVER);

	old_cfg = telemetry_cfg;
	next_enabled = cfg->enabled;

	/* Skip reconfiguration when nothing changed unless force is requested. */
	if (!force && telemetry_cfg.enabled == cfg->enabled &&
	    telemetry_cfg.sample_interval == cfg->sample_interval) {
		settings_initial_apply = true;
		k_mutex_unlock(&settings_mutex);
		k_mutex_unlock(&settings_apply_mutex);
		LOG_DBG("Settings unchanged, nothing to apply");
		return 0;
	}

	k_mutex_unlock(&settings_mutex);

	/* Reconcile sampling before committing new config.
	 *
	 * Do not call k_work_cancel_delayable_sync() while holding settings_mutex:
	 * the work handler also takes settings_mutex and could deadlock.
	 *
	 * When enabling, commit before scheduling sample work so the worker never
	 * observes the old config. Sensor power-domain holds span the service
	 * lifetime and are independent of the sampling enabled state.
	 */
	if (!next_enabled) {
		/* Stop sampling without releasing the service-lifetime domain holds. */
		struct k_work_sync sync;
		(void)k_work_cancel_delayable_sync(&telemetry_sample_work, &sync);
	}

	telemetry_config_commit(cfg);

	if (next_enabled) {
		rc = k_work_reschedule(&telemetry_sample_work, K_MSEC(cfg->sample_interval));
		if (rc < 0) {
			LOG_WRN("Failed to schedule telemetry sample work: %d", rc);
			telemetry_config_commit(&old_cfg);
			k_mutex_unlock(&settings_apply_mutex);
			return rc;
		}
		/*
		 * Close the race where shutdown cancels the old deadline immediately
		 * before this path installs a new one. A worker which starts in this
		 * window observes telemetry_quiescing before performing sensor I/O.
		 */
		if (atomic_get(&telemetry_quiescing) != 0) {
			(void)k_work_cancel_delayable(&telemetry_sample_work);
			telemetry_config_commit(&old_cfg);
			k_mutex_unlock(&settings_apply_mutex);
			return -ESHUTDOWN;
		}
	}

	LOG_INF("Settings apply: enabled=%d sample_interval=%u ms", cfg->enabled,
		cfg->sample_interval);

	if (persistence) {
		k_mutex_lock(&settings_mutex, K_FOREVER);
		settings_persistence_dirty = true;
		k_mutex_unlock(&settings_mutex);
		(void)k_work_reschedule(&settings_persistence_work,
					K_MSEC(CONFIG_MBS_SETTINGS_PERSISTENCE_DELAY));
	}

	k_mutex_unlock(&settings_apply_mutex);
	return 0;
}

/* settings_apply_mutex must be held so a saved snapshot and its dirty flag
 * cannot be reordered with a concurrent config apply.
 */
static int telemetry_settings_persist_dirty_locked(void)
{
	uint8_t buffer[MBS_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_TelemetryConfig_size)];
	bool dirty;
	int rc;

	k_mutex_lock(&settings_mutex, K_FOREVER);
	dirty = settings_persistence_dirty;
	k_mutex_unlock(&settings_mutex);
	if (!dirty) {
		return 0;
	}

	rc = mbs_settings_blob_save_locked_with_buffer(&telemetry_settings_schema,
						 &settings_mutex, &telemetry_cfg,
						 buffer, sizeof(buffer));
	if (rc == 0) {
		k_mutex_lock(&settings_mutex, K_FOREVER);
		settings_persistence_dirty = false;
		k_mutex_unlock(&settings_mutex);
	}
	return rc;
}

static void settings_persistence_work_handler(struct k_work *work)
{
	int rc;

	ARG_UNUSED(work);
	k_mutex_lock(&settings_apply_mutex, K_FOREVER);
	rc = telemetry_settings_persist_dirty_locked();
	k_mutex_unlock(&settings_apply_mutex);
	if (rc != 0) {
		LOG_ERR("Settings persistence failed: %d", rc);
	} else {
		LOG_DBG("Settings persistence complete");
	}
}

static int settings_handle_set(const char *name, size_t len, settings_read_cb read_cb,
			       void *cb_arg)
{
	uint8_t buffer[MBS_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_TelemetryConfig_size)];

	return mbs_settings_blob_handle_set_with_buffer(
		&telemetry_settings_schema, &settings_mutex, &settings_load_state,
		&settings_load_cfg, name, len, read_cb, cb_arg, buffer, sizeof(buffer));
}

static int settings_handle_commit(void)
{
	mbs_telemetry_config cfg;
	bool force;
	int rc;

	if (!mbs_settings_blob_commit_prepare(&telemetry_settings_schema, &settings_mutex,
					     &settings_load_state, &settings_load_cfg, &cfg,
					     &settings_initial_apply, &force)) {
		return 0;
	}

	rc = settings_handler_apply(&cfg, false, force);
	if (rc != 0) {
		LOG_WRN("Ignoring invalid persisted telemetry config: %d", rc);
	}
	return 0;
}

static int settings_handle_export(int (*export_func)(const char *name, const void *val,
						       size_t len))
{
	uint8_t buffer[MBS_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_TelemetryConfig_size)];

	return mbs_settings_blob_export_locked_with_buffer(
		&telemetry_settings_schema, &settings_mutex, &telemetry_cfg, buffer,
		sizeof(buffer), export_func);
}

SETTINGS_STATIC_HANDLER_DEFINE(mbs_telemetry, MBS_TELEMETRY_SETTINGS_SUBTREE, NULL,
			       settings_handle_set, settings_handle_commit,
			       settings_handle_export);

/* -------------------------------------------------------------------------- */
/* Callbacks And Work                                                         */
/* -------------------------------------------------------------------------- */
static void telemetry_sample_work_handler(struct k_work *work)
{
	bool enabled;
	uint32_t timestamp;

	ARG_UNUSED(work);
	if (atomic_get(&telemetry_quiescing) != 0) {
		return;
	}

	k_mutex_lock(&settings_mutex, K_FOREVER);
	enabled = telemetry_cfg.enabled;
	k_mutex_unlock(&settings_mutex);

	if (!enabled || atomic_get(&telemetry_quiescing) != 0) {
		return;
	}

	timestamp = k_uptime_get_32();
	k_mutex_lock(&telemetry_sensor_io_mutex, K_FOREVER);
	if (atomic_get(&telemetry_quiescing) != 0) {
		k_mutex_unlock(&telemetry_sensor_io_mutex);
		return;
	}

	for (size_t p = 0; p < telemetry_provider_count; p++) {
		struct telemetry_provider *provider = &telemetry_providers[p];
		const struct device *dev = provider->dev;
		bool fetch_per_channel = false;
		int rc;

		if (dev == NULL) {
			continue;
		}

		if (!device_is_ready(dev) || provider->runtime_error != 0) {
			if (!atomic_test_and_set_bit(&provider->warn_flags,
						     TELEMETRY_WARN_NOT_READY_BIT)) {
				LOG_WRN("Telemetry provider %s unavailable: %d", dev->name,
					provider->runtime_error != 0 ? provider->runtime_error : -ENODEV);
			}
			continue;
		}

		rc = sensor_sample_fetch(dev);
		if (rc == -ENOTSUP) {
			/* Some drivers intentionally expose only channel-scoped fetch. */
			fetch_per_channel = true;
		} else if (rc != 0) {
			if (!atomic_test_and_set_bit(&provider->warn_flags,
						     TELEMETRY_WARN_FETCH_FAIL_BIT)) {
				LOG_WRN("Telemetry fetch failed for %s: %d", dev->name, rc);
			}
			continue;
		}

		for (size_t i = 0; i < telemetry_binding_count; i++) {
			struct telemetry_binding *binding = &telemetry_bindings[i];

			if (binding->provider_idx != p) {
				continue;
			}

			struct sensor_value vals[MBS_TELEMETRY_MAX_VALUES] = {0};
			struct mbs_telemetry_data_event event = {
				.timestamp = timestamp,
				.chan = binding->chan,
			};
			size_t value_count;

			if (fetch_per_channel) {
				rc = sensor_sample_fetch_chan(dev, binding->chan);
				if (rc != 0) {
					if (!atomic_test_and_set_bit(
						    &binding->warn_flags,
						    TELEMETRY_WARN_FETCH_FAIL_BIT)) {
						LOG_WRN("Channel[%u] fetch failed for %s: %d",
							(unsigned int)binding->chan, dev->name, rc);
					}
					continue;
				}
			}

			value_count = mbs_telemetry_channel_value_count(binding->chan);
			value_count = MIN(value_count, MBS_TELEMETRY_MAX_VALUES);

			rc = sensor_channel_get(dev, binding->chan, vals);
			if (rc != 0) {
				if (!atomic_test_and_set_bit(&binding->warn_flags,
							     TELEMETRY_WARN_CHAN_FAIL_BIT)) {
					LOG_WRN("Channel[%u] not supported by %s",
						(unsigned int)binding->chan, dev->name);
				}
				continue;
			}

			event.value_count = (uint8_t)value_count;
			if (value_count > 0U) {
				memcpy(event.values, vals,
				       sizeof(struct sensor_value) * value_count);
			}

			rc = zbus_chan_pub(&mbs_telemetry_data_chan, &event, K_NO_WAIT);
			if (rc != 0) {
				LOG_ERR("Failed to publish telemetry channel[%u]: %d",
					(unsigned int)binding->chan, rc);
				continue;
			}

#if (CONFIG_MBS_TELEMETRY_LOG_LEVEL >= LOG_LEVEL_DBG)
			char tuple[128];

			telemetry_sensor_values_snprint(tuple, sizeof(tuple), vals, value_count);
			LOG_DBG("Published telemetry channel[%u] from %s %s",
				(unsigned int)binding->chan, dev->name, tuple);
#endif
		}
	}

	k_mutex_unlock(&telemetry_sensor_io_mutex);
	if (atomic_get(&telemetry_quiescing) != 0) {
		return;
	}

	/* Reschedule from current settings to honor runtime config changes. */
	k_mutex_lock(&settings_mutex, K_FOREVER);
	if (telemetry_cfg.enabled && atomic_get(&telemetry_quiescing) == 0) {
		(void)k_work_reschedule(&telemetry_sample_work,
					K_MSEC(telemetry_cfg.sample_interval));
	}
	k_mutex_unlock(&settings_mutex);
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */
int mbs_telemetry_config_get(mbs_telemetry_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&settings_mutex, K_FOREVER);
	*cfg = telemetry_cfg;
	k_mutex_unlock(&settings_mutex);

	return 0;
}

int mbs_telemetry_config_set(const mbs_telemetry_config *cfg)
{
	int rc;

	if (cfg == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&settings_transaction_mutex, K_FOREVER);
	rc = settings_handler_apply(cfg, true, false);
	k_mutex_unlock(&settings_transaction_mutex);
	return rc;
}

int mbs_telemetry_config_reset(void)
{
	struct k_work_sync sync;
	mbs_telemetry_config cfg = MBS_TELEMETRY_CONFIG_DEFAULTS;
	int rc;

	k_mutex_lock(&settings_transaction_mutex, K_FOREVER);
	(void)k_work_cancel_delayable_sync(&settings_persistence_work, &sync);

	rc = settings_handler_apply(&cfg, false, true);
	if (rc != 0) {
		goto out_unlock;
	}

	rc = mbs_settings_blob_delete(&telemetry_settings_schema);
	if (rc != 0) {
		LOG_ERR("Failed to delete persisted telemetry settings: %d", rc);
		goto out_unlock;
	}
	k_mutex_lock(&settings_mutex, K_FOREVER);
	settings_persistence_dirty = false;
	k_mutex_unlock(&settings_mutex);

	LOG_INF("Telemetry settings reset to defaults");

out_unlock:
	k_mutex_unlock(&settings_transaction_mutex);
	return rc;
}

int mbs_telemetry_sample_trigger(void)
{
	int rc;

	if (atomic_get(&telemetry_quiescing) != 0) {
		return -ESHUTDOWN;
	}

	k_mutex_lock(&settings_mutex, K_FOREVER);
	if (!telemetry_cfg.enabled) {
		k_mutex_unlock(&settings_mutex);
		LOG_DBG("Sample trigger ignored: telemetry disabled");
		return -ENODEV;
	}
	k_mutex_unlock(&settings_mutex);

	rc = k_work_reschedule(&telemetry_sample_work, K_NO_WAIT);
	if (rc < 0) {
		return rc;
	}
	if (atomic_get(&telemetry_quiescing) != 0) {
		(void)k_work_cancel_delayable(&telemetry_sample_work);
		return -ESHUTDOWN;
	}
	LOG_DBG("Immediate telemetry sample triggered");
	return 0;
}

size_t mbs_telemetry_bindings_count(void)
{
	return telemetry_binding_count;
}

int mbs_telemetry_binding_get(size_t index, struct mbs_telemetry_binding *out)
{
	if (out == NULL) {
		return -EINVAL;
	}
	if (index >= telemetry_binding_count) {
		return -ENOENT;
	}

	const struct telemetry_binding *binding = &telemetry_bindings[index];

	if (binding->provider_idx >= telemetry_provider_count) {
		return -ENOENT;
	}

	out->chan = binding->chan;
	out->sensor_name = telemetry_providers[binding->provider_idx].dev != NULL
				   ? telemetry_providers[binding->provider_idx].dev->name
				   : NULL;

	return 0;
}

int mbs_telemetry_channel_get(enum sensor_channel chan, struct sensor_value *val)
{
	bool enabled;
	const struct telemetry_binding *binding;
	const struct telemetry_provider *provider;
	const struct device *dev;
	int rc;

	if (val == NULL) {
		return -EINVAL;
	}
	if (chan < 0 || chan >= SENSOR_CHAN_ALL) {
		return -EINVAL;
	}
	if (atomic_get(&telemetry_quiescing) != 0) {
		return -ESHUTDOWN;
	}

	k_mutex_lock(&settings_mutex, K_FOREVER);
	enabled = telemetry_cfg.enabled;
	k_mutex_unlock(&settings_mutex);

	if (!enabled) {
		return -ENODEV;
	}

	binding = telemetry_binding_find(chan);
	if (binding == NULL) {
		return -ENOENT;
	}

	if (binding->provider_idx >= telemetry_provider_count) {
		return -ENOENT;
	}

	provider = &telemetry_providers[binding->provider_idx];
	dev = provider->dev;
	if (!device_is_ready(dev) || provider->runtime_error != 0) {
		LOG_WRN("Sensor %s not ready for channel[%u]", dev->name, (unsigned int)chan);
		return -EIO;
	}

	k_mutex_lock(&telemetry_sensor_io_mutex, K_FOREVER);
	if (atomic_get(&telemetry_quiescing) != 0) {
		rc = -ESHUTDOWN;
		goto out_unlock;
	}

	rc = sensor_sample_fetch(dev);
	if (rc == -ENOTSUP) {
		rc = sensor_sample_fetch_chan(dev, chan);
	}
	if (rc != 0) {
		LOG_WRN("Failed to fetch from %s: %d", dev->name, rc);
		goto out_unlock;
	}

	rc = sensor_channel_get(dev, chan, val);
	if (rc != 0) {
		LOG_WRN("Channel[%u] not supported by %s", (unsigned int)chan, dev->name);
	}

out_unlock:
	k_mutex_unlock(&telemetry_sensor_io_mutex);
	return rc;
}

/* -------------------------------------------------------------------------- */
/* Power Callback                                                             */
/* -------------------------------------------------------------------------- */
static int telemetry_quiesce_and_release(bool persist_config)
{
	int first_error = 0;
	int persist_rc;
	int provider_rc;
#if defined(CONFIG_PM_DEVICE) && defined(CONFIG_PM_DEVICE_RUNTIME)
	int rc;
#endif

	/* Power callbacks may run on the system workqueue, so cancellations stay
	 * asynchronous. Mutexes seal control paths and drain any active sensor I/O.
	 */
	atomic_set(&telemetry_quiescing, 1);
	(void)k_work_cancel_delayable(&telemetry_sample_work);

	/* Seal config apply first. An apply which passed the initial quiescing check
	 * must finish before its dirty snapshot is synchronously persisted here.
	 */
	k_mutex_lock(&settings_apply_mutex, K_FOREVER);
	(void)k_work_cancel_delayable(&settings_persistence_work);
	persist_rc = persist_config ? telemetry_settings_persist_dirty_locked() : 0;
	k_mutex_unlock(&settings_apply_mutex);
	if (persist_rc != 0) {
		LOG_ERR("Final telemetry settings persistence failed: %d", persist_rc);
		first_error = persist_rc;
	}
	provider_rc = telemetry_provider_leases_release();
	if (first_error == 0 && provider_rc != 0) {
		first_error = provider_rc;
	}

#if defined(CONFIG_PM_DEVICE) && defined(CONFIG_PM_DEVICE_RUNTIME)
	if (provider_rc == 0) {
		rc = telemetry_pd_claim_release();
		if (first_error == 0 && rc != 0) {
			first_error = rc;
		}
	} else {
		/* A failed provider put leaves its runtime lease retryable. Keep the
		 * underlying rails active until a later shutdown/reboot callback can
		 * complete provider cleanup first.
		 */
		LOG_WRN("Keeping telemetry power-domains active after provider release failure: %d",
			provider_rc);
	}
#endif
	return first_error;
}

static void mbs_power_telemetry_cb(enum mbs_power_action action, void *user_data)
{
	ARG_UNUSED(user_data);

	if (action != MBS_POWER_ACTION_SHUTDOWN && action != MBS_POWER_ACTION_REBOOT) {
		return;
	}

	(void)telemetry_quiesce_and_release(true);
	LOG_INF("Telemetry stopped");
}
MBS_POWER_ACTION_CALLBACK_DEFINE(mbs_power_telemetry_cb, NULL);

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

#if defined(CONFIG_PM_DEVICE) && defined(CONFIG_PM_DEVICE_RUNTIME)
/* Acquire one counted hold for every unique configured sensor power domain before
 * sensor drivers initialize. The holds remain until the Meshbus shutdown path.
 * POWER_DOMAIN_GPIO init priority defaults to 75, SENSOR init commonly 90.
 */
#define MBS_TELEMETRY_PD_HOLD_INIT_PRIORITY 80
static int mbs_telemetry_pd_hold_init(void)
{
#if TELEMETRY_HAS_CHANNEL
	return telemetry_pd_claim_acquire();
#else
	return 0;
#endif
}

SYS_INIT(mbs_telemetry_pd_hold_init, POST_KERNEL, MBS_TELEMETRY_PD_HOLD_INIT_PRIORITY);
#endif /* CONFIG_PM_DEVICE && CONFIG_PM_DEVICE_RUNTIME */

static int mbs_telemetry_init(void)
{
	int rc;
	bool enabled;
	uint32_t sample_interval;

	k_work_init_delayable(&telemetry_sample_work, telemetry_sample_work_handler);
	k_work_init_delayable(&settings_persistence_work, settings_persistence_work_handler);
	atomic_clear(&telemetry_quiescing);

	telemetry_runtime_map_build();
	rc = telemetry_provider_leases_acquire();
	if (rc != 0) {
		LOG_WRN("One or more static telemetry providers are unavailable: %d", rc);
	}

	if (TELEMETRY_CHANNEL_COUNT == 0) {
		LOG_WRN("No telemetry channels configured in device tree");
	}

	rc = settings_load_subtree(MBS_TELEMETRY_SETTINGS_SUBTREE);
	if (rc != 0) {
		LOG_ERR("Failed to load telemetry settings: %d", rc);
		(void)telemetry_quiesce_and_release(false);
		return rc;
	}

	if (!settings_initial_apply) {
		rc = settings_handler_apply(&telemetry_cfg, false, true);
		if (rc != 0) {
			LOG_ERR("Failed to apply initial telemetry settings: %d", rc);
			(void)telemetry_quiesce_and_release(false);
			return rc;
		}
	}

	k_mutex_lock(&settings_mutex, K_FOREVER);
	enabled = telemetry_cfg.enabled;
	sample_interval = telemetry_cfg.sample_interval;
	k_mutex_unlock(&settings_mutex);
	LOG_INF("Telemetry service ready: enabled=%d sample_interval=%u ms", enabled,
		sample_interval);

	return 0;
}

SYS_INIT(mbs_telemetry_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
