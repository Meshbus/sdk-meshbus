/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/drivers/gnss.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/sys/util.h>

#include "common/mgmt.h"
#include "meshbus/gnss.pb.h"
#include "gnss_internal.h"

#include <zephyr/meshbus/gnss.h>

LOG_MODULE_REGISTER(meshbus_gnss_mgmt, CONFIG_MESHBUS_GNSS_LOG_LEVEL);

#define MESHBUS_GNSS_MGMT_PROTO_RSP_MAX_SIZE MESHBUS_MESHBUS_GNSS_PB_H_MAX_SIZE

#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
static int meshbus_gnss_mgmt_translate_error_code(uint16_t err)
{
	return (int)err;
}
#endif

static int meshbus_gnss_mgmt_status(struct smp_streamer *ctxt)
{
	meshbus_GnssStatusRequest req = meshbus_GnssStatusRequest_init_zero;
	meshbus_GnssStatusResponse rsp = meshbus_GnssStatusResponse_init_zero;
	meshbus_gnss_config cfg;
	struct gnss_info info = {0};
	struct navigation_data nav = {0};
	struct gnss_time time = {0};
	struct meshbus_gnss_heading_snapshot heading = {0};
	struct meshbus_gnss_heading_runtime_status heading_runtime = {0};
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_GnssStatusRequest_fields,
				      true);

	if (rc != 0) {
		return rc;
	}

	rc = meshbus_gnss_config_get(&cfg);
	if (rc != 0) {
		return rc;
	}

	(void)meshbus_gnss_info_get(&info);

	rsp.has_config = true;
	rsp.config = cfg;
	rsp.state = (meshbus_GnssState)meshbus_gnss_state_get();
	rsp.fix_status = (meshbus_GnssFixStatus)info.fix_status;
	rsp.fix_quality = (meshbus_GnssFixQuality)info.fix_quality;
	rsp.tracked_satellites_count = info.satellites_cnt;
	rsp.hdop_milli = info.hdop;
	rsp.geoid_separation_mm = info.geoid_separation;

	rc = meshbus_gnss_position_get(&nav);
	if (rc == 0) {
		rsp.has_nav = true;
		rsp.nav.latitude = nav.latitude;
		rsp.nav.longitude = nav.longitude;
		rsp.nav.altitude_mm = nav.altitude;
		rsp.nav.speed_mm_per_s = nav.speed;
		rsp.nav.bearing_milli_deg = nav.bearing;
	}

	rc = meshbus_gnss_time_get(&time);
	if (rc == 0) {
		rsp.has_utc = true;
		rsp.utc.century_year = time.century_year;
		rsp.utc.month = time.month;
		rsp.utc.month_day = time.month_day;
		rsp.utc.hour = time.hour;
		rsp.utc.minute = time.minute;
		rsp.utc.millisecond = time.millisecond;
	}

	if (meshbus_gnss_heading_snapshot_get(&heading) == 0 &&
	    meshbus_gnss_heading_runtime_status_get(&heading_runtime) == 0) {
		rsp.has_heading = true;
		rsp.heading.sequence = heading.sequence;
		rsp.heading.source_timestamp_ms = heading.source_timestamp_ms;
		rsp.heading.heading_milli_deg = heading.heading_milli_deg;
		rsp.heading.last_error = heading.last_error;
		rsp.heading.source = (meshbus_GnssHeadingSource)heading.source;
		rsp.heading.state = (meshbus_GnssHeadingState)heading.state;
		rsp.heading.accuracy = (meshbus_GnssHeadingAccuracy)heading.accuracy;
		rsp.heading.calibration_hint =
			(meshbus_GnssHeadingCalibrationHint)heading.calibration_hint;
		rsp.heading.electronic_available = heading_runtime.electronic_available;
		rsp.heading.active = heading_runtime.active;
		rsp.heading.valid = heading.valid;
	}

#if defined(CONFIG_GNSS_SATELLITES) && defined(CONFIG_MESHBUS_GNSS_SATELLITE_CACHE_SIZE)
	{
		uint16_t sat_count = meshbus_gnss_satellites_count();

		if (sat_count > ARRAY_SIZE(rsp.satellites)) {
			LOG_ERR("GNSS satellites exceed protobuf max qty (%u > %u)",
				(unsigned int)sat_count, (unsigned int)ARRAY_SIZE(rsp.satellites));
			return -EOVERFLOW;
		}

		rsp.satellites_count = sat_count;
		for (uint16_t i = 0U; i < sat_count; i++) {
			struct gnss_satellite sat;

			rc = meshbus_gnss_satellite_get_by_index(i, &sat);
			if (rc != 0) {
				return -EAGAIN;
			}

			rsp.satellites[i].prn = sat.prn;
			rsp.satellites[i].system = (meshbus_GnssSystem)sat.system;
			rsp.satellites[i].snr_db = sat.snr;
			rsp.satellites[i].elevation_deg = sat.elevation;
			rsp.satellites[i].azimuth_deg = sat.azimuth;
			rsp.satellites[i].is_tracked = sat.is_tracked;
			rsp.satellites[i].is_corrected = sat.is_corrected;
		}
	}
#endif

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_GnssStatusResponse_fields,
				    MESHBUS_GNSS_MGMT_PROTO_RSP_MAX_SIZE);
}

MB_MGMT_CONFIG_GET_HANDLER_DEFINE(meshbus_gnss_mgmt_config_get, meshbus_GnssConfigGetRequest,
				      meshbus_GnssConfigGetResponse, meshbus_gnss_config, meshbus_gnss_config_get,
				      meshbus_GnssConfigGetRequest_fields,
				      meshbus_GnssConfigGetResponse_fields, MESHBUS_GNSS_MGMT_PROTO_RSP_MAX_SIZE);

MB_MGMT_CONFIG_SET_HANDLER_DEFINE_NO_VALIDATE(
	meshbus_gnss_mgmt_config_set, meshbus_GnssConfigSetRequest,
	meshbus_GnssConfigSetResponse, meshbus_gnss_config, meshbus_gnss_config_set_full,
	meshbus_GnssConfigSetRequest_fields, meshbus_GnssConfigSetResponse_fields,
	MESHBUS_GNSS_MGMT_PROTO_RSP_MAX_SIZE);

MB_MGMT_CONFIG_RESET_HANDLER_DEFINE(meshbus_gnss_mgmt_config_reset, meshbus_GnssConfigResetRequest,
					meshbus_GnssConfigResetResponse, meshbus_gnss_config, meshbus_gnss_config_reset,
					meshbus_gnss_config_get, meshbus_GnssConfigResetRequest_fields,
					meshbus_GnssConfigResetResponse_fields, MESHBUS_GNSS_MGMT_PROTO_RSP_MAX_SIZE);

static int meshbus_gnss_mgmt_enable(struct smp_streamer *ctxt)
{
	meshbus_GnssEnableRequest req = meshbus_GnssEnableRequest_init_zero;
	meshbus_GnssEnableResponse rsp = meshbus_GnssEnableResponse_init_zero;
	meshbus_gnss_config cfg;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_GnssEnableRequest_fields,
				      true);

	if (rc != 0) {
		return rc;
	}

	rc = meshbus_gnss_config_get(&cfg);
	if (rc != 0) {
		return rc;
	}

	cfg.enabled = req.enabled;

	rc = meshbus_gnss_config_set(&cfg);
	if (rc != 0) {
		return rc;
	}

	rsp.has_config = true;
	rsp.config = cfg;

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_GnssEnableResponse_fields,
				    MESHBUS_GNSS_MGMT_PROTO_RSP_MAX_SIZE);
}

static int meshbus_gnss_mgmt_update(struct smp_streamer *ctxt)
{
	meshbus_GnssUpdateRequest req = meshbus_GnssUpdateRequest_init_zero;
	meshbus_GnssUpdateResponse rsp = meshbus_GnssUpdateResponse_init_zero;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_GnssUpdateRequest_fields,
				      true);

	if (rc != 0) {
		return rc;
	}

	rc = meshbus_gnss_acquisition();
	if (rc != 0) {
		return rc;
	}

	rsp.triggered = true;

	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_GnssUpdateResponse_fields,
				    MESHBUS_GNSS_MGMT_PROTO_RSP_MAX_SIZE);
}

static int meshbus_gnss_mgmt_satellites_clear(struct smp_streamer *ctxt)
{
	meshbus_GnssSatellitesClearRequest req =
		meshbus_GnssSatellitesClearRequest_init_zero;
	meshbus_GnssSatellitesClearResponse rsp =
		meshbus_GnssSatellitesClearResponse_init_zero;
	int rc = mb_mgmt_decode_proto(
		ctxt, &req, sizeof(req),
		meshbus_GnssSatellitesClearRequest_fields, true);

	if (rc != 0) {
		return rc;
	}

	meshbus_gnss_satellites_cache_clear();
	rsp.cleared = true;
	return mb_mgmt_encode_proto(ctxt, &rsp,
				    meshbus_GnssSatellitesClearResponse_fields,
				    MESHBUS_GNSS_MGMT_PROTO_RSP_MAX_SIZE);
}

static const struct mgmt_handler meshbus_gnss_mgmt_group_handlers[] = {
	[meshbus_GnssMgmtCommandId_GNSS_MGMT_COMMAND_ID_STATUS] = {meshbus_gnss_mgmt_status, NULL},
	[meshbus_GnssMgmtCommandId_GNSS_MGMT_COMMAND_ID_CONFIG] = {meshbus_gnss_mgmt_config_get,
								   meshbus_gnss_mgmt_config_set},
	[meshbus_GnssMgmtCommandId_GNSS_MGMT_COMMAND_ID_CONFIG_RESET] =
		{NULL, meshbus_gnss_mgmt_config_reset},
	[meshbus_GnssMgmtCommandId_GNSS_MGMT_COMMAND_ID_ENABLE] = {NULL, meshbus_gnss_mgmt_enable},
	[meshbus_GnssMgmtCommandId_GNSS_MGMT_COMMAND_ID_UPDATE] = {NULL, meshbus_gnss_mgmt_update},
	[meshbus_GnssMgmtCommandId_GNSS_MGMT_COMMAND_ID_SATELLITES_CLEAR] = {
		NULL, meshbus_gnss_mgmt_satellites_clear
	},
};

#define MESHBUS_GNSS_MGMT_GROUP_SZ ARRAY_SIZE(meshbus_gnss_mgmt_group_handlers)

static struct mgmt_group meshbus_gnss_mgmt_group = {
	.mg_handlers = meshbus_gnss_mgmt_group_handlers,
	.mg_handlers_count = MESHBUS_GNSS_MGMT_GROUP_SZ,
	.mg_group_id = meshbus_GnssMgmtGroupId_GNSS_MGMT_GROUP_ID_MESHBUS_GNSS,
#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
	.mg_translate_error = meshbus_gnss_mgmt_translate_error_code,
#endif
#ifdef CONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME
	.mg_group_name = "meshbus gnss mgmt",
#endif
};

static void meshbus_gnss_mgmt_register_group(void)
{
	mgmt_register_group(&meshbus_gnss_mgmt_group);
}

MCUMGR_HANDLER_DEFINE(meshbus_gnss_mgmt, meshbus_gnss_mgmt_register_group);
