Meshbus Telemetry Sample
########################

This sample starts the Meshbus telemetry service and subscribes to
``mbs_telemetry_data_chan``. On real hardware it prints live sensor channel
events to the log.

Purpose
*******

- Start ``CONFIG_MBS_TELEMETRY`` with real board sensor devices.
- Bind the board telemetry channel table through ``chosen { meshbus,telemetry = ...; }``.
- Log telemetry ZBus events from application code.
- Keep ``meshbus telemetry`` shell commands available for manual validation.

This sample is not an automated ztest. Use
``meshbus/tests/subsys/telemetry`` for public API and public ZBus
contract coverage.

Requirements
************

- A board overlay that defines a ``fobe,meshbus-telemetry`` node.
- Sensor devices referenced by the telemetry channel children.
- ``CONFIG_SENSOR=y`` and any required board sensor drivers.

The sample-local ``idea_mesh_tracker_c2/nrf54l15/cpuapp`` overlay declares and
maps the optional product sensors; they are not part of the base board DTS:

- ``lps22`` ambient temperature and pressure
- ``lsm6ds3tr`` LSM6DS3TR-C accelerometer and gyroscope
- ``mmc5603`` magnetometer

Building
********

From the west workspace root::

  source ~/.zephyr/env/bin/activate
  west build -p always -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
    meshbus/samples/subsys/telemetry

Running
*******

After flashing, open the serial shell and inspect the service:

.. code-block:: shell

   meshbus telemetry status
   meshbus telemetry config get

When samples are published, logs include lines like:

.. code-block:: console

   telemetry: ts=1234 chan=13 values=(24.125000)

The exact channel IDs and values depend on the board sensors and current
runtime configuration.
