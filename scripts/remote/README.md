# West Remote Helpers

The `west remote` extension supports remote build/Twister compute and selected
local-hardware transports. This is an operational reference; load it only when
a task explicitly requires remote execution.

## Safety

Remote checks are non-mutating by default, but these actions require explicit
user authorization:

- `doctor --fix` or `doctor --fix --update`
- session deletion or cleanup of remote outputs
- flash, GDB, reset, serial forwarding, or physical-device operations
- syncing parent-workspace or sibling-project changes

Never place credentials, private keys, or live host/device maps in committed
commands or documentation.

## Preflight

```sh
source ~/.zephyr/env/bin/activate
sdk_root="$(git rev-parse --show-toplevel)"
west topdir
west config manifest.path
west remote --help
```

The SDK may be the active manifest repository or an imported project. Remote
build/session/Twister commands default to the active `manifest.path`. When
working from a consuming workspace whose manifest repository is not this SDK,
pass the SDK checkout explicitly:

```sh
--source "$sdk_root"
```

With `--source`, pass SDK source paths relative to that checkout, for example
`tests/subsys/...` rather than a hardcoded west project basename. The remote
session still uses the active workspace's `<manifest.path>` as its synchronized
module destination.

## Check The Remote Workspace

```sh
west remote doctor <host>:/absolute/remote/west-workspace
```

Use `--fix` only when repair is requested. Use `--fix --update` only when the
user accepts remote reset/pull, west update, package/toolchain setup, and blob
fetch effects.

## Build

```sh
west remote build <host>:/absolute/remote/west-workspace <session> \
  --source "$sdk_root" -- \
  -p auto -b qemu_x86 tests/subsys/meshbus/services/clock
```

Useful options:

- `--no-sync`: reuse an existing session
- `--sync <workspace-relative-path>`: include an explicitly scoped sibling tree
- `--fetch`: retrieve a runner/debug bundle
- `--clean`: delete successful remote build output
- `--local-build-dir <path>`: choose the fetched bundle destination

Build directories passed after `--` must be relative and remain inside the
remote session.

## Twister

```sh
west remote twister <host>:/absolute/remote/west-workspace <session> \
  --source "$sdk_root" -- \
  -T tests/subsys/meshbus/services/clock \
  -p qemu_x86 --inline-logs -v -c
```

Use `--no-delete` only when remote-only session files must survive source sync.
Use `--clean` to remove successful Twister output; failed output is retained for
diagnosis.

## Sessions

```sh
west remote session <host>:/absolute/remote/west-workspace <session> \
  --source "$sdk_root"
west remote session list <host>:/absolute/remote/west-workspace
west remote session delete <host>:/absolute/remote/west-workspace <session>
```

A session lives under `<remote-workspace>/.remote/<session>`. Deletion removes
the whole named session, including diagnostic output.

## Hardware Transports

`remote flash`, `remote gdb`, and `remote serial` are separate hardware-facing
operations. Read each subcommand's `--help` immediately before authorized use.
A successful transport command proves only that operation; collect separate
serial, instrument, or manual evidence for runtime behavior.

Use `west remote <subcommand> --help` as the detailed option contract. Keep this
README focused on stable workflow boundaries instead of duplicating every CLI
flag.
