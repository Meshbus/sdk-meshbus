# GitHub Actions

Validation CI checks this SDK, product firmware and host CLI without flashing
hardware or production signing. Candidate archives retain `publishable: false`.
The separate Alpha workflow publishes only the reviewed allowlisted downloads.

## Validation contracts

| Run | Trigger | What success establishes |
| --- | --- | --- |
| Daily CI | Pull request or main push | The checks selected for this change passed. |
| Full validation | Weekly Monday 06:00 Asia/Shanghai or manual CI | The complete device-free matrix passed for this source and resolved environment. |
| Candidate preparation | Manual or reusable Alpha call | Matching successful full CI Twister evidence, fresh strict validation, production-profile firmware packaging, EDK qualification and assembly. |
| Alpha release | Push of `v*-alpha.*` tag | The complete candidate pipeline and automatic material checks passed; uploaded assets and anonymous public downloads match the retained checksums. |
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

Candidate preparation reuses Twister from a successful full `ci.yml` run on main
for the exact source SHA and immutable Builder digest. The gate checks the root
manifest, both Twister layers, every planned shard and `Required checks`; the
new resolved dependency graph must also match. It retains `twister-baseline.json`
in the plan/source artifacts and includes the baseline in the public release
manifest. It grants no reuse to narrowed, failed, expired or conflicting runs.
If the gate fails, complete main CI first; use manual **CI** to obtain a full run
on the desired commit/image when ordinary impact selection was narrower.
Candidate and tag runs execute no duplicate Twister jobs. Their other strict
checks, six CLI targets, product builds, packaging and EDK qualification remain
fresh. Normal PR/main planning does not query past runs.

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
| OpenSpec artifacts, generated skills and root npm tool configuration | Source checks validate OpenSpec; no firmware or Rust builds. |
| Mapped service implementation/header or driver implementation/binding | Declared Twister roots and integration consumers, plus product sysbuilds; no unrelated CLI packages. |
| Board DTS/defconfig and product profiles | Known board integration builds and affected product sysbuilds, including local include consumers. Unresolved ownership expands coverage. |
| Product VERSION | Product sysbuilds; no unrelated SDK or CLI builds. |
| Common firmware helpers or unmapped firmware paths | All SDK runtime/build checks and all product sysbuilds. |
| Direct test changes | Both Twister layers use the nearest test metadata directory; no products or CLI. Deleted suites and shared fixtures without a local metadata owner fall back to the tests root. |
| Direct sample changes | Compile the nearest sample metadata directory; no runtime layer, products or CLI. Unresolved/deleted sample roots and samples without Twister metadata expand to all SDK and product checks. |
| CLI code | Python/Rust checks, three representative CLI targets and native artifact execution. |
| Host and ordinary CI Python tools | Python tests without restoring the firmware workspace or running Rust. |
| Root `LICENSE` and `LICENSES/Apache-2.0.txt` | CLI/Rust checks and representative CLI targets because EDK validation and export consume these texts; no firmware builds. |
| Other license texts and metadata | Source license/metadata checks; no firmware or CLI builds. |
| Release tools / product inventory tooling | Python/Rust checks, representative CLI packaging and native checks, product sysbuilds. Firmware package/EDK qualification runs in candidate preparation. |
| CLI cache/action helpers | CLI/host validation. |
| Candidate/Alpha workflows and publication helpers | Release/CLI/product validation. |
| Manifest, shared build/module files, validation workflow, execution/selection helpers, unknown paths | Full matrix. |

[ci-impact.toml](ci-impact.toml) records component paths and their test/sample
roots, including the integration consumers relevant to each component. These
are explicit selections: selecting a consumer's tests does not recursively
select the consumer as though its source changed. Clock implementation/header
changes include management configuration handlers, MeshCore and LLEXT tests;
the TCA8418 input driver and MFD binding select the same driver contract tests.
Power implementation/header changes include the Indicator audio and feedback
test roots that consume its fuel-gauge events.
Multiple changes union their selections, and ancestor roots absorb nested roots.
Scenarios and platforms remain owned by `testcase.yaml` and `sample.yaml`.

The mapping is reviewed ownership data, not a complete compiler dependency graph.
Update it when adding consumers or integration fixtures. Invalid or empty mapped
test roots fail planning. Common settings/management/shell helpers and
`subsys/clock/time.c` expand to all SDK tests and samples. Source Kconfig/CMake
changes also expand because they can alter dependencies; build files inside a
directly changed test/sample stay within that metadata root. Unmapped firmware
paths actually select all SDK roots, rather than merely setting an advisory flag.
Renames include both the old and new paths. Mapped public headers share their
component scope; unmapped headers expand SDK/product checks. Scheduled/manual
runs bypass narrowing and include every supported configuration.

Board DTS, overlays and defconfig inputs use existing board/profile directories
as owners. Local preprocessor includes extend selection to their consumers,
including deleted includes still referenced by retained sources. Board coverage
uses the affected boards' declared integration platforms. Product profile changes
select their product; `VERSION` selects all products. The final product list is
filtered from `west release matrix`, and an unknown requested owner fails.
Mixed component changes retain their unrestricted platforms alongside board builds.
Arbitrary Kconfig/CMake changes, unknown owners and shared implementation still
expand conservatively. This does not infer disabled services from handwritten
Kconfig fragments. Service/driver changes continue to build all products until
complete product configuration dependencies establish a narrower safe scope.

This policy follows the explicit area-to-tests and direct metadata-root selection
in [Zephyr's planner](https://github.com/zephyrproject-rtos/zephyr/blob/79f8ce2ca4d162fb61332ab17556a4ffad1d5385/scripts/ci/test_plan_v2.py),
and the cross-component path relationships in
[Nordic's CI tags](https://github.com/nrfconnect/sdk-nrf/blob/a6e29caa8e1bfcef2d38c809a3e2f732787161ba/scripts/ci/tags.yaml).
Nordic's public workflow directory does not establish its complete Twister PR
pipeline; these references describe selection mechanisms, not identical gates.

All runs perform source metadata, local documentation references, secret scanning,
repository license policy, workflow syntax, changed Python style and PR commit
style in one job with separate steps. OpenSpec checks validate specs/changes and
unfinished tasks in archives. These are structural checks, not proof of runtime
behavior, TDD execution or review approval. The job uses Node.js 24 with
`npm ci --ignore-scripts`, pinned Python tools and byte-verified actionlint/gitleaks,
without the Zephyr image.
C/C++ patch checks use the pinned Zephyr checkout. Font checks use pinned U8g2.
Workspace checks use the same direct dependency setup as build jobs.

Daily CLI selection is Linux x86-64, Windows x86-64 and macOS ARM64. Full runs
add Linux ARM64, Windows ARM64 and macOS x86-64. Firmware-only changes do not
build these packages. Host checks use the Linux builder. Prepare records `cli-source.json` from its
verified complete graph. CLI jobs restore only the pinned `meshbus-protobufs`
checkout, validating source SHA, root manifest, frozen graph, project URL, schema
revision and dirty state. Packaging records the inherited source receipt and its
locally verified SDK/schema inputs. Other dependency checkouts are not needed.
Daily CLI builds use the optimized `ci` Cargo profile (opt-level 1, 16 codegen
units, LTO off); full/candidate builds use `release`. Non-development packaging
rejects the CI profile. Both macOS targets compile on Apple Silicon. Targeted
Cargo tests execute x86 code under preinstalled Rosetta; the distributed Intel
archive is still checked on an Intel runner. Python-only host checks run the CI, host-tool and remote-tool unit suites without
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

Each firmware/host job needing the full graph initializes its own west workspace and runs
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

Prepare and heavy Linux jobs check free space before reclaiming unused SDKs
on disposable GitHub-hosted Linux runners, stopping once enough space exists.
The budget is 24 GiB before pulling the image, 12 GiB for firmware work and 8 GiB
for CLI jobs after image initialization. Insufficient space fails explicitly.
These cleanup scripts reject local/self-hosted use. Full Linux workspaces reuse
the image's shallow clones; CLI jobs fetch only schemas and need no `.west` state.
The existing Linux tool image supplies cross linkers without a new image rollout.
Cargo and ccache keys partition incompatible environments. Cargo keys include
profile, toolchain/SDK identity, Cargo manifest/lock and target; unrelated frozen
firmware graph changes do not invalidate compiled CLI dependencies. Cargo still
tracks schema changes. Candidate CLI builds reuse downloads only and compile
fresh binaries. Caches never certify checks.

## Test and security evidence

Twister owns scenarios and integration platforms; CI maintains no second scenario
matrix. Daily runs omit scenarios tagged `shuffle` or `performance` from indirect
component selections. Directly changed test roots retain their extended scenarios;
settings implementation changes retain the settings performance root. Full runs
include all variants. Filtering happens before Prepare freezes the runtime and
compilation inventories; the raw discovery reports remain in its task output.
Runtime builds
are removed from the compilation inventory only for the same scenario, platform
and toolchain. Each nonempty layer uses one deterministic shard per 12 instances, capped at
four. This reduces setup for small selections while preserving full-run parallelism. A focused
selection may need only one layer; a completely empty selection fails. Per-shard
and aggregate checks reject omissions, duplicates and unexpected skips. Runtime
requires execution evidence; build-only success is never runtime success.

Linux uses metadata-selected native/QEMU integration platforms. macOS developers
can explicitly use QEMU, without `--integration` for clock:

```sh
west twister -T meshbus/tests/subsys/clock -p qemu_x86 --inline-logs \
  -O 'twister-out/<task>-clock-qemu'
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

All Markdown (`.md`, case-insensitive) documents are exempt from missing per-file
copyright/license metadata, including hidden directories and OpenSpec archives.
Do not add SPDX declaration headers to them. New documents and content changes
need no hash entry or approval; the report records `markdown-documentation`
exemptions. Existing central REUSE metadata and third-party notices are retained.
Invalid/missing license texts, read errors and all other failure classes still
block, including for Markdown. See [per-file rules](../LICENSING.md#per-file-metadata).

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
license_tmp="$(mktemp -d "${TMPDIR:-/tmp}/meshbus-license.XXXXXX")"
python scripts/ci/license_policy.py --output "$license_tmp"
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
claiming hosted CI or candidate validation complete. Hardware, production signing,
notarization and public release are outside this validation.

## Alpha operation and recovery

The [Alpha workflow](workflows/alpha-release.yml) accepts canonical tags such as
`v1.0.0-alpha.1`. Commit `apps/meshbus/VERSION` with `EXTRAVERSION = alpha.1`
before tagging that exact source revision. The workflow never stamps a dirty
in-run version or uploads a locally built UF2. All called workflows check out
the triggering source; dependency snapshots and package/EDK provenance must
agree. Merge reviewed workflow changes to the default branch before the first
tag, so `GITHUB_TOKEN` publication does not require additional workflow rights.
Git operations and public publication require the repository's explicit
authorization; the examples here do not grant it.

Complete full **CI** on the committed Alpha source before running manual
**Candidate preparation**, using the same Builder digest. Candidate preparation
checks every firmware/EDK and CLI license material and native proof with `--evidence-only`, retaining
`alpha-license-evidence` and its `license-evidence.json` alongside the verified
candidate and qualification evidence. It does not generate public Alpha assets.
Permission decisions and any new or changed alternatives/exceptions are reviewed
before merging under [contribution review](../CONTRIBUTING.md#maintainer-review-and-ci).
There is no per-Alpha approval file or release-time approval digest check.
See [distribution evidence](../DISTRIBUTION.md#distribution-material-evidence).

The job order is preflight -> complete reusable Candidate preparation -> public
staging -> draft upload/verification -> publication -> anonymous verification.
The candidate pipeline resolves the builder once and retains full strict
validation, all four products and six native-checked CLI packages. Every
candidate job must succeed; missing, failed, cancelled and unexpected skipped
jobs fail the Candidate required checks. Ordinary PR/main selection is unchanged.

Preflight checks the remote tag and committed VERSION. An existing public
Prerelease is verified against its original manifest, inventory and downloads,
then completes without building or changing its assets. Fresh staging exports
the complete product and CLI public files and retains `alpha-evidence` plus `verified-alpha-assets`
for 90 days. Only the publication job has `contents: write`; all validation,
build and export jobs have read access. Same-tag runs are serialized without
cancelling an active upload. No personal token or signing key is required.

Missing or corrupt required license materials still fail staging. Successful
material checks retain `license-evidence.json`; its input digest is provenance,
not legal approval. Correct missing materials through a contribution before
creating the publication tag; preserve existing tags. Failed or incomplete
uploads retain an owned draft. **Rerun only the failed publication
job in its original Actions run**, using its original retained artifacts.
Matching uploaded assets remain untouched; missing assets are uploaded. A full
rerun detects the draft before rebuilding and fails with recovery instructions.
Conflicting ownership, source, inventory or existing asset bytes fail without
clobber, tag movement or deletion. After evidence expires, do not regenerate
bytes under an existing version; use a newly reviewed Alpha version.

Authenticated draft download checks precede `draft=false`, `prerelease=true`
and `make_latest=false`. Anonymous download checks follow publication. The
Actions summary distinguishes a retained draft from a failure after the Release
became public. Inspect the actual state after a network interruption: publishing
may have succeeded even if its response was lost. Preserve public bytes and use
the next Alpha number for corrections. Successful CI publication supplies no
physical-device or GA qualification evidence.

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

### Complete Alpha download matrix

After the immutable R1 alpha.1 pilot, Alpha staging publishes all targets from
`west release matrix`, each native APP image, firmware archive, public SBOM and
applicable EDK, plus all six native-validated CLI archives. Both Candidate
assembly and Alpha staging download `cli-*` and `native-validation-*` separately
from firmware assembly and pass `--cli` and `--native` to `alpha.py`. Missing or
conflicting product, CLI, native or material evidence fails the complete release.
CLI archives keep their Cargo version; no host signing or notarization is implied.
Manifest schema 2 records the per-product and CLI matrix. Schema 1 verification
remains available for original alpha.1 downloads. Advance firmware VERSION and
the Alpha tag together; do not add downloads to an existing public version.
