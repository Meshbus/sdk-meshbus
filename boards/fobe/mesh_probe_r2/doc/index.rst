.. zephyr:board:: mesh_probe_r2

Overview
********

Mesh Probe R2 is a FoBE Studio board based on the Nordic Semiconductor
nRF54L15. It combines an application processor and a RISC-V Fast Lightweight
Peripheral Processor (FLPR) with a Semtech SX1262 LoRa radio, a Quectel L76K
GNSS receiver, and a 128 x 64 SSD1306 OLED display. A rotary encoder, two
buttons, and a blue LED provide local interaction.

The board definition is provided by the Meshbus Zephyr module. It must be
available in the workspace for Zephyr to discover the board and its additional
devicetree bindings.

Hardware
********

The board devicetree describes the following peripherals:

* nRF54L15 application core (Arm Cortex-M33) and RISC-V FLPR core.
* SoC Bluetooth LE and IEEE 802.15.4 radio support on the application core.
* SX1262 on SPI00, with DIO2 RF-switch control and a 1.8 V DIO3-controlled TCXO.
* L76K GNSS receiver on UART22 at 115200 baud.
* SSD1306 OLED on I2C20 at address ``0x3d``.
* GPIO rotary encoder and push button, USR button, and active-low blue LED.
* Battery-voltage divider on ADC channel 4 and a USB VBUS presence input.
* A GPIO-controlled peripheral power domain for the display, GNSS receiver,
  and encoder.

Supported Features
==================

.. zephyr:board-supported-hw::

Peripheral availability depends on the selected target and application
configuration. An enabled devicetree node still requires the corresponding
Kconfig driver option. Applications may override the board defaults through
overlays.

Board targets
=============

.. list-table:: Available targets
   :header-rows: 1
   :widths: 55 45

   * - Target
     - Execution environment
   * - ``mesh_probe_r2/nrf54l15/cpuapp``
     - Application core; use this target for the examples below.
   * - ``mesh_probe_r2/nrf54l15/cpuapp/ns``
     - Non-secure application core, with TF-M enabled by default.
   * - ``mesh_probe_r2/nrf54l15/cpuflpr``
     - FLPR core, executing from SRAM.
   * - ``mesh_probe_r2/nrf54l15/cpuflpr/xip``
     - FLPR core, executing in place from RRAM.

FLPR applications need sysbuild to include the application-core
``vpr_launcher`` image. Peripherals shared between the two cores must have
consistent ownership in the application configuration.

The non-secure target disables UART30 in its devicetree so TF-M can use it.
Do not assume that the default application-console instructions below also
apply to a non-secure application.

Connections and IOs
===================

The following tables use SoC port and pin numbers, not connector positions.
TX and RX are named from the nRF54L15 perspective.

.. list-table:: Serial interfaces
   :header-rows: 1
   :widths: 20 45 35

   * - Peripheral
     - Pins
     - Board use
   * - UART30
     - TX: P0.04; RX: P0.03
     - Console, 115200 baud
   * - UART22
     - TX: P1.10; RX: P1.09
     - L76K GNSS, 115200 baud
   * - I2C20
     - SCL: P1.04; SDA: P1.05
     - SSD1306 at ``0x3d``; bus configured for 1 MHz
   * - I2C21
     - SCL: P1.03; SDA: P1.02
     - Enabled bus; no child devices in the base board definition
   * - SPI00
     - SCK: P2.01; MOSI: P2.02; MISO: P2.04; CS: P2.00
     - SX1262; maximum SPI frequency 8 MHz
   * - PWM20
     - Channel 0: P1.12
     - PWM output

.. list-table:: Control and input signals
   :header-rows: 1
   :widths: 30 20 50

   * - Signal
     - Pin
     - Configuration
   * - Blue LED (``led0``)
     - P2.07
     - Active low
   * - USR button (``button0``)
     - P0.02
     - Active low with pull-up; ``INPUT_KEY_0``
   * - Encoder push button
     - P1.06
     - Active low; ``INPUT_KEY_1``
   * - Encoder A / B
     - P0.01 / P0.00
     - ``INPUT_REL_WHEEL``; four steps per period
   * - Peripheral power enable
     - P2.10
     - Active high; 10 ms startup delay
   * - GNSS reset / wakeup
     - P2.08 / P2.09
     - Active low / active high
   * - SX1262 reset
     - P2.03
     - Active low
   * - SX1262 BUSY / DIO1
     - P1.07 / P1.08
     - Active high
   * - SX1262 RX enable
     - P2.05
     - Active high
   * - USB VBUS presence
     - P2.06
     - Active high

The battery divider uses a 1 MOhm upper resistor and a 1.5 MOhm lower resistor,
feeding SAADC input AIN4. The composite fuel gauge uses a generic protected
single-cell 4.20 V lithium-ion capacity curve. Its percentage is an estimate;
the configured empty point is not the cell protection cutoff. The composite
charger reports external-power presence from the VBUS input.

The default aliases include ``led0``, ``button0``, ``lora0``, and ``watchdog0``.
On the application-core target, ``zephyr,display`` selects the OLED and
``zephyr,console`` selects UART30.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

Run the commands below from the west workspace root, with the Meshbus module
included in the active manifest. For general setup, see :ref:`getting_started`
and :ref:`build_an_application`.

Flashing
========

Build the :zephyr:code-sample:`hello_world` sample for the application core:

.. code-block:: console

   west build -b mesh_probe_r2/nrf54l15/cpuapp zephyr/samples/hello_world -d build/mesh-probe-r2-hello

Connect a compatible SWD probe to the board and power the target. The commands
below explicitly select pyOCD; the board configures its target as ``nrf54l``
and its SWD frequency as 4 MHz. The installed pyOCD must support that target.

.. code-block:: console

   west flash -d build/mesh-probe-r2-hello --runner pyocd

Open the serial port connected to UART30 at 115200 baud, 8 data bits, no parity,
one stop bit, and no hardware flow control. When using a separate UART adapter,
connect its RX to P0.04, TX to P0.03, and ground to board ground. After reset,
the sample prints a ``Hello World!`` message with the board target name.

To exercise the blue LED instead, build :zephyr:code-sample:`blinky`:

.. code-block:: console

   west build -b mesh_probe_r2/nrf54l15/cpuapp zephyr/samples/basic/blinky -d build/mesh-probe-r2-blinky
   west flash -d build/mesh-probe-r2-blinky --runner pyocd

These examples build standalone Zephyr samples. Product firmware can select a
different partition layout, bootloader, and peripheral configuration; use the
product's build instructions for that image set.

Debugging
=========

With the same SWD connection and application-core build, start a GDB session:

.. code-block:: console

   west debug -d build/mesh-probe-r2-hello --runner pyocd

The board also supplies OpenOCD and J-Link configuration. OpenOCD defaults to
a CMSIS-DAP interface; J-Link selects ``nRF54L15_M33`` for the application core
and ``nRF54L15_RV32`` for FLPR. Select a runner appropriate to the connected
probe and target. See :ref:`west-flashing` for runner options.

References
**********

* `nRF54L15 product information <https://www.nordicsemi.com/Products/nRF54L15>`_
* `Zephyr board porting guide <https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html>`_

The adjacent ``board.yml``, DTS/DTSI, pinctrl, Kconfig, and ``board.cmake`` files
are the sources for target selection, wiring, defaults, and runner settings.
For an application with overlays, inspect the generated ``zephyr.dts`` and
``.config`` in its build directory for the effective configuration.
