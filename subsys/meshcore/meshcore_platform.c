// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "meshcore/platform.h"
#include "meshcore_prvi.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <meshcore/meshcore.h>
#include <contact/contact.h>
#if defined(CONFIG_MBS_GNSS)
#include <gnss/gnss.h>
#endif
#if defined(CONFIG_MBS_POWER)
#include <power/power.h>
#endif
#include <radio/radio.h>
#include <clock/timestamp.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>
#include <psa/crypto.h>

#include "meshcore_cayenne_lpp_compat.h"

static bool mbs_meshcore_radio_channel_active_get(void);

/*
 * FoBE host-platform / hardware MeshCore primitive implementation.
 *
 * This file owns radio bridging and device-backed telemetry gathering.
 * Platform-specific time and crypto primitives are implemented in this
 * Meshbus-private host adapter surface.
 */

#define MESHCORE_PLATFORM_TELEM_CHANNEL_SELF 1U

int meshcore_platform_telemetry_node_get(
	const meshcore_platform_request_source_t *requester,
	uint8_t permission_mask, meshcore_platform_telemetry_payload_t *out)
{
	int rc = 0;
	struct cayenne_lpp_writer lpp = { 0 };

	ARG_UNUSED(requester);

	if (out == NULL) {
		return -EINVAL;
	}

	memset(out, 0, sizeof(*out));
	cayenne_lpp_init(&lpp, out->payload, sizeof(out->payload));

	if ((permission_mask & MESHCORE_TELEM_PERM_BASE) != 0U) {
#if defined(CONFIG_MBS_POWER)
		uint16_t voltage_mv = 0U;

		rc = mbs_power_fuel_gauge_get(&voltage_mv, NULL, NULL);
		if (rc == 0) {
			rc = cayenne_lpp_add_voltage(
				&lpp, MESHCORE_PLATFORM_TELEM_CHANNEL_SELF,
				(float)voltage_mv / 1000.0f);
		} else {
			rc = 0;
		}
#else
		rc = 0;
#endif
	}

	if (rc == 0 &&
	    (permission_mask & MESHCORE_TELEM_PERM_LOCATION) != 0U) {
		mbs_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
		float latitude = 0.0f;
		float longitude = 0.0f;
		int cfg_rc = mbs_meshcore_config_get(&cfg);
		bool has_location = cfg_rc == 0;

		if (has_location) {
			latitude = (float)cfg.latitude / 1000000.0f;
			longitude = (float)cfg.longitude / 1000000.0f;
		}

#if defined(CONFIG_MBS_GNSS)
		struct navigation_data nav = { 0 };
		int gnss_rc;

		gnss_rc = mbs_gnss_position_get(&nav);
		if (gnss_rc == 0) {
			latitude = (float)nav.latitude / 1000000.0f;
			longitude = (float)nav.longitude / 1000000.0f;
			has_location = true;
		}
#endif

		if (!has_location) {
			return cfg_rc != 0 ? cfg_rc : -ENOENT;
		}

		rc = cayenne_lpp_add_gps(&lpp, MESHCORE_PLATFORM_TELEM_CHANNEL_SELF,
					 latitude, longitude, 0.0f);
		if (rc == 0) {
			out->has_latitude = true;
			out->latitude = (int32_t)(latitude * 1000000.0f);
			out->has_longitude = true;
			out->longitude = (int32_t)(longitude * 1000000.0f);
		}
	}

	/*
	 * TODO: map meshbus telemetry bindings into Cayenne LPP when environment
	 * payload format is finalized for meshcore-C.
	 */

	if (rc == -ENOSPC) {
		rc = 0;
	}
	if (rc != 0) {
		return rc;
	}

	out->payload_len = (uint8_t)MIN(cayenne_lpp_size(&lpp), sizeof(out->payload));
	return 0;
}

void meshcore_platform_radio_begin(void)
{
}

uint32_t meshcore_platform_radio_airtime(size_t len)
{
	return mbs_radio_airtime((uint16_t)len);
}

float meshcore_platform_radio_packet_score(int8_t snr_q4, size_t len)
{
	return mbs_radio_packet_score(((float)snr_q4) / 4.0f, (uint16_t)len);
}

bool meshcore_platform_radio_in_rx_mode_get(void)
{
	return mbs_radio_state_get() == MBS_RADIO_STATE_RECEIVE;
}

bool meshcore_platform_radio_receiving_get(void)
{
	return mbs_meshcore_radio_channel_active_get();
}

bool mbs_meshcore_radio_channel_active_get(void)
{
	return mbs_radio_channel_activity();
}

void meshcore_platform_radio_noise_floor_calibrate(int threshold)
{
	mbs_radio_noise_calibrate((int16_t)threshold);
}

void meshcore_platform_radio_agc_reset(void)
{
	mbs_radio_agc_reset();
}

int meshcore_platform_radio_packet_send(const uint8_t *data, size_t len)
{
	struct mbs_radio_publish_event event = { 0 };
	int rc;

	if (data == NULL || len == 0U || len > sizeof(event.data)) {
		return 0;
	}

	if (!mbs_meshcore_radio_tx_begin()) {
		return 0;
	}

	memcpy(event.data, data, len);
	event.len = (uint16_t)len;
	rc = zbus_chan_pub(&mbs_radio_publish_chan, &event, K_NO_WAIT);
	if (rc != 0) {
		mbs_meshcore_radio_tx_abort();
		return 0;
	}

	return 1;
}

#define MESHCORE_PLATFORM_SHA256_SIZE 32U
#define MESHCORE_PLATFORM_AES128_KEY_SIZE 16U
#define MESHCORE_PLATFORM_AES128_BLOCK_SIZE 16U

static atomic_t psa_initialized = ATOMIC_INIT(0);

static void meshcore_platform_psa_init_once(void)
{
	if (atomic_cas(&psa_initialized, 0, 1)) {
		(void)psa_crypto_init();
	}
}

unsigned long meshcore_platform_millis_get(void)
{
	return (unsigned long)k_uptime_get_32();
}

uint32_t meshcore_platform_rtc_get_current_time(void)
{
	uint32_t timestamp;

	if (mbs_clock_timestamp_s_get(&timestamp) != 0) {
		return 1U;
	}

	return timestamp;
}

void meshcore_platform_rng_random(uint8_t *dest, size_t size)
{
	uint32_t value;

	if (dest == NULL) {
		return;
	}

	meshcore_platform_psa_init_once();
	if (psa_generate_random(dest, size) == PSA_SUCCESS) {
		return;
	}

	value = (uint32_t)k_cycle_get_32() ^ (uint32_t)k_uptime_get_32();
	for (size_t i = 0; i < size; i++) {
		value = value * 1664525U + 1013904223U + (uint32_t)i;
		dest[i] = (uint8_t)(value >> 24);
	}
}

bool meshcore_platform_crypto_sha256(uint8_t *hash, size_t hash_len, const uint8_t *msg,
			 int msg_len)
{
	uint8_t full_hash[MESHCORE_PLATFORM_SHA256_SIZE];
	size_t full_len = 0U;
	psa_status_t status;

	if (hash == NULL || msg == NULL || msg_len < 0 ||
	    hash_len > sizeof(full_hash)) {
		return false;
	}

	meshcore_platform_psa_init_once();
	status = psa_hash_compute(PSA_ALG_SHA_256, msg, (size_t)msg_len, full_hash,
				  sizeof(full_hash), &full_len);
	if (status != PSA_SUCCESS || full_len < hash_len) {
		return false;
	}

	memcpy(hash, full_hash, hash_len);
	memset(full_hash, 0, sizeof(full_hash));
	return true;
}

bool meshcore_platform_crypto_sha256_two_fragments(uint8_t *hash, size_t hash_len,
				       const uint8_t *frag1, int frag1_len,
				       const uint8_t *frag2, int frag2_len)
{
	uint8_t full_hash[MESHCORE_PLATFORM_SHA256_SIZE];
	size_t full_len = 0U;
	psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
	psa_status_t status;

	if (hash == NULL || frag1 == NULL || frag2 == NULL ||
	    frag1_len < 0 || frag2_len < 0 ||
	    hash_len > sizeof(full_hash)) {
		return false;
	}

	meshcore_platform_psa_init_once();
	status = psa_hash_setup(&op, PSA_ALG_SHA_256);
	if (status != PSA_SUCCESS) {
		return false;
	}

	status = psa_hash_update(&op, frag1, (size_t)frag1_len);
	if (status == PSA_SUCCESS) {
		status = psa_hash_update(&op, frag2, (size_t)frag2_len);
	}
	if (status == PSA_SUCCESS) {
		status = psa_hash_finish(&op, full_hash, sizeof(full_hash), &full_len);
	}

	(void)psa_hash_abort(&op);
	if (status != PSA_SUCCESS || full_len < hash_len) {
		memset(full_hash, 0, sizeof(full_hash));
		return false;
	}

	memcpy(hash, full_hash, hash_len);
	memset(full_hash, 0, sizeof(full_hash));
	return true;
}

static bool meshcore_platform_aes128_crypt_block(const uint8_t *key_bytes,
						 uint8_t *dest,
						 const uint8_t *src,
						 bool encrypt)
{
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key = PSA_KEY_ID_NULL;
	psa_status_t status;
	size_t out_len = 0U;

	if (key_bytes == NULL || dest == NULL || src == NULL) {
		return false;
	}

	meshcore_platform_psa_init_once();
	psa_set_key_usage_flags(
		&attributes,
		encrypt ? PSA_KEY_USAGE_ENCRYPT : PSA_KEY_USAGE_DECRYPT);
	psa_set_key_algorithm(&attributes, PSA_ALG_ECB_NO_PADDING);
	psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
	psa_set_key_bits(&attributes, MESHCORE_PLATFORM_AES128_KEY_SIZE * 8U);

	status = psa_import_key(&attributes, key_bytes,
				MESHCORE_PLATFORM_AES128_KEY_SIZE, &key);
	if (status != PSA_SUCCESS) {
		goto out;
	}

	if (encrypt) {
		status = psa_cipher_encrypt(
			key, PSA_ALG_ECB_NO_PADDING, src,
			MESHCORE_PLATFORM_AES128_BLOCK_SIZE, dest,
			MESHCORE_PLATFORM_AES128_BLOCK_SIZE, &out_len);
	} else {
		status = psa_cipher_decrypt(
			key, PSA_ALG_ECB_NO_PADDING, src,
			MESHCORE_PLATFORM_AES128_BLOCK_SIZE, dest,
			MESHCORE_PLATFORM_AES128_BLOCK_SIZE, &out_len);
	}

out:
	if (key != PSA_KEY_ID_NULL) {
		psa_destroy_key(key);
	}
	psa_reset_key_attributes(&attributes);
	return status == PSA_SUCCESS &&
	       out_len == MESHCORE_PLATFORM_AES128_BLOCK_SIZE;
}

bool meshcore_platform_crypto_aes128_encrypt_block(const uint8_t *key, uint8_t *dest,
				       const uint8_t *src)
{
	return meshcore_platform_aes128_crypt_block(key, dest, src, true);
}

bool meshcore_platform_crypto_aes128_decrypt_block(const uint8_t *key, uint8_t *dest,
				       const uint8_t *src)
{
	return meshcore_platform_aes128_crypt_block(key, dest, src, false);
}

bool meshcore_platform_crypto_hmac_sha256(uint8_t *mac, size_t mac_len, const uint8_t *key,
			      size_t key_len, const uint8_t *msg,
			      size_t msg_len)
{
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key_id = PSA_KEY_ID_NULL;
	psa_status_t status;
	size_t full_len = 0U;
	uint8_t full_mac[MESHCORE_PLATFORM_SHA256_SIZE];

	if (mac == NULL || key == NULL || msg == NULL ||
	    mac_len > sizeof(full_mac)) {
		return false;
	}

	meshcore_platform_psa_init_once();
	psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
	psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
	psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
	psa_set_key_bits(&attributes, key_len * 8U);

	status = psa_import_key(&attributes, key, key_len, &key_id);
	if (status != PSA_SUCCESS) {
		goto out;
	}

	status = psa_mac_compute(key_id, PSA_ALG_HMAC(PSA_ALG_SHA_256), msg,
				 msg_len, full_mac, sizeof(full_mac), &full_len);
	if (status == PSA_SUCCESS && full_len >= mac_len) {
		memcpy(mac, full_mac, mac_len);
	}

out:
	if (key_id != PSA_KEY_ID_NULL) {
		psa_destroy_key(key_id);
	}
	psa_reset_key_attributes(&attributes);
	memset(full_mac, 0, sizeof(full_mac));
	return status == PSA_SUCCESS && full_len >= mac_len;
}
