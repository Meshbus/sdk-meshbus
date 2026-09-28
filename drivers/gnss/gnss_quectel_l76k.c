/*
 * Copyright (c) 2025 FoBE Projects
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/gnss.h>
#include <zephyr/drivers/gnss/gnss_publish.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/modem/backend/uart.h>
#include <zephyr/modem/chat.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>

#include <string.h>

/* Zephyr GNSS driver internal headers (path added via CMakeLists.txt) */
// #include "gnss_nmea0183.h"
#include "gnss_nmea0183_match.h"
// #include "gnss_parse.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(quectel_l76k, CONFIG_GNSS_LOG_LEVEL);

#define QUECTEL_L76K_PM_TIMEOUT K_MSEC(250U)

/* L76K PCAS11 navigation modes */
#define QUECTEL_L76K_NAV_MODE_PORTABLE    0
#define QUECTEL_L76K_NAV_MODE_STATIC      1
#define QUECTEL_L76K_NAV_MODE_WALKING     2
#define QUECTEL_L76K_NAV_MODE_CAR         3
#define QUECTEL_L76K_NAV_MODE_NAVIGATION  4
#define QUECTEL_L76K_NAV_MODE_AVIATION_1G 5
#define QUECTEL_L76K_NAV_MODE_AVIATION_2G 6
#define QUECTEL_L76K_NAV_MODE_AVIATION_4G 7

/* L76K PCAS04 satellite system modes */
#define QUECTEL_L76K_SYS_GPS                1
#define QUECTEL_L76K_SYS_BEIDOU             2
#define QUECTEL_L76K_SYS_GPS_BEIDOU         3
#define QUECTEL_L76K_SYS_GLONASS            4
#define QUECTEL_L76K_SYS_GPS_GLONASS        5
#define QUECTEL_L76K_SYS_BEIDOU_GLONASS     6
#define QUECTEL_L76K_SYS_GPS_BEIDOU_GLONASS 7

/* L76K PCAS01 baud rate modes */
#define QUECTEL_L76K_BAUD_4800        0
#define QUECTEL_L76K_BAUD_9600        1
#define QUECTEL_L76K_BAUD_19200       2
#define QUECTEL_L76K_BAUD_38400       3
#define QUECTEL_L76K_BAUD_57600       4
#define QUECTEL_L76K_BAUD_115200      5
#define QUECTEL_L76K_DEFAULT_BAUDRATE 9600

struct quectel_l76k_config {
	const struct device *uart;
	struct gpio_dt_spec pps_gpio;
	struct gpio_dt_spec reset_gpio;
	struct gpio_dt_spec wakeup_gpio;
};

struct quectel_l76k_data {
	struct gnss_nmea0183_match_data match_data;
#if CONFIG_GNSS_SATELLITES
	struct gnss_satellite satellites[CONFIG_GNSS_QUECTEL_L76K_SAT_ARRAY_SIZE];
#endif

	/* UART backend */
	struct modem_pipe *uart_pipe;
	/* Track whether uart_pipe has been opened (pm_runtime_get taken). */
	bool uart_pipe_open;
	struct modem_backend_uart uart_backend;
	uint8_t uart_backend_receive_buf[CONFIG_GNSS_QUECTEL_L76K_UART_RX_BUF_SIZE];
	uint8_t uart_backend_transmit_buf[CONFIG_GNSS_QUECTEL_L76K_UART_TX_BUF_SIZE];

	/* Modem chat */
	struct modem_chat chat;
	uint8_t chat_receive_buf[CONFIG_GNSS_QUECTEL_L76K_CHAT_RX_BUF_SIZE];
	uint8_t chat_delimiter[2];
	uint8_t *chat_argv[32];

	/* Command buffer for PCAS commands */
	uint8_t cmd_buf[64];

	/* Cached configuration (L76K doesn't support query commands) */
	uint32_t fix_rate_ms;
	enum gnss_navigation_mode navigation_mode;
	gnss_systems_t enabled_systems;

	/* Desired UART speed from devicetree/current-speed at init. */
	uint32_t target_baudrate;

	/* PPS timestamp */
	struct gpio_callback pps_cb;
	struct k_spinlock pps_lock;
	k_ticks_t pps_timestamp;
	bool pps_valid;

	/* Track if baudrate has been configured (survives suspend/resume) */
	bool baudrate_configured;

	struct k_sem lock;
	k_timepoint_t pm_deadline;
};

#if CONFIG_GNSS_SATELLITES
/* Custom GSV callback to handle BeiDou with $BDGSV prefix */
static void quectel_l76k_gsv_callback(struct modem_chat *chat, char **argv, uint16_t argc,
				      void *user_data)
{
	/* L76K may use $BDGSV for BeiDou instead of $GBGSV
	 * Convert $BDGSV to $GBGSV for the standard parser
	 */
	if (argc > 0 && argv[0] != NULL && strlen(argv[0]) >= 3) {
		if (argv[0][1] == 'B' && argv[0][2] == 'D') {
			/* Temporarily modify to $GBGSV format for parser */
			argv[0][1] = 'G';
			argv[0][2] = 'B';
		}
	}

	/* Call the standard GSV handler */
	gnss_nmea0183_match_gsv_callback(chat, argv, argc, user_data);
}
#endif /* CONFIG_GNSS_SATELLITES */

MODEM_CHAT_MATCHES_DEFINE(
	unsol_matches, MODEM_CHAT_MATCH_WILDCARD("$??GGA,", ",*", gnss_nmea0183_match_gga_callback),
	MODEM_CHAT_MATCH_WILDCARD("$??RMC,", ",*", gnss_nmea0183_match_rmc_callback),
#if CONFIG_GNSS_SATELLITES
	MODEM_CHAT_MATCH_WILDCARD("$??GSV,", ",*", quectel_l76k_gsv_callback),
#endif
);

static void quectel_l76k_lock(const struct device *dev)
{
	struct quectel_l76k_data *data = dev->data;

	(void)k_sem_take(&data->lock, K_FOREVER);
}

static void quectel_l76k_unlock(const struct device *dev)
{
	struct quectel_l76k_data *data = dev->data;

	k_sem_give(&data->lock);
}

static void quectel_l76k_pm_changed(const struct device *dev)
{
	struct quectel_l76k_data *data = dev->data;

	data->pm_deadline = sys_timepoint_calc(QUECTEL_L76K_PM_TIMEOUT);
}

static void quectel_l76k_await_pm_ready(const struct device *dev)
{
	struct quectel_l76k_data *data = dev->data;

	LOG_DBG("Waiting until PM ready");
	k_sleep(sys_timepoint_timeout(data->pm_deadline));
}

static void quectel_l76k_pps_callback(const struct device *port, struct gpio_callback *cb,
				      gpio_port_pins_t pins)
{
	struct quectel_l76k_data *data = CONTAINER_OF(cb, struct quectel_l76k_data, pps_cb);
	k_spinlock_key_t key;

	key = k_spin_lock(&data->pps_lock);
	data->pps_timestamp = k_uptime_ticks();
	data->pps_valid = true;
	k_spin_unlock(&data->pps_lock, key);
}

static int quectel_l76k_configure_pps_gpio(const struct device *dev)
{
	const struct quectel_l76k_config *config = dev->config;
	struct quectel_l76k_data *data = dev->data;
	int ret;

	if (!gpio_is_ready_dt(&config->pps_gpio)) {
		return 0; /* PPS GPIO not configured */
	}

	ret = gpio_pin_configure_dt(&config->pps_gpio, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure PPS GPIO");
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&config->pps_gpio, GPIO_INT_EDGE_RISING);
	if (ret < 0) {
		LOG_ERR("Failed to configure PPS GPIO interrupt");
		return ret;
	}

	gpio_init_callback(&data->pps_cb, quectel_l76k_pps_callback, BIT(config->pps_gpio.pin));
	ret = gpio_add_callback(config->pps_gpio.port, &data->pps_cb);
	if (ret < 0) {
		LOG_ERR("Failed to add PPS GPIO callback");
		return ret;
	}

	return 0;
}

static int quectel_l76k_baudrate_to_mode(uint32_t baudrate)
{
	switch (baudrate) {
	case 4800:
		return QUECTEL_L76K_BAUD_4800;
	case 9600:
		return QUECTEL_L76K_BAUD_9600;
	case 19200:
		return QUECTEL_L76K_BAUD_19200;
	case 38400:
		return QUECTEL_L76K_BAUD_38400;
	case 57600:
		return QUECTEL_L76K_BAUD_57600;
	case 115200:
		return QUECTEL_L76K_BAUD_115200;
	default:
		return -EINVAL;
	}
}

static int quectel_l76k_send_pcas_cmd(const struct device *dev, const char *cmd)
{
	struct quectel_l76k_data *data = dev->data;
	uint8_t checksum = 0;
	int ret;

	if (!data->uart_pipe_open) {
		return -EPIPE;
	}

	/* Calculate checksum (XOR of all characters between $ and *) */
	for (const char *p = cmd; *p != '\0'; p++) {
		checksum ^= *p;
	}

	ret = snprintf((char *)data->cmd_buf, sizeof(data->cmd_buf), "$%s*%02X\r\n", cmd, checksum);
	if (ret < 0 || ret >= sizeof(data->cmd_buf)) {
		return -ENOMEM;
	}

	ret = modem_pipe_transmit(data->uart_pipe, data->cmd_buf, ret);
	if (ret < 0) {
		LOG_ERR("Failed to send command: %s", cmd);
		return ret;
	}

	/* L76K PCAS commands don't have ACK, wait a short time for command to be processed */
	k_sleep(K_MSEC(100));

	return 0;
}

static int quectel_l76k_runtime_get(const struct device *dev, bool *acquired)
{
	int ret;

	*acquired = false;
	if (!IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME)) {
		return 0;
	}

	ret = pm_device_runtime_get(dev);
	if (ret < 0 && ret != -ENOTSUP) {
		return ret;
	}

	*acquired = (ret >= 0);
	return 0;
}

static void quectel_l76k_runtime_put(const struct device *dev, bool acquired)
{
	if (acquired) {
		(void)pm_device_runtime_put(dev);
	}
}

static int quectel_l76k_set_uart_session(const struct device *dev, struct uart_config *uart_cfg,
					 uint32_t baudrate)
{
	const struct quectel_l76k_config *config = dev->config;
	struct quectel_l76k_data *data = dev->data;
	int ret;

	if (data->uart_pipe_open) {
		modem_chat_release(&data->chat);
		ret = modem_pipe_close(data->uart_pipe, K_MSEC(500));
		if (ret < 0 && ret != -EALREADY) {
			LOG_WRN("Failed to close pipe for baudrate change: %d", ret);
			return ret;
		}
		data->uart_pipe_open = false;
	}

	uart_cfg->baudrate = baudrate;
	ret = uart_configure(config->uart, uart_cfg);
	if (ret < 0) {
		LOG_ERR("Failed to configure UART to %u: %d", baudrate, ret);
		return ret;
	}

	ret = modem_pipe_open(data->uart_pipe, K_SECONDS(10));
	if (ret < 0) {
		LOG_ERR("Failed to open pipe at %u baud: %d", baudrate, ret);
		return ret;
	}
	data->uart_pipe_open = true;

	ret = modem_chat_attach(&data->chat, data->uart_pipe);
	if (ret < 0) {
		LOG_ERR("Failed to attach chat at %u baud: %d", baudrate, ret);
		(void)modem_pipe_close(data->uart_pipe, K_SECONDS(10));
		data->uart_pipe_open = false;
		return ret;
	}

	return 0;
}

static int quectel_l76k_configure_baudrate(const struct device *dev)
{
	const struct quectel_l76k_config *config = dev->config;
	struct quectel_l76k_data *data = dev->data;
	struct uart_config uart_cfg;
	uint32_t recover_baudrate = QUECTEL_L76K_DEFAULT_BAUDRATE;
	char cmd[16];
	int baud_mode;
	int recover_ret;
	int ret;

	ret = uart_config_get(config->uart, &uart_cfg);
	if (ret < 0) {
		LOG_ERR("Failed to get UART config: %d", ret);
		return ret;
	}

	if (data->target_baudrate == QUECTEL_L76K_DEFAULT_BAUDRATE) {
		LOG_DBG("No baudrate change needed (already at %u)", data->target_baudrate);
		data->baudrate_configured = true;
		return 0;
	}

	if (data->baudrate_configured) {
		LOG_DBG("Baudrate already configured to %u", data->target_baudrate);
		return 0;
	}

	baud_mode = quectel_l76k_baudrate_to_mode(data->target_baudrate);
	if (baud_mode < 0) {
		LOG_ERR("Invalid target baudrate: %u", data->target_baudrate);
		return baud_mode;
	}

	LOG_INF("Changing module baudrate from %u to %u", QUECTEL_L76K_DEFAULT_BAUDRATE,
		data->target_baudrate);

	ret = quectel_l76k_set_uart_session(dev, &uart_cfg, QUECTEL_L76K_DEFAULT_BAUDRATE);
	if (ret < 0) {
		goto out_recover;
	}

	snprintf(cmd, sizeof(cmd), "PCAS01,%d", baud_mode);
	ret = quectel_l76k_send_pcas_cmd(dev, cmd);
	if (ret < 0) {
		LOG_ERR("Failed to send baudrate command");
		goto out_recover;
	}
	recover_baudrate = data->target_baudrate;

	ret = quectel_l76k_set_uart_session(dev, &uart_cfg, data->target_baudrate);
	if (ret < 0) {
		goto out_recover;
	}

	data->baudrate_configured = true;
	LOG_INF("UART and module now at %u bps", data->target_baudrate);
	return 0;

out_recover:
	data->baudrate_configured = false;
	recover_ret = quectel_l76k_set_uart_session(dev, &uart_cfg, recover_baudrate);
	if (recover_ret < 0) {
		LOG_WRN("Failed to recover UART session at %u baud: %d",
			recover_baudrate, recover_ret);
	}
	return ret;
}

static int quectel_l76k_resume(const struct device *dev)
{
	const struct quectel_l76k_config *config = dev->config;
	struct quectel_l76k_data *data = dev->data;
	int ret;

	LOG_DBG("Resuming");

	quectel_l76k_await_pm_ready(dev);

	/* Pull WAKEUP high if available */
	if (gpio_is_ready_dt(&config->wakeup_gpio)) {
		ret = gpio_pin_set_dt(&config->wakeup_gpio, 1);
		if (ret < 0) {
			LOG_ERR("Failed to set WAKEUP GPIO: %d", ret);
		}
		k_sleep(K_MSEC(250));
	}

	ret = modem_pipe_open(data->uart_pipe, K_SECONDS(10));
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("Failed to open pipe: %d", ret);
		return ret;
	}
	data->uart_pipe_open = true;

	ret = modem_chat_attach(&data->chat, data->uart_pipe);
	if (ret < 0) {
		LOG_ERR("Failed to attach chat: %d", ret);
		/* Best-effort close only if we actually opened the pipe. */
		if (data->uart_pipe_open) {
			(void)modem_pipe_close(data->uart_pipe, K_SECONDS(10));
			data->uart_pipe_open = false;
		}
		return ret;
	}

	/* Configure baudrate if UART current-speed differs from module default (9600).
	 * This will close/reopen the pipe with proper baudrate negotiation.
	 */
	ret = quectel_l76k_configure_baudrate(dev);
	if (ret < 0) {
		LOG_WRN("Failed to configure baudrate: %d", ret);
		/* Continue anyway - may work at current UART baudrate */
	}

	/* Configure NMEA output: GGA, GLL, GSA, GSV, RMC, VTG, ZDA, ANT */
	ret = quectel_l76k_send_pcas_cmd(dev, "PCAS03,1,0,0,1,1,0,0,0,0,0,0,0,0");
	if (ret < 0) {
		LOG_WRN("Failed to configure NMEA output: %d", ret);
	}

	LOG_DBG("Resumed");
	return 0;
}

#ifdef CONFIG_PM_DEVICE
static int quectel_l76k_suspend(const struct device *dev)
{
	const struct quectel_l76k_config *config = dev->config;
	struct quectel_l76k_data *data = dev->data;

	LOG_DBG("Suspending");

	quectel_l76k_await_pm_ready(dev);

	/* Detach chat before closing pipe (only if the pipe was opened). */
	if (data->uart_pipe_open) {
		modem_chat_release(&data->chat);
	}

	/* Pull WAKEUP low if available to enter low power mode */
	if (gpio_is_ready_dt(&config->wakeup_gpio)) {
		gpio_pin_set_dt(&config->wakeup_gpio, 0);
	}

	if (data->uart_pipe_open) {
		(void)modem_pipe_close(data->uart_pipe, K_SECONDS(10));
		data->uart_pipe_open = false;
	}

	LOG_DBG("Suspended");
	return 0;
}

static int quectel_l76k_turn_on(const struct device *dev)
{
	const struct quectel_l76k_config *config = dev->config;

	LOG_DBG("Powered on");

	/*
	 * Keep WAKEUP deasserted on TURN_ON.
	 *
	 * TURN_ON can happen when the shared power-domain is enabled for other
	 * peripherals. Asserting WAKEUP here would unintentionally start GNSS.
	 * PM_DEVICE_ACTION_RESUME is the point where the GNSS device is explicitly
	 * requested to become active.
	 */
	if (gpio_is_ready_dt(&config->wakeup_gpio)) {
		(void)gpio_pin_set_dt(&config->wakeup_gpio, 0);
	}

	if (gpio_is_ready_dt(&config->reset_gpio)) {
		/*
		 * reset-gpios is declared as GPIO_ACTIVE_LOW in DT, so "inactive"
		 * is the physical high level (module running).
		 */
		(void)gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT_INACTIVE);
	}

	return 0;
}

static int quectel_l76k_turn_off(const struct device *dev)
{
	const struct quectel_l76k_config *config = dev->config;
	struct quectel_l76k_data *data = dev->data;
	int ret;

	LOG_DBG("Powered off");

	/* Release chat and close pipe only if opened. */
	if (data->uart_pipe_open) {
		modem_chat_release(&data->chat);
	}

	/* Pull WAKEUP low before power off */
	if (gpio_is_ready_dt(&config->wakeup_gpio)) {
		gpio_pin_set_dt(&config->wakeup_gpio, 0);
	}

	if (gpio_is_ready_dt(&config->reset_gpio)) {
		gpio_pin_configure_dt(&config->reset_gpio, GPIO_DISCONNECTED);
	}

	/* Module power is removed, baudrate needs reconfiguration on next power-on */
	data->baudrate_configured = false;

	if (data->uart_pipe_open) {
		ret = modem_pipe_close(data->uart_pipe, K_SECONDS(10));
		if (ret == 0) {
			data->uart_pipe_open = false;
		}
		return ret;
	}

	return 0;
}

static int quectel_l76k_pm_action(const struct device *dev, enum pm_device_action action)
{
	int ret = -ENOTSUP;

	quectel_l76k_lock(dev);

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
		ret = quectel_l76k_suspend(dev);
		break;

	case PM_DEVICE_ACTION_RESUME:
		ret = quectel_l76k_resume(dev);
		break;

	case PM_DEVICE_ACTION_TURN_ON:
		ret = quectel_l76k_turn_on(dev);
		break;

	case PM_DEVICE_ACTION_TURN_OFF:
		ret = quectel_l76k_turn_off(dev);
		break;

	default:
		break;
	}

	quectel_l76k_pm_changed(dev);

	quectel_l76k_unlock(dev);
	return ret;
}
#endif /* CONFIG_PM_DEVICE */

static int quectel_l76k_set_fix_rate(const struct device *dev, uint32_t fix_interval_ms)
{
	struct quectel_l76k_data *data = dev->data;
	char cmd[32];
	bool pm_acquired;
	int ret;

	/* L76K supports 1000, 500, 250, 200, 100 ms intervals */
	if (fix_interval_ms != 1000 && fix_interval_ms != 500 && fix_interval_ms != 250 &&
	    fix_interval_ms != 200 && fix_interval_ms != 100) {
		return -EINVAL;
	}

	ret = quectel_l76k_runtime_get(dev, &pm_acquired);
	if (ret < 0) {
		return ret;
	}

	quectel_l76k_lock(dev);

	snprintf(cmd, sizeof(cmd), "PCAS02,%u", fix_interval_ms);
	ret = quectel_l76k_send_pcas_cmd(dev, cmd);
	if (ret == 0) {
		data->fix_rate_ms = fix_interval_ms;
	}

	quectel_l76k_unlock(dev);
	quectel_l76k_runtime_put(dev, pm_acquired);
	return ret;
}

static int quectel_l76k_get_fix_rate(const struct device *dev, uint32_t *fix_interval_ms)
{
	struct quectel_l76k_data *data = dev->data;

	quectel_l76k_lock(dev);
	*fix_interval_ms = data->fix_rate_ms;
	quectel_l76k_unlock(dev);

	return 0;
}

static int quectel_l76k_set_navigation_mode(const struct device *dev,
					    enum gnss_navigation_mode mode)
{
	struct quectel_l76k_data *data = dev->data;
	uint8_t nav_mode;
	char cmd[32];
	bool pm_acquired;
	int ret;

	switch (mode) {
	case GNSS_NAVIGATION_MODE_ZERO_DYNAMICS:
		nav_mode = QUECTEL_L76K_NAV_MODE_STATIC;
		break;
	case GNSS_NAVIGATION_MODE_LOW_DYNAMICS:
		nav_mode = QUECTEL_L76K_NAV_MODE_WALKING;
		break;
	case GNSS_NAVIGATION_MODE_BALANCED_DYNAMICS:
		nav_mode = QUECTEL_L76K_NAV_MODE_PORTABLE;
		break;
	case GNSS_NAVIGATION_MODE_HIGH_DYNAMICS:
		nav_mode = QUECTEL_L76K_NAV_MODE_CAR;
		break;
	default:
		return -EINVAL;
	}

	ret = quectel_l76k_runtime_get(dev, &pm_acquired);
	if (ret < 0) {
		return ret;
	}

	quectel_l76k_lock(dev);

	snprintf(cmd, sizeof(cmd), "PCAS11,%u", nav_mode);
	ret = quectel_l76k_send_pcas_cmd(dev, cmd);
	if (ret == 0) {
		data->navigation_mode = mode;
	}

	quectel_l76k_unlock(dev);
	quectel_l76k_runtime_put(dev, pm_acquired);
	return ret;
}

static int quectel_l76k_get_navigation_mode(const struct device *dev,
					    enum gnss_navigation_mode *mode)
{
	struct quectel_l76k_data *data = dev->data;

	quectel_l76k_lock(dev);
	*mode = data->navigation_mode;
	quectel_l76k_unlock(dev);

	return 0;
}

static int quectel_l76k_set_enabled_systems(const struct device *dev, gnss_systems_t systems)
{
	struct quectel_l76k_data *data = dev->data;
	uint8_t sys_mode;
	char cmd[32];
	int ret;
	gnss_systems_t supported;
	bool pm_acquired;

	supported = GNSS_SYSTEM_GPS | GNSS_SYSTEM_GLONASS | GNSS_SYSTEM_BEIDOU | GNSS_SYSTEM_QZSS;

	/* Check for unsupported systems (ignore QZSS as it's always enabled) */
	if ((systems & ~supported) != 0) {
		return -EINVAL;
	}

	/* Map bitmask to PCAS04 mode */
	bool gps = (systems & GNSS_SYSTEM_GPS) != 0;
	bool glonass = (systems & GNSS_SYSTEM_GLONASS) != 0;
	bool beidou = (systems & GNSS_SYSTEM_BEIDOU) != 0;

	if (gps && beidou && glonass) {
		sys_mode = QUECTEL_L76K_SYS_GPS_BEIDOU_GLONASS;
	} else if (beidou && glonass) {
		sys_mode = QUECTEL_L76K_SYS_BEIDOU_GLONASS;
	} else if (gps && glonass) {
		sys_mode = QUECTEL_L76K_SYS_GPS_GLONASS;
	} else if (gps && beidou) {
		sys_mode = QUECTEL_L76K_SYS_GPS_BEIDOU;
	} else if (glonass) {
		sys_mode = QUECTEL_L76K_SYS_GLONASS;
	} else if (beidou) {
		sys_mode = QUECTEL_L76K_SYS_BEIDOU;
	} else if (gps) {
		sys_mode = QUECTEL_L76K_SYS_GPS;
	} else {
		/* At least one system must be enabled */
		return -EINVAL;
	}

	ret = quectel_l76k_runtime_get(dev, &pm_acquired);
	if (ret < 0) {
		return ret;
	}

	quectel_l76k_lock(dev);

	snprintf(cmd, sizeof(cmd), "PCAS04,%u", sys_mode);

	ret = quectel_l76k_send_pcas_cmd(dev, cmd);
	if (ret == 0) {
		/* QZSS is always enabled */
		data->enabled_systems = systems | GNSS_SYSTEM_QZSS;
	}

	quectel_l76k_unlock(dev);
	quectel_l76k_runtime_put(dev, pm_acquired);
	return ret;
}

static int quectel_l76k_get_enabled_systems(const struct device *dev, gnss_systems_t *systems)
{
	struct quectel_l76k_data *data = dev->data;

	quectel_l76k_lock(dev);
	*systems = data->enabled_systems;
	quectel_l76k_unlock(dev);

	return 0;
}

static int quectel_l76k_get_supported_systems(const struct device *dev, gnss_systems_t *systems)
{
	*systems = GNSS_SYSTEM_GPS | GNSS_SYSTEM_GLONASS | GNSS_SYSTEM_BEIDOU | GNSS_SYSTEM_QZSS;
	return 0;
}

static int quectel_l76k_get_latest_timepulse(const struct device *dev, k_ticks_t *timestamp)
{
	const struct quectel_l76k_config *config = dev->config;
	struct quectel_l76k_data *data = dev->data;
	k_spinlock_key_t key;

	if (!gpio_is_ready_dt(&config->pps_gpio)) {
		return -ENOTSUP;
	}

	key = k_spin_lock(&data->pps_lock);
	if (!data->pps_valid) {
		k_spin_unlock(&data->pps_lock, key);
		return -EAGAIN;
	}

	*timestamp = data->pps_timestamp;
	k_spin_unlock(&data->pps_lock, key);
	return 0;
}

static DEVICE_API(gnss, gnss_api) = {
	.set_fix_rate = quectel_l76k_set_fix_rate,
	.get_fix_rate = quectel_l76k_get_fix_rate,
	.set_navigation_mode = quectel_l76k_set_navigation_mode,
	.get_navigation_mode = quectel_l76k_get_navigation_mode,
	.set_enabled_systems = quectel_l76k_set_enabled_systems,
	.get_enabled_systems = quectel_l76k_get_enabled_systems,
	.get_supported_systems = quectel_l76k_get_supported_systems,
	.get_latest_timepulse = quectel_l76k_get_latest_timepulse,
};

static int quectel_l76k_init_nmea0183_match(const struct device *dev)
{
	struct quectel_l76k_data *data = dev->data;

	const struct gnss_nmea0183_match_config config = {
		.gnss = dev,
#if CONFIG_GNSS_SATELLITES
		.satellites = data->satellites,
		.satellites_size = ARRAY_SIZE(data->satellites),
#endif
	};

	return gnss_nmea0183_match_init(&data->match_data, &config);
}

static void quectel_l76k_init_pipe(const struct device *dev)
{
	const struct quectel_l76k_config *config = dev->config;
	struct quectel_l76k_data *data = dev->data;

	const struct modem_backend_uart_config uart_backend_config = {
		.uart = config->uart,
		.receive_buf = data->uart_backend_receive_buf,
		.receive_buf_size = ARRAY_SIZE(data->uart_backend_receive_buf),
		.transmit_buf = data->uart_backend_transmit_buf,
		.transmit_buf_size = ARRAY_SIZE(data->uart_backend_transmit_buf),
	};

	data->uart_pipe = modem_backend_uart_init(&data->uart_backend, &uart_backend_config);
}

static int quectel_l76k_init_chat(const struct device *dev)
{
	struct quectel_l76k_data *data = dev->data;

	const struct modem_chat_config chat_config = {
		.user_data = data,
		.receive_buf = data->chat_receive_buf,
		.receive_buf_size = ARRAY_SIZE(data->chat_receive_buf),
		.delimiter = data->chat_delimiter,
		.delimiter_size = ARRAY_SIZE(data->chat_delimiter),
		.filter = NULL,
		.filter_size = 0,
		.argv = data->chat_argv,
		.argv_size = ARRAY_SIZE(data->chat_argv),
		.unsol_matches = unsol_matches,
		.unsol_matches_size = ARRAY_SIZE(unsol_matches),
	};

	return modem_chat_init(&data->chat, &chat_config);
}

static int quectel_l76k_init_gpios(const struct device *dev)
{
	const struct quectel_l76k_config *config = dev->config;
	struct quectel_l76k_data *data = dev->data;
	int ret;

	/* Configure RESET GPIO if available - low=reset, high=run */
	if (gpio_is_ready_dt(&config->reset_gpio)) {

		/* Start with RESET asserted (logical 0 = physical low = reset state) */
		ret = gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT_ACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure RESET GPIO: %d", ret);
			return ret;
		}
		LOG_DBG("RESET asserted");

		/* Module is being reset, baudrate needs to be reconfigured */
		data->baudrate_configured = false;

		/* Hold reset for 20ms */
		k_sleep(K_MSEC(20));

		/* Release reset (logical 0 = inactive = physical high = run state) */
		ret = gpio_pin_set_dt(&config->reset_gpio, 0);
		if (ret < 0) {
			LOG_ERR("Failed to release RESET GPIO: %d", ret);
			return ret;
		}

		/* Wait for module to boot */
		k_sleep(K_MSEC(250));
	}

	/* Configure WAKEUP GPIO if available */
	if (gpio_is_ready_dt(&config->wakeup_gpio)) {
		/*
		 * Keep WAKEUP deasserted by default.
		 *
		 * This driver is often placed behind a shared power-domain. Other
		 * peripherals may turn the domain on during boot even when GNSS is
		 * disabled (at the app/settings level). If WAKEUP is asserted on init,
		 * the GNSS module can unintentionally start.
		 *
		 * WAKEUP is asserted as part of PM_DEVICE_ACTION_RESUME when the GNSS
		 * device is explicitly resumed (i.e. actually used).
		 */
		ret = gpio_pin_configure_dt(&config->wakeup_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure WAKEUP GPIO: %d", ret);
			return ret;
		}
	}

	/* Configure PPS GPIO if available */
	ret = quectel_l76k_configure_pps_gpio(dev);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int quectel_l76k_init(const struct device *dev)
{
	const struct quectel_l76k_config *config = dev->config;
	struct quectel_l76k_data *data = dev->data;
	struct uart_config uart_cfg;
	int ret;

	if (!device_is_ready(config->uart)) {
		LOG_ERR("UART device is not ready");
		return -ENODEV;
	}

	ret = uart_config_get(config->uart, &uart_cfg);
	if (ret < 0) {
		LOG_ERR("Failed to get UART config: %d", ret);
		return ret;
	}

	k_sem_init(&data->lock, 1, 1);
	data->uart_pipe_open = false;

	/* Initialize default cached values */
	data->fix_rate_ms = 1000;
	data->navigation_mode = GNSS_NAVIGATION_MODE_BALANCED_DYNAMICS;
	data->enabled_systems = GNSS_SYSTEM_GPS | GNSS_SYSTEM_BEIDOU | GNSS_SYSTEM_QZSS;
	data->target_baudrate = uart_cfg.baudrate;
	data->pps_valid = false;
	data->baudrate_configured = false;

	ret = quectel_l76k_init_nmea0183_match(dev);
	if (ret < 0) {
		return ret;
	}

	quectel_l76k_init_pipe(dev);

	ret = quectel_l76k_init_chat(dev);
	if (ret < 0) {
		return ret;
	}

	ret = quectel_l76k_init_gpios(dev);
	if (ret < 0) {
		return ret;
	}

	quectel_l76k_pm_changed(dev);

#ifdef CONFIG_PM_DEVICE
	/*
	 * Avoid unintended GNSS startup:
	 *
	 * GNSS may share a switchable rail with other peripherals. When that rail
	 * is turned on, the power-domain framework can issue PM_DEVICE_ACTION_TURN_ON
	 * to all children. If the driver brings GNSS up during init, the module can
	 * start even when GNSS is disabled at the app/settings level.
	 *
	 * With runtime-PM, initialize into a low-power PM state and only assert
	 * WAKEUP on PM_DEVICE_ACTION_RESUME.
	 */
#if defined(CONFIG_PM_DEVICE_RUNTIME)
	if (pm_device_is_powered(dev)) {
		pm_device_init_suspended(dev);
	} else {
		pm_device_init_off(dev);
	}
	return pm_device_runtime_enable(dev);
#else
	/* Without runtime PM, bring the module up immediately. */
	if (pm_device_is_powered(dev)) {
		ret = quectel_l76k_resume(dev);
		if (ret < 0) {
			return ret;
		}
	} else {
		pm_device_init_off(dev);
	}
	return 0;
#endif /* CONFIG_PM_DEVICE_RUNTIME */
#else
	/* When PM is disabled, bring the module up immediately. */
	ret = quectel_l76k_resume(dev);
	if (ret < 0) {
		return ret;
	}
	return 0;
#endif /* CONFIG_PM_DEVICE */
}

#define L76K_INST_NAME(inst, name) _CONCAT(_CONCAT(_CONCAT(name, _), DT_DRV_COMPAT), inst)

#define L76K_DEVICE(inst)                                                                          \
	static const struct quectel_l76k_config L76K_INST_NAME(inst, config) = {                   \
		.uart = DEVICE_DT_GET(DT_INST_BUS(inst)),                                          \
		.pps_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, pps_gpios, {0}),                        \
		.reset_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {0}),                    \
		.wakeup_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, wakeup_gpios, {0}),                  \
	};                                                                                         \
                                                                                                   \
	static struct quectel_l76k_data L76K_INST_NAME(inst, data) = {                             \
		.chat_delimiter = {'\r', '\n'},                                                    \
	};                                                                                         \
                                                                                                   \
	PM_DEVICE_DT_INST_DEFINE(inst, quectel_l76k_pm_action);                                    \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, quectel_l76k_init, PM_DEVICE_DT_INST_GET(inst),                \
			      &L76K_INST_NAME(inst, data), &L76K_INST_NAME(inst, config),          \
			      POST_KERNEL, CONFIG_GNSS_INIT_PRIORITY, &gnss_api);

#define DT_DRV_COMPAT quectel_l76k
DT_INST_FOREACH_STATUS_OKAY(L76K_DEVICE)
