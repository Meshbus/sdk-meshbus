# Meshbus Service Test Rules

Scope: tests and reusable support under `tests/subsys/meshbus/`.

Public contracts come from `include/zephyr/meshbus/`; implementation rules come
from `subsys/meshbus/services/`. Local `testcase.yaml`, configuration, overlays,
and source define the runnable scenarios.

## Test Boundaries

Keep claims explicit and separate:

- `contract`: deterministic public API/ZBus behavior on the declared simulation
  platform, normally QEMU.
- `integration`: a focused cross-component or private seam that cannot be
  exercised through a stable public API. It does not replace contract coverage.
- `service_dut`: a real board using the real backend for the behavior claimed.
- `system`: multiple devices, product roles, or end-to-end protocol behavior.

Performance, upgrade, power, physical measurement, and soak evidence are
separate dimensions. A lower layer never proves a higher one; a build proves
compilation only.

## Contract Tests

Exercise public functions, types, documented errno values, and public ZBus
channels. Cover observable defaults, validation, set/get/reset, state changes,
settings behavior, bounded concurrency/timeouts, cleanup, and recovery where
they apply.

Contract applications must not include service-private headers or assert
private structs, mutexes, work items, static functions, or settings handlers. A
fake, emulator, or linker wrap may control a lower dependency, but assertions
remain at the public Meshbus boundary.

Do not retest Zephyr kernel, ZBus, Settings, or driver internals. Test the
additional behavior that Meshbus promises.

A separately named integration application may use one justified private seam.
Keep its purpose and claim narrow.

## Layout And Complexity

Service tests belong under `services/<service>/`. Shared non-application helper
sources belong under `common/`; multi-device flows under `system/`; performance
work under `performance/`; concise evidence guidance under `validation/`.

A service normally has one contract application and only the additional
scenario or child application required by a genuinely different platform,
backend, partition layout, reboot lifecycle, extension artifact, or role.

Prefer a small `src/main.c`; split by observable behavior only when that is
clearer. Add configuration, overlays, support doubles, and README files only
when used. Do not create a test DSL, capability registry, generated test layer,
generic runtime backend abstraction, or empty template files.

Default to zero test-only Kconfig symbols. When one is needed, name the exact
fault, DUT, or fixture purpose rather than a generic fake/hardware mode. Select
backend-specific sources at build time instead of skipping most tests at
runtime.

Extract shared helpers after repeated use or when one safety-critical helper
must be centralized. Keep assertion failures close enough to identify the
broken public contract.

## Test Doubles And Timing

Name every fake, emulator, simulator, or wrap at the substituted dependency
boundary. Do not reproduce the service state machine in a fake or bypass the
public API under test.

Use bounded event waits rather than arbitrary sleeps. Tests must not depend on
suite order. Keep deterministic simulator fault cases even when service-DUT
coverage exists.

## Settings And Real Storage

A persistent service should verify documented defaults, valid save/load,
reset/deletion of owned keys, malformed-record handling, reboot/reload
semantics, and failure rollback through public behavior.

Real-storage scenarios require a reviewed test-owned partition, compile-time
non-overlap/alignment evidence, bounded writes/erases, deterministic cleanup,
and a safe final state. Never reuse product settings storage. Interruption or
power-loss testing requires explicit authorization and a documented recovery
path.

## Service-DUT And Physical Evidence

A service-DUT scenario must use the real backend for each hardware behavior it
claims and must not silently fall back to a fake. Keep checks limited to the
real readiness, bus/storage/interrupt/timing, restart, and cleanup behavior that
simulation cannot prove.

Do not infer RF delivery, display appearance, buzzer output, button operation,
power limits, or peer interoperability from fixture selection or logs. Those
claims need an instrument assertion or explicit recorded human observation.

Do not run flash, debug, device-testing, reset, erase, power interruption, or
destructive shell commands without explicit user authorization for the exact
action. Record only reproducible testcase, artifact, device/fixture, command,
timeout, observation, result, cleanup, and final state. Never commit secrets or
live host-specific hardware maps.

## Validation

Use the exact leaf path and platform in the local YAML:

```sh
source ~/.zephyr/env/bin/activate
sdk_root="$(git rev-parse --show-toplevel)"
west_root="$(west topdir)"
cd "$west_root"
west twister -T "$sdk_root/tests/subsys/meshbus/services/<service>" \
  -p <platform-from-testcase.yaml> \
  -O "$west_root/twister-out/sdk-<service>" --inline-logs -v -c
```

Before reporting completion, state the observable contract changed, scenario
IDs/platforms, doubles or real backends used, exact commands/results, cleanup
for destructive state, and higher-layer behavior left unverified.
