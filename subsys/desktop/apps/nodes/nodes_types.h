/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/autoconf.h>
#include <contact/contact.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NODES_NAME_MAX 32U
#define NODES_ID_FULL_MAX 65U
#define NODES_PUBLIC_KEY_SIZE 32U
#define NODES_PUBLIC_KEY_PREFIX_SIZE CONFIG_MBS_CONTACT_PREFIX_BYTES
#define NODES_PUBLIC_KEY_HEX_MAX (NODES_PUBLIC_KEY_SIZE * 2U + 1U)
#define NODES_PREFIX_HEX_MAX (NODES_PUBLIC_KEY_PREFIX_SIZE * 2U + 1U)
#define NODES_OUT_PATH_MAX 192U
#define NODES_OUT_PATH_SNR_MAX 192U

#define NODES_VALUE_UNKNOWN_U8 0xFFU
#define NODES_VALUE_UNKNOWN_S16 INT16_MIN

enum zui_nodes_trace_state {
	ZUI_NODES_TRACE_IDLE = 0,
	ZUI_NODES_TRACE_WAITING = 1,
	ZUI_NODES_TRACE_READY = 2,
};

struct zui_nodes_entry {
	char id_full[NODES_ID_FULL_MAX];
	char id_short[NODES_PREFIX_HEX_MAX];
	char name[NODES_NAME_MAX];
	uint8_t public_key_prefix[NODES_PUBLIC_KEY_PREFIX_SIZE];
	mbs_contact_role role;
	char out_path[NODES_OUT_PATH_MAX];
	char out_path_with_snr[NODES_OUT_PATH_SNR_MAX];
	uint8_t battery_pct;
	int16_t rssi;
	int8_t snr;
	uint32_t first_seen_ms;
	uint32_t last_seen_ms;
	int32_t latitude_e7;
	int32_t longitude_e7;
	char public_key[NODES_PUBLIC_KEY_HEX_MAX];
	bool telemetry_valid;
	uint32_t telemetry_ts_ms;
	int16_t telemetry_temp_c_x10;
	uint16_t telemetry_humidity_x10;
	uint32_t telemetry_pressure_pa;
	enum zui_nodes_trace_state trace_state;
	uint8_t trace_req_count;
	uint32_t trace_last_req_ms;
	uint32_t trace_last_reply_ms;
};

#ifdef __cplusplus
}
#endif
