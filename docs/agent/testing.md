# Zephyr Testing Guide For Codex

Use this file for Twister, ztest, test creation, sample verification, test
failures, or hardware-test policy.

## Selection Rule

Choose the smallest verification that proves the changed behavior. In this repo,
do not default all tests to `native_sim`; read the nearest `testcase.yaml` or
`sample.yaml` and use its `platform_allow` / `integration_platforms`.

| Change type | First verification | Broader verification |
| --- | --- | --- |
| Meshbus service public API or ZBus behavior | `tests/subsys/meshbus/services/<service>` on `qemu_x86` | matching service sample build |
| Meshbus service sample | build or Twister that sample path | Desktop-capable application build when integration is affected |
| Meshbus Desktop/ZUI integration | separate firmware repository `app/` build | focused desktop/app/widget checks when present |
| MeshCore library behavior | `tests/lib/meshcore/protocol/<suite>` on `qemu_x86` | runtime oracle if upstream/reference behavior matters |
| Meshbus MeshCore service behavior | `tests/subsys/meshbus/services/meshcore` on `qemu_cortex_m3` | Bluetooth NUS test when relevant |
| Driver behavior | matching `tests/drivers/<driver>` if present | nearest sample or board-facing build |
| DTS, Kconfig, CMake, board files | build the closest consuming test or sample | relevant Twister path |
| Pure docs | `git diff --check` | no build unless behavior changed |

## Twister Patterns

From `sdk-meshbus/`, activate tooling and move to the west workspace root:

```sh
source ~/.zephyr/env/bin/activate
cd "$(west topdir)"
```

Narrow test path:

```sh
west twister -T sdk-meshbus/tests/<path> -p <platform> --inline-logs -v
```

Sample path:

```sh
west twister -T sdk-meshbus/samples/<path> -p <platform> --inline-logs -v
```

If a parallel Twister run fails with generated `.config` or setup noise, rerun
the same narrow path serially before treating it as a code regression:

```sh
west twister -T sdk-meshbus/tests/<path> -p <platform> --inline-logs -v -j 1
```

## Remote Twister

Use `west remote twister` only when the user asks for remote Twister or
provides a remote workspace target. Remote workspace preflight and session
management are documented in `workspace.md`; remote build offload is documented
in `build.md`.

General pattern:

```sh
west remote twister <host>:/absolute/remote/west-workspace <session-id> -- \
  -T sdk-meshbus/<tests-or-samples-path> -p <platform> --inline-logs -v -c
```

The command maps `sdk-meshbus/...` paths to
`<remote-workspace>/.remote/<session-id>/<manifest.path>`, sets the Twister
outdir to `<remote-workspace>/.remote/<session-id>/twister-out` by default, and
passes `-x EXTRA_ZEPHYR_MODULES=<remote-session>/<manifest.path>` so every test
build uses the session copy of this module. If `-O` or `--outdir` is provided,
it must be a relative path and is mapped under the session directory.

The remote Twister process also receives
`EXTRA_ZEPHYR_MODULES=<remote-session>/<manifest.path>` in its environment so
platform discovery can see boards from the synced session copy, not only the
remote workspace's baseline manifest checkout.

Use `--sync <west-workspace-relative-dir>` when Twister must include local
patches outside the manifest repository. For local Zephyr-tree patches, sync
`zephyr`:

```sh
west remote twister <host>:/absolute/remote/west-workspace <session-id> \
  --sync zephyr -- -T sdk-meshbus/<tests-or-samples-path> -p <platform> \
  --inline-logs -v -c
```

With `--sync zephyr`, the remote Twister command runs with
`ZEPHYR_BASE=<remote-session>/zephyr`. The synced Zephyr tree excludes `.git`,
so version-detection warnings are expected and do not invalidate build-only
Twister results.

After a session has already synced an extra root, use `--no-sync --sync zephyr`
to skip rsync while still mapping `zephyr/...` paths and setting
`ZEPHYR_BASE=<remote-session>/zephyr`.

Use `--source /absolute/path/to/worktree/sdk-meshbus` when the local code source is a
specific Git worktree. The remote destination still uses
`<remote-workspace>/.remote/<session-id>/<manifest.path>`.

Use `--no-delete` when preserving remote-only files inside the session matters.
After a successful sync, `--no-sync` may be used for follow-up runs against the
same session.

Use `--clean` to delete only the remote Twister outdir after a successful run.
It preserves the session manifest repository. Failed Twister output is kept for
logs and reports.

## Meshbus Contract Tests

For `tests/subsys/meshbus/services/*`, the binding rule is
`tests/subsys/meshbus/AGENTS.md`:

- test public APIs declared in `include/zephyr/meshbus/<service>.h`
- test public ZBus channels declared in the same header
- contract applications do not directly test private static functions,
  private structs, or private headers
- a separately named `integration` application may use a justified private
  seam, but it does not satisfy the public-contract gate
- use `qemu_x86` unless a specific test directory says otherwise

Shared Meshbus test sources live under `tests/subsys/meshbus/common/*`. That
directory is not a runnable application root; a declared QEMU application
must compile the helper source explicitly.

Typical command:

```sh
west twister -T sdk-meshbus/tests/subsys/meshbus/services/<service> -p qemu_x86 --inline-logs -v
```

Board-facing Meshbus samples are a separate validation surface:

```sh
west build -p auto -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
  sdk-meshbus/samples/subsys/meshbus/services/<service>
```

Do not claim hardware validation from a qemu contract test.

## MeshCore Tests

For module-level MeshCore work:

```sh
west twister -T sdk-meshbus/tests/lib/meshcore/protocol/<suite> -p qemu_x86 --inline-logs -v
```

Use oracle parity when the observable protocol behavior should match the
reference implementation:

```sh
west twister -T sdk-meshbus/tests/lib/meshcore/runtime/oracle -p qemu_x86 --inline-logs -v
```

For Zephyr-facing Meshbus MeshCore service integration:

```sh
west twister -T sdk-meshbus/tests/subsys/meshbus/services/meshcore -p qemu_cortex_m3 --inline-logs -v
```

## Adding Tests

- Keep tests close to the affected subsystem.
- Add or update `testcase.yaml`.
- Keep QEMU contract and hardware service-DUT coverage as separate Twister
  scenarios. Use separate applications only when lifecycle, artifact,
  partition, or role constraints require it.
- Prefer public APIs and observable channels over implementation hooks.
- Use fakes, overlays, linker wraps, or non-persistent settings only where the
  existing test style supports them.

## Hardware Policy

Do not run these without explicit user authorization:

```sh
west flash
west debug
west twister --device-testing ...
```

Hardware automation must be explicit: a separate Twister scenario with a real
board platform and fixture, and no fake/stub path for the behavior being
claimed.

If hardware validation is needed but not authorized or unavailable, report the
exact pending command and mark it as not verified.

## Meshbus Validation Records

Meshbus does not maintain a separate selector, evidence-schema, lease, or
release-report framework. Use the nearest `testcase.yaml` as the runnable
scenario source of truth and select the smallest affected Twister paths from
the changed public API, service, dependency, role, or system surface.

Twister output is the authoritative automated result. For authorized hardware
runs, retain the build command, testcase ID, board and configuration, probe and
serial mapping, expected waits, transcript or measurement output, cleanup, and
final device state. Keep these artifacts outside the source tree or under an
ignored build/output directory. Missing fixtures, authorization, manual
observations, cleanup, or higher-layer evidence remain explicitly unverified.

## Remote DAP Flashing

Prefer `west remote build --fetch` for images built on a remote workspace but
flashed, tested, or inspected locally. The fetched build directory is rewritten
for the local workspace and can be used by normal runner commands after explicit
user authorization:

```sh
west flash -d build.idea_mesh_tracker_c2 -r pyocd
```

Use the repo-local `west remote flash` extension only when the user provides an
existing remote build directory and no local fetched bundle is available. It
syncs a minimal runner bundle from the remote build directory and then runs
local `west flash --no-rebuild` with any remaining runner arguments passed
through.

General pattern:

```sh
west remote flash -s <host> -d /absolute/remote/build-dir \
  -c <local-runner-build-dir> -r <runner> -- <runner-args>
```

For a remote GDB/DAP endpoint that the user has already started or provided,
the endpoint is a flashing transport. After explicit user authorization, run
the provided `west flash -r gdb` command from `west topdir`; do not manually
attach GDB, halt, jump to reset vectors, issue `monitor reset`, or otherwise
control target execution through raw GDB commands.

Current tracker remote flash command:

```sh
west flash -d build.idea_mesh_tracker_c2 -r gdb -- --gdb-port 62001
```

If the flash command exits successfully, report the flash result. If post-flash
evidence is needed, use `serial-use` for passive log capture and classification;
do not add raw GDB recovery steps unless the user explicitly asks for
interactive debug control.

## Serial Evidence

For live device serial evidence, use the repo-local `serial-use` skill when the
user asks the agent to monitor or operate a connected Zephyr device console.
Default to baudrate `115200`; first list likely ports, then start with passive
listening before sending shell commands or trying serial-line resets.

Prefer the bundled script for repeatable waits, transcripts, and log
classification:

```sh
python3 .agents/skills/serial-use/scripts/serial_use.py list
python3 .agents/skills/serial-use/scripts/serial_use.py monitor <port> --timeout 20 --transcript /tmp/zephyr-serial.log
python3 .agents/skills/serial-use/scripts/serial_use.py check-log --file /tmp/zephyr-serial.log
```

Serial monitoring may support validation claims, but it does not authorize
`west flash`, `west debug`, hardware tests, destructive device commands, or
unknown reset commands. Report the port, baudrate, waits, commands sent, and
transcript path when serial evidence is used.

## Failure Handling

When tests fail:

1. Find the first relevant failing build log, runtime log, or assertion.
2. Avoid chasing unrelated failures outside the modified area.
3. Patch once if the cause is clear and in scope.
4. Rerun the same narrow command.
5. If it still fails, report the exact failure instead of expanding scope
   indefinitely.
