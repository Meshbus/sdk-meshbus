# Workspace Boundary

`sdk-meshbus/` is the active Git repository. It is also the west manifest repository in
the parent Zephyr workspace:

```text
<workspace>/
  .west/
  zephyr/
  sdk-meshbus/    # current Git repo
```

Zephyr Python tooling is provided by the shared user environment at
`~/.zephyr/env`, not by a workspace-local virtualenv.

The parent workspace is required for builds, but it is not the default edit
scope. Treat `sdk-meshbus/` as writable and the rest of the workspace as read-only
unless the user explicitly requests otherwise.

## Discovery Commands

Make `west` available first when needed:

```sh
source ~/.zephyr/env/bin/activate
```

Discover the workspace and repo roots:

```sh
west topdir
git rev-parse --show-toplevel
```

Show manifest and project layout when module visibility matters:

```sh
west config manifest.path
west list -f '{name} {path}'
```

In the current layout, `west topdir` should resolve to the parent workspace and
`west list` should show the manifest project at path `sdk-meshbus`.

## Mutation Policy

Allowed by default:

- files under `sdk-meshbus/`

Read-only unless explicitly requested:

- `<workspace>/.west/`
- `<workspace>/zephyr/`
- sibling west projects under `<workspace>/modules/`, `<workspace>/bootloader/`,
  `<workspace>/tools/`, or similar west-managed paths
- manifest and workspace control files outside `sdk-meshbus/`
- SDK, toolchain, cache, and generated build output paths

If a fix appears to require editing `../zephyr` or another sibling project,
stop and explain why. Prefer board roots, DTS roots, Kconfig, overlays, module
metadata, or local compatibility code inside `sdk-meshbus/` first.

## Remote Workspace Contract

Use `west remote doctor` only when the user asks for remote execution or
provides a remote workspace target:

```sh
west remote doctor <host>:/absolute/remote/west-workspace
```

The doctor command is check-only by default. It verifies the local workspace,
the remote west workspace, `.west/config`, `manifest.path`, the manifest
repository, `.remote/`, remote `rsync`, `west`, `twister`, and the shared remote
Zephyr environment at `~/.zephyr/env/bin/activate`.

Use `--fix` only when the user wants the remote workspace repaired. It may copy
the local `.west/config`, seed the manifest repository, create `.remote/`, and
reset/pull the remote manifest repository when it had to seed it. Use
`--fix --update` only when the user wants the remote checkout fully refreshed;
it may run `git reset --hard`, `git pull`, `west update`,
`west packages pip --install`, `west sdk install`, and `west blobs fetch` on the
remote machine.

Remote build and Twister runs use session directories under `.remote/`:

```sh
west remote session <host>:/absolute/remote/west-workspace <session-id>
west remote session <host>:/absolute/remote/west-workspace <session-id> \
  --source /absolute/path/to/worktree/sdk-meshbus
west remote session <host>:/absolute/remote/west-workspace <session-id> \
  --sync zephyr --sync modules/lib/<module>
west remote session list <host>:/absolute/remote/west-workspace
west remote session delete <host>:/absolute/remote/west-workspace <session-id>
```

The session path is `<remote-workspace>/.remote/<session-id>`. Session syncs
copy the local manifest repository into
`<remote-workspace>/.remote/<session-id>/<manifest.path>`, excluding `.git` and
honoring `.gitignore`. By default the sync source is
`<local-west-topdir>/<manifest.path>`. Use `--source` to sync a specific local
manifest repository, such as a Git worktree checkout of `sdk-meshbus`; the remote
destination still uses `<manifest.path>`.

Use `--sync <path>` to copy additional local west-workspace-relative
directories into the same relative path under the session. This is intended for
temporary parent-workspace patches such as `--sync zephyr` or a patched west
module. Extra sync roots exclude `.git` but do not honor `.gitignore`, because
Zephyr source directories such as `target/` can be valid source. When `--sync
zephyr` is used by `west remote build` or `west remote twister`, the remote
command uses `ZEPHYR_BASE=<remote-session>/zephyr`. For follow-up build or
Twister runs against an already-synced session, combine `--no-sync --sync
zephyr` so the command reuses the session path without running rsync again.

`delete` removes the whole named session directory; do not use it when the user
still needs remote build or Twister output for diagnosis.

For command-specific behavior, use `build.md` for `west remote build` and
`testing.md` for `west remote twister`.

## Command Location

Run `west build` and `west twister` from the workspace root returned by
`west topdir`, using `sdk-meshbus/<path>` for paths inside this repository:

```sh
cd "$(west topdir)"
west build -p auto -b <board> sdk-meshbus/<sample-or-test>
```

Do not hardcode `../zephyr` or an absolute workspace path in durable docs or
scripts. Short interactive commands may use the current known layout only after
`west topdir` has confirmed it.

## Sandbox Notes

Normal Codex entrypoint:

```sh
cd <workspace>/sdk-meshbus
codex
```

If a future sandboxed run cannot read the parent workspace during builds, start
Codex with parent access:

```sh
cd <workspace>/sdk-meshbus
codex --add-dir ..
```

This expands technical access only. The mutation policy above still applies.
Do not use unrestricted sandbox bypass modes for normal SDK work.

## Common Failure Modes

### `west` not found

Check the shared Zephyr environment:

```sh
source ~/.zephyr/env/bin/activate
west topdir
```

If the environment is missing or broken, report that tooling setup is unavailable
instead of installing global packages without approval.

### `west topdir` fails

Report that the checkout is not inside a valid west workspace. Do not guess the
Zephyr path.

### Build cannot find this module

Check:

```sh
west topdir
west config manifest.path
west list -f '{name} {path}'
```

This repository has `zephyr/module.yml`; if a consuming app is outside the west
manifest graph, it may need `-DEXTRA_ZEPHYR_MODULES=<path-to-sdk-meshbus>`. Use that
only after confirming the repo is not already visible through west.
