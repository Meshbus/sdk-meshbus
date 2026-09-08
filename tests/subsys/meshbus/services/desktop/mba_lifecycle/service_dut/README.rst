MBA lifecycle on DevKit nRF54L15
===============================

This dedicated service-DUT image runs the parent lifecycle assertions using
real ARM extension relocation, threads, RRAM and LittleFS. UI presentation is
still a recording adapter; unload failures and exit scheduling are injected
through the same linker wraps. It does not prove a physical display or input.

The image shortens slot1 and erases test settings at 0x164000..0x166fff and
test LittleFS at 0x167000..0x173fff once at boot and once at suite teardown.
Product storage at 0x174000..0x17cfff is not selected. The two package files
are removed and LittleFS unmounted before final erase. Test-owned storage
must be disposable; flashing invalidates any image in the shortened slot1.

Use only an authorized dedicated DevKit with the
``meshbus_mba_devkit_test_storage`` fixture. Capture serial before flashing,
identify the SWD probe separately, and preserve the original image and storage
when restoration is required. Build from west topdir::

  west build -b devkit_nrf54l15/nrf54l15/cpuapp \
    meshbus/tests/subsys/meshbus/services/desktop/mba_lifecycle/service_dut \
    -d build/<task>/devkit-mba

The shuffled scenario additionally sets ``CONFIG_ZTEST_SHUFFLE=y`` and
``CONFIG_TIMER_RANDOM_INITIAL_STATE=826366246``. Record every flashed artifact
and each device result independently of the QEMU integration evidence.
