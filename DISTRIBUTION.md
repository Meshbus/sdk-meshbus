# Product builds and distribution

The Meshbus repository owns the product firmware, reusable SDK, Rust `meshbus`
CLI, product matrix, firmware/EDK/DFOTA packaging, and candidate tooling.
Protobuf remains in its canonical independent repository. Firmware and CLI
retain separate release trains even though they share one source repository.

Commands below run from `west topdir`, with this repository at `meshbus/`.
Product profiles live under `apps/meshbus/boards/<vendor>/<board>/`.
No tool requires a Git repository or LICENSE in the workspace parent.

## Build the CLI

From the firmware west workspace, with Rust and the Zephyr environment active:

```sh
export MESHBUS_PROTO_ROOT="$(west list meshbus-protobufs -f '{abspath}')"
export CARGO_TARGET_DIR="$PWD/build-meshbus-cli"
export CARGO_ENCODED_RUSTFLAGS="--remap-path-prefix=$HOME=/build-home"
cargo build --locked --release --manifest-path meshbus/scripts/meshbus/Cargo.toml
export MESHBUS_CLI="$CARGO_TARGET_DIR/release/meshbus"
```

On Windows the executable ends in `.exe`. `west meshbus` uses `MESHBUS_CLI`
when explicitly set. Otherwise it runs `cargo build --locked --release` before
forwarding arguments, including help and version requests. Cargo builds missing
or changed inputs and reuses fresh outputs. The default executable is
`<west-workspace>/build-meshbus-cli/release/meshbus`; `CARGO_TARGET_DIR` overrides
the build directory (relative paths are resolved from the workspace), and
`CARGO` can select the Cargo executable. A CLI on PATH does not bypass this
local source check. Cargo diagnostics go to stderr so CLI JSON on stdout stays
usable. Build failure stops the command without running an older binary.

Other callers of the shared resolver, such as release packaging, only locate
existing tools: `MESHBUS_CLI`, then PATH, then the same local build directory.
They do not opt into automatic builds.

Production firmware packaging and final assembly are stricter: `MESHBUS_CLI`
must name an absolute executable path. The firmware part records that tool's
version and SHA-256, and assembly requires the same tool identity. CI therefore
builds one native host tool from the reviewed source for packaging; this does
not depend on publishing the independent six-platform CLI release train.

`west release` is the separate Python developer entry point for product builds,
archives, native client builds and final assembly. `west release -h` and
`west release matrix` work without Rust or an installed CLI. EDK export and
EDK/DFOTA verification call the existing Rust CLI only when needed; their
format algorithms are not reimplemented in Python. The distributed Rust CLI
has no `release` command and does not invoke Meshbus Python tools. The old Python wheel and macOS-only packager are
removed. CLI versioning is independent of firmware/EDK versioning.
Use Rust path remapping for manual distribution builds. `west release cli`
explicitly runs Cargo, applies path remapping and rejects binaries containing
the current workspace or user-home path. `CARGO_ENCODED_RUSTFLAGS`
keeps a remapping argument intact when a directory name contains spaces.

## Product matrix and complete images

Each APP `<normalized-target>.conf` under
`apps/meshbus/boards/<vendor>/<board>/` registers a product target. The normalized
name replaces `/` with `_`; discovery matches it against Zephyr's legal targets
instead of guessing where underscores separate the board, SoC and CPU.
Vendor and board names must match the hardware metadata. APP overlays are
optional; `_mcuboot.conf` and `_mcuboot.overlay` are companions, not targets.
Flat/deeper layouts, unknown or ambiguous targets, role qualifiers, symlinks
and orphaned companion files are rejected.

`west release matrix` lists all discovered targets: C2
(`idea_mesh_tracker_c2/nrf54l15/cpuapp`), Tracker T1000-E
(`tracker_t1000_e/nrf52840`) and Wio Tracker L1 (`wio_tracker_l1/nrf52840`).
`west release build` selects all of them
by default, including with `--development`. A repeated `--target` selects a
subset; a board ID selects all of its registered qualifiers and a complete
target selects one. There is no separate product list or GA allowlist.

Each selected board is configured, built and packaged independently. A board's
configuration, build, key-export or packaging failure does not stop the remaining
boards. Successful parts remain under `<output>/firmware/`; the final console
summary lists every target and the failed stage. The command returns nonzero if
any board fails, using the first failure's exit status. Invalid shared inputs
(such as an unknown target or invalid supplied key) fail before the loop; a user
interrupt stops immediately. Failed packaging can leave incomplete files, so
use `release-part.json` and its checksums to identify completed parts. Assembly
still requires the complete product matrix and rejects incomplete releases.

Discovery does not imply hardware qualification or approval to publish.
SDK board definitions alone do not register a product; DevKit product support
remains deferred. Discovery needs the local Zephyr checkout (`ZEPHYR_BASE` or
the workspace's `zephyr/`) and reads board/SoC definitions there and in Meshbus.

The two Seeed targets retain their UF2/SoftDevice boot paths. The packager
selects MCUboot or UF2 from the generated application configuration. UF2 builds
need no image signing key and package the application only; they require a
compatible bootloader and SoftDevice already installed on the device.

```sh
west release build --workspace "$PWD" --target tracker_t1000_e \
  --build-root build/products --output build/candidate-t1000 --development
```

Use repeated `--target` arguments to include Wio, or omit them for every discovered
board. The default matrix includes C2 and therefore requires its signing key.
Wio enables LTO with local ISR tables to fit its complete Desktop/LLEXT profile
within the existing 692 KiB application partition. The application excludes
Zephyr's generated syscall export and weak-alias bridge objects from GCC LTO
because their address-only data declarations conflict with function definitions.
The EDK removes host LTO flags so extensions contain relocatable machine code
rather than GCC intermediate objects. The final ELF retains the complete LLEXT
export table. Linker type/size warnings
for those weak aliases remain visible and require final-symbol validation.

MCUboot products use Zephyr's native build-time signing and share one Ed25519
Production Image Key across firmware versions; the DFOTA Manifest Key remains
independent. Board identity and image layout come from the selected target.
UF2 payload validation does not provide cryptographic signature verification.

Keep the private PEM outside the source and build trees and pass its file path:

```sh
west release matrix
west release build --workspace "$PWD" \
  --target idea_mesh_tracker_c2 \
  --image-signing-key /absolute/private/meshbus-image-v1.pem \
  --build-root build/products --output build/candidate
```

Use `--target idea_mesh_tracker_c2` or
`--target idea_mesh_tracker_c2/nrf54l15/cpuapp` to select C2 explicitly. All
selected MCUboot boards use the same supplied key file. There is no implicit
test key or PEM-content environment-variable interface. Use `--development`
when rehearsing with uncommitted sources or dependencies; this changes
packaging qualification, not the signing algorithm or key source.

For MCUboot, the release entry point passes the file path through Zephyr's standard
`SB_CONFIG_BOOT_SIGNATURE_KEY_FILE`. Zephyr embeds the public key in MCUboot
and invokes imgtool to produce the signed APP. No custom image configuration
script or separate Meshbus signing command is needed.

The release host or CI owns the private file, its backup and cleanup. On POSIX
it must have mode `0600` or stricter. Meshbus does not create, copy, or delete
private key files. Keep the file available while building, rebuilding, or
exporting the EDK, because Zephyr may invoke signing again. Never commit private
PEM contents or include them in logs or published artifacts. In CI, provision a
private file before invoking the command and remove it when the job finishes.

Each MCUboot sysbuild retains `image-public.pem`. Packaging verifies the native signed
APP, checks that this public key matches MCUboot's generated and linked key,
and records the key fingerprint and APP/MCUboot digests. The firmware archive
includes the public PEM. Signature verification needs no private key. The
standalone packaging command is:

```sh
west release firmware --build-dir build/<sysbuild-dir> \
  --output build/candidate-repack
```

For MCUboot builds made directly with `west build`, supply
`--image-public-key /absolute/path/to/public.pem` when packaging. Such builds
use the ordinary Zephyr key-file option and must enable APP/MCUboot metadata for
release packaging. Keep the configured private PEM path available for any
rebuild or EDK export.
If the path changes, rerun `west release build` with the new file path instead
of editing generated build configuration.

The former external-signing mode and `sign-c2`/`--signed-c2` interfaces have been
removed. Rebuild historical external-signing build trees through the native
flow; existing dated evidence is retained as historical evidence.

Use separate workspaces for
separate firmware versions; this command never checks out Git revisions or
runs `west update`. Its build directories include the version-file digest and
normalized target. Firmware part and archive names include the complete target
with `/` replaced by `_`, so multiple qualifiers cannot overwrite one another;
records store
`id` and the ordinary qualified `target`. The configured MeshCore role is not
part of the build or package identity. Every build uses sysbuild. A build can also be packaged separately:

```sh
west release firmware --build-dir build/<sysbuild-dir> \
  --output build/candidate --development
```

For LLEXT products, packaging asks the Rust exporter to run the standard
`llext-edk` build target. If this updates firmware binaries/configuration, the
package is rejected without a success record. Rebuild with `west release build`
and use a fresh output directory; do not mix an old image with a new EDK.

MCUboot product archives contain the actual bootloader and application BINs,
available HEX images, complete `full.bin`/`full.hex`, `flash-map.json`, notices
and checksums. Addresses and bounds come from final build configuration;
`full.bin` starts at its recorded address and fills intervening gaps with 0xff.
It is not an application-slot image or a DFOTA source. No command here flashes,
erases storage or authorizes erase-all.

UF2 archives contain `app.uf2`, `app.bin`, available `app.hex`, `flash-map.json`,
notices and checksums. They contain no bootloader, SoftDevice or `full.*` image.
Packaging validates UF2 headers, family ID, application partition bounds and
payload against the native HEX/BIN outputs, including sparse HEX regions.
Assembly repeats this validation against the retained application files.

Each part retains the exact application BIN separately for future delta input.
The generated record identifies target, version, final configuration/DTS
hashes, image ranges, compiler and resolved project revisions/dirty state.
Current candidates remain non-publishable and hardware qualification is not
inferred from a successful build. Rolling development manifest revisions are
not rewritten by local packaging.

## EDK and extension packages

Only LLEXT-enabled host builds produce EDKs. C2 supports the current release
packaging flow; the Wio APP profile also enables LLEXT. Build, EDK validation
and physical application execution are separate evidence levels.
The distributable EDK builds Desktop MBA packages. The host
checks package structure, metadata, target, and resource limits, but does not
require a publisher signature or allowlist.

This unrestricted MBA policy intentionally treats an authenticated BLE peer or
a user with physical UART access as a native-code publisher. C2 does not enable
MPU/userspace isolation for these apps, so firmware image signing does not
authenticate or sandbox an installed MBA. Release material must state this
trust boundary; hardware qualification must confirm unauthenticated BLE file
writes are rejected.

```sh
"$MESHBUS_CLI" edk -d build/<c2-sysbuild-dir> -o build/edk --development
"$MESHBUS_CLI" edk verify build/edk/<artifact>-edk.tar.xz
"$MESHBUS_CLI" edk qualify build/edk/<artifact>-edk.tar.xz \
  --zephyr-sdk /path/to/zephyr-sdk \
  --packages-root /path/to/c-extension \
  --packages-root /path/to/cxx-extension --packages-output build/extensions
meshbus llext --llext-sdk /path/to/llext-edk \
  --zephyr-sdk /path/to/zephyr-sdk -o build/extensions /path/to/extension
```

`edk verify` checks archive integrity and compiler inputs offline without a
toolchain. `edk qualify` additionally runs compiler/header/extension checks.

Display and ZUI headers use `<display/*.h>` and `<zui/*.h>`. The exporter
also accepts their legacy `zephyr/` layout, but rejects mixed shared roots.
Driver, devicetree binding, DFU and linker files remain outside the MBA public
header allowlist even though they now also use flat SDK paths.
This restriction applies to Meshbus-owned headers. Upstream
`<zephyr/drivers/*.h>` and the host's generated devicetree headers remain in the
EDK for native peripheral access. The LLEXT bridge exports host devices using
path-hash symbols; regenerate the EDK after adopting this export configuration.
See the [native peripheral example](samples/subsys/llext/README.rst)
for supported usage and the distinction between build, load and hardware evidence.

Current EDKs expose Meshbus headers as `<module/module.h>`, with narrow Clock,
GNSS and LLEXT capability headers documented in [the SDK guide](README.md).
Generated `meshbus/*.pb.h` paths stay unchanged.
The exporter retains an explicit list of public module directories; internal
Settings, MCUmgr and Shell helpers are excluded. Existing EDK archives with the
former `zephyr/meshbus/` or `meshbus/<module>/` layouts remain verifiable
and qualifiable. Mixed layouts or missing public module roots are rejected.

Current service APIs and exported symbols use `mbs_`, constants use `MBS_`,
and service configuration uses `CONFIG_MBS` / `CONFIG_MBS_*`. Source packages
must update both their includes and service identifiers when adopting this EDK.
Previously compiled MBA packages importing `meshbus_*` service symbols must be
rebuilt; the host does not export legacy service aliases. Old-archive verification
or qualification does not prove those packages load in the current firmware.
The earlier header-only relocation preserved symbols; this subsequent namespace
migration changes them while retaining metadata layout and numeric IDs.
See [LLEXT compatibility](subsys/llext/API_COMPATIBILITY.md) for the loader contract.

Released-EDK consumers need only the CLI, EDK, extension source, CMake, Ninja
and compiler tools. They do not need a firmware/SDK checkout, west, Python or
protoc. The CLI supplies its compiler forwarding and `xxd -ip` helper itself.
The EDK archive and compiler-input SHA retain schema 1. MBA metadata
retains its current byte layout and the SDK-generated metadata version.
The EDK's `host.version` is recorded in packages; it is not a new runtime
version-equality gate. Runtime policy remains in the SDK API compatibility doc.

The exporter preserves the `firmware` and `projects.meshbus` provenance fields;
both refer to the same source revision in this layout. The manifest project
has no dependency `manifest-rev`; firmware packaging checks only dependencies
against it. The exporter never modifies generated `build_info.yml`. Public-header pruning, internal-link materialization, path
bounds and deterministic archive metadata are enforced. Exported generated
configuration removes APP signing/encryption key paths, and offline verification
rejects an archive that retains either path. A host build may live outside the
west workspace; workspace identity comes from its recorded application source.
`--force` only replaces the same development identity. Formal EDK export
requires clean, committed source/dependencies; that is distinct from product
release approval.

## DFOTA

`meshbus firmware package create/inspect/verify` uses native Rust bsdiff,
converted to detools sequential CRLE and NEWP. No Python detools executable is
required. Package verification authenticates MCUboot images and canonical
manifest signatures, validates identities and reconstructs the exact target.

```sh
meshbus firmware package create old.signed.bin new.signed.bin build/delta \
  --role repeater --board-id devkit_nrf54l15 \
  --image-key-id 1 --image-public-key /path/to/image-public.pem \
  --manifest-key-id 1 --security-counter 1 \
  --campaign-id <32-hex-digits> \
  --manifest-signature /path/to/detached.sig \
  --manifest-public-key /path/to/manifest-public.pem
meshbus firmware package verify build/delta \
  --image-public-key /path/to/image-public.pem \
  --manifest-public-key /path/to/manifest-public.pem
```

The existing explicit `--manifest-private-key` offline/engineering interface
is retained, but candidate orchestration does not invoke it. The signer owns
key handling. Use fixed campaign ids for reproducibility. Client roles are
not Firmware endpoints. Only explicitly selected historical-image edges are
created; a patch larger than 24 KiB fails, without changing the wire contract.
Historical source images must be the originally retained signed bytes.

## CI and candidate assembly

Firmware CI and CLI CI are independent. Firmware releases use the same native
build-and-sign command locally on a trusted release host or in protected CI.
Freeze and review the source/dependency revisions before supplying the shared
Production Image Key. The build system, toolchain, and build scripts are now
inside the signing trust boundary, as accepted in ADR 0010.

Production secrets must not be available to pull requests, forks, or unreviewed
build inputs. Keep an encrypted backup of the shared key and retain its reviewed
public fingerprint. CI automation is not provisioned by the release command;
it must configure secret access and runner cleanup separately. Signing produces
a candidate, not a public release: tags, publication, and qualification remain
separate actions.

**macOS Intel toolchain limitation:** Zephyr SDK 1.0.1 has no Intel bundle.
SDK 0.17.4 is the last available Intel bundle, but its GCC 12.2 cannot compile
the current Zephyr public header using `__rbit`. The Intel EDK qualification
job is intentionally not skipped or marked successful. A compatible compiler
must be selected and validated before the six-platform gate can pass.
An Intel GNU Arm GCC 14.2 trial also failed because its newlib-based bundle
lacks the EDK-required `picolibc.specs`; it is not used as a fallback. No
headers or ABI-sensitive compiler flags are changed to hide these failures.

```sh
west release assemble --input build/candidate --output build/assembled
```

Firmware assembly requires exactly the discovered product set (C2 and both Seeed targets)
and rejects CLI parts and unregistered targets. C2 1.0.0 has no DFOTA package because there is no
previous Release Baseline. Assembly never pushes tags or creates a GitHub
Release.

Firmware packaging generates and checks four raw Zephyr SPDX 2.3 documents
per packaged image: eight for MCUboot plus APP, four for UF2 APP only.
`west release build` enables metadata automatically. Production packaging
requires metadata for every packaged image; development packages may omit it.
SPDX generation requires every active project in the west manifest to be
present. A UF2 SBOM does not inventory the preinstalled bootloader or SoftDevice.

The public firmware archive contains one curated `SBOM.spdx`. It lists only
components used by the APP or MCUboot and retains the exact version, license,
source URL and package reference reported by Zephyr where available. When module
metadata gives a release version for the same exact checkout URL, the source
SHA remains the package version and the release references are retained. It omits
source filenames and checksums plus private Firmware/SDK URLs and revisions;
those two private components use the released firmware version as their public
identity. Metadata that Zephyr reports as `NOASSERTION` is not guessed and must
be resolved during the third-party notice and license-text review. That review
remains a GA gate.

This curation applies to the customer-facing SBOM. The technical `flash-map`,
release-part, and EDK manifests currently retain opaque Firmware/SDK commit SHAs
for exact support traceability, but no private repository URL. If commit IDs are
also confidential, define and test a separate public/private provenance record
before GA rather than silently removing the only exact build identity.

The raw documents and their `SHA256SUMS` stay under
`spdx-private/<identity>/` in the sysbuild output. They include private project
origins/revisions and a generation time, so copy them into private retention
before cleaning the build. Their timestamp also means repackaging later is not
necessarily byte-identical even when the firmware binaries are reproducible.
Zephyr's `-off` suffix can also reflect local Git remote configuration, so it is
diagnostic rather than a release gate. Production packaging instead compares
every dependency checkout HEAD directly with its resolved `manifest-rev` SHA and separately
rejects any dirty Firmware or dependency checkout.

The public 1.0.0 release set should contain:

- the firmware archive with signed `app.bin`/`app.hex`, `mcuboot.bin`/
  `mcuboot.hex`, `full.bin`/`full.hex`, flash map, checksums, SPDX, licenses,
  notices, and release notes;
- the C2 EDK, its checksum, and MBA compatibility statement;
- a signed release manifest or detached signature authenticating the archives.

C2 has no UF2 output. Version 1.0.0 has no DFOTA output because no prior
Release Baseline exists. Keep APP/MCUboot ELF and map files, final configs/DTS,
the raw SPDX directory and its checksums, frozen manifest, build logs, unsigned
signing input, signed output, key fingerprint, and qualification evidence in a
private retention bundle rather than the public download.

`west release cli` remains available to the separate CLI release train. Its six
native platform archives, code signing, notarization and EDK host-platform
qualification do not block firmware GA.

Before promoting a candidate, all of these gates must be explicit passes:

- a clean committed Meshbus revision and dependency revisions with
  `apps/meshbus/VERSION` set to 1.0.0;
- reviewed release source, reviewed Production Image public key, protected PEM secret,
  key-pair fingerprint match, and tested backup/rotation procedure;
- reproducible C2 native build/signing, independent cryptographic image verification,
  SPDX/license completion, archive authentication, and immutable provenance;
- C2 hardware tests for normal boot, unsigned/wrong-key/tampered rejection,
  UART recovery, absence of BLE recovery, SWD full erase/programming, retained
  settings, authenticated MBA load/run/unload, unauthenticated BLE upload
  rejection, and stack/heap watermarks while a maximum-size MBA runs with
  concurrent BLE, radio, and UI activity;
- tagged release notes, factory programming instructions, support/rollback
  policy, artifact retention, and a final human release approval record.

## Validation

```sh
python -m unittest discover -s meshbus/scripts/release/tests -v
cargo fmt --manifest-path meshbus/scripts/meshbus/Cargo.toml --check
cargo clippy --locked --manifest-path meshbus/scripts/meshbus/Cargo.toml --all-targets -- -D warnings
cargo test --locked --manifest-path meshbus/scripts/meshbus/Cargo.toml
```

The C detools interoperability test is explicit, because it needs the pinned
C decoder build. CI runs it on Linux with `MESHBUS_DETOOLS_C` set. Frozen legacy
fixtures contain synthetic test images, public keys and Python-generated
metadata; production Python implementations are not retained. Tests never
open a probe or flash hardware. Local, remote CI, device and product-release
evidence must be reported separately.
