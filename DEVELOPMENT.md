# Meshbus Development

The Meshbus repository owns the reusable Zephyr module, product application,
and host tools. Start agent sessions at this repository root. Its parent is a
local west workspace; dependency projects retain their independent Git state.

See the [OpenSpec workflow](openspec/README.md) for tooling setup, shared
specifications and change plans.

Use the task-specific links in [AGENTS.md](AGENTS.md) to locate current contracts.

## Engineering contracts

Implement the current requirement with existing facilities. New abstractions,
configuration switches, recovery paths or compatibility layers need a concrete
consumer or reachable failure to justify them. Similar-looking code across
independent owners does not automatically need a shared framework.

Validate untrusted inputs at public, protocol, persistence and device boundaries.
Within a private call chain, rely on established preconditions; avoid repeating
checks for impossible arguments or silently recovering from programming errors.
Keep overflow, resource, authorization and asynchronous-lifetime protections.

Public APIs live in `include/<module>/<module>.h`, with narrow capability
headers beside them. Services use `mbs_` / `MBS_` and `CONFIG_MBS_*`; protobuf
names, persisted keys and protocol values retain their owning contracts.
Repository-owned header guards use `MESHBUS_INCLUDE_<PATH>_H_`. Keep private
state and transport helpers out of public headers. Document ownership, units,
buffer sizes, persistence, errors and ZBus payload direction/lifetime. Update
affected consumers together when public layouts or identifiers change.
For exported extension symbols, follow [MBA metadata](subsys/llext/METADATA.md).

Services own state, synchronization, persistence and device policy. Keep state
locks out of driver calls, I/O, sleeps, callbacks and work cancellation; define
lock ordering and expose copied or immutable snapshots. Keep listeners bounded
and hand blocking work to the owner. Stage settings loads, apply at commit,
coalesce writes and remove owned keys on reset. Treat missing optional hardware
as an unavailable capability and preserve configuration when hardware apply fails.
Shell and MCUmgr adapters reuse public service behavior.

Desktop uses public ZUI APIs. Apps and widgets own their behavior; reusable
components belong in ZUI. Draw callbacks only render prepared state, while
service queries and updates happen outside rendering. Stop observations and
callbacks, wait for in-flight access and cancel work before freeing state.
Keep UI text in the central English text contract with matching format arguments.

## Workspace Discovery

For Zephyr work, start at the Meshbus Git root and discover the live workspace:

```sh
source ~/.zephyr/env/bin/activate
repo_root="$(git rev-parse --show-toplevel)"
west_root="$(west topdir)"
sdk_root="$repo_root"
west config manifest.path
west manifest --path
west list -f '{name} {path} {revision}'
```

This repository provides `west.yml`; the active workspace may select a
different manifest. Use the discovered manifest and project paths for the
current dependency graph. If west discovery fails, report it and continue
source-only inspection where useful; do not guess dependency paths, run
`west update`, or repair shared environments automatically.

Activate the environment in the same shell that starts the build or tool.
Invoking `west` by absolute path alone does not put its Python environment on
`PATH` for child processes such as nanopb generation. The CLI build also needs
workspace/schema discovery; use the activated workspace or the documented
`MESHBUS_PROTO_ROOT` override in [CLI development](scripts/meshbus/README.md).

The SDK is a Zephyr module, not a root application. Select a consuming sample,
test, or product. Before adding an extra module path, check whether the active
manifest already exposes it. Use the consumer's current module-loading option
only when it is outside that graph.

## Product Builds

### Local product builds

Select device targets with `west release matrix`. APP `.conf` files under
`apps/meshbus/boards/<vendor>/<board>/` define the product inventory; their
full target names are validated against Zephyr board metadata. The application
loads these profiles for ordinary `west build` as well as release builds.
[application README](apps/meshbus/README.md) documents the layout.

`west release build --development` applies `apps/meshbus/prj.dev.conf` after
the board profile; omitting the flag applies `prj.prod.conf` with LTO and local
ISR tables. Dev and prod have separate build directories. Development preserves
board-required optimizations, including LTO where configured. Ordinary `west build` keeps
the board defaults; to select a fragment explicitly with sysbuild, append
`-Dmeshbus_EXTRA_CONF_FILE=prj.dev.conf` or
`-Dmeshbus_EXTRA_CONF_FILE=prj.prod.conf` after `--`.

```sh
cd "$west_root"
board_target='<qualified-board-target>'
build_dir="$west_root/build/<task>-<target>"
west build -p auto --sysbuild -b "$board_target" \
  "$repo_root/apps/meshbus" -d "$build_dir"
```

Reuse the task's valid build directory for ordinary source edits.

Public SDK MCUboot builds default to hash-only validation and need no key.
The generated bootloader and APP retain their configured layout and recovery
transports. To opt into Ed25519 authentication, append both settings:

```sh
-- -DSB_CONFIG_BOOT_SIGNATURE_TYPE_ED25519=y \
  '-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE="/absolute/path/to/development-ed25519.pem"'
```

For the same authenticated mode through the release tool, use
`west release build --image-signing-key /absolute/path/to/key.pem`. Omitting the
key uses the default unsigned mode. Neither mode silently falls back from a
requested authentication policy; MCUboot example keys are rejected.

The release host owns private-file storage, backup and cleanup. Meshbus does
not accept private PEM contents through an environment variable. Keep an opted-in
key available for builds and EDK export. Production keys belong only on a trusted
release host running reviewed code. See [distribution](DISTRIBUTION.md).

Use a fresh build directory when changing authentication modes. An installed
signed bootloader rejects an unsigned APP: a mode transition needs a matching
MCUboot/application pair and separately authorized physical programming. Build
success proves neither physical recovery nor signed-product qualification.

Replace placeholders for the current task. Shared composition changes require
all affected targets, not an arbitrary full matrix. Inspect both app and MCUboot
final configuration/DTS/layout and affected image/memory reports when relevant.
App-only builds are diagnostic and successful builds are not runtime proof.

When reconfiguring an existing sysbuild directory, retain `--sysbuild`, the
board and application arguments. A plain application CMake reconfiguration
cannot reuse the sysbuild cache. A public verification PEM cannot sign an
image; setting only the application's unsigned-image option does not override
sysbuild's authentication policy. Select the mode at sysbuild level and package
the complete matching output.

The default sysbuild image directories are `<build-dir>/meshbus/` and
`<build-dir>/mcuboot/`. Use a fresh task build directory when source locations,
target, or configuration changes would invalidate a CMake cache.

## SDK Builds and Tests

Service code uses `mbs_<module>_*` APIs from `<module/module.h>` and
`CONFIG_MBS` / `CONFIG_MBS_*` configuration. The
[SDK guide](README.md#zephyr-integration) defines public headers, service
namespaces, and the protobuf, storage, and product naming boundaries.

The west module is `meshbus`: its generated CMake discovery variable is
`ZEPHYR_MESHBUS_MODULE_DIR`. Host-tool variables such as `MESHBUS_CLI` and
`MESHBUS_PROTO_ROOT` configure CLI and schema discovery. For extensions, follow the
[EDK package requirements](DISTRIBUTION.md#edk-and-extension-packages) and build
MBA imports against the intended firmware's EDK.

For host CLI development, run `west meshbus <arguments>` from the west
workspace or repository. Unless `MESHBUS_CLI` explicitly selects an executable,
the adapter runs Cargo's locked release build before each invocation. Cargo
reuses fresh outputs and rebuilds changed inputs. The default target directory
is `<west-workspace>/build-meshbus-cli`; `CARGO_TARGET_DIR` overrides it, with
relative paths resolved from the workspace. This also applies to `--help` and
`--version`. Cargo output goes to stderr; a failed build prevents CLI execution.

Select the scenario, platform and check scope using
[coverage guidance](docs/testing.md#choosing-coverage) and the nearest test/sample
metadata. Then run the relevant command from the discovered workspace:

```sh
cd "$west_root"
west build -p auto -d "$west_root/build/<task>-sdk" \
  -b '<platform-from-metadata>' "$sdk_root/<sample-or-test>"
west twister -T "$sdk_root/tests/<leaf>" -p '<platform-from-YAML>' \
  -s '<scenario-from-YAML>' -O "$west_root/twister-out/<task>" --inline-logs
```

For repeated source edits, rebuild the selected test with
`west build -d <its-build-dir>` and execute it with
`west build -d <its-build-dir> -t run` when its runner supports that target.
Return to Twister when scenario metadata/configuration changes or for the final
recorded regression. Keep each task's outputs distinct, but reuse its own valid
build instead of creating a clean matrix for every Red/Green cycle.
Use the shared Zephyr Python for Twister and report
the first relevant error; a setup failure is not automatically a firmware bug.

For Meshbus service tests, use the [testing guide](docs/testing.md)
for public-contract boundaries and evidence classification. Tests live directly
in `tests/subsys/<service>/`, with specialized scenarios under their owner.
Use `-T meshbus/tests/subsys -t meshbus` to select Meshbus tests without adding
the neighboring DFU suites. ZUI's own tests live in its independent module.

If logs implicate parallel setup or generated-file races, a serial retry can
distinguish infrastructure from a product defect. Do not retry deterministic
compiler errors or assertions without addressing their cause:

```sh
west twister -T "$sdk_root/tests/<leaf>" -p '<platform-from-YAML>' \
  -s '<scenario-from-YAML>' \
  -O "$west_root/twister-out/<task>-serial" --inline-logs -j 1
```

On failure, inspect the first relevant build log, runtime log, or assertion.
Patch only an in-scope cause, then rerun the same narrow command before
expanding validation. Preserve both results when retrying a flaky or
infrastructure failure.

For standalone MBA build/install/run/watch workflows, see
[app development](scripts/meshbus/APP_DEVELOPMENT.md).

## Serial and Remote Tools

For tool selection and evidence boundaries, see
[validation tools](docs/testing.md). For a complete UART OLED screenshot,
use [Display capture](scripts/meshbus/README.md#display-capture); it assembles
chunks and exports PNGs without extra Python packages.

For authorized live serial work, use the shared helper at
`$sdk_root/scripts/serial_use.py` and the existing Zephyr Python with pyserial:

```sh
serial_python="$HOME/.zephyr/env/bin/python"
"$serial_python" "$sdk_root/scripts/serial_use.py" --help
"$serial_python" "$sdk_root/scripts/serial_use.py" list
"$serial_python" "$sdk_root/scripts/serial_use.py" monitor '<port>' \
  --baudrate 115200 --wait '<business-success>' \
  --fail-pattern '<terminal-business-failure>' --post-wait-seconds 2 \
  --timeout 30 --transcript "$west_root/build/<task>/serial.log"
"$serial_python" "$sdk_root/scripts/serial_use.py" check-log \
  --file "$west_root/build/<task>/serial.log" \
  --fail-pattern '<terminal-business-failure>'
```

Create the task output directory first. List ports and start passively instead
of assuming a device path. `send` is only for a known, authorized command and
must wait for its business result. Use `--wait-all` for multiple mandatory
success checkpoints. Serial access does not authorize reset, flash, or killing
another process. Read current `--help` for advanced options.

Each `--wait` is a success condition; multiple waits are ORed unless
`--wait-all` is used. Put terminal business failures in `--fail-pattern`, never
in a success wait expression. Wait for the business endpoint rather than the
first boot banner or shell prompt, and use `--post-wait-seconds` when trailing
asynchronous output is part of the evidence. Transient retry logs are not
terminal failures unless the task's contract says so.

For an asynchronous command, use `send` followed by `monitor`, or use `session`
to keep one connection and transcript. Run `check-log` before claiming a clean
result. Record the port, baudrate, reset method, waits, failure patterns,
timeout, commands sent, and transcript path. Treat the serial endpoint and SWD
probe as separate identities. Use DTR/RTS or an external reset command only for
a known board connection and an explicitly authorized reset.

The helper's device-free regression suite is:

```sh
"$serial_python" -m unittest discover -s "$sdk_root/scripts/tests" \
  -p test_serial_use.py
```

For requested remote work, read the SDK's `scripts/remote/README.md` and selected
command help. `west remote` forwards GDB/serial; build and Twister run normally
on the development host. Missing tools are reported, not installed globally.

## History retrieval

Establish current behavior from the relevant contract, source and tests. Retrieve
history when tracing a decision, regression or specific unfinished work:

1. Locate Git or OpenSpec records by path, symbol, requirement or change name.
   Use `git log --oneline -- <path>` or `git log -S '<symbol>' -- <path>` for
   code history, and `rg -l '<topic>' openspec/changes/archive` for archived work.
2. Read matching summaries before opening a specific design, task record or
   `git show <commit> -- <path>`. Do not load the entire archive.
3. Cite the commit or change identity and recheck its conclusions against the
   current version. Say when evidence is missing; an old plan is not implementation.

Start ordinary searches in relevant source and current guides, excluding
archives, build outputs and temporary records. Expand deliberately for missing
evidence, historical investigations or explicit complete-file audits. Preserve
completed OpenSpec records and their original commands and paths; they describe
past work, not current operating instructions. Fix a broken historical locator
only when necessary, without rewriting the recorded result.

## Outputs and records

Report routine investigations and verification in conversation. For a formal
change, use its existing task record as described in
[OpenSpec acceptance](openspec/README.md#implementation-and-review); do not add
another report, checklist, daily memory or progress document. Durable findings
update the owning current guide. Repeated workflows may justify a skill later.

Builds and Twister keep their native output directories, isolated by task and
configuration, and reuse valid caches. For raw logs, one-off checks and generated
intermediates, explicitly create an external task directory, for example:

```sh
task_tmp="$(mktemp -d "${TMPDIR:-/tmp}/meshbus-task.XXXXXX")"
```

Use a named output destination for requested screenshots, prototypes and other
deliverables; do not automatically delete them or existing local experiments.
Keep private paths and raw logs out of tracked documents. A private log path
alone is insufficient evidence for shared review.

## Acceptance Records

Use the [OpenSpec review and completion rules](openspec/README.md#implementation-and-review)
for planned work, and the [testing guide](docs/testing.md) for check selection
and evidence boundaries. Report product failures, test defects, flaky results,
infrastructure blocks and unavailable capabilities distinctly.

## Automated validation

[GitHub Actions](.github/CI.md) describes PR/main/weekly layers, the common
Docker tool image, native CLI exceptions, exact manifest snapshots and report
retention. Use a fresh isolated west workspace to reproduce CI; each dependency setup
runs `west update` from the SHA-pinned source manifest and must not be used on
the shared developer workspace.
License metadata blockers in the selected scope remain strict failures,
independent of build and runtime results. Use the default incremental
[license check](.github/CI.md#license-check-scope) during development; complete
audits are explicit or selected automatically for shared licensing changes.
