Meshbus Radio Sample
====================

This sample builds the Meshbus radio management layer (``CONFIG_MBS_RADIO``)
against the SDK radio drivers (including SX1262 and LoRa basics modem backend).
It keeps the Meshbus shell enabled, so ``meshbus radio`` commands can configure
links and trigger noise-floor calibration interactively.

Features
********

- Enables :kconfig:`CONFIG_MBS_RADIO`, :kconfig:`CONFIG_MBS_RADIO_SHELL`, and
  :kconfig:`CONFIG_MBS_STATS` via ``prj.conf``
- Includes platform overlays (``boards/devkit_nrf54l15_nrf54l15_cpuapp.overlay`` and
  ``idea_mesh_tracker_c2_nrf54l15_cpuapp.overlay``) that bind ``meshbus,radio`` to
  real SX1262 hardware
- Uses sample-level ``prj.conf`` to include diagnostics (shell, logging, stats)

Requirements
************

- LoRa backend support (``CONFIG_LORA`` and ``CONFIG_LORA_MODULE_BACKEND_LORA_BASICS_MODEM``)
- A board with ``chosen { meshbus,radio = &sx1262; }`` (provided in the overlays)
- Optional: the devkit_nrf54l15 overlay also configures SPI/GPIO pins to power the
  SX1262 modem

Building
********

From the workspace root run:

.. code-block:: shell

   west build -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
     meshbus/samples/subsys/radio

The resulting ``build/zephyr/zephyr.bin`` image can be flashed over USB and the
Meshbus shell inspected via UART. ``sample.yaml`` declares
``idea_mesh_tracker_c2/nrf54l15/cpuapp`` as the integration platform.

Running
*******

Use the shell to interact with the ``meshbus radio`` command tree. Key commands:

.. list-table::
   :header-rows: 1

   * - Command
     - Description
   * - ``meshbus radio status``
     - Shows modem state, receive activity, and signal stats
   * - ``meshbus radio config get``
     - Prints current radio parameters
   * - ``meshbus radio config set ...``
     - Applies a full radio parameter set; see shell help for arguments
   * - ``meshbus radio calibrate [threshold]``
     - Re-runs noise-floor collection
   * - ``meshbus radio enable`` / ``meshbus radio disable``
     - Enables or disables the radio

Example shell output (values depend on hardware):

.. code-block:: console

   [00:00:00.500] <inf> mbs_test: Meshbus test application started
   [00:00:00.500] <inf> mbs_test: Build timestamp: Feb  7 2026 00:00:00
   [00:00:00.600] <inf> mbs_radio: Settings apply: enabled=1 receive_only=0 rx_boosted=1 crc=1 duty_cycle=0
   [00:00:00.650] <inf> mbs_radio_mgmt: Noise floor calibrated: -113 dBm (threshold: 14 dB)

Use ``meshbus radio status`` after runtime to see RSSI, SNR, and noise floor states.
