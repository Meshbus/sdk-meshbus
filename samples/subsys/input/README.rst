Meshbus Input Sample
####################

Select ``<qualified-board-target>`` from this sample's ``sample.yaml`` and
inspect the matching configuration/overlays. Additional boards require their
own integration and validation.

This sample runs the Meshbus input service and demonstrates how input events are
published over ZBus and consumed by application code.

The app subscribes to both channels with **async listeners**:

- ``mbs_input_raw_event_chan`` (raw input events)
- ``mbs_input_action_chan`` (processed input action events: short/long/scroll)
  where held directional keys publish repeated short actions.

Features
********

- Enables :kconfig:`CONFIG_MBS_INPUT` and :kconfig:`CONFIG_MBS_INPUT_SHELL`
- Supports button, encoder, and keypad input backends
- Provides shell injection commands for fast validation:

  - ``meshbus input inject act <type> <code> <action>``
  - ``meshbus input inject raw <type> <code> <value>``

- Demonstrates runtime ZBus subscription in ``src/main.c`` using
  :c:macro:`ZBUS_ASYNC_LISTENER_DEFINE`
- Includes runtime PM settings for boards using power-domain controlled input devices

Requirements
************

- Zephyr board with input devices enabled (buttons/encoder/keypad)
- DTS ``chosen`` nodes for meshbus input devices

  - ``meshbus,input-buttons``
  - ``meshbus,input-encoder``
  - ``meshbus,input-keypad``

- Check the selected overlay in ``boards/`` for the required chosen-node mappings.

Building
********

Activate your Zephyr development environment and run from the west workspace
root:

.. code-block:: shell

   west build -b '<qualified-board-target>' \
     meshbus/samples/subsys/input

Running
*******

After flashing, open serial shell and verify module status:

.. code-block:: shell

   meshbus input status

Inject processed key events:

.. code-block:: shell

   meshbus input inject act key 1 short
   meshbus input inject act key 1 long
   meshbus input inject act rel 8 cw
   meshbus input inject act rel 8 ccw

Inject raw input events:

.. code-block:: shell

   meshbus input inject raw 1 1 1
   meshbus input inject raw 1 1 0

Notes:

- action-event ``<type>`` accepts ``key``, ``rel``, ``abs``, ``msc`` or a number
- raw ``<type>``, ``<code>`` and ``<value>`` accept numeric arguments
- ``<action>`` accepts ``short``, ``long``, ``cw``, ``ccw``
  (also ``scroll_cw``/``scroll_ccw``)

Sample Output
*************

Boot logs are similar to:

.. code-block:: console

   [00:00:00.000] <inf> mbs_test: Meshbus test application started
   [00:00:00.001] <inf> mbs_test: Build timestamp: Feb 10 2026 20:00:00
   [00:00:00.020] <inf> mbs_test: Subscribed to mbs_input_raw_event_chan
   [00:00:00.021] <inf> mbs_test: Subscribed to mbs_input_action_chan

When events arrive (hardware or inject), logs include:

.. code-block:: console

   [00:00:10.100] <inf> mbs_test: input/raw: type=1 code=0x0001 value=1
   [00:00:10.950] <inf> mbs_test: input/act: type=1 code=0x0001 action=short(0)
