Meshbus Power Sample
####################

Select ``<qualified-board-target>`` from this sample's ``sample.yaml`` and
inspect the matching configuration/overlays. Additional boards require their
own integration and validation.

This sample starts the Meshbus power service on a board with power-management
hardware. It keeps the serial shell enabled so fuel-gauge, charger, wakeup,
shutdown, and reboot behavior can be validated manually.

Purpose
*******

- Start ``CONFIG_MBS_POWER`` with real board devices.
- Bind the board fuel gauge and power button through devicetree chosen nodes.
- Expose ``meshbus power`` shell commands for manual validation.
- Keep logs enabled for service state, settings, and PM diagnostics.

This sample is not an automated ztest. Use
``meshbus/tests/subsys/power`` for public API and public ZBus
contract coverage.

Requirements
************

- A board overlay with:

  - ``chosen { meshbus,fuel-gauge = &fuel_gauge; }``
  - ``chosen { meshbus,power-button = &button0; }``

- Fuel gauge and charger support if the board has those devices:

  - ``CONFIG_FUEL_GAUGE=y``
  - ``CONFIG_CHARGER=y``

- Power-off and reboot support for manual destructive tests:

  - ``CONFIG_POWEROFF=y``
  - ``CONFIG_REBOOT=y``

Building
********

Activate your Zephyr development environment, then build from the west
workspace root::

  west build -p always -b '<qualified-board-target>' \
    meshbus/samples/subsys/power

Running
*******

After flashing, open the serial shell and inspect the service:

.. code-block:: shell

   meshbus power status
   meshbus power config get

Use shutdown and reboot commands only when the board is in a safe state for
power-cycle testing.

Expected boot logs include the sample startup line and Meshbus power settings
application logs. Fuel-gauge values depend on the attached board hardware and
power state.
