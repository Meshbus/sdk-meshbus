# Meshbus Firmware

Meshbus is a standalone Zephyr application with one firmware composition per
device. The device profile selects services and static capacities. MeshCore
Settings selects CHAT, REPEATER, ROOM, or SENSOR protocol behavior independently.
Firmware 1.0.0 targets Idea Mesh Tracker C2; DevKit migration is deferred.
Shared hardware and services belong to the shared module in this repository.

## Workspace setup

Follow the [repository setup guide](../../README.md#workspace-setup).
The single west manifest is at repository-root `west.yml`. From the workspace,
this application is `meshbus/apps/meshbus`; shared tools are `meshbus/scripts`.
Firmware and SDK changes share one commit history. Dependencies remain separate
west projects, and release artifacts record their resolved SHAs and toolchain.

## Device firmware and MeshCore role

C2 uses `idea_mesh_tracker_c2/nrf54l15/cpuapp`, with device configuration and
application/MCUboot overlays under `apps/meshbus/boards/`. Its SDK board owns the physical
peripherals. `apps/meshbus/boards/products.yml` owns the release device identity.

```sh
west build -p always --sysbuild \
  -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
  meshbus/apps/meshbus -d build/meshbus-c2 -- \
  '-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE="/absolute/path/to/development-ed25519.pem"'
```

C2 retains MCUboot single-app Ed25519 validation, UART recovery, Management
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

Static RAM is reserved for compiled services across all roles, including after
reboot. Smaller devices can omit unneeded services or reduce capacities: CHAT
requires Contact, Channel, and Message integration, but Channel capacity may be
as small as 1. A full custom-channel table remains valid at startup. C2 retains
64 channels, 10 stored messages, and 8 pending messages. Unsupported role
selections are rejected; role changes do not enable or disable services.

## Repository integration

The SDK defines the ordinary C2 board; the application supplies product overlays. `sdk-meshbus` is the
single shared Zephyr module and owns Meshbus, DFU, platform APIs and drivers,
DTS bindings, U8G2, ZUI, and external-module adapters. Meshbus public headers
remain available as `<zephyr/meshbus/...>` and generated schemas as
`<meshbus/*.pb.h>`.

## Validation

Select the smallest relevant suite and its declared platform, as described in
[DEVELOPMENT.md](../../DEVELOPMENT.md). The commands below are broader examples.

Shared subsystem and driver tests live in `sdk-meshbus` and are run from the
workspace root:

```sh
west twister -T meshbus/tests/subsys/meshbus \
  -p qemu_x86 -p qemu_cortex_m3 --inline-logs -v -c -j 1
west twister -T meshbus/tests/subsys/dfu/flash_delta \
  -p qemu_x86 --inline-logs -v -c -j 1
west twister -T meshbus/tests/drivers/sensor/compass_composite \
  -p qemu_x86 --inline-logs -v -c -j 1
```

Hardware evidence must be recorded separately from build and Twister results.

C2 recovery deliberately exposes the whole 1300 KiB CPUAPP application
allocation as `image-0`, allowing the application partition layout to change
without reflashing MCUboot. Recovery uploads can therefore overwrite settings
and other application-owned data. C2 MCUboot accepts only Ed25519-authorized
applications and validates slot0 on every boot. Recovery transport is UART-only;
physical access can still erase or replace the complete trust chain over SWD.

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
