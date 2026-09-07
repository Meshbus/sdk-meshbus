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
[distribution and release gates](DISTRIBUTION.md). C2 builds require an explicit
Ed25519 key; a build alone does not establish hardware or release qualification.

## Zephyr integration

The module descriptor at `zephyr/module.yml` exports this repository as a
Zephyr CMake/Kconfig module and contributes its board, devicetree, and module
extension roots. A consuming west workspace must make `sdk-meshbus` visible as
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

- `include/zephyr/meshbus/`: public Meshbus APIs
- `subsys/meshbus/`: Meshbus core and service implementations
- `subsys/dfu/`: delta-image installation support
- `subsys/u8g2/` and `subsys/zui/`: embedded display/UI components
- `boards/`, `drivers/`, and `dts/`: SDK hardware integration
- `samples/` and `tests/`: reusable SDK validation surfaces
- `apps/meshbus/`: product composition, device profiles, and sysbuild policy
- `scripts/`: Rust CLI and meshbus/release/zui/mklfs/remote west extensions
- `CONTEXT.md` and `docs/adr/`: domain vocabulary and accepted decisions
- `.scratch/`: ignored local specifications, tickets, and historical evidence

Run Zephyr commands from the west workspace root. For example:

```sh
west build -p auto -b qemu_x86 \
  meshbus/tests/subsys/meshbus/services/clock
west twister -T meshbus/tests/subsys/meshbus/services/clock \
  -p qemu_x86 --inline-logs -v
```

The exact boards and test platforms supported by each sample or suite are
declared by its local `sample.yaml` or `testcase.yaml`.

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
