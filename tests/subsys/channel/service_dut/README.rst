Channel real-ZMS validation
===========================

Select ``<qualified-board-target>`` from the service-DUT scenario in
``testcase.yaml`` and use its exact configuration and fixture. These commands
do not extend platform support or authorize device execution.


This test is restricted to the platforms and fixtures declared in
``testcase.yaml`` and requires a device dedicated to destructive validation.
It is not safe for a normal product device.

Storage boundary
----------------

The selected test overlay shortens an upgrade slot to provide test-only ZMS
storage and an independent reboot-stage marker. Product storage remains mapped
but is not selected by the settings backend. Review the overlay and final
``zephyr.dts`` for addresses, sizes and erase/write alignment; compile-time
assertions reject layout, fake-backend or overlap drift.

This layout invalidates any secondary upgrade image occupying the reused range.
Execution therefore requires a dedicated device and approval of the exact flash
command. A build-only check is non-destructive.

Behavior and bounds
-------------------

Before Meshbus settings initialization, the test harness checks the independent
stage-marker page.  A missing or invalid marker causes only the test-owned ZMS
partition and marker page to be erased, then records the prepared phase.  This
makes the first run deterministic even when the reused slot1 range contains a
secondary image. The test then writes three deterministic Channel records
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

Functional markers and timing measurements are printed separately. A normal
reboot does not prove power-cut behavior; interrupted-power recovery requires
separate scenarios and device evidence.

Build-only preflight from ``west topdir``::

   west twister -T meshbus/tests/subsys/channel/service_dut \
     -p '<qualified-board-target>' --build-only -v -c

Alternatively, build the application directly::

   west build -p always -d build/meshbus-channel-dut-preflight \
     -b '<qualified-board-target>' \
     meshbus/tests/subsys/channel/service_dut

Do not run or flash the result until the local hardware map, device lease,
serial endpoint, capture command, SWD runner, cleanup plan, and exact command
have been approved.
