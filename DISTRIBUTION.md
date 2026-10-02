# Product builds and distribution

This guide covers product firmware, EDKs, delta packages and the standalone
Meshbus CLI. Firmware and CLI have independent versions in
`apps/meshbus/VERSION` and `scripts/meshbus/Cargo.toml`. A version in source
does not establish that a release is published or qualified on hardware.

Device qualification and host-platform packaging have different constraints,
so their release cycles are independent. Each firmware release records compatible
CLI versions. Alpha releases include the six CLI platform archives as companion
downloads with their own CLI version. Firmware assembly consumes firmware parts
only; Alpha staging verifies the CLI parts separately.
CLI code signing, notarization and platform coverage belong to the CLI release.

A GA Release is production-qualified firmware made available for supported
Product Targets. A Release Candidate or Engineering Candidate is not a GA
Release. An Engineering Candidate is a non-publishable firmware build used to
collect engineering evidence before GA; it may be authorized by a Production
Image Key without carrying a production qualification claim.

Start with [workspace setup](README.md#workspace-setup) and
[development prerequisites](DEVELOPMENT.md). Commands below run from
`west topdir`, with this repository at `meshbus/`. Replace angle-bracket
placeholders before running them. Use separate build and output directories
for each candidate. Packaging does not flash a device, push a tag, or publish
an artifact.

## Build the CLI

With Rust and the Zephyr Python environment active:

```sh
export MESHBUS_PROTO_ROOT="$(west list meshbus-protobufs -f '{abspath}')"
export CARGO_TARGET_DIR="$PWD/build-meshbus-cli"
export CARGO_ENCODED_RUSTFLAGS="--remap-path-prefix=$HOME=/build-home"
cargo build --locked --release --manifest-path meshbus/scripts/meshbus/Cargo.toml
export MESHBUS_CLI="$CARGO_TARGET_DIR/release/meshbus"
```

On Windows, the executable ends in `.exe`. `west meshbus` uses `MESHBUS_CLI`
when explicitly set. Otherwise it runs Cargo's locked release build before
forwarding any arguments, including help and version requests. Its default
target directory is `<west-workspace>/build-meshbus-cli`; relative
`CARGO_TARGET_DIR` values are resolved from the workspace. `CARGO` can select
the Cargo executable. Build diagnostics go to stderr and a failed build stops
execution, even when an older executable exists.

`west release` is the Python entry point for product builds and packaging.
`west release -h` and `west release matrix` do not require Rust or a built CLI.
Packaging locates an existing CLI through `MESHBUS_CLI`, PATH, or the local
build directory; it does not build one automatically. Production packaging of
LLEXT products requires `MESHBUS_CLI` to name an absolute executable path.
Assembly checks its version and SHA-256 against the tool recorded by packaging.

To build an archive for the current host:

```sh
west release cli --workspace "$PWD" --output build/cli-candidate
```

This explicitly invokes Cargo, applies path remapping, and rejects binaries
containing the current workspace or home path. The supported archive targets
are arm64 and x86-64 for macOS, Windows and Linux. Use `--target` with a
configured Cargo linker/SDK to cross-compile; the default target is the build
host. The command does not sign or notarize the executable. Names use the
firmware release version: `meshbus-cli-<release>-<target>.tar.gz` (Windows `.zip`).
The manifest separately records the independent Cargo/executable version.
Each `meshbus/` archive root contains the executable, manifest.json, SHA256SUMS
and NOTICE.txt. Detailed dependency/generator materials stay in the private CI
part; see [licensing](LICENSING.md#distribution-notices-and-evidence).

See [CLI usage](scripts/meshbus/README.md).

## Product matrix and builds

```sh
west release matrix
```

APP `.conf` profiles under `apps/meshbus/boards/<vendor>/<board>/` are the
product inventory. Filenames contain the full qualified target with `/`
replaced by `_`. Optional `.overlay`, `_mcuboot.conf`, and `_mcuboot.overlay`
files accompany the APP profile. Discovery validates names against local
Zephyr and SDK board metadata and rejects unknown or ambiguous targets,
role qualifiers, symlinks and orphaned companions. See the
[product guide](apps/meshbus/README.md#device-firmware-and-meshcore-role).

`west release build` selects every registered target unless restricted with
repeated `--target` options. A board ID selects all its registered qualifiers;
a full target selects one. The MeshCore runtime role is not part of a product's
build or archive identity. Registration does not imply hardware qualification.

The command always uses sysbuild. Production builds merge `prj.conf`, the board
profile, and `prj.prod.conf` in that order. `--development` selects `prj.dev.conf`
instead and permits packaging dirty or off-manifest sources. Production enables
size optimization, local ISR tables and LTO. Development preserves
board-required optimizations, including LTO. These fragments affect only the
APP; ordinary `west build` retains board defaults unless explicitly configured.

Build directories are
`<build-root>/<VERSION-file-digest>/<dev|prod>/<unsigned|ed25519>/<normalized-target>/`.
The authentication directory reflects whether the build command supplied a key;
UF2 products remain unauthenticated even in a mixed invocation with a key.
Use different output directories for development and production packages;
packaging refuses an already populated product destination. The command does
not check out revisions or update west dependencies.

### MCUboot products

The public SDK defaults to MCUboot without signature authentication. A build
needs no private key; MCUboot still checks image structure and hash on every
boot. Mesh Probe R2 retains its single-application layout and physical UART recovery.
Hash verification detects corruption, not the identity of a publisher.
Downstream users can build and distribute firmware without adopting a
Meshbus-controlled trust root, retaining the native image format and recovery.

```sh
west release build --workspace "$PWD" \
  --target '<qualified-mcuboot-target>' \
  --build-root build/products --output build/firmware-candidate
```

For an authenticated downstream product, opt into Ed25519 explicitly:

```sh
west release build --workspace "$PWD" \
  --target '<qualified-mcuboot-target>' \
  --image-signing-key /absolute/private/meshbus-image.pem \
  --build-root build/products --output build/authenticated-candidate
```

`--image-signing-key` selects `SB_CONFIG_BOOT_SIGNATURE_TYPE_ED25519` and passes
the file path through `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE`. MCUboot embeds the
public key; Zephyr's imgtool step signs the APP. All selected MCUboot targets
use that key. Missing, invalid or mismatched keys fail rather than falling back
to an unsigned build. MCUboot repository example keys are rejected. Private PEM
contents are not accepted through an environment variable.

The Production Image Key is the trust root selected by an authenticated MCUboot
product to authorize application images for execution. The caller owns it;
sharing it across boards means a compromise affects every board trusting it.
A signature identifies a publisher, not a compatible board, layout or individual
device; target and partition checks remain necessary. Authenticated MCUboot validates the primary
image on every boot. Native sysbuild/MCUboot signing avoids a separate
build-to-signer handoff while trusting the reviewed build inputs with the key.

For products adopting this signed boot policy, the official firmware trust
boundary extends from MCUboot through the verified base application. It ends
when the owner launches an MBA or replaces the boot chain, and does not
establish the integrity of retained user data. Signed rollback means executing
an older official application still authorized by the Production Image Key;
it is distinct from automatic recovery after a failed update. Whether rollback
is permitted depends on the [product policy](apps/meshbus/README.md#product-policy-and-release-qualification).

Authentication is independent of `--development`, which controls source and
build-profile checks. Clean, pinned unsigned builds can be packaged without
that flag. Signed builds retain full verification even in development mode.

Keep private keys outside source and build trees, with POSIX permissions `0600`
or stricter. The release operator owns storage, backup and cleanup. Keep the
configured PEM available during rebuilds and EDK export because Zephyr may sign
again. The reviewed sources, build scripts and toolchain share that trust
boundary. Never put private keys in source, logs or artifacts.

Authenticated release builds retain `image-public.pem`; packaging checks the APP
signature and public key against MCUboot's generated and linked key. Unsigned
packages contain no public PEM or signing claim. Both modes validate the native
MCUboot header, hash and partition bounds. A filename such as
`zephyr.signed.bin` is a Zephyr output convention and does not prove that a
signature is present. Mesh Probe R2 packaging requires primary-slot validation and UART-only
MCUboot recovery in both modes.

Install a matching MCUboot/application pair for the selected authentication
mode. An Ed25519 MCUboot requires an authenticated APP. Plan physical
programming and retained-data handling separately. See the
[application build instructions](apps/meshbus/README.md) for native sysbuild use.
Hashing and signing do not qualify hardware, authorize publication or authenticate
[owner-supplied MBA code](scripts/meshbus/APP_DEVELOPMENT.md#execution-and-trust-model).

### UF2 products

Select a UF2 target from the matrix:

```sh
west release build --workspace "$PWD" \
  --target '<qualified-uf2-target>' \
  --build-root build/products --output build/uf2-candidate --development
```

UF2 targets need no image signing key. They require a compatible preinstalled
bootloader and any platform runtime required by the profile. Packaging checks
UF2 headers, family ID, partition bounds, and payload against native HEX/BIN
outputs, including sparse HEX regions. This is structural verification, not
cryptographic authentication.

### Package existing builds

```sh
west release firmware --build-dir 'build/<sysbuild-dir>' \
  --output build/repacked-candidate
```

For authenticated MCUboot builds made directly with `west build`, provide
`--image-public-key /absolute/path/to/public.pem`. Production packaging requires
`CONFIG_BUILD_OUTPUT_META=y` in every packaged image; `west release build`
enables this automatically. `west release firmware --development` relaxes
source/metadata requirements for an existing build; it does not reconfigure
optimization or bypass signature verification.

For LLEXT products, packaging exports and verifies an EDK, then generates a
local `release-source.json`. Pass `--app-sdk /path/to/sdk-arduboy` to include an
Arduboy SDK in that index. EDK export runs the native `llext-edk` build target.
If it changes the firmware binaries or configuration, packaging rejects the
part. Rebuild and package into a fresh output directory.

A per-target build or packaging failure does not stop other selected targets.
The command reports each target's outcome and returns nonzero if any failed.
Shared input errors fail before the loop; interruption stops immediately.
Failures can leave partial files: only a completed `release-part.json` and its
checksums identify a completed part.

## Firmware archive contents

A Factory Image is the complete first-install firmware set for a Product Target,
including required boot components and the application. It differs from an
application-only update and is not a backup of per-device data. The archive
contents depend on the boot profile; UF2 archives require separately supplied
boot components as described above.

| Format | Images |
| --- | --- |
| MCUboot | Bootloader and bootable APP BINs, available HEX files, merged `firmware.bin` / `firmware.hex`; public verification PEM only when authenticated |
| UF2 | `app.uf2`, `app.bin`, available `app.hex`; no bootloader, SoftDevice or merged image |

Archives also include `flash-map.json`, SHA256SUMS, NOTICE.txt,
and an SBOM when metadata is available. Image addresses and bounds come from
the final build. `flash-map.json` also records `CONFIG_SOC` and native image
SHA-256 values. It is copied beside `release-part.json` and consumed by
`meshbus firmware inspect/flash --manifest` after extracting the matching images;
the CLI does not maintain a separate product partition table. `firmware.bin` begins
at its recorded address and fills gaps with
`0xff`; it is not an application-slot image or a delta-package input.

The Release Baseline is the exact released application bytes and release
identity retained for future update packages; authenticated formats require an
appropriately signed baseline. Each part retains the exact APP BIN, together with
target/version identity, final configuration and DTS hashes, compiler identity,
and resolved source/dependency revisions. Records identify `authentication` as
`none` or `ed25519`; only authenticated records include verified `signing`
metadata. Assembly's production-signing gate applies only when authenticated
products are included. Retain exact APP bytes; recreating an old version later
does not establish the same delta baseline.

## EDK and extension packages

LLEXT-enabled builds produce EDKs for Desktop MBA applications. An EDK carries
the host's headers, exports, build flags and target and format metadata. MBA metadata
uses version 1. Use an EDK matching
the intended firmware and run compiler qualification on every host
platform you intend to support. Archive verification, extension compilation,
and physical application execution establish different results.

```sh
"$MESHBUS_CLI" edk -d 'build/<sysbuild-dir>' -o build/edk --development
"$MESHBUS_CLI" edk verify 'build/edk/meshbus-edk-<version>-<board>.tar.xz'
"$MESHBUS_CLI" edk qualify 'build/edk/meshbus-edk-<version>-<board>.tar.xz' \
  --zephyr-sdk /path/to/zephyr-sdk \
  --packages-root /path/to/c-extension \
  --packages-root /path/to/cxx-extension --packages-output build/extensions
"$MESHBUS_CLI" llext --llext-sdk /path/to/extracted-edk \
  --zephyr-sdk /path/to/zephyr-sdk -o build/extensions /path/to/extension
```

`edk verify` checks archive integrity, compiler inputs and license texts without
a toolchain. `edk qualify` adds compiler/header checks and builds the supplied
extensions. Compiler selection must match the EDK's target ABI and C library;
availability of a CLI archive for a host does not qualify its compiler.

EDKs use `meshbus-edk-<version>-<board>.tar.xz`. They retain compiler inputs,
source header declarations and one NOTICE.txt covering actual exported headers.
Schema 2 binds this notice to its digest; schema 1 archives remain verifiable.
Font arrays and the full font catalog are not part of the EDK.

For public downloads, keep release-manifest.json beside the EDK archive;
`edk verify` checks its archive size and SHA256 from that manifest. Private
exports retain their `.sha256` sidecars. Missing or conflicting checksums fail.

The low-level `llext` builder needs the CLI, extracted EDK, extension source,
CMake, Ninja and compatible compiler tools. Its built-in compiler forwarding
and `xxd -ip` helper do not require Python. Extension-specific build steps can
have additional dependencies. The managed `meshbus app source/sync/build`
workflow also requires a local Python installation and records its path along
with CMake and Ninja. See [MBA development](scripts/meshbus/APP_DEVELOPMENT.md)
for source indexes, locks, installation and device sessions.

`release-source.json` describes local installations; it is not a portable tool
bundle. It can contain absolute toolchain, tool and SDK paths. Prepare an index
for the consumer's installations instead of publishing a release-host index
as a ready-to-use cross-host configuration.

Current SDK service headers use `<module/module.h>`, service symbols use
`mbs_` / `MBS_`, and generated headers retain `meshbus/*.pb.h`. Display and ZUI
headers use `<display/*.h>` and `<zui/*.h>`. The exporter includes an explicit
public-header allowlist and excludes internal Settings, MCUmgr and Shell
helpers, plus SDK-owned driver, binding, DFU and linker headers. Upstream
Zephyr driver and generated devicetree headers remain available for native
peripheral access; see the [LLEXT sample](samples/subsys/llext/README.rst).

Build MBA packages against an EDK matching the target firmware. Metadata
version 1 is checked along with target identity, required symbols and resource
requirements. Build revision and firmware image differences warn rather than
block installation or execution; package file integrity remains mandatory. See
[MBA metadata](subsys/llext/METADATA.md) for runtime checks.

The exporter removes APP signing/encryption key paths from generated
configuration and verification rejects retained paths. `--force` replaces only
the same development identity. Formal export requires clean committed inputs;
an EDK's export status is not product release approval.

Distributed products must explain the
[MBA execution and trust model](scripts/meshbus/APP_DEVELOPMENT.md#execution-and-trust-model).
Loader compatibility checks and base-image authentication do not create an
application isolation boundary.

## DFOTA

`meshbus firmware package create/inspect/verify` creates and authenticates
detools sequential CRLE packages containing NEWP patches. No Python detools
executable is required. Source and target must be signed MCUboot APP images;
UF2 and merged `full.*` images are not inputs. The target version must increase,
and its protected security counter must not decrease and must match
`--security-counter`. The complete NEWP patch is limited to 24 KiB.
The public firmware's hash-only default does not relax these signed-image
requirements.

```sh
"$MESHBUS_CLI" firmware package create old.signed.bin new.signed.bin build/delta \
  --role repeater --board-id '<board-id>' --soc-id '<soc-id>' \
  --image-key-id 1 --image-public-key /path/to/image-public.pem \
  --manifest-key-id 1 --security-counter 1 \
  --campaign-id '<32-hex-digits>' \
  --manifest-signature /path/to/detached.sig \
  --manifest-public-key /path/to/manifest-public.pem
"$MESHBUS_CLI" firmware package verify build/delta \
  --image-public-key /path/to/image-public.pem \
  --manifest-public-key /path/to/manifest-public.pem
```

The Firmware endpoint roles are `repeater`, `room` and `sensor`; choose the
identity and layout fields to match the target. The DFOTA Manifest Key authorizes
package metadata independently of the Production Image Key that authorizes
application execution. The manifest private key stays behind an offline signing
boundary. The explicit `--manifest-private-key`
interface is available for offline engineering use; production workflows can
import a detached signature as above. The signer owns private-key handling.
Fixed campaign IDs and identical inputs permit reproducible package creation.
See the [CLI guide](scripts/meshbus/README.md#commands) for transfer commands.

Delta creation requires a previously retained source image; a version number
alone does not supply a baseline. Firmware assembly's optional `--delta-package`
path cannot map packages to the device-profile product matrix. Distribute
verified delta packages separately from firmware assembly.

## Candidate assembly and provenance

```sh
west release assemble --input build/firmware-candidate --output build/assembled
```

Assembly requires exactly the complete discovered product set, one firmware
version, valid checksums and compatible EDKs. It rejects CLI parts and
unregistered products. Output must be outside the input tree. The generated
release record remains `publishable: false`; assembly does not qualify hardware
or authenticate archives for publication.

Production firmware packaging generates four raw SPDX 2.3 documents per image:
eight for MCUboot plus APP, or four for a UF2 APP. Every active manifest project
must be present for SPDX generation. A UF2 SBOM does not cover its preinstalled
bootloader or SoftDevice. Raw documents and their checksums are retained under
`spdx-private/<identity>/` in the sysbuild output.

The archive's curated `SBOM.spdx` lists compiled components and available
source revisions, license expressions and source references. Meshbus source
identity is retained in the `meshbus-sdk` component; the firmware package records
the product version. Source locations must be credential-free repository or
download URLs, and the generator omits source-file inventories. Retain source
references and license information with the release. Resolve permission gaps
through contribution review; an SPDX `NOASSERTION` field alone does not establish
that the actual permission is unknown.

Firmware archives contain one NOTICE.txt with applicable component, selected
font, schema, dictionary and toolchain runtime terms. Detailed selection and
material files stay under `material-evidence/` beside the archive in private CI
parts. Staging regenerates the notice and checks the existing material and font
constraints. Missing/changed required materials fail; development collections
without SPDX remain `partial-no-spdx` and are ineligible for public staging.

[Licensing](LICENSING.md#distribution-notices-and-evidence) owns selection and
contribution obligations; binary packaging does not change source redistribution
requirements.

Technical flash maps, release-part and EDK manifests retain exact source SHAs.
Raw SPDX data can also include local source origins and generation times.
Retain these records with build logs, ELF/map files, final configs/DTS, frozen
dependency revisions, signed-image inputs/outputs and qualification evidence.
Review the selected publication files rather than uploading the entire build
or assembly directory. Timestamps mean repackaging identical images need not
produce byte-identical archives.

Production packaging rejects dirty source/dependency checkouts and compares
dependency HEADs with their resolved `manifest-rev` SHAs. Rolling manifest
branches are not rewritten by packaging; retain resolved revisions, toolchain
identity and the relevant key fingerprints with each release.

## Publication requirements

The requirements below apply to production-qualified GA publication. Public
Alpha eligibility is a separate channel described under
[CI Alpha publication](#ci-alpha-publication); it does not satisfy or remove
these device and production requirements.

Repository-owned SDK, firmware and CLI code use Apache-2.0, with file/subtree
exceptions described in [third-party notices](LICENSING.md). Review
the applicable source and notice obligations for every delivered component;
binary packaging does not replace that review.

Apply the [compiled-dependency license policy](LICENSING.md#compiled-dependency-admission)
to each delivered target's actual inputs, including runtime libraries, generated
code and fonts. Record selected permissive alternatives and applicable
exceptions; uncompiled tools still retain their own redistribution obligations.

Before publishing a firmware release, record:

- the committed source, dependency revisions and intended firmware version;
- signature verification and key identity for authenticated products, plus
  archive authentication and the release operator's key backup/rotation policy;
- per-target boot, recovery, factory programming, retained-data and update
  results for the selected profile, including rejected-image tests where
  authentication is enabled;
- MBA load/run/unload, resource usage and unauthenticated-write rejection where
  Desktop/LLEXT and authenticated transports are part of the product;
- source delivery, completed license/SBOM review, release notes, programming
  instructions, support/rollback policy and retained qualification evidence.

Verify only the transports and capabilities actually configured for a target;
do not apply Mesh Probe R2's MCUboot/UART policy to a UF2 target. CLI code signing,
notarization and host-platform qualification are separate release work.

## CI Alpha publication

The immutable first public pilot, `v1.0.0-alpha.1`, contains R1 downloads.
Subsequent Alpha releases include every registered APP target and all six CLI
platform archives. Firmware uses a canonical `vMAJOR.MINOR.PATCH-alpha.N` tag
matching its committed VERSION; companion CLI packages keep their Cargo version.
Use a new Alpha number for the expanded matrix; never replace alpha.1 assets.

An Engineering Candidate remains unpublished and records `publishable: false`.
An eligible public Alpha adds a separate publication manifest: full CI Twister
evidence for the exact source/Builder and fresh strict candidate validation,
production-profile packaging for the complete product matrix, license/SPDX
collection, EDK compiler qualification, assembly and automatic distribution
material checks must pass. Permission decisions belong to contribution review
under the existing admission policy. Hardware validation stays `not-run`, production qualification
stays false and R1 UF2 authentication stays `none`. CI cannot establish boot,
RF, recovery, resource/soak or MBA runtime acceptance.

The current public set contains **14 assets**:

| Count | Download | Archive contents |
| --- | --- | --- |
| 4 | `meshbus-<release>-<board>.tar.gz` | Actual app.bin/hex/uf2; MCUboot products also mcuboot.bin/hex and firmware.bin/hex; flash-map.json, SBOM.spdx, SHA256SUMS, NOTICE.txt |
| 3 | `meshbus-edk-<release>-<board>.tar.xz` | Public compiler inputs, source headers, edk-release.json, NOTICE.txt |
| 6 | `meshbus-cli-<release>-<target>.tar.gz` (Windows `.zip`) | meshbus (or meshbus.exe), manifest.json, SHA256SUMS, NOTICE.txt |
| 1 | `release-manifest.json` | Product/CLI identity, qualification and payload hashes |

Firmware and CLI archive roots remain `firmware/` and `meshbus/`; EDK remains
`llext-edk/`. No standalone APP, SBOM or outer SHA256SUMS is published. Full
qualified targets remain in metadata; conflicting board basenames fail.
UF2 still requires an existing compatible bootloader and SoftDevice. MCUboot
app.bin is a slot image; firmware.bin/hex are merged images whose addresses come
from flash-map.json. Preserve storage and existing boot components.

Manifest schema 3 records the matrix, release and independent CLI versions,
source/tag, frozen dependencies, immutable builder, qualification and every
payload's size/SHA256. Internal Actions inventory additionally hashes the public
manifest, covering all fourteen files. Schemas 1/2 remain supported for immutable
alpha.1/2. Staging can normalize retained legacy candidates after validation;
newly produced archives are already compact. Private SPDX, raw material files,
build logs and release-source.json are excluded from public downloads.

### Distribution material evidence

Candidate preparation checks the complete matrix's component, font, runtime,
generated-code and EDK materials and native proofs. `--evidence-only` retains
`alpha-license-evidence/license-evidence.json` without public assets. Alpha
staging performs the same checks and then generates its public set.

Evidence schema 2 preserves product/CLI scopes, selected declarations, dependency
revisions, material hashes and qualification. `scope_sha256` is provenance,
not approval. Missing/empty notices, corrupt hashes, incomplete runtime evidence,
restricted/unreviewed fonts and source/version/asset conflicts remain blocking.
No approval field or per-Alpha approval file is required. Contribution obligations
and license choices live in [licensing](LICENSING.md); each public archive retains
its applicable terms in NOTICE.txt.

Only CI-owned drafts can be resumed from their original verified upload set.
Uploaded bytes are downloaded and checked before public visibility. The final
Release is a Prerelease with `make_latest=false`, followed by anonymous download
verification. Published bytes are never replaced or deleted by this workflow.
See [Alpha operation and recovery](.github/CI.md#alpha-operation-and-recovery).

## Host-tool validation

From the configured workspace, use the checks relevant to the changed tool:

```sh
python -m unittest discover -s meshbus/scripts/release/tests -v
cargo fmt --manifest-path meshbus/scripts/meshbus/Cargo.toml --check
cargo clippy --locked --manifest-path meshbus/scripts/meshbus/Cargo.toml --all-targets -- -D warnings
cargo test --locked --manifest-path meshbus/scripts/meshbus/Cargo.toml
```

The optional C detools interoperability check requires `MESHBUS_DETOOLS_C` to
name the prepared C decoder. Host tests do not establish device behavior,
physical recovery, production signing or public-release qualification.

## Actions candidate preparation

The manual [Candidate preparation workflow](.github/CI.md) requires full
commit SHAs throughout the manifest graph and successful strict validation.
It reuses successful full main CI Twister and default-product evidence only for the same source,
resolved manifest graph and immutable Builder digest. Missing or conflicting
evidence blocks preparation; run full **CI** first with that commit and image.
The snapshot and public manifest retain the reused baseline run identity.
It builds six CLI targets, checks the produced bytes on native hosts, assembles
all discovered firmware products and compiles C/C++ samples against every
exported LLEXT EDK.
The default product matrix is verified from that baseline; each Candidate builds
production configuration and packaging metadata once. Completion records exact
artifact IDs, digests and the qualifying run attempt.
Linux/Windows cross builds record both build host and output target; macOS uses
Apple-hosted tooling. `west release cli --target <triple>` supports configured
cross toolchains. `--development` explicitly permits dirty/off-manifest local
engineering packages; these are not clean candidates.

Snapshots and candidate artifacts expire after 90 days. Candidate completion
requires successful strict checks, including the license policy and font inventory.
Neither artifact upload nor successful assembly authorizes production signing
or publication. See the workflow guide for bootstrap, environment identity and
remaining qualification boundaries.

Alpha tag runs first select a retained successful main Candidate for the exact
source and version. Staging uses its recorded immutable Builder and rechecks the
complete artifact matrix, bytes, native/EDK qualification and license materials.
Reused Candidates receive current vulnerability checks under the same policy.
Missing or expired automatic selections fall back to fresh preparation; explicit
Candidate conflicts, corrupt inputs and API failures fail staging. The release
manifest distinguishes the Candidate run from the publishing run. Existing
published assets are never rewritten by this reuse mechanism.

Manual Alpha dispatch exercises staging without a remote tag or Release write.
An optional Candidate run ID forces that exact source of evidence. Successful
dry-runs retain the verified asset inventory; only a tag-push run can publish.
