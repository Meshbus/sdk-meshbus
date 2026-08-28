.. code-block:: none

   Meshbus Indicator Sample

This sample runs the meshbus indicator subsystem on a board with a chosen
`meshbus,indicator-light-active` LED and optional `meshbus,indicator-buzzer`
devices. It keeps the Meshbus shell enabled so you can inspect or adjust the
indicator configuration and control the visual/buzzer outputs in real time.

Features
********

- Enables :kconfig:`CONFIG_MESHBUS_INDICATOR`, :kconfig:`CONFIG_MESHBUS_INDICATOR_LIGHT`, and
  :kconfig:`CONFIG_MESHBUS_INDICATOR_BUZZER` via ``prj.conf``
- Keeps the Meshbus and ZBus shell interfaces built in (``CONFIG_MESHBUS_SHELL``,
  ``CONFIG_ZBUS``) for diagnostics and runtime control
- Provides platform overlays in ``boards/`` that map ``meshbus,indicator-*``
  chosen nodes to LEDs or PWM buzzers on supported devkits

Requirements
************

- A board that defines ``chosen { meshbus,indicator-light-active = &led0; }`` in its
  DTS or overlay (``boards/idea_mesh_tracker_c2_nrf54l15_cpuapp.overlay`` already
  does this)
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

The sample uses Zephyr's standard build flow. From the repository root run::

.. code-block:: shell

   west build -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
     sdk-meshbus/samples/subsys/meshbus/services/indicator

Flash a compatible board using the resulting image in ``build/zephyr/zephyr.bin``
and monitor the shell over UART/serial. ``sample.yaml`` declares
``idea_mesh_tracker_c2/nrf54l15/cpuapp`` as the integration platform.

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

Console logs when the app boots::

.. code-block:: console

   [00:00:00.000] <inf> meshbus_test: Meshbus test application started
   [00:00:00.000] <inf> meshbus_test: Build timestamp: Feb  7 2026 00:00:00
   [00:00:00.050] <inf> meshbus_indicator: Settings apply: light_enabled=1 light_heartbeat=1 buzzer_enabled=1 buzzer_dm=1 buzzer_channel=1 buzzer_system=1

Run ``shell`` commands such as ``meshbus indicator status`` to see additional
output like ``light_ready: yes`` and ``buzzer_ready: yes`` once the hardware is
initialized.
