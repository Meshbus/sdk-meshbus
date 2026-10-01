# Licensing

FoBE Studio-owned SDK, product firmware, CLI, tools, examples and documentation
use [Apache-2.0](LICENSE). Copyright attribution is **FoBE Studio**. Original
third-party copyright and license notices remain applicable to their portions.

This permits proprietary use while preserving notice and attribution obligations.
The default does not relicense upstream or derived material, independently owned
dependencies, or every component of a firmware distribution. Keep file metadata,
generator templates, packaging, verification and current documentation consistent
with actual ownership. Retain unresolved provenance findings instead of relabeling
third-party material. Publication, signing and history rewriting remain subject
to [repository authorization](AGENTS.md).

## Per-file metadata

| Files | Repository metadata rule |
| --- | --- |
| Markdown (`.md`, case-insensitive), including README, agent guidance, PR templates and OpenSpec artifacts/archives | No per-file SPDX declarations. Project-authored documentation uses the repository license; preserve original third-party notices. |
| Source and headers, scripts, build files, device/configuration files, test/sample metadata and CI workflows | Keep SPDX copyright and license metadata using native comments where supported, or provenance-backed `REUSE.toml` annotations. |
| Files whose format cannot carry comments | Use suitable existing `REUSE.toml` metadata; do not insert comments that break the format. |
| Board documentation raster images and plain version values | No per-file metadata required within the existing scoped policy. |
| Reviewed tool/workspace configuration exceptions | Apply only the exact paths and content hashes in the existing license policy; new or changed files do not automatically qualify. |

The [license-check scope](.github/CI.md#license-check-scope) waives only missing
Markdown copyright/license metadata, without a content hash or approval entry.
It preserves other scanner failures and checks on non-Markdown files. Existing
central REUSE attribution and third-party license/permission texts remain
applicable; this policy neither changes those grants nor claims full REUSE
compliance. Generated skills retain their upstream content and notices.

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

For a dependency addition, update, or newly enabled feature, record the exact
source revision, applicable file-level license, selected alternative or
exception, and how the component enters the output. Check the final configuration
and compiler/linker inputs, including generated and header-only content, rather
than just a repository's root license. For CLI dependencies, inspect each
supported release target's resolved dependency graph.

Use the [contribution guide](CONTRIBUTING.md#rights-and-third-party-material) and
PR template to describe these inputs. Maintainers review them before merging;
reuse existing decisions when terms and usage are unchanged. Keep selections
and exceptions here without a separate approval registry. Ordinary contributions
can mark the external-material section as not applicable.

Preserve the component notices and reconcile them with the delivered files.
Genuinely unknown permission or attribution requires resolution during review.
An SPDX `NOASSERTION` field can mean the producer made no assertion; inspect
actual source terms rather than treating the field alone as missing permission.
The curated release SBOM and a text search for `GPL` are not complete admission checks.
Standalone tool or source redistribution has its own notice/source obligations
even when excluded from this compiled-output rule.

Release CI checks actual material presence and integrity and retains evidence.
It does not require a per-Alpha approval file, repeat an exhaustive component
review or prove legal clearance. Resolve newly discovered permission gaps
through contribution review before publication. Existing metadata and font
checks remain blocking; automated success does not replace maintainer review.

Record component-specific choices under
[external dependency license selections](#external-dependency-license-selections)
and font provenance and exclusions in the [font inventory](docs/licensing/fonts.md).
New language packs and fonts require the same review. The
[distribution guide](DISTRIBUTION.md#publication-requirements) owns release procedure.

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

## CLI distributions

Release packaging copies the CLI `NOTICE` to `THIRD-PARTY-NOTICES.txt` and
collects package license files from Cargo's target-filtered resolve graph.
`dependencies.json` distinguishes runtime dependencies from proc-macro/code
generators whose notices are conservatively retained. Development dependencies
and unreachable packages are excluded. Host build tools are recorded separately
in `build-tools.json`; their presence does not imply their code is distributed.

The collector honors Cargo `license-file`, uses upstream license/notice files
first, and retains limited Apache/Boost/protoc fallbacks. Missing required text
fails packaging. `generated-materials.json` records the embedded protobuf
descriptor digest and the schema project's retained materials, using the same
schema root as the CLI build. The standalone protoc executable is not shipped.
Other generated templates and build-script outputs still require release review.

## Source, EDK and firmware distributions

Source distributions retain `LICENSE`, `LICENSES/`, REUSE metadata, this guide,
component-local declarations and applicable source records. Independent west
projects retain their own materials at the resolved revision; their complete
texts are not mirrored into this repository.

EDKs include Apache-2.0 `LICENSE.txt`, `LICENSES/Apache-2.0.txt`,
`NOTICE.txt` and file declarations. Exported ZUI headers additionally carry
`ZUI-NOTICES.md`; exported U8g2 headers retain `U8G2-NOTICES.md`. The verifier
requires the root and standard Apache-2.0 license texts to match and checks
the notices for exported components.

Firmware archives include `licenses/` and `license-materials.json`. Packaging
selects component roots from both images' private SPDX source inventories and
build module records, copies their root/standard license materials, and retains
nested licenses and leading C/C++ source notices. Protobuf schema materials are
included explicitly for generated bindings; the built-in predictive dictionary
keeps its ISC notice. Custom dictionaries use adjacent license/notice files
and optional `<dictionary>.license` sidecars; missing declarations fail packaging.

U8g2 font arrays are selected from the final unstripped ELF's defined object
symbols. The collector preserves per-font attribution, catalog license/status,
selected family notices and the dependency's supplemental full license texts.
Those generic texts form a conservative superset; inclusion is not a license
choice or approval of a restricted font. Source hashes for retained font notices
remain checked against the external module's own records.

Development builds without SPDX are marked `partial-no-spdx`; they are not
complete dependency collections. Even with SPDX, the bundle is component-level
evidence: toolchain runtimes, other generated material, compatibility, required
source delivery and unresolved grants remain release-review responsibilities.
See [distribution guidance](DISTRIBUTION.md#candidate-assembly-and-provenance).
