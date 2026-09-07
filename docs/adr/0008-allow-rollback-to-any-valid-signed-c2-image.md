---
status: accepted
---

# Allow rollback to any valid signed C2 image

The C2 boot policy permits Signed Rollback to any structurally valid application
authorized by the Production Image Key. C2 firmware 1.0.0 does not use a
monotonic security counter or another anti-rollback mechanism.

## Consequences

- An older official image remains bootable while its signature and signing key
  remain accepted by the installed FoBE MCUboot.
- A security defect in an older signed image cannot be revoked selectively by
  version under this policy. Changing the accepted trust root requires replacing
  MCUboot, which the owner can do over SWD.
- Cryptographic acceptance does not guarantee downgrade compatibility. Each
  future release must state whether returning to an older image preserves or
  requires erasing settings, filesystems, and user data.
- The existing security-counter rules for Repeater, Room, Sensor, and their DFOTA
  package tooling remain unchanged. C2 DFOTA is outside firmware 1.0.0.
- Adding C2 DFOTA later requires an explicit decision on how its downgrade policy
  interacts with the current DFOTA package format and validation tools.
