NTC Thermistor Sample (qikira_f9802)
####################################

Overview
********

This sample reads ambient temperature from the board-level NTC node exposed as
``ambient-temp0``.

On ``qikira_f9802``, the DTS is configured for:

- ``GPIO0`` (ESP32-C3 ADC1 channel 0) as ``NTC_P``
- Pull-up resistor ``10k`` to ``3.3V``
- NTC type ``R25=10k``, ``B=3435K`` via ``ntc-thermistor-generic``

Building and running
********************

From the west workspace root:

.. code-block:: sh

   source ~/.zephyr/env/bin/activate
   west build -p always -b qikira_f9802 meshbus/samples/drivers/sensor/ntc_thermistor
   west flash
   west espressif monitor

Expected log output:

.. code-block:: text

   NTC thermistor sample started
   Sensor device: ntc_p
   NTC temperature: 25.000000 C
