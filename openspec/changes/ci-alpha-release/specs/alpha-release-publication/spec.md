<!-- SPDX-FileCopyrightText: 2026 FoBE Studio -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Alpha release publication

## Purpose

Provide public firmware Alpha downloads whose bytes, version, source and CI
validation can be independently checked without implying GA qualification.

## ADDED Requirements

### Requirement: Bind publication to a committed version

An Alpha publication SHALL use one existing version tag and its committed source
revision throughout validation, packaging and publication. The tag's version,
firmware VERSION, firmware package and matching EDK SHALL agree. Publication
SHALL reject dirty or off-manifest candidate inputs.

#### Scenario: First Alpha version
- **WHEN** `v1.0.0-alpha.1` points to committed firmware version `1.0.0-alpha.1`
- **THEN** every published firmware and EDK asset records that version and source

#### Scenario: Conflicting version or source
- **WHEN** the tag, source, package version or EDK identity disagrees
- **THEN** publication fails without exposing a public Release

### Requirement: Require complete candidate validation

Publication SHALL require the existing complete strict device-free validation,
production-profile product packaging, SPDX and license-material collection,
matching EDK verification/qualification and complete firmware assembly. A
successful narrowed main CI result SHALL NOT substitute for these checks.

#### Scenario: Candidate succeeds
- **WHEN** all required checks and complete candidate assembly succeed
- **THEN** their verified outputs become eligible for Alpha publication staging

#### Scenario: Missing or failed evidence
- **WHEN** a required job, product, EDK, notice material or checksum is missing,
  failed or unexpectedly skipped
- **THEN** the workflow fails before public publication

### Requirement: Publish an explicit pilot asset set

The first pilot SHALL publish only Mesh Probe R1's application UF2, complete
license-bearing firmware archive, matching EDK, public SBOM, checksums and a
portable release manifest. The manifest SHALL enumerate every payload asset's
name, size and SHA256 and identify the tag, source, dependency revisions,
builder digest and CI run. The checksum inventory SHALL cover payload assets
and the release manifest, excluding itself. Internal logs, private SPDX
inventories and local build paths SHALL remain outside the public asset set.

#### Scenario: Complete R1 downloads
- **WHEN** the R1 candidate is selected for publication
- **THEN** its standalone UF2 matches the UF2 in the firmware archive
- **AND** the companion archive retains required notices and license material
- **AND** the release manifest identifies the complete selected download set

#### Scenario: Unexpected or conflicting asset
- **WHEN** staging contains an unexpected asset, duplicate name, missing expected
  asset or digest that differs from the verified candidate
- **THEN** publication fails

### Requirement: Stage and verify before public visibility

The workflow SHALL create a draft, upload the complete selected asset set and
verify the uploaded bytes before exposing it as a Pre-release. It SHALL publish
with `latest=false` and SHALL verify anonymous download of the final assets.

#### Scenario: Successful staged publication
- **WHEN** every draft asset downloads with its expected size and SHA256
- **THEN** the workflow publishes a Pre-release and verifies its public downloads

#### Scenario: Upload or draft verification fails
- **WHEN** an upload or draft download checksum verification fails
- **THEN** the Release remains a draft and the workflow reports the failed stage

#### Scenario: Public download verification fails
- **WHEN** publication succeeds but final anonymous download verification fails
- **THEN** the run fails and reports that publication already occurred
- **AND** the existing assets are preserved for investigation

### Requirement: Preserve published version contents

The workflow SHALL NOT move a published tag or replace published assets. It
SHALL recover only a CI-owned draft whose tag/source and retained verified
asset inventory agree. Existing public releases with matching identity and
verified inventory SHALL be treated as already published; conflicts SHALL fail.

#### Scenario: Resume an interrupted upload
- **WHEN** the failed publication job is retried using its retained verified
  artifacts and a matching CI-owned draft
- **THEN** matching uploaded assets are retained and missing assets are uploaded

#### Scenario: Revisit a published version
- **WHEN** the version is already public with matching identity and verified assets
- **THEN** the workflow verifies the existing downloads without replacing bytes

#### Scenario: Existing release conflict
- **WHEN** an existing draft or public release has different ownership, source
  identity or asset digests
- **THEN** the workflow fails without moving tags or overwriting assets

### Requirement: Declare Alpha qualification separately

An Alpha publication SHALL have explicit Alpha publication eligibility and
report the actual CI checks and unmet hardware/production qualification. It
SHALL preserve candidate qualification records and SHALL NOT manufacture a GA,
device-operation, image-authentication or host-signing claim. The pilot's CLI
builds SHALL remain internal validation/tool inputs with independent versions.

#### Scenario: Device-free Alpha
- **WHEN** a candidate passes CI without physical-device validation
- **THEN** the public manifest and release notes identify Alpha status and the
  unperformed hardware validation
- **AND** candidate records retain their unqualified production status
