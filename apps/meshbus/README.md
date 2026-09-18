# Meshbus Firmware

Meshbus is a standalone Zephyr application with one firmware composition per
device. The device profile selects services and static capacities. MeshCore
Settings selects CHAT, REPEATER, ROOM, or SENSOR protocol behavior independently.
Registered application targets are Idea Mesh Tracker C2, Seeed Tracker T1000-E
and Seeed Wio Tracker L1. DevKit migration is deferred.
Shared hardware and services belong to the shared module in this repository.

## Workspace setup

Follow the [repository setup guide](../../README.md#workspace-setup).
The single west manifest is at repository-root `west.yml`. From the workspace,
this application is `meshbus/apps/meshbus`; shared tools are `meshbus/scripts`.
Firmware and SDK changes share one commit history. Dependencies remain separate
west projects, and release artifacts record their resolved SHAs and toolchain.

## Device firmware and MeshCore role

C2 uses `idea_mesh_tracker_c2/nrf54l15/cpuapp`, with device configuration and
application/MCUboot overlays under
`apps/meshbus/boards/fobe/idea_mesh_tracker_c2/`. Its SDK board owns the physical
peripherals. APP profiles are the sole product target inventory:

```text
boards/<vendor>/<board>/
  <normalized-target>.conf
  <normalized-target>.overlay
  <normalized-target>_mcuboot.conf
  <normalized-target>_mcuboot.overlay
```

Only the APP `.conf` registers a target; overlays and MCUboot fragments are
optional companions. Keep the full target in each filename, with `/` replaced
by `_`. Multiple qualifiers for one board may coexist. The scanner resolves
these names through Zephyr's board/SoC metadata and validates the vendor.
Do not place flat profiles or further nested configuration files here.

`ZephyrAppConfig.cmake` loads the selected APP profile after Zephyr resolves the
board and before Kconfig/devicetree processing. `sysbuild.cmake` uses the same
scanner to locate MCUboot companions. Both ordinary `west build` and
`west release build` therefore use this layout, retaining the shared `prj.conf`.
Use fresh build directories after moving profiles from the former flat layout.
`west release matrix` reports all registered targets; publication still requires
the separate qualification gates in `DISTRIBUTION.md`.

The Seeed profiles live under `boards/seeed/tracker_t1000_e/` and
`boards/seeed/wio_tracker_l1/`, for `tracker_t1000_e/nrf52840` and
`wio_tracker_l1/nrf52840`. They preserve the supplied UF2/SoftDevice boot layout
and do not enable MCUboot. Wio's overlay includes its adjacent
`wio_tracker_l1_partitions.dtsi` (692 KiB application, 128 KiB settings and
2 MiB external `/extra`). T1000-E keeps the supplied sensor-only profile with
MeshCore and messaging disabled pending sizing and radio validation.
Wio keeps Desktop/LLEXT and the MeshCore client profile. The optional source
performance-log fragment is not part of the default product profile.

`west release build --target tracker_t1000_e` and `--target wio_tracker_l1`
select native UF2 packaging without an image signing key. Supply the usual
`--workspace`, `--build-root`, `--output` and, for dirty sources, `--development`
arguments described in `DISTRIBUTION.md`. UF2 packages contain only the APP and
preserve the existing bootloader/SoftDevice layout. Wio uses LTO and local ISR
tables to retain its full feature set within the 692 KiB application partition.
The application CMake keeps Zephyr's syscall export/weak-alias bridge objects
outside GCC LTO; final-symbol checks cover their linker type/size warnings.
EDK compiler flags exclude LTO so MBA extensions contain machine code.
Hardware release qualification is separate.

Release builds add `prj.prod.conf` (size optimization, local ISR tables and LTO)
after `prj.conf` and the board profile. `west release build --development` uses
`prj.dev.conf`, retaining board-required LTO while otherwise using size
optimization. The fragments use the native `meshbus_EXTRA_CONF_FILE` sysbuild
argument and affect only the APP. Dev/prod build trees are separate; use separate
artifact output directories too. Plain `west build` retains board defaults
unless one of these fragments is explicitly selected.

```sh
west build -p always --sysbuild \
  -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
  meshbus/apps/meshbus -d build/meshbus-c2 -- \
  '-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE="/absolute/path/to/development-ed25519.pem"'
```

C2 defaults to MCUboot single-app Ed25519 validation, UART recovery, Management
operator, Bluetooth, desktop, contacts, channels, messages, and product sensors.
The default MeshCore role is CHAT. Change it through MeshCore settings on the
desktop, Management, or the CLI command `meshcore config --role meshcore_role_chat`
(with `chat`, `repeater`, `room`, or `sensor` as the role suffix).
Settings calls wait for the protocol runtime to apply changes, then update the
configuration and schedule persistence. Failed changes preserve the prior
configuration and attempt to restore its runtime. Unsupported roles are rejected
according to compiled services. Same-role settings updates apply directly;
role or identity changes rebuild the engine and discard queued protocol work.
MeshCore reset applies defaults with a new identity without rebooting the device.
Desktop and MBA apps keep running. Clients use the request result; no activation
polling is required. Configuration responses contain only the applied settings.

For explicitly authorized unsigned hardware validation, use a separate build:

```sh
west build -p always --sysbuild \
  -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
  meshbus/apps/meshbus -d build/meshbus-c2-unsigned -- \
  -DSB_CONFIG_BOOT_SIGNATURE_TYPE_NONE=y
```

This selects MCUboot without signature authentication and generates a matching
unsigned application with its MCUboot header and hash. Program both images;
the normal authenticated bootloader rejects this application. This engineering
mode retains the partition layout and UART recovery, uses no signing key, and
does not qualify as a signed release build. The default Ed25519 build still
requires the explicit caller-owned key shown above.

Static RAM is reserved for compiled services across all roles, including after
reboot. Smaller devices can omit unneeded services or reduce capacities: CHAT
requires Contact, Channel, and Message integration, but Channel capacity may be
as small as 1. A full custom-channel table remains valid at startup. C2 retains
64 channels, 10 stored messages, and 8 pending messages. Unsupported role
selections are rejected; role changes do not enable or disable services.

## Repository integration

The SDK defines the ordinary C2 board; the application supplies product overlays. `meshbus` is the
single shared Zephyr module and owns Meshbus, DFU, platform APIs and drivers,
DTS bindings, U8G2, ZUI, and external-module adapters. Meshbus public headers
are available as `<module/module.h>` (for example `<clock/clock.h>`) and generated
schemas as `<meshbus/*.pb.h>`. Profiles select services with `CONFIG_MBS` and
`CONFIG_MBS_*`; application code calls `mbs_*` service APIs. The product-owned
`CONFIG_MESHBUS_UART_MCUMGR_LOGGING` option and the `meshbus` CLI/Shell name
remain unchanged. See the [SDK naming boundary](../../README.md#zephyr-integration).

When adopting firmware with the new service symbols, rebuild installed MBA
packages that import the former `meshbus_*` APIs using the matching EDK.
Package metadata retains its format, but renamed imports require recompilation;
see [EDK migration](../../DISTRIBUTION.md#edk-and-extension-packages).

## Validation

Select the smallest relevant suite and its declared platform, as described in
[DEVELOPMENT.md](../../DEVELOPMENT.md). The commands below are broader examples.
Replace `<task>` with a unique name for the current run; each suite uses its own
output directory so `-c` cannot delete another suite's results.

Shared subsystem and driver tests live in this repository and are run from the
workspace root:

```sh
west twister -T meshbus/tests/subsys -t meshbus \
  -p qemu_x86 -p qemu_cortex_m3 -O 'twister-out/<task>-services' \
  --inline-logs -v -c -j 1
west twister -T meshbus/tests/subsys/dfu/flash_delta \
  -p qemu_x86 -O 'twister-out/<task>-flash-delta' --inline-logs -v -c -j 1
west twister -T meshbus/tests/drivers/sensor/compass_composite \
  -p qemu_x86 -O 'twister-out/<task>-compass' --inline-logs -v -c -j 1
```

Hardware evidence must be recorded separately from build and Twister results.

C2 recovery deliberately exposes the whole 1300 KiB CPUAPP application
allocation as `image-0`, allowing the application partition layout to change
without reflashing MCUboot. Recovery uploads can therefore overwrite settings
and other application-owned data. The default C2 MCUboot accepts only
Ed25519-authorized applications and validates slot0 on every boot. Recovery
transport is UART-only; physical access can still erase or replace the complete
trust chain over SWD.

## Product policy and release qualification

The C2 Client retains raw Contact adverts in persistent storage as an explicit
engineering/product-policy choice. The 1300 KiB recovery window excludes the
top 96 KiB FLPR region. Progressive UART recovery erases sectors reached by the
upload and can destroy application data before an unauthorized image is
rejected. Signed rollback to any valid authorized C2 image remains allowed.

Normal boot, UART recovery, rejected-image behavior, SWD factory programming,
and retained settings require separate C2 hardware qualification. Production
key provisioning and protected CI signing remain prerequisites for release;
see [DISTRIBUTION.md](../../DISTRIBUTION.md). Repository relocation provides
no new hardware or GA evidence.

## Migration baseline

The first standalone snapshot is migrated from FoBE commit
`748e79923cd091e0c99e9701af19fcc598811d8a`. History before that snapshot stays
in the FoBE repository; this repository intentionally starts with new history.
