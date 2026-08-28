Message service contract and C2 runtime validation
==================================================

This application defines two Twister scenarios:

* ``subsys.meshbus.message.contract`` runs the public Message API and ZBus
  contract on ``qemu_x86``.
* ``subsys.meshbus.message.service_dut.c2`` runs the same
  hardware-safe contract on ``idea_mesh_tracker_c2/nrf54l15/cpuapp`` and adds
  focused service-DUT runtime checks.

Both scenarios wrap MeshCore configuration, Contact lookup, and Channel lookup
at their public dependency boundaries. The test-only radio-ready Kconfig
option stops outbound requests at the public Message request ZBus channels; no
Radio service or RF transmission path is linked.

The shared contract verifies on the real CPU and kernel scheduler that a full
inbound FIFO drops only the oldest record. The additional C2 check verifies
that pending node-attempt identifiers remain reserved until the configured
timeout, and that both timeout and ACK processing release an identifier for
reuse. The scenario does not use a persistent Settings backend and does not
prove peer delivery, retry behavior in MeshCore, LoRa ACK exchange, RF
behavior, or multi-device ordering.

Run the QEMU contract from the west workspace root::

   west twister -T sdk-meshbus/tests/subsys/meshbus/services/message \
     -p qemu_x86 --inline-logs -v -c

Build the C2 service-DUT image without flashing it::

   west build -p always -d /tmp/meshbus-message-c2-service-dut \
     -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
     sdk-meshbus/tests/subsys/meshbus/services/message -- \
     -DEXTRA_CONF_FILE=configs/service_dut.conf

Running the test on a device does not erase or write product Settings, but it
does replace the currently flashed application image.
