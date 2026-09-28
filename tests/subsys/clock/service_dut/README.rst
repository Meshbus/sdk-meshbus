Clock reboot validation
=======================

Select ``<qualified-board-target>`` from the service-DUT scenario in
``testcase.yaml`` and use its exact configuration and fixture. These commands
do not extend platform support or authorize device execution.


This service-DUT scenario is restricted to the platforms and fixtures in
``testcase.yaml``.  It sets the Zephyr realtime clock,
uses a 4 KiB retained-RAM region only as a reboot-stage marker, performs a warm
reboot, and asserts the Meshbus contract that wall-clock synchronization is
volatile and must be supplied again after boot.

The overlay reserves the reboot-stage region for the retained-memory driver;
inspect the final devicetree for its location and the remaining application RAM.  It does not write flash or
product settings.  The marker is cleared after verification.

Build-only preflight from ``west topdir``::

   west build -p always -d build/meshbus-clock-dut-preflight \
     -b '<qualified-board-target>' \
     meshbus/tests/subsys/clock/service_dut

Running still requires an approved serial mapping, capture command, SWD probe,
device lease, and exact flash/reset command.
