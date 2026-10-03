# Meshbus

This repository contains the Meshbus Zephyr module, product firmware at
`apps/meshbus/`, and host tools under `scripts/`. The parent directory is a
local Zephyr workspace. Product and SDK code share this Git repository;
firmware and CLI versions retain independent release trains.

## Workspace setup

Install the Zephyr host prerequisites and a toolchain for your target, then
make `west` available in an active Python virtual environment. Commands in the
development guides use `~/.zephyr/env` as an example environment location;
substitute your own path. Building the Rust host CLI also requires Rust and Cargo.

With this checkout at `<workspace>/meshbus`, initialize from `<workspace>`:

```sh
west init -l meshbus
west update
west zephyr-export
west packages pip --install
python -m pip install -r meshbus/scripts/requirements.txt
```

For an existing workspace, use `west topdir`, `west config manifest.path`, and
`west manifest --path` to identify its root and active manifest. This
repository's `west.yml` pins dependency revisions; a consuming workspace may
select its own manifest. Inspect `west list` before using or updating the
resolved projects. Release artifacts must retain resolved project SHAs,
toolchain, configuration, authentication mode, and any signing identity.

See [product targets and builds](apps/meshbus/README.md),
[development and validation](DEVELOPMENT.md), and
[distribution and release gates](DISTRIBUTION.md). Public SDK MCUboot builds
default to hash-only validation without a private key. Downstream products may
explicitly enable Ed25519 authentication with their own key. A build alone does
not establish hardware or release qualification.

See [contributing](CONTRIBUTING.md) for source rights, third-party material and
maintainer review before merging.

## Zephyr integration

The Meshbus SDK provides reusable services, public APIs, hardware support and
UI components for product firmware and other Zephyr applications. An Extension
Development Kit (EDK) instead contains release-matched public compiler inputs
for building Desktop MBA applications for one product target; see
[EDK distribution](DISTRIBUTION.md#edk-and-extension-packages).

The module descriptor at `zephyr/module.yml` exports this repository as a
Zephyr CMake/Kconfig module and contributes its board, devicetree, and module
extension roots. A consuming west workspace must make `meshbus` visible as
the west manifest repository, a west project, or through `ZEPHYR_EXTRA_MODULES` before calling
`find_package(Zephyr)`.

The active workspace manifest is also responsible for resolving the external
Meshbus dependencies:

- `meshbus-protobufs`
- `meshcore`
- `u8g2` and `zui` for display/UI consumers
- `detools`
- `heatshrink`
- `arduboy` for SDK-backed MBA projects; native Meshbus MBA examples use the EDK directly
- Zephyr and the modules imported by Zephyr

U8g2 and ZUI are independent west projects (`sdk-u8g2` and `sdk-zui`).
ZUI consumes U8g2 for drawing; neither requires Meshbus service Kconfig
symbols.

## Repository layout

- `include/<module>/`: public Meshbus APIs, such as `clock/clock.h`
- `subsys/<service>/`: Meshbus service implementations, including `indicator` and `clock`
- `subsys/settings/`, `subsys/mgmt/`, `subsys/shell/`: shared persistence and adapters
- `cmake/protobuf/`: shared schema generation into each image's `generated/meshbus/`
- `subsys/dfu/`: delta-image installation support
- ZUI UI components and U8g2 are independent Zephyr modules in the workspace
  at `modules/lib/zui` (`sdk-zui`) and `modules/lib/u8g2` (`sdk-u8g2`).
- `boards/`, `drivers/`, and `dts/`: SDK hardware integration
- `samples/` and `tests/`: reusable SDK validation surfaces
- `apps/meshbus/`: product composition, device profiles, and sysbuild policy
- `scripts/`: Rust CLI and meshbus/release/mklfs/remote west extensions
- [openspec/](openspec/README.md): shared specifications and change plans
- `.agents/skills/`: generated OpenSpec workflows for Codex

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
Generated protobuf headers use paths such as `meshbus/clock.pb.h`, with
types and constants defined by their schema namespace. Repository-owned
display, driver, devicetree binding, DFU, and linker interfaces use category
paths, such as `<display/display.h>`, `<drivers/sensor/compass.h>`, and
`<dt-bindings/sensor/compass.h>`. U8g2 and ZUI provide `<display/u8g2.h>` and
`<zui/zui.h>` through their own module include roots. Upstream Zephyr headers
use paths such as `<zephyr/drivers/sensor.h>`.

Internal Settings, MCUmgr, and Shell helpers remain private. Register the SDK's
`include` root on the compiler search path. Devicetree chosen names, settings
paths, and protocol values follow their respective bindings and contracts.
Product and CLI identifiers use Meshbus; the application's
`CONFIG_MESHBUS_UART_MCUMGR_LOGGING` option owns product UART policy.

Build MBA packages against the intended firmware's EDK and device target.
See [MBA metadata](subsys/llext/METADATA.md) for the metadata,
ABI, and package requirements.

Business timestamp helpers are declared in `clock/timestamp.h`:
`mbs_clock_realtime_is_valid()`, `mbs_clock_timestamp_s_get()`, and
`mbs_clock_timestamp_ms_get()`. They remain available with `CONFIG_MBS=y`
even when `CONFIG_MBS_CLOCK=n`. Timestamps use valid realtime or nonzero uptime
as a fallback.

## Development workflow

Use the [OpenSpec workflow](openspec/README.md) to propose, review, implement
and archive shared changes. [AGENTS.md](AGENTS.md) is the agent entry point;
[DEVELOPMENT.md](DEVELOPMENT.md) describes engineering and build practices.
Local `sample.yaml` and `testcase.yaml` files define supported build and test surfaces.
Small corrections can use focused checks without a new change. Current guides
own their contracts; use [history retrieval](DEVELOPMENT.md#history-retrieval)
only when tracing earlier decisions or work. Follow
[output handling](DEVELOPMENT.md#outputs-and-records) for temporary files and deliverables.

The serial console helper is an ordinary SDK tool at `scripts/serial_use.py`;
its device-free regression tests are in `scripts/tests/test_serial_use.py`.

## Licensing and provenance

Unless a file or subtree states otherwise, this repository is licensed under
the Apache License, Version 2.0 (`Apache-2.0`); see
[LICENSE](LICENSE). This default covers the Meshbus SDK, product application
and host tools. Embedded third-party components and
their retained notices are documented in
[LICENSING.md](LICENSING.md).

Compiled third-party dependencies follow the
[dependency license policy](LICENSING.md#compiled-dependency-admission).
Dependency changes and newly enabled features require review of the actual
license path, including embedded fonts and generated material. This policy
does not replace the Apache-2.0 license of Meshbus-owned code or the individual
terms of third-party components.

## Project website

The customer site and documentation target https://meshbus.org/. Website source,
local preview and contribution instructions live in [web/README.md](web/README.md).
The site imports the owning guides; edit those guides to update technical content.
