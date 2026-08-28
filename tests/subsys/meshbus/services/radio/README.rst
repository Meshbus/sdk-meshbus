Meshbus Radio service test
==========================

This application has two Twister scenarios:

* ``subsys.meshbus.radio.contract`` runs the public API, Settings, and ZBus
  contract on ``qemu_x86`` with a simulated flash device and a fake SX1262
  devicetree node.
* ``subsys.meshbus.radio.service_dut.c2`` runs the same contract plus a
  service-DUT suite on ``idea_mesh_tracker_c2/nrf54l15/cpuapp``.

Shared ``mb_settings_*`` helper implementation tests live in
``tests/subsys/meshbus/common/settings/test_settings.c``.  The Channel
contract application compiles that support source; the Radio application does
not.

The service-DUT scenario uses the real C2 SX1262 and the LoRa Basics Modem
backend.  It starts disabled, forces ``receive_only`` at build time, and never
publishes a TX request or invokes continuous-wave mode.

The scenario proves driver readiness, receive state, instantaneous RSSI,
airtime calculation, bounded noise-floor calibration, disable/re-enable state
transitions, AGC restart, and cleanup.  It is a single-DUT result: it does not
claim RF packet delivery, MeshCore interoperability, range, sensitivity, or
regulatory conformance.

The final 64 KiB of slot1 is temporarily mapped as the test-only Settings ZMS
partition.  Product storage at ``0x174000..0x17cfff`` remains mapped but
unselected and is never erased.  The test erases its own partition before
service initialization.  An after-hook always resets the public Radio config,
waits for Settings deletion, and leaves the modem idle, including after a
failed assertion.  Running it invalidates any secondary upgrade image
occupying the shortened slot1 and therefore requires the dedicated device and
explicit flash authorization.

Build-only preflight from ``west topdir``::

   west build -p always -d build/meshbus-radio-c2-preflight \
     -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
     sdk-meshbus/tests/subsys/meshbus/services/radio -- \
     -DEXTRA_CONF_FILE=configs/service_dut.conf
