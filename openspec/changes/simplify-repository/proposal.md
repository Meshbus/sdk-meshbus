# Simplify repository development and maintenance

## Why

Routine changes expand through recursive CI ownership rules into unrelated tests
and builds, while duplicate scenarios and local helper implementations add work
without independent coverage. A repository-wide convergence pass should remove
verified redundancy and establish proportionate development practices.

## What Changes

- Replace recursive component expansion with explicit, reviewed test ownership;
  keep full validation for scheduled/manual runs and uncertain shared changes.
- Separate routine regression from shuffle/performance qualification using
  existing Twister metadata, while running directly changed extended tests.
- Keep license metadata and CI-tool changes within their verification domains;
  preserve CLI checks for license texts consumed by CLI code and tests.
- Remove the identical LLEXT scenario and centralize identical Desktop input
  predicates; simplify proven unreachable private Clock argument checks.
- Align development, testing, CI and OpenSpec guidance around bounded TDD,
  real failure risks, existing test seams and explicit stopping criteria.
- Include Power's direct Indicator test consumers, finish MeshCore UI predicate
  reuse and make ordinary product-build examples incremental.
- Review firmware, drivers, CLI/tools, product profiles, packaging and examples;
  retain compatibility, security and device-lifecycle protections with distinct
  responsibilities rather than changing files to meet a cleanup quota.

## Capabilities

### New Capabilities

- `development-validation`: Proportionate CI selection and routine/extended
  Twister coverage with strict result accounting.

### Modified Capabilities

None. Firmware and CLI public behavior, protocols, persistence, device support
and release formats remain unchanged.

## Impact

CI planning and regression tests, Twister metadata, Desktop/Clock internals and
engineering documentation. No new framework, dependency update, manifest change,
hardware operation, signing, commit or publication is included. Existing
uncommitted OpenSpec adoption work is preserved.
