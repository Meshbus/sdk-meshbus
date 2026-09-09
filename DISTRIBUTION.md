# Product builds and distribution

The Meshbus repository owns the product firmware, reusable SDK, Rust `meshbus`
CLI, product matrix, firmware/EDK/DFOTA packaging, and candidate tooling.
Protobuf remains in its canonical independent repository. Firmware and CLI
retain separate release trains even though they share one source repository.

Commands below run from `west topdir`, with this repository at `meshbus/`.
Repository-relative product metadata is `apps/meshbus/boards/products.yml`.
No tool requires a Git repository or LICENSE in the workspace parent.

## Build the CLI

From the firmware west workspace, with Rust and the Zephyr environment active:

```sh
export MESHBUS_PROTO_ROOT="$(west list meshbus-protobufs -f '{abspath}')"
export CARGO_TARGET_DIR="$PWD/build/meshbus-cli/cargo"
export CARGO_ENCODED_RUSTFLAGS="--remap-path-prefix=$HOME=/build-home"
cargo build --locked --release --manifest-path meshbus/scripts/meshbus/Cargo.toml
export MESHBUS_CLI="$CARGO_TARGET_DIR/release/meshbus"
```

On Windows the executable ends in `.exe`. `west meshbus` only forwards to an
existing CLI: `MESHBUS_CLI` first, then `meshbus` on PATH, then the local
`CARGO_TARGET_DIR/release/meshbus` (default `build/meshbus-cli/cargo`). It never
runs Cargo. A missing executable fails immediately with setup instructions;
help never triggers a build.

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

Firmware-owned `apps/meshbus/boards/products.yml` declares each device's `id` and ordinary
qualified `board` target. Duplicate device identities, role qualifiers, and
malformed metadata are rejected. `west release matrix` reports the firmware GA
set, currently only `idea_mesh_tracker_c2/nrf54l15/cpuapp`. The old DevKit
product role profiles have been removed; the SDK base board remains available
for samples and tests. DevKit product support is deferred.

```sh
west release matrix
west release build --workspace "$PWD" \
  --build-root build/candidate-builds --output build/candidate --development \
  --image-signing-key /absolute/path/to/development-ed25519.pem
```

Use `--target <board-id>` or `--target <fully-qualified-board-target>` to select
the same device firmware:

```sh
west release build --target idea_mesh_tracker_c2 \
  --build-root build/products --output build/candidate-c2 --development \
  --image-signing-key /absolute/path/to/development-ed25519.pem
west release build --target idea_mesh_tracker_c2/nrf54l15/cpuapp \
  --build-root build/products --output build/candidate-c2 --development \
  --image-signing-key /absolute/path/to/development-ed25519.pem
```

The integrated `--image-signing-key` path is development-only. Production
builds embed a reviewed public key and pass the unsigned APP to a separate
protected CI signer; the production private key must never enter a build job.

The same external handoff can be rehearsed with a disposable test key. The
private PEM and signing output must be outside the source and sysbuild trees;
on POSIX the private PEM must have mode `0600` or stricter.

```sh
west build -p always --sysbuild \
  -b idea_mesh_tracker_c2/nrf54l15/cpuapp meshbus/apps/meshbus \
  -d build/c2-public-key -- \
  '-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE="/absolute/path/to/public-ed25519.pem"' \
  -DSB_CONFIG_MESHBUS_C2_EXTERNAL_SIGNING=y \
  -DCONFIG_BUILD_OUTPUT_META=y \
  -Dmcuboot_CONFIG_BUILD_OUTPUT_META=y
chmod 600 /absolute/path/to/private-ed25519.pem
west release sign-c2 --build-dir build/c2-public-key \
  --image-private-key /absolute/path/to/private-ed25519.pem \
  --image-public-key /absolute/path/to/public-ed25519.pem \
  --output /absolute/path/to/signing-bundle
west release firmware --build-dir build/c2-public-key \
  --signed-c2 /absolute/path/to/signing-bundle \
  --image-public-key /absolute/path/to/public-ed25519.pem \
  --output build/candidate-c2 --development
```

`sign-c2` accepts only the exact C2 external-signing build. It requires
a public-only PEM that matches MCUboot's generated and linked key, requires a
clean committed MCUboot signer, matches the private key, signs with the final
build parameters, and verifies the result. Its deterministic handoff contains
only `app.signed.bin`, `signing-record.json`, and `SHA256SUMS`; the record binds
the unsigned APP, MCUboot binary, key fingerprint, target, versions, signing
parameters, MCUboot revision, and `imgtool.py` digest. Packaging independently
revalidates that handoff and records its request digest.

Use separate workspaces for
separate firmware versions; this command never checks out Git revisions or
runs `west update`. Its build directories include the version-file digest and
device ID. Firmware part and archive names include `<board-id>`; records store
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

Each product archive contains the actual bootloader and application BINs,
available HEX images, complete `full.bin`/`full.hex`, `flash-map.json`, notices
and checksums. Addresses and bounds come from final build configuration;
`full.bin` starts at its recorded address and fills intervening gaps with 0xff.
It is not an application-slot image or a DFOTA source. No command here flashes,
erases storage or authorizes erase-all.

Each part retains the exact application BIN separately for future delta input.
The generated record identifies target, version, final configuration/DTS
hashes, image ranges, compiler and resolved project revisions/dirty state.
Current candidates remain non-publishable and hardware qualification is not
inferred from a successful build. Rolling development manifest revisions are
not rewritten by local packaging.

## EDK and extension packages

Only LLEXT-enabled host builds produce EDKs; currently this is C2.
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

Firmware CI and CLI CI are independent. The firmware production workflow must
freeze resolved dependencies, build only C2 on Linux using the reviewed
Production Image public key, and pass an immutable unsigned APP artifact to a
separate signing job. Only a push to protected `main` may enter the
`production-signing` environment and read its PEM secret. The signed output is
still an Engineering Candidate until the separate GA gates pass.

The signing job must use only digest/SHA-pinned actions and a reviewed immutable
signer tool, with read-only repository permissions, no pull-request trigger,
no cache containing the PEM, and no execution of the unsigned artifact. It must
match the secret key fingerprint to the tracked public key, bind the output to
the build request digest, verify the completed image independently, and remove
the temporary PEM before exit. Required review and CODEOWNERS for workflow,
signer, and key-policy changes are part of the key boundary. Every protected
`main` push may create a signed candidate; only an approved version tag may
publish one.

The public-key-only build, immutable signing handoff, post-sign verification,
and signed-image packaging interfaces are implemented. The production workflow
is still intentionally inactive: the repository has no reviewed production
public key or `production-signing` environment, and the current private
organization repository plan does not provide branch protection or rulesets.
Do not expose the Production Image private key until all three prerequisites
exist. The signer does not delete a caller-owned key file, so CI must materialize
the PEM in an ephemeral location and remove it on every exit path.

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

Firmware assembly requires exactly the C2 product part and rejects CLI
parts and DevKit fixtures. C2 1.0.0 has no DFOTA package because there is no
previous Release Baseline. Assembly never pushes tags or creates a GitHub
Release.

When both APP and MCUboot enable `CONFIG_BUILD_OUTPUT_META`, firmware packaging
generates and checks eight raw Zephyr SPDX 2.3 documents directly from that
exact build. `west release build` enables both settings automatically for C2.
A production C2 package rejects missing metadata; development packages may omit
it. SPDX generation requires every active project in the west manifest to be
present.

The public firmware archive contains one curated `SBOM.spdx`. It lists only
components used by the APP or MCUboot and retains the exact version, license,
source URL and package reference reported by Zephyr where available. It omits
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

The eight raw documents and their `SHA256SUMS` stay under
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
- protected `main`, reviewed Production Image public key, protected PEM secret,
  key-pair fingerprint match, and tested backup/rotation procedure;
- reproducible C2 build, independent signing, cryptographic image verification,
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
