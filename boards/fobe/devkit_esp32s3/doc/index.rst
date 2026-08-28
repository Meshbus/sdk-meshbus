.. zephyr:board:: quill_esp32s3_mesh

Overview
********

The FoBE Quill ESP32S3 Mesh is a development kit featuring the ESP32-S3 SoC and an integrated
SX1262 LoRa® transceiver.

For more details see the `FoBE Quill ESP32S3 Mesh`_ wiki page.

Hardware
********

The FoBE Quill ESP32S3 Mesh is a compact and feature-rich IoT development platform, integrating
Espressif's high-performance multi-protocol SoC ESP32-S3 and Semtech's ultra-low-power sub-GHz
radio transceiver SX1262 (SiP package). Designed for IoT applications, it provides a
comprehensive wireless connectivity solution supporting IEEE 802.11 b/g/n Wi-Fi,
Bluetooth 5, Thread, Zigbee, and LoRa®.

This development board features efficient battery charging management, discrete power path,
ultra-low quiescent current DC-DC converter, a 1.3-inch 16-pin OLED display (SSD1312 driver),
user-programmable LED and button, MFP expansion connector, reversible USB-C connector, and
easy-to-expand pin headers, making it ideal for rapid prototyping and development.

Supported Features
==================

.. zephyr:board-supported-hw::

Connections and IOs
===================

The `FoBE Quill ESP32S3 Mesh`_ Documentation has detailed information about board
connections.

Prerequisites
-------------

Espressif HAL requires WiFi and Bluetooth binary blobs in order work. Run the command
below to retrieve those files.

.. code-block:: console

   west blobs fetch hal_espressif

.. note::

   It is recommended running the command above after :file:`west update`.

Building & Flashing
*******************

.. zephyr:board-supported-runners::

Simple boot
===========

The board could be loaded using the single binary image, without 2nd stage bootloader.
It is the default option when building the application without additional configuration.

.. note::

   Simple boot does not provide any security features nor OTA updates.

MCUboot bootloader
==================

User may choose to use MCUboot bootloader instead. In that case the bootloader
must be built (and flashed) at least once.

There are two options to be used when building an application:

1. Sysbuild
2. Manual build

.. note::

   User can select the MCUboot bootloader by adding the following line
   to the board default configuration file.

   .. code:: cfg

      CONFIG_BOOTLOADER_MCUBOOT=y

Sysbuild
========

The sysbuild makes possible to build and flash all necessary images needed to
bootstrap the board with the ESP32 SoC.

To build the sample application using sysbuild use the command:

.. zephyr-app-commands::
   :tool: west
   :zephyr-app: samples/hello_world
   :board: quill_esp32s3_mesh
   :goals: build
   :west-args: --sysbuild
   :compact:

By default, the ESP32 sysbuild creates bootloader (MCUboot) and application
images. But it can be configured to create other kind of images.

Build directory structure created by sysbuild is different from traditional
Zephyr build. Output is structured by the domain subdirectories:

.. code-block::

  build/
  ├── hello_world
  │   └── zephyr
  │       ├── zephyr.elf
  │       └── zephyr.bin
  ├── mcuboot
  │    └── zephyr
  │       ├── zephyr.elf
  │       └── zephyr.bin
  └── domains.yaml

.. note::

   With ``--sysbuild`` option the bootloader will be re-build and re-flash
   every time the pristine build is used.

For more information about the system build please read the :ref:`sysbuild` documentation.

Manual build
============

During the development cycle, it is intended to build & flash as quickly possible.
For that reason, images can be built one at a time using traditional build.

The instructions following are relevant for both manual build and sysbuild.
The only difference is the structure of the build directory.

.. note::

   Remember that bootloader (MCUboot) needs to be flash at least once.

Build and flash applications as usual (see :ref:`build_an_application` and
:ref:`application_run` for more details).

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: quill_esp32s3_mesh/esp32s3/procpu
   :goals: build

The usual ``flash`` target will work with the ``quill_esp32s3_mesh`` board
configuration. Here is an example for the :zephyr:code-sample:`hello_world`
application.

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: quill_esp32s3_mesh/esp32s3/procpu
   :goals: flash

Open the serial monitor using the following command:

.. code-block:: shell

   west espressif monitor

After the board has automatically reset and booted, you should see the following
message in the monitor:

.. code-block:: console

   ***** Booting Zephyr OS vx.x.x-xxx-gxxxxxxxxxxxx *****
   Hello World! quill_esp32s3_mesh

Debugging
*********

ESP32-S3 support on OpenOCD is available at `OpenOCD ESP32`_.

ESP32-S3 has a built-in JTAG circuitry and can be debugged without any additional chip.
Only an USB cable connected to the D+/D- pins is necessary.

Further documentation can be obtained from the SoC vendor
in `JTAG debugging for ESP32-S3`_.

Here is an example for building the :zephyr:code-sample:`hello_world` application.

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: quill_esp32s3_mesh/esp32s3/procpu
   :goals: debug

You can debug an application in the usual way. Here is an example for the
:zephyr:code-sample:`hello_world` application.

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: quill_esp32s3_mesh/esp32s3/procpu
   :goals: debug

References
**********

.. target-notes::

.. _`FoBE Quill ESP32S3 Mesh`: https://docs.fobestudio.com/product/f1102
.. _`JTAG debugging for ESP32-S3`: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/jtag-debugging/
.. _`OpenOCD ESP32`: https://github.com/espressif/openocd-esp32/releases
