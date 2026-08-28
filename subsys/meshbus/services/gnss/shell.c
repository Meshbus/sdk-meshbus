/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>

#include <zephyr/meshbus/gnss.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include "common/shell.h"
#include "gnss_internal.h"

#define GNSS_HELP_ROOT         SHELL_HELP("GNSS module control and configuration", NULL)
#define GNSS_HELP_STATUS       SHELL_HELP("Show GNSS status", NULL)
#define GNSS_HELP_ENABLE       SHELL_HELP("Enable GNSS", NULL)
#define GNSS_HELP_DISABLE      SHELL_HELP("Disable GNSS", NULL)
#define GNSS_HELP_UPDATE       SHELL_HELP("Trigger immediate GNSS acquisition", NULL)
#define GNSS_HELP_SATS         SHELL_HELP("Show visible satellite details", NULL)
#define GNSS_HELP_CONFIG       SHELL_HELP("GNSS configuration", NULL)
#define GNSS_HELP_CONFIG_GET   SHELL_HELP("Show current configuration", NULL)
#define GNSS_HELP_CONFIG_SET                                                                      \
	SHELL_HELP("Set full GNSS configuration",                                                 \
		   "<enabled> <nav_mode> <fix_rate_hz> <system_mask> <update_interval_ms> <min_active_time_ms> <time_sync> <electronic_compass>")
#define GNSS_HELP_CONFIG_RESET SHELL_HELP("Reset configuration to defaults", NULL)

static const char *fix_status_str(enum gnss_fix_status status)
{
	switch (status) {
	case GNSS_FIX_STATUS_NO_FIX:
		return "no_fix";
	case GNSS_FIX_STATUS_GNSS_FIX:
		return "gnss_fix";
	case GNSS_FIX_STATUS_DGNSS_FIX:
		return "dgnss_fix";
	case GNSS_FIX_STATUS_ESTIMATED_FIX:
		return "estimated_fix";
	default:
		return "unknown";
	}
}

static const char *fix_quality_str(enum gnss_fix_quality quality)
{
	switch (quality) {
	case GNSS_FIX_QUALITY_INVALID:
		return "invalid";
	case GNSS_FIX_QUALITY_GNSS_SPS:
		return "gnss_sps";
	case GNSS_FIX_QUALITY_DGNSS:
		return "dgnss";
	case GNSS_FIX_QUALITY_GNSS_PPS:
		return "gnss_pps";
	case GNSS_FIX_QUALITY_RTK:
		return "rtk";
	case GNSS_FIX_QUALITY_FLOAT_RTK:
		return "float_rtk";
	case GNSS_FIX_QUALITY_ESTIMATED:
		return "estimated";
	default:
		return "unknown";
	}
}

static const char *gnss_system_str(enum gnss_system system)
{
	switch (system) {
	case GNSS_SYSTEM_GPS:
		return "GPS";
	case GNSS_SYSTEM_GLONASS:
		return "GLONASS";
	case GNSS_SYSTEM_GALILEO:
		return "Galileo";
	case GNSS_SYSTEM_BEIDOU:
		return "BeiDou";
	case GNSS_SYSTEM_QZSS:
		return "QZSS";
	case GNSS_SYSTEM_IRNSS:
		return "IRNSS";
	case GNSS_SYSTEM_SBAS:
		return "SBAS";
	case GNSS_SYSTEM_IMES:
		return "IMES";
	default:
		return "Unknown";
	}
}

static const char *nav_mode_str(uint8_t mode)
{
	switch (mode) {
	case 0:
		return "zero_dynamics";
	case 1:
		return "low_dynamics";
	case 2:
		return "balanced_dynamics";
	case 3:
		return "high_dynamics";
	default:
		return "unknown";
	}
}

static const char *state_str(enum meshbus_gnss_state state)
{
	switch (state) {
	case MESHBUS_GNSS_STATE_SLEEP:
		return "sleep";
	case MESHBUS_GNSS_STATE_ACQUIRING:
		return "acquiring";
	case MESHBUS_GNSS_STATE_TRACK:
		return "track";
	case MESHBUS_GNSS_STATE_ERROR:
		return "error";
	default:
		return "unknown";
	}
}

static const char *heading_source_str(uint8_t source)
{
	switch ((enum meshbus_gnss_heading_source)source) {
	case MESHBUS_GNSS_HEADING_SOURCE_ELECTRONIC:
		return "electronic";
	case MESHBUS_GNSS_HEADING_SOURCE_COURSE:
		return "course";
	default:
		return "unspecified";
	}
}

static const char *heading_state_str(uint8_t state)
{
	switch ((enum meshbus_gnss_heading_state)state) {
	case MESHBUS_GNSS_HEADING_STATE_UNAVAILABLE:
		return "unavailable";
	case MESHBUS_GNSS_HEADING_STATE_IDLE:
		return "idle";
	case MESHBUS_GNSS_HEADING_STATE_STARTING:
		return "starting";
	case MESHBUS_GNSS_HEADING_STATE_READY:
		return "ready";
	case MESHBUS_GNSS_HEADING_STATE_WAITING_FOR_FIX:
		return "waiting_for_fix";
	case MESHBUS_GNSS_HEADING_STATE_WAITING_FOR_MOTION:
		return "waiting_for_motion";
	case MESHBUS_GNSS_HEADING_STATE_STALE:
		return "stale";
	case MESHBUS_GNSS_HEADING_STATE_SOURCE_DISABLED:
		return "source_disabled";
	case MESHBUS_GNSS_HEADING_STATE_ERROR:
		return "error";
	default:
		return "unknown";
	}
}

static void print_config(const struct shell *sh, const meshbus_gnss_config *cfg)
{
	shell_print(sh, "Settings:");
	shell_print(sh, "  enabled:         %s", cfg->enabled ? "yes" : "no");
	shell_print(sh, "  nav_mode:        %s (%u)", nav_mode_str(cfg->nav_mode), cfg->nav_mode);
	shell_print(sh, "  fix_rate:        %u Hz", cfg->fix_rate);
	shell_print(sh, "  system_mask:     0x%02x", cfg->system_mask);
	shell_print(sh, "  update_interval: %u ms", cfg->update_interval);
	shell_print(sh, "  min_active_time: %u ms", cfg->min_active_time);
	shell_print(sh, "  time_sync:       %s", cfg->time_sync ? "yes" : "no");
	shell_print(sh, "  electronic_compass: %s",
		    cfg->electronic_compass ? "yes" : "no");
}

static int cmd_gnss_config_get(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_gnss_config cfg;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = meshbus_gnss_config_get(&cfg);
	if (ret != 0) {
		mb_shell_error(sh, ret);
		return ret;
	}

	print_config(sh, &cfg);
	return 0;
}

static int cmd_gnss_config_set(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_gnss_config cfg;
	int ret;
	uint8_t nav_mode = 0;
	uint32_t fix_rate = 0;
	uint32_t system_mask;

	ARG_UNUSED(argc);

	ret = meshbus_gnss_config_get(&cfg);
	if (ret != 0) {
		mb_shell_error(sh, ret);
		return ret;
	}

	ret = mb_shell_parse_bool_arg(argv[1], &cfg.enabled);
	ret |= mb_shell_parse_u8_arg(argv[2], &nav_mode);
	ret |= mb_shell_parse_u32_arg(argv[3], &fix_rate);
	ret |= mb_shell_parse_u32_arg(argv[4], &system_mask);
	ret |= mb_shell_parse_u32_arg(argv[5], &cfg.update_interval);
	ret |= mb_shell_parse_u32_arg(argv[6], &cfg.min_active_time);
	ret |= mb_shell_parse_bool_arg(argv[7], &cfg.time_sync);
	ret |= mb_shell_parse_bool_arg(argv[8], &cfg.electronic_compass);
	if (ret != 0) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	cfg.nav_mode = nav_mode;
	cfg.has_electronic_compass = true;

	if (fix_rate > UINT8_MAX) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}
	cfg.fix_rate = (uint8_t)fix_rate;

	if (system_mask == 0U || system_mask > 0xFFU) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}
	cfg.system_mask = system_mask;

	ret = meshbus_gnss_config_set_full(&cfg);
	if (ret != 0) {
		mb_shell_error(sh, ret);
		return ret;
	}

	return cmd_gnss_config_get(sh, 0, NULL);
}

static int cmd_gnss_config_reset(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = meshbus_gnss_config_reset();
	if (ret != 0) {
		mb_shell_error(sh, ret);
		return ret;
	}

	return cmd_gnss_config_get(sh, 0, NULL);
}

static int cmd_gnss_status(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_gnss_config cfg;
	struct gnss_info info = { 0 };
	struct navigation_data nav;
	struct gnss_time time;
	struct meshbus_gnss_heading_snapshot heading = {0};
	struct meshbus_gnss_heading_runtime_status heading_runtime = {0};
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = meshbus_gnss_config_get(&cfg);
	if (ret != 0) {
		mb_shell_error(sh, ret);
		return ret;
	}

	(void)meshbus_gnss_info_get(&info);

	shell_print(sh, "Status:");
	shell_print(sh, "  enabled:    %s", cfg.enabled ? "yes" : "no");
	shell_print(sh, "  state:      %s", state_str(meshbus_gnss_state_get()));
	shell_print(sh, "  fix_status: %s", fix_status_str(info.fix_status));
	shell_print(sh, "  quality:    %s", fix_quality_str(info.fix_quality));
	shell_print(sh, "  hdop:       %u.%03u", info.hdop / 1000, info.hdop % 1000);
#if defined(CONFIG_GNSS_SATELLITES) && defined(CONFIG_MESHBUS_GNSS_SATELLITE_CACHE_SIZE)
	shell_print(sh, "  tracked: %u", info.satellites_cnt);
	shell_print(sh, "  visible: %u", meshbus_gnss_satellites_count());
#endif

	ret = meshbus_gnss_position_get(&nav);
	if (ret == 0) {
		int64_t lat_deg;
		int64_t lat_frac;
		int64_t lon_deg;
		int64_t lon_frac;

		lat_deg = nav.latitude / 1000000000LL;
		lat_frac = (nav.latitude % 1000000000LL) / 1000LL;
		lon_deg = nav.longitude / 1000000000LL;
		lon_frac = (nav.longitude % 1000000000LL) / 1000LL;
		if (lat_frac < 0) {
			lat_frac = -lat_frac;
		}
		if (lon_frac < 0) {
			lon_frac = -lon_frac;
		}

		shell_print(sh, "Position:");
		shell_print(sh, "  latitude:  %lld.%06lld deg", (long long)lat_deg, (long long)lat_frac);
		shell_print(sh, "  longitude: %lld.%06lld deg", (long long)lon_deg, (long long)lon_frac);
		shell_print(sh, "  altitude:  %d.%03u m", nav.altitude / 1000,
			    (unsigned int)abs(nav.altitude % 1000));
		shell_print(sh, "  speed:     %u.%03u m/s", nav.speed / 1000, nav.speed % 1000);
		shell_print(sh, "  bearing:   %u.%03u deg", nav.bearing / 1000, nav.bearing % 1000);
	}

	if (meshbus_gnss_heading_snapshot_get(&heading) == 0 &&
	    meshbus_gnss_heading_runtime_status_get(&heading_runtime) == 0) {
		shell_print(sh, "Heading:");
		shell_print(sh, "  source:     %s", heading_source_str(heading.source));
		shell_print(sh, "  state:      %s", heading_state_str(heading.state));
		shell_print(sh, "  available:  %s",
			    heading_runtime.electronic_available ? "yes" : "no");
		shell_print(sh, "  active:     %s", heading_runtime.active ? "yes" : "no");
		shell_print(sh, "  valid:      %s", heading.valid ? "yes" : "no");
		if (heading.valid) {
			shell_print(sh, "  direction:  %d.%03u deg",
				    heading.heading_milli_deg / 1000,
				    (unsigned int)abs(heading.heading_milli_deg % 1000));
		}
	}

	ret = meshbus_gnss_time_get(&time);
	if (ret == 0) {
		shell_print(sh, "UTC:");
		shell_print(sh, "  %04u-%02u-%02u %02u:%02u:%02u.%03u", 2000 + time.century_year,
			    time.month, time.month_day, time.hour, time.minute,
			    time.millisecond / 1000, time.millisecond % 1000);
	}

	return 0;
}

static int cmd_gnss_sats(const struct shell *sh, size_t argc, char **argv)
{
#if defined(CONFIG_GNSS_SATELLITES) && defined(CONFIG_MESHBUS_GNSS_SATELLITE_CACHE_SIZE)
	struct gnss_satellite sat;
	uint16_t sat_count;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	sat_count = meshbus_gnss_satellites_count();
	if (sat_count == 0U) {
		shell_print(sh, "No satellites visible");
		return 0;
	}

	shell_print(sh, "Satellites (Visible %u):", sat_count);
	shell_print(sh, "  PRN  SYS      SNR   EL   AZ  tracked corrected");
	for (uint16_t i = 0; i < sat_count; i++) {
		int ret = meshbus_gnss_satellite_get_by_index(i, &sat);
		if (ret != 0) {
			mb_shell_error(sh, -EAGAIN);
			return -EAGAIN;
		}
		shell_print(sh, "  %-4u %-8s %-4u %-4u %-4u %-7s %-8s", sat.prn,
			    gnss_system_str(sat.system), sat.snr, sat.elevation, sat.azimuth,
			    sat.is_tracked ? "yes" : "no", sat.is_corrected ? "yes" : "no");
	}

	return 0;
#else
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return -ENOTSUP;
#endif
}

static int cmd_gnss_enable(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_gnss_config cfg;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = meshbus_gnss_config_get(&cfg);
	if (ret != 0) {
		mb_shell_error(sh, ret);
		return ret;
	}

	if (!cfg.enabled) {
		cfg.enabled = true;
		ret = meshbus_gnss_config_set(&cfg);
		if (ret != 0) {
			mb_shell_error(sh, ret);
			return ret;
		}
	}

	return 0;
}

static int cmd_gnss_disable(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_gnss_config cfg;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = meshbus_gnss_config_get(&cfg);
	if (ret != 0) {
		mb_shell_error(sh, ret);
		return ret;
	}

	if (cfg.enabled) {
		cfg.enabled = false;
		ret = meshbus_gnss_config_set(&cfg);
		if (ret != 0) {
			mb_shell_error(sh, ret);
			return ret;
		}
	}

	return 0;
}

static int cmd_gnss_update(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = meshbus_gnss_acquisition();
	if (ret != 0) {
		mb_shell_error(sh, ret);
		return ret;
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	meshbus_gnss_config_subcmds,
	SHELL_CMD_ARG(get, NULL, GNSS_HELP_CONFIG_GET, cmd_gnss_config_get, 1, 0),
	SHELL_CMD_ARG(set, NULL, GNSS_HELP_CONFIG_SET, cmd_gnss_config_set, 9, 0),
	SHELL_CMD_ARG(reset, NULL, GNSS_HELP_CONFIG_RESET, cmd_gnss_config_reset, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_SET_CREATE(meshbus_gnss_subcmds, (meshbus, gnss));

SHELL_SUBCMD_ADD((meshbus, gnss), status, NULL, GNSS_HELP_STATUS, cmd_gnss_status, 1, 0);
SHELL_SUBCMD_ADD((meshbus, gnss), config, &meshbus_gnss_config_subcmds, GNSS_HELP_CONFIG, NULL, 0,
		 0);
SHELL_SUBCMD_ADD((meshbus, gnss), enable, NULL, GNSS_HELP_ENABLE, cmd_gnss_enable, 1, 0);
SHELL_SUBCMD_ADD((meshbus, gnss), disable, NULL, GNSS_HELP_DISABLE, cmd_gnss_disable, 1, 0);
SHELL_SUBCMD_ADD((meshbus, gnss), update, NULL, GNSS_HELP_UPDATE, cmd_gnss_update, 1, 0);
SHELL_SUBCMD_ADD((meshbus, gnss), sats, NULL, GNSS_HELP_SATS, cmd_gnss_sats, 1, 0);

SHELL_SUBCMD_ADD((meshbus), gnss, &meshbus_gnss_subcmds, GNSS_HELP_ROOT, NULL, 0, 0);
