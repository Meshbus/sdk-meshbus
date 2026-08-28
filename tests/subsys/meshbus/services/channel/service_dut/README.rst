Channel C2 real-ZMS validation
==============================

This test is restricted to ``idea_mesh_tracker_c2/nrf54l15/cpuapp`` and a
device explicitly dedicated to destructive validation.  It is not safe for a
normal product device.

Partition proof
---------------

The stock board includes Zephyr's ``nrf54l15_cpuapp_partition.dtsi``.  The
1,524 KiB CPUAPP RRAM has no unallocated range:

================ ======== ======== =====================================
Region           Base     Size     Owner
================ ======== ======== =====================================
MCUboot          0x000000 62 KiB   bootloader
alignment gap    0x00f800 2 KiB    reserved alignment
slot0            0x010000 712 KiB  running application
slot1            0x0c2000 712 KiB  secondary upgrade image
product storage  0x174000 36 KiB   product settings/ZMS
================ ======== ======== =====================================

The test overlay does not claim an unused region.  It creates a reviewed,
test-only layout for a dedicated device by shrinking slot1 to 648 KiB and
assigning ``0x164000..0x172fff`` (60 KiB) to
``meshbus-test-storage``.  The final 4 KiB at ``0x173000..0x173fff`` is an
independent stage-marker page used only by the test harness.  The 36 KiB
product-storage bytes remain mapped at
``0x174000..0x17cfff`` as ``product-storage-preserved`` and are never selected
by the settings backend.  All boundaries are 4 KiB erase aligned and 16-byte
write aligned.  Compile-time assertions reject address, size, alignment, fake
backend, or overlap drift.

This layout destroys the compatibility of any secondary image already held in
slot1.  It therefore requires both a dedicated device and explicit approval of
the exact flash/device command.  A build-only check is non-destructive.

Behavior and bounds
-------------------

Before Meshbus settings initialization, the test harness checks the independent
stage-marker page.  A missing or invalid marker causes only the test-owned ZMS
partition and marker page to be erased, then records the prepared phase.  This
makes the first run deterministic even when the former slot1 range contains an
old secondary image.  The test then writes three deterministic Channel records
and records the verify phase before a normal software reboot.  On the second
boot it verifies restore,
duplicate handling, maximum secret/name values, fills the eight-slot logical
capacity, and rejects an out-of-range slot.  At full capacity, it then runs a
deterministic overlapping-write case and a naturally scheduled two-thread
case against the real ZMS backend.  The deterministic case holds the first
writer immediately before ``settings_save_one()``, proves the contender
returns ``-EBUSY`` promptly, then retries and verifies both records.  The
natural case accepts either immediate serialization or ``-EBUSY``, retries if
needed, and verifies both records.  Finally, the test cleans every record,
injects one malformed record in the test partition, proves it remains
invisible, and deletes it.  The run performs at most 22 settings mutations,
erases the stage marker, and ends with an empty Channel subtree.  If a prior
run stopped in the verify phase, the next boot attempts verification rather
than silently erasing the evidence.

Functional markers and timing measurements are printed separately.  A normal
reboot does not prove power-cut behavior; destructive interruption remains a
Phase 5B scenario.

Build-only preflight from ``west topdir``::

   west twister -T sdk-meshbus/tests/subsys/meshbus/services/channel/service_dut \
     -p idea_mesh_tracker_c2/nrf54l15/cpuapp --build-only -v -c

Twister discovery may be unavailable if an unrelated workspace board violates
the active board schema.  In that case, use the narrow direct build only and
classify Twister as infrastructure-blocked::

   west build -p always -d build/meshbus-channel-c2-preflight \
     -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
     sdk-meshbus/tests/subsys/meshbus/services/channel/service_dut

Do not run or flash the result until the local hardware map, device lease,
serial endpoint, capture command, SWD runner, cleanup plan, and exact command
have been approved.
