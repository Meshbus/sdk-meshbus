/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <radio/radio.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include "mbs_shell_internal.h"

#define RADIO_HELP_ROOT         SHELL_HELP("Radio control and configuration", NULL)
#define RADIO_HELP_STATUS       SHELL_HELP("Show radio runtime status", NULL)
#define RADIO_HELP_CALIBRATE    SHELL_HELP("Trigger noise-floor calibration", "[threshold_dB]")
#define RADIO_HELP_ENABLE       SHELL_HELP("Enable radio and resume receive", NULL)
#define RADIO_HELP_DISABLE      SHELL_HELP("Disable radio and stop receive", NULL)
#define RADIO_HELP_AIRTIME      SHELL_HELP("Calculate Transmit airtime", "<bytes>")
#define RADIO_HELP_SCORE        SHELL_HELP("Calculate packet score", "<snr_dB> <len>")
#define RADIO_HELP_AGC          SHELL_HELP("Reset AGC state", NULL)
#define RADIO_HELP_SEND         SHELL_HELP("Send packet", "<\"text\"|hex>")
#define RADIO_HELP_CW           SHELL_HELP("Transmit continuous wave (test only)", "<frequency_hz> <tx_power_dbm> <duration_s>")
#define RADIO_HELP_CONFIG       SHELL_HELP("Radio configuration", NULL)
#define RADIO_HELP_CONFIG_GET   SHELL_HELP("Show current", NULL)
#define RADIO_HELP_CONFIG_RESET SHELL_HELP("Reset default", NULL)
#define RADIO_HELP_CONFIG_SET                                                                      \
	SHELL_HELP("Set full radio config",                                                        \
		   "<enabled> <frequency> <bandwidth> <spread_factor> <coding_rate> "              \
		   "<preamble_length> <tx_power> <receive_only> <rx_boosted> <crc> "               \
		   "<duty_cycle> <duty_cycle_rx_time> <duty_cycle_sleep_time>")

static const char *radio_state_str(enum mbs_radio_state state)
{
	switch (state) {
	case MBS_RADIO_STATE_IDLE:
		return "idle";
	case MBS_RADIO_STATE_RECEIVE:
		return "receive";
	case MBS_RADIO_STATE_TRANSMIT:
		return "transmit";
	default:
		return "unknown";
	}
}

static int parse_payload_len(const char *arg, uint16_t *len)
{
	uint32_t value;
	int ret = mbs_shell_parse_u32_arg(arg, &value);

	if ((ret != 0) || (value > MBS_RADIO_MAX_PAYLOAD)) {
		return -EINVAL;
	}

	*len = (uint16_t)value;
	return 0;
}

static int cmd_radio_send(const struct shell *sh, size_t argc, char **argv)
{
	const char *input;
	size_t input_len;
	uint16_t data_len;
	size_t n;
	struct mbs_radio_publish_event event = {0};

	if ((argc != 2U) || (argv[1] == NULL)) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}

	input = argv[1];
	input_len = strlen(input);
	if (input_len == 0U) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}

	if ((input_len >= 2U) && (input[0] == '"') && (input[input_len - 1U] == '"')) {
		data_len = (uint16_t)(input_len - 2U);
		if ((data_len == 0U) || (data_len > MBS_RADIO_MAX_PAYLOAD)) {
			mbs_shell_invalid(sh);
			return -EINVAL;
		}

		memcpy(event.data, &input[1], data_len);
	} else {
		if ((input_len % 2U) != 0U) {
			mbs_shell_invalid(sh);
			return -EINVAL;
		}

		data_len = (uint16_t)(input_len / 2U);
		if ((data_len == 0U) || (data_len > MBS_RADIO_MAX_PAYLOAD)) {
			mbs_shell_invalid(sh);
			return -EINVAL;
		}

		n = hex2bin(input, input_len, event.data, data_len);
		if (n != data_len) {
			mbs_shell_invalid(sh);
			return -EINVAL;
		}
	}

	event.len = data_len;

	int ret = zbus_chan_pub(&mbs_radio_publish_chan, &event, K_NO_WAIT);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	return 0;
}

static int cmd_radio_calibrate(const struct shell *sh, size_t argc, char **argv)
{
	int16_t threshold = 14;

	if ((argc == 2U) && (mbs_shell_parse_i16_arg(argv[1], &threshold) != 0)) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}

	mbs_radio_noise_calibrate(threshold);
	return 0;
}

static int cmd_radio_airtime(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t len;

	ARG_UNUSED(argc);

	if (parse_payload_len(argv[1], &len) != 0) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}

	shell_print(sh, "Air time: %u ms", mbs_radio_airtime(len));

	return 0;
}

static int cmd_radio_score(const struct shell *sh, size_t argc, char **argv)
{
	float snr;
	float score;
	uint16_t len;

	ARG_UNUSED(argc);

	if ((mbs_shell_parse_float_arg(argv[1], &snr) != 0) ||
	    (parse_payload_len(argv[2], &len) != 0)) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}

	score = mbs_radio_packet_score(snr, len);
	shell_print(sh, "Packet score: %u%%", (unsigned int)(score * 100.0f));

	return 0;
}

static int cmd_radio_agc(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	mbs_radio_agc_reset();
	return 0;
}

static int cmd_radio_enable(const struct shell *sh, size_t argc, char **argv)
{
	mbs_radio_config cfg;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = mbs_radio_config_get(&cfg);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	if (!cfg.enabled) {
		cfg.enabled = true;
		ret = mbs_radio_config_set(&cfg);
		if (ret != 0) {
			mbs_shell_error(sh, ret);
			return ret;
		}
	}

	return 0;
}

static int cmd_radio_disable(const struct shell *sh, size_t argc, char **argv)
{
	mbs_radio_config cfg;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = mbs_radio_config_get(&cfg);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	if (cfg.enabled) {
		cfg.enabled = false;
		ret = mbs_radio_config_set(&cfg);
		if (ret != 0) {
			mbs_shell_error(sh, ret);
			return ret;
		}
	}

	return 0;
}

static int cmd_radio_cw(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t frequency;
	int16_t tx_power_i16;
	uint32_t duration;

	ARG_UNUSED(argc);

	if ((mbs_shell_parse_u32_arg(argv[1], &frequency) != 0) ||
	    (mbs_shell_parse_i16_arg(argv[2], &tx_power_i16) != 0) ||
	    (mbs_shell_parse_u32_arg(argv[3], &duration) != 0)) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}

	if (tx_power_i16 < INT8_MIN || tx_power_i16 > INT8_MAX || duration > UINT16_MAX) {
		mbs_shell_invalid(sh);
		return -EINVAL;
	}

	struct mbs_radio_cw_request_event event = {
		.frequency_hz = frequency,
		.duration_s = (uint16_t)duration,
		.tx_power_dbm = (int8_t)tx_power_i16,
	};
	int ret = zbus_chan_pub(&mbs_radio_cw_chan, &event, K_NO_WAIT);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	shell_print(sh, "CW started");
	return 0;
}

static int cmd_radio_config_get(const struct shell *sh, size_t argc, char **argv)
{
	mbs_radio_config cfg;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = mbs_radio_config_get(&cfg);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	shell_print(sh, "Settings:");
	shell_print(sh, "  enabled:       %s", cfg.enabled ? "yes" : "no");
	shell_print(sh, "  frequency:     %llu Hz", (unsigned long long)cfg.frequency);
	shell_print(sh, "  bandwidth:     %u Hz", cfg.bandwidth);
	shell_print(sh, "  spread_factor: %u", cfg.spread_factor);
	shell_print(sh, "  coding_rate:   4/%u", cfg.coding_rate);
	shell_print(sh, "  preamble:      %u sym", cfg.preamble_length);
	shell_print(sh, "  tx_power:      %d dBm", cfg.tx_power);
	shell_print(sh, "  receive_only:  %s", cfg.receive_only ? "yes" : "no");
	shell_print(sh, "  rx_boosted:    %s", cfg.rx_boosted ? "yes" : "no");
	shell_print(sh, "  crc:           %s", cfg.crc ? "yes" : "no");
	shell_print(sh, "  duty_cycle:    %s", cfg.duty_cycle ? "yes" : "no");
	shell_print(sh, "  duty_rx_time:  %u ms", cfg.duty_cycle_rx_time);
	shell_print(sh, "  duty_slp_time: %u ms", cfg.duty_cycle_sleep_time);

	return 0;
}

static int cmd_radio_config_set(const struct shell *sh, size_t argc, char **argv)
{
	mbs_radio_config cfg;
	uint32_t spread_factor;
	uint32_t coding_rate;
	int32_t tx_power;
	int ret;

	ARG_UNUSED(argc);

	ret = mbs_radio_config_get(&cfg);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	ret = mbs_shell_parse_bool_arg(argv[1], &cfg.enabled);
	ret |= mbs_shell_parse_u64_arg(argv[2], &cfg.frequency);
	ret |= mbs_shell_parse_u32_arg(argv[3], &cfg.bandwidth);
	ret |= mbs_shell_parse_u32_arg(argv[4], &spread_factor);
	ret |= mbs_shell_parse_u32_arg(argv[5], &coding_rate);
	ret |= mbs_shell_parse_u32_arg(argv[6], &cfg.preamble_length);
	ret |= mbs_shell_parse_i32_arg(argv[7], &tx_power);
	ret |= mbs_shell_parse_bool_arg(argv[8], &cfg.receive_only);
	ret |= mbs_shell_parse_bool_arg(argv[9], &cfg.rx_boosted);
	ret |= mbs_shell_parse_bool_arg(argv[10], &cfg.crc);
	ret |= mbs_shell_parse_bool_arg(argv[11], &cfg.duty_cycle);
	ret |= mbs_shell_parse_u32_arg(argv[12], &cfg.duty_cycle_rx_time);
	ret |= mbs_shell_parse_u32_arg(argv[13], &cfg.duty_cycle_sleep_time);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	cfg.spread_factor = (uint8_t)spread_factor;
	cfg.coding_rate = (uint8_t)coding_rate;
	cfg.tx_power = tx_power;

	ret = mbs_radio_config_set(&cfg);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	return cmd_radio_config_get(sh, 0, NULL);
}

static int cmd_radio_config_reset(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = mbs_radio_config_reset();
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	return cmd_radio_config_get(sh, 0, NULL);
}

static int cmd_radio_status(const struct shell *sh, size_t argc, char **argv)
{
	struct mbs_radio_status status;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = mbs_radio_status_get(&status);
	if (ret != 0) {
		mbs_shell_error(sh, ret);
		return ret;
	}

	shell_print(sh, "Status:");
	shell_print(sh, "  Modem state:   %s", radio_state_str(status.state));
	shell_print(sh, "  Receiving:     %s", status.receiving ? "yes" : "no");

	cmd_radio_config_get(sh, 0, NULL);

	shell_print(sh, "Signal:");
	shell_print(sh, "  RSSI:          %d dBm", status.last_rssi_dbm);
	shell_print(sh, "  SNR:           %d.%02u dB", status.last_snr_q4 / 4,
		    (status.last_snr_q4 >= 0 ? status.last_snr_q4 : -status.last_snr_q4) %
			    4 * 25);
	shell_print(sh, "  Noise Floor:   %d dBm", status.noise_floor_dbm);

	if (status.has_rssi_inst_dbm) {
		shell_print(sh, "  RSSI Inst:  %d dBm", status.rssi_inst_dbm);
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	mbs_radio_config_subcmds,
	SHELL_CMD_ARG(get, NULL, RADIO_HELP_CONFIG_GET, cmd_radio_config_get, 1, 0),
	SHELL_CMD_ARG(set, NULL, RADIO_HELP_CONFIG_SET, cmd_radio_config_set, 14, 0),
	SHELL_CMD_ARG(reset, NULL, RADIO_HELP_CONFIG_RESET, cmd_radio_config_reset, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_SET_CREATE(mbs_radio_subcmds, (meshbus, radio));

SHELL_SUBCMD_ADD((meshbus, radio), status, NULL, RADIO_HELP_STATUS, cmd_radio_status, 1, 0);
SHELL_SUBCMD_ADD((meshbus, radio), config, &mbs_radio_config_subcmds, RADIO_HELP_CONFIG, NULL,
		 0, 0);
SHELL_SUBCMD_ADD((meshbus, radio), calibrate, NULL, RADIO_HELP_CALIBRATE, cmd_radio_calibrate, 1,
		 1);
SHELL_SUBCMD_ADD((meshbus, radio), enable, NULL, RADIO_HELP_ENABLE, cmd_radio_enable, 1, 0);
SHELL_SUBCMD_ADD((meshbus, radio), disable, NULL, RADIO_HELP_DISABLE, cmd_radio_disable, 1, 0);
SHELL_SUBCMD_ADD((meshbus, radio), airtime, NULL, RADIO_HELP_AIRTIME, cmd_radio_airtime, 2, 0);
SHELL_SUBCMD_ADD((meshbus, radio), score, NULL, RADIO_HELP_SCORE, cmd_radio_score, 3, 0);
SHELL_SUBCMD_ADD((meshbus, radio), agc, NULL, RADIO_HELP_AGC, cmd_radio_agc, 1, 0);
SHELL_SUBCMD_ADD((meshbus, radio), send, NULL, RADIO_HELP_SEND, cmd_radio_send, 1,
		 SHELL_OPT_ARG_RAW);
SHELL_SUBCMD_ADD((meshbus, radio), cw, NULL, RADIO_HELP_CW, cmd_radio_cw, 4, 0);

SHELL_SUBCMD_ADD((meshbus), radio, &mbs_radio_subcmds, RADIO_HELP_ROOT, NULL, 0, 0);
