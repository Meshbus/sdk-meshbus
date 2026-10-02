# Alpha publication delta

## MODIFIED Requirements

### Requirement: Require complete candidate validation

Publication SHALL require complete strict device-free validation, production
packaging, license materials, matching EDK qualification and complete assembly.
Candidate SHALL reuse Twister and default-product results only from trusted
successful full main CI with exact source, manifest, frozen graph and immutable
Builder identity. All expected product profiles and actual Twister tasks SHALL
be proven successful. Narrowed, benchmark, expired or conflicting evidence SHALL
NOT qualify. Fresh Candidate SHALL retain strict CLI, host, production packaging,
EDK and assembly checks.

#### Scenario: Complete baseline reuse
- **WHEN** matching full CI proves all default products and Twister tasks
- **THEN** Candidate reuses them and performs the remaining strict checks

#### Scenario: Missing qualification
- **WHEN** any required product, test or identity proof is absent or conflicting
- **THEN** Candidate fails before packaging is accepted

## ADDED Requirements

### Requirement: Promote qualified immutable Candidate artifacts

Publication SHALL accept a successful trusted Candidate for its exact source and
version, binding the qualifying run/attempt and artifact IDs/digests. It SHALL
use that Candidate's Builder identity, recheck staged materials and bytes, and
refresh current vulnerability checks under existing policy. Missing or expired
automatic selections SHALL fall back to fresh preparation; explicit conflicts,
API errors and corrupt inputs SHALL fail. Historical public assets SHALL remain
readable and unchanged.

#### Scenario: Qualified Candidate exists
- **WHEN** retained Candidate inputs match the publication source and version
- **THEN** staging reuses their bytes without rebuilding firmware or CLI

#### Scenario: Explicit Candidate conflict
- **WHEN** a specified run has mismatched source, identity or artifacts
- **THEN** staging fails without selecting a different run

### Requirement: Exercise staging without publication

Manual Alpha runs SHALL exercise the same qualification and staging checks with
read-only repository permissions and SHALL NOT create tags or Releases. They
SHALL NOT require a pre-existing remote tag. Only tag-push publication SHALL
write release assets after successful staging.

#### Scenario: Manual staging succeeds
- **WHEN** a qualified Candidate passes a manual staging run
- **THEN** verified assets and evidence are retained without public publication
