NTC Heater Control Sample (qikira_f9802)
########################################

Overview
********

This sample implements a closed-loop heater controller using:

- ``ambient-temp0`` (NTC thermistor sensor)
- ``heater-pwm0`` (``LEDC_CH0`` on ``IO1`` / ``HEATER_EN``)

Control strategy is hysteresis only (no PID):

- ``temp <= target - hyst``: heater ON
- ``temp >= target + hyst``: heater OFF
- in-band: hold previous state

Safety behavior:

- Sensor read failure forces heater OFF.
- Over-temperature cutoff forces heater OFF.
- Startup forces heater OFF until the first valid temperature sample is read.

Default runtime parameters
**************************

- ``target`` = ``45.0`` C
- ``hyst`` = ``1.0`` C
- ``max_duty`` = ``8`` %
- ``cutoff`` = ``50.0`` C
- ``period_ms`` = ``250``
- ``enabled`` = ``on``

Runtime behavior
****************

- Shell control is disabled in this variant.
- Heater status is printed periodically by the main loop.
- Change defaults in ``src/main.c`` and rebuild when retuning is required.

Building and running
********************

From the west workspace root:

.. code-block:: sh

   source ~/.zephyr/env/bin/activate
   west build -p always -b qikira_f9802 meshbus/samples/drivers/sensor/ntc_heater_control
   west flash
   west espressif monitor
