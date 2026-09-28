MBA lifecycle on a storage fixture
==================================

Select ``<qualified-board-target>`` from the service-DUT scenario in
``testcase.yaml`` and use its exact configuration and fixture. These commands
do not extend platform support or authorize device execution.


This dedicated service-DUT image runs the parent lifecycle assertions using
real ARM extension relocation, threads, RRAM and LittleFS. UI presentation is
still a recording adapter; unload failures and exit scheduling are injected
through the same linker wraps. It does not prove a physical display or input.

The image shortens an upgrade slot for test Settings and LittleFS storage,
erased at boot and suite teardown. Product storage remains unselected. Inspect
the test overlay and final devicetree for exact bounds. The two package files
are removed and LittleFS unmounted before final erase. Test storage must be
disposable; flashing invalidates any image in the shortened slot.

Use only an authorized dedicated device matching the platform and fixture in
``testcase.yaml``. Capture serial before flashing, identify the probe separately,
and preserve the original image and storage when restoration is required.
Build from ``west topdir``::

  west build -b '<qualified-board-target>' \
    meshbus/tests/subsys/desktop/mba_lifecycle/service_dut \
    -d 'build/<task>/mba'

The shuffled scenario additionally sets ``CONFIG_ZTEST_SHUFFLE=y`` and
``CONFIG_TIMER_RANDOM_INITIAL_STATE=826366246``. Record every flashed artifact
and each device result independently of the QEMU integration evidence.
