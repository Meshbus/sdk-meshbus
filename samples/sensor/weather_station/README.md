# LED matrix weather station

Standalone sample for `devkit_esp32c6/esp32c6/hpcore`. The 12x8 Modulino
matrix scrolls nine pages: temperature (C), relative humidity (%), pressure
(hPa), equivalent CO2 (ppm), TVOC (ppb), ENS160 UBA AQI (1..5), and SPS30 PM1.0, PM2.5 and PM10 (ug/m3).
Each message scrolls once before advancing. Press GPIO9's active-low button
to interrupt the current message and start the next page immediately after
25 ms debounce. Holding the button advances once; release before pressing again.

## Connections

All devices share the board's I2C0 bus on GPIO22/SDA and GPIO23/SCL at 100 kHz.
The sample overlay configures the user-confirmed sensor models and addresses:

| Device | 7-bit address | Use |
| --- | --- | --- |
| SHT40 | `0x44` | Temperature and humidity |
| LPS22HBTR | `0x5c` | Pressure |
| ENS160 | `0x52` | eCO2, TVOC and AQI |
| SPS30 | `0x69` | PM1.0, PM2.5 and PM10 |
| Modulino LED Matrix | `0x39` | Display, supplied by board definition |

Connect the button between GPIO9 and GND; the sample enables its pull-up.
GPIO9 is also the boot strap, so release it during reset/power-on for normal
application boot. This sample adds its own button node without changing the
board's shared configuration.

## Operation

A separate thread samples every two seconds. The display thread polls the
button every five milliseconds and scrolls one column every 80 milliseconds.
The button does not wait for the sampling thread or the current scroll to end.
A page snapshots the latest readings when it begins, keeping its text stable
throughout the scroll. Units and labels are included in the scrolling text.

Missing/failed sensors display `ERR` on their pages while other pages continue.
No new sample displays `WAIT`; stale readings are not shown as current values.
ENS160 conditioning is shown as `WARM` or `INIT`, rather than a numeric value.
`INIT` is the sensor's validity flag 2: initial conditioning can take one hour
of operation. `WARM` is flag 1: normal warm-up can take three minutes.
Keep the sensor continuously powered. The datasheet notes that initial
conditioning completion is stored in nonvolatile memory only after 24 hours
of continuous operation; earlier power-off can cause `INIT` again on restart.
The sample follows the reported status, not a countdown, and starts showing
numeric readings once valid new data is available.
The GPIO and matrix are required; a failure stops the display loop and prints
`WEATHER_FATAL`. Serial output includes sample status, displayed values and
`WEATHER_BUTTON page=...` events.

SHT40 readings feed ENS160 temperature/humidity compensation when within its
recommended compensation range. Failed/out-of-range compensation is logged;
the sensor retains its previous compensation setting. eCO2 is an estimate
from the gas sensor, not a direct CO2 measurement.

The workspace ENS160 driver rejects conditioning during initialization and
does not expose validity through the Sensor API. This sample therefore owns
ENS160 through its small `air_quality.c` I2C adapter, with `CONFIG_ENS160=n`.
It validates part ID, selects standard mode, checks validity/new-data flags,
and reads the AQI/TVOC/eCO2 registers together. SHT40 and LPS22HB use the
standard Zephyr Sensor API. No shared Zephyr driver is changed.
The register and conditioning contract follows the
[ScioSense ENS160 datasheet v1.3](https://www.sciosense.com/wp-content/uploads/2023/12/ENS160-Datasheet.pdf).

## Build and run

From the west workspace, with the standard environment activated:

```sh
west build -b devkit_esp32c6/esp32c6/hpcore \
  meshbus/samples/sensor/weather_station -d build/weather-station-esp32c6
west flash -d build/weather-station-esp32c6 \
  --runner esp32 --esp-device <confirmed-serial-port>
```

The console is native USB Serial/JTAG, 115200 baud. Expect `WEATHER_READY`,
then repeating `WEATHER sample` and `WEATHER page` messages. Check each of the
nine scrolling values, press during a scroll, hold, release and press again.
The button should advance once per press, including while ENS160 conditions.
Disconnecting a sensor should show its error without stopping page selection.
Build-only sample metadata does not assert physical readings or button behavior.

## SPS30

Supply SPS30 with 5 V, connect SEL to GND for I2C, and share ground with the
host. Use the existing GPIO22/23 I2C bus at 100 kHz. Matrix pages use `UG/M3`
for micrograms per cubic meter. The module driver exposes standard Sensor API
channels `SENSOR_CHAN_PM_1_0`, `SENSOR_CHAN_PM_2_5`, and `SENSOR_CHAN_PM_10`.
PM4, number concentration, particle size, sleep and manual fan cleaning are
not exposed by this driver. Its normal automatic cleaning configuration is
left unchanged.

Initialization resets only SPS30 and starts float-format measurement. The
sensor must be powered and awake at host startup. Data-ready polling returns
`-EAGAIN` when a new measurement is unavailable; the UI shows `WAIT`. Every
16-bit word of the full response is CRC-checked. Malformed values or CRC errors
reject the frame without overwriting the last accepted driver sample; the
application displays `ERR` on failed fetches rather than treating old values
as fresh. A missing SPS30 does not stop other sensors or the button.

The implementation follows the
[SPS30 datasheet](https://sensirion.com/file/datasheet_sps30) and
[Sensirion I2C reference library](https://github.com/Sensirion/embedded-i2c-sps30).
A protocol emulator test is available under `tests/drivers/sensor/sps30` for
`qemu_x86`; this checks software behavior, not particle measurement accuracy.

## Bluetooth LE publication

The device advertises as **FoBE Weather** with a custom connectable GATT
service. Connect with a BLE GATT client, read a characteristic or enable its
notifications. One client is supported at a time. Advertising resumes after
disconnection. Data is read-only and available without pairing. The matrix and
button continue operating independently of a BLE connection. Sensor values
are delivered over GATT; advertising carries the service UUID and device name.

Service UUID: `8e7a0000-6d4b-4f23-9a71-5c2e9f0b3401`.
Characteristic UUIDs share the suffix `-6d4b-4f23-9a71-5c2e9f0b3401`:

| UUID prefix | Contents | Length |
| --- | --- | --- |
| `8e7a0001` | Temperature, humidity, pressure | 16 bytes |
| `8e7a0002` | eCO2, TVOC, AQI, ENS160 validity | 14 bytes |
| `8e7a0003` | PM1.0, PM2.5, PM10 | 20 bytes |

All integers are little-endian. Notifications are sent after each sampling
cycle (approximately two seconds); all three carry the same sequence number.
The packets fit the default ATT MTU without fragmentation. Notifications are
not acknowledged; consumers can detect missed updates using sequence numbers
and must discard samples that stop updating. Read each characteristic for the
latest snapshot; separate reads may span two cycles, so compare sequences.

Common header:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | u8 | Protocol version, currently 1 |
| 1 | u8 | Validity bits, below; all other bits reserved |
| 2 | u16 | Sample sequence, wraps at 65536; restarts after host boot |
| 4 | u32 | Sample uptime in milliseconds, wraps at 2^32; not wall-clock time |

Environment payload: signed i16 temperature at offset 8, in 0.01 C; u16 RH
at 10, in 0.01%; u32 pressure at 12, in Pa. Header bit 0 validates both
T/RH, bit 1 validates pressure independently.

Air payload: u16 eCO2 at 8 in ppm, u16 TVOC at 10 in ppb, u8 AQI at 12.
Header bit 0 validates these readings. Byte 13 is the ENS160 validity report:
0 normal, 1 warm-up, 2 initial conditioning, 3 invalid, 255 unavailable.
Conditioning is transmitted with the measurement-valid bit clear.

PM payload: u32 PM1.0 at 8, PM2.5 at 12, PM10 at 16, each in 0.001 ug/m3.
Header bit 0 validates the three mass concentrations together.

Ignore measurement fields whose validity bit is clear, even if nonzero.
Reads clear validity bits if the snapshot is older than six seconds; a
connected client should apply its own timeout to cached notifications.
The protocol encoding tests are `tests/samples/weather_station` on `qemu_x86`.

### Readable text for LightBlue on iPhone

Nine additional characteristics expose ASCII text (also valid UTF-8). In
LightBlue, connect to **FoBE Weather**, open the custom service, select a text
characteristic, choose UTF-8/string display and enable notifications. Text
updates approximately every two seconds. Each value fits within 20 bytes,
without a terminating NUL on the wire, and is independently readable.
The Characteristic User Description gives each text characteristic a name.

| UUID prefix | Description | Example |
| --- | --- | --- |
| `8e7a0011` | Temperature text | `TEMP 26.95 C` |
| `8e7a0012` | Humidity text | `RH 76.39 %` |
| `8e7a0013` | Pressure text | `PRESS 1008.39 hPa` |
| `8e7a0014` | eCO2 text | `eCO2 600 ppm` |
| `8e7a0015` | TVOC text | `TVOC 21 ppb` |
| `8e7a0016` | AQI text | `AQI 2` |
| `8e7a0017` | PM1.0 text | `PM1.0 14.874 ug/m3` |
| `8e7a0018` | PM2.5 text | `PM2.5 15.984 ug/m3` |
| `8e7a0019` | PM10 text | `PM10 16.299 ug/m3` |

All UUIDs use the same `-6d4b-4f23-9a71-5c2e9f0b3401` suffix.
`WAIT` means no initial snapshot, `WARM`/`INIT` reflects ENS160 conditioning,
`N/A` means unavailable/invalid data, and `RANGE` means a number cannot fit
without truncation. Stale reads return `N/A`. Text is intended for human
inspection; the original three binary characteristics retain their format
and sequence fields for applications requiring synchronized samples.
