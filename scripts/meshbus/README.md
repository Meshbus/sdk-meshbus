# Meshbus Rust CLI

`meshbus` is the standalone Rust user client for device operations and offline
EDK/DFOTA/extension tools. Firmware developers build and archive products with
Python `west release`; the Rust CLI has no `release` command. `west meshbus`
checks the local Rust build with Cargo before executing it. Missing or changed
inputs are rebuilt incrementally; fresh outputs are reused. Its default build
directory is `<west-workspace>/build-meshbus-cli`, overridden by
`CARGO_TARGET_DIR` (relative to the workspace). Setting `MESHBUS_CLI` explicitly
uses that executable and skips the local build. Build failures stop execution.

CLI and bundled implementation notices are maintained in the root
[third-party notices](../../LICENSING.md#distribution-notices-and-evidence).
The release tool includes the applicable texts in each CLI archive.

## Commands

- `meshbus app` creates, synchronizes and builds projects from local release
  artifacts; see [MBA development](APP_DEVELOPMENT.md).

- `meshbus llext` builds a Desktop `.mba` package.
- `meshbus edk` exports an EDK; `edk verify` checks it offline; `edk qualify` runs compiler checks.
- `meshbus firmware package` creates or verifies delta packages.

The Rust CLI owns delta generation, detached-manifest verification, target
inspection and transfer. Create and verify a package before sending it:

The current publisher contract is detools sequential CRLE. The Rust
client rejects a package whose inventory or inner patch header
declares another compression format.

```sh
meshbus firmware package create source.signed.bin target.signed.bin out/ \
  --role repeater --board-id '<board-id>' --soc-id '<soc-id>' \
  --image-key-id 1 --image-public-key image-root.pem \
  --manifest-key-id 1 --security-counter 2 \
  --manifest-private-key manifest-engineering.pem
meshbus firmware package verify out/ \
  --image-public-key image-root.pem \
  --manifest-public-key manifest-root.pem
meshbus firmware delta inspect out/
```

Production signing should import a detached signature with
`--manifest-signature` and `--manifest-public-key`; private production keys do
not belong in a checkout or package. The package command rejects unsigned or
wrong-key MCUboot images, a non-increasing image version, a decreasing or
mismatched security counter, offline reconstruction mismatch, a non-canonical
manifest, and a complete NEWP blob larger than 24 KiB. The target's protected
security counter must equal `--security-counter`; it may equal the source's
counter. Select board, SoC and layout fields for the actual Firmware endpoint.

Stream targets sequentially through one locally connected client. Target status
is authoritative after timeout or restart, and `--state` contains no secret:

```sh
meshbus firmware delta send out/ -p /dev/cu.usbmodem... \
  --target 01020304 --target aabbccdd \
  --state campaign.json --yes
meshbus firmware delta status -p /dev/cu.usbmodem... --target 01020304
meshbus firmware delta abort -p /dev/cu.usbmodem... --target 01020304 \
  --transfer-id '<64-hex-digits>' --yes
```

Use `--no-apply` to stop after target-side patch verification or
`--no-activate` to leave an already reconstructed candidate pending reboot.
The sender waits for the activate response before accepting transport loss,
then requires a confirmed post-reboot status before reporting success.

## UART Management console

This is the operator/configuration console for a locally attached Meshbus device.
Firmware is a transport-independent MCUmgr service: if a product profile
enables UART SMP and exposes a Firmware or standard image-group command, MCUmgr
dispatches it to the same handler used for every other transport. Current
target release policy normally prefers MCUboot serial mode for physical UART
recovery and keeps the LoRa Management allowlist limited to Firmware
`DELTA_*` commands.

Connect to a release firmware that enables Base64 UART MCUmgr:

```sh
meshbus connect -p /dev/cu.usbmodem...
```

Interactive mode keeps one serial session open while displaying device logs.
Tab completes nested command paths, configuration options, enum values, and
custom command choices. Up/Down navigate command history, Ctrl-C clears the
current input, and Ctrl-D exits. Use `--color auto|always|never`,
`--no-history`, or `--quiet-logs` to override the corresponding defaults.

### Standalone Rust executable

The customer-facing `meshbus connect` and `meshbus firmware` entry points are
implemented by one Rust executable. It embeds the canonical protobuf descriptor
and the probe-rs CMSIS-DAP programming backend, so the target machine does not
need Python, west, protoc, pyOCD, OpenOCD, or a Zephyr workspace:

```sh
meshbus --version
meshbus connect -p /dev/cu.usbmodem...
meshbus firmware probes
```

Build and archive from the firmware west workspace:

```sh
export MESHBUS_PROTO_ROOT="$(west list meshbus-protobufs -f '{abspath}')"
export CARGO_TARGET_DIR="$PWD/build-meshbus-cli"
west release cli --workspace "$PWD" --output build/candidate
```

One archive is produced for the current host OS/architecture by default.
Use `--target <Cargo triple>` with a configured linker and SDK for cross builds;
supported targets cover x86-64 and ARM64 on Linux, Windows and macOS. Candidate
packaging requires clean source at manifest revisions; `--development` permits
dirty/off-manifest engineering archives. Device operations, EDK verification
and delta packaging run in Rust. Low-level
extension compilation requires external CMake, Ninja and compatible GNU Arm
tools; the managed `meshbus app` workflow also records a local Python
installation. Extension-specific build steps can add dependencies. Firmware
builds and EDK export require a standard west environment. CLI candidate
packaging does not code-sign or notarize the executable. See
[distribution](../../DISTRIBUTION.md) and [MBA development](APP_DEVELOPMENT.md).

### Standalone firmware programming

Firmware inspection and SWD programming consume the matching build-generated
`flash-map.json` from the extracted firmware archive. The map provides the
qualified target, SoC, native image hashes, addresses and partition bounds.
Product layouts are not compiled into the CLI. Use metadata from a trusted
build or release: matching hashes establish byte correspondence, not publisher
identity or the identity of the physical board.

Inspect images without connecting to a debug probe:

```sh
meshbus firmware inspect --manifest firmware/flash-map.json --app firmware/app.bin
# The same interface is available through west:
west meshbus firmware inspect --manifest firmware/flash-map.json --app firmware/app.bin
```

BIN addresses come from the manifest. BIN and Intel HEX (`.hex`/`.ihex`) inputs
must reproduce the native image size and SHA-256 in the map and fit its partition.
Sparse HEX holes within an image are materialized as verified `0xff` bytes.
The programming plan shows the target, SoC and resolved image segments.

Program an application using the only connected probe, or select an exact serial:

```sh
meshbus firmware flash --manifest firmware/flash-map.json --app firmware/app.bin
meshbus firmware flash --manifest firmware/flash-map.json --app firmware/app.bin \
  --probe '<exact-probe-id>' --yes
```

The embedded probe-rs backend selects connection settings and physical memory
limits by SoC. Its current adapter supports `nrf54l15`; unknown SoCs fail before
probe discovery or connection. Metadata for other SoCs can still be inspected.
The part-register check confirms the MCU model, not the board model. UF2 products
are installed through their compatible preinstalled UF2 bootloader; this SWD
interface accepts their native BIN/HEX for offline inspection.

Complete MCUboot programming can use `--bootloader` plus `--app`, or one merged
image. Provisioning requires all native images declared by the manifest:

```sh
meshbus firmware flash --manifest firmware/flash-map.json --provision firmware/firmware.hex
meshbus firmware flash --manifest firmware/flash-map.json --provision firmware/firmware.bin \
  --erase-all --yes
```

Merged padding must be `0xff`. Only declared image segments are programmed;
merged-file gaps cannot write storage or inject extra data. Ordinary programming
preserves bytes outside those segments. `--erase-all` requires the complete
MCUboot/application set and erases the SoC's internal nonvolatile memory,
including settings and files. APP-only UF2 metadata cannot authorize chip erase
or bootloader provisioning. After full erase, the CLI verifies programmed bytes
and every unwritten range before resetting. Device recovery is outside this
command.

The interactive console exposes the Meshbus service command tree, including
Bluetooth, Channel, Clock, Contact, Display, filesystem, GNSS, Indicator,
Input, LLEXT, Management, MeshCore, Message, Power, Radio, and Telemetry.
Use `help [path]` for contextual usage. Device logs remain visible above the
active prompt. Logs are written to stderr and command results to stdout.

Commands that power off, reboot, erase, delete, or reset persistent state ask
for `y`/`yes` at an interactive `Confirm ... [y/N]` prompt. For non-interactive
use, pass `--yes` to `meshbus connect`, outside the `--command` string.

Configuration commands use named options consistently across services. A bare
command reads the current configuration, specified fields are merged with the
current values and written once, and `--reset` restores service defaults:

```sh
bluetooth config
bluetooth config --enabled on --passkey-mode fixed
radio config --frequency 915000000 --tx-power 17
gnss config --time-sync=on
radio config --reset
```

Unspecified fields retain their current values. `--field=value` and
`--field value` are equivalent. Use `help <service> config` or Tab after a
configuration command to list its available fields. `--reset` cannot be
combined with field updates and follows the normal destructive-command
confirmation rules.

Telemetry channel/provider routing is compiled from the product devicetree.
Settings and `connect` only control the sampling policy; there is no runtime
provider selector. Inspect the fixed channels and read one by numeric Zephyr
sensor channel ID with:

```sh
telemetry status
telemetry read 13
```

Missing hardware does not fall back to another sensor. Electronic Compass
provider devices remain exclusively owned by the GNSS heading runtime. It
acquires data while a consumer, such as the Compass widget, holds a heading
lease. `gnss status` exposes its read-only heading state. Select electronic
Compass with `gnss config --electronic-compass on`, or GNSS course with
`gnss config --electronic-compass off`.

For scripts, run one command and keep stdout machine-readable:

```sh
meshbus connect -p /dev/cu.usbmodem... \
  --command "power status" --json --quiet-logs
```

Use `--log-file <path>` to append raw device logs and `--timeout <seconds>` to
change the serial/request timeout. Remote LoRa management wrappers use
`--remote-timeout` (130 seconds by default) and wait for the correlated result
instead of returning only the local acceptance tag.

Management credentials are exposed as passwords at the terminal boundary:

```sh
management config
management config --password 12345678
contact set 01020304 management_password 12345678
contact set 01020304 management_password --clear
management remote password set 01020304 new-pass-123
```

Passwords contain 8 to 16 printable, non-whitespace ASCII characters. A bare
`management config` returns only `Set`/`Not set` and effective remote-SMP
capacity. Interactive history is stored below
`$XDG_STATE_HOME/meshbus/history`, or `~/.local/state/meshbus/history`, with
restricted permissions. Commands containing management passwords, private
keys, fixed passkeys, channel keys, or other catalog-marked secrets are never
written to it; `--no-history` disables history entirely. Remote rotation
updates the local Contact only after the correlated
remote response is decoded as successful; timeout leaves the Contact unchanged,
and a remote-success/local-save failure is reported as a distinct partial
success. `management remote smp_with_password` is the transient recovery and
debug path and never persists its password. Raw remote operations accept
`read`/`0` and `write`/`2`, using the SMP request operation values.

Both the west adapter and standalone executable use the descriptor embedded at
Rust build time. Building requires the canonical `meshbus-protobufs` checkout,
but uses a vendored `protoc`; running does not require either one. Neither path
carries a second handwritten protobuf definition. Opening the port does not
perform a Meshbus compatibility handshake. An unsupported firmware reports an
error when a command is executed.

The firmware service namespace is `mbs_` / `MBS_`; the executable and interactive
service commands use `meshbus`, `clock`, `radio`, and so on. Host environment
variables use `MESHBUS_*` names. Protobuf descriptors use their schema namespace.
MBA metadata uses version 1;
build each package against an EDK for its target. Firmware build differences
warn but do not block execution when the required interfaces remain available. See
[EDK requirements](../../DISTRIBUTION.md#edk-and-extension-packages).

## Display capture

Firmware with Display MCUmgr and U8G2 full-buffer snapshots can export the last
complete software frame. From this repository's root, with the development
environment active:

```sh
capture_tmp="$(mktemp -d "${TMPDIR:-/tmp}/meshbus-display.XXXXXX")"
python scripts/display_capture.py --port '<serial-port>' \
  --output-dir "$capture_tmp/display-capture"
```

For a requested screenshot deliverable, replace the temporary output with its
explicit destination and retain it. Use a new output directory. The helper
assembles and validates one frozen snapshot, then writes `frame.bin`, `frame.png`,
enlarged `preview.png` and
`result.json`. Only `result.json` records a completed capture; failures write
`failure.json`. The helper uses Python's standard library and resolves/builds
the repository CLI once; `MESHBUS_CLI` selects an existing executable instead.
Defaults are 115200 baud and a five-second request timeout; see `--help`.

For an individual chunk:

```sh
meshbus connect -p '<serial-port>' --json --command 'display dump'
```

It returns at most 256 pixel bytes, encoded as hex, plus a snapshot ID and
metadata. Continue with
`display dump <snapshot-id> <offset> <length>` using that same ID. One snapshot
slot is shared across clients; a new capture invalidates an older ID. Do not
combine chunks from different snapshots. Prefer the helper for a complete PNG.

A capture proves the transferred software frame, not physical panel output.
It does not reset or flash the device. Managed MBA projects can instead use
[`meshbus app capture`](APP_DEVELOPMENT.md#watch-logs-and-diagnostics) to associate
the frame with the running application Session.

## Input injection

Firmware with Input MCUmgr exposes synthetic actions and raw edges. Desktop
navigation also requires an active Desktop input bridge. For example:

```sh
meshbus connect -p '<serial-port>' --json --command 'input status'
meshbus connect -p '<serial-port>' --json --command 'input inject act key 108 short'
meshbus connect -p '<serial-port>' --json --command 'input inject raw key 108 0'
```

This requests Down and releases the key. Desktop's numeric key codes are
Up `103`, Down `108`, Left `105`, Right `106`, Enter `28` and Back `1`.
Use `short` for a click or `long` for an already interpreted long-press action.
Raw values `1` and `0` publish press/release edges downstream of gesture
detection; raw press/release alone does not generate a click.

Always release an injected key. The first event can wake an inactive display
and be consumed; a matching release clears wake suppression before the next
navigation action. `accepted: true` confirms event publication, not UI
consumption or completed rendering. Observe the resulting screen after a
bounded wait. These commands have the active screen's normal effects, including
settings and power actions, and do not test physical buttons or debounce.
