<!-- SPDX-FileCopyrightText: 2026 FoBE Studio -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# GitHub Actions

CI validates this SDK, product firmware and host CLI without flashing hardware,
production signing or publishing releases. Candidate archives retain
`publishable: false`.

## Validation contracts

| Run | Trigger | What success establishes |
| --- | --- | --- |
| Daily CI | Pull request or main push | The checks selected for this change passed. |
| Full validation | Weekly Monday 06:00 Asia/Shanghai or manual CI | The complete device-free matrix passed for this source and resolved environment. |
| Candidate preparation | Manual | Full strict validation passed, followed by production-profile firmware packaging, EDK qualification and assembly. |
| Builder image | Weekly Monday 04:00, manual, or image/consumer-check PR | The proposed builder and its shallow dependency clones passed consumer smoke checks. Only successful default-branch scheduled/manual runs activate it. |

`ci.yml` calls `validation.yml` for daily and full validation. The candidate
workflow calls the same validation with fresh CLI requirements,
then owns the firmware packaging/assembly jobs. Its normal sysbuild checks and
production-profile package builds verify different configurations.

Configure **Required checks** as the required status after confirming its exact
hosted check name. It always runs and rejects failed/cancelled jobs, a failed
planner, unexpectedly skipped selected jobs and missing Twister instances.
Candidate success additionally requires packaging and assembly; a successful
validation gate alone does not qualify the candidate.

## Stateless selection

PRs compare with the merge base; main pushes compare with the exact
`event.before` tree. Missing/unresolvable comparisons, unknown paths and shared
contracts conservatively select the full matrix. Scheduled/manual runs always
select full validation. The planner does not query previous Actions runs.
`ci-plan` and the run summary record paths, categories, selected domains,
matched components and paths that required broader SDK coverage.

| Change | Daily checks beyond source checks |
| --- | --- |
| Ordinary Markdown/RST, including CI/CLI documentation | None: no builder resolution, west workspace, firmware or CLI build. |
| Mapped service or driver implementation/binding | Twister roots for the component and its known consumers, plus all discovered product sysbuilds; no unrelated CLI packages. |
| Boards, product configuration, common firmware helpers or unmapped firmware paths | All SDK runtime/build checks and all product sysbuilds. |
| Direct test changes | Both Twister layers use the nearest test metadata directory; no products or CLI. Deleted suites and shared fixtures without a local metadata owner fall back to the tests root. |
| Direct sample changes | Compile the nearest sample metadata directory; no runtime layer, products or CLI. Unresolved/deleted sample roots and samples without Twister metadata expand to all SDK and product checks. |
| CLI code | Python/Rust checks, three representative CLI targets and native artifact execution. |
| Host Python tools | Python tests without restoring the firmware workspace or running Rust. |
| Release tools / product inventory tooling | Python/Rust checks, representative CLI packaging and native checks, product sysbuilds. Firmware package/EDK qualification runs in candidate preparation. |
| Public headers, manifest, shared build/module files, CI implementation, license policy, unknown paths | Full matrix. |

[ci-impact.toml](ci-impact.toml) records component paths and their test/sample
roots, including consumers across components. Reverse dependencies are followed
transitively, including conditional consumers, with cycles visited only once.
For example, Clock implementation
changes include management configuration handlers, MeshCore and LLEXT tests;
the TCA8418 input driver and MFD binding select the same driver contract tests.
Multiple changes union their selections, and ancestor roots absorb nested roots.
Scenarios and platforms remain owned by `testcase.yaml` and `sample.yaml`.

The mapping is reviewed ownership data, not a complete compiler dependency graph.
Update it when adding consumers or integration fixtures. Invalid or empty mapped
test roots fail planning. Common settings/management/shell helpers and
`subsys/clock/time.c` expand to all SDK tests and samples. Source Kconfig/CMake
changes also expand because they can alter dependencies; build files inside a
directly changed test/sample stay within that metadata root. Unmapped firmware
paths actually select all SDK roots, rather than merely setting an advisory flag.
Renames include both the old and new paths. Public API changes retain the full
matrix, and scheduled/manual runs bypass narrowing.

Products remain broad for service, driver, board and application changes until
their configuration/devicetree consumer relationships are explicitly established.
A Twister impact map alone cannot prove that a product is unaffected. A new driver
without a mapped test, such as QMA6100P, therefore still receives all SDK and
product checks. Direct Twister sample/test changes do not build the CLI.

This policy follows the explicit area-to-tests and direct metadata-root selection
in [Zephyr's planner](https://github.com/zephyrproject-rtos/zephyr/blob/79f8ce2ca4d162fb61332ab17556a4ffad1d5385/scripts/ci/test_plan_v2.py),
and the cross-component path relationships in
[Nordic's CI tags](https://github.com/nrfconnect/sdk-nrf/blob/a6e29caa8e1bfcef2d38c809a3e2f732787161ba/scripts/ci/tags.yaml).
Nordic's public workflow directory does not establish its complete Twister PR
pipeline; these references describe selection mechanisms, not identical gates.

All runs perform source metadata, local documentation references, secret scanning,
repository license policy, workflow syntax, changed Python style and PR commit
style in one job with separate steps. It runs on Ubuntu with existing pinned
Python tools plus byte-verified actionlint/gitleaks, without the Zephyr image.
C/C++ patch checks use the pinned Zephyr checkout. Font checks use pinned U8g2.
Workspace checks use the same direct dependency setup as build jobs.

Daily CLI selection is Linux x86-64, Windows x86-64 and macOS ARM64. Full runs
add Linux ARM64, Windows ARM64 and macOS x86-64. Firmware-only changes do not
build these packages. Host checks use the Linux builder; CLI consumers retain a
complete workspace because current packaging records provenance across west's
active graph. Python-only host checks run the CI, host-tool and remote-tool unit suites without
that dependency setup. Release tests run with CLI/release/full checks because their
fixtures consume Zephyr metadata and native image tools.

A daily main green status does not certify unchanged components or inherit an
earlier full result. In particular, a documentation push following a failing
firmware push does not retest that firmware. Consult the full-validation result
for complete matrix status. Superseded PR runs are cancelled; main, scheduled
and candidate runs are not cancelled by newer commits.

## Source and environment identity

`west.yml` and its imports are the dependency authority. Every CI workspace
requires full commit SHAs throughout the resolved manifest graph, including
ordinary PR/main validation. Branches and tags are rejected. Root and available
imports are checked before updating; newly fetched imports are checked before
any build or scan runs.

Each job needing dependencies initializes its own west workspace and runs
`west update --narrow -o=--depth=1` directly against the checked-out source
manifest. Linux jobs also pass `--path-cache "$MESHBUS_WEST_SEED"` to reuse
the shallow repositories already in the builder image. West initializes matching
project paths from that local cache and fetches missing revisions from the
manifest URL. Missing cache entries use a shallow remote fetch. The active
manifest is never replaced with a generated manifest.
Prepare records `west-frozen.yml`, source identity and the tool inventory along
with the product matrix and Twister plans in `source-snapshot`. This artifact
contains version records and plans, not dependency source archives. Downstream
jobs update independently and compare their source identity and resolved graph
with those records. Identical pinned manifests determine identical dependency
commits regardless of cache availability.

Use a new isolated workspace for reproduction. The setup checks existing
checkouts for dirty or unrelated dependencies and rejects a different active
manifest; it must not update the shared developer workspace.

Heavy runs resolve the public GHCR `stable` channel to one immutable digest.
Manual runs can select a validated builder tag or digest using `image`. Prepare
checks builder schema 1. During image construction, `west update --narrow
-o=--depth=1` preloads dependencies at `/opt/west-workspace`; their versions are
recorded in `/opt/builder/west-seed.yml`. Consumer jobs use west's native path
cache support without copying the seed's `.west` configuration or replacing the
source manifest. The seed remains unchanged when jobs fetch newer revisions.
There is no separate Actions dependency-source cache or source archive transfer.
Tool versions/checksums remain owned by `.github/docker/`.
Weekly builder refreshes admit Ubuntu patches; failed refreshes preserve the
previous stable digest. The two weekly schedules are independent: full CI uses
the stable digest available when it starts.

The builder contains ARM/x86/RISC-V Zephyr tools, Rust, native 32-bit C/C++,
Linux ARM64 cross tools and cargo-xwin. Microsoft SDK/CRT inputs are downloaded
into CLI jobs and their actual file hashes are retained. macOS uses Apple-hosted
tooling, recorded with its Rust/Xcode/SDK identity. Linux packages require a
compatible glibc/libudev runtime; Ubuntu 24.04 is the validated baseline.
Native jobs check the actual produced CLI archives on their OS/architecture:
checksums, executable architecture, identity/help, offline package verification
and tampered-fixture rejection. They do not qualify drivers or device transports.

Prepare and heavy Linux jobs reclaim unused SDK directories only on disposable
GitHub-hosted Linux runners. These cleanup scripts reject local/self-hosted use.
Linux jobs reuse the image's shallow clones. macOS jobs perform shallow remote
fetches because they do not run in that Linux image. Neither uses an Actions
dependency-source cache; each job initializes its own `.west` state.
Cargo and ccache keys partition incompatible environments. Candidate CLI builds
reuse downloads only and compile fresh binaries. Caches never certify checks.

## Test and security evidence

Twister owns scenarios and integration platforms; CI maintains no second scenario
matrix. Prepare freezes exact runtime and compilation inventories. Runtime builds
are removed from the compilation inventory only for the same scenario, platform
and toolchain. Each nonempty layer has up to four deterministic shards. A focused
selection may need only one layer; a completely empty selection fails. Per-shard
and aggregate checks reject omissions, duplicates and unexpected skips. Runtime
requires execution evidence; build-only success is never runtime success.

Linux uses metadata-selected native/QEMU integration platforms. macOS developers
can explicitly use QEMU, without `--integration` for clock:

```sh
west twister -T meshbus/tests/subsys/clock -p qemu_x86 --inline-logs \
  -O meshbus/.scratch/clock-qemu
```

Run from `west topdir`. Simulation is contract evidence, not hardware qualification.
Product identities come from `west release matrix`; no fixed product count lives
in the workflows. Candidates use the validated Linux CLI artifact to package all
products and qualify their EDKs with existing C Snake and C++ Hello MBA samples.

Security checks have distinct purposes:

- Every run scans the source tree for secrets, plus added PR commits.
- Cargo dependency changes run advisory deltas. Shared/dependency changes also
  run west CPE advisory deltas. Both sides use the same database snapshot.
- Scheduled/manual full runs and candidates assess all current Cargo/west
  findings against an empty baseline. Cargo HIGH/CRITICAL findings remain
  blocking; an issue also present in `HEAD^` is not automatically exempted.
- West dependency CPE findings are report-only, including HIGH/CRITICAL matches.
  They do not fail the job or Required checks. Scanner execution, dependency
  resolution and report-generation errors still fail so missing evidence is
  visible. This policy applies to both delta and full scans.
- Builder refreshes retain an OS/tool vulnerability inventory. Environment
  findings are diagnostic and do not establish firmware applicability.

West reports retain module-owned versioned CPEs and identify components requiring
manual assessment. CPE matches require applicability review; an empty match list
is not proof that all compiled dependencies are vulnerability-free. Unknown
severity and existing delta findings remain visible in reports. The
`workspace-checks` artifact retains the SBOM, coverage, full scan, database
identity and delta report; the delta declares `policy: report-only`.

Ordinary diagnostics and transferred CLI packages expire after 14 days.
Candidate source snapshots, CLI/native records, firmware parts and successful
assembly expire after 90 days. Failed-run parts are diagnostic material. Daily
product builds retain logs/configuration/maps without producing firmware packages.
Source checks and Twister diagnostics use 14-day retention even on candidate runs.

## License check scope

The source-check job retains the unmodified `reuse.json` report and applies
[license-policy.toml](license-policy.toml) to reviewed file-level copyright
and licensing metadata gaps, plus explicitly named distribution-only license
texts. BSL-1.0 is retained for the external clipboard-win packaging fallback;
REUSE reports it unused because that Cargo source is outside this repository.
Only that unused-text finding is filtered, without assigning BSL to local code.
This is a repository policy check, not a claim of full
REUSE compliance. It does not add copyright claims, relicense files, or remove
existing notices. All other REUSE findings, including missing standard license
texts, invalid licenses and read errors, continue to fail the job. Font inventory
checks run for product changes and full validation.

Following Zephyr's separation of text metadata checks from binary documentation,
missing per-file metadata does not block board documentation raster images:
PNG, JPEG, WebP, GIF, BMP and ICO files under `boards/<vendor>/<board>/doc/`,
including nested directories. Text documentation and SVG remain checked, as do
runtime assets, fonts and third-party source fixtures outside those directories.
Existing image notices and annotations remain in place.

`VERSION` files under `apps/`, `samples/` and `tests/` are exempt when they contain
only the five Zephyr version fields (numeric components and an optional extension
value), blank lines and comments. The LLEXT `subsys/llext/METADATA_VERSION`
file qualifies when it contains only an integer. New files and version bumps
need no metadata header or hash update; files containing logic do not qualify.
These scope rules live in `scripts/ci/license_policy.py`.

The remaining reviewed tool/workspace configuration exemptions record exact paths,
reasons and SHA256 values in `license-policy.toml`. Changed content needs another
review before updating those entries. Do not regenerate them from scanner failures.

`license-policy.json` records applied and inactive exemptions, every remaining
finding, and the separate raw/policy results. `license-remaining-files.md` and
the three `license-*.txt` path lists make the remaining and exempted files easy to
inspect in the uploaded source-check artifact. Run the same policy locally with:

```sh
python scripts/ci/license_policy.py --output .scratch/license-review
```

## Interpreting validation results

Use each run's license report for active exemptions and remaining findings;
repository-policy compliance and raw REUSE compliance are separate results.
The font inventory check verifies catalog consistency. Distribution and font
selections must also respect the terms of the selected fonts.

All workspace checks require full SHAs throughout the manifest graph.
Candidate preparation additionally requires successful strict validation and
assembly. Local checks and pinned revisions do not
establish a successful hosted run; rerun CI on the submitted changes before
claiming completion. Hardware, production signing, notarization and public
release are outside this validation.

## First activation and reproduction

1. Commit/push the reviewed implementation through the normal repository process.
2. Run **Builder image** on the default branch. The initial GHCR package may need
   its visibility set to public by an owner. Anonymous availability is mandatory;
   until it passes the workflow does not change `stable`. Rerun after setting
   visibility. Future failures preserve the previous validated stable digest.
3. Run **CI**, inspect all layer reports, and set the verified required-check name.
   Enabling Actions alone does not create a builder or establish successful CI.
4. Run **Candidate preparation** for strict checks, packaging and assembly.
   Download the snapshot and the two independent
   release-train artifacts. Do not infer publishability from artifact existence.

For local static/unit checks from the repository:

```sh
python -m unittest discover -s scripts/ci/tests -v
python -m unittest discover -s scripts/release/tests -q
actionlint
ruff check --select E4,E7,E9,F scripts/ci
```

For image construction (Docker x86-64; ARM Macs use emulation):

```sh
docker buildx build --platform linux/amd64 --load --build-context source=. \
  -t meshbus-builder:test .github/docker
```

For source reproduction, create `<new-workspace>/meshbus` at the recorded source
SHA, copy the downloaded snapshot to `<new-workspace>/snapshot`, and run
`python meshbus/scripts/ci/workspace.py --workspace <new-workspace> --verify
<new-workspace>/snapshot` in the recorded image. Then run the appropriate
`scripts/ci/run.py` layer. Keep outputs outside the source checkout.
No remote Actions runs, successful image publication, or complete candidate set
are implied by local unit/static checks.
