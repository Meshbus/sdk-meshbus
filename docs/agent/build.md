# Zephyr Build Guide

Read this guide only for build selection, board choice, sysbuild, module
visibility, or build failures.

## Preflight

```sh
source ~/.zephyr/env/bin/activate
sdk_root="$(git rev-parse --show-toplevel)"
west_root="$(west topdir)"
```

This repository is a Zephyr module, not a top-level application: it has module
entry points, boards, drivers, samples, and tests, but no root `prj.conf`.
Build a consuming sample, test, or external application.

## Select The Build Surface

Use this order:

1. The target explicitly requested by the user.
2. `platform_allow` or `integration_platforms` in the nearest
   `sample.yaml`/`testcase.yaml`.
3. The smallest sample or test that consumes the changed code.
4. A consuming product application only when integration composition matters.

Do not copy a board target from another repository. Product applications may
require role qualifiers, sysbuild, partitions, or signing policy absent from an
SDK sample; read that application's current documentation.

## Local Commands

Run from the west workspace and pass a discovered source path:

```sh
cd "$west_root"
west build -p auto -d "$west_root/build/sdk-<name>" \
  -b <board> "$sdk_root/samples/<path>"
west build -p auto -d "$west_root/build/sdk-<name>" \
  -b <board> "$sdk_root/tests/<path>"
```

For a single runnable test application:

```sh
build_dir="$west_root/build/sdk-<name>"
west build -p auto -d "$build_dir" -b <platform> "$sdk_root/tests/<path>"
west build -d "$build_dir" -t run
```

Use `-d <dir>` when outputs must coexist. Never edit generated build files.

## Module Visibility

Check west before passing an extra module:

```sh
west list -f '{name} {path}'
```

Only a consuming app outside the manifest graph should need:

```sh
west build -p auto -b <board> <app> -- \
  -DEXTRA_ZEPHYR_MODULES="$sdk_root"
```

## Sysbuild And Partitions

For MCUboot, sysbuild, or partition failures, inspect the consuming
application's sysbuild config, board overlays, and partition definitions first.
Do not invent or alter a flash layout unless the requested work requires it.

## Remote Builds

When remote execution is explicitly requested, use
`scripts/remote/README.md` and `west remote build --help`. Keep remote session,
fetch, and cleanup details out of ordinary local build reasoning.

## Failure Handling

Record the exact command, target, and first relevant error. Verify tooling,
source path, board metadata, and module visibility before changing code. After
a clear in-scope fix, rerun the same narrow command; do not expand into an
unrelated build matrix.
