# Meshbus SDK

`sdk-meshbus` is the Zephyr module that provides the reusable Meshbus platform
layer. It contains the Meshbus and DFU subsystems, the embedded U8G2 and ZUI
graphics stack, supported boards and drivers, samples and subsystem tests.

Product firmware is intentionally not part of this repository. The separate
Meshbus firmware repository owns its single `app/`, board role variants,
role-service matrix, sysbuild policy, signing policy, and release images. It
also owns the standalone Rust `meshbus` CLI, EDK and DFOTA packaging, and
client distribution. Install that CLI to consume a released EDK; SDK samples
do not require the firmware source checkout.

## Zephyr integration

The module descriptor at `zephyr/module.yml` exports this repository as a
Zephyr CMake/Kconfig module and contributes its board, devicetree, and module
extension roots. A consuming west workspace must make `sdk-meshbus` visible as
a west project or through `ZEPHYR_EXTRA_MODULES` before calling
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
- `scripts/`: SDK build helpers and general zui/mklfs/remote west extensions

Run Zephyr commands from the west workspace root. For example:

```sh
west build -p auto -b qemu_x86 \
  sdk-meshbus/tests/subsys/meshbus/services/clock
west twister -T sdk-meshbus/tests/subsys/meshbus/services/clock \
  -p qemu_x86 --inline-logs -v
```

The exact boards and test platforms supported by each sample or suite are
declared by its local `sample.yaml` or `testcase.yaml`.

## Development workflow

Within the Meshbus product workspace, firmware and SDK changes use the single
Spec Kit project in the firmware root. The [SDK entry guide](AGENTS.md) explains
how to locate that project's constitution and standards. Source ownership and
Git history remain separate; SDK-only work does not create a second planning
system here. Existing standalone sample builds still use their own metadata.

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
