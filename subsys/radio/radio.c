/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/pm/device.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>
#include <radio/radio.h>
#include <power/power.h>
#include <stddef.h>

#include "mbs_settings_internal.h"

LOG_MODULE_REGISTER(mbs_radio, CONFIG_MBS_RADIO_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* ZBus Channels                                                              */
/* -------------------------------------------------------------------------- */

static bool radio_publish_validator(const void *msg, size_t msg_size);
static bool radio_cw_request_validator(const void *msg, size_t msg_size);
static bool radio_state_event_validator(const void *msg, size_t msg_size);
static void radio_publish_work_handler(const struct zbus_channel *chan, const void *message);
static void radio_cw_request_work_handler(const struct zbus_channel *chan,
					  const void *message);

ZBUS_CHAN_DEFINE(mbs_radio_receive_chan, struct mbs_radio_receive_event,
		 NULL, /* validator */
		 NULL, /* user_data */
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_radio_publish_chan, struct mbs_radio_publish_event,
		 radio_publish_validator, /* validator */
		 NULL,                    /* user_data */
		 ZBUS_OBSERVERS(radio_publish_listener), ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_radio_tx_done_chan, struct mbs_radio_tx_done_event,
		 NULL, /* validator */
		 NULL, /* user_data */
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_radio_cw_chan, struct mbs_radio_cw_request_event,
		 radio_cw_request_validator, /* validator */
		 NULL,                       /* user_data */
		 ZBUS_OBSERVERS(radio_cw_request_listener), ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_radio_cw_done_chan, struct mbs_radio_cw_done_event,
		 NULL, /* validator */
		 NULL, /* user_data */
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_radio_state_chan, struct mbs_radio_state_event,
		 radio_state_event_validator, /* validator */
		 NULL,                        /* user_data */
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_ASYNC_LISTENER_DEFINE(radio_publish_listener, radio_publish_work_handler);
ZBUS_ASYNC_LISTENER_DEFINE(radio_cw_request_listener, radio_cw_request_work_handler);

/* -------------------------------------------------------------------------- */
/* Statistics                                                                 */
/* -------------------------------------------------------------------------- */

#ifdef CONFIG_MBS_RADIO_STATS
STATS_SECT_DECL(mbs_radio_stats) mbs_radio_stats;
#endif

/* -------------------------------------------------------------------------- */
/* Devices                                                                    */
/* -------------------------------------------------------------------------- */

#if DT_HAS_CHOSEN(meshbus_radio)
static const struct device *const lora_dev = DEVICE_DT_GET(DT_CHOSEN(meshbus_radio));
#else
static const struct device *const lora_dev = NULL;
#endif

/* -------------------------------------------------------------------------- */
/* Defaults And State                                                         */
/* -------------------------------------------------------------------------- */

/* SNR thresholds per SF for successful reception (from Semtech datasheets) */
static const float snr_threshold_table[] = {
	-7.5f,  /* SF7 needs at least -7.5 dB SNR */
	-10.0f, /* SF8 needs at least -10 dB SNR */
	-12.5f, /* SF9 needs at least -12.5 dB SNR */
	-15.0f, /* SF10 needs at least -15 dB SNR */
	-17.5f, /* SF11 needs at least -17.5 dB SNR */
	-20.0f  /* SF12 needs at least -20 dB SNR */
};
/** Current radio operating state */
static atomic_t radio_state = ATOMIC_INIT(MBS_RADIO_STATE_IDLE);
static atomic_t radio_enabled = ATOMIC_INIT(IS_ENABLED(CONFIG_MBS_RADIO_DEFAULT_ENABLED));
static atomic_t radio_receive_only =
	ATOMIC_INIT(IS_ENABLED(CONFIG_MBS_RADIO_DEFAULT_RECEIVE_ONLY));
static atomic_t radio_spread_factor = ATOMIC_INIT(CONFIG_MBS_RADIO_DEFAULT_SPREAD_FACTOR);
static atomic_t radio_state_sequence;

enum radio_receive_mode {
	RADIO_RECEIVE_MODE_NONE = 0,
	RADIO_RECEIVE_MODE_ASYNC,
	RADIO_RECEIVE_MODE_DUTY_CYCLE,
};

static atomic_t radio_receive_mode = ATOMIC_INIT(RADIO_RECEIVE_MODE_NONE);

#define MBS_RADIO_CONFIG_DEFAULTS                                                              \
	{                                                                                          \
		.enabled = IS_ENABLED(CONFIG_MBS_RADIO_DEFAULT_ENABLED),                       \
		.frequency = CONFIG_MBS_RADIO_DEFAULT_FREQUENCY,                               \
		.bandwidth = CONFIG_MBS_RADIO_DEFAULT_BANDWIDTH,                               \
		.spread_factor = CONFIG_MBS_RADIO_DEFAULT_SPREAD_FACTOR,                       \
		.coding_rate = CONFIG_MBS_RADIO_DEFAULT_CODING_RATE,                           \
		.preamble_length = CONFIG_MBS_RADIO_DEFAULT_PREAMBLE_LENGTH,                   \
		.tx_power = CONFIG_MBS_RADIO_DEFAULT_TX_POWER,                                 \
		.receive_only = IS_ENABLED(CONFIG_MBS_RADIO_DEFAULT_RECEIVE_ONLY),             \
		.rx_boosted = IS_ENABLED(CONFIG_MBS_RADIO_DEFAULT_RX_BOOSTED),                 \
		.crc = IS_ENABLED(CONFIG_MBS_RADIO_DEFAULT_CRC),                               \
		.duty_cycle = IS_ENABLED(CONFIG_MBS_RADIO_DEFAULT_DUTY_CYCLE),                 \
		.duty_cycle_rx_time = CONFIG_MBS_RADIO_DEFAULT_DUTY_CYCLE_RX_TIME,             \
		.duty_cycle_sleep_time = CONFIG_MBS_RADIO_DEFAULT_DUTY_CYCLE_SLEEP_TIME,       \
	}

static mbs_radio_config radio_cfg = MBS_RADIO_CONFIG_DEFAULTS;

/* Guard shared runtime state accessed from workqueue callbacks and APIs. */
static K_MUTEX_DEFINE(radio_state_mutex);
/** Last received packet RSSI */
static int16_t last_rssi;
/** Last received packet SNR (scaled by 4) */
static int8_t last_snr;
/** Noise floor estimate in dBm */
static int16_t noise_floor;
/** True once noise floor has been calibrated */
static bool noise_floor_valid;
/** Most recent instantaneous RSSI sample */
static int16_t rssi_inst;
/** True once an instantaneous RSSI sample has been collected */
static bool rssi_inst_valid;
/** Uptime when the instantaneous RSSI cache was last refreshed */
static uint32_t rssi_inst_timestamp_ms;
/** Channel activity detection threshold above noise floor */
static int16_t cad_threshold;
/** Noise floor calibration state */
static uint16_t noise_floor_sample_count;
static int32_t noise_floor_sample_sum;
static bool noise_floor_calibrating;

K_THREAD_STACK_DEFINE(radio_operation_work_q_stack,
		      CONFIG_MBS_RADIO_OPERATION_WORK_QUEUE_STACK_SIZE);
static struct k_work_q radio_operation_work_q;

#define RADIO_TX_COMPLETION_POLL_MS 20
static uint8_t radio_tx_buffer[MBS_RADIO_MAX_PAYLOAD];
static struct k_poll_signal radio_tx_done = K_POLL_SIGNAL_INITIALIZER(radio_tx_done);
static struct k_work_delayable radio_tx_completion_work;

/* Noise floor calibration worker */
#define NOISE_FLOOR_SAMPLE_PERIOD_MS CONFIG_MBS_RADIO_NOISE_FLOOR_SAMPLE_PERIOD_MS
#define NOISE_FLOOR_SAMPLE_RETRY_MS  CONFIG_MBS_RADIO_NOISE_FLOOR_SAMPLE_RETRY_MS
#define RADIO_RSSI_CACHE_MAX_AGE_MS  1000U
static struct k_work_delayable radio_noise_floor_work;

#define RADIO_CW_DRIVER_COMPLETION_MARGIN_MS 100U
#define RADIO_CW_RESTORE_RETRY_MS            50U
#define RADIO_CW_RESTORE_TIMEOUT_MS          5000U

struct radio_cw_request {
	mbs_radio_config restore_cfg;
	uint32_t frequency_hz;
	uint32_t restore_deadline_ms;
	uint16_t duration_s;
	int8_t tx_power_dbm;
	int operation_status;
};

static void radio_rssi_cache_update_locked(int16_t rssi)
{
	rssi_inst = rssi;
	rssi_inst_valid = true;
	rssi_inst_timestamp_ms = k_uptime_get_32();
}

static struct k_work_delayable radio_cw_work;
static struct radio_cw_request radio_cw_request;
static atomic_t radio_cw_active;
static atomic_t radio_cw_sequence;

/*
 * apply_mutex serializes driver control paths. settings_mutex protects radio_cfg and settings
 * staging only. Lock order: radio_apply_mutex -> settings_mutex -> radio_state_mutex.
 */
static K_MUTEX_DEFINE(radio_apply_mutex);
static K_MUTEX_DEFINE(settings_mutex);
static bool settings_initial_apply = false;
static struct k_work_delayable settings_persistence_work;
static mbs_radio_config settings_load_cfg = MBS_RADIO_CONFIG_DEFAULTS;
static struct mbs_settings_blob_load_state settings_load_state;

/* -------------------------------------------------------------------------- */
/* Declarations                                                               */
/* -------------------------------------------------------------------------- */

static int radio_start_receive(const mbs_radio_config *cfg);
static int radio_stop_receive(void);
static int radio_apply_modem_config(const mbs_radio_config *cfg);
static int radio_restore_receive_locked(const mbs_radio_config *cfg);
static int radio_send(const uint8_t *data, uint16_t len);
static void radio_tx_completion_work_handler(struct k_work *work);
static void radio_noise_floor_work_handler(struct k_work *work);
static void radio_cw_work_handler(struct k_work *work);
static void settings_persistence_work_handler(struct k_work *work);
static void radio_receive_callback(const struct device *dev, uint8_t *data, uint16_t size,
				   int16_t rssi, int8_t snr, void *user_data);

/* -------------------------------------------------------------------------- */
/* Validation And Helpers                                                     */
/* -------------------------------------------------------------------------- */

static inline enum lora_signal_bandwidth radio_config_bandwidth_to_lbm(uint32_t bw_hz)
{
	if (bw_hz <= 7800) {
		return BW_7_KHZ;
	} else if (bw_hz <= 10400) {
		return BW_10_KHZ;
	} else if (bw_hz <= 15600) {
		return BW_15_KHZ;
	} else if (bw_hz <= 20800) {
		return BW_20_KHZ;
	} else if (bw_hz <= 31250) {
		return BW_31_KHZ;
	} else if (bw_hz <= 41700) {
		return BW_41_KHZ;
	} else if (bw_hz <= 62500) {
		return BW_62_KHZ;
	} else if (bw_hz <= 125000) {
		return BW_125_KHZ;
	} else if (bw_hz <= 200000) {
		return BW_200_KHZ;
	} else if (bw_hz <= 250000) {
		return BW_250_KHZ;
	} else if (bw_hz <= 400000) {
		return BW_400_KHZ;
	} else if (bw_hz <= 500000) {
		return BW_500_KHZ;
	} else if (bw_hz <= 800000) {
		return BW_800_KHZ;
	} else if (bw_hz <= 1000000) {
		return BW_1000_KHZ;
	} else if (bw_hz <= 1600000) {
		return BW_1600_KHZ;
	} else {
		return BW_500_KHZ;
	}
}

static inline enum lora_coding_rate radio_config_coding_rate_to_driver(uint8_t cr)
{
	switch (cr) {
	case 5:
		return CR_4_5;
	case 6:
		return CR_4_6;
	case 7:
		return CR_4_7;
	case 8:
	default:
		return CR_4_8;
	}
}

static inline enum lora_datarate radio_config_spread_factor_to_driver(uint8_t sf)
{
	/*
	 * Avoid casting arbitrary integers to an enum (can trigger warnings and hides invalid
	 * values).
	 */
	switch (sf) {
	case SF_5:
		return SF_5;
	case SF_6:
		return SF_6;
	case SF_7:
		return SF_7;
	case SF_8:
		return SF_8;
	case SF_9:
		return SF_9;
	case SF_10:
		return SF_10;
	case SF_11:
		return SF_11;
	case SF_12:
		return SF_12;
	default:
		/* Clamp to the nearest supported spreading factor. */
		return (sf < SF_5) ? SF_5 : SF_12;
	}
}

static enum mbs_radio_state radio_state_get(void)
{
	return (enum mbs_radio_state)atomic_get(&radio_state);
}

static void radio_state_set(enum mbs_radio_state state)
{
	atomic_set(&radio_state, (atomic_val_t)state);
}

static void radio_state_publish_snapshot(void)
{
	atomic_val_t sequence = atomic_inc(&radio_state_sequence) + 1;
	struct mbs_radio_state_event event = {
		.enabled = atomic_get(&radio_enabled) != 0,
		.receive_only = atomic_get(&radio_receive_only) != 0,
		.state = radio_state_get(),
		.sequence = (uint32_t)sequence,
	};
	int rc = zbus_chan_pub(&mbs_radio_state_chan, &event, K_NO_WAIT);

	if (rc != 0) {
		LOG_DBG("State publish failed: %d", rc);
	}
}

static bool radio_state_is_receive_mode(enum mbs_radio_state state)
{
	return state == MBS_RADIO_STATE_RECEIVE;
}

static enum radio_receive_mode radio_receive_mode_get(void)
{
	return (enum radio_receive_mode)atomic_get(&radio_receive_mode);
}

static void radio_receive_mode_set(enum radio_receive_mode mode)
{
	atomic_set(&radio_receive_mode, (atomic_val_t)mode);
}

static bool radio_device_ready(void)
{
	return (lora_dev != NULL) && device_is_ready(lora_dev);
}

static bool radio_restore_retryable(int rc)
{
	return rc == -EBUSY || rc == -EIO;
}

static bool radio_state_switch_transmit(void)
{
	while (true) {
		atomic_val_t state = atomic_get(&radio_state);

		if (state == MBS_RADIO_STATE_TRANSMIT) {
			return false;
		}
		if (atomic_cas(&radio_state, state, MBS_RADIO_STATE_TRANSMIT)) {
			return true;
		}
	}
}

static bool radio_state_event_validator(const void *msg, size_t msg_size)
{
	if (msg == NULL) {
		return false;
	}
	if (msg_size != sizeof(struct mbs_radio_state_event)) {
		return false;
	}

	const struct mbs_radio_state_event *event =
		(const struct mbs_radio_state_event *)msg;

	return (uint32_t)event->state <= (uint32_t)MBS_RADIO_STATE_TRANSMIT;
}

static bool radio_publish_validator(const void *msg, size_t msg_size)
{
	ARG_UNUSED(msg_size);

	if (msg == NULL) {
		return false;
	}

	const struct mbs_radio_publish_event *event = msg;
	bool enabled = atomic_get(&radio_enabled) != 0;
	bool receive_only = atomic_get(&radio_receive_only) != 0;

	if (event->len == 0U || event->len > MBS_RADIO_MAX_PAYLOAD) {
		return false;
	}
	if (!enabled || receive_only) {
		return false;
	}
	if (!radio_device_ready()) {
		return false;
	}

	return radio_state_get() != MBS_RADIO_STATE_TRANSMIT;
}

static bool radio_cw_request_validator(const void *msg, size_t msg_size)
{
	if (msg == NULL || msg_size != sizeof(struct mbs_radio_cw_request_event)) {
		return false;
	}

	const struct mbs_radio_cw_request_event *event = msg;
	bool enabled = atomic_get(&radio_enabled) != 0;

	if (event->frequency_hz < 400000000U || event->frequency_hz > 2500000000U) {
		return false;
	}
	if (event->tx_power_dbm < 0 || event->tx_power_dbm > 22) {
		return false;
	}
	if (event->duration_s == 0U || !enabled || !radio_device_ready()) {
		return false;
	}

	return atomic_get(&radio_cw_active) == 0 &&
	       radio_state_get() != MBS_RADIO_STATE_TRANSMIT;
}

/* -------------------------------------------------------------------------- */
/* Runtime PM And Hardware Apply                                              */
/* -------------------------------------------------------------------------- */

static int radio_stop_receive(void)
{
	if (!radio_device_ready()) {
		return -ENODEV;
	}

	int rc;

	switch (radio_receive_mode_get()) {
	case RADIO_RECEIVE_MODE_DUTY_CYCLE:
		rc = lora_recv_duty_cycle_async(lora_dev, K_NO_WAIT, K_NO_WAIT, NULL, NULL);
		break;
	case RADIO_RECEIVE_MODE_ASYNC:
		rc = lora_recv_async(lora_dev, NULL, NULL);
		break;
	case RADIO_RECEIVE_MODE_NONE:
	default:
		return -EINVAL;
	}

	if (rc == 0 || rc == -EINVAL) {
		radio_receive_mode_set(RADIO_RECEIVE_MODE_NONE);
		if (radio_state_get() != MBS_RADIO_STATE_TRANSMIT) {
			radio_state_set(MBS_RADIO_STATE_IDLE);
		}
	}

	return rc;
}

static int radio_start_receive(const mbs_radio_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	if (!radio_device_ready()) {
		return -ENODEV;
	}

	int rc;
	enum radio_receive_mode mode;

	if (cfg->duty_cycle) {
		mode = RADIO_RECEIVE_MODE_DUTY_CYCLE;
		rc = lora_recv_duty_cycle_async(lora_dev, K_MSEC(cfg->duty_cycle_rx_time),
						K_MSEC(cfg->duty_cycle_sleep_time),
						radio_receive_callback, NULL);
	} else {
		mode = RADIO_RECEIVE_MODE_ASYNC;
		rc = lora_recv_async(lora_dev, radio_receive_callback, NULL);
	}

	if (rc == -EBUSY) {
		radio_receive_mode_set(mode);
		radio_state_set(MBS_RADIO_STATE_RECEIVE);
		LOG_DBG("RX already active (%s)", cfg->duty_cycle ? "duty-cycle" : "async");
		return 0;
	}
	if (rc != 0) {
		LOG_ERR("Failed to start %s receive: %d", cfg->duty_cycle ? "duty-cycle" : "async",
			rc);
		radio_receive_mode_set(RADIO_RECEIVE_MODE_NONE);
		radio_state_set(MBS_RADIO_STATE_IDLE);
		return rc;
	}

	radio_receive_mode_set(mode);
	radio_state_set(MBS_RADIO_STATE_RECEIVE);
	LOG_DBG("RX %s started", cfg->duty_cycle ? "duty-cycle" : "async");
	return 0;
}

static int radio_restore_receive_locked(const mbs_radio_config *cfg)
{
	if (cfg == NULL || !cfg->enabled) {
		radio_state_set(MBS_RADIO_STATE_IDLE);
		return 0;
	}

	int rc = radio_start_receive(cfg);

	if (rc != 0) {
		radio_state_set(MBS_RADIO_STATE_IDLE);
	}

	return rc;
}

static int radio_apply_modem_config(const mbs_radio_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	struct lora_modem_config lora_cfg = {
		.frequency = (uint32_t)cfg->frequency,
		.bandwidth = radio_config_bandwidth_to_lbm(cfg->bandwidth),
		.datarate = radio_config_spread_factor_to_driver(cfg->spread_factor),
		.coding_rate = radio_config_coding_rate_to_driver(cfg->coding_rate),
		.preamble_len = (uint16_t)cfg->preamble_length,
		.tx_power = (int8_t)cfg->tx_power,
		.iq_inverted = false,
		.public_network = false,
		.tx = false,
		.packet_crc_disable = !cfg->crc,
		.cad =
			{
				.mode = LORA_CAD_MODE_NONE,
			},
		.rx_boosted = cfg->rx_boosted ? RX_BOOST_ENABLED : RX_BOOST_DISABLED,
	};

	return lora_config(lora_dev, &lora_cfg);
}

static int radio_send(const uint8_t *data, uint16_t len)
{
	bool enabled;
	bool receive_only;

	if (data == NULL || len == 0 || len > MBS_RADIO_MAX_PAYLOAD) {
		return -EINVAL;
	}

	if (!radio_device_ready()) {
		return -ENODEV;
	}

	enabled = atomic_get(&radio_enabled) != 0;
	receive_only = atomic_get(&radio_receive_only) != 0;
	if (!enabled) {
		LOG_DBG("Send denied: radio disabled");
		return -EACCES;
	}
	if (receive_only) {
		LOG_DBG("Send denied: receive-only state");
		return -EACCES;
	}

	k_mutex_lock(&radio_apply_mutex, K_FOREVER);

	/* Serialize state transition against settings apply reconfiguration. */
	k_mutex_lock(&settings_mutex, K_FOREVER);
	enabled = atomic_get(&radio_enabled) != 0;
	receive_only = atomic_get(&radio_receive_only) != 0;
	if (!enabled) {
		k_mutex_unlock(&settings_mutex);
		k_mutex_unlock(&radio_apply_mutex);
		LOG_DBG("Send denied: radio disabled");
		return -EACCES;
	}
	if (receive_only) {
		k_mutex_unlock(&settings_mutex);
		k_mutex_unlock(&radio_apply_mutex);
		LOG_DBG("Send denied: receive-only state");
		return -EACCES;
	}
	if (!radio_state_switch_transmit()) {
		k_mutex_unlock(&settings_mutex);
		k_mutex_unlock(&radio_apply_mutex);
		LOG_DBG("Send denied: radio busy");
		return -EBUSY;
	}
	k_mutex_unlock(&settings_mutex);

	LOG_DBG("TX start: len=%u", len);

	/* Stop RX before TX. Ignore -EINVAL when already stopped. */
	int stop_rx_rc = radio_stop_receive();
	if (stop_rx_rc != 0 && stop_rx_rc != -EINVAL) {
		LOG_WRN("Stop RX before TX failed: %d", stop_rx_rc);
	}

	memcpy(radio_tx_buffer, data, len);
	k_poll_signal_reset(&radio_tx_done);

	int rc = lora_send_async(lora_dev, radio_tx_buffer, len, &radio_tx_done);

#ifdef CONFIG_MBS_RADIO_STATS
	if (rc == 0) {
		uint32_t airtime_ms = lora_airtime(lora_dev, len);
		if (airtime_ms > 0U) {
			STATS_INCN(mbs_radio_stats, tx_air_time_ms, airtime_ms);
		}
	}
#endif

	if (rc != 0) {
		int restore_rc;
		mbs_radio_config cfg_snapshot;

		k_mutex_lock(&settings_mutex, K_FOREVER);
		cfg_snapshot = radio_cfg;
		k_mutex_unlock(&settings_mutex);

		restore_rc = radio_restore_receive_locked(&cfg_snapshot);
		if (restore_rc != 0) {
			LOG_ERR("Failed to return to RX state after TX error: %d", restore_rc);
		}
		k_mutex_unlock(&radio_apply_mutex);
		radio_state_publish_snapshot();
		return rc;
	}

	(void)k_work_reschedule(&radio_tx_completion_work, K_NO_WAIT);
	k_mutex_unlock(&radio_apply_mutex);
	radio_state_publish_snapshot();
	return rc;
}

/* -------------------------------------------------------------------------- */
/* Settings Schema And Apply                                                  */
/* -------------------------------------------------------------------------- */

#define MBS_RADIO_SETTINGS_SUBTREE "meshbus/radio"
#define MBS_RADIO_SETTINGS_KEY_CONFIG "config"

MBS_SETTINGS_BLOB_SCHEMA_DEFINE(radio_settings_schema, MBS_RADIO_SETTINGS_SUBTREE,
			       MBS_RADIO_SETTINGS_KEY_CONFIG, meshbus_RadioConfig,
			       mbs_radio_config);

static int settings_handler_apply(const mbs_radio_config *cfg, bool persistence, bool force)
{
	mbs_radio_config previous_cfg;
	bool modem_params_changed;
	bool need_modem_apply;
	bool previous_receive_active;
	bool reset_noise_floor = false;
	bool config_applied = false;
	int rc = 0;

	if (cfg == NULL) {
		return -EINVAL;
	}

	/* Validate config before taking locks or touching hardware. */
	if (cfg->frequency < 400000000ULL || cfg->frequency > 2500000000ULL) {
		LOG_ERR("Invalid frequency: %llu (valid: 400-2500 MHz)",
			(unsigned long long)cfg->frequency);
		return -EINVAL;
	}
	if (cfg->bandwidth < 7800U || cfg->bandwidth > 500000U) {
		LOG_ERR("Invalid bandwidth: %u (valid: 7800-500000 Hz)",
			(unsigned int)cfg->bandwidth);
		return -EINVAL;
	}
	if (cfg->spread_factor < 5 || cfg->spread_factor > 12) {
		LOG_ERR("Invalid spread factor: %u (valid: 5-12)",
			(unsigned int)cfg->spread_factor);
		return -EINVAL;
	}
	if (cfg->coding_rate < 5 || cfg->coding_rate > 8) {
		LOG_ERR("Invalid coding rate: %u (valid: 5-8)", (unsigned int)cfg->coding_rate);
		return -EINVAL;
	}
	if (cfg->preamble_length < 6U || cfg->preamble_length > 65535U) {
		LOG_ERR("Invalid preamble length: %u (valid: 6-65535)",
			(unsigned int)cfg->preamble_length);
		return -EINVAL;
	}
	if (cfg->duty_cycle_rx_time < 1U || cfg->duty_cycle_rx_time > 262143U) {
		LOG_ERR("Invalid duty cycle RX time: %u (valid: 1-262143 ms)",
			(unsigned int)cfg->duty_cycle_rx_time);
		return -EINVAL;
	}
	if (cfg->duty_cycle_sleep_time < 1U || cfg->duty_cycle_sleep_time > 262143U) {
		LOG_ERR("Invalid duty cycle sleep time: %u (valid: 1-262143 ms)",
			(unsigned int)cfg->duty_cycle_sleep_time);
		return -EINVAL;
	}
	if (cfg->tx_power < 0 || cfg->tx_power > 22) {
		LOG_ERR("Invalid TX power: %d (valid: 0-22 dBm)", (int)cfg->tx_power);
		return -EINVAL;
	}

	k_mutex_lock(&radio_apply_mutex, K_FOREVER);
	k_mutex_lock(&settings_mutex, K_FOREVER);

	/* Avoid reconfiguring the modem during TX. */
	if (radio_state_get() == MBS_RADIO_STATE_TRANSMIT) {
		k_mutex_unlock(&settings_mutex);
		k_mutex_unlock(&radio_apply_mutex);
		LOG_DBG("Settings apply blocked: TX in progress");
		return -EBUSY;
	}

	/* Check if there are any changes (skip if force is set). */
	if (!force && radio_cfg.enabled == cfg->enabled && radio_cfg.frequency == cfg->frequency &&
	    radio_cfg.bandwidth == cfg->bandwidth &&
	    radio_cfg.spread_factor == cfg->spread_factor &&
	    radio_cfg.coding_rate == cfg->coding_rate &&
	    radio_cfg.preamble_length == cfg->preamble_length &&
	    radio_cfg.tx_power == cfg->tx_power && radio_cfg.receive_only == cfg->receive_only &&
	    radio_cfg.rx_boosted == cfg->rx_boosted && radio_cfg.crc == cfg->crc &&
	    radio_cfg.duty_cycle == cfg->duty_cycle &&
	    radio_cfg.duty_cycle_rx_time == cfg->duty_cycle_rx_time &&
	    radio_cfg.duty_cycle_sleep_time == cfg->duty_cycle_sleep_time) {
		settings_initial_apply = true;
		k_mutex_unlock(&settings_mutex);
		k_mutex_unlock(&radio_apply_mutex);
		LOG_DBG("Settings unchanged, nothing to apply");
		return 0;
	}

	LOG_INF("Settings apply: enabled=%d receive_only=%d rx_boosted=%d crc=%d "
		"frequency=%llu bandwidth=%u spread_factor=%d coding_rate=%d preamble=%u "
		"duty_cycle=%d rx_time=%u sleep_time=%u tx_power=%d",
		cfg->enabled, cfg->receive_only, cfg->rx_boosted, cfg->crc,
		cfg->frequency, cfg->bandwidth, cfg->spread_factor, cfg->coding_rate,
		cfg->preamble_length, cfg->duty_cycle, cfg->duty_cycle_rx_time,
		cfg->duty_cycle_sleep_time, cfg->tx_power);

	modem_params_changed =
		(radio_cfg.frequency != cfg->frequency) ||
		(radio_cfg.bandwidth != cfg->bandwidth) ||
		(radio_cfg.spread_factor != cfg->spread_factor) ||
		(radio_cfg.coding_rate != cfg->coding_rate) ||
		(radio_cfg.preamble_length != cfg->preamble_length) ||
		(radio_cfg.tx_power != cfg->tx_power) ||
		(radio_cfg.duty_cycle != cfg->duty_cycle) ||
		(radio_cfg.duty_cycle_rx_time != cfg->duty_cycle_rx_time) ||
		(radio_cfg.duty_cycle_sleep_time != cfg->duty_cycle_sleep_time) ||
		(radio_cfg.rx_boosted != cfg->rx_boosted) ||
		(radio_cfg.crc != cfg->crc);

	need_modem_apply = force || modem_params_changed || (!radio_cfg.enabled && cfg->enabled);
	previous_cfg = radio_cfg;
	previous_receive_active = radio_state_is_receive_mode(radio_state_get());
	k_mutex_unlock(&settings_mutex);

	if (modem_params_changed || !cfg->enabled) {
		struct k_work_sync noise_floor_cancel_sync;

		(void)k_work_cancel_delayable_sync(&radio_noise_floor_work,
						   &noise_floor_cancel_sync);
		k_mutex_lock(&radio_state_mutex, K_FOREVER);
		noise_floor_calibrating = false;
		if (!cfg->enabled) {
			rssi_inst_valid = false;
		}
		k_mutex_unlock(&radio_state_mutex);
	}
	if (!cfg->enabled) {
		struct k_work_sync tx_cancel_sync;

		(void)k_work_cancel_delayable_sync(&radio_tx_completion_work, &tx_cancel_sync);
	}

	/* Hardware state handling */
	if (!cfg->enabled) {
		/* Stop RX when disabling the radio. Ignore -EINVAL when already stopped. */
		if (radio_device_ready() && previous_receive_active) {
			(void)radio_stop_receive();
		}
		radio_receive_mode_set(RADIO_RECEIVE_MODE_NONE);
		radio_state_set(MBS_RADIO_STATE_IDLE);
	} else {
		/* Hardware configure only when enabled. */
		if (!radio_device_ready()) {
			LOG_ERR("LoRa device not ready");
			k_mutex_unlock(&radio_apply_mutex);
			return -ENODEV;
		}

		/* Stop RX before reconfiguring the modem. */
		if (previous_receive_active) {
			int stop_rc = radio_stop_receive();
			if (stop_rc != 0 && stop_rc != -EINVAL) {
				LOG_ERR("Failed to stop RX before settings apply: %d", stop_rc);
				k_mutex_unlock(&radio_apply_mutex);
				return stop_rc;
			}
		}

		if (need_modem_apply) {
			rc = radio_apply_modem_config(cfg);
			if (rc != 0) {
				if (previous_cfg.enabled) {
					int restore_rc = radio_apply_modem_config(&previous_cfg);

					if (restore_rc != 0) {
						LOG_ERR("Failed to restore previous radio config: %d",
							restore_rc);
						k_mutex_unlock(&radio_apply_mutex);
						return restore_rc;
					}
					if (previous_receive_active) {
						restore_rc = radio_start_receive(&previous_cfg);
						if (restore_rc != 0) {
							LOG_ERR("Failed to restore previous RX: %d",
								restore_rc);
							k_mutex_unlock(&radio_apply_mutex);
							return restore_rc;
						}
					}
				}
				LOG_ERR("Failed to apply radio config: %d", rc);
				k_mutex_unlock(&radio_apply_mutex);
				return rc;
			}
		}

		/* Ensure we are in RX state when enabled. */
		if (!radio_state_is_receive_mode(radio_state_get())) {
			rc = radio_start_receive(cfg);
			if (rc != 0) {
				if (previous_cfg.enabled) {
					int restore_rc = radio_apply_modem_config(&previous_cfg);

					if (restore_rc == 0 && previous_receive_active) {
						restore_rc = radio_start_receive(&previous_cfg);
					}
					if (restore_rc != 0) {
						LOG_ERR("Failed to restore previous radio state: %d",
							restore_rc);
						k_mutex_unlock(&radio_apply_mutex);
						return restore_rc;
					}
				}
				k_mutex_unlock(&radio_apply_mutex);
				return rc;
			}
		}
	}

	config_applied = true;
	k_mutex_lock(&settings_mutex, K_FOREVER);
	if (radio_state_get() == MBS_RADIO_STATE_TRANSMIT) {
		rc = -EBUSY;
	} else {
		reset_noise_floor = modem_params_changed;
	}
	if (rc != 0) {
		k_mutex_unlock(&settings_mutex);
		if (config_applied && previous_cfg.enabled) {
			int restore_rc = radio_apply_modem_config(&previous_cfg);

			if (restore_rc == 0 && previous_receive_active) {
				restore_rc = radio_start_receive(&previous_cfg);
			}
			if (restore_rc != 0) {
				LOG_ERR("Failed to restore previous radio state: %d", restore_rc);
				k_mutex_unlock(&radio_apply_mutex);
				return restore_rc;
			}
		}
		k_mutex_unlock(&radio_apply_mutex);
		return rc;
	}

	/* Copy configuration */
	memcpy(&radio_cfg, cfg, sizeof(mbs_radio_config));
	atomic_set(&radio_enabled, cfg->enabled ? 1 : 0);
	atomic_set(&radio_receive_only, cfg->receive_only ? 1 : 0);
	atomic_set(&radio_spread_factor, cfg->spread_factor);
	if (reset_noise_floor) {
		k_mutex_lock(&radio_state_mutex, K_FOREVER);
		noise_floor = 0;
		noise_floor_valid = false;
		noise_floor_sample_count = 0;
		noise_floor_sample_sum = 0;
		rssi_inst_valid = false;
		k_mutex_unlock(&radio_state_mutex);
		LOG_DBG("Noise floor invalidated due to modem config change");
	}
	settings_initial_apply = true;

	k_mutex_unlock(&settings_mutex);
	k_mutex_unlock(&radio_apply_mutex);

	if (persistence) {
		k_work_reschedule(&settings_persistence_work,
				  K_MSEC(CONFIG_MBS_SETTINGS_PERSISTENCE_DELAY));
	}

	radio_state_publish_snapshot();
	return 0;
}

MBS_SETTINGS_BLOB_CONFIG_DEFINE(radio_settings_schema, settings_mutex, settings_load_state,
			       settings_load_cfg, radio_cfg, settings_initial_apply,
			       mbs_radio_config, meshbus_RadioConfig_size,
			       settings_handler_apply, "radio")

SETTINGS_STATIC_HANDLER_DEFINE(mbs_radio, MBS_RADIO_SETTINGS_SUBTREE, NULL,
			       settings_handle_set, settings_handle_commit,
			       settings_handle_export);

/* -------------------------------------------------------------------------- */
/* Callbacks And Work                                                         */
/* -------------------------------------------------------------------------- */

static void radio_noise_floor_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	/* Do not keep retrying while radio is disabled. */
	bool enabled = atomic_get(&radio_enabled) != 0;
	if (!enabled) {
		k_mutex_lock(&radio_state_mutex, K_FOREVER);
		noise_floor_calibrating = false;
		k_mutex_unlock(&radio_state_mutex);
		return;
	}

	if (!radio_device_ready()) {
		(void)k_work_reschedule(&radio_noise_floor_work,
					K_MSEC(NOISE_FLOOR_SAMPLE_RETRY_MS));
		return;
	}

	if (!radio_state_is_receive_mode(radio_state_get())) {
		(void)k_work_reschedule(&radio_noise_floor_work,
					K_MSEC(NOISE_FLOOR_SAMPLE_RETRY_MS));
		return;
	}

	int16_t rssi = 0;
	int rc = lora_rssi_inst(lora_dev, &rssi);
	if (rc != 0) {
		(void)k_work_reschedule(&radio_noise_floor_work,
					K_MSEC(NOISE_FLOOR_SAMPLE_RETRY_MS));
		return;
	}

	bool done = false;
	int16_t calibrated_noise_floor = 0;
	int16_t threshold = 0;

	k_mutex_lock(&radio_state_mutex, K_FOREVER);
	radio_rssi_cache_update_locked(rssi);
	noise_floor_sample_sum += rssi;
	noise_floor_sample_count++;

	if (noise_floor_sample_count >= CONFIG_MBS_RADIO_NOISE_FLOOR_SAMPLES) {
		noise_floor = (int16_t)(noise_floor_sample_sum /
					CONFIG_MBS_RADIO_NOISE_FLOOR_SAMPLES);
		noise_floor_valid = true;
		calibrated_noise_floor = noise_floor;
		threshold = cad_threshold;
		noise_floor_calibrating = false;
		done = true;
	}
	k_mutex_unlock(&radio_state_mutex);

	if (done) {
#ifdef CONFIG_MBS_RADIO_STATS
		STATS_INC(mbs_radio_stats, noise_calibrations);
#endif
		LOG_INF("Noise floor calibrated: %d dBm (threshold: %d dB)", calibrated_noise_floor,
			threshold);
		return;
	}

	(void)k_work_reschedule(&radio_noise_floor_work, K_MSEC(NOISE_FLOOR_SAMPLE_PERIOD_MS));
}

static void radio_publish_finalize(int status)
{
	struct mbs_radio_tx_done_event done_event = {
		.status = status,
	};

#ifdef CONFIG_MBS_RADIO_STATS
	if (status == 0) {
		STATS_INC(mbs_radio_stats, packets_sent);
	} else {
		STATS_INC(mbs_radio_stats, send_failures);
	}
#endif

	mbs_radio_config cfg_snapshot;
	bool enabled;
	int rc = 0;

	k_mutex_lock(&settings_mutex, K_FOREVER);
	cfg_snapshot = radio_cfg;
	enabled = cfg_snapshot.enabled;
	k_mutex_unlock(&settings_mutex);

	if (enabled) {
		k_mutex_lock(&radio_apply_mutex, K_FOREVER);
		rc = radio_restore_receive_locked(&cfg_snapshot);
		k_mutex_unlock(&radio_apply_mutex);
		if (rc != 0) {
			LOG_ERR("Failed to return to RX state: %d", rc);
		}
	} else {
		radio_state_set(MBS_RADIO_STATE_IDLE);
	}

	radio_state_publish_snapshot();
	(void)zbus_chan_pub(&mbs_radio_tx_done_chan, &done_event, K_NO_WAIT);
}

static void radio_tx_completion_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	/* TX completion processing only applies while the service is in TX state. */
	if (radio_state_get() != MBS_RADIO_STATE_TRANSMIT) {
		return;
	}

	int signaled = 0;
	int result = 0;

	k_poll_signal_check(&radio_tx_done, &signaled, &result);
	if (signaled == 0) {
		(void)k_work_reschedule(&radio_tx_completion_work,
					K_MSEC(RADIO_TX_COMPLETION_POLL_MS));
		return;
	}

	k_poll_signal_reset(&radio_tx_done);
	if (result != 0) {
		LOG_ERR("TX failed: %d", result);
	}

	radio_publish_finalize(result);
}

static void radio_publish_work_handler(const struct zbus_channel *chan, const void *message)
{
	if (chan != &mbs_radio_publish_chan || message == NULL) {
		return;
	}

	const struct mbs_radio_publish_event *event = message;
	if (event->len == 0U || event->len > MBS_RADIO_MAX_PAYLOAD) {
		LOG_ERR("TX invalid length: %u", event->len);
#ifdef CONFIG_MBS_RADIO_STATS
		STATS_INC(mbs_radio_stats, send_failures);
#endif
		return;
	}

	LOG_DBG("TX Packet (len: %u)", event->len);
	LOG_HEXDUMP_DBG(event->data, event->len, "Packet dump");
	int rc = radio_send(event->data, event->len);
	if (rc != 0) {
		struct mbs_radio_tx_done_event done_event = {
			.status = rc,
		};

		LOG_ERR("TX failed: %d", rc);
		(void)zbus_chan_pub(&mbs_radio_tx_done_chan, &done_event,
				    K_NO_WAIT);
	}
}

static void radio_receive_callback(const struct device *dev, uint8_t *data, uint16_t size,
				   int16_t rssi, int8_t snr, void *user_data)
{
	ARG_UNUSED(user_data);

	struct mbs_radio_receive_event event = {0};
	bool oversized = size > MBS_RADIO_MAX_PAYLOAD;

	if (oversized) {
		LOG_WRN("Received packet too large: %u bytes", size);
		size = MBS_RADIO_MAX_PAYLOAD;
	}

	memcpy(event.data, data, size);
	event.len = size;
	event.rssi = rssi;
	event.snr = snr;

	k_mutex_lock(&radio_state_mutex, K_FOREVER);
	last_rssi = rssi;
	last_snr = snr;
	radio_rssi_cache_update_locked(rssi);
	k_mutex_unlock(&radio_state_mutex);

#ifdef CONFIG_MBS_RADIO_STATS
	STATS_INC(mbs_radio_stats, packets_recv);
	if (oversized) {
		STATS_INC(mbs_radio_stats, recv_errors);
	}

	uint32_t airtime_ms = lora_airtime(dev, size);
	if (airtime_ms > 0U) {
		STATS_INCN(mbs_radio_stats, rx_air_time_ms, airtime_ms);
	}
#endif

	int rc = zbus_chan_pub(&mbs_radio_receive_chan, &event, K_NO_WAIT);
	if (rc != 0) {
		LOG_ERR("Failed to publish RX message: %d", rc);
#ifdef CONFIG_MBS_RADIO_STATS
		STATS_INC(mbs_radio_stats, recv_errors);
#endif
	}

	LOG_DBG("RX Packet (len: %u, rssi: %d, snr: %d)", size, rssi, snr);
	LOG_HEXDUMP_DBG(event.data, event.len, "Packet dump");
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

int mbs_radio_config_get(mbs_radio_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&settings_mutex, K_FOREVER);
	memcpy(cfg, &radio_cfg, sizeof(*cfg));
	k_mutex_unlock(&settings_mutex);

	return 0;
}

int mbs_radio_config_set(const mbs_radio_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}
	return settings_handler_apply(cfg, true, false);
}

int mbs_radio_config_reset(void)
{
	mbs_radio_config cfg = MBS_RADIO_CONFIG_DEFAULTS;
	struct k_work_sync sync;
	int rc;

	(void)k_work_cancel_delayable_sync(&settings_persistence_work, &sync);

	rc = settings_handler_apply(&cfg, false, true);

	if (rc != 0) {
		return rc;
	}

	rc = mbs_settings_blob_delete(&radio_settings_schema);
	if (rc != 0) {
		LOG_ERR("Failed to delete persisted settings: %d", rc);
		return rc;
	}

	return 0;
}

enum mbs_radio_state mbs_radio_state_get(void)
{
	return radio_state_get();
}

int mbs_radio_status_get(struct mbs_radio_status *status)
{
	if (status == NULL) {
		return -EINVAL;
	}

	enum mbs_radio_state state = radio_state_get();
	uint32_t now = k_uptime_get_32();
	bool rssi_fresh;

	k_mutex_lock(&radio_state_mutex, K_FOREVER);
	rssi_fresh = rssi_inst_valid &&
		     (uint32_t)(now - rssi_inst_timestamp_ms) <=
			     RADIO_RSSI_CACHE_MAX_AGE_MS;
	status->state = state;
	status->last_rssi_dbm = last_rssi;
	status->last_snr_q4 = last_snr;
	status->noise_floor_dbm = noise_floor_valid ? noise_floor : 0;
	status->has_rssi_inst_dbm =
		radio_state_is_receive_mode(state) && rssi_fresh;
	status->rssi_inst_dbm = rssi_inst;
	status->receiving = status->has_rssi_inst_dbm && noise_floor_valid &&
			    rssi_inst > (noise_floor + cad_threshold);
	k_mutex_unlock(&radio_state_mutex);

	return 0;
}

bool mbs_radio_channel_activity(void)
{
	if (!radio_state_is_receive_mode(radio_state_get())) {
		return false;
	}

	if (!radio_device_ready()) {
		return false;
	}

	int16_t rssi = 0;
	int rc = lora_rssi_inst(lora_dev, &rssi);
	int16_t threshold;
	int16_t floor;
	bool valid;

	k_mutex_lock(&radio_state_mutex, K_FOREVER);
	if (rc == 0) {
		radio_rssi_cache_update_locked(rssi);
	}
	threshold = cad_threshold;
	floor = noise_floor;
	valid = noise_floor_valid;
	k_mutex_unlock(&radio_state_mutex);

	if (rc != 0 || !valid) {
		return false;
	}

	return rssi > (floor + threshold);
}

int16_t mbs_radio_last_rssi(void)
{
	k_mutex_lock(&radio_state_mutex, K_FOREVER);
	int16_t rssi = last_rssi;
	k_mutex_unlock(&radio_state_mutex);

	return rssi;
}

int8_t mbs_radio_last_snr(void)
{
	k_mutex_lock(&radio_state_mutex, K_FOREVER);
	int8_t snr = last_snr;
	k_mutex_unlock(&radio_state_mutex);

	return snr;
}

int16_t mbs_radio_noise_floor(void)
{
	k_mutex_lock(&radio_state_mutex, K_FOREVER);
	int16_t floor = noise_floor;
	bool valid = noise_floor_valid;
	k_mutex_unlock(&radio_state_mutex);

	return valid ? floor : 0;
}

void mbs_radio_noise_calibrate(int16_t threshold)
{
	bool start;

	k_mutex_lock(&radio_state_mutex, K_FOREVER);
	cad_threshold = threshold;
	start = !noise_floor_calibrating;
	if (start) {
		noise_floor_sample_count = 0;
		noise_floor_sample_sum = 0;
		noise_floor_calibrating = true;
	}
	k_mutex_unlock(&radio_state_mutex);

	if (!start) {
		LOG_DBG("Noise floor calibration already running; threshold updated to %d dB",
			threshold);
		return;
	}

	LOG_DBG("Noise floor calibration started (threshold: %d dB)", threshold);
	(void)k_work_reschedule(&radio_noise_floor_work, K_NO_WAIT);
}

void mbs_radio_agc_reset(void)
{
	if (mbs_radio_state_get() != MBS_RADIO_STATE_RECEIVE ||
	    mbs_radio_channel_activity()) {
		return;
	}

	if (!radio_device_ready()) {
		return;
	}

	mbs_radio_config cfg_snapshot;
	int rc;

	k_mutex_lock(&radio_apply_mutex, K_FOREVER);
	k_mutex_lock(&settings_mutex, K_FOREVER);
	cfg_snapshot = radio_cfg;
	k_mutex_unlock(&settings_mutex);

	rc = radio_stop_receive();
	if (rc != 0 && rc != -EINVAL) {
		k_mutex_unlock(&radio_apply_mutex);
		LOG_WRN("AGC reset stop RX failed: %d", rc);
		return;
	}

	rc = radio_start_receive(&cfg_snapshot);
	k_mutex_unlock(&radio_apply_mutex);
	if (rc != 0 && rc != -EBUSY) {
		LOG_WRN("AGC reset start RX failed: %d", rc);
		radio_state_set(MBS_RADIO_STATE_IDLE);
		radio_state_publish_snapshot();
		return;
	}

	LOG_DBG("AGC reset complete");
}

int mbs_radio_rssi_inst(int16_t *rssi)
{
	int rc;

	if (rssi == NULL) {
		return -EINVAL;
	}

	if (!radio_device_ready()) {
		return -ENODEV;
	}

	rc = lora_rssi_inst(lora_dev, rssi);
	if (rc == 0) {
		k_mutex_lock(&radio_state_mutex, K_FOREVER);
		radio_rssi_cache_update_locked(*rssi);
		k_mutex_unlock(&radio_state_mutex);
	}

	return rc;
}

uint32_t mbs_radio_airtime(uint16_t len)
{
	if (len > MBS_RADIO_MAX_PAYLOAD) {
		return 0;
	}

	if (!radio_device_ready()) {
		return 0;
	}

	/*
	 * Some drivers invalidate internal modem parameters while running test CW (e.g. LBM),
	 * which can make lora_airtime() crash (division by zero). During TX/CW we therefore
	 * report "unknown" airtime as 0.
	 */
	if (radio_state_get() == MBS_RADIO_STATE_TRANSMIT) {
		return 0;
	}

	return lora_airtime(lora_dev, len);
}

float mbs_radio_packet_score(float snr, uint16_t len)
{
	if (len == 0U || len > MBS_RADIO_MAX_PAYLOAD) {
		return 0.0f;
	}

	uint8_t sf = (uint8_t)atomic_get(&radio_spread_factor);

	/* SNR threshold table starts at SF7, clamp SF5-6 to SF7 threshold */
	if (sf < 7) {
		sf = 7;
	}

	float threshold = snr_threshold_table[sf - 7];

	if (snr < threshold) {
		return 0.0f;
	}

	float success_rate = (snr - threshold) / 10.0f;
	float collision_penalty = 1.0f - ((float)len / 256.0f);
	float score = success_rate * collision_penalty;

	return CLAMP(score, 0.0f, 1.0f);
}

static void radio_cw_publish_done(int status)
{
	struct mbs_radio_cw_done_event event = {
		.status = status,
		.sequence = (uint32_t)atomic_inc(&radio_cw_sequence) + 1U,
	};

	(void)zbus_chan_pub(&mbs_radio_cw_done_chan, &event, K_NO_WAIT);
}

static void radio_cw_complete(int status)
{
	radio_state_publish_snapshot();
	atomic_clear(&radio_cw_active);
	radio_cw_publish_done(status);
}

static bool radio_cw_restore_deadline_expired(void)
{
	return (int32_t)(k_uptime_get_32() - radio_cw_request.restore_deadline_ms) >= 0;
}

static void radio_cw_restore_step(void)
{
	int restore_rc = 0;

	k_mutex_lock(&radio_apply_mutex, K_FOREVER);
	if (radio_cw_request.restore_cfg.enabled) {
		restore_rc = radio_apply_modem_config(&radio_cw_request.restore_cfg);
		if (restore_rc == 0) {
			restore_rc = radio_start_receive(&radio_cw_request.restore_cfg);
		}

		if (radio_restore_retryable(restore_rc) &&
		    !radio_cw_restore_deadline_expired()) {
			radio_state_set(MBS_RADIO_STATE_TRANSMIT);
			k_mutex_unlock(&radio_apply_mutex);
			(void)k_work_reschedule_for_queue(&radio_operation_work_q,
							 &radio_cw_work,
							 K_MSEC(RADIO_CW_RESTORE_RETRY_MS));
			return;
		}
	} else {
		radio_state_set(MBS_RADIO_STATE_IDLE);
	}

	if (restore_rc != 0) {
		radio_receive_mode_set(RADIO_RECEIVE_MODE_NONE);
		radio_state_set(MBS_RADIO_STATE_IDLE);
	}
	k_mutex_unlock(&radio_apply_mutex);

	if (restore_rc != 0) {
		LOG_ERR("CW restore failed: %d", restore_rc);
		radio_cw_complete(restore_rc);
		return;
	}

	radio_cw_complete(radio_cw_request.operation_status);
}

static void radio_cw_start_step(void)
{
	uint32_t start_ms;
	uint32_t elapsed_ms;
	uint32_t expected_ms;
	uint32_t restore_delay_ms = 0U;
	int stop_rx_rc;

	k_mutex_lock(&radio_apply_mutex, K_FOREVER);

	/* Stop RX before starting CW. Ignore -EINVAL when already stopped. */
	stop_rx_rc = radio_stop_receive();
	if (stop_rx_rc != 0 && stop_rx_rc != -EINVAL) {
		LOG_WRN("Stop RX before CW failed: %d", stop_rx_rc);
	}

	start_ms = k_uptime_get_32();
	LOG_INF("CW start: freq=%u Hz, tx_power=%d dBm, duration=%u s",
		(unsigned int)radio_cw_request.frequency_hz,
		(int)radio_cw_request.tx_power_dbm,
		(unsigned int)radio_cw_request.duration_s);

	radio_cw_request.operation_status =
		lora_test_cw(lora_dev, radio_cw_request.frequency_hz,
			     radio_cw_request.tx_power_dbm, radio_cw_request.duration_s);
	k_mutex_unlock(&radio_apply_mutex);

	if (radio_cw_request.operation_status == 0) {
		expected_ms = (uint32_t)radio_cw_request.duration_s * MSEC_PER_SEC;
		elapsed_ms = k_uptime_get_32() - start_ms;
		if (elapsed_ms < expected_ms) {
			restore_delay_ms = expected_ms - elapsed_ms +
					   RADIO_CW_DRIVER_COMPLETION_MARGIN_MS;
		}
	}

	radio_cw_request.restore_deadline_ms =
		k_uptime_get_32() + restore_delay_ms + RADIO_CW_RESTORE_TIMEOUT_MS;
	(void)k_work_reschedule_for_queue(&radio_operation_work_q, &radio_cw_work,
					 K_MSEC(restore_delay_ms));
}

static void radio_cw_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	radio_cw_restore_step();
}

static void radio_cw_request_work_handler(const struct zbus_channel *chan,
					  const void *message)
{
	if (chan != &mbs_radio_cw_chan || message == NULL) {
		return;
	}

	const struct mbs_radio_cw_request_event *event = message;
	int rc = 0;

	if (atomic_get(&radio_enabled) == 0) {
		rc = -EACCES;
	} else if (!radio_device_ready()) {
		rc = -ENODEV;
	} else if (!atomic_cas(&radio_cw_active, 0, 1)) {
		rc = -EBUSY;
	}

	if (rc != 0) {
		radio_cw_publish_done(rc);
		return;
	}

	/*
	 * Packet TX and CW share one Radio operation workqueue. TRANSMIT blocks
	 * new TX/CW requests and settings apply while the CW operation runs.
	 */
	k_mutex_lock(&radio_apply_mutex, K_FOREVER);
	k_mutex_lock(&settings_mutex, K_FOREVER);
	radio_cw_request.restore_cfg = radio_cfg;
	if (!radio_state_switch_transmit()) {
		k_mutex_unlock(&settings_mutex);
		k_mutex_unlock(&radio_apply_mutex);
		atomic_clear(&radio_cw_active);
		radio_cw_publish_done(-EBUSY);
		return;
	}
	k_mutex_unlock(&settings_mutex);
	k_mutex_unlock(&radio_apply_mutex);

	radio_cw_request.frequency_hz = event->frequency_hz;
	radio_cw_request.tx_power_dbm = event->tx_power_dbm;
	radio_cw_request.duration_s = event->duration_s;
	radio_cw_request.operation_status = 0;

	radio_state_publish_snapshot();
	radio_cw_start_step();
}

/* -------------------------------------------------------------------------- */
/* Power Callback                                                             */
/* -------------------------------------------------------------------------- */

static void mbs_power_radio_cb(enum mbs_power_action event, void *user_data)
{
	ARG_UNUSED(user_data);

	if (event != MBS_POWER_ACTION_SHUTDOWN && event != MBS_POWER_ACTION_REBOOT) {
		return;
	}

	if (!radio_device_ready()) {
		return;
	}

	(void)k_work_cancel_delayable(&radio_noise_floor_work);
	(void)k_work_cancel_delayable(&settings_persistence_work);
	(void)k_work_cancel_delayable(&radio_tx_completion_work);
	k_mutex_lock(&radio_state_mutex, K_FOREVER);
	noise_floor_calibrating = false;
	rssi_inst_valid = false;
	k_mutex_unlock(&radio_state_mutex);

	/* Best-effort: stop any ongoing RX/TX and put the modem into sleep. */
	int rc = radio_stop_receive();
	if (rc != 0 && rc != -EINVAL) {
		LOG_WRN("Failed to stop LoRa for power action: %d", rc);
	}

	radio_receive_mode_set(RADIO_RECEIVE_MODE_NONE);
	radio_state_set(MBS_RADIO_STATE_IDLE);
	radio_state_publish_snapshot();
	LOG_INF("Radio stopped");
}
MBS_POWER_ACTION_CALLBACK_DEFINE(mbs_power_radio_cb, NULL);

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

static int mbs_radio_init(void)
{
	k_work_init_delayable(&settings_persistence_work, settings_persistence_work_handler);
	k_work_init_delayable(&radio_noise_floor_work, radio_noise_floor_work_handler);
	k_work_init_delayable(&radio_tx_completion_work, radio_tx_completion_work_handler);
	k_work_init_delayable(&radio_cw_work, radio_cw_work_handler);
	k_work_queue_init(&radio_operation_work_q);
	k_work_queue_start(&radio_operation_work_q, radio_operation_work_q_stack,
			   K_THREAD_STACK_SIZEOF(radio_operation_work_q_stack),
			   K_PRIO_COOP(CONFIG_MBS_RADIO_OPERATION_WORK_QUEUE_PRIORITY),
			   NULL);
	(void)zbus_async_listener_set_work_queue(&radio_publish_listener,
						 &radio_operation_work_q);
	(void)zbus_async_listener_set_work_queue(&radio_cw_request_listener,
						 &radio_operation_work_q);
	int rc;

#ifdef CONFIG_MBS_RADIO_STATS
	rc = STATS_INIT_AND_REG(mbs_radio_stats, STATS_SIZE_32, "mbs_radio");
	if (rc != 0) {
		LOG_WRN("Failed to register stats: %d", rc);
	}
#endif

	if (!radio_device_ready()) {
		LOG_WRN("LoRa device not ready");
	} else {
		LOG_INF("LoRa device ready (%s)", lora_dev->name);
	}

	/* Load settings */
	rc = settings_load_subtree(MBS_RADIO_SETTINGS_SUBTREE);
	if (rc != 0) {
		return rc;
	}
	if (!settings_initial_apply) {
		rc = settings_handler_apply(&radio_cfg, false, true);
		if (rc != 0) {
			return rc;
		}
	}

	radio_state_publish_snapshot();
	return 0;
}

SYS_INIT(mbs_radio_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
