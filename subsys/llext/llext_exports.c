/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/llext/symbol.h>
#include <llext/llext.h>
#include <llext/zbus.h>

#if defined(CONFIG_ADC)
#include <zephyr/drivers/adc.h>

/* Native adc_raw_to_*() helpers call these out-of-line gain conversions. */
EXPORT_SYMBOL(adc_gain_invert);
EXPORT_SYMBOL(adc_gain_invert_64);
#endif

#if defined(CONFIG_FILE_SYSTEM)
#include <zephyr/fs/fs.h>
#endif

#if defined(CONFIG_MBS_BLUETOOTH)
#include <bluetooth/bluetooth.h>
#endif
#if defined(CONFIG_MBS_CHANNEL)
#include <channel/channel.h>
#endif
#if defined(CONFIG_MBS_CLOCK)
#include <clock/clock.h>
#endif
#if defined(CONFIG_MBS_DISPLAY)
#include <display/display.h>
#endif
#if defined(CONFIG_MBS_GNSS)
#include <gnss/gnss.h>
#endif
#if defined(CONFIG_MBS_INDICATOR)
#include <indicator/indicator.h>
#endif
#if defined(CONFIG_MBS_INPUT)
#include <input/input.h>
#endif
#if defined(CONFIG_MBS_MESSAGE)
#include <message/message.h>
#endif
#if defined(CONFIG_MBS_MESHCORE)
#include <meshcore/meshcore.h>
#endif
#if defined(CONFIG_MBS_CONTACT)
#include <contact/contact.h>
#endif
#if defined(CONFIG_MBS_NOTIFY)
#include <notify/notify.h>
#endif
#if defined(CONFIG_MBS_POWER)
#include <power/power.h>
#endif
#if defined(CONFIG_MBS_RADIO)
#include <radio/radio.h>
#endif
#if defined(CONFIG_MBS_TELEMETRY)
#include <telemetry/telemetry.h>
#endif

#if defined(CONFIG_MBS_BLUETOOTH)
EXPORT_SYMBOL(mbs_bluetooth_config_get);
EXPORT_SYMBOL(mbs_bluetooth_config_set);
EXPORT_SYMBOL(mbs_bluetooth_config_reset);
#endif

#if defined(CONFIG_MBS_CHANNEL)
EXPORT_SYMBOL(mbs_channel_get);
EXPORT_SYMBOL(mbs_channel_set);
EXPORT_SYMBOL(mbs_channel_reset);
EXPORT_SYMBOL(mbs_channel_next_by_hash);
EXPORT_SYMBOL(mbs_channel_store_count);
EXPORT_SYMBOL(mbs_channel_store_size);
EXPORT_SYMBOL(mbs_channel_next_free_slot);
#endif

#if defined(CONFIG_MBS_CLOCK)
EXPORT_SYMBOL(mbs_clock_config_get);
EXPORT_SYMBOL(mbs_clock_config_set);
EXPORT_SYMBOL(mbs_clock_config_reset);
#endif

#if defined(CONFIG_MBS_DISPLAY)
EXPORT_SYMBOL(mbs_display_config_get);
EXPORT_SYMBOL(mbs_display_config_set);
EXPORT_SYMBOL(mbs_display_config_reset);
EXPORT_SYMBOL(mbs_display_is_active);
EXPORT_SYMBOL(mbs_display_active);
#endif

#if defined(CONFIG_MBS_GNSS)
EXPORT_SYMBOL(mbs_gnss_config_get);
EXPORT_SYMBOL(mbs_gnss_config_set);
EXPORT_SYMBOL(mbs_gnss_config_reset);
EXPORT_SYMBOL(mbs_gnss_state_get);
EXPORT_SYMBOL(mbs_gnss_acquisition);
EXPORT_SYMBOL(mbs_gnss_position_get);
EXPORT_SYMBOL(mbs_gnss_info_get);
EXPORT_SYMBOL(mbs_gnss_time_get);
EXPORT_SYMBOL(mbs_gnss_satellites_get);
EXPORT_SYMBOL(mbs_gnss_satellite_get_by_index);
EXPORT_SYMBOL(mbs_gnss_satellites_count);
EXPORT_SYMBOL(mbs_gnss_satellites_cache_clear);
#endif

#if defined(CONFIG_MBS_INDICATOR)
EXPORT_SYMBOL(mbs_indicator_config_get);
EXPORT_SYMBOL(mbs_indicator_config_set);
EXPORT_SYMBOL(mbs_indicator_config_reset);
EXPORT_SYMBOL(mbs_indicator_light_idle_color);
EXPORT_SYMBOL(mbs_indicator_light_idle);
EXPORT_SYMBOL(mbs_indicator_light_color);
EXPORT_SYMBOL(mbs_indicator_light_play);
EXPORT_SYMBOL(mbs_indicator_light_stop);
EXPORT_SYMBOL(mbs_indicator_buzzer_play);
EXPORT_SYMBOL(mbs_indicator_buzzer_rtttl);
EXPORT_SYMBOL(mbs_indicator_buzzer_stop);
EXPORT_SYMBOL(mbs_indicator_buzzer_play_owned);
EXPORT_SYMBOL(mbs_indicator_buzzer_play_owned_repeat);
EXPORT_SYMBOL(mbs_indicator_buzzer_playing);
EXPORT_SYMBOL(mbs_indicator_buzzer_stop_owned);
EXPORT_SYMBOL(mbs_indicator_light_is_ready);
EXPORT_SYMBOL(mbs_indicator_buzzer_is_ready);
#endif

#if defined(CONFIG_MBS_INPUT)
EXPORT_SYMBOL(mbs_input_key_event_publish);
EXPORT_SYMBOL(mbs_input_action_event_publish);
#endif

#if defined(CONFIG_MBS_MESSAGE)
EXPORT_SYMBOL(mbs_message_send_to_node);
EXPORT_SYMBOL(mbs_message_send_to_channel);
EXPORT_SYMBOL(mbs_message_next);
#endif

#if defined(CONFIG_MBS_MESHCORE)
EXPORT_SYMBOL(mbs_meshcore_config_set);
EXPORT_SYMBOL(mbs_meshcore_config_get);
EXPORT_SYMBOL(mbs_meshcore_active_role_get);
EXPORT_SYMBOL(mbs_meshcore_config_reset);
EXPORT_SYMBOL(mbs_meshcore_advert_request);
EXPORT_SYMBOL(mbs_meshcore_node_discover_request);
#endif

#if defined(CONFIG_MBS_CONTACT)
EXPORT_SYMBOL(mbs_contact_find_by_key);
EXPORT_SYMBOL(mbs_contact_find_by_prefix);
EXPORT_SYMBOL(mbs_contact_next_by_hash);
EXPORT_SYMBOL(mbs_contact_share_request);
EXPORT_SYMBOL(mbs_contact_discover_path_request);
EXPORT_SYMBOL(mbs_contact_trace_path_request);
EXPORT_SYMBOL(mbs_contact_telemetry_request);
EXPORT_SYMBOL(mbs_contact_get);
EXPORT_SYMBOL(mbs_contact_set);
EXPORT_SYMBOL(mbs_contact_reset);
EXPORT_SYMBOL(mbs_contact_store_count);
EXPORT_SYMBOL(mbs_contact_store_size);
#endif

#if defined(CONFIG_MBS_NOTIFY)
EXPORT_SYMBOL(mbs_notify_publish);
#endif

#if defined(CONFIG_FILE_SYSTEM)
EXPORT_SYMBOL(fs_open);
EXPORT_SYMBOL(fs_read);
EXPORT_SYMBOL(fs_seek);
EXPORT_SYMBOL(fs_write);
EXPORT_SYMBOL(fs_close);
EXPORT_SYMBOL(fs_sync);
EXPORT_SYMBOL(fs_rename);
EXPORT_SYMBOL(fs_mkdir);
EXPORT_SYMBOL(fs_unlink);
#endif

EXPORT_SYMBOL(mbs_llext_zbus_subscribe);
EXPORT_SYMBOL(mbs_llext_zbus_unsubscribe);
EXPORT_SYMBOL(mbs_llext_zbus_take_pending);
EXPORT_SYMBOL(mbs_llext_zbus_read);
EXPORT_SYMBOL(mbs_llext_zbus_publish);

#if defined(CONFIG_MBS_POWER)
EXPORT_SYMBOL(mbs_power_config_get);
EXPORT_SYMBOL(mbs_power_config_set);
EXPORT_SYMBOL(mbs_power_config_reset);
EXPORT_SYMBOL(mbs_power_is_charging);
EXPORT_SYMBOL(mbs_power_is_online);
EXPORT_SYMBOL(mbs_power_fuel_gauge_get);
EXPORT_SYMBOL(mbs_power_shutdown);
EXPORT_SYMBOL(mbs_power_reboot);
EXPORT_SYMBOL(mbs_power_reboot_to_bootloader);
#endif

#if defined(CONFIG_MBS_RADIO)
EXPORT_SYMBOL(mbs_radio_config_get);
EXPORT_SYMBOL(mbs_radio_config_set);
EXPORT_SYMBOL(mbs_radio_config_reset);
EXPORT_SYMBOL(mbs_radio_state_get);
EXPORT_SYMBOL(mbs_radio_channel_activity);
EXPORT_SYMBOL(mbs_radio_last_rssi);
EXPORT_SYMBOL(mbs_radio_last_snr);
EXPORT_SYMBOL(mbs_radio_noise_floor);
EXPORT_SYMBOL(mbs_radio_noise_calibrate);
EXPORT_SYMBOL(mbs_radio_agc_reset);
EXPORT_SYMBOL(mbs_radio_rssi_inst);
EXPORT_SYMBOL(mbs_radio_airtime);
EXPORT_SYMBOL(mbs_radio_packet_score);
#endif

#if defined(CONFIG_MBS_TELEMETRY)
EXPORT_SYMBOL(mbs_telemetry_config_get);
EXPORT_SYMBOL(mbs_telemetry_config_set);
EXPORT_SYMBOL(mbs_telemetry_config_reset);
EXPORT_SYMBOL(mbs_telemetry_sample_trigger);
EXPORT_SYMBOL(mbs_telemetry_bindings_count);
EXPORT_SYMBOL(mbs_telemetry_binding_get);
EXPORT_SYMBOL(mbs_telemetry_channel_get);
#endif
