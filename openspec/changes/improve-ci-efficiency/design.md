# Design

## Context

The approved conversation plan is the implementation scope. Existing complete
changes retain their historical evidence. This delta supersedes the fresh-build
requirement in ci-alpha-release and refines streamline-ci/scope-license-checks.

## Goals / Non-Goals

Balance elapsed time with runner use while preserving exact selected coverage,
configuration identity, release integrity and website work in progress. Keep
dependency versions, device operations and public publication outside this task.

## Decisions

1. Keep products.json as the unique packaging inventory. Expand product_requests
   into product-builds.json rows keyed by product and default/dev/prod profile.
   Full CI requires every default product and unions explicitly changed profiles;
   it does not multiply every full build into three profiles. Known application
   inputs do not select SDK roots. Review local clock/display/input/telemetry
   build paths explicitly; mgmt/shell implementation retains all subsystem roots.
2. Ignore rules and licensing prose stay incremental. Sidecars select their
   counterpart; nested REUSE selects its subtree, including deleted/moved context.
   Root REUSE compares parsed TOML after removing only complete annotations whose
   paths provably stay under web/. Other root changes, global texts, policy,
   checker or unavailable bases select full. Retain current metadata exceptions.
3. Gate Node/npm/OpenSpec on relevant inputs or full checks. Cache pinned Python
   downloads and tool archives; verify tool archive SHA-256 on every restoration.
4. Shards schema 2 contains id/name/tasks; tasks retain layer/shard reports.
   Combine the two nonempty layers only when each has one shard and total active
   instances is at most 12. Run every task, fail the job if any task fails, and
   preserve final identity accounting. Add run.py twister --job and product
   --product-profile without changing CLI --profile.
5. Preserve min(4, ceil(count/12)) shards per layer. Use median build+execution
   time from at most three same-Builder successful main CI artifacts found among
   ten recent runs. Longest-first placement uses deterministic ties; use the
   better predicted maximum of this and round-robin. Missing weights use layer
   medians; wholly missing/invalid history uses round-robin. History only groups
   instances. Bound JSON inputs and reject invalid timings. Publish ci-timings
   after coverage passes, with source, run/attempt and Builder identity.
6. A separate manual benchmark accepts full/clock-small and legacy/balanced.
   Each dispatch runs one combination. Clock-small selects clock tests plus its
   sample and asserts both layers and at most 12 instances. Pin SHA, Builder,
   history and inventory; benchmark plans never qualify as CI baselines. Ordinary
   CI/Candidate reject benchmark inputs, including legacy scheduling.
7. Baseline schema 2 binds normalized products, profiles, actual Twister tasks
   and successful jobs to exact source/manifest/frozen graph/Builder/run/attempt.
   Candidate skips default product jobs only with complete matching evidence,
   keeps its inventory and builds prod+META/EDK once. Historical schema 1 remains
   readable but cannot authorize product skipping.
8. Candidate completion writes attempt-specific qualification with exact
   artifact IDs/digests. Promotion selects a trusted successful main Candidate
   for the same source; it pins the Candidate Builder, not moving stable. Missing,
   expired or older unqualified candidates fall back to fresh preparation;
   explicit run conflicts, API errors or corrupt content fail. Stage rechecks
   materials, bytes and current vulnerabilities. Public manifests distinguish
   Candidate identity from publication identity and remain backward readable.
9. Manual Alpha dispatch is staging-only, optionally selecting a Candidate run.
   Only tag-push publication has write permission. Staging needs no real tag.

## Risks / Trade-offs

- Input narrowing can miss consumers: exact reviewed mappings, union tests and
  conservative unresolved fallback preserve coverage.
- Cached timings vary: freeze comparison inputs, record cold/warm runs, and keep
  hints separate from qualification. Mixed tasks preserve independent reports.
- Artifact reuse can mix attempts: receipts pin IDs, bytes and qualifying attempt;
  missing evidence cannot satisfy an expected gate.
- Website hunks share files: preserve the initial patch and validate the exact
  staged tree in an isolated checkout before committing.

## Migration Plan

Land four functional commits (license/source tooling, selection, scheduling,
release reuse), then push main and qualify the exact implementation SHA. Complete
full CI, Candidate and explicit/automatic staging dry-runs. For both benchmark
cases run a cold preparation and two warm samples per scheduler, alternating
schedulers. Small-case runner minutes must decrease; both cases' Twister wall
time may regress by at most max(60 seconds, 10%). Coverage must match exactly.
Fix failures with additional commits, retain unresolved acceptance as open, and
record actual run links and measurements before the final evidence commit.
