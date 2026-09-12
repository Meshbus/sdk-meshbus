ES4-AG1 VOC Sensor Sample
#########################

Overview
********

This sample reads EC Sense ES4-AG1 VOC data through the Zephyr sensor API.
The driver uses ``io-channels`` to sample ADC voltage from an external
transimpedance/amplifier front-end, then reports:

- ``SENSOR_CHAN_VOLTAGE`` in volts
- ``SENSOR_CHAN_VOC`` in ppm

Building and running
********************

From the west workspace root:

.. code-block:: sh

   west build -p always -b qikira_f9802 meshbus/samples/drivers/sensor/ecsense/es4_ag1

The sample overlay configures:

- ``ecsense,es4-ag1`` node on ``<&ads1110 0>``
- ``transimpedance-ohms = <220000>``
- ``zero-offset-microvolt = <0>``
- ``sensitivity-nanoamp-per-ppm = <55>``
- ``inverted-polarity`` for the inverting front-end on this board
- ``averaging-samples = <16>``

``zero-offset-microvolt`` can be tuned at runtime or in DTS and may be
negative for differential ADC setups.

To keep ADS1110 channel setup compatible with the local driver implementation,
the sample overrides channel gain to ``ADC_GAIN_1``.
