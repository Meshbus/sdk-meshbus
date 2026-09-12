LLEXT C2 real-runtime validation
================================

This destructive service-DUT test is restricted to
``idea_mesh_tracker_c2/nrf54l15/cpuapp``.  It shortens slot1 by 64 KiB and
splits that test-owned range into 12 KiB of Settings ZMS, 48 KiB of LittleFS
mounted at ``/extra``, and a 4 KiB stage page.  Product storage at
``0x174000..0x17cfff`` remains mapped as a guard and is never selected, mounted,
or formatted.

The test writes a current-target ``.mba`` and a missing-symbol ``.mba``
to real LittleFS.  It proves metadata probe, load/relocation, entry execution,
explicit unload, failed-load resource release, successful reload after the
failure, artifact cleanup, and stage cleanup.  Timing output is informational
and is not a performance threshold.  ZUI's required Display dependency is
satisfied by an explicitly declared dummy display; display behavior is not an
asserted boundary of this service-DUT test.

The test erases only their test settings/filesystem/stage partitions at
boot.  Flashing invalidates any secondary image occupying the shortened slot1,
so device execution requires a dedicated C2 and approval of the exact flash
command.

Build-only preflight from ``west topdir``::

   west build -p always -d build/meshbus-llext-c2-app \
     -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
     meshbus/tests/subsys/llext/service_dut -- \
     -DCONFIG_DISPLAY=y -DCONFIG_U8G2=y -DCONFIG_ZUI=y \
     -DCONFIG_MBS_LLEXT_APP_HEAP_RESERVE_SIZE=32768 \
     -DCONFIG_MBS_LLEXT_APP_MAX_HEAP_SIZE=32768
