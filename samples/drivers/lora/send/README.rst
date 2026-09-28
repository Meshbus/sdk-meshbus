.. zephyr:code-sample:: lora-send
   :name: LoRa send
   :relevant-api: lora_interface

   Transmit a preconfigured payload every second using the LoRa radio.

Overview
********

This sample demonstrates how to use the LoRa radio driver to configure
the encoding settings and send data over the radio.

Transmitted messages can be received by building and flashing the accompanying
LoRa receive sample :zephyr:code-sample:`lora-receive` on another board within
range.

Building and Running
********************

Select a supported target from ``sample.yaml`` with a ``lora0`` devicetree
alias and a driver supporting the operations exercised by this sample.

Build from the west workspace root::

   west build -b '<qualified-board-target>' meshbus/samples/drivers/lora/send \
     -d 'build/<task>-lora'
   west flash -d 'build/<task>-lora'

Sample Output
=============

.. code-block:: console

    [00:00:00.531,000] <inf> lora_send: Data sent 0!
    [00:00:01.828,000] <inf> lora_send: Data sent 1!
    [00:00:03.125,000] <inf> lora_send: Data sent 2!
