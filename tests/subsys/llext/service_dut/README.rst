LLEXT real-runtime validation
=============================

Select ``<qualified-board-target>`` from the service-DUT scenario in
``testcase.yaml`` and use its exact configuration and fixture. These commands
do not extend platform support or authorize device execution.


This destructive service-DUT test is restricted to the platforms and fixtures
in ``testcase.yaml``. Its overlay shortens an upgrade slot for separate test
Settings, LittleFS (``/extra``) and stage partitions. Product storage remains
mapped as a guard and is never selected, mounted or formatted. Inspect the
overlay and final devicetree for exact bounds before device execution.

The test writes a current-target ``.mba`` and a missing-symbol ``.mba``
to real LittleFS.  It proves metadata probe, load/relocation, entry execution,
explicit unload, failed-load resource release, successful reload after the
failure, artifact cleanup, and stage cleanup.  Timing output is informational
and is not a performance threshold.  ZUI's required Display dependency is
satisfied by an explicitly declared dummy display; display behavior is not an
asserted boundary of this service-DUT test.

The test erases only its test settings/filesystem/stage partitions at
boot.  Flashing invalidates any secondary image occupying the shortened slot1,
so device execution requires a dedicated test device and approval of the exact flash
command.

Build-only preflight from ``west topdir``::

   west build -p always -d build/meshbus-llext-dut-app \
     -b '<qualified-board-target>' \
     meshbus/tests/subsys/llext/service_dut -- \
     -DCONFIG_DISPLAY=y -DCONFIG_U8G2=y -DCONFIG_ZUI=y \
     -DCONFIG_MBS_LLEXT_APP_HEAP_RESERVE_SIZE=32768 \
     -DCONFIG_MBS_LLEXT_APP_MAX_HEAP_SIZE=32768
