/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/llext/symbol.h>
#include <zephyr/meshbus/llext.h>

#if defined(CONFIG_FILE_SYSTEM)
#include <zephyr/fs/fs.h>
#endif

#if defined(CONFIG_MESHBUS_BLUETOOTH)
#include <zephyr/meshbus/bluetooth.h>
#endif
#if defined(CONFIG_MESHBUS_CHANNEL)
#include <zephyr/meshbus/channel.h>
#endif
#if defined(CONFIG_MESHBUS_CLOCK)
#include <zephyr/meshbus/clock.h>
#endif
#if defined(CONFIG_MESHBUS_DISPLAY)
#include <zephyr/meshbus/display.h>
#endif
#if defined(CONFIG_MESHBUS_GNSS)
#include <zephyr/meshbus/gnss.h>
#endif
#if defined(CONFIG_MESHBUS_INDICATOR)
#include <zephyr/meshbus/indicator.h>
#endif
#if defined(CONFIG_MESHBUS_INPUT)
#include <zephyr/meshbus/input.h>
#endif
#if defined(CONFIG_MESHBUS_MESSAGE)
#include <zephyr/meshbus/message.h>
#endif
#if defined(CONFIG_MESHBUS_MESHCORE)
#include <zephyr/meshbus/meshcore.h>
#endif
#if defined(CONFIG_MESHBUS_CONTACT)
#include <zephyr/meshbus/contact.h>
#endif
#if defined(CONFIG_MESHBUS_NOTIFY)
#include <zephyr/meshbus/notify.h>
#endif
#if defined(CONFIG_MESHBUS_POWER)
#include <zephyr/meshbus/power.h>
#endif
#if defined(CONFIG_MESHBUS_RADIO)
#include <zephyr/meshbus/radio.h>
#endif
#if defined(CONFIG_MESHBUS_TELEMETRY)
#include <zephyr/meshbus/telemetry.h>
#endif

#if defined(CONFIG_MESHBUS_BLUETOOTH)
EXPORT_SYMBOL(meshbus_bluetooth_config_get);
EXPORT_SYMBOL(meshbus_bluetooth_config_set);
EXPORT_SYMBOL(meshbus_bluetooth_config_reset);
#endif

#if defined(CONFIG_MESHBUS_CHANNEL)
EXPORT_SYMBOL(meshbus_channel_get);
EXPORT_SYMBOL(meshbus_channel_set);
EXPORT_SYMBOL(meshbus_channel_reset);
EXPORT_SYMBOL(meshbus_channel_next_by_hash);
EXPORT_SYMBOL(meshbus_channel_store_count);
EXPORT_SYMBOL(meshbus_channel_store_size);
EXPORT_SYMBOL(meshbus_channel_next_free_slot);
#endif

#if defined(CONFIG_MESHBUS_CLOCK)
EXPORT_SYMBOL(meshbus_clock_config_get);
EXPORT_SYMBOL(meshbus_clock_config_set);
EXPORT_SYMBOL(meshbus_clock_config_reset);
#endif

#if defined(CONFIG_MESHBUS_DISPLAY)
EXPORT_SYMBOL(meshbus_display_config_get);
EXPORT_SYMBOL(meshbus_display_config_set);
EXPORT_SYMBOL(meshbus_display_config_reset);
EXPORT_SYMBOL(meshbus_display_is_active);
EXPORT_SYMBOL(meshbus_display_active);
#endif

#if defined(CONFIG_MESHBUS_GNSS)
EXPORT_SYMBOL(meshbus_gnss_config_get);
EXPORT_SYMBOL(meshbus_gnss_config_set);
EXPORT_SYMBOL(meshbus_gnss_config_reset);
EXPORT_SYMBOL(meshbus_gnss_state_get);
EXPORT_SYMBOL(meshbus_gnss_acquisition);
EXPORT_SYMBOL(meshbus_gnss_position_get);
EXPORT_SYMBOL(meshbus_gnss_info_get);
EXPORT_SYMBOL(meshbus_gnss_time_get);
EXPORT_SYMBOL(meshbus_gnss_satellites_get);
EXPORT_SYMBOL(meshbus_gnss_satellite_get_by_index);
EXPORT_SYMBOL(meshbus_gnss_satellites_count);
EXPORT_SYMBOL(meshbus_gnss_satellites_cache_clear);
#endif

#if defined(CONFIG_MESHBUS_INDICATOR)
EXPORT_SYMBOL(meshbus_indicator_config_get);
EXPORT_SYMBOL(meshbus_indicator_config_set);
EXPORT_SYMBOL(meshbus_indicator_config_reset);
EXPORT_SYMBOL(meshbus_indicator_light_idle_color);
EXPORT_SYMBOL(meshbus_indicator_light_idle);
EXPORT_SYMBOL(meshbus_indicator_light_color);
EXPORT_SYMBOL(meshbus_indicator_light_play);
EXPORT_SYMBOL(meshbus_indicator_light_stop);
EXPORT_SYMBOL(meshbus_indicator_buzzer_play);
EXPORT_SYMBOL(meshbus_indicator_buzzer_rtttl);
EXPORT_SYMBOL(meshbus_indicator_buzzer_stop);
EXPORT_SYMBOL(meshbus_indicator_light_is_ready);
EXPORT_SYMBOL(meshbus_indicator_buzzer_is_ready);
#endif

#if defined(CONFIG_MESHBUS_INPUT)
EXPORT_SYMBOL(meshbus_input_key_event_publish);
EXPORT_SYMBOL(meshbus_input_action_event_publish);
#endif

#if defined(CONFIG_MESHBUS_MESSAGE)
EXPORT_SYMBOL(meshbus_message_send_to_node);
EXPORT_SYMBOL(meshbus_message_send_to_channel);
EXPORT_SYMBOL(meshbus_message_next);
#endif

#if defined(CONFIG_MESHBUS_MESHCORE)
EXPORT_SYMBOL(meshbus_meshcore_config_set);
EXPORT_SYMBOL(meshbus_meshcore_config_get);
EXPORT_SYMBOL(meshbus_meshcore_config_reset);
EXPORT_SYMBOL(meshbus_meshcore_advert_request);
EXPORT_SYMBOL(meshbus_meshcore_node_discover_request);
#endif

#if defined(CONFIG_MESHBUS_CONTACT)
EXPORT_SYMBOL(meshbus_contact_find_by_key);
EXPORT_SYMBOL(meshbus_contact_find_by_prefix);
EXPORT_SYMBOL(meshbus_contact_next_by_hash);
EXPORT_SYMBOL(meshbus_contact_share_request);
EXPORT_SYMBOL(meshbus_contact_discover_path_request);
EXPORT_SYMBOL(meshbus_contact_trace_path_request);
EXPORT_SYMBOL(meshbus_contact_telemetry_request);
EXPORT_SYMBOL(meshbus_contact_get);
EXPORT_SYMBOL(meshbus_contact_set);
EXPORT_SYMBOL(meshbus_contact_reset);
EXPORT_SYMBOL(meshbus_contact_store_count);
EXPORT_SYMBOL(meshbus_contact_store_size);
#endif

#if defined(CONFIG_MESHBUS_NOTIFY)
EXPORT_SYMBOL(meshbus_notify_publish);
#endif

#if defined(CONFIG_FILE_SYSTEM)
EXPORT_SYMBOL(fs_open);
EXPORT_SYMBOL(fs_read);
EXPORT_SYMBOL(fs_write);
EXPORT_SYMBOL(fs_close);
EXPORT_SYMBOL(fs_mkdir);
EXPORT_SYMBOL(fs_unlink);
#endif

EXPORT_SYMBOL(meshbus_llext_zbus_subscribe);
EXPORT_SYMBOL(meshbus_llext_zbus_unsubscribe);
EXPORT_SYMBOL(meshbus_llext_zbus_take_pending);
EXPORT_SYMBOL(meshbus_llext_zbus_read);
EXPORT_SYMBOL(meshbus_llext_zbus_publish);

#if defined(CONFIG_MESHBUS_POWER)
EXPORT_SYMBOL(meshbus_power_config_get);
EXPORT_SYMBOL(meshbus_power_config_set);
EXPORT_SYMBOL(meshbus_power_config_reset);
EXPORT_SYMBOL(meshbus_power_is_charging);
EXPORT_SYMBOL(meshbus_power_is_online);
EXPORT_SYMBOL(meshbus_power_fuel_gauge_get);
EXPORT_SYMBOL(meshbus_power_shutdown);
EXPORT_SYMBOL(meshbus_power_reboot);
EXPORT_SYMBOL(meshbus_power_reboot_to_bootloader);
#endif

#if defined(CONFIG_MESHBUS_RADIO)
EXPORT_SYMBOL(meshbus_radio_config_get);
EXPORT_SYMBOL(meshbus_radio_config_set);
EXPORT_SYMBOL(meshbus_radio_config_reset);
EXPORT_SYMBOL(meshbus_radio_state_get);
EXPORT_SYMBOL(meshbus_radio_channel_activity);
EXPORT_SYMBOL(meshbus_radio_last_rssi);
EXPORT_SYMBOL(meshbus_radio_last_snr);
EXPORT_SYMBOL(meshbus_radio_noise_floor);
EXPORT_SYMBOL(meshbus_radio_noise_calibrate);
EXPORT_SYMBOL(meshbus_radio_agc_reset);
EXPORT_SYMBOL(meshbus_radio_rssi_inst);
EXPORT_SYMBOL(meshbus_radio_airtime);
EXPORT_SYMBOL(meshbus_radio_packet_score);
#endif

#if defined(CONFIG_MESHBUS_TELEMETRY)
EXPORT_SYMBOL(meshbus_telemetry_config_get);
EXPORT_SYMBOL(meshbus_telemetry_config_set);
EXPORT_SYMBOL(meshbus_telemetry_config_reset);
EXPORT_SYMBOL(meshbus_telemetry_sample_trigger);
EXPORT_SYMBOL(meshbus_telemetry_bindings_count);
EXPORT_SYMBOL(meshbus_telemetry_binding_get);
EXPORT_SYMBOL(meshbus_telemetry_channel_get);
#endif
