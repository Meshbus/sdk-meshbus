---
status: accepted
---

# Make image authentication optional and keep update trust separate

Public SDK MCUboot builds default to hash-only image validation. They need no
private key and do not authenticate publishers. This lets downstream users build
and distribute the firmware without adopting a Meshbus-controlled trust root,
while retaining the bootloader's image format, integrity checks and recovery.

Authenticated products opt into Ed25519 using a caller-owned key. A downstream
product may share one Production Image Key across its boards, accepting that a
compromise affects every board trusting it. A signature identifies a publisher,
not a compatible board, layout or device. Target and partition checks remain
necessary in either mode. An authenticated bootloader validates the primary
image on every boot; example keys and silent fallback to hash-only mode are
not acceptable substitutes for the selected authentication policy.

Use Zephyr sysbuild's native MCUboot/imgtool signing when authentication is
selected. This avoids a separate build-to-signer handoff, at the cost of trusting
the reviewed source, build scripts and toolchain with the private key. The
release host owns key storage and cleanup. Private material stays outside source,
logs and artifacts. Packaging verifies the signature and the correspondence
between the supplied public key and MCUboot's embedded key. Unsigned packages
verify the image hash and make no signing claim.

The DFOTA Manifest Key remains independent of the image key and its private key
stays behind an offline signing boundary. The authenticated DFOTA package format
continues to require signed source and target images; the public firmware's
unsigned default does not relax that contract.

Retain exact application bytes, authentication mode and any key identity with
each release. Rebuilding a version does not recreate its delta baseline. Neither
hashing nor signing establishes hardware qualification, authorizes publication,
or authenticates owner-supplied [MBA code](0004-owner-trusted-mba-applications.md).
See the [distribution guide](../../DISTRIBUTION.md#mcuboot-products) for commands
and the required bootloader/application pairing when changing modes.
