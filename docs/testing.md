<!-- SPDX-FileCopyrightText: 2026 FoBE Studio -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Testing and evidence

Use the nearest `testcase.yaml` or `sample.yaml` to select a runnable target and
scenario. [DEVELOPMENT.md](../DEVELOPMENT.md#sdk-builds-and-tests) owns environment
setup and commands. Run the smallest check that proves the changed behavior;
expand only to resolve a specific failure or acceptance gap.

## Choosing coverage

Add a test for an agreed behavior, a reachable failure with meaningful impact,
or a known defect. Reuse a matching test application and configuration first.
Another scenario, platform or fixture needs distinct coverage; different names
or tags alone do not justify rebuilding the same program. Do not require a test
per helper, a failure at every internal call, or a coverage percentage target.
Pure refactors use existing regressions unless they expose an uncovered contract.

| Stage | Default scope |
| --- | --- |
| Red / Green | One behavior, one scenario, one declared platform |
| Refactor | Affected regressions using the same build where valid |
| PR | Explicit component and integration owners; affected product composition |
| Full scheduled/manual CI | All supported scenarios, including extended variants |
| Device/release qualification | Named physical and release acceptance |

Use the existing `shuffle` and `performance` tags for extended coverage. CI retains
them for directly changed test roots and full validation; settings changes also
retain settings performance tests. Performance claims need a defined platform,
sample policy and threshold; printing timing alone is diagnostic evidence.
Distinct heap, persistence, backend and lifecycle configurations remain useful
when they cover independent risks. See [CI selection](../.github/CI.md#stateless-selection).

Stop when the stated acceptance and affected checks pass. Add another check only
to answer a specific remaining question; do not replay the full matrix after
each edit. Preserve regressions for corrupt external data, resource exhaustion,
asynchronous cleanup and compatibility.

## Public behavior

Service contract tests exercise public functions, errors, state, persistence,
ZBus payloads and lifecycle cleanup. Keep assertions independent of private
structs, locks, work items and settings handlers. Place fakes or linker wraps
below the public boundary and name substituted dependencies. A separately named
integration scenario may use a small, justified internal seam. Avoid reshaping
production APIs or adding configuration switches solely to make a test possible.
Do not replace a bounded existing seam with a larger mocking framework.
Use bounded waits and independent test setup; test Meshbus behavior rather than
reproducing Zephyr tests.

Keep tests ordinary ztest applications. Split scenarios when platform, backend,
boot lifecycle, fixture or destructiveness differs. Prefer configuration and
Twister fixtures over runtime platform detection and broad skipping. Test IDs
use `subsys.meshbus` and the `meshbus` tag; independent ZUI tests live in sdk-zui.

## Evidence boundaries

| Evidence | What it establishes |
| --- | --- |
| Contract | Deterministic public behavior on the declared simulation platform |
| Integration | Behavior through a named integration seam |
| Service DUT | Real-board behavior using the claimed physical backend |
| Physical | Instrument evidence or an explicit human observation |
| Product role | Behavior in the actual product and role composition |
| System | End-to-end behavior across the required devices |

Build/link success is not startup proof. QEMU does not prove a physical backend;
one device does not prove RF interoperability. Upgrade, performance, power, soak
and release qualification need their own evidence. Report unavailable fixtures,
setup failures, pending authorization and missing observations as gaps, not passes.

## Tools and device work

| Task | Existing tool | Evidence limit |
| --- | --- | --- |
| Service contracts | Declared Twister scenario | Simulation is not a hardware result |
| Product composition | `west build --sysbuild`, final configs/DTS and layout | Build success is not runtime qualification |
| UART commands | `west meshbus connect ... --json` | Inspect firmware capabilities and command schema |
| Startup/asynchronous events | `scripts/serial_use.py` | Wait for the business result and retain trailing output |
| Framebuffer export | `scripts/display_capture.py` | PNG proves the software frame and transfer, not panel appearance |
| Synthetic navigation | MCUmgr input injection | Acceptance is not UI consumption or physical gesture detection |
| Flash, reset and debug | Board runner or existing probe tools | Exact device/action authorization is required |
| Remote transport | `scripts/remote/README.md` | Forwarding does not move build ownership or grant remote access |

Read current command help before adding a transport or dependency. Python host
helpers have device-free tests in `scripts/tests/`. Use the existing development
environment; authorization for tool use does not imply dependency installation.
See the [CLI guide](../scripts/meshbus/README.md#display-capture) for display
capture and input injection, and [serial commands](../DEVELOPMENT.md#serial-and-remote-tools)
for connection and transcript handling.

Identify the target, installed firmware, serial endpoint and probe separately.
Prepare the concrete image and layout before requesting deployment permission.
Start capture before an authorized reset/flash when startup matters. Keep one
UART owner at a time, release injected keys, and record cleanup and the observed
final state. Use bounded waits; preserve the first failure and any justified retry.
Stop task-owned connections and retries if the user removes the test from scope.

For BLE, derive filters from advertising and GATT/security configuration. Discovery,
connection, authenticated service access and payload transfer are separate results.
For storage tests, use a reviewed test-owned partition and bounded writes/erases;
do not reuse product settings. Physical OLED acceptance needs observation beyond
the exported frame. Store sensitive mappings and raw transcripts only in ignored
task storage; shared validation records contain non-sensitive summaries.
