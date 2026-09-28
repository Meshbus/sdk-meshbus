---
status: accepted
---

# Keep MCUboot replacement and recovery under owner control

The Mesh Probe R2 MCUboot product profile favors owner control and physical recovery
over preventing physical modification or enforcing a minimum firmware version.
It leaves programming/debug access available so the owner can erase and replace
both bootloader and application over SWD. Other product profiles define their
own boot and recovery capabilities; SDK board support alone does not adopt
this policy.

The official MCUboot provides UART serial recovery and no BLE recovery. Normal
boot checks the image hash by default. An explicitly authenticated build also
checks its signature with the selected Production Image Key. Recovery upload
can erase or write sectors before validation. Hash checking is not publisher
authentication, and enabled signing prevents unauthorized execution rather than
unauthorized bytes from reaching flash through physical recovery.
Recovery therefore does not guarantee preservation of application or user data.
Separately authenticated application-level BLE management remains available
where configured.

This boot policy allows older target- and layout-compatible applications. In
authenticated mode their signature and key must also remain accepted.
There is no monotonic anti-rollback counter: an old vulnerability cannot be
revoked solely by rejecting its firmware version, even in authenticated mode.
Changing the boot trust root requires replacing MCUboot. Signature acceptance does not establish downgrade
compatibility for settings, filesystems or other user data, and signed rollback
is distinct from automatic recovery after a failed update.

Firmware signing does not stop a physical owner from copying firmware or
installing a different trust root. The official boot-chain assurance ends when
the owner replaces that chain on an authenticated product; the default hash-only
build provides no publisher-authentication claim. Restoring factory images does
not by itself prove the integrity of retained user data or owner-installed MBAs.

The DFOTA package format has separate version and security-counter checks.
This boot policy neither enables DFOTA for this profile nor relaxes those
checks; adding it requires reconciling both policies. See
[product qualification](../../apps/meshbus/README.md#product-policy-and-release-qualification)
and [DFOTA](../../DISTRIBUTION.md#dfota) for current scope and validation.
