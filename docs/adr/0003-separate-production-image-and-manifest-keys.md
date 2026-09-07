---
status: accepted
---

# Separate production image and manifest trust roots

Every GA application image, including the C2 Client image, must be authorized by
a Production Image Key. The DFOTA Manifest Key is a separate trust root. Its
private key remains behind a controlled offline signing boundary, while the
Production Image private key is held by protected CI as defined by ADR 0009.
Neither private key is committed to source control or placed on ordinary
developer machines.

## Consequences

- Production bootloaders must not trust repository development keys.
- Production public verification keys may be embedded in MCUboot and distributed
  with release artifacts. Their secrecy is not a security property.
- The C2 bootloader must validate the primary-slot image authorization on every
  boot; producing a signed file without enforcing validation is insufficient.
- Firmware build and production-image signing remain separate CI jobs. The
  signing job verifies its frozen input and the resulting signature before
  accepting a signed candidate; publication remains a separate GA decision.
- Every GA Release records the key identifiers and public-key fingerprints used
  for its artifacts.
- The exact signed 1.0.0 application bytes become the retained Release Baseline
  for future DFOTA packages.
- Base-application authorization does not authenticate or sandbox an
  owner-supplied MBA; that boundary is defined separately.
- Key rotation, recovery authentication, and bootloader replacement remain
  separate release-policy decisions.
