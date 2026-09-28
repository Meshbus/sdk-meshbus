---
status: accepted
---

# Restrict licenses of compiled third-party dependencies

Meshbus must remain usable in proprietary products.
Third-party dependencies incorporated into compiled outputs must not require
GPLv3 licensing of those outputs. This admission policy is separate from
[the Apache-2.0 license for Meshbus-owned content](0013-license-meshbus-under-apache-2-0.md)
and does not itself grant rights to third-party material.

## Scope and admission

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

## Change and release checks

For a dependency addition, update, or newly enabled feature, record the exact
source revision, applicable file-level license, selected alternative or
exception, and how the component enters the output. Check the final configuration
and compiler/linker inputs, including generated and header-only content, rather
than just a repository's root license. For CLI dependencies, inspect each
supported release target's resolved dependency graph.

Preserve the component notices and reconcile them with the delivered files.
Unknown attribution or `NOASSERTION` is an unresolved review item. The curated
release SBOM and a text search for `GPL` are not complete admission checks.
Standalone tool or source redistribution has its own notice/source obligations
even when excluded from this compiled-output rule.

Component-specific license selections belong in
[dependency notices](../../LICENSING.md#external-dependency-license-selections),
and font provenance and exclusions in the
[font inventory](../licensing/fonts.md). New language packs and
fonts require the same review. The [distribution guide](../../DISTRIBUTION.md#publication-requirements)
owns the release procedure.
