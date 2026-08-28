# Meshbus west commands

`west meshbus` groups SDK-owned packaging and device utilities. Product
firmware build and release orchestration belongs to the separate Meshbus
application repository and is intentionally not exposed here.

## Commands

- `west meshbus llext` builds a `.mba` or `.mbs` package.
- `west meshbus edk` creates one public EDK from an existing host build.
- `west meshbus firmware package` creates or verifies delta packages.

West owns deterministic delta generation and detached-manifest verification;
the standalone Rust executable owns target inspection and transfer. Create and
verify a package before exposing it to a device:

The current publisher contract is detools sequential CRLE. Both the west
verifier and Rust client reject a package whose inventory or inner patch header
declares another compression format.

```sh
west meshbus firmware package create source.signed.bin target.signed.bin out/ \
  --role repeater --board-id devkit_nrf54l15 \
  --image-key-id 1 --image-public-key image-root.pem \
  --manifest-key-id 1 --security-counter 2 \
  --manifest-private-key manifest-engineering.pem
west meshbus firmware package verify out/ \
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

This is the locally attached `meshbus_client` operator/configuration console.
Firmware is a transport-independent MCUmgr service: if a product profile
enables UART SMP and exposes a Firmware or standard image-group command, MCUmgr
dispatches it to the same handler used for every other transport. Current
target release policy normally prefers MCUboot serial mode for physical UART
recovery and keeps the LoRa Management allowlist limited to Firmware
`DELTA_*` commands.

Connect to a release firmware that enables Base64 UART MCUmgr:

```sh
west meshbus connect -p /dev/cu.usbmodem...
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

Install Rust with rustup, activate the Zephyr environment so the build can
locate `meshbus-protobufs`, and package from macOS with:

```sh
source "$HOME/.cargo/env"
source ~/.zephyr/env/bin/activate
python scripts/meshbus/package_macos.py
```

Release packaging requires clean sdk-meshbus and schema repositories. Use
`--development` only for a local snapshot. The output is written below
`build/meshbus-cli/dist/` with a manifest containing source, schema, size,
and SHA-256 identities. A local build is ad-hoc signed unless
`--codesign-identity` supplies a Developer ID Application identity. Build once
per target operating system and architecture; public macOS distribution still
requires Developer ID signing and notarization.

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
west meshbus firmware inspect \
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
west meshbus connect -p /dev/cu.usbmodem... \
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
