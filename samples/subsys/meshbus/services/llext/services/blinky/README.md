# blinky LLEXT Service

`blinky` is a Meshbus boot service extension packaged as `.mbs`.

The service logic is in `src/main.c`. Author metadata is defined in
`llext.yaml`; `meshbus llext` injects metadata format version, EDK version, exact
`target`, and heap estimate into ELF section `.meshbus.llext.meta`
(`struct meshbus_llext_service_metadata`).

Generated artifact:

- `build/llext/blinky.mbs`

Target runtime location:

- `/extra/svcs/blinky.mbs`

## Build the service extension

Install the Rust `meshbus` CLI, CMake, Ninja and a compatible Zephyr SDK.
Use an EDK with the `service` profile for the intended host; the C2 product's
app-profile EDK cannot substitute for a service-profile EDK.

```sh
meshbus llext --llext-sdk /path/to/service-edk.tar.xz \
  --zephyr-sdk /path/to/zephyr-sdk -o build/llext \
  sdk-meshbus/samples/subsys/meshbus/services/llext/services/blinky
```

No Firmware source checkout or complete Zephyr workspace is needed to build
this extension. See the parent sample README for building the SDK test host.

## Upload service package to device

- `build/llext/blinky.mbs` ->
  `/extra/svcs/blinky.mbs`

Reboot the device after upload. Services are only discovered during boot.

## Validate service status from the device shell

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
- `meshbus llext` injects metadata and sets
  `.meshbus.llext.meta=contents,readonly` so metadata is scanned from ELF but
  not loaded into service RAM.
- `heap-size` is an estimated LLEXT heap requirement. The manager sums all valid
  service estimates and adds
  `CONFIG_MESHBUS_LLEXT_DYNAMIC_HEAP_MARGIN_PERCENT` before heap init.
- A direct standalone CMake build creates a plain `.llext` without metadata
  injection.
