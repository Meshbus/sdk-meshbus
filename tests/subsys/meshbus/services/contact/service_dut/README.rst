Contact C2 real-ZMS validation
==============================

This test is restricted to ``idea_mesh_tracker_c2/nrf54l15/cpuapp`` and a
device dedicated to destructive validation.  It uses the same reviewed layout
as the Channel reference: the final 64 KiB of slot1 becomes a 60 KiB test-only
ZMS backend and a 4 KiB stage page, while product storage at
``0x174000..0x17cfff`` remains mapped but unselected.

The test persists three contacts, warm-reboots, verifies restoration, and fills
the eight-record logical capacity.  At full capacity it models the two real
writers: a BLE-side public ``meshbus_contact_set()`` call and a MeshCore-side
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

   west build -p always -d build/meshbus-contact-c2-preflight \
     -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
     sdk-meshbus/tests/subsys/meshbus/services/contact/service_dut
