# Meshbus Service Tests

Public contracts come from `include/zephyr/meshbus/`. Local `testcase.yaml`,
configuration, overlays, and sources define runnable scenarios and backends.
Service tests, common support, system flows, and performance scenarios retain
their existing ownership and directory structure.

`validation/README.rst` is a short evidence guide, not another framework,
scenario matrix, or source of execution permission.

## Contract Boundary

- Exercise public functions, types, constants, error behavior, and ZBus
  channels. Assert observable state, payloads, persistence, timeouts, recovery,
  and deterministic cleanup where the public contract exposes them.
- Contract scenarios do not include service-private headers or assert private
  structs, locks, work items, settings handlers, or file-local state.
- A separately named `integration` scenario may use a justified private seam.
  Its pass does not prove the public contract or a physical backend.
- Put fakes, emulators, and linker wraps below the public service boundary.
  Name every substituted dependency and keep assertions on public behavior.
- Test Meshbus use of Zephyr facilities; do not reproduce Zephyr's own ZBus,
  Settings, kernel, or driver test suites.
- Use bounded waits with useful failures. Do not depend on suite or test order.

Keep tests ordinary ztest applications. Add files, Kconfig selectors, helper
layers, or separate applications only when a real behavior or lifecycle seam
requires them. Shared helpers are source libraries compiled by a declared test
application, not additional runnable roots.

## Evidence Layers

Keep these claims distinct:

1. `contract`: deterministic public behavior on a declared simulation platform.
2. `integration`: focused in-tree behavior using a justified internal seam.
3. `service_dut`: a dedicated real-board image using the claimed real backend.
4. `physical`: instrument evidence or an explicit recorded human observation.
5. `role`: a release-like product-role image with realistic composition.
6. `system`: two or more devices and an end-to-end protocol flow.

Upgrade, performance, power, soak, and release qualification are separate
dimensions. A lower layer never proves a higher one: QEMU does not prove
hardware, and one DUT does not prove RF, peer interoperability, product
composition, upgrade safety, power limits, or long-term reliability.

Use separate testcase IDs or applications when platform, backend, boot
lifecycle, fixture, destructiveness, firmware role, or evidence requirements
differ materially. Prefer scenario-specific overlays, configuration files, and
Twister fixtures over runtime platform detection and broad test skipping.

## Service-DUT And Physical Evidence

- A service-DUT scenario uses the real backend for every behavior it claims.
  Fixture matching describes capability; it is not a physical assertion.
- A real-storage scenario uses a reviewed test-owned partition, bounded erase
  and write counts, deterministic cleanup, and a known safe final state. It does
  not reuse product settings or storage.
- Flash, reset, debug, device-testing, destructive shell commands, settings
  erase, and power interruption require explicit authorization for the exact
  action.
- Start serial capture before flash or reset when startup evidence matters.
  Record the serial endpoint and probe identity separately, use an approved
  reset method, wait for the business result, and retain trailing output.
- Record source revision and dirty state, testcase ID, board, configuration,
  artifact, runner, fixture, device, serial endpoint, commands, waits, timeout,
  result, cleanup, and final device state.
- Keep credentials, private keys, pairing secrets, and live host-device maps out
  of committed records and responses.

Classify results as pass, product failure, test defect, flaky,
infrastructure-blocked, capability-unavailable, authorization-pending, or
manual-pending/rejected as applicable. Missing fixtures, discovery failures,
unavailable devices, and pending authorization are never passes. Retain both
attempts after a bounded infrastructure or flaky retry.

## Validation

Run from `west topdir`. Select the narrowest leaf and platform declared by its
`testcase.yaml`:

```sh
west twister -T <sdk-path>/tests/subsys/meshbus/<leaf> \
  -p <platform-from-testcase> --inline-logs -v -c
```

If a parallel run fails with generated configuration or setup noise, rerun the
same leaf with `-j 1` before classifying a product regression. On failure, find
the first relevant build log, runtime log, or assertion, patch only an in-scope
cause, and rerun the same narrow command before expanding coverage.

Before completion, report changed public contracts, testcase IDs, platforms,
substituted and real boundaries, exact commands and results, cleanup, and every
higher evidence layer that remains unverified.
