# Compact Release Distribution

## Purpose

Provide consistently named firmware, development-kit and command-line downloads
with complete applicable notices, while retaining detailed verification evidence
inside CI rather than exposing it as redundant public assets.

## ADDED Requirements

### Requirement: Complete compact download matrix

Future releases SHALL distribute one firmware archive per supported product,
one EDK per EDK-capable product, one CLI archive per supported host target and a
release manifest. Names SHALL use the release version and board basename or host
target. Standalone APP, SBOM and outer checksum files SHALL NOT be public assets.

#### Scenario: Current matrix is staged
- **WHEN** all four product, three EDK and six CLI parts pass validation
- **THEN** staging produces exactly fourteen public assets with the agreed names
- **AND** duplicate board basenames or conflicting assets fail

### Requirement: Minimal archives retain applicable notices

Firmware archives SHALL contain actual images, flash-map.json, SBOM.spdx,
SHA256SUMS and NOTICE.txt. Merged MCUboot images SHALL use firmware.bin/hex.
CLI archives SHALL contain the executable, manifest.json, SHA256SUMS and
NOTICE.txt. EDK SHALL preserve source headers and a single root notice covering
actual exported headers. Identical notice text SHALL be deduplicated without
losing component associations, copyright, license terms or source obligations.

#### Scenario: Selected materials are packaged
- **WHEN** a firmware or CLI archive is generated
- **THEN** its notice contains complete applicable terms and attribution
- **AND** detailed component evidence remains in the private CI part
- **AND** unselected font terms do not appear in the firmware or EDK notice

#### Scenario: Public EDK source is exported
- **WHEN** exported headers carry multiple applicable license declarations
- **THEN** original copyright and permission declarations remain in the headers
- **AND** the root notice contains each required full text once with checked hashes
- **AND** missing selected full texts fail export

#### Scenario: Material evidence is damaged
- **WHEN** component, runtime, font, generated-code or EDK material is missing,
  changed or disallowed
- **THEN** validation fails before public assets are generated

### Requirement: Integrity and published compatibility

Manifest schema 3 SHALL bind payload checksums and record the CLI executable
version separately from the release version. Upload inventory SHALL additionally
bind the public manifest. Source, qualification and native verification gates
SHALL remain effective. Published manifest schemas 1 and 2 and EDK schema 1
SHALL remain verifiable.

#### Scenario: Archive or identity conflict
- **WHEN** bytes, versions, sources, target qualification or checksums conflict
- **THEN** staging or upload validation fails

#### Scenario: Public archive has no sidecar
- **WHEN** a consumer verifies an archive beside its release-manifest.json
- **THEN** verification checks the manifest's exact filename, size and digest
- **AND** missing, duplicate or conflicting entries fail
- **AND** an existing corrupt sidecar cannot be bypassed with a manifest

#### Scenario: Evidence-only mode
- **WHEN** Candidate collects complete material evidence
- **THEN** it produces evidence without public release assets or approval fields

### Requirement: Task-scoped licensing guidance

LICENSING.md SHALL remain the policy owner, and contribution review SHALL
resolve dependency admission and attribution. Agent guidance SHALL distinguish
source metadata from Markdown exemptions and retrieve detailed licensing policy
only for relevant work. Generated skill files and third-party notices SHALL be
preserved.

#### Scenario: Ordinary source or documentation work
- **WHEN** an agent reads repository guidance
- **THEN** it can determine SPDX scope without loading historical release audits
- **AND** current policy validation still checks in-scope changes
