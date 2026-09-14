.. zephyr:board:: devkit_esp32c6

Overview
********

FoBE DevKit ESP32-C6 uses an ESP32-C6FH4 with 4 MiB in-package flash,
512 KiB HP SRAM and a 160 MHz RISC-V HP core. The supported targets are
``devkit_esp32c6/esp32c6/hpcore`` and ``devkit_esp32c6/esp32c6/lpcore``.
This board definition assumes the standard 40 MHz crystal.

Pin assignment
**************

The following mapping follows the supplied U10/U11 connector schematic.
IOREF is 3.3 V; AREF is unconnected. The header carries 3V3, 5V, VIN,
GND and NRESET. Arduino layout does not imply 5 V GPIO compatibility.

.. list-table:: Arduino-compatible connector
   :header-rows: 1

   * - Header signal
     - ESP32-C6 GPIO
     - Function / shared connection
   * - A0 / D14
     - 0
     - ADC1 channel 0
   * - A1 / D15
     - 1
     - ADC1 channel 1
   * - A2 / D16
     - 2
     - ADC1 channel 2
   * - A3 / D17
     - 9
     - BOOT; digital only
   * - A4 / D18, SDA
     - 22
     - I2C0 SDA; digital only
   * - A5 / D19, SCL
     - 23
     - I2C0 SCL; digital only
   * - D0 / RX
     - 17
     - UART0 RX
   * - D1 / TX
     - 16
     - UART0 TX
   * - D2
     - 3
     - GPIO
   * - D3
     - 4
     - GPIO
   * - D4
     - 5
     - GPIO
   * - D5
     - 6
     - GPIO
   * - D6
     - 7
     - GPIO
   * - D7
     - 8
     - GPIO / strapping pin
   * - D8
     - 14
     - GPIO
   * - D9
     - 15
     - GPIO / strapping pin
   * - D10 / NSS
     - 21
     - SPI2 chip select 0, active low
   * - D11 / MOSI
     - 18
     - SPI2 MOSI
   * - D12 / MISO
     - 20
     - SPI2 MISO
   * - D13 / SCK
     - 19
     - SPI2 clock

``arduino_header`` maps the GPIO connector; ``arduino_adc`` exposes only
A0 through A2 as ADC channels. The Zephyr Arduino R3 binding constants
``ARDUINO_HEADER_R3_D14`` / ``D15`` denote the dedicated SDA/SCL header
positions (GPIO22/23), not the schematic's A0/D14 and A1/D15 labels.
Use ``ARDUINO_HEADER_R3_A0`` through ``A5`` for those analog-header positions.

``arduino_serial``, ``arduino_i2c`` and ``arduino_spi`` refer to UART0,
I2C0 (100 kHz) and SPI2 respectively. Applications enable the corresponding
Kconfig drivers. I2C needs suitable external pull-ups for the connected bus;
internal weak pull-ups are enabled. GPIO4, GPIO5, GPIO8, GPIO9 and GPIO15
are strapping pins: attached shields must preserve the required reset levels.
A3 shares GPIO9 with BOOT and cannot be used as an analog input.
The onboard Modulino LED Matrix shares I2C0 on GPIO22/23 with the header.
It is a 12x8 display at the default 7-bit address 0x39, exposed as
``modulino_matrix`` and selected by ``zephyr,display``. Applications enable
``CONFIG_I2C=y`` and ``CONFIG_DISPLAY=y``; the Modulino driver is then
selected automatically. It supports monochrome and 16-level grayscale
through the Display API. See ``docs/modulino-led-matrix.md`` for usage and
the required Modulino firmware protocol. No physical user button is assumed.

USB, programming and debugging
******************************

Native USB Serial/JTAG is connected to GPIO12 (D-) and GPIO13 (D+).
It is the HP core's default Zephyr console and shell UART; UART0 on D0/D1 remains
available for shields. No USB CDC ACM stack is needed for this controller.
The OpenOCD runner uses ``board/esp32c6-builtin.cfg`` for the built-in JTAG
interface and requires Espressif OpenOCD on the host.

From the west workspace with the documented Zephyr environment activated::

   west build -b devkit_esp32c6/esp32c6/hpcore zephyr/samples/hello_world -d build/devkit-esp32c6
   west flash -d build/devkit-esp32c6
   west debug -d build/devkit-esp32c6

The board uses Zephyr's standard ESP32 4 MiB partition layout. Product
composition, MCUboot signing policy and RF qualification are application
concerns, not established by this board definition. USB enumeration, flashing,
JTAG attachment and peripheral electrical operation require physical validation.

LP core
*******

The LP core target follows Zephyr's ``esp32c6_devkitc`` configuration:
a 20 MHz CPU, ``ulp_ram`` (15 KiB of the 16 KiB LP SRAM), a 256-byte
board heap contribution and ``slot0_lpcore_partition`` at flash offset
0x3a0000 (32 KiB). Both cores use the same standard 4 MiB partition map.

The LP console uses LP UART at 115200 baud: TX on GPIO5 (header D4) and
RX on GPIO4 (header D3). It does not use native USB or UART0 on D0/D1.
Applications must coordinate these shared pads with the HP core and shields.
As upstream, the LP defconfig disables the boot banner and ``printk`` to
reduce memory use. The onboard I2C display remains an HP-core peripheral.

To compile the LP target alone::

   west build -b devkit_esp32c6/esp32c6/lpcore zephyr/samples/hello_world -d build/devkit-esp32c6-lpcore

An LP binary needs an HP application to load and start it. Standalone
compilation does not establish dual-core startup. See Zephyr's
``samples/boards/espressif/ulp/lp_core`` applications for coordinated
sysbuild examples. The default OpenOCD configuration above targets the HP
core; LP debugging needs the dedicated setup described by the upstream
``debug_ulp`` sample, including ``board/esp32c6-lpcore-builtin.cfg``.

References
**********

* `ESP32-C6 datasheet <https://documentation.espressif.com/esp32-c6_datasheet_en.html>`_
* User-supplied U10/U11 schematic and GPIO12/13 USB wiring confirmation.
