# Meshbus Service Test Rules

Scope: tests, test support, and validation metadata under
`tests/subsys/meshbus/`.

These tests primarily verify public Meshbus service contracts. Explicitly
named integration applications cover a small number of justified in-tree
seams. Product implementation rules remain in
`subsys/meshbus/services/AGENTS.md`; public API and ABI rules remain in
`include/zephyr/meshbus/AGENTS.md`.

The only test-layout roots under this directory are:

- `services/`: service contract, service-DUT, physical, and service-specific
  integration applications;
- `common/`: reusable test sources and helpers, never an application root;
- `system/`: multi-device and end-to-end applications;
- `performance/`: performance and resource-measurement applications;
- `validation/`: lightweight human-readable evidence and reporting guidance.

Do not add service applications or `*_hardware` directories beside these
roots. Service tests belong under `services/<service>/`.

## 1. Design Goal

Use a thin, conventional Zephyr ztest template, not a Meshbus-specific test
framework.

The template should:

- make QEMU contract tests fast enough for the normal development loop;
- reuse hardware-safe public contract tests on the maintained C2 board;
- add small service-DUT checks for behavior that QEMU cannot prove;
- keep fake, emulator, real-device, fixture, and evidence boundaries explicit;
- make each PASS state exactly what was and was not proven.

Do not build a capability registry, runtime test-environment virtual table,
backend abstraction framework, test DSL, or generated test layer merely to
make service directories look identical. Templates standardize entry points,
names, scenario semantics, and evidence; service-specific assertions stay
ordinary readable ztest code.

## 2. Public Contracts And Focused Integration

Exercise public functions, types, constants, and ZBus channels declared under
`include/zephyr/meshbus/`.

Assert observable behavior:

- return values and documented error codes;
- copied output and stable defaults;
- configuration validation, set/get/reset, and no-op behavior;
- public ZBus payloads, state transitions, sequence changes, and timing bounds;
- service-owned settings save, restore, reset, malformed-record handling, and
  reboot behavior;
- externally visible busy, timeout, exhaustion, concurrency, shutdown, and
  recovery behavior where applicable;
- deterministic cleanup and the final public state.

Contract applications must not include service-private headers or directly
test private static functions, internal structs, mutexes, work items, settings
handlers, or file-local state. A fake or linker wrap may control a lower
dependency, but contract assertions must still be made through the public
Meshbus API or public ZBus contract.

A focused service-specific `integration` application may include a private
header only when a cross-component lifecycle, registry, access-policy, or
backpressure seam cannot be exercised through a stable public API. Keep it
separate from the contract scenario, name it `integration`, and do not count
its PASS as proof of the public contract or a physical backend.

Do not duplicate Zephyr's own ZBus, Settings, kernel, or driver API test
suites. Verify how the Meshbus service uses those facilities. Common Meshbus
settings helpers, key builders, and record parsers belong in one focused
common-helper suite rather than being retested by every service.

Reusable suites under `tests/subsys/meshbus/common/` may include the matching
private common-helper header and directly test that shared implementation.
They are implementation-test sources, not standalone applications: they must
not contain `CMakeLists.txt`, `prj.conf`, or `testcase.yaml`. Compile them into
one explicitly selected QEMU application and never into a service-DUT image.

When a public header or generated protobuf contract changes, update every
affected service test, schema/MCUmgr test, consumer, testcase manifest, and
relevant documentation.

## 3. Validation Layers

Keep these claims separate:

1. `contract`: deterministic public API/ZBus behavior on QEMU or another
   declared simulation platform.
2. `integration`: focused in-tree component behavior that may use a private
   seam and is not itself a stable public contract.
3. `service_dut`: a dedicated image on a real board using the real backend for
   the service behavior it claims.
4. `physical`: a calibrated instrument assertion or an explicit recorded human
   observation.
5. `role`: a release-like product-role image with realistic service
   composition and resource interaction.
6. `system`: two or more devices and an end-to-end protocol flow.

Upgrade, performance, power, and soak validation are separate release
dimensions and do not become service contracts merely because they exercise a
service.

A lower layer never proves a higher one. QEMU does not prove hardware. A
single-DUT C2 result does not prove RF delivery, peer interoperability, product
composition, routing, upgrade safety, power limits, or long-term reliability.

Use a separate testcase ID, test application, or external orchestration
scenario when the platform, boot lifecycle, backend, firmware role, fixture,
destructiveness, or evidence requirements differ materially.

## 4. Preferred Service-Test Shape

The preferred upper-bound template is:

```text
tests/subsys/meshbus/services/<service>/
├── CMakeLists.txt
├── Kconfig                         # only when test-only choices are needed
├── prj.conf                        # default contract configuration
├── testcase.yaml
├── README.rst                      # only when fixtures/risks need explanation
├── configs/
│   └── service_dut.conf            # only when a C2 scenario exists
├── boards/
│   ├── qemu_x86.overlay            # only when contract DTS is needed
│   └── idea_mesh_tracker_c2_nrf54l15_cpuapp.overlay
├── src/
│   ├── main.c                      # allowed for a small service
│   ├── test_api.c
│   ├── test_zbus.c
│   ├── test_settings.c             # optional
│   ├── test_errors.c               # optional, deterministic fault paths
│   └── test_service_dut.c          # optional, real backend checks
└── support/
    └── fake_<dependency>.c          # optional lower-boundary test double
```

This is a menu, not a required empty skeleton:

- Keep a small suite in `src/main.c`.
- Split files by public behavior only when the suite becomes easier to read.
- Do not create empty `test_zbus.c`, `test_settings.c`, `support/`, `configs/`,
  or board files to satisfy the template.
- Prefer explicit source lists in CMake. Use a glob only when the local Zephyr
  test style and file set make it unambiguous.

Suggested file responsibilities:

- `test_api.c`: arguments, return values, set/get/reset, defaults, state, and
  documented error behavior.
- `test_zbus.c`: public channel payloads, ordering, sequence, completion, and
  timeout behavior.
- `test_settings.c`: only the service-owned persistence contract.
- `test_errors.c`: deterministic unavailable, busy, timeout, exhaustion, and
  rollback paths enabled by a fake, emulator, or linker wrap.
- `test_service_dut.c`: the smallest checks that require the real device,
  storage, interrupt, bus, clock, or other real backend.

File names describe the behavior being tested. `fake` or `mock` belongs in the
support implementation name; generic `contract_fake.c` and `hardware.c` files
are not the default template.

## 5. Default Scenario Model

A service normally has no more than two Twister scenarios:

```text
subsys.meshbus.<service>.contract
subsys.meshbus.<service>.service_dut.c2
```

A justified private-seam application is an explicit exception:

```text
subsys.meshbus.<service>.integration.<purpose>
```

Do not fold it into `contract` merely to satisfy the two-scenario default.

The contract scenario:

- runs on the platform declared by `testcase.yaml`, normally `qemu_x86`;
- is the primary development and CI gate;
- runs public API, public ZBus, settings-contract, and deterministic
  fault-injection tests;
- uses explicitly named fakes, emulators, simulators, or linker wraps.

The C2 service-DUT scenario:

- runs on `idea_mesh_tracker_c2/nrf54l15/cpuapp`;
- runs the same hardware-safe API, ZBus, and settings contract tests;
- adds `test_service_dut.c` for real-backend behavior;
- declares its fixtures, timeout, destructive actions, cleanup, and claim
  boundary;
- must not compile or silently fall back to the fake backend for behavior
  claimed as real.

The C2 scenario does not have to run simulator-only fault tests. Reuse public
contract source where it is safe and meaningful; do not force byte-for-byte
identical test sets across fundamentally different backends.

If the service has no real-device behavior, keep only the contract scenario.
If a scenario requires a peer, instrument, controlled power, multiple firmware
roles, bootloader, N-1 artifact, or multiple devices, move it to the matching
physical, system, upgrade, performance, power, or soak workflow instead of
growing the service-DUT application.

## 6. Scenario And Configuration Naming

Use validation-layer names for scenarios and specific-purpose names for test
configuration.

Preferred:

```text
contract
service_dut.c2
physical.rf
service_dut.conf
test_service_dut.c
fake_radio.c
```

Avoid generic mutually exclusive modes such as:

```text
CONFIG_MESHBUS_TEST_FAKE
CONFIG_MESHBUS_TEST_HARDWARE
```

`fake` and `hardware` are not exact opposites: a real-board test may still wrap
an unrelated dependency, and a hardware result may mean a real MCU, real
storage, real sensor, RF peer, or calibrated instrument.

When a test-only Kconfig selector is genuinely needed, name the exact purpose:

```text
CONFIG_TEST_MESHBUS_<SERVICE>_FAULT_INJECTION
CONFIG_TEST_MESHBUS_<SERVICE>_SERVICE_DUT
CONFIG_TEST_MESHBUS_<SERVICE>_RF_FIXTURE
```

Public API, ZBus, and ordinary settings tests should normally compile without a
test-mode selector. Prefer `platform_allow`, board overlays, `extra_configs`,
`extra_conf_files`, and Twister fixtures for scenario selection.

Select simulator-only or service-DUT sources at build time. Do not detect the
platform at runtime and skip large parts of a suite.

## 7. Complexity Limits

Keep the test system smaller than the services it verifies:

- Default to one Zephyr test application and up to two scenarios per service.
- Default to zero test-only Kconfig symbols; ordinarily do not exceed two.
- Do not introduce a generic `test_env` vtable or fake/real backend interface.
- Do not reproduce the service state machine inside a fake.
- Do not create one source file per public function.
- Keep tests in one `main.c` while that remains clearer than splitting them.
- Extract a shared helper only after the same safe pattern appears in at least
  three services, or when one safety-critical implementation must be
  centralized.
- Keep helper assertions small enough that a failure still identifies the
  public contract that broke.
- Use suite predicates only for a real multi-phase lifecycle, not as a general
  platform-selection mechanism.
- Do not use a coverage percentage as a substitute for boundary, fault,
  concurrency, and recovery review.

One application is not absolute. Use multiple applications with shared public
contract source when configurations cannot safely coexist, including
incompatible partition layouts, reboot phases, extension artifacts, bootloader
states, or firmware roles.

## 8. Test Doubles

Name every substituted boundary in source, devicetree, testcase metadata, and
evidence.

Valid contract tools include:

- Zephyr flash, UART, GPIO, ADC, and sensor emulators;
- fake or synthetic drivers;
- FFF fakes;
- linker-wrapped dependencies;
- deterministic test hooks already permitted by the local service design.

Use a double only at a lower dependency boundary. Do not use it to bypass the
public service API being tested. Preserve deterministic QEMU fault injection
after a C2 scenario exists; real hardware is usually worse at producing
repeatable `-EBUSY`, timeout, malformed, and I/O-failure paths.

## 9. ZBus Contract Rules

Test only the service's public use of ZBus:

- channel payload and validation;
- publish/response/completion relationships;
- state and sequence changes;
- bounded notification or timeout behavior;
- behavior while disabled, unavailable, busy, or shutting down;
- observable queue or subscriber backpressure where the public contract
  promises it.

Do not retest ZBus internals such as channel registration, observer allocation,
priority boost, or core delivery algorithms unless Meshbus owns an additional
public guarantee around them.

Tests must use bounded waits and produce a useful failure instead of sleeping
for arbitrary time. Avoid depending on suite or test order.

## 10. Settings And Real Storage

Each persistent service tests only its own public persistence behavior:

- missing records keep documented defaults;
- valid set/save/load returns the same public configuration;
- reset removes service-owned records and restores defaults;
- malformed or unknown records do not block boot;
- reboot or reload semantics match the public contract;
- failed apply or persistence does not expose an invalid committed state.

A C2 real-storage claim requires:

- `CONFIG_FLASH_SIMULATOR=n`;
- a reviewed test-owned partition;
- alignment and non-overlap compile-time proof;
- bounded and documented write/erase count;
- no erase or reuse of product settings/storage;
- deterministic cleanup before the next run;
- a known safe final state even after a previously interrupted run.

Mid-write reset or power interruption requires a dedicated device, an explicit
recovery procedure, bounded attempts, and separate user authorization.

## 11. Service-DUT And Physical Boundaries

A service-DUT test must use the real backend for every behavior it claims. A
build-only result proves compilation only.

Keep service-DUT checks focused:

- device readiness and correct devicetree selection;
- real bus/driver/storage operation;
- real interrupt, timing, or restart behavior that simulation cannot prove;
- disable/re-enable and bounded recovery;
- cleanup and safe final state.

Do not claim packet delivery, display appearance, buzzer output, button input,
current limits, RF parameters, sensitivity, range, or peer interoperability
from fixture matching or a boot log. Such claims require automated instrument
evidence or an explicit manual record.

The maintained physical board for these tests is currently
`idea_mesh_tracker_c2/nrf54l15/cpuapp`. A C2 result never qualifies another
board.

## 12. Harness, Authority, And Evidence

- `harness: ztest` or external pytest/instrument assertions may produce an
  automated PASS.
- `harness: keyboard` is a manual startup/diagnosis surface and cannot produce
  an automated release PASS.
- A Twister fixture matches capabilities; it does not itself assert physical
  behavior.
- A manual record includes the prompt, expected observation, operator answer,
  timestamp, device identity, and optional attachment. Pending or rejected
  checks remain non-pass.

Do not run `west flash`, `west debug`, `west twister --device-testing`, reset,
settings erase, power interruption, or destructive shell commands without
explicit user authorization for the exact action.

For hardware evidence:

- treat the C2 serial endpoint and SWD probe identity as separate fields;
- start serial capture before flash/reset when startup evidence matters;
- use only an explicitly approved reset method;
- wait for the business result, not only a boot banner or shell prompt;
- retain trailing asynchronous output;
- record source and dirty state, testcase/source version, board, configuration,
  artifact hash, runner, device, serial endpoint, commands, waits, timeout,
  result, measurement/manual evidence, cleanup, and final classification.

Never publish credentials, private keys, pairing secrets, or live
host-specific hardware maps.

## 13. Results, Determinism, And Coverage

Keep result classes distinct:

- `pass`
- `product-failed`
- `test-defect`
- `flaky`
- `infrastructure-blocked`
- `capability-unavailable`
- `authorization-pending`
- `manual-pending`
- `manual-rejected`
- `filtered`
- `not-applicable`

Never convert a missing fixture, unavailable board, discovery failure, pending
manual check, or missing authorization into pass. A bounded retry is allowed
only after classifying an infrastructure or flaky condition; retain both
results.

Keep canonical-order QEMU execution as the primary contract gate. Add a
recorded fixed-seed shuffle scenario only where order independence matters, and
preserve every failing seed. Collect QEMU contract coverage where supported,
track uncovered public behavior and trends, and avoid setting a numerical gate
before the public-contract inventory is credible.

## 14. Layout And Maintenance

Maintain each service test in this order:

1. Preserve the reviewed behavior and testcase claims when reorganizing the
   local application.
2. Keep the QEMU contract scenario passing with the same or explicitly
   reviewed cases.
3. Reuse only hardware-safe contract tests in the C2 service-DUT scenario.
4. Keep real-backend checks to the smallest set needed for the declared
   hardware claim.
5. Keep testcase manifests, local READMEs, and validation guidance aligned in
   the same change.
6. Expand missing API, ZBus, settings, fault, concurrency, and recovery
   coverage in a separate reviewed step where practical.

Do not reintroduce legacy top-level service roots or generic
`<service>_hardware` application names. Keep framework invention, service
implementation rewrites, and large coverage expansion separate from layout
maintenance.

Use representative services before freezing or expanding the template:

- Radio for API/ZBus plus a real peripheral backend.
- Channel or Contact for settings, capacity, concurrency, and real storage.
- Notify, Input, or Message for event flow, queueing, and dependency doubles.

When adding or changing a suite:

- keep `testcase.yaml`, CMake, Kconfig/prj.conf, overlays, sources, and local
  documentation aligned;
- cover normal, invalid, unavailable, busy, timeout, exhaustion, malformed,
  concurrency, shutdown, and recovery paths that apply;
- test important Kconfig on/off and capacity variants where observable;
- identify service-owned settings keys and migration/reset/reboot behavior;
- document substituted and real boundaries, fixtures, destructive actions,
  cleanup, and non-applicable higher layers;
- run the narrow suite before a broader Meshbus sweep.

## 15. Validation Commands

Run Zephyr builds and Twister from `west topdir`, using `meshbus/` paths.

For a service contract:

```sh
source ~/.zephyr/env/bin/activate
cd "$(west topdir)"
west twister -T sdk-meshbus/tests/subsys/meshbus/services/<service> \
  -p qemu_x86 --inline-logs -v -c
```

Use the exact leaf application path declared by the local `testcase.yaml`.
The MeshCore companion protocol may continue using `qemu_cortex_m3` where its
local testcase requires it.

Do not run a C2 device-testing, flash, reset, or destructive command without
the explicit authorization required by this file.

## 16. Completion Checklist

Before reporting a Meshbus service-test change complete, state:

- which public API, ZBus, settings, and error contracts changed;
- which sources are common contract, simulator-only fault, or service-DUT
  checks;
- every fake, emulator, wrap, real backend, and fixture used;
- testcase IDs, platforms, commands, and results;
- real-storage layout and cleanup when applicable;
- higher-layer claims that remain unverified;
- whether testcase manifests and documentation reference anything outside the
  canonical layout roots.
