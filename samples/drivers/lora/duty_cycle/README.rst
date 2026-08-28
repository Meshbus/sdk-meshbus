.. zephyr:code-sample:: lora-duty-cycle
   :name: LoRa duty-cycle receive
   :relevant-api: lora_interface

   Receive packets using LoRa RX duty-cycle mode.

Overview
********

This sample demonstrates receiver-only duty-cycle mode with
``lora_recv_duty_cycle_async()``.

Default radio parameters match the existing SDK LoRa samples:

* Frequency: ``915125000``
* Bandwidth: ``BW_125_KHZ``
* Datarate: ``SF_12``
* Coding rate: ``CR_4_8``

The duty-cycle defaults are tuned for easy interop with
``sdk-meshbus/samples/drivers/lora/send``:

* RX period: ``100 ms``
* Sleep period: ``100 ms``

Building and Running
********************

Use two boards:

1. Board A runs :zephyr:code-sample:`lora-send`.
2. Board B runs this duty-cycle receive sample.

Build and flash this sample:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/lora/duty_cycle
   :host-os: unix
   :board: idea_mesh_tracker_c2/nrf54l15/cpuapp
   :goals: build flash
   :compact:

Sample Output
*************

.. code-block:: console

   [00:00:00.235,000] <inf> lora_duty_cycle: RX duty-cycle started (rx=100 ms, sleep=100 ms)
   [00:00:01.456,000] <inf> lora_duty_cycle: RX 12 bytes, RSSI: -55 dBm, SNR: 9 dB
   [00:00:01.456,000] <inf> lora_duty_cycle: payload
