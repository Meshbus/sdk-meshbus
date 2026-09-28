Contact real-ZMS validation
===========================

Select ``<qualified-board-target>`` from the service-DUT scenario in
``testcase.yaml`` and use its exact configuration and fixture. These commands
do not extend platform support or authorize device execution.


This test is restricted to the platforms and fixtures in ``testcase.yaml`` and
a device dedicated to destructive validation. Its overlay assigns separate
test-only ZMS and reboot-stage storage while keeping product storage unselected.
Inspect the overlay and final devicetree for exact partition bounds.

The test persists three contacts, warm-reboots, verifies restoration, and fills
the eight-record logical capacity.  At full capacity it models the two real
writers: a BLE-side public ``mbs_contact_set()`` call and a MeshCore-side
path-response event.  The first write is held immediately before the real ZMS
call; the test proves the MeshCore update blocks behind the Contact writer
transaction, then verifies that the BLE alias and MeshCore path/timestamp are
both preserved.  A second case releases two ordinary Contact writers together,
requires both to complete, reports their timing and verifies neither update is
lost.  The dedicated writer stacks are test-fixture resources sized for the
full Contact/ZBus/settings call path; they are not a product RAM claim.

The test then removes every record, rejects a malformed stored record, and
erases its stage marker.  It performs at most 22 settings mutations.  Running
it invalidates any secondary upgrade image occupying the shortened slot1 and
therefore requires a dedicated device and approval of the exact flash command.

Build-only preflight from ``west topdir``::

   west build -p always -d build/meshbus-contact-dut-preflight \
     -b '<qualified-board-target>' \
     meshbus/tests/subsys/contact/service_dut
