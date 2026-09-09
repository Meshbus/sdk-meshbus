---
status: accepted
---

# Separate production image and manifest trust roots

Every GA application image, including the C2 Client image, must be authorized by
a Production Image Key. The DFOTA Manifest Key is a separate trust root. Its
private key remains behind a controlled offline signing boundary, while the
Production Image private key is supplied to a trusted native release build as
defined by ADR 0010. Neither private key is committed to source control.

All official Meshbus board models share one Ed25519 Production Image Key and
the corresponding public verification key. Keys are not allocated per board
model, physical device, hardware revision, or firmware version. This keeps
development-board release management small; future supported boards adopt the
same trust root without introducing another private key to maintain.

## Consequences

- Production bootloaders must not trust repository development keys.
- Production public verification keys may be embedded in MCUboot and distributed
  with release artifacts. Their secrecy is not a security property.
- The C2 bootloader must validate the primary-slot image authorization on every
  boot; producing a signed file without enforcing validation is insufficient.
- Zephyr signs during the trusted release build. Packaging independently
  verifies the signed APP and embedded public key before accepting a candidate;
  publication remains a separate decision.
- Every GA Release records the key identifiers and public-key fingerprints used
  for its artifacts.
- A shared-key signature authenticates the publisher, not board compatibility.
  Build, packaging, and update paths must still validate the target identity.
- Disclosure or replacement of the shared key affects all boards that trust
  it. This common recovery scope is accepted in exchange for simpler custody.
- The exact signed 1.0.0 application bytes become the retained Release Baseline
  for future DFOTA packages.
- Base-application authorization does not authenticate or sandbox an
  owner-supplied MBA; that boundary is defined separately.
- Key rotation, recovery authentication, and bootloader replacement remain
  separate release-policy decisions.
