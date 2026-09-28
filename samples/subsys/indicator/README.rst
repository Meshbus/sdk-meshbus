Meshbus Indicator Sample
########################

Select ``<qualified-board-target>`` from this sample's ``sample.yaml`` and
inspect the matching configuration/overlays. Additional boards require their
own integration and validation.

This sample runs the meshbus indicator subsystem on a board with a chosen
``meshbus,indicator-light-active`` LED and optional ``meshbus,indicator-buzzer``
devices. It keeps the Meshbus shell enabled so you can inspect or adjust the
indicator configuration and control the visual/buzzer outputs in real time.

Features
********

- Enables :kconfig:`CONFIG_MBS_INDICATOR`, :kconfig:`CONFIG_MBS_INDICATOR_LIGHT`, and
  :kconfig:`CONFIG_MBS_INDICATOR_BUZZER` through service defaults and the board configuration
- Keeps the Meshbus shell and ZBus event interfaces built in (``CONFIG_MBS_SHELL``,
  ``CONFIG_ZBUS``) for diagnostics and runtime control
- Provides platform overlays in ``boards/`` that map ``meshbus,indicator-*``
  chosen nodes to LEDs or PWM buzzers on supported targets

Requirements
************

- A board that defines ``chosen { meshbus,indicator-light-active = &led0; }`` in its
  DTS or overlay
- Optional buzzer chosen node ``meshbus,indicator-buzzer`` mapped to a PWM device
- Optional power-domain chosen node ``meshbus,indicator-buzzer-power`` mapped to a
  power-domain device; when present, the indicator service uses runtime PM
  ``pm_device_runtime_get()``/``pm_device_runtime_put()`` to keep the shared
  domain on while ``buzzer_enabled`` is true (and releases it when
  ``buzzer_enabled`` becomes false)
- Optional power-domain chosen node ``meshbus,indicator-light-power`` mapped to a
  power-domain device; when present, the indicator service uses runtime PM
  ``pm_device_runtime_get()``/``pm_device_runtime_put()`` to keep the shared
  domain on while ``light_enabled`` is true (and releases it when
  ``light_enabled`` becomes false)

Building
********

The sample uses Zephyr's standard build flow. From the west workspace root run:

.. code-block:: shell

   west build -b '<qualified-board-target>' \
     meshbus/samples/subsys/indicator

Flash a compatible board using the resulting image in ``build/zephyr/zephyr.bin``
and monitor the configured console. Supported integration platforms are declared
in ``sample.yaml``.

Running
*******

By default the Meshbus shell commands (``meshbus indicator ...``) are available on
the console. Use ``meshbus indicator status`` to check hardware readiness and
``meshbus indicator config get``/``set`` to inspect or change LED/buzzer feedback
categories. The indicator service controls the light/buzzer automatically when
meshbus messages arrive or when system events (like power actions) happen, so
simply sending input or calling ``meshbus indicator light play <on_ms> <off_ms>
<count>`` can exercise the hardware.

Sample Output
*************

Console logs when the app boots:

.. code-block:: console

   [00:00:00.000] <inf> mbs_test: Meshbus test application started
   [00:00:00.000] <inf> mbs_test: Build timestamp: Feb  7 2026 00:00:00
   [00:00:00.050] <inf> mbs_indicator: Settings apply: light_enabled=1 light_heartbeat=1 buzzer_enabled=1 buzzer_dm=1 buzzer_channel=1 buzzer_system=1

Run ``shell`` commands such as ``meshbus indicator status`` to see additional
output like ``light_ready: yes`` and ``buzzer_ready: yes`` once the hardware is
initialized.
