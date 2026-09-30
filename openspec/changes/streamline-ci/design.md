<!-- SPDX-FileCopyrightText: 2026 FoBE Studio -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Design

## Context

See proposal.md. Existing source snapshots record the complete pinned graph;
CLI packaging currently resolves it again in every job. Run and Build inventories
are already disjoint and Candidate reuse depends on their named shard records.

## Goals / Non-Goals

Reduce preparation and ordinary validation work while preserving source identity,
full release profiles, actual native package checks and exact Twister accounting.
Do not alter dependency pins, publish images/releases, or weaken candidate gates.

## Decisions

1. Record complete clean provenance once during prepare. CLI jobs validate the
   source/manifest receipt, restore only its pinned protobuf Git checkout, and
   bind packaging to that receipt and the actual schema revision. Keep the Linux
   tool image for its existing cross linkers; no separate image publication is
   needed before the combined CI run. Full workspace tests retain their graph.
2. Check free disk space before deleting unused hosted SDKs, stopping as soon as
   the required headroom exists. Preserve hosted-runner guards and explicit logs.
3. Add an optimized CI Cargo profile without LTO; full/candidate runs use release.
   Build both macOS targets on ARM runners, run x86 tests under Rosetta, and keep
   distributed-package verification on a native Intel runner. Cache keys include
   profile and use source-input identity rather than unrelated firmware revisions.
4. Keep Run/Build plans and names compatible with baseline reuse. Use one worker
   per bounded batch, capped at four per layer. Small selections avoid redundant
   setup; full validation keeps current parallelism until comparative evidence
   supports changing it. Timing hints may balance work but cannot change selection.
5. Classify known CLI/release CI helpers by consumer; planner, workspace, common
   validation and unknown changes remain full integration checks. Build board
   ownership from the existing hardware/profile inventory and local include
   closure. Board-only changes select those platforms; product-only changes
   select their product consumers. Shared implementation/configuration retains
   conservative coverage. Never infer Kconfig absence from one handwritten file.

## Risks / Trade-offs

- Cross-build success does not prove native execution: preserve native jobs.
- Smaller free-space reserves risk disk exhaustion: use explicit conservative
  per-job budgets and report free space before/after cleanup.
- Include analysis cannot prove arbitrary CMake/Kconfig semantics: unknown or
  shared inputs retain broad coverage; profile inventory remains authoritative.
- Cache hits and fewer jobs do not guarantee faster elapsed time: compare hosted
  step timings and separately report cold/warm effects and untested performance.

## Migration Plan

Implement and verify all four stages locally, commit/push once, inspect the full
CI selected by changes to shared validation, and repair any actual failures.
A failed hosted acceptance remains open in tasks.md; no release is dispatched.
