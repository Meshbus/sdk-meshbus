# Meshbus LLEXT EDK Release

Each Meshbus firmware release publishes an EDK generated from that exact host
build. The application `VERSION` file is the single version source for both
the firmware and EDK. `METADATA_VERSION` independently tracks the `.mba/.mbs`
metadata wire format.

## Build

Build the host firmware first, then generate its EDK:

```sh
west build -p auto \
  -d build.meshbus_client.c2 \
  -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
  app

west meshbus edk \
  -d build.meshbus_client.c2 \
  -o release/0.1.0
```

A formal EDK requires clean sdk-meshbus and Zephyr Git worktrees. During development,
use `--development`; the archive is marked non-publishable and receives a
`-dev` filename marker. `--force` is available only with `--development`.

The command produces exactly:

```text
<application>-<version>-<target>-<profile>-edk.tar.xz
<application>-<version>-<target>-<profile>-edk.tar.xz.sha256
```

There is no ABI JSON, rolling baseline, or baseline promotion step.

## Manifest

The archive contains `edk-release.json`. Its compatibility-relevant identity
has this shape:

```json
{
  "schema": 1,
  "metadata-version": 1,
  "publishable": true,
  "host": {
    "application": "meshbus_client",
    "version": "0.1.0"
  },
  "target": "idea_mesh_tracker_c2/nrf54l15/cpuapp",
  "profile": "app"
}
```

The full manifest also records source, Zephyr, toolchain, header-policy, and SDK
digests. `host.version` is the EDK version injected into package metadata.

## Archive Contents

The release EDK retains upstream Zephyr EDK inputs plus the approved Meshbus SDK
public header roots:

```text
include/zephyr/display
include/zephyr/meshbus
include/zephyr/zui
```

The EDK does not contain Python tooling, an API contract, ABI fingerprints,
source baselines, or package API reports. Install the independently published
`meshbus-cli` package to build `.mba` and `.mbs` artifacts.

`meshbus-cli` has its own release version and is published only when the CLI
changes. It is not tied to each firmware version. Compatibility is determined
by whether the CLI understands the EDK manifest schema and metadata format.

## Build A Package

After installing `meshbus-cli` and extracting the EDK:

```sh
meshbus llext \
  --llext-sdk /path/to/llext-edk \
  --zephyr-sdk /path/to/zephyr-sdk \
  -o build/llext \
  /path/to/package-source
```

The packager validates target/profile, estimates the required LLEXT heap, and
injects `metadata_version`, `edk_version`, target, heap size, and app icon data.
Package authors cannot override these generated fields in `llext.yaml`.

At runtime, an EDK-version difference is visible in Desktop properties and the
LLEXT shell but is not a rejection condition. See `API_COMPATIBILITY.md` for
the best-effort compatibility policy.
