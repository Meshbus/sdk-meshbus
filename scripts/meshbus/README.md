# Meshbus Rust CLI

`meshbus` is the standalone Rust user client for device operations and offline
EDK/DFOTA/extension tools. Firmware developers build and archive products with
Python `west release`; the Rust CLI has no `release` command. `west meshbus`
checks the local Rust build with Cargo before executing it. Missing or changed
inputs are rebuilt incrementally; fresh outputs are reused. Its default build
directory is `<west-workspace>/build-meshbus-cli`, overridden by
`CARGO_TARGET_DIR` (relative to the workspace). Setting `MESHBUS_CLI` explicitly
uses that executable and skips the local build. Build failures stop execution.

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
  --role repeater --board-id devkit_nrf54l15 \
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
wrong-key MCUboot images, a non-increasing version/security counter, offline
reconstruction mismatch, a non-canonical manifest, and a complete NEWP blob
larger than 24 KiB.

Stream targets sequentially through one locally connected client. Target status
is authoritative after timeout or restart, and `--state` contains no secret:

```sh
meshbus firmware delta send out/ -p /dev/cu.usbmodem... \
  --target 01020304 --target aabbccdd \
  --state campaign.json --yes
meshbus firmware delta status -p /dev/cu.usbmodem... --target 01020304
meshbus firmware delta abort -p /dev/cu.usbmodem... --target 01020304 \
  --transfer-id <64-hex-digits> --yes
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

One native archive is produced per OS/architecture. User operations, including
EDK and delta packaging, are Rust; product orchestration is Python. Extension compilation requires
external CMake, Ninja and Zephyr SDK. Firmware builds/EDK generation require a
standard west environment; the firmware-owned `west meshbus` adapter builds
the local CLI as needed and forwards arguments. No private keys, production
signatures or notarization are created by candidate packaging. See the firmware
`DISTRIBUTION.md` guide.

### Standalone firmware programming

Firmware programming requires an exact device profile name. The first profile
is `idea_mesh_tracker_c2`; the abbreviated name `c2` is intentionally not
accepted. The same firmware interface is available as either the standalone
`meshbus firmware` command or `west meshbus firmware` from a configured west
workspace. Inspect the images and resolved address ranges without connecting
to a debug probe:

```sh
meshbus firmware inspect \
  --device idea_mesh_tracker_c2 \
  --app meshbus-client-c2-app.bin
```

For example, the equivalent west command is:

```sh
meshbus firmware inspect \
  --device idea_mesh_tracker_c2 \
  --app meshbus-client-c2-app.bin
```

Program a routine application update through the only connected CMSIS-DAP
probe, or select an exact probe ID when more than one is connected:

```sh
meshbus firmware flash \
  --device idea_mesh_tracker_c2 \
  --app meshbus-client-c2-app.bin

meshbus firmware flash \
  --device idea_mesh_tracker_c2 \
  --app meshbus-client-c2-app.bin \
  --probe <exact-probe-id> \
  --yes
```

`.bin`, `.hex`, and `.ihex` inputs are supported. C2 binary addresses are
profile-owned: MCUboot starts at `0x00000000` and the application starts at
`0x00020000`. Intel HEX records are rejected when any addressed data falls
outside the selected role's C2 partition. A merged provisioning image may
restore MCUboot, application, and persistent partition data through
`0x00164fff`. A provisioning image is mutually exclusive with separate
bootloader and application images:

```sh
meshbus firmware flash \
  --device idea_mesh_tracker_c2 \
  --provision meshbus-client-c2-provision.hex
```

Programming uses the probe-rs library embedded in the executable. It normally
erases only image-covered sectors and preserves other partitions. Full
internal-RRAM erase is accepted only with a complete merged
provisioning image or both MCUboot and application images:

```sh
meshbus firmware flash \
  --device idea_mesh_tracker_c2 \
  --provision meshbus-client-c2-provision.hex \
  --erase-all \
  --yes
```

`--erase-all` destroys MCUboot, application firmware, settings, messages, and
the extra filesystem before programming. The tool uses the nRF54L15 chip-erase
operation across all internal RRAM, writes the supplied complete image set,
verifies every programmed segment, then reads back every unwritten flash range
and requires it to remain `0xff` before resetting the device. Device recovery
is not part of this command.

The interactive console exposes the Meshbus service command tree, including
Bluetooth, Channel, Clock, Contact, Display, filesystem, GNSS, Indicator,
Input, LLEXT, Management, MeshCore, Message, Power, Radio, and Telemetry.
Use `help [path]` for contextual usage. Device logs remain visible above the
active prompt. Logs are written to stderr and command results to stdout.

Commands that power off, reboot, erase, delete, or reset persistent state
require `--yes`.

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
provider devices remain exclusively owned by the GNSS heading runtime. The
runtime stays idle until the local Compass widget holds a lease. `gnss status`
exposes its read-only heading state; `gnss config set` selects electronic
Compass or GNSS course through the `electronic_compass` field.

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
debug path and never persists its password.

Both the west adapter and standalone executable use the descriptor embedded at
Rust build time. Building requires the canonical `meshbus-protobufs` checkout,
but uses a vendored `protoc`; running does not require either one. Neither path
carries a second handwritten protobuf definition. Opening the port does not
perform a Meshbus compatibility handshake. An unsupported firmware reports an
error when a command is executed.

The firmware service namespace is `mbs_` / `MBS_`; the executable and interactive
service commands remain `meshbus`, `clock`, `radio`, and so on. Host environment
variables retain their `MESHBUS_*` names. Protobuf descriptors also keep their
schema namespace. EDK consumers must update service imports and rebuild old MBA
packages for the new firmware; see [EDK migration](../../DISTRIBUTION.md#edk-and-extension-packages).
