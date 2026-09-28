Meshbus Radio Sample
====================

Select ``<qualified-board-target>`` from this sample's ``sample.yaml`` and
inspect the matching configuration/overlays. Additional boards require their
own integration and validation.

This sample builds the Meshbus radio management layer (``CONFIG_MBS_RADIO``)
against the SDK radio drivers (including SX1262 and LoRa basics modem backend).
It keeps the Meshbus shell enabled, so ``meshbus radio`` commands can configure
links and trigger noise-floor calibration interactively.

Features
********

- Enables :kconfig:`CONFIG_MBS_RADIO`, :kconfig:`CONFIG_MBS_RADIO_SHELL`, and
  :kconfig:`CONFIG_MBS_STATS` via ``prj.conf``
- Includes platform overlays in ``boards/`` that bind ``meshbus,radio`` to
  real SX1262 hardware
- Uses sample-level ``prj.conf`` to include diagnostics (shell, logging, stats)

Requirements
************

- LoRa backend support (``CONFIG_LORA`` and ``CONFIG_LORA_MODULE_BACKEND_LORA_BASICS_MODEM``)
- A board with ``chosen { meshbus,radio = &sx1262; }`` (provided in the overlays)
- SPI/GPIO and power configuration matching the attached modem

Building
********

From the workspace root run:

.. code-block:: shell

   west build -b '<qualified-board-target>' \
     meshbus/samples/subsys/radio

Use the selected target's runner to flash the resulting image and inspect the
Meshbus shell on its configured console. ``sample.yaml`` declares supported
integration platforms.

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
