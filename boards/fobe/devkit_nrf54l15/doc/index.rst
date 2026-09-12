.. zephyr:board:: devkit_nrf54l15

Overview
********

The FoBE DevKit nRF54L15 is a development board for the Nordic nRF54L15 SoC.
It exposes an Arduino R3-compatible expansion header, a user button, and a
115200 baud UART console. The board target supports the application CPU and the
FLPR coprocessor, including the FLPR RRAM XIP variant.


Hardware
********

- 128 MHz Arm® Cortex®-M33 processor
- Scalable memory configurations up to 1.5 MB NVM and up to 256 KB RAM
- Multiprotocol 2.4 GHz radio supporting Bluetooth Low Energy, 802.15.4-2020,
  and 2.4 GHz proprietary modes (up to 4 Mbps)
- Comprehensive set of peripherals including new Global RTC available in System OFF,
  14-bit ADC, and high-speed serial interfaces
- 128 MHz RISC-V coprocessor
- Advanced security including TrustZone® isolation, tamper detection,
  and cryptographic engine side-channel leakage protection


For more information about the nRF54L15 SoC, refer to these documents:

- `nRF54L15 Website`_
- `nRF54L15 Datasheet`_

Supported Features
==================

.. zephyr:board-supported-hw::

Connections and IOs
===================

In the following table, the column **Name** contains Pin names. For example, P2_0
means Pin number 0 on PORT2, as used in the board's datasheets and manuals.

+-------+-------------+------------------+
| Name  | Function    | Usage            |
+=======+=============+==================+
| P0_2  | UART30_TX   | UART Console TX  |
+-------+-------------+------------------+
| P0_3  | UART30_RX   | UART Console RX  |
+-------+-------------+------------------+
| P0_4  | GPIO        | User button      |
+-------+-------------+------------------+

SX1262 Arduino Shield
=====================

The Meshbus application development profile connects an SX1262 shield as
follows. The shield is an application-level hardware combination and is not
part of the generic board definition.

+--------+--------+-------------------+
| Signal | SoC pin| Arduino header pin|
+========+========+===================+
| SCK    | P2.06  | D13               |
+--------+--------+-------------------+
| MISO   | P2.09  | D12               |
+--------+--------+-------------------+
| MOSI   | P2.08  | D11               |
+--------+--------+-------------------+
| NSS    | P2.07  | D10               |
+--------+--------+-------------------+
| RESET  | P2.10  | D9                |
+--------+--------+-------------------+
| DIO1   | P1.08  | D8                |
+--------+--------+-------------------+
| BUSY   | P1.14  | D7                |
+--------+--------+-------------------+
| RXEN   | P1.13  | D6                |
+--------+--------+-------------------+

The SX1262 DIO2 pin controls TX and DIO3 supplies the 1.8 V TCXO control
voltage. The initial SPI clock is limited to 2 MHz for conservative bring-up.


Programming and Debugging
*************************

.. zephyr:board-supported-runners::

Use a supported debug probe with the OpenOCD, pyOCD, J-Link, nRF Util, or
nRFJProg runner. The board console is exposed separately through UART30.

Flashing
========

Here is an example for the :zephyr:code-sample:`hello_world` application.

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: devkit_nrf54l15/nrf54l15/cpuapp
   :goals: flash

Open a serial terminal connected to the UART30 serial port.

Reset the board and you should see the following message in the terminal:

.. code-block:: console

   Hello World! devkit_nrf54l15/nrf54l15/cpuapp

.. _nRF54L15 Website:
   https://www.nordicsemi.com/Products/nRF54L15

.. _nRF54L15 Datasheet:
   https://docs.nordicsemi.com/bundle/ps_nrf54L15/page/keyfeatures_html5.html
