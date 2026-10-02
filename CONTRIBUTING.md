# Contributing

Use [development](DEVELOPMENT.md) for the environment and engineering workflow,
[testing](docs/testing.md) for relevant checks, and [OpenSpec](openspec/README.md)
for planned changes. Keep contributions focused and report actual validation.

## Rights and third-party material

Submit only material you have the right to contribute under its applicable
license. Meshbus-owned contributions use Apache-2.0; preserve original upstream
copyright, attribution, license headers and permission notices. Do not relabel
external or derived material as Meshbus-owned, or copy incompatible code into
a contribution. Follow the existing [admission policy](LICENSING.md#compiled-dependency-admission),
including its documented license alternatives and exceptions.

For new or updated external code, dependencies, fonts, generated templates or
data, runtime inputs and exported headers, describe in the PR:

- The source and exact version or revision, and how the material enters the
  compiled output or distributed package.
- Applicable file-level terms, the selected license alternative or exception,
  and any conditions that the intended build or distribution must satisfy.
- Required copyright, license, attribution and source-delivery materials,
  where they are retained, and any unresolved issues.

An ordinary contribution that changes none of these inputs can mark the PR's
external-material section as not applicable. Existing conclusions can be reused
when the applicable terms and usage are unchanged. Reconcile genuinely unknown
grants before merging; an SPDX `NOASSERTION` field alone does not establish
that the actual permission is unknown.

## Maintainer review and CI

Maintainers review the above information before merging. Record new or changed
license selections and exceptions in [LICENSING.md](LICENSING.md); retain font
provenance in the existing [font inventory](docs/licensing/fonts.md). These
records and upstream notices remain the source of truth. This workflow adds
no approval registry or new signing or certification requirement.

Existing SPDX/REUSE metadata, repository license-policy and font checks remain
required within the [per-file scope](LICENSING.md#per-file-metadata). Markdown
documents need no SPDX declaration headers; source/scripts/configuration outside
the temporarily excluded `web/` tree remain in scope. Daily checks cover changed
files; shared licensing changes and explicit audits select complete coverage
outside `web/`. Follow the [check commands](.github/CI.md#license-check-scope).
Preserve original upstream notices. CI checks declared metadata and
material integrity; it does not
prove ownership, legal clearance or completion of maintainer review.

## Releases

The [distribution guide](DISTRIBUTION.md#distribution-material-evidence) describes
automatic checks of actual firmware and EDK license materials. Candidate and
Alpha jobs retain material evidence without requiring a per-Alpha approval
file or repeating an exhaustive component review. A newly discovered permission
gap must be resolved through the same contribution review before publication.
Redistribute the applicable NOTICE.txt with extracted firmware or CLI binaries.
