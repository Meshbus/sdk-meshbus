# Meshbus Development

The Meshbus repository owns the reusable Zephyr module, product application,
and host tools. Start agent sessions at this repository root. Its parent is a
local west workspace; dependency projects retain their independent Git state.

Local specifications and issues live under `.scratch/` according to
`docs/agents/issue-tracker.md`. Domain vocabulary and architectural decisions
are consumed according to `docs/agents/domain.md`. Installed engineering skills
are user-level tools; reload the session if newly installed skills are absent.

Use the tracker or ticket location already established for the task, including
when working on reusable SDK code. Missing tracker configuration does not
block work that needs no tracker write. Small, clear changes can be implemented
and validated directly; create specifications and tickets when their scope or
coordination needs warrant them.

## Workspace Discovery

For Zephyr work, start at the Meshbus Git root and discover the live workspace:

```sh
source ~/.zephyr/env/bin/activate
repo_root="$(git rev-parse --show-toplevel)"
west_root="$(west topdir)"
sdk_root="$repo_root"
west config manifest.path
west list -f '{name} {path} {revision}'
```

The current manifest is repository-root `west.yml` (`meshbus/west.yml` from
the workspace). Check actual roots rather than assuming
a checkout basename. If west discovery fails, report it and continue source-only
inspection where useful; do not guess dependency paths, run `west update`, or
repair shared environments automatically.

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

Select device targets from `apps/meshbus/boards/products.yml`, device profiles, and
`apps/meshbus/CMakeLists.txt`. [application README](apps/meshbus/README.md) documents the current
supported targets.

```sh
cd "$west_root"
board_target='<qualified-board-target>'
build_dir="$west_root/build/<task>-<target>"
west build -p always --sysbuild -b "$board_target" \
  "$repo_root/apps/meshbus" -d "$build_dir"
```

C2's default build requires an explicit Ed25519 key and never falls back to the
MCUboot repository development key. For a local engineering build, append:

```sh
-- '-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE="/absolute/path/to/development-ed25519.pem"'
```

For release builds, use `west release build --image-signing-key /absolute/path/to/key.pem`.
This passes the caller-owned PEM path to Zephyr's native signing and exports a
public PEM for verification. The release host or CI owns private-file storage,
backup and cleanup; Meshbus does not accept private PEM contents through an
environment variable. Keep the file available for builds and EDK export.
See `DISTRIBUTION.md`. Production keys are used only with reviewed code on a
trusted release host or protected CI; native builds and signing share the same
trust boundary.

When the user explicitly requests unsigned hardware validation, a separate
sysbuild with `-DSB_CONFIG_BOOT_SIGNATURE_TYPE_NONE=y` creates a matching
unauthenticated MCUboot/application pair. See the [application guide](apps/meshbus/README.md).
It uses no key and retains the partition layout; both images must be programmed.
Record this as unsigned engineering evidence, not signed-product qualification.

Replace placeholders for the current task. Shared composition changes require
all affected targets, not an arbitrary full matrix. Inspect both app and MCUboot
final configuration/DTS/layout and affected image/memory reports when relevant.
App-only builds are diagnostic and successful builds are not runtime proof.

When reconfiguring an existing sysbuild directory, retain `--sysbuild`, the
board and application arguments. A plain application CMake reconfiguration
cannot reuse the sysbuild cache. A public verification PEM cannot sign an
image; setting only the application's unsigned-image option does not override
sysbuild's signature policy. Use the authorized signing flow or report the
app-only diagnostic result with packaging still incomplete.

The default sysbuild application directory is now `<build-dir>/meshbus/`.
`mcuboot/` is unchanged. Use a fresh task build directory after relocation;
existing CMake caches contain old absolute source paths. Host packaging tools
also recognize legacy `<build-dir>/app/` outputs, but this does not make old
builds valid evidence for the current source.

## SDK Builds and Tests

Service code uses `mbs_<module>_*` APIs from `<module/module.h>` and
`CONFIG_MBS` / `CONFIG_MBS_*` configuration. Use a fresh build directory when
migrating from the former service names; update application configs, overlays,
test selections and linker wrappers together. The [SDK guide](README.md#zephyr-integration)
defines the naming boundary and retained protobuf, storage and product names.

The west module is still `meshbus`: its generated CMake discovery variable is
`ZEPHYR_MESHBUS_MODULE_DIR`. Host-tool variables such as `MESHBUS_CLI` and
`MESHBUS_PROTO_ROOT` also retain their names. For extensions, follow the
[EDK migration guidance](DISTRIBUTION.md#edk-and-extension-packages) and rebuild
MBA imports against the intended firmware's EDK.

For host CLI development, run `west meshbus <arguments>` from the west
workspace or repository. Unless `MESHBUS_CLI` explicitly selects an executable,
the adapter runs Cargo's locked release build before each invocation. Cargo
reuses fresh outputs and rebuilds changed inputs. The default target directory
is `<west-workspace>/build-meshbus-cli`; `CARGO_TARGET_DIR` overrides it, with
relative paths resolved from the workspace. This also applies to `--help` and
`--version`. Cargo output goes to stderr; a failed build prevents CLI execution.

Use the requested target when one is named. Otherwise read the nearest
`testcase.yaml` or `sample.yaml`, select its declared platform, and run the
smallest consumer of the changed behavior. Do not invent a board matrix or
default to another simulation platform. Add a product build only when product
composition matters.

Once required checks pass, broaden or repeat validation only for new changes,
failures, or unresolved concerns. Documentation-only changes normally need
reference, syntax, and consistency checks rather than firmware builds. Existing
approved test boundaries remain valid; ask about a new boundary only when it
materially changes the public contract, acceptance coverage, or side effects.

```sh
cd "$west_root"
west build -p auto -d "$west_root/build/<task>-sdk" \
  -b '<platform-from-metadata>' "$sdk_root/<sample-or-test>"
west twister -T "$sdk_root/tests/<leaf>" -p '<platform-from-YAML>' \
  -O "$west_root/twister-out/<task>" --inline-logs -v -c
```

For a runnable test app, use `west build -d <its-build-dir> -t run`. Keep each
task's outputs distinct. Use the shared Zephyr Python for Twister and report
the first relevant error; a setup failure is not automatically a firmware bug.

For Meshbus service tests, use the [test rules](tests/subsys/AGENTS.md)
for public-contract boundaries and evidence classification. Tests live directly
in `tests/subsys/<service>/`, with specialized scenarios under their owner.
Use `-T meshbus/tests/subsys -t meshbus` to select Meshbus tests without adding
the neighboring DFU and ZUI suites. Existing `subsys.meshbus.*` testcase IDs
remain stable after the directory migration. Use fresh build directories;
existing caches still point to the old test source paths.

If a parallel Twister run fails with generated configuration or setup noise,
rerun the same narrow path serially before classifying it as a product defect:

```sh
west twister -T "$sdk_root/tests/<leaf>" -p '<platform-from-YAML>' \
  -O "$west_root/twister-out/<task>-serial" --inline-logs -v -c -j 1
```

On failure, inspect the first relevant build log, runtime log, or assertion.
Patch only an in-scope cause, then rerun the same narrow command before
expanding validation. Preserve both results when retrying a flaky or
infrastructure failure.

## Serial and Remote Tools

For tool selection and evidence boundaries, see
[validation tools](docs/agents/testing.md). For a complete UART OLED screenshot,
use [Display capture](docs/display-dump.md#uart-capture-tool); it assembles
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

## Acceptance Records

Deliver the agreed behavior or artifact within scope. Run the relevant
implementation and required checks where applicable; fix in-scope failures and
rerun the affected checks. Reuse valid evidence and expand investigation only
to resolve a named acceptance gap. Work is complete when its acceptance criteria
pass. If blocked, report the exact unmet criteria and blocker without marking
them complete; keep optional follow-ups separate.

For tracked work, state acceptance criteria in the local specification or
ticket before implementation. Record actual outcomes, dates, revisions,
command or log locations, and limitations in that ticket. Raw output stays
ignored. Required missing acceptance remains open and distinct from separately
scoped platform, physical-device, upgrade, or release qualification.

Classify blocked evidence honestly. Missing fixtures, unavailable devices,
workspace discovery failures, pending authorization, and pending manual
observations are not passes. Distinguish product failures from test defects,
flaky results, infrastructure blocks, and unavailable capabilities.
