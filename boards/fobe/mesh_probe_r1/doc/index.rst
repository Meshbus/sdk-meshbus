.. zephyr:board:: mesh_probe_r1

Overview
********

Mesh Probe R1 is a FoBE Studio board based on the Nordic Semiconductor
nRF52840. It combines a Semtech SX1262 LoRa radio, a Quectel L76K GNSS receiver,
and a 128 x 64 SSD1306 OLED display with a rotary encoder, two buttons, and a
blue LED.

The board definition is provided by the Meshbus Zephyr module. Include this
module in the workspace so Zephyr can discover the board and its additional
devicetree bindings. The board target is ``mesh_probe_r1``.

Hardware
********

The board definition describes the following hardware:

* nRF52840 Arm Cortex-M4F SoC with 1 MiB flash and 256 KiB RAM.
* SoC Bluetooth LE and IEEE 802.15.4 radio support.
* SX1262 on SPI2, with DIO2 RF-switch control and a 1.8 V DIO3-controlled TCXO.
* L76K GNSS receiver on UART0 at 115200 baud, with reset, wakeup, and PPS signals.
* SSD1306 OLED on I2C0 at address ``0x3d``.
* GPIO rotary encoder and push button, USR button, and active-low blue LED.
* Battery-voltage measurement on ADC channel 3 and a charging-status input.
* Separate GPIO-controlled power domains for GNSS and the display/encoder.
* USB device controller with a CDC ACM console.

Supported Features
==================

.. zephyr:board-supported-hw::

Applications must enable the Kconfig drivers for the peripherals they use.
An enabled devicetree node does not by itself enable its driver. Application
overlays can change the board defaults.

Connections and IOs
===================

The tables below use SoC port and pin numbers, not connector positions.
TX and RX are named from the nRF52840 perspective.

.. list-table:: Peripheral interfaces
   :header-rows: 1
   :widths: 20 45 35

   * - Peripheral
     - Pins
     - Board use
   * - UART0
     - TX: P1.09; RX: P0.12
     - L76K GNSS, 115200 baud
   * - I2C0
     - SCL: P0.27; SDA: P0.07
     - SSD1306 at ``0x3d``; bus configured for 400 kHz
   * - I2C1
     - SCL: P1.10; SDA: P1.14
     - Enabled bus; no child devices in the base board definition
   * - SPI2
     - SCK: P0.20; MOSI: P0.22; MISO: P0.24; CS: P1.08
     - SX1262; maximum SPI frequency 8 MHz
   * - PWM0
     - Channel 0: P0.14
     - PWM output

.. list-table:: Control and input signals
   :header-rows: 1
   :widths: 30 20 50

   * - Signal
     - Pin
     - Configuration
   * - Blue LED (``led0``)
     - P1.11
     - Active low
   * - USR button (``button0``)
     - P1.00
     - Active low with pull-up; ``INPUT_KEY_0``
   * - Encoder push button
     - P1.04
     - Active low; ``INPUT_KEY_1``
   * - Encoder A / B
     - P1.06 / P1.02
     - ``INPUT_REL_WHEEL``; four steps per period
   * - Display/encoder power enable
     - P0.16
     - Active high; 10 ms startup delay
   * - GNSS power enable
     - P0.26
     - Active high; 10 ms startup delay
   * - GNSS reset / wakeup
     - P0.04 / P0.06
     - Active low / active high
   * - GNSS PPS
     - P0.08
     - Active high
   * - SX1262 reset
     - P0.13
     - Active low
   * - SX1262 BUSY / DIO1
     - P0.15 / P0.17
     - Active high
   * - SX1262 antenna enable
     - P0.11
     - Active high
   * - Charging status
     - P1.12
     - Active low

The battery divider uses a 1 MOhm upper resistor and a 1.5 MOhm lower resistor,
feeding SAADC input AIN3. The composite fuel gauge uses the default
lithium-ion-polymer open-circuit-voltage curve and a configured design capacity
of 1500 mAh. These are software estimation parameters, not a measurement of the
installed battery's capacity. The composite charger uses the charging-status
input and a configured maximum charge voltage of 4.20 V.

The board provides ``led0``, ``button0``, and ``watchdog0`` aliases, together
with MCUboot LED/button aliases. ``zephyr,display`` selects the OLED.

USB console
===========

The default console and shell use ``board_cdc_acm_uart`` on the USB device
controller. The shared board Kconfig enables the CDC ACM serial backend and
USB initialization at boot for ordinary applications. Connect the board's USB
device port to the host with a data cable and open the enumerated serial port.
For the shell, use a terminal that asserts DTR.

UART0 is connected to the GNSS receiver and is not the default console.
Changing console transport requires corresponding devicetree and Kconfig
changes in the application.

Flash layout
============

The base devicetree defines the following regions. Offsets and sizes are in
bytes relative to the start of internal flash.

.. list-table:: Default flash partitions
   :header-rows: 1
   :widths: 40 30 30

   * - Partition label
     - Offset
     - Size
   * - ``mbr``
     - ``0x00000000``
     - ``0x00001000``
   * - ``softdevice``
     - ``0x00001000``
     - ``0x00026000``
   * - ``app``
     - ``0x00027000``
     - ``0x000c3000``
   * - ``storage``
     - ``0x000ea000``
     - ``0x00004000``
   * - ``lfs_storage``
     - ``0x000ee000``
     - ``0x00006000``
   * - ``bootloader``
     - ``0x000f4000``
     - ``0x0000a000``
   * - ``mbr_params``
     - ``0x000fe000``
     - ``0x00001000``
   * - ``bootloader_settings``
     - ``0x000ff000``
     - ``0x00001000``

``zephyr,code-partition`` selects ``app``. The board defconfig does not enable
``CONFIG_USE_DT_CODE_PARTITION``; explicitly enable it when building an
application for this layout. Partition declarations do not install an MBR,
SoftDevice, or bootloader. An image linked at ``0x27000`` requires a compatible
installed boot chain to start it.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

Run the examples from the west workspace root with the Meshbus module included
in the active manifest. See :ref:`getting_started` and
:ref:`build_an_application` for general setup.

Flashing
========

The following example builds :zephyr:code-sample:`blinky` for the declared
application partition and generates a UF2 image:

.. code-block:: console

   west build -b mesh_probe_r1 zephyr/samples/basic/blinky -d build/mesh-probe-r1-blinky -- -DCONFIG_USE_DT_CODE_PARTITION=y -DCONFIG_BUILD_OUTPUT_UF2=y

Use this image only with an installed bootloader compatible with the layout
above. Enter that bootloader's USB mass-storage mode using its documented
procedure and mount the volume. The board's UF2 runner matches the board ID
``nRF52840-FoBEF2102-rev1a``:

.. code-block:: console

   west flash -d build/mesh-probe-r1-blinky --runner uf2

The generated image is ``build/mesh-probe-r1-blinky/zephyr/zephyr.uf2``.
Once the bootloader starts the application, the blue LED should blink.
UF2 support in the runner configuration does not establish which bootloader
is currently installed on a particular device.

Alternatively, connect a compatible SWD probe and power the board to program
the same application with pyOCD:

.. code-block:: console

   west flash -d build/mesh-probe-r1-blinky --runner pyocd

The board selects pyOCD target ``nrf52840`` at 4 MHz. This programs the
application; it does not provision the boot chain required by the offset
image. Avoid a full-chip erase when retaining the existing bootloader and
stored data. For product firmware, use the product's build and provisioning
instructions, since its overlays may replace this partition layout.

Debugging
=========

With the same SWD probe and build directory, start a GDB session:

.. code-block:: console

   west debug -d build/mesh-probe-r1-blinky --runner pyocd

J-Link is also configured, using device ``nRF52840_xxAA`` at 4 MHz. Select the
runner matching the connected probe. UF2 provides image transfer only; it
does not provide a debugging connection. See :ref:`west-flashing` for runner
selection and options.

References
**********

* `nRF52840 product information <https://www.nordicsemi.com/Products/nRF52840>`_
* `Zephyr board porting guide <https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html>`_

The adjacent ``board.yml``, DTS/DTSI, pinctrl, Kconfig, and ``board.cmake`` files
are the sources for board defaults. Inspect the generated ``zephyr.dts`` and
``.config`` in the selected application's build directory for its effective
configuration.
