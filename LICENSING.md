# Licensing

FoBE Studio-owned SDK, product firmware, CLI, tools, examples and documentation
use [Apache-2.0](LICENSE). Copyright attribution is **FoBE Studio**. Original
third-party copyright and license notices remain applicable to their portions.

Preserve upstream terms and attribution. Dependency admission happens in
contribution review; release CI checks the materials actually distributed.
[CONTRIBUTING.md](CONTRIBUTING.md) owns the review workflow and
[DISTRIBUTION.md](DISTRIBUTION.md) owns archive layout and publication procedure.

## Per-file metadata

| Files | Repository metadata rule |
| --- | --- |
| Entire root `web/` tree | Temporarily excluded from repository SPDX scans, including complete audits. Retain existing attribution and third-party notices. |
| Markdown (`.md`, case-insensitive), including README, agent guidance, PR templates and OpenSpec artifacts/archives | No per-file SPDX declarations. Project-authored documentation uses the repository license; preserve original third-party notices. |
| Source and headers, scripts, build files, device/configuration files, test/sample metadata and CI workflows | Keep SPDX copyright and license metadata using native comments where supported, or provenance-backed `REUSE.toml` annotations. |
| Files whose format cannot carry comments | Use suitable existing `REUSE.toml` metadata; do not insert comments that break the format. |
| Board documentation raster images and plain version values | No per-file metadata required within the existing scoped policy. |
| Reviewed tool/workspace configuration exceptions | Apply only the exact paths and content hashes in the existing license policy; new or changed files do not automatically qualify. |

Outside `web/`, the [license-check scope](.github/CI.md#license-check-scope) waives
only missing Markdown copyright/license metadata, without a content hash or approval entry.
It preserves other scanner failures and checks on non-Markdown files. Existing
central REUSE attribution and third-party license/permission texts remain
applicable; this policy neither changes those grants nor claims full REUSE
compliance. Generated skills retain their upstream content and notices.

Daily checks inspect changed files, including local untracked inputs. Complete
audits run explicitly, during scheduled/manual complete CI, and when shared
licensing inputs change. Ignored untracked dependencies and generated output
are excluded before scanning. An incremental pass covers the reported selection;
release checks continue to verify the materials actually distributed.

## Compiled dependency admission

Meshbus must remain usable in proprietary products. Third-party dependencies
incorporated into compiled outputs must not require GPLv3 licensing of those
outputs. This admission policy is separate from the repository's own license
and does not itself grant rights to third-party material.

### Scope and admission

- Apply the rule to direct and transitive dependencies in firmware, libraries,
  the CLI, and maintained MBA samples. Include compiled or linked source,
  runtime libraries, header implementations, generated code and descriptors,
  and embedded data such as fonts. An optional component is in scope when its
  feature is enabled; a disabled default is not approval to enable it later.
- Reject third-party GPL-3.0-only and GPL-3.0-or-later code without an applicable,
  documented alternative grant or exception. AGPLv3 is subject to the same
  restriction. Other copyleft licenses, noncommercial terms, and unknown grants
  still need their own review; absence of GPLv3 is not a commercial-use grant.
- A dual license may be used through an identified non-GPL alternative. Record
  the selected license and retain its copyright and permission text. An `OR`
  expression permits a choice; an `AND` expression requires both sets of terms.
- A linking or runtime exception is acceptable only when its actual text
  permits the intended proprietary combination and the build satisfies its
  conditions. Record the component, version, exception, and affected outputs;
  a font's document-embedding exception is not firmware-linking permission.
- Independently executed build, analysis, generation, and test tools are outside
  this compiled-output restriction when their code is not incorporated into the
  output. Merely using a GPL compiler does not make its generated output GPL.
  Copied templates, runtime objects, and generated code containing licensed
  material are evaluated separately. Unselected source and standalone license
  texts do not count as compiled dependencies.
- Meshbus-owned code, including separately maintained first-party dependencies,
  keeps its declared public license. First-party classification requires actual
  rights, not a repository name. Consumers must comply with each dependency's
  declared license; this policy neither replaces it nor covers third-party
  portions inside a first-party repository.

### Contribution review and distribution checks

For additions, updates or newly enabled features, follow
[contribution review](CONTRIBUTING.md#rights-and-third-party-material). Review
actual linked, generated and exported inputs across supported targets. Reuse
existing decisions when terms and usage are unchanged; record new selections
and exceptions below. Resolve unknown grants before merging. `NOASSERTION`
alone is not unknown permission, and a SBOM or GPL text search is not legal
clearance. No per-Alpha approval file or separate approval registry is required.

Release CI checks notices, selected fonts, runtime materials and integrity;
source delivery obligations remain part of contribution review. Font provenance
lives in the [font inventory](docs/licensing/fonts.md).

## Third-party exceptions

`LICENSES/` holds standard SPDX texts. Original source declarations remain
with their files; `REUSE.toml` supplies missing machine-readable metadata.
Component-specific copyright and complete permission notices live beside their
sources. A standard license identifier does not replace those notices.
The CI policy documents BSL-1.0 as a distribution-only standard text for an
external Cargo dependency; its raw REUSE unused-text finding remains visible.

| Component | Applicable files | Terms | Source and notice | Distribution scope |
| --- | --- | --- | --- | --- |
| Inherited board support, LoRa samples and Zephyr configuration | Original file headers | Apache-2.0, original upstream authors | File headers and [standard text](LICENSES/Apache-2.0.txt) | Selected source/build inputs |
| ZUI and English predictive dictionary | External `sdk-zui` module | Apache-2.0 for ZUI; ISC, Zeke Sikelianos for dictionary | Module `LICENSING.md` and `src/dicts/LICENSE` | Exported public headers; firmware using ZUI and the built-in dictionary |
| CLI bsdiff and detools-derived implementation | `scripts/meshbus/` | BSD-2-Clause, original authors | [CLI NOTICE](scripts/meshbus/NOTICE) | CLI distributions |
| CLI clipboard-win dependency | Target-specific Cargo graph | BSL-1.0 | Upstream package; [standard fallback](LICENSES/BSL-1.0.txt) | Selected CLI targets |
| CLI protoc-bin-vendored tool | Cargo build dependency | MIT, Stepan Koltsov | Supplemental full notice in [CLI NOTICE](scripts/meshbus/NOTICE) | Build tool; not bundled as a CLI runtime |
| OpenSpec generated Codex workflows | `.agents/skills/openspec-*/SKILL.md` | MIT, OpenSpec Contributors | [Retained upstream notice](.agents/skills/OPENSPEC-LICENSE); generated by `@fission-ai/openspec` 1.13.2 | Development workflow files; not compiled into firmware or CLI |

The root npm package pins OpenSpec and its transitive development tools in
`package-lock.json`. Generated skills retain upstream MIT ownership through
`REUSE.toml`; project-authored OpenSpec configuration and change documents use
Apache-2.0. Installed npm packages retain their own notices in `node_modules/`
and are not vendored into the source tree or product distributions.

## Desktop segment fonts

`subsys/desktop/assets/assets_fonts.h` contains the project-designed and
generated `F_segment_46` and `F_segment_24` arrays. These are FoBE Studio-owned
Apache-2.0 content, as recorded in `REUSE.toml`.

## External projects and fonts

External west projects retain their own source headers, licensing guides and
complete terms. Their resolved versions are recorded by the manifest and build
provenance. The SDK's default does not relicense dependency contents.

- [sdk-u8g2](https://github.com/Meshbus/sdk-u8g2) owns the U8g2 implementation,
  three public display headers and fonts. Its core retains BSD-2-Clause;
  FoBE Studio integration uses Apache-2.0. Font terms are independent. See
  [Meshbus font selections](docs/licensing/fonts.md) and the dependency's
  `LICENSING.md`, `fonts/catalog.json`, `fonts/sources.json` and `fonts/notices/`.
- `meshbus-protobufs` owns its schemas, options and other original content under
  Apache-2.0. Its generators and runtimes have their own terms. Preserve its
  license and notices for schema content incorporated into generated outputs.
- `sdk-meshcore` and `sdk-arduboy` retain their own `LICENSING.md`, `LICENSES/`
  and source records. Use the materials from the resolved dependency revision.

## External dependency license selections

These recorded cases apply the
[compiled-dependency policy](#compiled-dependency-admission).
They are not a complete inventory of every target. Recheck resolved versions
and delivered files when preparing a distribution.

- The CLI's `unescaper` 0.1.10 dependency uses the MIT option of its
  `MIT OR GPL-3.0-only` license. It is reached through `serialport` on Linux
  non-musl targets. Preserve its MIT notice when packaging that target.
- GCC runtime material bearing `GPL-3.0-or-later WITH GCC-exception-3.1`,
  including relevant `libgcc`, `libstdc++` and Zephyr GCOV material, is evaluated
  under the [GCC Runtime Library Exception](https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html).
  Keep the exception with its license and review the actual compilation and
  distribution inputs. This selection does not cover unrelated GPL material.
- Mbed TLS and TF-PSA-Crypto source offering `Apache-2.0 OR GPL-2.0-or-later`
  uses the Apache-2.0 option. Their resolved `LICENSE` files document that choice;
  retain applicable copyright and notices, including separately required terms
  on files not covered by that alternative.
- Heatshrink's `LICENSE` grants ISC terms; retain Scott Vokes' copyright and
  permission notice. Nanopb's `LICENSE.txt` grants Zlib terms; preserve origin,
  mark altered source versions and retain its notice in source distributions.
  Missing SBOM declarations do not replace these source grants.
- LoRa Basics Modem's Semtech portions use the BSD-3-Clause-Clear terms in
  `LICENSE.txt`. Retain its binary-distribution copyright, conditions and
  disclaimer; the license grants no patent rights. `LICENSES.txt` and nested
  source terms govern its third-party portions and must remain applicable.

These migration entries fill known declaration/choice gaps using the existing
dependency terms. They do not approve all files in those repositories or
replace review of later changes. Selected GCC and Picolibc/Newlib runtime texts
remain packaged from the actual SDK; their collection is material evidence,
not a new compiler-wide permission grant.

## Source provenance

The FoBE source baseline is commit
`748e79923cd091e0c99e9701af19fcc598811d8a`. This identifies the original source
selection; current implementation and ownership are recorded in this repository.

The `support/openocd.cfg` scripts for `boards/fobe/devkit_nrf54l15/` and
`boards/fobe/mesh_probe_r2/` retain the Apache-2.0 notice from Zephyr's
`boards/seeed/xiao_nrf54l15/support/openocd.cfg` at commit
`53374c62579468908f3b7534a58d4c641f20f0f5`. Their unchanged upstream bodies retain
upstream ownership.

`REUSE.toml` records FoBE Studio attribution for these board files alongside
the retained upstream declarations. Upstream portions keep their own provenance
and ownership.

## Distribution notices and evidence

Each firmware, CLI and EDK archive contains one `NOTICE.txt`. Identical text is
stored once with its component/path associations; original copyright, complete
terms and applicable exceptions remain. This is an aggregation of existing
notices, not a change of license or a claim of distribution rights.

Firmware selects components from private SPDX/build inputs and fonts from the
final ELF. Only selected font terms are included; restricted/unreviewed fonts
still fail. Installed GCC runtime exceptions and Picolibc/Newlib notices remain
required. EDK notices cover exported headers, whose original declarations remain
in place. Full applicable standard texts occur once in NOTICE.txt instead of
duplicating every header banner. Apache/GPL dual-licensed headers use Apache-2.0;
BSD-2-Clause/CC0 dual-licensed headers use BSD-2-Clause. AND expressions retain
both texts. These choices do not relicense portions with separate terms.
Font arrays and the full font inventory are not exported.

CLI selection uses Cargo's target-filtered runtime and code-generator graph.
Development/unreachable dependencies are excluded; build tools are recorded
separately. Cargo license-file and existing Apache/Boost/protoc fallbacks remain
supported. Generated protobuf descriptors retain schema attribution. MPL-2.0
packages include exact-version source download locations in the notice.

Detailed component, generator, font and runtime files stay in private
`material-evidence/` CI parts. Their manifests and hashes permit notice
regeneration and integrity checks without requiring a human approval artifact.
See [distribution evidence](DISTRIBUTION.md#distribution-material-evidence).

Source distributions retain their existing declarations, REUSE metadata and
component-local texts, including unselected source and fonts. External projects
keep their own notices at the resolved revision. Do not delete original source
notices to simplify binary downloads.
