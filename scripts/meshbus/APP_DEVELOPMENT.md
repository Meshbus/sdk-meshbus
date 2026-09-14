# MBA projects from local release artifacts

The first supported host is macOS Apple Silicon, using local files. No command
below downloads inputs or needs GitHub, west, or firmware source. The firmware
publisher supplies a matching EDK and a local release index. Keep the imported
CMake, Ninja, Python and ARM compiler installations available at their recorded
paths; they are verified local installations, not redistributable tool bundles.

The publisher can create an index from an EDK directory or `.tar.xz` archive:

```sh
meshbus app source --output release-source.json \
  --edk /path/to/llext-edk.tar.xz --toolchain /path/to/zephyr-sdk \
  --sdk /path/to/sdk-arduboy --profile default \
  --cmake /path/to/cmake --ninja /path/to/ninja --python /path/to/python3
```

`source` derives the exact target, firmware build revision, metadata version and
interface ABI from the EDK. It refuses to overwrite an existing index. Without
explicit host tools it resolves them from PATH once and records their canonical
paths. Only the selected ARM compiler subtree is hashed. This command describes
existing release inputs. `west release build` and `west release firmware` also
generate `release-source.json` beside each LLEXT EDK, before writing the success
record and checksums. Pass `--app-sdk /path/to/sdk-arduboy` to include the SDK;
otherwise that source supports native projects. Firmware signing and publication
policy remain unchanged.

Create a project and select one exact entry from that index:

```sh
meshbus app new my-game --template arduboy
meshbus app --project my-game target --source release-source.json \
  --target idea_mesh_tracker_c2/nrf54l15/cpuapp \
  --firmware <build-revision-from-index> --profile default
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

`llext.yaml` adds `project-schema: 1` and the following fields to the existing
MBA metadata. Legacy `meshbus llext` declarations remain accepted.

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
`metadata_version`, `interface_abi`, `edk`, `toolchain`, optional `sdk`, and
`tools` (`cmake`, `ninja`, `python`). Each input is `{ "path": "...", "sha256":
"..." }`. Index paths can be relative to the index directory. Duplicate release
identities are rejected. Selection matches all four identity fields exactly;
there is no nearest-version or same-board fallback.

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
machine. This initial local workflow does not claim cross-host tool portability.

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
