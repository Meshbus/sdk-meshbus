MeshCore receive-only validation
================================

Select ``<qualified-board-target>`` from the service-DUT scenario in
``testcase.yaml`` and use its exact configuration and fixture. These commands
do not extend platform support or authorize device execution.


This service-DUT test is restricted to the platforms and fixtures in
``testcase.yaml``. It runs the real MeshCore repeater runtime with the fixture's
SX1262 and LoRa Basics Modem backend.  Radio TX is
disabled at build time through ``receive_only``; the test submits no advert,
discovery, trace, message, raw-data, control-data, or continuous-wave request.

The scenario proves current MeshCore library/adapter linkage, runtime startup,
generated local identity and repeater role, Radio-driven pause/resume behavior,
identity stability, request-drop stability when idle, and deterministic
cleanup.  Runtime start/pause/resume logs are required alongside the ztest
business marker during serial orchestration.  This is a single-DUT result and
does not claim packet delivery, peer interoperability, relay correctness,
range, sensitivity, or regulatory conformance.

The overlay reuses part of the secondary image slot as test-only Settings ZMS
storage; product storage remains unselected and is never erased. Inspect the
final devicetree for exact bounds. The test erases its partition before service
initialization and after disabling the radio. Running it invalidates any
secondary upgrade image occupying that range and requires a dedicated device
and explicit flash authorization.

Build-only preflight from ``west topdir``::

   west build -p always -d build/meshbus-meshcore-dut-preflight \
     -b '<qualified-board-target>' \
     meshbus/tests/subsys/meshcore/service_dut
