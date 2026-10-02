# Development validation delta

## ADDED Requirements

### Requirement: Validate affected product configurations

Daily validation SHALL build explicitly affected product profiles and reviewed
consumers. Mixed changes SHALL union product/profile pairs without widening
unrelated products. Known application-only inputs SHALL NOT select unrelated
SDK tests. Full validation SHALL retain all default products and any explicitly
changed development or production profiles. Unresolved inputs SHALL expand.

#### Scenario: Production fragment changes
- **WHEN** only the production fragment changes
- **THEN** products build with that fragment and unrelated SDK tests are omitted

#### Scenario: Mixed board and profile changes
- **WHEN** one board's defaults and the common production fragment change
- **THEN** that board's default and all production profiles are checked

### Requirement: Expand license checks only for affected attribution

Routine license checks SHALL include affected sidecar files and nested context
subtrees while preserving the existing website exclusion. Ignore rules and
licensing prose SHALL NOT cause full audits. Provably website-only root REUSE
changes SHALL stay incremental. Global effective attribution, texts, checker,
policy and unavailable bases SHALL retain full audits and blocking errors.

#### Scenario: Removed sidecar
- **WHEN** a sidecar is removed or moved
- **THEN** its surviving old and new source counterparts are checked

#### Scenario: Website-only root attribution
- **WHEN** only annotations confined to web change
- **THEN** unrelated source files are not selected by that change

### Requirement: Prepare relevant source tooling

OpenSpec installation and checks SHALL run for relevant tooling/spec inputs and
full validation. Other source checks SHALL remain selected. Restored tool
archives SHALL retain pinned digest verification.

#### Scenario: Ordinary firmware change
- **WHEN** no OpenSpec input changes in a narrowed run
- **THEN** Node/npm/OpenSpec preparation is omitted

### Requirement: Schedule the complete selected Twister inventory

Small dual-layer selections SHALL share workspace preparation. Measured history
SHALL only affect grouping, never selected coverage or execution requirements.
Missing or invalid history SHALL use deterministic default grouping. Failed,
missing, duplicated or unexpectedly skipped instances SHALL fail validation.

#### Scenario: Small mixed selection
- **WHEN** both layers have one shard and total at most twelve instances
- **THEN** one worker prepares their workspace and retains separate reports

#### Scenario: Unusable timing history
- **WHEN** history is absent, corrupt, incompatible or unavailable
- **THEN** the complete selection runs with default grouping

### Requirement: Keep performance experiments outside qualification

Benchmark runs SHALL compare fixed inventories and environments and SHALL NOT
qualify Candidate baselines. Ordinary qualification SHALL reject benchmark-only
selection and scheduler overrides.

#### Scenario: Successful narrowed benchmark
- **WHEN** a benchmark completes successfully
- **THEN** its results support performance comparison but cannot replace full CI
