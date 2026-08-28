.. code-block:: none

   Meshbus Input Sample

This sample runs the Meshbus input service and demonstrates how input events are
published over ZBus and consumed by application code.

The app subscribes to both channels with **async listeners**:

- ``meshbus_input_key_chan`` (raw input events)
- ``meshbus_input_action_chan`` (processed input action events: short/long/scroll)
  where held directional keys publish repeated short actions.

Features
********

- Enables :kconfig:`CONFIG_MESHBUS_INPUT` and :kconfig:`CONFIG_MESHBUS_INPUT_SHELL`
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

- For the included SDK board overlay
  ``boards/idea_mesh_tracker_c2_nrf54l15_cpuapp.overlay``, these chosen nodes are
  already mapped.

Building
********

From repository root::

.. code-block:: shell

   west build -b idea_mesh_tracker_c2/nrf54l15/cpuapp sdk-meshbus/samples/subsys/meshbus/services/input

Running
*******

After flashing, open serial shell and verify module status::

.. code-block:: shell

   meshbus input status

Inject processed key events::

.. code-block:: shell

   meshbus input inject key short 1
   meshbus input inject key long 1
   meshbus input inject key cw 8
   meshbus input inject key ccw 8

Inject raw input events::

.. code-block:: shell

   meshbus input inject raw 1 1 1
   meshbus input inject raw 1 1 0

Notes:

- ``<type>``/``<code>``/``<value>`` are decimal arguments
- key ``<type>`` accepts ``short``, ``long``, ``cw``, ``ccw``
  (also ``scroll_cw``/``scroll_ccw``)

Sample Output
*************

Boot logs are similar to::

.. code-block:: console

   [00:00:00.000] <inf> meshbus_test: Meshbus test application started
   [00:00:00.001] <inf> meshbus_test: Build timestamp: Feb 10 2026 20:00:00
   [00:00:00.020] <inf> meshbus_test: Subscribed to meshbus_input_key_chan
   [00:00:00.021] <inf> meshbus_test: Subscribed to meshbus_input_action_chan

When events arrive (hardware or inject), logs include::

.. code-block:: console

   [00:00:10.100] <inf> meshbus_test: input/raw: type=1 code=0x0001 value=1
   [00:00:10.950] <inf> meshbus_test: input/act: type=1 code=0x0001 action=short(1)
