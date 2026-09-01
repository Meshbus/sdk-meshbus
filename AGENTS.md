# Meshbus SDK Entry

This is an independent Git repository for reusable services, public APIs,
drivers, protocols, UI components, samples, and tests. Product composition,
boot/release policy, host CLI, and distribution belong to the firmware repository.

Before substantive changes, review, or validation, read the nearest applicable
`AGENTS.md`. Public headers, schemas, Kconfig, CMake, devicetree, test metadata,
source, and tests remain the technical sources of truth.

Use `git rev-parse --show-toplevel` for the SDK Git root and `west topdir` for
the consuming workspace. Keep SDK work and firmware-product work separately
scoped. Treat sibling west projects, toolchains, caches, and generated output as
read-only build context unless the user requests changes there.

Read these local rules only when the task enters their scope:

- `include/zephyr/meshbus/AGENTS.md` for public Meshbus API and ABI.
- `subsys/meshbus/services/AGENTS.md` for service runtime and persistence.
- `subsys/meshbus/services/desktop/AGENTS.md` for Desktop and ZUI integration.
- `tests/subsys/meshbus/AGENTS.md` for tests and evidence boundaries.

Preserve unrelated changes and keep patches narrow. Obtain explicit
authorization for dependency or manifest changes, remote or hardware actions,
flash, reset, debug, signing, and publication. Stage, commit, or push only when
requested. Leave generated build output unchanged and keep secrets out of
source, logs, and responses.

Select the smallest build or test that proves the affected behavior. Read the
nearest `sample.yaml` or `testcase.yaml` for supported targets and scenarios,
run Zephyr commands from `west topdir`, and report the exact commands and
results. State unverified hardware and higher-level qualification separately.
