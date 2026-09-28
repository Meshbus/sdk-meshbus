.. zephyr:code-sample:: lora-cad
   :name: LoRa CAD
   :relevant-api: lora_interface

   Demonstrate LoRa Channel Activity Detection (CAD) in both synchronous and
   asynchronous mode.

Overview
********

This sample demonstrates how to use the LoRa radio driver to run Channel
Activity Detection (CAD) using :c:func:`lora_cad` and :c:func:`lora_cad_async`.

Building and Running
********************

Select a supported target from ``sample.yaml`` with a ``lora0`` devicetree
alias and a driver supporting the operations exercised by this sample.

Build from the west workspace root::

   west build -b '<qualified-board-target>' meshbus/samples/drivers/lora/cad \
     -d 'build/<task>-lora'
   west flash -d 'build/<task>-lora'

Sample Output
=============

If the driver does not support CAD:

.. code-block:: console

   <inf> lora_cad: CAD not supported by this driver

If CAD is supported:

.. code-block:: console

   <inf> lora_cad: CAD supported
   <inf> lora_cad: Synchronous CAD result: channel free
   <inf> lora_cad: Asynchronous CAD result: channel busy

