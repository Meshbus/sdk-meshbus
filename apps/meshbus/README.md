# Meshbus Firmware

Meshbus is a standalone Zephyr application with one firmware composition per
device. The device profile selects services and static capacities. MeshCore
Settings selects CHAT, REPEATER, ROOM, or SENSOR protocol behavior independently.
Discover registered application targets with `west release matrix`. A board
definition alone does not register a product or establish hardware qualification.
Shared hardware and services belong to the shared module in this repository.

## Workspace setup

Follow the [repository setup guide](../../README.md#workspace-setup).
The single west manifest is at repository-root `west.yml`. From the workspace,
this application is `meshbus/apps/meshbus`; shared tools are `meshbus/scripts`.
Firmware and SDK changes share one commit history. Dependencies remain separate
west projects, and release artifacts record their resolved SHAs and toolchain.

## Device firmware and MeshCore role

SDK board definitions own physical peripherals. Application profiles select
services, capacities, boot policy and partition overrides under
`apps/meshbus/boards/<vendor>/<board>/`. APP profiles are the sole product target inventory:

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
Use fresh build directories when changing profile locations or build configuration.
`west release matrix` reports all registered targets; publication still requires
the separate qualification gates in `DISTRIBUTION.md`.

Boot and packaging behavior follow the selected profile and generated build
configuration. MCUboot profiles default to hash-only images and can opt into
native signing; UF2 profiles package the
application for a compatible preinstalled bootloader. Partition sizes, storage
mounts and enabled services come from the profile and final devicetree rather
than a separate table in this guide.

When LTO is enabled, application CMake keeps Zephyr's syscall export/weak-alias
bridge objects outside GCC LTO; final-symbol checks cover their linker type/size
warnings. EDK compiler flags exclude LTO so MBA extensions contain machine code.

Release builds add `prj.prod.conf` (size optimization, local ISR tables and LTO)
after `prj.conf` and the board profile. `west release build --development` uses
`prj.dev.conf`, retaining board-required LTO while otherwise using size
optimization. The fragments use the native `meshbus_EXTRA_CONF_FILE` sysbuild
argument and affect only the APP. Dev/prod build trees are separate; use separate
artifact output directories too. Plain `west build` retains board defaults
unless one of these fragments is explicitly selected.

Select a registered target whose profile enables MCUboot for these commands.
Replace the target, task and key-path placeholders before running them.
Reuse a valid task build directory for ordinary source edits; use a fresh
directory when changing the target or authentication mode.

```sh
west build -p auto --sysbuild \
  -b '<qualified-board-target>' \
  meshbus/apps/meshbus -d 'build/<task>-unsigned'
```

The selected profile determines which services and MeshCore roles are available.
For builds with MeshCore enabled, inspect the configured default role. Change it through MeshCore settings on the
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

The default MCUboot build needs no key and produces an APP with a valid image
header and hash. MCUboot validates integrity on every boot without authenticating
the publisher. For an authenticated downstream build, select Ed25519 and a key:

```sh
west build -p auto --sysbuild \
  -b '<qualified-board-target>' \
  meshbus/apps/meshbus -d 'build/<task>-signed' -- \
  -DSB_CONFIG_BOOT_SIGNATURE_TYPE_ED25519=y \
  '-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE="/absolute/path/to/development-ed25519.pem"'
```

Both modes retain the configured partition layout and recovery transports.
Changing modes on an installed device requires programming the matching
bootloader and APP: an existing authenticated bootloader rejects an unsigned
application. Mode selection does not authorize device access or a full erase,
and neither build constitutes physical recovery or release qualification.

Static RAM is reserved for compiled services across all roles, including after
reboot. Smaller devices can omit unneeded services or reduce capacities: CHAT
requires Contact, Channel, and Message integration, but Channel capacity may be
as small as 1. A full custom-channel table remains valid at startup. Channel
and message capacities come from the selected profile and final Kconfig. Unsupported role
selections are rejected; role changes do not enable or disable services.

## Mesh Probe R1

`mesh_probe_r1/nrf52840` provides an application-only UF2 for a compatible
preinstalled bootloader. The application starts at `0x27000`; packaging excludes
the MBR, SoftDevice and bootloader. Enter the bootloader's USB mass-storage mode
using its supported reset procedure and copy `app.uf2` to that volume. The UF2
runner identifies the board as `nRF52840-FoBEF2102-rev1a`.

The profile defaults to CHAT and retains runtime role selection. It enables
SX1262 radio, L76K GNSS, SSD1306 Desktop, rotary encoder and buttons, battery and
charging status, BLE Companion, USB CDC/BLE management, and the MBA host.
The encoder button selects and the USR button goes back. UART0 belongs to GNSS;
the host serial connection is USB CDC.

The product profile allocates 660 KiB to the application, 32 KiB to Settings,
and 128 KiB to internal LittleFS at `/extra`. It supports 16 contacts (including
cached raw adverts), 8 channels and an 8-message receive cache. The system heap
is 96 KiB, including the MBA host's 90 KiB reservation; that reservation is not
additional RAM. MBA file, installation metadata, filesystem and runtime limits
are separate budgets. This storage suits small applications; replacement or
atomic installation may require removing another package to free space.

First installation does not preserve previous firmware's settings or files.
An APP UF2 only writes its application payload, so it is not a storage-erase
image. Storage initialization and any required data reset must use the product's
storage regions while retaining the installed boot chain. Firmware updates
using this same layout retain data through the existing storage services.

Build and package from the west workspace root:

```sh
west release build --workspace "$PWD" --target mesh_probe_r1/nrf52840 \
  --build-root build/r1-products --output build/r1-candidate --development
```

Build and packaging checks do not qualify the installed bootloader, physical
peripherals or MBA execution on a device.

## Repository integration

The SDK supplies board definitions; the application supplies product overlays.
The `meshbus` Zephyr module owns the services, Desktop integration, DFU,
platform APIs and drivers, DTS bindings, and external-module adapters. U8g2
and ZUI are independent west modules consumed by Desktop. Meshbus public headers
are available as `<module/module.h>` (for example `<clock/clock.h>`) and generated
schemas as `<meshbus/*.pb.h>`. Profiles select services with `CONFIG_MBS` and
`CONFIG_MBS_*`; application code calls `mbs_*` service APIs. The product-owned
`CONFIG_MESHBUS_UART_MCUMGR_LOGGING` option and the `meshbus` CLI/Shell name
belong to the product interface. See the
[SDK naming boundary](../../README.md#zephyr-integration).

Build MBA packages with the EDK exported for the intended firmware and device
target. See [EDK and extension packages](../../DISTRIBUTION.md#edk-and-extension-packages)
for the package and loading requirements.

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

## Product policy and release qualification

Review the selected profile's bootloader and application configuration together.
For authenticated MCUboot profiles, verify the configured signature algorithm,
public key, primary-slot validation and permitted recovery transports. Recovery
writes erase the sectors reached by an upload and can destroy application data
before a rejected image is detected; check the final recovery partition bounds.

Persistence, rollback and owner replacement are profile-level policies. Inspect
the final Kconfig and devicetree for retained data, image slots, recovery ranges
and debug access rather than assuming identical behavior on every target.

Normal boot, configured recovery paths, rejected-image behavior, factory
programming and retained settings require qualification on each target.
Production signing and publication require the applicable gates in
[DISTRIBUTION.md](../../DISTRIBUTION.md).
