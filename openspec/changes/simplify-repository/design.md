<!-- SPDX-FileCopyrightText: 2026 FoBE Studio -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Repository convergence

## Context

See proposal.md. The initial tree had 76 test scenarios and 30 sample scenarios.
Clock implementation selection reaches roots containing 58 test scenarios;
Indicator reaches 24. LLEXT has two identical 13-case configurations. The
existing runtime/compile deduplication and strict inventory gates already work.
The four APP profiles differ in hardware, boot, memory and enabled services.

## Goals / Non-Goals

Reduce repeated work and implementation while preserving observable behavior.
This is a bounded pass over repository-owned domains, not proof that every
possible simplification has been exhausted. Do not add a testing framework,
coverage-percentage gate, dependency graph generator or mandatory document for
every small fix. Do not remove compatibility or security checks to reduce counts.

## Decisions

1. Keep explicit component-to-test roots in ci-impact.toml and remove the second,
   recursive consumer graph. A root list includes reviewed integration consumers;
   source changes in a consumer independently select its own mapping. Public
   module headers use the same mapping; unknown/shared firmware paths expand SDK
   coverage. License metadata uses source checks; CI Python uses host tests.
   The root LICENSE and LICENSES/Apache-2.0.txt are CLI inputs and retain CLI
   selection. Power explicitly selects Indicator audio/feedback test roots,
   which exercise its fuel-gauge event contract.
2. Use existing `shuffle` and `performance` tags. Freeze the complete candidate
   inventory, then retain extended scenarios only for full validation, directly
   changed test roots or settings performance work. Apply the same policy before
   runtime/compile deduplication; loaded shards need no new workflow profile.
   Preserve device-test compilation and all four products: simple source-text
   Kconfig guesses cannot establish effective product dependencies.
3. Delete the duplicate LLEXT edk_provenance scenario; retain its assertions in
   the ordinary app scenario. Distinct heap/backend/lifecycle configurations stay.
4. Reuse Desktop's existing input predicates instead of adding a generic event
   abstraction, including the same three predicates in the MeshCore UI. Keep
   application routing/lifecycle logic separate. Remove only
   Clock private null checks whose call sites establish non-null arguments;
   preserve public input validation, numeric bounds and system-call failures.
5. Document one focused TDD loop and an explicit test/design admission rule.
   Existing regressions validate pure refactors; new regression tests target
   changed planner behavior rather than mirroring implementation details.
   Ordinary product-build examples use pristine auto; switching incompatible
   configurations or signing modes still uses a separate build directory.

## Repository review coverage

| Area | Finding and disposition |
| --- | --- |
| CI and test inventory | Remove recursive selection and identical scenarios; preserve strict result accounting. |
| Public headers and services | Keep API/ABI, wire/persistence values and external error contracts; simplify proven internal redundancy. |
| Desktop | Repeated click/long-press/navigation predicates have an existing shared implementation. |
| Drivers and power | Similar runtime-PM sequences cross independent ownership boundaries; keep local implementations and device failure checks. |
| MeshCore, management and Bluetooth | Large files contain protocol variants, security checks and asynchronous ownership; preserve compatibility and bounded test seams. |
| CLI and Python host/release tools | Archive validation, image bounds, target identity and signing checks protect external inputs; no consolidation into a generic validation framework. |
| Product boards and samples | Four distinct profiles and sample backends remain supported; no speculative configuration merging. |
| Documentation and OpenSpec | Consolidate engineering choices and remove incentives for exhaustive defensive tests. |
| Generated assets and external modules | Preserve generated sources, attribution and independent ownership. |

## Risks / Trade-offs

- Explicit ownership lists need maintenance: validate roots and retain scheduled
  full coverage, unknown-path fallback and mixed-change union tests.
- Extended scenarios run less often: direct edits and full runs retain them;
  ordinary correctness regressions remain in routine validation.
- Local workspace uses the enterprise manifest: local test results are distinct
  from the SDK's hosted frozen-manifest qualification.
- Desktop helper reuse must not change optional-feature linking: verify the
  shared CMake owner and run affected integration tests and a product build.

## Migration Plan

Apply and validate in local functional steps within this change. Keep prior
OpenSpec adoption edits. No dependency update, commit or hosted workflow is
performed. Review actual results before archiving; unexecuted hosted/hardware
qualification is recorded separately from local acceptance.
