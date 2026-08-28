C2 Management, MeshCore and Contact settings contention
========================================================

This C2 service-DUT test exercises three real Meshbus settings domains on one
real ZMS backend: synchronous Contact records plus delayed Management and
MeshCore configuration persistence.  Two deterministic barrier rounds align a
Contact write with each delayed configuration writer, followed by a six-update
burst from three application threads.  A warm reboot verifies the final
Contact alias, Management secret, MeshCore configuration and MeshCore identity.

The fixture observes only public APIs and wraps ``settings_save_one`` at the
test boundary to count domains and align calls before the real Settings/ZMS
implementation.  It does not inspect private service locks.  The test never
logs secret or identity bytes.  Management remote-session framing, peer
authentication and SMP delivery remain covered by QEMU contracts or later
multi-DUT scenarios; this image proves same-device persistence contention and
reboot durability only.

The real SX1262 and MeshCore repeater runtime remain enabled so the aggregate
service composition is genuine, but Radio is compile-time receive-only and no
MeshCore request is submitted.  Product storage at ``0x174000..0x17cfff`` is a
guard and is never selected or erased.  The shortened slot1 provides 60 KiB of
test ZMS plus a 4 KiB reboot-stage page, both erased at completion.  Running the
test invalidates any secondary upgrade image in that shortened slot and needs
the dedicated authorized device.
