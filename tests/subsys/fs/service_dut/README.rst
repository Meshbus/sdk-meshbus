FS real-LittleFS validation
===========================

Select ``<qualified-board-target>`` from the service-DUT scenario in
``testcase.yaml`` and use its exact configuration and fixture. These commands
do not extend platform support or authorize device execution.


This test is restricted to the platforms and fixtures in ``testcase.yaml`` and
a device dedicated to destructive validation. Its overlay shortens an upgrade
slot for a test-only LittleFS volume at ``/extra`` and a separate reboot-stage
page. Product storage remains mapped but is not mounted or formatted. Inspect
the overlay and final devicetree for exact partition bounds.

The first boot creates and writes a file, then warm-reboots.  The second boot
verifies the bytes, formats only the test filesystem through the public FS API,
checks directory recreation, and erases the stage marker.  Flashing invalidates
any secondary image in the shortened slot1, so execution requires a dedicated
device and approval of the exact flash command.

Build-only preflight from ``west topdir``::

   west build -p always -d build/meshbus-fs-dut-preflight \
     -b '<qualified-board-target>' \
     meshbus/tests/subsys/fs/service_dut
