.. zephyr:code-sample:: lora-rssi-inst
   :name: LoRa instantaneous RSSI
   :relevant-api: lora_interface

   Monitor the instantaneous RSSI while the LoRa modem is in receive mode.

Overview
********

This sample demonstrates how to use :c:func:`lora_rssi_inst` to query the
instantaneous RSSI (channel energy level) while the LoRa modem is running in
receive mode (asynchronous receive).

For a more dynamic setup, build and flash the accompanying LoRa send sample
:zephyr:code-sample:`lora-send` on another board within range.

Building and Running
********************

Select a supported target from ``sample.yaml`` with a ``lora0`` devicetree
alias and a driver supporting the operations exercised by this sample.

Build from the west workspace root::

   west build -b '<qualified-board-target>' meshbus/samples/drivers/lora/rssi_inst \
     -d 'build/<task>-lora'
   west flash -d 'build/<task>-lora'

Sample Output
=============

.. code-block:: console

   [00:00:00.220,000] <inf> lora_rssi_inst: Starting asynchronous reception (required for rssi_inst)
   [00:00:01.220,000] <inf> lora_rssi_inst: Instantaneous RSSI: -98 dBm
   [00:00:02.220,000] <inf> lora_rssi_inst: Instantaneous RSSI: -97 dBm

