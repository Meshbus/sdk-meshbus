FS C2 real-LittleFS validation
================================

This test is restricted to ``idea_mesh_tracker_c2/nrf54l15/cpuapp`` and a
device dedicated to destructive validation.  It shortens slot1 by 64 KiB,
mounts a 60 KiB test-only LittleFS volume at ``/extra``, and reserves a separate
4 KiB reboot-stage page.  Product storage at ``0x174000..0x17cfff`` remains
mapped but is not mounted or formatted.

The first boot creates and writes a file, then warm-reboots.  The second boot
verifies the bytes, formats only the test filesystem through the public FS API,
checks directory recreation, and erases the stage marker.  Flashing invalidates
any secondary image in the shortened slot1, so execution requires a dedicated
device and approval of the exact flash command.

Build-only preflight from ``west topdir``::

   west build -p always -d build/meshbus-fs-c2-preflight \
     -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
     sdk-meshbus/tests/subsys/meshbus/services/fs/service_dut
