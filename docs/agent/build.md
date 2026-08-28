# Zephyr Build Guide For Codex

Use this file only for build, board, sysbuild, module-visibility, or build
failure tasks. Keep build reasoning tied to files in this repository.

## First Checks

From `sdk-meshbus/`:

```sh
source ~/.zephyr/env/bin/activate
west topdir
git rev-parse --show-toplevel
find . -maxdepth 3 \( -name CMakeLists.txt -o -name prj.conf -o -name module.yml -o -name west.yml \) -print
```

Then run builds from the workspace root:

```sh
cd "$(west topdir)"
```

Do not assume a top-level application. This repo is currently a west manifest
repository and Zephyr module with `zephyr/module.yml`, top-level `CMakeLists.txt`
and `Kconfig`, board roots, DTS roots, samples, tests, drivers, libraries, and
subsystems.

## Repository Classification

### Current repo shape

Current signs:

- `west.yml` at repo root: this checkout is the manifest repository.
- `zephyr/module.yml`: exports Kconfig, CMake, board roots, DTS roots, and
  module extension roots.
- `CMakeLists.txt` and `Kconfig`: module entry points.
- `samples/` and `tests/`: normal build and verification entry points.
- No top-level `prj.conf`: do not build `meshbus/` itself as an app.

Preferred pattern:

```sh
west build -p auto -b <board> sdk-meshbus/<sample-or-test-path>
```

Use Twister for test suites when practical:

```sh
west twister -T sdk-meshbus/<test-or-sample-path> -p <platform> --inline-logs -v
```

### Consuming from outside west

If a separate app outside this west workspace must consume `sdk-meshbus`, verify
visibility first:

```sh
west list -f '{name} {path}'
```

Only if the `sdk-meshbus` path is not visible through west, pass it explicitly:

```sh
west build -p auto -b <board> <app> -- \
  -DEXTRA_ZEPHYR_MODULES=<path-to-sdk-meshbus>
```

Do not use `EXTRA_ZEPHYR_MODULES` for ordinary builds inside this workspace.

## Board Selection

Prefer this order:

1. Board explicitly requested by the user.
2. Board or platform named in the relevant `testcase.yaml` or `sample.yaml`.
3. Board documented in the closest nested `AGENTS.md`.
4. A repo-used integration board only when the changed area has no narrower
   target.

Do not invent a board matrix. Check the local YAML first.

Known current anchors:

- Meshbus service contract tests: `qemu_x86`.
- Meshbus service samples: `idea_mesh_tracker_c2/nrf54l15/cpuapp`.
- Meshbus Desktop/LLEXT app host smoke: the separate firmware repository's
  role-qualified `app/` target.
- MeshCore module tests: `qemu_x86`.
- MeshCore subsystem test: `qemu_cortex_m3`.
- Display samples: read their YAML; some use `frdm_k64f` with shield args or
  `reel_board`.

## Common Build Commands

Meshbus service sample:

```sh
west build -p auto -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
  sdk-meshbus/samples/subsys/meshbus/services/<service>
```

Meshbus Desktop/LLEXT app host smoke:

```sh
west build -p auto -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
  app
```

Driver-facing build:

```sh
west build -p auto -b <board> sdk-meshbus/tests/drivers/<driver>
```

If no focused driver test exists, compile the nearest sample or board-facing
integration target that consumes the driver.

Single test project build:

```sh
west build -p auto -b qemu_x86 \
  sdk-meshbus/tests/subsys/meshbus/services/<service>
west build -t run
```

## Remote Build Offload

Use `west remote build` only when the user asks for remote build execution or
provides a remote workspace target. Preflight and session management are
documented in `workspace.md`.

General pattern:

```sh
west remote build <host>:/absolute/remote/west-workspace <session-id> -- \
  -p auto -b <board> sdk-meshbus/<sample-or-test-path>
```

The command creates or reuses
`<remote-workspace>/.remote/<session-id>`, syncs the local manifest repository
into `<remote-workspace>/.remote/<session-id>/<manifest.path>` unless
`--no-sync` is used, maps `sdk-meshbus/...` arguments to that session checkout, and
passes `-DEXTRA_ZEPHYR_MODULES=<remote-session>/<manifest.path>` to the remote
build. The remote shared Zephyr environment must already exist at
`~/.zephyr/env/bin/activate`.

Use `--sync <west-workspace-relative-dir>` when the remote build must include
local patches outside the manifest repository. The path is copied under the
same relative path in the remote session, can be repeated, and must stay inside
the local west workspace. For local Zephyr-tree patches, sync `zephyr`:

```sh
west remote build <host>:/absolute/remote/west-workspace <session-id> \
  --sync zephyr -- -p auto -b <board> sdk-meshbus/<sample-or-test-path>
```

With `--sync zephyr`, the remote command runs with
`ZEPHYR_BASE=<remote-session>/zephyr`. The synced Zephyr tree excludes `.git`,
so builds may warn that Zephyr is not a Git repository; that warning does not
invalidate compile results.

After a session has already synced an extra root, use `--no-sync --sync zephyr`
to skip rsync while still mapping `zephyr/...` paths and setting
`ZEPHYR_BASE=<remote-session>/zephyr`.

Use `--source /absolute/path/to/worktree/sdk-meshbus` when the code to sync is a
specific local Git worktree rather than `<local-west-topdir>/<manifest.path>`:

```sh
west remote build <host>:/absolute/remote/west-workspace <session-id> \
  --source /absolute/path/to/worktree/sdk-meshbus -- \
  -p auto -b <board> sdk-meshbus/<sample-or-test-path>
```

`--source` changes only the local sync source and local path rewrites. The
remote destination remains
`<remote-workspace>/.remote/<session-id>/<manifest.path>`, and `--fetch`
without `--local-build-dir` still writes artifacts under `<local-west-topdir>`.

If `-d` or `--build-dir` is provided, it must be relative. The remote command
maps it under the session directory. For example, `-d build.tracker` becomes
`<remote-workspace>/.remote/<session-id>/build.tracker`. Absolute paths and
parent-relative paths are rejected.

Use `--fetch` when the remote build artifacts are needed locally for flashing,
runner-based testing, debugging, dump analysis, or ELF/map inspection:

```sh
west remote build <host>:/absolute/remote/west-workspace <session-id> \
  --fetch -- -d build.idea_mesh_tracker_c2 -p auto \
  -b idea_mesh_tracker_c2/nrf54l15/cpuapp app
```

The default local bundle path is `<local-west-topdir>/<remote-build-subdir>`.
For example, `-d build.idea_mesh_tracker_c2` fetches to
`<local-west-topdir>/build.idea_mesh_tracker_c2`, so local runner commands can
use:

```sh
west flash -d build.idea_mesh_tracker_c2 -r pyocd
```

Do not run `west flash` without explicit user authorization. Use
`west flash --context` when only runner metadata inspection is needed.

The fetched bundle includes runner metadata, `.config`, generated devicetree
and autoconf metadata, `zephyr.elf`, `zephyr.map`, common image files, and
artifacts referenced by `zephyr/runners.yaml`. `runners.yaml` and
`CMakeCache.txt` are rewritten from remote workspace/session/toolchain paths to
local paths. `build.ninja` is replaced with a no-op file so runner commands do
not try to rebuild the remote artifacts locally.

Use `--clean` to delete only the remote build output directory after a
successful build and any requested fetch. It preserves the session manifest
repository. Failed remote build output is kept for diagnosis.

## Build Directories

Prefer `-p auto` for Codex-driven builds. Use default build directories for a
single narrow command, or specify `-d build/<area>/<board>` when multiple build
outputs must coexist.

Never edit generated files under `build/`.

## Sysbuild, Bootloader, And Partitions

If failures mention sysbuild, MCUboot, partition manager, flash partitions, or
multi-image configuration:

1. Inspect existing `sysbuild.conf`, `pm_static.yml`, board overlays, sample
   config, and board files.
2. Do not create a new partition layout unless the user asked for it.
3. Prefer the smallest local `sdk-meshbus/` change that restores the documented build.

## Failure Handling

On build failure:

1. Capture the exact command, board, path, and first relevant error.
2. Check whether `west` came from `~/.zephyr/env`.
3. Verify module visibility before changing source.
4. Patch only when the failure is related to the requested task.
5. Rerun the same narrow command once after a clear fix.

Report parent-workspace access requirements and any unverified hardware effects.
