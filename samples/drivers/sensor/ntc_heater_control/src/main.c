/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(ntc_heater_control, LOG_LEVEL_INF);

#define AMBIENT_TEMP_NODE DT_ALIAS(ambient_temp0)
#define HEATER_PWM_NODE   DT_ALIAS(heater_pwm0)

#if !DT_NODE_EXISTS(AMBIENT_TEMP_NODE)
#error "No ambient-temp0 alias found in devicetree"
#endif

#if !DT_NODE_EXISTS(HEATER_PWM_NODE)
#error "No heater-pwm0 alias found in devicetree"
#endif

#define HEATER_TARGET_MC_DEFAULT    45000
#define HEATER_HYST_MC_DEFAULT      1000
#define HEATER_MAX_DUTY_DEFAULT     8U
#define HEATER_CUTOFF_MC_DEFAULT    50000
#define HEATER_PERIOD_MS_DEFAULT    250U
#define HEATER_PERIOD_MS_MIN        50U
#define HEATER_PERIOD_MS_MAX        5000U
#define HEATER_TARGET_MC_MIN        0
#define HEATER_TARGET_MC_MAX        50000
#define HEATER_HYST_MC_MIN          100
#define HEATER_HYST_MC_MAX          20000
#define HEATER_CUTOFF_MC_MIN        20000
#define HEATER_CUTOFF_MC_MAX        50000

struct heater_config {
	bool enabled;
	int32_t target_mc;
	int32_t hyst_mc;
	uint8_t max_duty;
	int32_t cutoff_mc;
	uint32_t period_ms;
};

struct heater_state {
	bool first_sample_ready;
	bool heater_on;
	bool overtemp_active;
	bool sensor_fault;
	bool temp_valid;
	int32_t last_temp_mc;
	uint8_t duty_requested;
	uint8_t duty_applied;
};

struct heater_snapshot {
	struct heater_config cfg;
	struct heater_state state;
};

static const struct device *const ambient_sensor = DEVICE_DT_GET(AMBIENT_TEMP_NODE);
static const struct pwm_dt_spec heater_pwm = PWM_DT_SPEC_GET(HEATER_PWM_NODE);

static struct heater_config heater_cfg = {
	.enabled = true,
	.target_mc = HEATER_TARGET_MC_DEFAULT,
	.hyst_mc = HEATER_HYST_MC_DEFAULT,
	.max_duty = HEATER_MAX_DUTY_DEFAULT,
	.cutoff_mc = HEATER_CUTOFF_MC_DEFAULT,
	.period_ms = HEATER_PERIOD_MS_DEFAULT,
};

static struct heater_state heater_state = {
	.first_sample_ready = false,
	.heater_on = false,
	.overtemp_active = false,
	.sensor_fault = false,
	.temp_valid = false,
	.last_temp_mc = 0,
	.duty_requested = 0,
	.duty_applied = 0,
};

K_MUTEX_DEFINE(heater_lock);

static void mdegc_snprint(char *buf, size_t len, int32_t milli_deg_c)
{
	int64_t abs_mc = (milli_deg_c < 0) ? -(int64_t)milli_deg_c : (int64_t)milli_deg_c;
	int64_t ip = abs_mc / 1000LL;
	int64_t fp = abs_mc % 1000LL;

	(void)snprintk(buf, len, "%s%lld.%03lld", (milli_deg_c < 0) ? "-" : "",
		       (long long)ip, (long long)fp);
}

static int heater_apply_duty(uint8_t duty_pct)
{
	uint32_t pulse = (uint32_t)(((uint64_t)heater_pwm.period * duty_pct) / 100ULL);

	return pwm_set_pulse_dt(&heater_pwm, pulse);
}

static int heater_read_temp_mc(int32_t *temp_mc)
{
	struct sensor_value temp = {0};
	int64_t temp_milli;
	int rc;

	if (temp_mc == NULL) {
		return -EINVAL;
	}

	rc = sensor_sample_fetch_chan(ambient_sensor, SENSOR_CHAN_AMBIENT_TEMP);
	if (rc != 0) {
		return rc;
	}

	rc = sensor_channel_get(ambient_sensor, SENSOR_CHAN_AMBIENT_TEMP, &temp);
	if (rc != 0) {
		return rc;
	}

	temp_milli = sensor_value_to_milli(&temp);
	if ((temp_milli > INT32_MAX) || (temp_milli < INT32_MIN)) {
		return -ERANGE;
	}

	*temp_mc = (int32_t)temp_milli;
	return 0;
}

static void heater_snapshot_get(struct heater_snapshot *snap)
{
	k_mutex_lock(&heater_lock, K_FOREVER);
	*snap = (struct heater_snapshot){
		.cfg = heater_cfg,
		.state = heater_state,
	};
	k_mutex_unlock(&heater_lock);
}

static void heater_status_snprint(char *buf, size_t len, const struct heater_snapshot *snap)
{
	char target_buf[24];
	char hyst_buf[24];
	char cutoff_buf[24];
	char temp_buf[24];

	if ((buf == NULL) || (len == 0U) || (snap == NULL)) {
		return;
	}

	mdegc_snprint(target_buf, sizeof(target_buf), snap->cfg.target_mc);
	mdegc_snprint(hyst_buf, sizeof(hyst_buf), snap->cfg.hyst_mc);
	mdegc_snprint(cutoff_buf, sizeof(cutoff_buf), snap->cfg.cutoff_mc);

	if (snap->state.temp_valid) {
		mdegc_snprint(temp_buf, sizeof(temp_buf), snap->state.last_temp_mc);
	} else {
		(void)snprintk(temp_buf, sizeof(temp_buf), "n/a");
	}

	(void)snprintk(buf, len,
		       "temp=%s C enabled=%s first_sample=%s heater=%s duty=%u%%(applied=%u%%) "
		       "target=%s C hyst=%s C max_duty=%u%% cutoff=%s C period=%u ms sensor_fault=%s cutoff_active=%s",
		       temp_buf, snap->cfg.enabled ? "on" : "off",
		       snap->state.first_sample_ready ? "yes" : "no",
		       snap->state.heater_on ? "on" : "off", snap->state.duty_requested,
		       snap->state.duty_applied, target_buf, hyst_buf, snap->cfg.max_duty, cutoff_buf,
		       snap->cfg.period_ms, snap->state.sensor_fault ? "yes" : "no",
		       snap->state.overtemp_active ? "yes" : "no");
}

static void heater_control_step(void)
{
	int32_t temp_mc = 0;
	int rc = heater_read_temp_mc(&temp_mc);
	uint8_t duty = 0U;
	bool log_sensor_fault = false;
	bool log_sensor_recovered = false;
	bool log_cutoff_enter = false;
	bool log_cutoff_exit = false;
	int32_t cutoff_mc = 0;
	int pwm_rc;

	k_mutex_lock(&heater_lock, K_FOREVER);

	if (rc == 0) {
		if (heater_state.sensor_fault) {
			log_sensor_recovered = true;
		}

		heater_state.sensor_fault = false;
		heater_state.temp_valid = true;
		heater_state.last_temp_mc = temp_mc;

		if (!heater_state.first_sample_ready) {
			heater_state.first_sample_ready = true;
			heater_state.heater_on = false;
		} else {
			if (temp_mc >= heater_cfg.cutoff_mc) {
				if (!heater_state.overtemp_active) {
					log_cutoff_enter = true;
				}
				heater_state.overtemp_active = true;
			} else if (heater_state.overtemp_active) {
				heater_state.overtemp_active = false;
				log_cutoff_exit = true;
			}

			if (!heater_cfg.enabled || heater_state.overtemp_active) {
				heater_state.heater_on = false;
			} else if (temp_mc <= (heater_cfg.target_mc - heater_cfg.hyst_mc)) {
				heater_state.heater_on = true;
			} else if (temp_mc >= (heater_cfg.target_mc + heater_cfg.hyst_mc)) {
				heater_state.heater_on = false;
			}
		}
	} else {
		if (!heater_state.sensor_fault) {
			log_sensor_fault = true;
		}
		heater_state.sensor_fault = true;
		heater_state.temp_valid = false;
		heater_state.heater_on = false;
	}

	duty = heater_state.heater_on ? heater_cfg.max_duty : 0U;
	heater_state.duty_requested = duty;
	cutoff_mc = heater_cfg.cutoff_mc;

	k_mutex_unlock(&heater_lock);

	pwm_rc = heater_apply_duty(duty);
	if (pwm_rc != 0) {
		LOG_ERR("Failed to set heater duty=%u%%: %d", duty, pwm_rc);
		(void)heater_apply_duty(0U);

		k_mutex_lock(&heater_lock, K_FOREVER);
		heater_state.heater_on = false;
		heater_state.duty_requested = 0U;
		heater_state.duty_applied = 0U;
		k_mutex_unlock(&heater_lock);
	} else {
		k_mutex_lock(&heater_lock, K_FOREVER);
		heater_state.duty_applied = duty;
		k_mutex_unlock(&heater_lock);
	}

	if (log_sensor_fault) {
		LOG_ERR("NTC read failed (%d), heater forced OFF", rc);
	}

	if (log_sensor_recovered) {
		char temp_buf[24];

		mdegc_snprint(temp_buf, sizeof(temp_buf), temp_mc);
		LOG_INF("NTC recovered at %s C", temp_buf);
	}

	if (log_cutoff_enter) {
		char temp_buf[24];
		char cutoff_buf[24];

		mdegc_snprint(temp_buf, sizeof(temp_buf), temp_mc);
		mdegc_snprint(cutoff_buf, sizeof(cutoff_buf), cutoff_mc);
		LOG_WRN("Overtemp cutoff: temp=%s C cutoff=%s C, heater forced OFF", temp_buf,
			cutoff_buf);
	}

	if (log_cutoff_exit) {
		char temp_buf[24];
		char cutoff_buf[24];

		mdegc_snprint(temp_buf, sizeof(temp_buf), temp_mc);
		mdegc_snprint(cutoff_buf, sizeof(cutoff_buf), cutoff_mc);
		LOG_INF("Cutoff released: temp=%s C < cutoff=%s C, hysteresis resumed", temp_buf,
			cutoff_buf);
	}
}

int main(void)
{
	int rc;

	if (!device_is_ready(ambient_sensor)) {
		LOG_ERR("NTC sensor not ready: %s", ambient_sensor->name);
		return -ENODEV;
	}

	if (!device_is_ready(heater_pwm.dev)) {
		LOG_ERR("Heater PWM device not ready: %s", heater_pwm.dev->name);
		return -ENODEV;
	}

	rc = heater_apply_duty(0U);
	if (rc != 0) {
		LOG_ERR("Failed to initialize heater output OFF: %d", rc);
		return rc;
	}

	LOG_INF("NTC heater control sample started");
	LOG_INF("Sensor=%s PWM=%s", ambient_sensor->name, heater_pwm.dev->name);
	LOG_INF("Default config: target=45.000 C hyst=1.000 C max_duty=8%% cutoff=50.000 C period=250 ms");
	LOG_INF("Runtime control: fixed defaults (shell disabled), status is logged periodically");

	while (1) {
		struct heater_snapshot snap;
		char line_buf[320];
		uint32_t period_ms;

		heater_control_step();
		heater_snapshot_get(&snap);
		heater_status_snprint(line_buf, sizeof(line_buf), &snap);
		LOG_INF("%s", line_buf);

		k_mutex_lock(&heater_lock, K_FOREVER);
		period_ms = heater_cfg.period_ms;
		k_mutex_unlock(&heater_lock);

		k_sleep(K_MSEC(period_ms));
	}

	return 0;
}
