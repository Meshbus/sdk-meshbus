# MeshCore C Library Test Architecture

This document describes the current SDK-side test architecture for the
external MeshCore module at `modules/lib/meshcore`. Tests are organized by the
same three-layer model used by the MeshCore module architecture document.

1. `protocol/`: parity against upstream protocol class/helper behavior
2. `runtime/`: observable runtime behavior against upstream runtime examples
3. `boundary/`: public ABI, HAL, PAL, and scheduling contracts

No migration-only suite is part of the current target tree. If a future active
phase needs temporary preservation coverage, it should create that suite with
an explicit target layer and deletion condition.

Library source selection is owned by the external module's
`cmake/meshcore_sources.cmake`. SDK tests resolve the module through Zephyr's
`ZEPHYR_MESHCORE_MODULE_DIR` and then consume the manifest variables.
Runtime and full-library tests should consume `MESHCORE_RUNTIME_LIBRARY_SOURCES`;
focused protocol tests should consume the smallest manifest groups that cover
the module under test. Do not add new hand-maintained complete MeshCore source
lists to tests.

## Goals

- Make upstream compatibility reviewable by layer.
- Separate protocol parity from runtime behavior.
- Separate generic library contracts from host integration tests.
- Avoid preserving old test paths as accidental architecture.
- Allow tests to be migrated, replaced, or deleted deliberately.

## Non-Goals

- Do not test current source-file names as the contract.
- Do not preserve stale test directories as architecture by accident.
- Do not mix Zephyr/Meshbus service behavior into generic library tests.
- Do not use upstream example UI, board, transport, or persistence code as
  generic library oracle evidence.
- Do not make migration-only tests permanent without assigning them to a target
  layer.

## Test Tree

The test tree is organized by verification responsibility:

```text
tests/lib/meshcore/
  TESTING.md

  protocol/
    packet/
    identity/
    utils/
    rng/
    clock/
    radio/
    packet_manager/
    dispatcher/
    mesh/
    tables/
    group_channel/
    advert_data/
    text_data/

  runtime/
    oracle/
    requests/
    receive/
    pending/
    negative/

  boundary/
    public_abi/
    platform_contract/
    scheduling/

```

Do not move files mechanically just to match this tree. When a suite moves,
name the upstream evidence, target layer, and behavior being preserved. Some
leaf directories may be logical coverage groups rather than physical
directories when a nearby suite already covers the behavior.

## Layer 1: `protocol/` Tests

`protocol/` tests verify the C protocol API against upstream protocol
class/helper evidence from `.reference/meshcore/src`.

They should be mostly focused, deterministic, module-level tests.

### Oracle Source

Use only upstream protocol sources for Layer 1 oracle behavior:

- `.reference/meshcore/src/Packet.h`
- `.reference/meshcore/src/Packet.cpp`
- `.reference/meshcore/src/Utils.h`
- `.reference/meshcore/src/Utils.cpp`
- `.reference/meshcore/src/Identity.h`
- `.reference/meshcore/src/Identity.cpp`
- `.reference/meshcore/src/Dispatcher.h`
- `.reference/meshcore/src/Dispatcher.cpp`
- `.reference/meshcore/src/Mesh.h`
- `.reference/meshcore/src/Mesh.cpp`
- `.reference/meshcore/src/MeshCore.h`
- `.reference/meshcore/src/helpers/StaticPoolPacketManager.h`
- `.reference/meshcore/src/helpers/StaticPoolPacketManager.cpp`
- `.reference/meshcore/src/helpers/SimpleMeshTables.h`
- `.reference/meshcore/src/helpers/AdvertDataHelpers.h`
- `.reference/meshcore/src/helpers/AdvertDataHelpers.cpp`
- `.reference/meshcore/src/helpers/TxtDataHelpers.h`
- `.reference/meshcore/src/helpers/TxtDataHelpers.cpp`

Layer 1 must not use `examples/` as oracle input.

### Required Coverage Areas

`packet/` should cover:

- packet type and payload layout
- direct, flood, and zero-hop route fields
- path and transport-code fields
- read/write helpers
- boundary length handling
- malformed input rejection

`utils/` should cover:

- encrypt-then-MAC behavior
- MAC layout and verification behavior
- AES block iteration and padding behavior
- hex helpers
- text parsing helpers

`identity/` should cover:

- identity and local identity data shape
- public/private key handling
- sign and verify behavior
- malformed key/signature handling

`dispatcher/`, `packet_manager/`, and `mesh/` should cover:

- delayed packet ownership
- queue capacity and allocation behavior
- receive routing
- send routing
- ACK behavior
- timeout behavior
- CAD and duty-budget decisions when protocol-visible
- path and trace packet handling

`tables/` and `group_channel/` should cover:

- peer lookup behavior
- channel lookup behavior
- hash matching behavior
- minimal table behavior required by protocol logic

`advert_data/` and `text_data/` should cover:

- wire encoding
- parser boundary behavior
- missing or malformed field handling
- upstream-compatible truncation or rejection decisions

### Protocol API Map Check

`protocol/protocol_api_map.json` is the Layer 1 method inventory ledger. Its
upstream method rows are generated from selected upstream protocol classes and
helpers, then each row maps to C protocol symbols or is marked as `excluded` /
`deferred` with a reason.

Regenerate the map template from upstream headers:

```sh
python3 tests/lib/meshcore/protocol/tools/check_protocol_api_map.py \
  --repo-root . \
  --map tests/lib/meshcore/protocol/protocol_api_map.json \
  --emit-upstream-template
```

To refresh the checked-in map in place:

```sh
python3 tests/lib/meshcore/protocol/tools/check_protocol_api_map.py \
  --repo-root . \
  --map tests/lib/meshcore/protocol/protocol_api_map.json \
  --refresh-map
```

New upstream methods are emitted as `unmapped`, which intentionally fails the
normal checker until the row is mapped to C symbols or explicitly excluded /
deferred.

Run the checker from the repository root:

```sh
python3 tests/lib/meshcore/protocol/tools/check_protocol_api_map.py \
  --repo-root . \
  --map tests/lib/meshcore/protocol/protocol_api_map.json
```

Run this host check separately from protocol Twister suites. It exits nonzero
when the SDK map is stale or the required upstream reference is unavailable;
no Zephyr build or QEMU application is needed. Use the reference revision from
the external MeshCore module's `upstream.lock`, and do not omit evidence checks
when that checkout is missing. The module's own native/CI checks own
source-manifest and sync-report validation. The SDK protocol inventory check
only checks that the SDK compatibility ledger still matches the external
module and upstream evidence.

This check prevents inventory drift: missing upstream methods, stale mapped C
symbols, overload-count changes, and undocumented exclusions. It is not a
semantic parity proof. Behavior still needs focused protocol tests that compare
the upstream C++ implementation and the C implementation under equivalent
inputs.

## Layer 2: `runtime/` Tests

`runtime/` tests verify observable behavior of the host-driven C runtime.

They should treat the runtime as a black box:

- call public runtime APIs
- inject radio RX frames
- inject TX completion
- inject timer expiry
- observe outbound raw packets
- observe PAL publish callbacks
- observe pending request lifecycle

Runtime tests must not retest packet internals that belong to `protocol/`.

Runtime white-box hooks such as `meshcore_test_runtime_*` are test-only
symbols. Suites that need them must compile runtime sources with
`MESHCORE_ENABLE_TEST_HOOKS`; generic production consumers must not rely on
those names being linked.

### Oracle Source

Use runtime behavior evidence from:

- `.reference/meshcore/src/helpers/BaseChatMesh.h`
- `.reference/meshcore/src/helpers/BaseChatMesh.cpp`
- `.reference/meshcore/src/helpers/TxtDataHelpers.h`
- `.reference/meshcore/src/helpers/TxtDataHelpers.cpp`
- `.reference/meshcore/src/helpers/AdvertDataHelpers.h`
- `.reference/meshcore/src/helpers/AdvertDataHelpers.cpp`
- `.reference/meshcore/src/helpers/ContactInfo.h`
- `.reference/meshcore/src/helpers/ChannelDetails.h`
- `.reference/meshcore/examples/companion_radio/main.cpp`
- `.reference/meshcore/examples/companion_radio/MyMesh.h`
- `.reference/meshcore/examples/companion_radio/MyMesh.cpp`
- `.reference/meshcore/examples/companion_radio/NodePrefs.h`
- `.reference/meshcore/examples/companion_radio/DataStore.h`
- `.reference/meshcore/examples/companion_radio/DataStore.cpp`

Use `ContactInfo` and `ChannelDetails` only for protocol fields consumed by
runtime behavior, such as peer public keys, paths, channel secrets, and hashes.
Caller-owned Contact/Channel business abstractions and persistence must not be
projected into generic runtime tests.

### Required Coverage Areas

`runtime/oracle/` should cover behavior where upstream and C runtime can be run
with equivalent fixtures and compared directly.

Required observable surfaces:

- local advert request
- flood advert request
- peer advert replay
- direct peer message
- flood peer message
- channel message
- path discovery request
- path discovery response
- trace request
- trace response
- telemetry request
- telemetry response
- timeout cleanup
- invalid inbound packet handling

`runtime/requests/` should cover public request shape:

- required parameters
- size limits
- request serialization into protocol operations
- no-local-identity behavior
- missing-peer or missing-channel behavior

`runtime/receive/` should cover inbound behavior:

- advert observe/publish
- peer message publish
- channel message publish
- path result publish
- trace result publish
- telemetry result publish
- no-follow-up cases

`runtime/pending/` should cover:

- request correlation
- wrong-peer response ignore
- wrong-tag response ignore
- duplicate response ignore
- second request overwrite behavior
- timeout clearing

`runtime/negative/` should cover:

- malformed raw frames
- invalid encrypted payload variants
- unsupported payload types
- short payloads
- oversized payloads
- unexpected response types

### Runtime API Map Check

`runtime/runtime_api_map.json` maps each public `meshcore_*` runtime ABI from
`modules/lib/meshcore/include/meshcore/runtime.h` to its coverage owner, upstream
behavior evidence, and concrete ZTEST names. It is an API/evidence coverage
ledger, not an upstream behavior oracle.

Rows marked `covered` are runtime behaviors backed by upstream evidence and
must list `upstream_evidence`. Rows marked `target_only` are intentional C
host-facade APIs, such as lifecycle or event-ingress calls, and must explain why
there is no direct Arduino public-method counterpart.

Regenerate the map template from `meshcore/runtime.h`:

```sh
python3 tests/lib/meshcore/runtime/tools/check_runtime_api_map.py \
  --repo-root . \
  --map tests/lib/meshcore/runtime/runtime_api_map.json \
  --emit-template
```

To refresh the checked-in map in place:

```sh
python3 tests/lib/meshcore/runtime/tools/check_runtime_api_map.py \
  --repo-root . \
  --map tests/lib/meshcore/runtime/runtime_api_map.json \
  --refresh-map
```

New public APIs are emitted as `unmapped`, which intentionally fails the normal
checker until the row is assigned a status, coverage owners, upstream evidence
or target-only rationale, and concrete tests.

Run the checker from the repository root:

```sh
python3 tests/lib/meshcore/runtime/tools/check_runtime_api_map.py \
  --repo-root . \
  --map tests/lib/meshcore/runtime/runtime_api_map.json
```

Run this host check separately from runtime Twister suites. It exits nonzero
if a public runtime API is missing coverage classification, references stale
ZTEST names, or points at missing upstream evidence files. Missing reference
files remain a validation dependency failure, not a reason to downgrade the
map's coverage classifications. This check does not build or run a Zephyr
application and does not replace the runtime behavior suites.

## Layer 3: `boundary/` Tests

`boundary/` tests verify the C library contract with hosts. They are not
upstream oracle tests.

### `boundary/public_abi/`

Verify:

- lifecycle state rules
- argument validation
- public size limits
- error codes
- ABI version expectations
- callback invocation timing visible through the public API

### `boundary/platform_contract/`

Verify:

- singleton runtime initialization uses linked `meshcore_platform_*` hooks
- platform hook symbols are direct link-time functions, not installed function
  tables

### `boundary/scheduling/`

Verify:

- host-driven event pump behavior
- timer/deadline behavior
- TX completion behavior
- no reentrant callback requirement
- no hidden thread or workqueue dependency

## Migration Area

There is no active migration-only test area after the architecture
stabilization. Future temporary migration suites are allowed only inside an
accepted phase and must record the behavior being preserved, upstream evidence
when known, target layer, reason it is not yet in the target suite, and the
deletion or promotion condition.

## Test Classification

Use these labels when reviewing or moving tests:

- `protocol target`: belongs in Layer 1
- `runtime target`: belongs in Layer 2
- `boundary target`: belongs in Layer 3
- `migration only`: temporary preservation test
- `host integration`: belongs outside generic `lib/meshcore` tests
- `delete after rewrite`: old implementation-detail coverage

## Rules For Adding Tests

Before adding a test:

1. Identify the target layer.
2. Identify upstream evidence, unless it is a boundary test.
3. Identify the public or internal target C surface.
4. State whether the test proves protocol parity, runtime behavior, or boundary
   contract.
5. Avoid asserting implementation details that are not part of the target
   layer.

## Rules For Removing Tests

Before removing a test:

1. Classify the behavior it currently protects.
2. Check whether the behavior is covered by upstream evidence.
3. Decide whether it belongs in `protocol/`, `runtime/`, `boundary/`, or host
   integration.
4. Delete it only if it is old implementation-detail coverage or has been
   replaced by a target-layer test.

## Host Integration Boundary

Generic `tests/lib/meshcore` tests should stop at the C library boundary.

Zephyr service integration, Meshbus events, ZBus, Bluetooth transport, board
configuration, storage backends, and hardware validation belong in host or
service test areas outside the generic library test architecture.

The generic library may use test doubles for HAL/PAL, but those doubles must
model the C boundary, not a concrete host implementation.
