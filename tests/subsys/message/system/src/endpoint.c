/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <contact/contact.h>
#include <meshcore/meshcore.h>
#include <message/message.h>
#include <radio/radio.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#define TEST_PARTITION_NODE    DT_NODELABEL(storage_partition)
#define PRODUCT_PARTITION_NODE DT_NODELABEL(product_storage_partition)
#define SLOT0_PARTITION_NODE   DT_NODELABEL(slot0_partition)
#define SLOT1_PARTITION_NODE   DT_NODELABEL(slot1_partition)
#define RRAM_NODE              DT_MEM_FROM_PARTITION(TEST_PARTITION_NODE)
#define RADIO_NODE             DT_CHOSEN(meshbus_radio)

#define PARTITION_START(node_id) DT_REG_ADDR(node_id)
#define PARTITION_END(node_id)   (DT_REG_ADDR(node_id) + DT_REG_SIZE(node_id))
#define IDENTITY_WAIT_MS         5000U
#define IDENTITY_POLL_MS         20U

static const struct gpio_dt_spec radio_dio1 = GPIO_DT_SPEC_GET(RADIO_NODE, dio1_gpios);
static const struct gpio_dt_spec radio_busy = GPIO_DT_SPEC_GET(RADIO_NODE, busy_gpios);

BUILD_ASSERT(!IS_ENABLED(CONFIG_FLASH_SIMULATOR), "Message system endpoint must use real RRAM");
BUILD_ASSERT(IS_ENABLED(CONFIG_MBS_RADIO_DEFAULT_RECEIVE_ONLY),
	     "Message system endpoint must boot with TX locked");
BUILD_ASSERT(CONFIG_MBS_MESHCORE_DEFAULT_ROLE == MBS_MESHCORE_ROLE_CHAT,
	     "Message system endpoint must use chat role");
BUILD_ASSERT(DT_NODE_HAS_STATUS(TEST_PARTITION_NODE, okay), "test storage missing");
BUILD_ASSERT(DT_NODE_HAS_STATUS(PRODUCT_PARTITION_NODE, okay), "product storage guard missing");
BUILD_ASSERT(DT_REG_ADDR(TEST_PARTITION_NODE) == 0x164000, "wrong test storage base");
BUILD_ASSERT(DT_REG_SIZE(TEST_PARTITION_NODE) == 0x10000, "wrong test storage size");
BUILD_ASSERT(DT_REG_ADDR(PRODUCT_PARTITION_NODE) == 0x174000, "wrong product storage base");
BUILD_ASSERT(DT_REG_SIZE(PRODUCT_PARTITION_NODE) == 0x9000, "wrong product storage size");
BUILD_ASSERT((DT_REG_ADDR(TEST_PARTITION_NODE) % DT_PROP(RRAM_NODE, erase_block_size)) == 0,
	     "test storage base is not erase aligned");
BUILD_ASSERT((DT_REG_SIZE(TEST_PARTITION_NODE) % DT_PROP(RRAM_NODE, erase_block_size)) == 0,
	     "test storage size is not erase aligned");
BUILD_ASSERT(PARTITION_END(SLOT0_PARTITION_NODE) <= PARTITION_START(SLOT1_PARTITION_NODE),
	     "slot0 overlaps slot1");
BUILD_ASSERT(PARTITION_END(SLOT1_PARTITION_NODE) <= PARTITION_START(TEST_PARTITION_NODE),
	     "slot1 overlaps test storage");
BUILD_ASSERT(PARTITION_END(TEST_PARTITION_NODE) <= PARTITION_START(PRODUCT_PARTITION_NODE),
	     "test storage overlaps product storage");
BUILD_ASSERT(PARTITION_END(PRODUCT_PARTITION_NODE) <= DT_REG_SIZE(RRAM_NODE),
	     "product storage exceeds CPUAPP RRAM");

static atomic_t rx_count;
static atomic_t ack_count;
static atomic_t tx_done_count;
static atomic_t tx_error_count;
static atomic_t anon_rx_count;
static atomic_t raw_rx_count;
static atomic_t anon_timing_active;
static atomic_t anon_submit_ms;
static atomic_t anon_expected_delay_ms;
static atomic_t anon_last_tx_elapsed_ms;

static void bytes_to_hex(const uint8_t *bytes, size_t len, char *out, size_t out_size)
{
	static const char hex[] = "0123456789abcdef";

	if (out == NULL || out_size == 0U) {
		return;
	}

	if (bytes == NULL || out_size < (len * 2U) + 1U) {
		out[0] = '\0';
		return;
	}

	for (size_t i = 0U; i < len; i++) {
		out[i * 2U] = hex[bytes[i] >> 4];
		out[i * 2U + 1U] = hex[bytes[i] & 0x0fU];
	}
	out[len * 2U] = '\0';
}

static int parse_hex_exact(const char *arg, uint8_t *out, size_t out_len)
{
	int parsed;

	if (arg == NULL || out == NULL || strlen(arg) != out_len * 2U) {
		return -EINVAL;
	}

	parsed = hex2bin(arg, strlen(arg), out, out_len);
	return parsed == (int)out_len ? 0 : -EINVAL;
}

static int parse_hex_variable(const char *arg, uint8_t *out, size_t out_capacity,
			      size_t *out_len)
{
	size_t arg_len;
	int parsed;

	if (arg == NULL || out == NULL || out_len == NULL) {
		return -EINVAL;
	}
	if (strcmp(arg, "-") == 0) {
		*out_len = 0U;
		return 0;
	}

	arg_len = strlen(arg);
	if (arg_len == 0U || (arg_len % 2U) != 0U || arg_len / 2U > out_capacity) {
		return -EINVAL;
	}

	parsed = hex2bin(arg, arg_len, out, out_capacity);
	if (parsed < 0 || (size_t)parsed != arg_len / 2U) {
		return -EINVAL;
	}

	*out_len = (size_t)parsed;
	return 0;
}

static int parse_u8(const char *arg, uint8_t *out)
{
	char *end = NULL;
	unsigned long value;

	if (arg == NULL || out == NULL || arg[0] == '\0') {
		return -EINVAL;
	}

	errno = 0;
	value = strtoul(arg, &end, 0);
	if (errno != 0 || end == arg || *end != '\0' || value > UINT8_MAX) {
		return -EINVAL;
	}

	*out = (uint8_t)value;
	return 0;
}

static int parse_u32(const char *arg, uint32_t *out)
{
	char *end = NULL;
	unsigned long value;

	if (arg == NULL || out == NULL || arg[0] == '\0') {
		return -EINVAL;
	}

	errno = 0;
	value = strtoul(arg, &end, 0);
	if (errno != 0 || end == arg || *end != '\0' || value > UINT32_MAX) {
		return -EINVAL;
	}

	*out = (uint32_t)value;
	return 0;
}

static int parse_bool(const char *arg, bool *out)
{
	if (arg == NULL || out == NULL) {
		return -EINVAL;
	}
	if (strcmp(arg, "1") == 0 || strcmp(arg, "true") == 0) {
		*out = true;
		return 0;
	}
	if (strcmp(arg, "0") == 0 || strcmp(arg, "false") == 0) {
		*out = false;
		return 0;
	}

	return -EINVAL;
}

static bool identity_is_ready(const mbs_meshcore_config *cfg)
{
	return cfg != NULL && cfg->public_key.size == MBS_MESHCORE_PUBLIC_KEY_SIZE &&
	       cfg->private_key.size == MBS_MESHCORE_PRIVATE_KEY_SIZE;
}

static int identity_wait(mbs_meshcore_config *cfg)
{
	int64_t deadline = k_uptime_get() + IDENTITY_WAIT_MS;
	int rc;

	do {
		rc = mbs_meshcore_config_get(cfg);
		if (rc != 0) {
			return rc;
		}
		if (identity_is_ready(cfg)) {
			return 0;
		}
		k_sleep(K_MSEC(IDENTITY_POLL_MS));
	} while (k_uptime_get() < deadline);

	return -ETIMEDOUT;
}

static void identity_print(const struct shell *sh, const mbs_meshcore_config *cfg,
			   const char *marker)
{
	char public_key_hex[MBS_MESHCORE_PUBLIC_KEY_SIZE * 2U + 1U];

	bytes_to_hex(cfg->public_key.bytes, cfg->public_key.size, public_key_hex,
		     sizeof(public_key_hex));
	shell_print(sh, "%s role=chat name=%s public_key=%s private_key=redacted", marker,
		    cfg->name, public_key_hex);
}

static int radio_tx_set(bool enabled)
{
	mbs_radio_config cfg = meshbus_RadioConfig_init_zero;
	int rc;

	rc = mbs_radio_config_get(&cfg);
	if (rc != 0) {
		return rc;
	}

	cfg.enabled = true;
	cfg.receive_only = !enabled;
	cfg.frequency = CONFIG_MBS_RADIO_DEFAULT_FREQUENCY;
	cfg.tx_power = CONFIG_MBS_RADIO_DEFAULT_TX_POWER;
	return mbs_radio_config_set(&cfg);
}

static void message_queue_drain(void)
{
	mbs_message_content message = meshbus_MessageContent_init_zero;

	while (mbs_message_next(&message) == 0) {
		message = (mbs_message_content)meshbus_MessageContent_init_zero;
	}
}

static int contacts_clear(void)
{
	int first_error = 0;

	for (size_t i = 0U; i < mbs_contact_store_size(); i++) {
		mbs_contact contact = meshbus_Contact_init_zero;
		int rc = mbs_contact_get(i, &contact);

		if (rc == -ENOENT) {
			continue;
		}
		if (rc == 0) {
			rc = mbs_contact_reset(contact.public_key.bytes);
		}
		if (rc != 0 && first_error == 0) {
			first_error = rc;
		}
	}

	return first_error;
}

static void counters_clear(void)
{
	atomic_set(&rx_count, 0);
	atomic_set(&ack_count, 0);
	atomic_set(&tx_done_count, 0);
	atomic_set(&tx_error_count, 0);
	atomic_set(&anon_rx_count, 0);
	atomic_set(&raw_rx_count, 0);
	atomic_set(&anon_timing_active, 0);
	atomic_set(&anon_submit_ms, 0);
	atomic_set(&anon_expected_delay_ms, 0);
	atomic_set(&anon_last_tx_elapsed_ms, 0);
}

static void radio_receive_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_radio_receive_event *event;
	unsigned int count;

	if (chan != &mbs_radio_receive_chan) {
		return;
	}

	event = zbus_chan_const_msg(chan);
	if (event == NULL) {
		return;
	}

	count = (unsigned int)atomic_inc(&raw_rx_count) + 1U;
	printk("MBS_RADIO_SYSTEM_RAW_RX count=%u uptime_ms=%u len=%u "
	       "rssi_dbm=%d snr_q4=%d\n",
	       count, k_uptime_get_32(), (unsigned int)event->len, event->rssi, event->snr);
}

static void message_response_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_message_response_event *event;
	char payload_hex[CONFIG_MBS_MESSAGE_TX_MAX_LEN * 2U + 1U];
	int16_t rssi;
	int8_t snr;
	unsigned int count;

	if (chan != &mbs_message_response_chan) {
		return;
	}

	event = zbus_chan_const_msg(chan);
	if (event == NULL) {
		return;
	}

	bytes_to_hex(event->payload, event->payload_len, payload_hex, sizeof(payload_hex));
	rssi = mbs_radio_last_rssi();
	snr = mbs_radio_last_snr();
	count = (unsigned int)atomic_inc(&rx_count) + 1U;
	printk("MBS_MESSAGE_SYSTEM_RX count=%u type=%u route=%u "
	       "target=%02x%02x%02x%02x sender=%s rssi_dbm=%d snr_q4=%d "
	       "payload_hex=%s\n",
	       count, (unsigned int)event->type, (unsigned int)event->route, event->target[0],
	       event->target[1], event->target[2], event->target[3], event->sender_name, rssi, snr,
	       payload_hex);
}

static void message_ack_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_message_ack_response_event *event;
	int16_t rssi;
	int8_t snr;
	unsigned int count;

	if (chan != &mbs_message_ack_response_chan) {
		return;
	}

	event = zbus_chan_const_msg(chan);
	if (event == NULL) {
		return;
	}

	rssi = mbs_radio_last_rssi();
	snr = mbs_radio_last_snr();
	count = (unsigned int)atomic_inc(&ack_count) + 1U;
	printk("MBS_MESSAGE_SYSTEM_ACK count=%u attempt=%u "
	       "target=%02x%02x%02x%02x rssi_dbm=%d snr_q4=%d\n",
	       count, (unsigned int)event->attempt, event->target[0], event->target[1],
	       event->target[2], event->target[3], rssi, snr);
}

static void anon_data_response_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_meshcore_anon_data_response_event *event;
	char path_hex[MBS_MESHCORE_PATH_MAX_LEN * 2U + 1U];
	char payload_hex[MBS_MESHCORE_ANON_DATA_RECEIVED_MAX_LEN * 2U + 1U];
	int16_t rssi;
	int8_t snr;
	unsigned int count;

	if (chan != &mbs_meshcore_anon_data_response_chan) {
		return;
	}

	event = zbus_chan_const_msg(chan);
	if (event == NULL) {
		return;
	}

	bytes_to_hex(event->path, event->path_len, path_hex, sizeof(path_hex));
	bytes_to_hex(event->payload, event->payload_len, payload_hex, sizeof(payload_hex));
	rssi = mbs_radio_last_rssi();
	snr = mbs_radio_last_snr();
	count = (unsigned int)atomic_inc(&anon_rx_count) + 1U;
	printk("MBS_MESHCORE_SYSTEM_ANON_RX count=%u uptime_ms=%u route=%u "
	       "sender=%02x%02x%02x%02x path_len=%u path_hex=%s "
	       "rssi_dbm=%d snr_q4=%d payload_hex=%s\n",
	       count, k_uptime_get_32(), (unsigned int)event->route, event->public_key[0],
	       event->public_key[1], event->public_key[2], event->public_key[3],
	       (unsigned int)event->path_len, path_hex, rssi, snr, payload_hex);
}

static void radio_tx_done_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_radio_tx_done_event *event;
	uint32_t now_ms;
	uint32_t elapsed_ms = 0U;
	uint32_t expected_delay_ms = 0U;
	unsigned int count;

	if (chan != &mbs_radio_tx_done_chan) {
		return;
	}

	event = zbus_chan_const_msg(chan);
	if (event == NULL) {
		return;
	}

	now_ms = k_uptime_get_32();
	if (atomic_cas(&anon_timing_active, 1, 0)) {
		uint32_t submitted_ms = (uint32_t)atomic_get(&anon_submit_ms);

		expected_delay_ms = (uint32_t)atomic_get(&anon_expected_delay_ms);
		elapsed_ms = now_ms - submitted_ms;
		atomic_set(&anon_last_tx_elapsed_ms, (atomic_val_t)elapsed_ms);
	}

	count = (unsigned int)atomic_inc(&tx_done_count) + 1U;
	if (event->status != 0) {
		(void)atomic_inc(&tx_error_count);
	}
	printk("MBS_MESSAGE_SYSTEM_TX_DONE count=%u status=%d uptime_ms=%u "
	       "elapsed_ms=%u expected_delay_ms=%u\n",
	       count, event->status, now_ms, elapsed_ms, expected_delay_ms);
}

ZBUS_LISTENER_DEFINE(message_system_response_listener, message_response_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_message_response_chan, message_system_response_listener, 0);
ZBUS_LISTENER_DEFINE(message_system_raw_radio_listener, radio_receive_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_radio_receive_chan, message_system_raw_radio_listener, 0);
ZBUS_LISTENER_DEFINE(message_system_ack_listener, message_ack_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_message_ack_response_chan, message_system_ack_listener, 0);
ZBUS_LISTENER_DEFINE(meshcore_system_anon_listener, anon_data_response_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_meshcore_anon_data_response_chan, meshcore_system_anon_listener, 0);
ZBUS_LISTENER_DEFINE(message_system_tx_done_listener, radio_tx_done_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_radio_tx_done_chan, message_system_tx_done_listener, 0);

static int cmd_identity(const struct shell *sh, size_t argc, char **argv)
{
	mbs_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = identity_wait(&cfg);
	if (rc != 0) {
		shell_error(sh, "MBS_MESSAGE_SYSTEM_RESULT command=identity rc=%d", rc);
		return rc;
	}

	identity_print(sh, &cfg, "MBS_MESSAGE_SYSTEM_IDENTITY");
	return 0;
}

static int cmd_identity_new(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	rc = radio_tx_set(false);
	if (rc == 0) {
		rc = contacts_clear();
	}
	message_queue_drain();
	counters_clear();
	if (rc == 0) {
		rc = mbs_meshcore_config_reset();
	}
	shell_print(sh, "MBS_MESSAGE_SYSTEM_RESULT command=identity_new rc=%d reset_accepted=%u",
		    rc, rc == 0);
	return rc;
}

static int cmd_tx(const struct shell *sh, size_t argc, char **argv)
{
	mbs_radio_config cfg = meshbus_RadioConfig_init_zero;
	bool enabled;
	int rc;

	ARG_UNUSED(argc);

	if (parse_bool(argv[1], &enabled) != 0) {
		return -EINVAL;
	}

	rc = radio_tx_set(enabled);
	if (rc == 0) {
		rc = mbs_radio_config_get(&cfg);
	}
	if (rc != 0) {
		shell_error(sh, "MBS_MESSAGE_SYSTEM_RESULT command=tx rc=%d", rc);
		return rc;
	}

	shell_print(sh,
		    "MBS_MESSAGE_SYSTEM_TX_LOCK tx_enabled=%u frequency_hz=%llu "
		    "power_dbm=%d",
		    cfg.receive_only ? 0U : 1U, (unsigned long long)cfg.frequency,
		    (int)cfg.tx_power);
	return 0;
}

static int cmd_peer_add(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t public_key[MBS_CONTACT_PUBLIC_KEY_SIZE];
	int rc;

	ARG_UNUSED(argc);

	if (parse_hex_exact(argv[1], public_key, sizeof(public_key)) != 0 || argv[2][0] == '\0') {
		return -EINVAL;
	}

	rc = mbs_contact_insert(public_key, argv[2], MBS_CONTACT_ROLE_CHAT);
	if (rc != 0) {
		shell_error(sh, "MBS_MESSAGE_SYSTEM_RESULT command=peer_add rc=%d", rc);
		return rc;
	}

	shell_print(sh, "MBS_MESSAGE_SYSTEM_PEER_ADDED prefix=%02x%02x%02x%02x name=%s",
		    public_key[0], public_key[1], public_key[2], public_key[3], argv[2]);
	return 0;
}

static int cmd_peer_path(const struct shell *sh, size_t argc, char **argv)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	uint8_t public_key[MBS_CONTACT_PUBLIC_KEY_SIZE];
	uint8_t path[MBS_CONTACT_OUTPATH_MAX_LEN];
	uint8_t hash_size;
	size_t path_len;
	int rc;

	ARG_UNUSED(argc);

	if (parse_hex_exact(argv[1], public_key, sizeof(public_key)) != 0 ||
	    parse_u8(argv[2], &hash_size) != 0 || hash_size == 0U ||
	    hash_size > MBS_CONTACT_PATH_HASH_SIZE_MAX ||
	    parse_hex_variable(argv[3], path, sizeof(path), &path_len) != 0 ||
	    (path_len % hash_size) != 0U || path_len / hash_size > 63U) {
		return -EINVAL;
	}

	rc = mbs_contact_find_by_key(public_key, &contact);
	if (rc == 0) {
		memset(contact.out_path.bytes, 0, sizeof(contact.out_path.bytes));
		if (path_len > 0U) {
			memcpy(contact.out_path.bytes, path, path_len);
		}
		contact.out_path.size = path_len;
		contact.path_hash_size = hash_size;
		contact.is_neighbor = path_len == 0U;
		rc = mbs_contact_set(public_key, &contact);
	}
	if (rc != 0) {
		shell_error(sh, "MBS_MESSAGE_SYSTEM_RESULT command=peer_path rc=%d", rc);
		return rc;
	}

	shell_print(sh,
		    "MBS_MESHCORE_SYSTEM_PEER_PATH prefix=%02x%02x%02x%02x "
		    "hash_size=%u path_bytes=%u neighbor=%u",
		    public_key[0], public_key[1], public_key[2], public_key[3],
		    (unsigned int)hash_size, (unsigned int)path_len, path_len == 0U ? 1U : 0U);
	return 0;
}

static int cmd_peer_remove(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t prefix[MBS_CONTACT_PREFIX_BYTES];
	int rc;

	ARG_UNUSED(argc);

	if (parse_hex_exact(argv[1], prefix, sizeof(prefix)) != 0) {
		return -EINVAL;
	}

	rc = mbs_contact_reset(prefix);
	if (rc != 0) {
		shell_error(sh, "MBS_MESSAGE_SYSTEM_RESULT command=peer_remove rc=%d", rc);
		return rc;
	}

	shell_print(sh, "MBS_MESSAGE_SYSTEM_PEER_REMOVED prefix=%02x%02x%02x%02x", prefix[0],
		    prefix[1], prefix[2], prefix[3]);
	return 0;
}

static int cmd_advert(const struct shell *sh, size_t argc, char **argv)
{
	bool flood;
	int rc;

	ARG_UNUSED(argc);

	if (parse_bool(argv[1], &flood) != 0) {
		return -EINVAL;
	}

	rc = mbs_meshcore_advert_request(flood);
	if (rc != 0) {
		shell_error(sh, "MBS_MESSAGE_SYSTEM_RESULT command=advert rc=%d", rc);
		return rc;
	}

	shell_print(sh, "MBS_MESSAGE_SYSTEM_ADVERT accepted=1 flood=%u", flood ? 1U : 0U);
	return 0;
}

static int cmd_anon(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t path[MBS_MESHCORE_PATH_MAX_LEN];
	uint8_t hash_size;
	uint32_t delay_ms;
	uint32_t submitted_ms;
	size_t path_len;
	size_t payload_len = strlen(argv[6]);
	const char *mode = argv[1];
	int rc;

	ARG_UNUSED(argc);

	if (parse_hex_exact(argv[2], public_key, sizeof(public_key)) != 0 ||
	    parse_u32(argv[3], &delay_ms) != 0 || parse_u8(argv[4], &hash_size) != 0 ||
	    parse_hex_variable(argv[5], path, sizeof(path), &path_len) != 0 || payload_len == 0U ||
	    payload_len > MBS_MESHCORE_ANON_DATA_PAYLOAD_MAX_LEN) {
		return -EINVAL;
	}

	if (strcmp(mode, "path") == 0) {
		if (hash_size == 0U || hash_size > MBS_MESHCORE_PATH_HASH_SIZE_MAX ||
		    (path_len % hash_size) != 0U || path_len / hash_size > 63U) {
			return -EINVAL;
		}
	} else if (strcmp(mode, "normal") == 0 || strcmp(mode, "direct") == 0) {
		if (hash_size != 0U || path_len != 0U) {
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}

	submitted_ms = k_uptime_get_32();
	atomic_set(&anon_submit_ms, (atomic_val_t)submitted_ms);
	atomic_set(&anon_expected_delay_ms, (atomic_val_t)delay_ms);
	atomic_set(&anon_last_tx_elapsed_ms, 0);
	atomic_set(&anon_timing_active, 1);

	if (strcmp(mode, "normal") == 0) {
		rc = mbs_meshcore_anon_data_send_delayed(
			public_key, (const uint8_t *)argv[6], payload_len, delay_ms);
	} else if (strcmp(mode, "direct") == 0) {
		rc = mbs_meshcore_anon_data_send_direct_delayed(
			public_key, (const uint8_t *)argv[6], payload_len, delay_ms);
	} else {
		rc = mbs_meshcore_anon_data_send_via_path_delayed(
			public_key, (const uint8_t *)argv[6], payload_len, path,
			(uint8_t)path_len, hash_size, delay_ms);
	}
	if (rc != 0) {
		atomic_set(&anon_timing_active, 0);
		shell_error(sh, "MBS_MESSAGE_SYSTEM_RESULT command=anon rc=%d", rc);
		return rc;
	}

	shell_print(sh,
		    "MBS_MESHCORE_SYSTEM_ANON_SEND accepted=1 mode=%s submitted_ms=%u "
		    "delay_ms=%u target=%02x%02x%02x%02x path_bytes=%u payload=%s",
		    mode, submitted_ms, delay_ms, public_key[0], public_key[1], public_key[2],
		    public_key[3], (unsigned int)path_len, argv[6]);
	return 0;
}

static int cmd_send(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t prefix[MBS_CONTACT_PREFIX_BYTES];
	uint8_t attempt;
	uint64_t ack_token = 0U;
	bool flood;
	size_t payload_len = strlen(argv[4]);
	int rc;

	ARG_UNUSED(argc);

	if (parse_hex_exact(argv[1], prefix, sizeof(prefix)) != 0 ||
	    parse_u8(argv[2], &attempt) != 0 || attempt == 0U || parse_bool(argv[3], &flood) != 0 ||
	    payload_len == 0U || payload_len > CONFIG_MBS_MESSAGE_TX_MAX_LEN) {
		return -EINVAL;
	}

	rc = mbs_message_send_to_node(prefix, (const uint8_t *)argv[4], payload_len, flood,
					  attempt, &ack_token);
	if (rc != 0) {
		shell_error(sh, "MBS_MESSAGE_SYSTEM_RESULT command=send rc=%d", rc);
		return rc;
	}

	shell_print(sh,
		    "MBS_MESSAGE_SYSTEM_SEND accepted=1 prefix=%02x%02x%02x%02x "
		    "attempt=%u flood=%u ack_token=%llu payload=%s",
		    prefix[0], prefix[1], prefix[2], prefix[3], (unsigned int)attempt,
		    flood ? 1U : 0U, (unsigned long long)ack_token, argv[4]);
	return 0;
}

static int cmd_burst(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t prefix[MBS_CONTACT_PREFIX_BYTES];
	uint8_t first_attempt;
	uint8_t count;
	size_t prefix_len = strlen(argv[4]);

	ARG_UNUSED(argc);

	if (parse_hex_exact(argv[1], prefix, sizeof(prefix)) != 0 ||
	    parse_u8(argv[2], &first_attempt) != 0 || first_attempt == 0U ||
	    parse_u8(argv[3], &count) != 0 || count == 0U ||
	    count > CONFIG_MBS_MESSAGE_PENDING_SEND_COUNT ||
	    (uint16_t)first_attempt + count - 1U > UINT8_MAX || prefix_len == 0U ||
	    prefix_len + 3U > CONFIG_MBS_MESSAGE_TX_MAX_LEN) {
		return -EINVAL;
	}

	for (uint8_t i = 0U; i < count; i++) {
		char payload[CONFIG_MBS_MESSAGE_TX_MAX_LEN + 1U];
		uint8_t attempt = first_attempt + i;
		uint64_t ack_token = 0U;
		int payload_len;
		int rc;

		payload_len = snprintk(payload, sizeof(payload), "%s-%02u", argv[4],
				       (unsigned int)(i + 1U));
		if (payload_len <= 0 || payload_len > CONFIG_MBS_MESSAGE_TX_MAX_LEN) {
			return -EINVAL;
		}

		rc = mbs_message_send_to_node(prefix, (const uint8_t *)payload,
						  (size_t)payload_len, true, attempt, &ack_token);
		if (rc != 0) {
			shell_error(sh, "MBS_MESSAGE_SYSTEM_RESULT command=burst index=%u rc=%d",
				    (unsigned int)i, rc);
			return rc;
		}

		shell_print(sh,
			    "MBS_MESSAGE_SYSTEM_BURST_ITEM index=%u attempt=%u "
			    "ack_token=%llu payload=%s",
			    (unsigned int)i, (unsigned int)attempt, (unsigned long long)ack_token,
			    payload);
	}

	shell_print(sh, "MBS_MESSAGE_SYSTEM_BURST accepted=%u", (unsigned int)count);
	return 0;
}

static int cmd_next(const struct shell *sh, size_t argc, char **argv)
{
	mbs_message_content message = meshbus_MessageContent_init_zero;
	char payload_hex[CONFIG_MBS_MESSAGE_TX_MAX_LEN * 2U + 1U];
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = mbs_message_next(&message);
	if (rc != 0) {
		shell_error(sh, "MBS_MESSAGE_SYSTEM_RESULT command=next rc=%d", rc);
		return rc;
	}

	bytes_to_hex(message.payload.bytes, message.payload.size, payload_hex, sizeof(payload_hex));
	shell_print(sh,
		    "MBS_MESSAGE_SYSTEM_NEXT type=%u route=%u sender=%s "
		    "payload_hex=%s timestamp_ms=%llu",
		    (unsigned int)message.type, (unsigned int)message.route, message.sender_name,
		    payload_hex, (unsigned long long)message.timestamp);
	return 0;
}

static int cmd_stats(const struct shell *sh, size_t argc, char **argv)
{
	mbs_radio_config cfg = meshbus_RadioConfig_init_zero;
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = mbs_radio_config_get(&cfg);
	if (rc != 0) {
		return rc;
	}

	shell_print(sh,
		    "MBS_MESSAGE_SYSTEM_STATS rx=%ld anon_rx=%ld ack=%ld tx_done=%ld "
		    "tx_error=%ld anon_last_tx_elapsed_ms=%u contacts=%u tx_enabled=%u "
		    "raw_rx=%ld",
		    (long)atomic_get(&rx_count), (long)atomic_get(&anon_rx_count),
		    (long)atomic_get(&ack_count), (long)atomic_get(&tx_done_count),
		    (long)atomic_get(&tx_error_count),
		    (uint32_t)atomic_get(&anon_last_tx_elapsed_ms),
		    (unsigned int)mbs_contact_store_count(), cfg.receive_only ? 0U : 1U,
		    (long)atomic_get(&raw_rx_count));
	return 0;
}

static int cmd_radio_diag(const struct shell *sh, size_t argc, char **argv)
{
	mbs_radio_config cfg = meshbus_RadioConfig_init_zero;
	int16_t rssi = 0;
	int dio1;
	int busy;
	int rssi_rc;
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = mbs_radio_config_get(&cfg);
	if (rc != 0) {
		shell_error(sh, "MBS_RADIO_SYSTEM_RESULT command=radio_diag rc=%d", rc);
		return rc;
	}

	dio1 = gpio_is_ready_dt(&radio_dio1) ? gpio_pin_get_dt(&radio_dio1) : -ENODEV;
	busy = gpio_is_ready_dt(&radio_busy) ? gpio_pin_get_dt(&radio_busy) : -ENODEV;
	rssi_rc = mbs_radio_rssi_inst(&rssi);
	shell_print(sh,
		    "MBS_RADIO_SYSTEM_DIAG uptime_ms=%u state=%u enabled=%u receive_only=%u "
		    "frequency_hz=%llu power_dbm=%d rssi_rc=%d rssi_dbm=%d dio1=%d "
		    "busy=%d raw_rx=%ld",
		    k_uptime_get_32(), (unsigned int)mbs_radio_state_get(),
		    cfg.enabled ? 1U : 0U, cfg.receive_only ? 1U : 0U,
		    (unsigned long long)cfg.frequency, (int)cfg.tx_power, rssi_rc, (int)rssi,
		    dio1, busy, (long)atomic_get(&raw_rx_count));
	return 0;
}

static int cmd_radio_rearm(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	mbs_radio_agc_reset();
	shell_print(sh, "MBS_RADIO_SYSTEM_REARM state=%u",
		    (unsigned int)mbs_radio_state_get());
	return 0;
}

static int cmd_radio_cycle(const struct shell *sh, size_t argc, char **argv)
{
	mbs_radio_config cfg = meshbus_RadioConfig_init_zero;
	mbs_radio_config disabled;
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = mbs_radio_config_get(&cfg);
	if (rc == 0) {
		disabled = cfg;
		disabled.enabled = false;
		rc = mbs_radio_config_set(&disabled);
	}
	if (rc == 0) {
		rc = mbs_radio_config_set(&cfg);
	}
	if (rc != 0) {
		shell_error(sh, "MBS_RADIO_SYSTEM_RESULT command=radio_cycle rc=%d", rc);
		return rc;
	}

	shell_print(sh, "MBS_RADIO_SYSTEM_CYCLE pass=1 state=%u",
		    (unsigned int)mbs_radio_state_get());
	return 0;
}

static int cmd_clear(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	message_queue_drain();
	counters_clear();
	shell_print(sh, "MBS_MESSAGE_SYSTEM_CLEAR pass=1");
	return 0;
}

static int cmd_cleanup(const struct shell *sh, size_t argc, char **argv)
{
	int tx_rc;
	int contacts_rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	tx_rc = radio_tx_set(false);
	contacts_rc = contacts_clear();
	message_queue_drain();
	counters_clear();
	if (tx_rc != 0 || contacts_rc != 0) {
		int rc = tx_rc != 0 ? tx_rc : contacts_rc;

		shell_error(sh, "MBS_MESSAGE_SYSTEM_RESULT command=cleanup rc=%d", rc);
		return rc;
	}

	shell_print(sh, "MBS_MESSAGE_SYSTEM_CLEANUP pass=1 tx_enabled=0 contacts=%u",
		    (unsigned int)mbs_contact_store_count());
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	mbs_msg_test_commands,
	SHELL_CMD_ARG(identity, NULL, "Print public identity only", cmd_identity, 1, 0),
	SHELL_CMD_ARG(identity_new, NULL, "Reset test state and reboot to generate identity",
		      cmd_identity_new, 1, 0),
	SHELL_CMD_ARG(tx, NULL, "Explicitly enable or disable TX: <0|1>", cmd_tx, 2, 0),
	SHELL_CMD_ARG(peer_add, NULL, "Add peer: <full_public_key_hex> <name>", cmd_peer_add, 3, 0),
	SHELL_CMD_ARG(peer_path, NULL, "Set peer path: <full_public_key_hex> <hash_size> <path_hex|->",
		      cmd_peer_path, 4, 0),
	SHELL_CMD_ARG(peer_remove, NULL, "Remove peer: <public_key_prefix_hex>", cmd_peer_remove, 2,
		      0),
	SHELL_CMD_ARG(advert, NULL, "Send advert: <flood:0|1>", cmd_advert, 2, 0),
	SHELL_CMD_ARG(anon, NULL,
		      "Anonymous send: <normal|direct|path> <full_public_key_hex> "
		      "<delay_ms> <hash_size> <path_hex|-> <payload>",
		      cmd_anon, 7, 0),
	SHELL_CMD_ARG(send, NULL, "Send message: <prefix> <attempt> <flood:0|1> <text>", cmd_send,
		      5, 0),
	SHELL_CMD_ARG(burst, NULL,
		      "Immediate sends: <prefix> <first_attempt> <count> <text_prefix>", cmd_burst,
		      5, 0),
	SHELL_CMD_ARG(next, NULL, "Pop the next received message", cmd_next, 1, 0),
	SHELL_CMD_ARG(stats, NULL, "Show endpoint counters", cmd_stats, 1, 0),
	SHELL_CMD_ARG(radio_diag, NULL, "Show raw radio and SX1262 GPIO diagnostics",
		      cmd_radio_diag, 1, 0),
	SHELL_CMD_ARG(radio_rearm, NULL, "Stop and restart RX through the radio service",
		      cmd_radio_rearm, 1, 0),
	SHELL_CMD_ARG(radio_cycle, NULL, "Disable and re-enable the radio service",
		      cmd_radio_cycle, 1, 0),
	SHELL_CMD_ARG(clear, NULL, "Drain messages and clear counters", cmd_clear, 1, 0),
	SHELL_CMD_ARG(cleanup, NULL, "Disable TX and clear test-owned contacts/messages",
		      cmd_cleanup, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(mbs_msg_test, &mbs_msg_test_commands, "Meshbus two-DUT Message test control",
		   NULL);

int main(void)
{
	mbs_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	char public_key_hex[MBS_MESHCORE_PUBLIC_KEY_SIZE * 2U + 1U];
	int rc;

	counters_clear();

	rc = radio_tx_set(false);
	if (rc != 0) {
		printk("MBS_MESSAGE_SYSTEM_STARTUP_FAILED stage=tx_lock rc=%d\n", rc);
		return 0;
	}

	rc = identity_wait(&cfg);
	if (rc != 0) {
		printk("MBS_MESSAGE_SYSTEM_STARTUP_FAILED stage=identity rc=%d\n", rc);
		return 0;
	}

	bytes_to_hex(cfg.public_key.bytes, cfg.public_key.size, public_key_hex,
		     sizeof(public_key_hex));
	printk("MBS_MESSAGE_SYSTEM_READY role=chat tx_enabled=0 "
	       "frequency_hz=%u power_dbm=%d public_key=%s\n",
	       CONFIG_MBS_RADIO_DEFAULT_FREQUENCY, CONFIG_MBS_RADIO_DEFAULT_TX_POWER,
	       public_key_hex);
	return 0;
}
