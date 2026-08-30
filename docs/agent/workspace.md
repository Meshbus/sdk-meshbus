# Workspace And Execution Boundaries

Read this guide only for west layout, module visibility, parent-workspace
access, or remote execution.

## Discover The Layout

The SDK can be the manifest repository in one workspace and an imported west
project in another. Discover both roots before constructing paths:

```sh
source ~/.zephyr/env/bin/activate
sdk_root="$(git rev-parse --show-toplevel)"
west_root="$(west topdir)"
west config manifest.path
west list -f '{name} {path}'
```

A failing `west list` does not justify guessing paths. Report the workspace
problem and use `sdk_root` for source inspection. Local Zephyr commands may use
absolute paths below `sdk_root`.

## Mutation Boundary

Writable by default:

- files inside `sdk_root`
- task-owned ignored build/Twister outputs created by validation tools

Read-only unless explicitly requested:

- `.west/`, Zephyr, bootloader, modules, tools, and sibling source projects
- toolchains and shared Python environments
- pre-existing generated, cache, and fetched artifact directories not owned by
  the current task

If a fix appears to require another project, explain the dependency and ask for
scope instead of silently crossing the boundary.

## Module Visibility

This repository exports a Zephyr module through `zephyr/module.yml`. Before
adding `EXTRA_ZEPHYR_MODULES`, check whether west already exposes the checkout.
Use an explicit module path only for a consuming application outside the active
manifest graph.

## Command Location

Prefer discovered absolute source paths so commands work regardless of the
west project name:

```sh
cd "$west_root"
west build -p auto -d "$west_root/build/sdk-<name>" \
  -b <board> "$sdk_root/<sample-or-test>"
```

Do not encode a checkout basename in durable instructions.

## Remote Execution

Use remote helpers only when the user requests remote execution or supplies a
remote target. Read `scripts/remote/README.md` and the selected command's
`--help` at that point.

`west remote doctor --fix`, remote updates, session deletion, remote cleanup,
flashing, GDB, and serial forwarding can mutate remote or physical state; apply
the command-specific authorization rules before running them.

If tooling is missing, report it rather than installing global packages or
modifying the shared environment without approval.
