C2 MeshCore receive-only validation
====================================

This service-DUT test is restricted to
``idea_mesh_tracker_c2/nrf54l15/cpuapp``.  It runs the real MeshCore repeater
runtime with the real C2 SX1262 and LoRa Basics Modem backend.  Radio TX is
disabled at build time through ``receive_only``; the test submits no advert,
discovery, trace, message, raw-data, control-data, or continuous-wave request.

The scenario proves current MeshCore library/adapter linkage, runtime startup,
generated local identity and repeater role, Radio-driven pause/resume behavior,
identity stability, request-drop stability when idle, and deterministic
cleanup.  Runtime start/pause/resume logs are required alongside the ztest
business marker during serial orchestration.  This is a single-DUT result and
does not claim packet delivery, peer interoperability, relay correctness,
range, sensitivity, or regulatory conformance.

The final 64 KiB of slot1 is temporarily mapped as the test-only Settings ZMS
partition.  Product storage at ``0x174000..0x17cfff`` remains mapped but
unselected and is never erased.  The test erases its own partition before
service initialization and again after disabling the radio.  Running it
invalidates any secondary upgrade image occupying the shortened slot1 and
therefore requires the dedicated device and explicit flash authorization.

Build-only preflight from ``west topdir``::

   west build -p always -d build/meshbus-meshcore-c2-preflight \
     -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
     meshbus/tests/subsys/meshcore/service_dut
