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

Build and flash the sample as follows, changing ``b_l072z_lrwan1`` for your
board, where your board has a ``lora0`` alias in the devicetree.

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/lora/cad
   :host-os: unix
   :board: b_l072z_lrwan1
   :goals: build flash
   :compact:

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

