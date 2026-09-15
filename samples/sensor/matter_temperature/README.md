# Matter environment sensor prototype

Experimental standalone Zephyr application for `devkit_esp32c6/esp32c6/hpcore`.
The directory and existing product identity are retained from the temperature
prototype so firmware updates can preserve commissioning credentials.

All sensors share I2C0 on GPIO22/SDA and GPIO23/SCL. Measurements update every
five seconds. BLE is used for commissioning; operational communication uses
Wi-Fi and IPv6. Fabrics and Wi-Fi credentials use settings/NVS.

| Endpoint | Device type | Measurements | Sensor/address | Matter units |
| --- | --- | --- | --- | --- |
| 1 | Temperature sensor `0x0302` | Temperature | SHT40 `0x44` | 0.01 °C |
| 2 | Humidity sensor `0x0307` | Relative humidity | SHT40 `0x44` | 0.01 % RH |
| 3 | Air quality sensor `0x002C` | Air quality, eCO2, TVOC | ENS160 `0x52` | Enum, ppm, ppb |
| 3 | Same air quality endpoint | PM1.0, PM2.5, PM10 | SPS30 `0x69` | µg/m³ |
| 4 | Pressure sensor `0x0305` | Atmospheric pressure | LPS22HB `0x5c` | 0.1 kPa = 1 hPa |

ENS160 reports **estimated CO2 (eCO2)** derived from gas sensing, not a direct
CO2 measurement. The standard Carbon Dioxide Concentration Measurement cluster
does not label this distinction; a controller may simply display “CO2”. TVOC
is total volatile organic compounds. The five ENS160 air-quality levels map
to Matter Good/Fair/Moderate/Poor/VeryPoor; this is the ENS160 rating, not a
combined particulate/gas index. SHT40 supplies temperature/humidity compensation
within ENS160's supported compensation range.

Invalid or failed measurements become Matter null; air quality becomes Unknown.
ENS160 conditioning is allowed to finish without repeatedly resetting its mode.
Validity 2 (INIT) can require one hour of continuous initial operation; validity
1 (WARM) can require three minutes. See the [ScioSense ENS160 datasheet v1.3](https://www.sciosense.com/wp-content/uploads/2023/12/ENS160-Datasheet.pdf).
Keep the sensor powered while conditioning.
A no-new-data response retains the last valid gas sample for at most 15 seconds.
Peak, average and level concentration features are not advertised. Concentration
uncertainty is omitted because this prototype does not calculate it. Pressure
uses the base hPa resolution; extended scaled pressure is not advertised.

`environment.zap` is the local data model input. `environment.matter` is generated
by the pinned ZAP tool through `build.sh`; do not edit generated IDL by hand.
The matrix and weather station's custom BLE service remain outside this sample.

## Apple Home and upgrade behavior

The original temperature firmware passed user-assisted Apple Home pairing,
temperature display and software-reset recovery. Physical power-cycle recovery
has not been qualified. New endpoint visibility requires separate Home checks:
protocol publication does not guarantee that Home displays every concentration
or pressure value. Existing accessories may cache their endpoint layout.

Flash without erasing NVS first. If Home does not discover the additional
services, remove the old accessory in Home, press the user-wired GP9 active-low
button after boot, then add it again with manual code `34970112332`. GP9 opens
a 15-minute basic commissioning window without erasing other fabrics or Wi-Fi.
Release GP9 during reset because it is also a boot strap pin. Button input is
polled between sensor operations; a press held for about one second is suitable.
Do not erase saved credentials merely to update sensor code.

## Dependencies

The sample now defaults to the Meshbus Matter fork at
`<west-topdir>/modules/lib/matter`, declared as project `matter` in Meshbus's
`west.yml`. The build script uses the local checkout, including development
patches. The fork was based on upstream
`fe4e1c8667fab20fb4a83c59e8277c846568e8c3`; SDK and generator versions must be
updated together when upgrading that baseline.

The tested host tools are:
- GN: `2565 (4c122caddf1b)`
- ZAP: `v2026.09.09`, matching the SDK's `scripts/setup/zap.version`
- Python package `python-path==0.1.3`, in a task-local directory
- The workspace's existing Zephyr Python environment, SDK and Ninja

The original `.scratch/matter-temperature/connectedhomeip` checkout is retained
as historical prototype context and is no longer the default SDK. Host tools
currently remain in `.scratch/matter-temperature/tools`.
Set `CHIP_ROOT`, `MATTER_TOOLS_ROOT`,
`ZAP_INSTALL_PATH` and `MATTER_BUILD_DIR` to use other isolated locations.
`build.sh` does not download dependencies or flash hardware.

The SDK needs its pinned `third_party/pigweed/repo`, `third_party/nlassert/repo`,
`third_party/nlio/repo`, `third_party/uriparser/repo` and
`third_party/jsoncpp/repo` submodules. The GN build imports
`build_overrides/pigweed_environment.gni`; for this cross-build the task-local
file contains only a comment, since neither Pigweed's host clang nor its CIPD
toolchain is selected. Full Matter bootstrap is another way to provide it.

The macOS arm64 download archives used during development have SHA-256:

- `gn.zip`: `c4da65b208186127e0ea0394d750825617853f74108ae1f6f1f01dcf0589a6de`
- `zap-mac-arm64.zip`: `8c324d66631dd90a08152fcea0cd5ac28e72344b591157c835b37608acf25dcc`

## Build

From the repository root, with the prerequisites prepared:

```sh
source ~/.zephyr/env/bin/activate
samples/sensor/matter_temperature/build.sh
```

The default build directory is `<west-topdir>/build/matter-temperature-fork`.
The previous prototype build remains intact. Ordinary incremental invocations
reuse the CMake cache; explicit arguments to `build.sh` force configuration.
The sample opts into ZAP-selected GN cluster implementations. See `config/zephyr/README.md`
in the Matter fork for the build controls and regression tests.

This sample defaults the nested Matter Ninja build to four concurrent jobs.
Override it with, for example,
`samples/sensor/matter_temperature/build.sh -DMATTER_GN_BUILD_JOBS=8`.
The best value depends on available memory and other workloads. This cache
setting persists across builds and does not change the outer Zephyr job limit.

The sample defaults `MATTER_GN_USE_COMPILER_LAUNCHERS=OFF`: direct compilation
outperformed cold/preprocessed ccache reuse in the tested Matter workload on
this host. The outer Zephyr compiler launcher is unchanged. Hosts with different
cache behavior can opt in with `-DMATTER_GN_USE_COMPILER_LAUNCHERS=ON`; the SDK
integration itself inherits Zephyr's launchers by default.

The sample is skipped by default in Twister because its pinned host generators
require separate preparation. Use the explicit build above. If this workspace
uses an enclosing Enterprise manifest, that manifest's imports determine what
`west list` sees; the explicit `CHIP_ROOT` module integration still uses the
local Matter checkout without changing the workspace's manifest entry point.

### Build measurements (2026-09-15)

On the tested macOS arm64 host (10 logical CPUs, 16 GiB RAM), the sample with
four nested jobs and direct GN compiler invocation produced these single-run
end-to-end measurements:

| Build | Original integration | Updated integration |
| --- | ---: | ---: |
| First complete build | 556.48 s | 363.97 s |
| No changes | 12.13 s | 1.58 s |
| One application startup string changed | 20.08 s | 10.84 s |

The original outer Zephyr build used an existing ccache; the final build used
a fresh private outer cache. Host activity and filesystem caches were not
controlled, so these timings are observations rather than portable guarantees.
In a separate same-argument, four-job, ccache-disabled Matter-only comparison,
cluster selection reduced 509 compiler commands to 348 and elapsed time from
220.86 s to 135.24 s. Common generated cluster types still compile.

Early experiments inheriting ccache inside GN were slower on this host,
including a 617.93 s cold build with four jobs. Those measurements are why the
sample's launcher default differs from the SDK's. Raw benchmark logs, commands,
cache deltas and firmware identities remain in `.scratch/matter-efficiency/`.

The full C6 build, six host integration tests, Python lint/import checks and
shell syntax check passed. Temporary benchmark source/config changes were
restored. Flash/RAM allocation matched the original image. The board was not
connected for this build-efficiency change; its optimized image has not yet
been flashed or verified in Apple Home.

The sample also registers the SDK's example device-information provider before
server initialization, because the upstream model includes User Label. This
provider is used only as prototype support, not as production device metadata.

The CMake integration selects RISC-V explicitly. The cryptographic configuration
uses PSA with the SDK's Mbed TLS SPAKE2+ fallback, and enables the Wi-Fi statistics
API used by the generic Zephyr Wi-Fi adapter. The local `environment.zap` and its
generated `environment.matter` describe the sample's data model; generated C++
files belong in the build directory.

## Physical acceptance

After a successful build and authorized flash, wait for `Matter server ready`
and the onboarding output. In iPhone Home, add an accessory using the displayed
Matter QR or manual onboarding code. For the current development configuration, the manual onboarding code is
`34970112332`. The commissioning passcode below is not the manual onboarding
code. Keep the iPhone and Apple TV on the same home
network; configure a 2.4 GHz Wi-Fi network reachable by the ESP32-C6.

This prototype uses upstream test attestation credentials, vendor `0xFFF1`,
product `0x8000`, discriminator `0xF00`, and setup passcode `20202021`.
Apple Home may present an uncertified-accessory confirmation. These test
credentials are for development, not deployment as a product.

Acceptance criteria:

1. The C6 boots, initializes the four sensors and starts all Matter clusters.
2. iPhone commissions it into Apple Home through the Apple TV home hub.
3. Serial readings and Matter updates succeed for all measurements after ENS160
   conditioning. Check which additional values Apple Home actually displays.
4. Power cycling restores the existing fabric and Wi-Fi association without
   requiring another pairing.

Raw development logs are in `.scratch/matter-temperature/`.

## Current validation limits

The native USB serial boot capture requires DTR deasserted before an RTS reset.
Using a generic RTS-only pulse while DTR is asserted can select download mode.
The successful capture used esptool's `HardReset(uses_usb=True)` with DTR false.

The current ESP simple-boot image reports a ROM SHA-256 comparison warning,
then boots; esptool's write/read verification succeeds. This has not been
qualified as a secure or signed boot path. Startup also drops some deferred
log messages and reports one long-dispatch diagnostic during initialization.
Direct serial startup and temperature messages are retained for observability.
These diagnostics remain recorded in the raw logs; the result is functional
startup evidence, not a clean-log or release qualification.

## Wi-Fi power requirement and pairing diagnosis

On the attached test setup, starting a Wi-Fi scan repeatedly disconnected USB
and reset the ESP32-C6, even without an iPhone or Matter commissioning session.
Reading `LP_CLKRST_RESET_CAUSE_REG` at `0x600B0410` through USB JTAG **before**
reopening the serial port returned `0x2F`: the low five bits are `0x0F`,
`RESET_REASON_SYS_BROWN_OUT`. The confirmed immediate failure is a supply
undervoltage reset during Wi-Fi activity. The exact electrical source (cable,
hub, board regulator or attached load) has not yet been isolated.

A subsequent host serial reconnect can itself reset the CPU and replace that
reason with `0x15` (USB UART reset). A serial transcript stopping is therefore
not sufficient evidence of a deadlock or stack overflow. Earlier stack-only
experiments did not establish complete pairing success.

Use a stable supply and a suitable USB data cable before retrying Apple Home.
The sample keeps 4096-byte logging and Bluetooth receive stacks plus the Zephyr
stack sentinel for diagnostic headroom. Automatic scan and temporary thread
stack/reset instrumentation have been removed from the application.

After changing the USB connection path, the same automatic scan completed
without USB loss. The user then confirmed successful Apple Home commissioning
and temperature display.

`CONFIG_ESP32_WIFI_STA_RECONNECT=y` enables driver retries for transient
association failures after restoring a paired device. With this setting, the
software-reset check restored two saved fabrics, connected to Wi-Fi at about
12 seconds and established a new CASE session at about 25 seconds; temperature
sampling continued. A physical power-cycle restoration test remains outstanding.
These are prototype results, not production or RF qualification.

## Multisensor validation (2026-09-15)

`source ~/.zephyr/env/bin/activate` followed by
`samples/sensor/matter_temperature/build.sh` passed. From the west root,
`west flash -d build/matter-temperature --runner esp32 --esp-device /dev/tty.usbmodem21244101`
passed with esptool hash verification. The identified C6 retained two fabrics
and reconnected to Wi-Fi. All cluster setters succeeded during sampling.
Temperature, humidity, pressure and all three particulate channels produced
changing physical readings. ENS160 reported initial conditioning (validity 2),
so valid gas readings and their eventual publication remain unverified.

The generated model contains the intended endpoint/cluster inventory and
numeric-only concentration features. This is static model inspection plus a
physical smoke test; no sensor calibration, certification, physical power-cycle
recovery or complete protocol conformance testing was performed.

The existing Home accessory initially continued to show only temperature after
upgrading. The user subsequently confirmed that additional sensor values were
visible in Home. This does not establish which individual concentration or
pressure fields every Home version displays.
