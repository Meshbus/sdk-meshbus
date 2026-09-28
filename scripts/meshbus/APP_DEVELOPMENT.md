# MBA projects from local release artifacts

The managed host workflow targets macOS Apple Silicon and local files. These
commands do not download inputs or need GitHub, west, or firmware source.
Obtain an EDK matching the intended firmware, plus local CMake, Ninja, Python
and compatible GNU Arm compiler installations. The managed workflow records
and verifies those tools at their local paths, including Python for native
projects. An index is not a redistributable tool bundle. Device operations use
UART MCUmgr and require firmware with supported identity, Desktop and LLEXT
services. See [distribution](../../DISTRIBUTION.md#edk-and-extension-packages)
for EDK export and target requirements.
EDKs must contain the flat Meshbus public headers and the independent ZUI/U8g2
headers. Both direct and managed MBA builds require a named `exported-symbols`
inventory. SLID hosts export an explicit null inventory; their EDKs can be
exported and verified, but cannot be used by these MBA build commands.

Create an index from an EDK directory or `.tar.xz` archive and the installations
on this host. Omit `--sdk` for native projects:

```sh
meshbus app source --output release-source.json \
  --edk /path/to/llext-edk.tar.xz --toolchain /path/to/zephyr-sdk \
  --sdk /path/to/sdk-arduboy --profile default \
  --cmake /path/to/cmake --ninja /path/to/ninja --python /path/to/python3
```

`source` derives the exact target, firmware build revision and metadata version from the EDK. It refuses to overwrite an existing index. Without
explicit host tools it resolves them from PATH once and records their canonical
paths. Only the selected ARM compiler subtree is hashed. This command describes
existing release inputs. `west release build` and `west release firmware` also
generate `release-source.json` beside each LLEXT EDK, before writing the success
record and checksums. Pass `--app-sdk /path/to/sdk-arduboy` to include the SDK;
otherwise that source supports native projects. Generated indexes retain the
release host's tool paths; create an index for another machine's installations
before using it there.

Create a project and select one exact entry from that index:

```sh
meshbus app new my-game --template arduboy
meshbus app --project my-game target --source release-source.json \
  --target '<qualified-board-target>' \
  --firmware '<build-revision-from-index>' --profile default
meshbus app --project my-game sync
meshbus app --project my-game doctor
meshbus app --project my-game build --locked --offline
```

Use `--template native` for a C example. Native and Arduboy templates generate
readable CMake projects; edit CMake for additional sources and compiler options.
The Arduboy Sketch lives in `src/sketch/` so automatic Sketch source collection
does not duplicate the separate runtime entry. Both templates use the existing
MBA builder, including its EDK, import, metadata and memory checks.

`build/<id>.mba` and its JSON reports are the outputs. SDK compatibility and
resource conversion remain owned by sdk-arduboy. Explicit local development
builds are also available:

```sh
meshbus app --project my-game build --edk /path/to/edk \
  --toolchain /path/to/zephyr-sdk --sdk /path/to/sdk-arduboy
```

## Project declaration

Managed projects use `project-schema: 1` in `llext.yaml` with the following
declaration fields. This project schema is separate from the MBA binary's
metadata version 1.

```yaml
project-schema: 1
id: my-game
name: My Game
version: 0.1.0
entry-point: app_main
stack-size: 4096
build:
  template: arduboy
dependencies: {}
requires:
  - symbol:printk
```

`build` currently accepts only `template`. `dependencies` optionally maps
`sdk-arduboy` to its content SHA256; no other dependency types are silently
accepted. `requires` lists `symbol:<name>` entries that must appear in the EDK's
actual export inventory. SDK-specific capability checks also run through the
SDK build interface. Unknown fields and unsupported declarations fail before
compilation. A `build --sdk <directory>` override is explicit local development
input; the report includes its actual path and digest even with a project lock.

## Index and lock contract (schema 1)

The index has `schema`, `minimum_cli` (three numeric version components), and
`releases`. Each release records `target`, `firmware`, `profile`, `host_platform`,
`metadata_version` (1), `edk`, `toolchain`, optional
`sdk`, and `tools` (`cmake`, `ninja`, `python`). Each input is `{ "path": "...", "sha256":
"..." }`. Index paths can be relative to the index directory. Duplicate release
identities are rejected. An optional `image_sha256` distinguishes exact firmware
images. Selection matches target, firmware, profile and host platform, plus the
image hash when selecting from a connected device. An ambiguous match is
rejected; there is no nearest-version or same-board fallback.

A file's SHA256 is the usual hash of its bytes. Directory identities sort files
by relative path, then hash each UTF-8 path prefixed by its little-endian u64
byte length, followed by the little-endian u64 file size and contents. `.git`,
`__pycache__` and `.DS_Store` are excluded; `.scratch`, `build`, `build-*` and
`out` are excluded only at the input root. Nested `include/build` is preserved.
File symlinks must resolve within the input directory; directory symlinks and
special files are rejected. These hashes provide integrity within an explicitly
trusted local source, not independent publisher authentication.

`target` writes local `.meshbus-target.json`. `sync` validates inputs, atomically
caches EDK/SDK by digest, and writes `meshbus.lock`. The lock retains the source
selection and origin identities as well as cached paths, so removing the local
selection file does not prevent locked builds. Commit the declaration and lock;
local installation paths may require explicit reselection/update on another
machine. This local workflow does not provide cross-host tool portability.

An existing lock is preserved by ordinary `sync`. Missing EDK/SDK caches can be
restored from the locked origin; complete caches do not require the original
index or source directories. Corrupted inputs fail rather than being accepted.
`--cache-dir` overrides the default `~/Library/Caches/meshbus/inputs`.
`--locked --offline` never resolves newer inputs. Changing the declaration or
selection requires explicit `app update`; if the source index changes, select
it again first. To move a missing cache to another cache directory, use `sync`
without `--locked` to record its new path. Tools remain at their pinned locations.

`doctor` is read-only and reports missing/changed inputs, stale declarations,
wrong host platforms, incompatible EDK identity and compiler version mismatches.
Restore the indicated input, or explicitly select/update to accept new inputs.
Build success proves a package was produced for that EDK; it does not prove
installed firmware identity, physical display behavior, audio or device execution.

## Select a compatible connected device and manage a Session

Identity-capable firmware reports a stable hardware ID and the SHA256 of its
actual MCUboot image (header, payload and TLVs, excluding slot padding). The
publisher's EDK records the same digest. This is an exact build match, not a
signature or an authentication claim. Firmware without this endpoint
can still be targeted explicitly for offline builds.

```sh
meshbus app --project my-game --device '<UART-port-or-USB-serial>' \
  target --source release-source.json
meshbus app --project my-game update
meshbus app --project my-game build --locked
meshbus app --project my-game start my-game --path /extra/apps/my-game.mba
meshbus app --project my-game status
meshbus app --project my-game stop my-game --timeout-ms 5000
```

Device selection rejects ambiguous interfaces; macOS `tty`/`cu` aliases of the
same UART use the call-out endpoint when selecting by USB serial. A local
`.meshbus-device.json` binding verifies hardware identity on subsequent opens.
Remove that binding explicitly to choose another device. The project lock must
match the connected target and metadata format before starting an app.
Different build revisions or firmware image hashes warn that the app may
malfunction, but allow it to run. Missing required symbols still reject loading.
`start` expects an already installed MBA. Use the installation commands below
or `run` to build, install and start one.

A start acknowledgement contains a device-generated Session ID. The CLI waits
for running, ended or failed status. `stop` targets that exact Session and asks
it to exit cooperatively; it never aborts the app thread. The sdk-arduboy build
enables polling only when the matching EDK exports the stop API. Native apps can
poll `mbs_desktop_app_stop_requested()` and return normally. A timeout or cleanup
failure retains a failed Session with `resources_reclaimed: false`; replacing
its files is forbidden. Query status and retry cleanup after the app returns.
Use `--session` to reject a stale instance and `--log-file` to save logs received
while exchanging commands. Deferred firmware logs may arrive after a short
command has finished; this option alone is not a continuous log follower.

## Generic file collection (schema 1)

Every successful MBA build also writes `<id>.install/package.json`. Recollect
an existing MBA and its matching schema 1 build report with
`meshbus app package app.mba`. The report must explicitly declare the boolean
`resource_collection`; the CLI never infers it from files left by another build.
The manifest identifies the MBA, application version, build provenance, host requirements,
resource paths, installation destinations, byte sizes and SHA256 digests. The
CLI checks these against the actual MBA metadata and import table. Resources
are opaque files; their conversion remains in the SDK.

The schema discriminator is `schema: 1, kind: "meshbus-app"`. `mba` is one file
record, `resources` is an array (empty for ordinary apps), and `host` contains
`target`, `firmware`, optional `image_sha256`, `metadata_version` (1) and
required exported symbol names. Build revision and image identity are advisory
at runtime, while package file hashes remain mandatory. Each file record contains
`path` relative to the collection, `destination` under `/extra/apps`, `length`
and `sha256`. Paths cannot traverse directories or address saves. Duplicate
files/destinations, missing files, conflicting MBA identity/metadata, wrong digests
and unsupported schemas are rejected.

Arduboy `install.json` schema 1 is adapted into the generic collection,
retaining its MBA and sidecar destinations.
Its identity, MBA bytes and sidecar descriptor must match. No resource payload
is parsed or re-encoded by the CLI. An EDK without an exact image hash can
produce a collection; installation warns about unknown build provenance and
continues. Neither matching hashes nor successful loading prove application
behavior on the device.

## Run and install

After selecting the connected device and syncing the project inputs:

```sh
meshbus app --project my-game run --follow-seconds 5
```

`run` builds first, checks the bound device's firmware identity, installs the
complete file collection, confirms the managed Session and follows logs. It
allows replacement of this registered app. A build failure leaves the existing
device app unchanged. Without `--follow-seconds`, observation continues until
the app ends or Ctrl-C is pressed. Ctrl-C ends observation and retains the app;
it does not flash or reset firmware.

The independent installation operations are:

```sh
meshbus app --project my-game install            # build/<id>.install/package.json
meshbus app --project my-game install --replace
meshbus app --project my-game installed
meshbus app --project my-game installed my-game
meshbus app --project my-game start my-game
meshbus app --project my-game uninstall my-game
meshbus app --project my-game uninstall my-game --remove-saves
```

Replacement stops only the same managed app and requires confirmed resource
reclamation. Another app, an uncooperative app, or an unknown stop result blocks
file mutation. Unregistered files are not overwritten or adopted. Uninstall
removes registered current/previous files and preserves saves unless
`--remove-saves` explicitly selects this app's `.dat`, `.sav` and corresponding
`.tmp` files under `/extra/saves` for removal.

## Space and recovery

Ordinary replacement can reclaim the registered previous payload before
uploading a new one; it does not promise rollback of that payload.
`install --replace --atomic` and `run --atomic` use version directories and
retain a complete previous version when space permits. Arduboy resource builds
also require the EDK's resource-location resolver. Capacity checks include files,
transaction records and directories, and report required/free bytes on failure.

The installer verifies local files, uploads a control record, reserves a
transaction, uploads and checks device file hashes, then commits. Pending
transactions block MBA launch; a registry rename selects the complete group.
Failed cleanup retains a journal for recovery:

```sh
meshbus app --project my-game recover my-game
meshbus app --project my-game recover my-game --abort
meshbus app --project my-game rollback my-game
```

`recover` retries commit/cleanup; it cannot supply missing uploaded bytes. Abort
an incomplete upload before reinstalling. Abort is refused after commit and
during rollback or uninstall; resume those operations with `recover`.
An interrupted uninstall retains its original save-removal policy. A complete
matching install is idempotent within the same layout. To switch identical
content between direct and atomic layouts, uninstall while retaining saves
and reinstall in the desired mode. These recovery paths do not establish
physical power-cut durability.

## Watch, logs and diagnostics

```sh
meshbus app --project my-game watch
meshbus app --project my-game logs --session 123
meshbus app --project my-game capture frame.png
meshbus app --project my-game diagnose --section .text --offset 0
```

Replace `123` with the Session ID returned by the device. `watch` coalesces
source changes for 500 ms and performs one build/deploy at a time, ignoring
build outputs and its own state/logs. A failed revision is not retried
indefinitely. After connection loss, reconnect the same bound device and save
a source file to retry. Ctrl-C releases the UART and reports the last query
result without stopping the app.

Each `run` records a directory under `.meshbus-runs/` with device identity,
package digests, Session, raw device logs, MBA and matching symbols.
`.meshbus-last-run.json` references the latest confirmed start. Firmware logs
are a shared stream and deferred lines can predate the Session; protocol replies
determine Session state. `logs` follows an exact Session on that shared stream.
Keep these local records and device bindings out of source control.

`capture` verifies the recorded build is running before and after reading the
frozen frame. It writes a PNG and companion JSON associated with that Session.
This is software framebuffer evidence, not physical display readback.
`diagnose` verifies the archived MBA and `.symbols.elf` digests before invoking
local `addr2line`. Supply a section-relative offset from matching symbols;
without a trusted runtime address map, a raw runtime PC is not sufficient.
Run archives survive later builds and failed deployments.
