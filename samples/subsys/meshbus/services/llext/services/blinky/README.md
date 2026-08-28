# blinky LLEXT Service

`blinky` is a Meshbus boot service extension packaged as `.mbs`.

The service logic is in `src/main.c`. Author metadata is defined in
`llext.yaml`; `west meshbus llext` injects metadata format version, EDK version, exact
`target`, and heap estimate into ELF section `.meshbus.llext.meta`
(`struct meshbus_llext_service_metadata`).

Generated artifact:

- `build.meshbus_llext_service_hw/zephyr/llext/blinky.mbs`

Target runtime location:

- `/extra/svcs/blinky.mbs`

## 1) Build host firmware

Run from workspace root (`meshbus/..`):

```bash
source ~/.zephyr/env/bin/activate
BOARD=idea_mesh_tracker_c2/nrf54l15/cpuapp

west build -p auto -b ${BOARD} \
  -d build.meshbus_llext_service_hw \
  -s sdk-meshbus/samples/subsys/meshbus/services/llext
```

The host firmware build is the image you flash to the device. `west meshbus llext`
uses this same build directory for EDK generation, EDK extraction, and service
extension outputs.

## 2) Build `blinky` service extension

Run from workspace root (`meshbus/..`):

```bash
west meshbus llext -d build.meshbus_llext_service_hw \
  sdk-meshbus/samples/subsys/meshbus/services/llext/services/blinky
```

On first use, `west meshbus llext` runs the host build's `llext-edk` target, copies
the EDK tarball to `build.meshbus_llext_service_hw/zephyr/llext/`, and extracts
it to `build.meshbus_llext_service_hw/zephyr/llext/llext-edk`. Repeated builds
reuse this managed EDK cache.

Expected output:

- `build.meshbus_llext_service_hw/zephyr/llext/blinky.mbs`

## 3) Upload service package to device

- `build.meshbus_llext_service_hw/zephyr/llext/blinky.mbs` ->
  `/extra/svcs/blinky.mbs`

Reboot the device after upload. Services are only discovered during boot.

## 4) Validate service status from shell

```text
meshbus llext service list
meshbus llext service status blinky
```

## Metadata notes

- service identity comes from metadata `id`; runtime filename basename is not
  used.
- `metadata-version`, `edk-version`, `target`, and `heap-size` are injected by
  the build helper and must not appear in `llext.yaml`.
- the runtime requires a supported metadata format and exact target. EDK
  version is provenance and does not reject an otherwise valid package.
- the helper reads target and version provenance from the EDK release manifest.
- `entry-point` must be a valid C symbol name and exported by
  `LL_EXTENSION_SYMBOL(...)`.
- `stack-size` is required and must be greater than zero.
- `west meshbus llext` injects metadata and sets
  `.meshbus.llext.meta=contents,readonly` so metadata is scanned from ELF but
  not loaded into service RAM.
- `heap-size` is an estimated LLEXT heap requirement. The manager sums all valid
  service estimates and adds
  `CONFIG_MESHBUS_LLEXT_DYNAMIC_HEAP_MARGIN_PERCENT` before heap init.
- A direct standalone CMake build creates a plain `.llext` without metadata
  injection.
