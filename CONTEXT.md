# Meshbus Platform and Firmware Product

This context defines the language used for Meshbus firmware products, releases,
and loadable extensions. Product firmware and the reusable Meshbus SDK share
this source repository and domain context.

## Releases

**GA Release**:
A production-qualified firmware release made available for supported Product
Targets. A Release Candidate or Engineering Candidate is not a GA Release.
_Avoid_: Formal release, stable candidate

**Engineering Candidate**:
A non-publishable firmware build used to collect engineering evidence before a
GA Release. It may be authorized by the Production Image Key but carries no
production qualification claim.
_Avoid_: Development release, production candidate

**Firmware Release Train**:
The independent sequence of versioned firmware releases for Product Targets.
_Avoid_: Product release train

**CLI Release Train**:
The independent sequence of versioned releases of the Meshbus host CLI.
_Avoid_: Firmware tool version

**Release Baseline**:
The exact released application bytes and release identity retained as the
source for future update packages. Authenticated update formats additionally
require an appropriately signed baseline.
_Avoid_: Latest build, reconstructed baseline

## Product Scope

**Meshbus SDK**:
The reusable Meshbus services, public APIs, hardware support, and UI components
consumed by product firmware and other applications. It is distinct from the
release-matched EDK used to build Desktop MBA packages.
_Avoid_: EDK, firmware product

**Product Target**:
A device firmware identity covered by the support and qualification contract
of a GA Release, independent of its selected MeshCore role.
_Avoid_: Build target, board variant

**Device Capabilities**:
The services and storage capacities available in a device's firmware, shared
by all supported MeshCore roles.
_Avoid_: Role features

**MeshCore Role**:
The node's CHAT, REPEATER, ROOM, or SENSOR protocol behavior. The configured
role is the owner's saved selection; the active role governs the current session.
_Avoid_: Firmware role, product role

**Qualification Fixture**:
A board and role configuration used to collect engineering evidence without
being covered by the support contract of a GA Release.
_Avoid_: Development product, test product

**Factory Image**:
The complete first-install firmware set for a Product Target, including its
required boot components and application. It is distinct from an application-only
update and does not imply a backup of per-device data.
_Avoid_: Full image, merged package

## Loadable Extensions

**Extension Development Kit (EDK)**:
A release-matched set of public compiler inputs used to build loadable
Desktop applications for one Product Target.
_Avoid_: SDK archive, generic EDK

**MBA**:
A loadable, owner-trusted native application package built against an EDK
and run in Desktop, independently of the current MeshCore Role.
The MBA host does not require publisher approval or a package signature.
_Avoid_: LLEXT app

**MBA Session**:
One attempt to run an MBA in Desktop, including any resources retained after
loading or execution fails. It ends only when all host-owned resources have
been reclaimed; returning from the application does not by itself end it.
_Avoid_: Loaded package, running MBA

## Release Trust

**Production Image Key**:
The trust root selected by an authenticated MCUboot product to authorize
application images for execution. It identifies the publisher, not the compatible
board or an individual device. Downstream products own their trust roots; public
SDK builds do not require one by default.
_Avoid_: Firmware key, signing key

**DFOTA Manifest Key**:
The independent production trust root used to authorize DFOTA package metadata.
_Avoid_: Image key, update key

**Signed Rollback**:
Execution of an older official application that remains authorized by the
Production Image Key. It is not the same as automatic recovery after a failed
update.
_Avoid_: Recovery rollback, unsigned downgrade

**Official Firmware Trust Boundary**:
The authenticity boundary from the official MCUboot through the verified base
application on products adopting that signed boot policy. It ends when the
owner launches an MBA or replaces the boot chain. It does not establish the
integrity of retained user data.
_Avoid_: Device trust, MBA sandbox
