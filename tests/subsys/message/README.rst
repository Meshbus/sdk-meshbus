Message service contract and runtime validation
===============================================

Select ``<qualified-board-target>`` from the service-DUT scenario in
``testcase.yaml`` and use its exact configuration and fixture. These commands
do not extend platform support or authorize device execution.


This application defines two Twister scenarios:

* ``subsys.meshbus.message.contract`` runs the public Message API and ZBus
  contract on ``qemu_x86``.
* The service-DUT scenario in ``testcase.yaml`` runs the same hardware-safe
  contract on its declared platform and adds focused runtime checks.

Both scenarios wrap MeshCore configuration, Contact lookup, and Channel lookup
at their public dependency boundaries. The test-only radio-ready Kconfig
option stops outbound requests at the public Message request ZBus channels; no
Radio service or RF transmission path is linked.

The shared contract verifies on the real CPU and kernel scheduler that a full
inbound FIFO drops only the oldest record. The additional service-DUT check verifies
that pending node-attempt identifiers remain reserved until the configured
timeout, and that both timeout and ACK processing release an identifier for
reuse. The scenario does not use a persistent Settings backend and does not
prove peer delivery, retry behavior in MeshCore, LoRa ACK exchange, RF
behavior, or multi-device ordering.

Run the QEMU contract from the west workspace root::

   west twister -T meshbus/tests/subsys/message \
     -p qemu_x86 --inline-logs -v -c

Build the service-DUT image without flashing it::

   west build -p always -d /tmp/meshbus-message-dut-service-dut \
     -b '<qualified-board-target>' \
     meshbus/tests/subsys/message -- \
     -DEXTRA_CONF_FILE=configs/service_dut.conf

Running the test on a device does not erase or write product Settings, but it
does replace the currently flashed application image.
