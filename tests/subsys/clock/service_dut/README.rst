Clock C2 reboot validation
==========================

This service-DUT scenario is restricted to
``idea_mesh_tracker_c2/nrf54l15/cpuapp``.  It sets the Zephyr realtime clock,
uses a 4 KiB retained-RAM region only as a reboot-stage marker, performs a warm
reboot, and asserts the Meshbus contract that wall-clock synchronization is
volatile and must be supplied again after boot.

The overlay shrinks normal SRAM from 188 KiB to 184 KiB and assigns the final
4 KiB to the standard retained-memory driver.  It does not write flash or
product settings.  The marker is cleared after verification.

Build-only preflight from ``west topdir``::

   west build -p always -d build/meshbus-clock-c2-preflight \
     -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
     meshbus/tests/subsys/clock/service_dut

Running still requires an approved serial mapping, capture command, SWD probe,
device lease, and exact flash/reset command.
