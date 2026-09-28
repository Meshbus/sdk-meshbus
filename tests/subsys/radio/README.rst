Meshbus Radio service test
==========================

Select ``<qualified-board-target>`` from the service-DUT scenario in
``testcase.yaml`` and use its exact configuration and fixture. These commands
do not extend platform support or authorize device execution.


This application has two Twister scenarios:

* ``subsys.meshbus.radio.contract`` runs the public API, Settings, and ZBus
  contract on ``qemu_x86`` with a simulated flash device and a fake SX1262
  devicetree node.
* The service-DUT scenario in ``testcase.yaml`` runs the same contract plus
  runtime checks on its declared hardware platform.

Shared ``mbs_settings_*`` helper implementation tests live in
``tests/subsys/settings/test_settings.c``.  The Channel
contract application compiles that support source; the Radio application does
not.

The service-DUT scenario uses the fixture's real SX1262 and the LoRa Basics Modem
backend.  It starts disabled, forces ``receive_only`` at build time, and never
publishes a TX request or invokes continuous-wave mode.

The scenario proves driver readiness, receive state, instantaneous RSSI,
airtime calculation, bounded noise-floor calibration, disable/re-enable state
transitions, AGC restart, and cleanup.  It is a single-DUT result: it does not
claim RF packet delivery, MeshCore interoperability, range, sensitivity, or
regulatory conformance.

The selected test overlay reuses part of the secondary image slot as test-only
Settings ZMS storage. Inspect the overlay and final devicetree for exact bounds.
Product storage remains unselected and is never erased.  The test erases its own partition before
service initialization.  An after-hook always resets the public Radio config,
waits for Settings deletion, and leaves the modem idle, including after a
failed assertion.  Running it invalidates any secondary upgrade image
occupying the shortened slot1 and therefore requires the dedicated device and
explicit flash authorization.

Build-only preflight from ``west topdir``::

   west build -p always -d build/meshbus-radio-dut-preflight \
     -b '<qualified-board-target>' \
     meshbus/tests/subsys/radio -- \
     -DEXTRA_CONF_FILE=configs/service_dut.conf
