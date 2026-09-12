/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Meshbus Contact API
 *
 * This module manages persisted remote MeshCore contacts and contact-directed
 * request events.
 */

#ifndef ZEPHYR_INCLUDE_MBS_CONTACT_H_
#define ZEPHYR_INCLUDE_MBS_CONTACT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#include "meshbus/contact.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Contact record (maps to meshbus_Contact). */
typedef meshbus_Contact mbs_contact;

/** @brief Contact role (Contact-owned protobuf enum). */
typedef meshbus_ContactRole mbs_contact_role;

/** @name Contact roles
 *  @{
 */
#define MBS_CONTACT_ROLE_NONE     meshbus_ContactRole_CONTACT_ROLE_NONE
#define MBS_CONTACT_ROLE_CHAT     meshbus_ContactRole_CONTACT_ROLE_CHAT
#define MBS_CONTACT_ROLE_REPEATER meshbus_ContactRole_CONTACT_ROLE_REPEATER
#define MBS_CONTACT_ROLE_ROOM     meshbus_ContactRole_CONTACT_ROLE_ROOM
#define MBS_CONTACT_ROLE_SENSOR   meshbus_ContactRole_CONTACT_ROLE_SENSOR
/** @} */

#define MBS_CONTACT_PUBLIC_KEY_SIZE 32U
#define MBS_CONTACT_OUTPATH_MAX_LEN 64U
#define MBS_CONTACT_PATH_HASH_SIZE_MAX 3U
#define MBS_CONTACT_TELEMETRY_PAYLOAD_MAX_LEN 230U
#define MBS_CONTACT_BINARY_REQUEST_PAYLOAD_MAX_LEN 163U
#define MBS_CONTACT_BINARY_RESPONSE_PAYLOAD_MAX_LEN 163U
#define MBS_CONTACT_NAME_MAX_LEN 32U
#define MBS_CONTACT_ALIAS_MAX_LEN 32U
/** Stored target password bounds for Contact.management_secret, in ASCII bytes. */
#define MBS_CONTACT_MANAGEMENT_SECRET_MIN_LEN 8U
#define MBS_CONTACT_MANAGEMENT_SECRET_MAX_LEN 16U
/**
 * Generated storage reserved for @c Contact.management_secret in the public
 * @c mbs_contact ABI. This is a layout capacity, not an accepted password
 * length; keep it stable for existing LLEXT consumers.
 */
#define MBS_CONTACT_MANAGEMENT_SECRET_ABI_CAPACITY 64U

BUILD_ASSERT(sizeof(((mbs_contact *)0)->management_secret.bytes) ==
		     MBS_CONTACT_MANAGEMENT_SECRET_ABI_CAPACITY,
	     "mbs_contact management-secret ABI capacity changed");
BUILD_ASSERT(MBS_CONTACT_MANAGEMENT_SECRET_MAX_LEN <=
		     MBS_CONTACT_MANAGEMENT_SECRET_ABI_CAPACITY,
	     "management password policy exceeds its ABI storage capacity");
#if defined(CONFIG_MBS_CONTACT_PREFIX_BYTES)
#define MBS_CONTACT_PREFIX_BYTES CONFIG_MBS_CONTACT_PREFIX_BYTES
#else
#define MBS_CONTACT_PREFIX_BYTES 4U
#endif
/* Packet::writeTo() returns uint8_t length, max 255 bytes. */
#define MBS_CONTACT_ADVERT_RAW_MAX_LEN 255U

/** @name Contact flags bit definitions
 *  @{
 */
#define MBS_CONTACT_FLAG_FAVORITE              BIT(0)
#define MBS_CONTACT_FLAG_TELEMETRY_BASE        BIT(1)
#define MBS_CONTACT_FLAG_TELEMETRY_LOCATION    BIT(2)
#define MBS_CONTACT_FLAG_TELEMETRY_ENVIRONMENT BIT(3)
/** @} */

/** @name Contact auto-add policy bit definitions
 *  @{
 */
#define MBS_CONTACT_ADD_FILTER_OVERWRITE_OLDEST 0b00000001U
#define MBS_CONTACT_ADD_FILTER_CHAT             0b00000010U
#define MBS_CONTACT_ADD_FILTER_REPEATER         0b00000100U
#define MBS_CONTACT_ADD_FILTER_ROOM             0b00001000U
#define MBS_CONTACT_ADD_FILTER_SENSOR           0b00010000U
#define MBS_CONTACT_ADD_FILTER_MANUAL_MODE      0b00100000U
/** @} */

ZBUS_CHAN_DECLARE(mbs_contact_share_request_chan);
ZBUS_CHAN_DECLARE(mbs_contact_discover_path_request_chan);
ZBUS_CHAN_DECLARE(mbs_contact_trace_path_request_chan);
ZBUS_CHAN_DECLARE(mbs_contact_telemetry_request_chan);
ZBUS_CHAN_DECLARE(mbs_contact_binary_request_chan);
ZBUS_CHAN_DECLARE(mbs_contact_advert_response_chan);
ZBUS_CHAN_DECLARE(mbs_contact_advert_chan);
ZBUS_CHAN_DECLARE(mbs_contact_store_change_chan);
ZBUS_CHAN_DECLARE(mbs_contact_discover_response_chan);
ZBUS_CHAN_DECLARE(mbs_contact_path_response_chan);
ZBUS_CHAN_DECLARE(mbs_contact_trace_path_response_chan);
ZBUS_CHAN_DECLARE(mbs_contact_telemetry_response_chan);
ZBUS_CHAN_DECLARE(mbs_contact_binary_response_chan);

/** @brief Request replay of a cached contact advert. */
typedef struct mbs_contact_share_request_event {
	uint8_t key_prefix[MBS_CONTACT_PREFIX_BYTES];
	uint8_t raw_advert_len;
	uint8_t raw_advert[MBS_CONTACT_ADVERT_RAW_MAX_LEN];
} mbs_contact_share_request_event;

/** @brief Contact discover-path request event. */
typedef struct mbs_contact_discover_path_request_event {
	uint8_t key_prefix[MBS_CONTACT_PREFIX_BYTES];
	uint32_t tag;
} mbs_contact_discover_path_request_event;

/** @brief Contact trace-path request event. */
typedef struct mbs_contact_trace_path_request_event {
	uint8_t key_prefix[MBS_CONTACT_PREFIX_BYTES];
	uint32_t tag;
} mbs_contact_trace_path_request_event;

/** @brief Contact telemetry request event. */
typedef struct mbs_contact_telemetry_request_event {
	uint8_t key_prefix[MBS_CONTACT_PREFIX_BYTES];
	uint32_t tag;
} mbs_contact_telemetry_request_event;

/** @brief Contact binary request event. */
typedef struct mbs_contact_binary_request_event {
	uint8_t key_prefix[MBS_CONTACT_PREFIX_BYTES];
	uint32_t tag;
	uint8_t payload_len;
	uint8_t payload[MBS_CONTACT_BINARY_REQUEST_PAYLOAD_MAX_LEN];
} mbs_contact_binary_request_event;

/** @brief MeshCore advert response projected into Contact policy. */
typedef struct mbs_contact_response_advert_event {
	uint8_t public_key[MBS_CONTACT_PUBLIC_KEY_SIZE];
	char name[MBS_CONTACT_NAME_MAX_LEN];
	mbs_contact_role role;
	/** True for runtime-new adverts; post-policy, true only for unstored candidates. */
	bool is_new;
	uint32_t advert_timestamp;
	bool has_response_snr;
	int8_t response_snr;
	bool has_position;
	int32_t latitude;
	int32_t longitude;
	/**
	 * True when out_path_len/path_hash_size contain known path metadata.
	 * out_path_len may be 0 for a zero-hop direct neighbor.
	 */
	bool has_out_path;
	uint8_t out_path_len;
	uint8_t path_hash_size;
	uint8_t out_path[MBS_CONTACT_OUTPATH_MAX_LEN];
	uint8_t raw_advert_len;
	uint8_t raw_advert[MBS_CONTACT_ADVERT_RAW_MAX_LEN];
} mbs_contact_response_advert_event;

/** @brief Contact store change event payload. */
typedef struct mbs_contact_store_change_event {
	/** Changed contact public-key prefix. */
	uint8_t key_prefix[MBS_CONTACT_PREFIX_BYTES];
} mbs_contact_store_change_event;

/** @brief Contact path response event. */
typedef struct mbs_contact_response_path_event {
	bool is_discover;
	uint8_t key_prefix[MBS_CONTACT_PREFIX_BYTES];
	uint32_t timestamp;
	uint32_t tag;
	bool has_response_snr;
	int8_t response_snr;
	/**
	 * True when out_path_len/path_hash_size contain known path metadata.
	 * out_path_len may be 0 for a zero-hop direct neighbor.
	 */
	bool has_out_path;
	/** Upstream MeshCore encoded out-path length field when known. */
	uint8_t out_path_len_field;
	uint8_t out_path_len;
	uint8_t path_hash_size;
	uint8_t out_path[MBS_CONTACT_OUTPATH_MAX_LEN];
	/** Upstream MeshCore encoded inbound path length field when known. */
	uint8_t in_path_len_field;
	uint8_t in_path_len;
	uint8_t in_path[MBS_CONTACT_OUTPATH_MAX_LEN];
	uint8_t out_path_snr_count;
	int8_t out_path_snr[MBS_CONTACT_OUTPATH_MAX_LEN];
	uint8_t return_path_snr_count;
	int8_t return_path_snr[MBS_CONTACT_OUTPATH_MAX_LEN];
} mbs_contact_response_path_event;

/** @brief Node-discover response event carrying discovered public-key bytes. */
typedef struct mbs_contact_response_discover_event {
	/** Discovered node role. */
	mbs_contact_role role;
	/** Request correlation tag echoed by the responder. */
	uint32_t tag;
	/** Discovered node full public key. */
	uint8_t public_key[MBS_CONTACT_PUBLIC_KEY_SIZE];
	/** Number of valid bytes in @ref path. */
	uint8_t path_len;
	/** Encoded response source path. */
	uint8_t path[MBS_CONTACT_OUTPATH_MAX_LEN];
	/** SNR measured by the responder when it received the request. */
	int8_t uplink_snr;
	/** SNR measured locally when this node received the response. */
	int8_t downlink_snr;
} mbs_contact_response_discover_event;

/** @brief Contact trace-path response event. */
typedef struct mbs_contact_response_trace_path_event {
	uint8_t key_prefix[MBS_CONTACT_PREFIX_BYTES];
	uint32_t timestamp;
	uint32_t tag;
	/* Trace state code, reserved for future extensions. */
	uint8_t state;
	bool has_response_snr;
	int8_t response_snr;
	uint8_t out_path_snr_count;
	int8_t out_path_snr[MBS_CONTACT_OUTPATH_MAX_LEN];
	uint8_t return_path_snr_count;
	int8_t return_path_snr[MBS_CONTACT_OUTPATH_MAX_LEN];
} mbs_contact_response_trace_path_event;

/** @brief Telemetry response event carrying the request tag and raw payload. */
typedef struct mbs_contact_response_telemetry_event {
	uint8_t key_prefix[MBS_CONTACT_PREFIX_BYTES];
	uint32_t timestamp;
	uint32_t tag;
	uint8_t payload_len;
	uint8_t payload[MBS_CONTACT_TELEMETRY_PAYLOAD_MAX_LEN];
} mbs_contact_response_telemetry_event;

/** @brief Binary response event carrying the request tag and raw payload. */
typedef struct mbs_contact_response_binary_event {
	uint8_t key_prefix[MBS_CONTACT_PREFIX_BYTES];
	uint32_t timestamp;
	uint32_t tag;
	uint8_t payload_len;
	uint8_t payload[MBS_CONTACT_BINARY_RESPONSE_PAYLOAD_MAX_LEN];
} mbs_contact_response_binary_event;

/**
 * @brief Get a persisted contact by stable slot index.
 *
 * @param index Zero-based local contact slot index.
 * @param contact Output contact record.
 * @return 0 on success; -ENOENT when the slot is empty or out of range;
 *         -ENOTSUP when contact storage is disabled; negative errno on error.
 */
int mbs_contact_get(size_t index, mbs_contact *contact);

/**
 * @brief Update a contact slot by public-key prefix.
 *
 * @param public_key_prefix Contact public-key prefix.
 * @param contact Contact record to persist. A non-empty management credential
 *        must contain 8 to 16 printable non-whitespace ASCII bytes.
 * @return 0 on success; -ENOTSUP when contact storage is disabled; negative errno on error.
 */
int mbs_contact_set(const uint8_t *public_key_prefix, const mbs_contact *contact);

/**
 * @brief Insert a contact identity by full public key.
 *
 * New records store the supplied public key, name, and role with no management
 * secret. Existing records are rejected with -EEXIST; use
 * mbs_contact_set() to update mutable contact fields.
 *
 * @param public_key Full 32-byte contact public key.
 * @param name Optional NUL-terminated contact name.
 * @param role Contact role to store.
 * @return 0 on success; -EEXIST when the contact already exists;
 *         -ENOTSUP when contact storage is disabled; negative errno on error.
 */
int mbs_contact_insert(const uint8_t *public_key, const char *name,
			   mbs_contact_role role);

/**
 * @brief Reset a contact slot by public-key prefix.
 *
 * Empty slots are treated as already reset.
 *
 * @param public_key_prefix Contact public-key prefix.
 * @return 0 on success or when already empty; -ENOTSUP when contact storage is
 *         disabled; negative errno on error.
 */
int mbs_contact_reset(const uint8_t *public_key_prefix);

/**
 * @brief Find persistent contact data by public key.
 *
 * @return 0 on success; -ENOENT if not found; -ENOTSUP when contact storage is
 *         disabled; negative errno on error.
 */
int mbs_contact_find_by_key(const uint8_t *public_key, mbs_contact *contact);

/**
 * @brief Find persistent contact data by public key prefix.
 *
 * @param public_key_prefix Contact public-key prefix.
 * @param contact Output contact record.
 * @return 0 on success; -ENOENT if not found; -ENOTSUP when contact storage is
 *         disabled; negative errno on error.
 */
int mbs_contact_find_by_prefix(const uint8_t *prefix, mbs_contact *contact);

/**
 * @brief Find the next persistent contact matching a 1-byte mesh hash.
 *
 * @param hash Pointer to hash bytes. First byte is used.
 * @param start_slot First stable contact slot index to consider.
 * @param[out] slot_id Stable contact slot index for the returned contact.
 * @param[out] contact Output contact record.
 * @return 0 on success; -ENOENT when no further match exists; negative errno
 *         on error; -ENOTSUP when contact storage is disabled.
 */
int mbs_contact_next_by_hash(const uint8_t *hash, size_t start_slot,
				 size_t *slot_id, mbs_contact *contact);

/**
 * @brief Request cached contact advert replay by public key prefix.
 *
 * @param prefix Contact public-key prefix.
 * @return 0 on success; -ENOTSUP when contact storage is disabled; negative errno
 *         on failure.
 */
int mbs_contact_share_request(const uint8_t *prefix);

/**
 * @brief Request discover-path for a contact by public key prefix.
 *
 * @param prefix Contact public-key prefix.
 * @param[out] out_tag Optional generated request tag.
 * @return 0 on success; -ENOTSUP when contact storage is disabled; negative errno
 *         on failure.
 */
int mbs_contact_discover_path_request(const uint8_t *prefix, uint32_t *out_tag);

/**
 * @brief Request trace-path for a contact by public key prefix.
 *
 * Contact must have known path metadata. A zero-hop direct neighbor may have an
 * empty @c out_path with @c is_neighbor true; an empty unknown path returns
 * -EINVAL.
 *
 * @param prefix Contact public-key prefix.
 * @param[out] out_tag Optional generated request tag.
 * @return 0 on success; -ENOTSUP when contact storage is disabled; negative errno
 *         on failure.
 */
int mbs_contact_trace_path_request(const uint8_t *prefix, uint32_t *out_tag);

/**
 * @brief Request telemetry for a contact by public key prefix.
 *
 * @param prefix Contact public-key prefix.
 * @param[out] out_tag Optional generated request tag.
 * @return 0 on success; -ENOTSUP when contact storage is disabled; negative errno
 *         on failure.
 */
int mbs_contact_telemetry_request(const uint8_t *prefix, uint32_t *out_tag);

/**
 * @brief Request binary data from a contact by public key prefix.
 *
 * @param prefix Contact public-key prefix.
 * @param payload Binary request payload.
 * @param payload_len Payload length.
 * @param[out] out_tag Optional generated correlation tag.
 * @return 0 on success; -ENOTSUP when contact storage is disabled; negative errno
 *         on failure.
 */
int mbs_contact_binary_request(const uint8_t *prefix, const uint8_t *payload,
				   size_t payload_len, uint32_t *out_tag);

/**
 * @brief Get the number of stored contact records currently in use.
 *
 * @return Current contact-store record count, or 0 when contact storage is disabled.
 */
uint8_t mbs_contact_store_count(void);

/**
 * @brief Get the configured contact-store capacity.
 *
 * @return Maximum number of contact records supported by the current build, or
 *         0 when contact storage is disabled.
 */
uint8_t mbs_contact_store_size(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MBS_CONTACT_H_ */
