.. zephyr:code-sample:: lora-duty-cycle
   :name: LoRa duty-cycle receive
   :relevant-api: lora_interface

   Receive packets using LoRa RX duty-cycle mode.

Overview
********

This sample demonstrates receiver-only duty-cycle mode with
``lora_recv_duty_cycle_async()``.

Default radio parameters match the LoRa send sample:

* Frequency: ``915125000``
* Bandwidth: ``BW_125_KHZ``
* Datarate: ``SF_8``
* Preamble length: ``96`` symbols
* Coding rate: ``CR_4_7``

The duty-cycle defaults are tuned for easy interop with
``meshbus/samples/drivers/lora/send``:

* RX period: ``73 ms``
* Sleep period: ``141 ms``

Building and Running
********************

Use two boards:

1. Board A runs :zephyr:code-sample:`lora-send`.
2. Board B runs this duty-cycle receive sample.

Choose a target from ``sample.yaml`` with a compatible radio and duty-cycle
driver support. Replace the target and task placeholders below.

Build from the west workspace root::

   west build -b '<qualified-board-target>' meshbus/samples/drivers/lora/duty_cycle \
     -d 'build/<task>-lora'
   west flash -d 'build/<task>-lora'

Sample Output
*************

.. code-block:: console

   [00:00:00.235,000] <inf> lora_duty_cycle: RX duty-cycle started (rx=73 ms, sleep=141 ms)
   [00:00:01.456,000] <inf> lora_duty_cycle: RX 12 bytes, RSSI: -55 dBm, SNR: 9 dB
   [00:00:01.456,000] <inf> lora_duty_cycle: payload
