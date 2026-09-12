# Meshbus

This repository contains the Meshbus Zephyr module, product firmware at
`apps/meshbus/`, and host tools under `scripts/`. The parent directory is a
local Zephyr workspace. Product and SDK code share this Git repository;
firmware and CLI versions retain independent release trains.

## Workspace setup

With this checkout at `<workspace>/meshbus`, initialize from `<workspace>`:

```sh
west init -l meshbus
west update
west zephyr-export
```

For an existing workspace, inspect `west config manifest.path` first; it should
select `meshbus`. Do not reinitialize or update dependencies just to relocate
application sources. `west.yml` is the only Meshbus manifest. It follows rolling
development branches and records fixed revisions where specified; release
artifacts must retain resolved project SHAs, toolchain, configuration, and
signing identity. No outer workspace source repository is required.

See [product targets and builds](apps/meshbus/README.md),
[development and validation](DEVELOPMENT.md), and
[distribution and release gates](DISTRIBUTION.md). C2's default Ed25519 build
requires an explicit key; the application guide also covers explicitly authorized
unsigned engineering builds. A build alone does not establish hardware or release
qualification.

## Zephyr integration

The module descriptor at `zephyr/module.yml` exports this repository as a
Zephyr CMake/Kconfig module and contributes its board, devicetree, and module
extension roots. A consuming west workspace must make `meshbus` visible as
the west manifest repository, a west project, or through `ZEPHYR_EXTRA_MODULES` before calling
`find_package(Zephyr)`.

The active workspace manifest is also responsible for resolving the external
Meshbus dependencies:

- `meshbus-protobufs`
- `meshcore`
- `detools`
- `heatshrink`
- Zephyr and the modules imported by Zephyr

U8G2 and ZUI are SDK-owned embedded components under `subsys/`; they are not
separate west projects. U8G2 exposes a generic display-adapter API and does not
depend on Meshbus service Kconfig symbols.

## Repository layout

- `include/<module>/`: public Meshbus APIs, such as `clock/clock.h`
- `subsys/<service>/`: Meshbus service implementations, including `indicator` and `clock`
- `subsys/settings/`, `subsys/mgmt/`, `subsys/shell/`: shared persistence and adapters
- `cmake/protobuf/`: shared schema generation into each image's `generated/meshbus/`
- `subsys/dfu/`: delta-image installation support
- `subsys/u8g2/` and `subsys/zui/`: embedded display/UI components
- `boards/`, `drivers/`, and `dts/`: SDK hardware integration
- `samples/` and `tests/`: reusable SDK validation surfaces
- `apps/meshbus/`: product composition, device profiles, and sysbuild policy
- `scripts/`: Rust CLI and meshbus/release/zui/mklfs/remote west extensions
- `CONTEXT.md` and `docs/adr/`: domain vocabulary and accepted decisions
- `.scratch/`: ignored local specifications, tickets, and historical evidence

Run Zephyr commands from the west workspace root. Replace `<task>` with a unique
name for the current run to preserve other build and test outputs. For example:

```sh
west build -p auto -b qemu_x86 -d 'build/<task>-clock' \
  meshbus/tests/subsys/clock
west twister -T meshbus/tests/subsys/clock \
  -p qemu_x86 -O 'twister-out/<task>-clock' --inline-logs -v
```

The exact boards and test platforms supported by each sample or suite are
declared by its local `sample.yaml` or `testcase.yaml`.

Meshbus services use `CONFIG_MBS` and `CONFIG_MBS_*` options and expose public headers
as `<module/module.h>`, for example `<clock/clock.h>` and
`<firmware/firmware.h>`.
Each service owns a named build library. Protobuf schemas remain
in the `meshbus-protobufs` west project; generated headers and implementations
belong to the consuming image's build directory and are never written into the SDK.
The shared `mgmt` module supplies MCUmgr encoding and access hooks; `management`
owns the remote management service. Service-specific adapters remain beside their
owning implementation.

Include only the capability needed by a caller:

| Public header | Capability |
| --- | --- |
| `clock/clock.h` | Clock configuration, local civil time, realtime setting |
| `clock/timestamp.h` | Basic business timestamps and realtime validity |
| `gnss/gnss.h` | GNSS configuration, acquisition, fixes and events |
| `gnss/heading.h` | Heading leases, snapshots and calibration |
| `llext/llext.h` | Application loading and runtime configuration |
| `llext/metadata.h` | MBA metadata layout and section registration |
| `llext/zbus.h` | Extension channel IDs, subscriptions and messages |
| `firmware/firmware.h` | Firmware update lifecycle |

Other services use one `<module/module.h>` entry. Clock and GNSS main headers
also include their narrow capability header; LLEXT runtime includes metadata,
while bridge users include `llext/zbus.h` explicitly. Public service functions,
types and ZBus objects use `mbs_`; constants and enum values use `MBS_`.
Migrate former `zephyr/meshbus/<module>.h` or
`meshbus/<module>/<module>.h` includes to `<module>/<module>.h`; there are no
forwarding headers. Public headers use module paths such as `clock/clock.h`;
generated protobuf headers retain paths such as `meshbus/clock.pb.h`.
Repository-owned display, ZUI, driver, devicetree binding, DFU and linker
interfaces also use flat category paths, such as `<display/u8g2.h>`,
`<zui/zui.h>`, `<drivers/sensor/compass.h>` and
`<dt-bindings/sensor/compass.h>`. Migrate their former `zephyr/` includes;
upstream Zephyr headers such as `<zephyr/drivers/sensor.h>` keep that prefix.
Internal Settings,
MCUmgr and Shell helpers remain private. Register the SDK's `include` root,
not each module directory, on the compiler search path.

Replace former hand-written `meshbus_` / `MESHBUS_` service identifiers with
`mbs_` / `MBS_`, including service Kconfig selections and ZBus observers.
Private shared helpers formerly using `mb_` / `MB_` follow the same convention.
Protobuf-generated types and constants retain their schema namespace. Existing
devicetree chosen names, settings paths, protocol values and product/CLI names
remain unchanged. The application's `CONFIG_MESHBUS_UART_MCUMGR_LOGGING` option
continues to own product UART policy.

Rebuild MBA packages against the new firmware's EDK: old `meshbus_` service
symbols are not exported as compatibility aliases. This does not change the
MBA metadata wire format; see [LLEXT compatibility](subsys/llext/API_COMPATIBILITY.md).

Business timestamp helpers are declared in `clock/timestamp.h`:
`mbs_clock_realtime_is_valid()`, `mbs_clock_timestamp_s_get()`, and
`mbs_clock_timestamp_ms_get()`. They remain available with `CONFIG_MBS=y`
even when `CONFIG_MBS_CLOCK=n`. Timestamps use valid realtime or nonzero uptime
as a fallback. Consumers of the former Time header and functions must migrate to
these Clock interfaces; no compatibility aliases are provided.

| Former Time interface | Clock replacement |
| --- | --- |
| `zephyr/meshbus/time.h` | `clock/timestamp.h` |
| `meshbus_time_realtime_is_valid` | `mbs_clock_realtime_is_valid` |
| `meshbus_time_timestamp_s_get` | `mbs_clock_timestamp_s_get` |
| `meshbus_time_timestamp_ms_get` | `mbs_clock_timestamp_ms_get` |
| `MESHBUS_TIME_VALID_UNIX_TIMESTAMP_S` | `MBS_CLOCK_VALID_UNIX_TIMESTAMP_S` |

## Development workflow

Read the [agent entry guide](AGENTS.md) and the nearest local `AGENTS.md` before
changing an owned area. Product and SDK work share root guidance and Git history. Local `sample.yaml` and `testcase.yaml`
files define supported build and test surfaces.

The serial console helper is an ordinary SDK tool at `scripts/serial_use.py`;
its device-free regression tests are in `scripts/tests/test_serial_use.py`.

## Licensing and provenance

Unless a file or subtree states otherwise, this repository is licensed under
Apache License 2.0; see [LICENSE](LICENSE). Embedded third-party components and
their retained notices are documented in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

The initial SDK source selection was migrated without FoBE Git history from
FoBE commit `748e79923cd091e0c99e9701af19fcc598811d8a`. That identifier records the
migration baseline, not the current SDK revision.
