/* Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */
#include "environment.h"
extern "C" {
#include "air_quality.h"
}
#include <cmath>
#include <cstdio>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <app/clusters/air-quality-server/CodegenIntegration.h>
#include <app/clusters/concentration-measurement-server/CodegenIntegration.h>
#include <app/clusters/identify-server/CodegenIntegration.h>
#include <app/clusters/pressure-measurement-server/CodegenIntegration.h>
#include <app/clusters/relative-humidity-measurement-server/CodegenIntegration.h>
#include <app/clusters/temperature-measurement-server/CodegenIntegration.h>
#include <platform/CHIPDeviceLayer.h>

LOG_MODULE_REGISTER(environment, LOG_LEVEL_INF);

namespace {
using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using DataModel::Nullable;
using Concentration = ConcentrationMeasurement::Instance<true, false, false, false, false, false>;
using Unit = ConcentrationMeasurement::MeasurementUnitEnum;
constexpr auto kAir = ConcentrationMeasurement::MeasurementMediumEnum::kAir;
constexpr EndpointId kTemperature = 1, kHumidity = 2, kAirQuality = 3, kPressure = 4;
const device *const sht40 = DEVICE_DT_GET(DT_NODELABEL(sht40));
const device *const pressure = DEVICE_DT_GET(DT_NODELABEL(matter_pressure));
const device *const particles = DEVICE_DT_GET(DT_NODELABEL(matter_particles));

/* This prototype has no physical identification indicator. */
::Identify identify_temperature(kTemperature, nullptr, nullptr, chip::app::Clusters::Identify::IdentifyTypeEnum::kNone);
::Identify identify_humidity(kHumidity, nullptr, nullptr, chip::app::Clusters::Identify::IdentifyTypeEnum::kNone);
::Identify identify_air(kAirQuality, nullptr, nullptr, chip::app::Clusters::Identify::IdentifyTypeEnum::kNone);
::Identify identify_pressure(kPressure, nullptr, nullptr, chip::app::Clusters::Identify::IdentifyTypeEnum::kNone);
AirQuality::Instance air_quality(kAirQuality, BitMask<AirQuality::Feature>(
	AirQuality::Feature::kFair, AirQuality::Feature::kModerate, AirQuality::Feature::kVeryPoor));
Concentration eco2(kAirQuality, CarbonDioxideConcentrationMeasurement::Id, kAir, Unit::kPpm);
Concentration tvoc(kAirQuality, TotalVolatileOrganicCompoundsConcentrationMeasurement::Id, kAir, Unit::kPpb);
Concentration pm1(kAirQuality, Pm1ConcentrationMeasurement::Id, kAir, Unit::kUgm3);
Concentration pm25(kAirQuality, Pm25ConcentrationMeasurement::Id, kAir, Unit::kUgm3);
Concentration pm10(kAirQuality, Pm10ConcentrationMeasurement::Id, kAir, Unit::kUgm3);
Concentration *const concentrations[] = {&eco2, &tvoc, &pm1, &pm25, &pm10};
constexpr sensor_channel pm_channels[] = {SENSOR_CHAN_PM_1_0, SENSOR_CHAN_PM_2_5, SENSOR_CHAN_PM_10};

int fetch(const device *dev)
{
	return device_is_ready(dev) ? sensor_sample_fetch(dev) : -ENODEV;
}

template <typename T>
Nullable<T> scaled(const device *dev, sensor_channel channel, int64_t divisor,
		   int64_t minimum, int64_t maximum)
{
	Nullable<T> result;
	sensor_value value;
	if (sensor_channel_get(dev, channel, &value) == 0) {
		int64_t micro = sensor_value_to_micro(&value);
		if (micro >= minimum * divisor && micro <= maximum * divisor) {
			result.SetNonNull(static_cast<T>(micro / divisor));
		}
	}
	return result;
}

void check(CHIP_ERROR err)
{
	if (err != CHIP_NO_ERROR) {
		LOG_ERR("Matter measurement update failed: %" CHIP_ERROR_FORMAT, err.Format());
	}
}
} // namespace

CHIP_ERROR environment_init()
{
	CHIP_ERROR err = TemperatureMeasurement::SetMeasuredValueRange(kTemperature,
		Nullable<int16_t>(-4000), Nullable<int16_t>(12500));
	if (err != CHIP_NO_ERROR) {
		return err;
	}
	err = air_quality.Init();
	if (err != CHIP_NO_ERROR) {
		return err;
	}
	for (auto *cluster : concentrations) {
		err = cluster->Init();
		if (err != CHIP_NO_ERROR) {
			return err;
		}
	}
	return CHIP_NO_ERROR;
}

void environment_update()
{
	Nullable<int16_t> temperature_value, pressure_value;
	Nullable<uint16_t> humidity_value;
	Nullable<float> values[5];
	AirQuality::AirQualityEnum quality = AirQuality::AirQualityEnum::kUnknown;
	static bool air_initialized;
	static air_reading last_air{};
	static int64_t last_air_time = -1;
	int sht_rc = fetch(sht40);
	int pressure_rc = fetch(pressure);
	int pm_rc = fetch(particles);

	if (sht_rc == 0) {
		temperature_value = scaled<int16_t>(sht40, SENSOR_CHAN_AMBIENT_TEMP, 10000, -4000, 12500);
		humidity_value = scaled<uint16_t>(sht40, SENSOR_CHAN_HUMIDITY, 10000, 0, 10000);
	}
	if (pressure_rc == 0) {
		/* Zephyr kPa -> Matter 0.1 kPa (hPa). */
		pressure_value = scaled<int16_t>(pressure, SENSOR_CHAN_PRESS, 100000, 260, 1260);
	}
	if (pm_rc == 0) {
		for (size_t i = 0; i < 3; ++i) {
			sensor_value value;
			if (sensor_channel_get(particles, pm_channels[i], &value) == 0) {
				float concentration = static_cast<float>(sensor_value_to_double(&value));
				if (std::isfinite(concentration) && concentration >= 0) {
					values[i + 2].SetNonNull(concentration);
				}
			}
		}
	}
	int air_rc = 0;
	if (!air_initialized) {
		air_rc = air_init();
		air_initialized = air_rc == 0;
	}
	if (air_initialized) {
		if (!temperature_value.IsNull() && !humidity_value.IsNull()) {
			sensor_value t, rh;
			if (sensor_channel_get(sht40, SENSOR_CHAN_AMBIENT_TEMP, &t) == 0 &&
			    sensor_channel_get(sht40, SENSOR_CHAN_HUMIDITY, &rh) == 0) {
				int rc = air_compensate(&t, &rh);
				if (rc != 0 && rc != -ERANGE) {
					LOG_WRN("ENS160 compensation failed: %d", rc);
				}
			}
		}
		air_reading reading{};
		air_rc = air_read(&reading);
		if (air_rc == 0 && reading.validity == 0) {
			last_air = reading;
			last_air_time = k_uptime_get();
		} else if (air_rc != -EAGAIN) {
			last_air_time = -1;
			if (air_rc != 0) {
				air_initialized = false;
			} else {
				LOG_INF("ENS160 conditioning/invalid state: %u", reading.validity);
			}
		}
	}
	/* No-new-data is transient; expire old readings after three sample periods. */
	if (last_air_time >= 0 && k_uptime_get() - last_air_time < 15000) {
		values[0].SetNonNull(last_air.eco2);
		values[1].SetNonNull(last_air.tvoc);
		quality = static_cast<AirQuality::AirQualityEnum>(last_air.aqi);
	}

	DeviceLayer::PlatformMgr().LockChipStack();
	check(TemperatureMeasurement::SetMeasuredValue(kTemperature, temperature_value));
	check(RelativeHumidityMeasurement::SetMeasuredValue(kHumidity, humidity_value));
	check(PressureMeasurement::SetMeasuredValue(kPressure, pressure_value));
	for (size_t i = 0; i < 5; ++i) {
		check(concentrations[i]->SetMeasuredValue(values[i]));
	}
	if (air_quality.UpdateAirQuality(quality) != Protocols::InteractionModel::Status::Success) {
		LOG_ERR("Matter air quality update failed");
	}
	DeviceLayer::PlatformMgr().UnlockChipStack();

	/* Integer diagnostics avoid requiring floating-point printf. -1 means unknown. */
	std::printf("Environment: T=%d centi-C RH=%d centi-%% P=%d hPa AQ=%u "
		    "eCO2=%d ppm TVOC=%d ppb PM1/2.5/10=%d/%d/%d milli-ug/m3\n",
		temperature_value.IsNull() ? -32768 : temperature_value.Value(),
		humidity_value.IsNull() ? -1 : humidity_value.Value(),
		pressure_value.IsNull() ? -1 : pressure_value.Value(), static_cast<unsigned>(quality),
		values[0].IsNull() ? -1 : static_cast<int>(values[0].Value()),
		values[1].IsNull() ? -1 : static_cast<int>(values[1].Value()),
		values[2].IsNull() ? -1 : static_cast<int>(values[2].Value() * 1000),
		values[3].IsNull() ? -1 : static_cast<int>(values[3].Value() * 1000),
		values[4].IsNull() ? -1 : static_cast<int>(values[4].Value() * 1000));
	if (sht_rc || pressure_rc || pm_rc || (air_rc && air_rc != -EAGAIN)) {
		LOG_WRN("Sensor status: SHT40=%d LPS22HB=%d SPS30=%d ENS160=%d",
			sht_rc, pressure_rc, pm_rc, air_rc);
	}
}
