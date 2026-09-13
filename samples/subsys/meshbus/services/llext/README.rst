Meshbus Desktop MBA examples
############################

These directories contain Desktop applications packaged as ``.mba`` files.
The host firmware supplies Desktop and the Meshbus LLEXT loader. Changing the
MeshCore role does not change application availability or restart the app.

Build and install
*****************

Use the Rust ``meshbus`` CLI with an EDK exported from the consuming firmware.
This directory is not a standalone firmware application. This repository's root
``DISTRIBUTION.md`` describes EDK creation, verification and qualification.

Current service headers use paths such as ``<indicator/indicator.h>`` and
``<llext/zbus.h>``. Service functions, types and ZBus objects use ``mbs_``;
constants use ``MBS_``. Update former ``meshbus_`` imports and rebuild against
the intended firmware's EDK. Old MBA service symbols have no host compatibility
aliases, even though metadata layout and channel IDs retain their values.

Snake, C++ Hello and RTTTL use the EDK directly. Arduboy ports additionally
require the external Meshbus Arduboy SDK selected by ``MESHBUS_ARDUBOY_SDK_DIR``
or their CMake default workspace location. Its ``meshbus_arduboy`` namespace
belongs to that separate dependency and is not renamed by the service migration.

From the Zephyr workspace, build representative C and C++ applications::

  meshbus llext --llext-sdk /path/to/edk.tar.xz -o build/llext \
    meshbus/samples/subsys/meshbus/services/llext/apps/snake
  meshbus llext --llext-sdk /path/to/edk.tar.xz -o build/llext \
    meshbus/samples/subsys/meshbus/services/llext/apps/cxx_hello

The resulting ``build/llext/<id>.mba`` files belong under ``/extra/apps``.
Desktop discovers packages through its application launcher and runs one
foreground application at a time. Other examples use the same build command.
There is no package type or EDK profile selector.

The CLI validates the EDK digest and injects metadata format version, EDK
version, exact target and estimated heap requirements. EDK version records
build provenance; it is not a runtime version-equality gate. Retain these
identities and package hashes in validation records. Package generation,
successful relocation/load and observed application behavior are separate
levels of evidence.

Native Zephyr peripherals
*************************

``CONFIG_MBS_LLEXT_BRIDGE`` exports the host's devicetree devices. MBA code can
include ``<zephyr/drivers/i2c.h>``, ``spi.h``, ``gpio.h`` and ``adc.h`` and use
``DEVICE_DT_GET()``, ``GPIO_DT_SPEC_GET()`` and the other native Zephyr device
macros with the generated devicetree headers from its EDK. Drivers run in the
host; inline driver APIs in the MBA dispatch through the host device's API
table. Out-of-line APIs and helpers must also be present in the host's live
LLEXT export table.
When ADC is enabled, the host exports ``adc_gain_invert()`` and
``adc_gain_invert_64()`` for native raw-to-voltage conversion helpers.

``apps/native_peripherals`` is a C2 example containing synchronous I2C register
read, SPI exchange, GPIO output and ADC sampling recipes::

  west meshbus llext --llext-sdk /path/to/edk.tar.xz \
    --zephyr-sdk /path/to/zephyr-sdk -o build/llext \
    meshbus/samples/subsys/meshbus/services/llext/apps/native_peripherals

Its entry only prints readiness for ``i2c21``, ``spi00``, ``gpio1`` and ``adc``;
it does not issue transfers or reconfigure pins. Add calls to the included
recipes for your wiring, chip select, ADC channel and peripheral protocol.
These functions are example code compiled into the MBA, not new Meshbus APIs.
On another board, change the node labels to match that firmware's devicetree.

The C2 controllers are shared: ``spi00`` hosts the radio, I2C controllers host
onboard devices, and the ADC includes the battery measurement channel. Consult
the board DTS and product overlay before selecting addresses, pins or channels.
Bus speed, pin configuration and power-state changes also affect host users of
that controller. An application must unregister callbacks and finish pending
I/O before returning; the loader cannot reclaim arbitrary driver registrations.

The host must enable each controller's driver, devicetree node and pinctrl.
Changing an MBA's compile definitions does not enable missing host hardware.
An external I2C/SPI chip can use an application-owned protocol implementation
over an existing controller without adding a Zephyr device for that chip.

Device exports use hashes of devicetree paths, reducing dependency-ordinal
changes between builds. Export a fresh EDK after enabling this feature. Old
ordinal-based device imports are not rewritten or provided with aliases.
Path hashes do not stabilize ``struct device``, driver API layouts, pin mappings
or behavior. Cross-version loading retains the metadata v2 interface ABI gate;
metadata v1 remains best effort. Neither compares EDK version strings for
equality. A missing directly imported device fails relocation; use
``device_get_binding()`` when a missing runtime device should be handled by the
application instead.

``tests/subsys/llext`` loads a real MBA using ``DEVICE_DT_GET()`` for four
test-owned I2C/SPI/GPIO/ADC devices on QEMU. It checks driver dispatch and returned
data and ADC voltage conversion, then unloads the MBA. The drivers and storage
are simulated; this does not qualify physical buses, analog readings, pinmux or
shared-device operation.

Application metadata
********************

Application authors supply ``llext.yaml``::

   id: snake
   name: Snake
   version: 1.0.0
   entry-point: snake_app_main
   stack-size: 4096

The CLI generates resource and target fields. An optional ``icon.png`` must be
exactly 10x10 pixels and is converted to a raw monochrome bitmap. The CLI uses
its bundled icon when the file is absent. The loader checks metadata, target,
entry symbol and heap limits before handing execution to Desktop.

Application lifecycle
*********************

Desktop owns the application thread and invokes loader cleanup after the entry
returns. Before returning, applications must remove ZBus subscriptions and stop
all callbacks, work items and timers that could enter unloaded code. The dynamic
LLEXT heap is allocated on demand and released after the session ends.

``meshbus llext config set false`` on the device shell prevents new probes and
loads; an already loaded application continues until normal exit. Use ``true``
to re-enable loading. Configuration changes require no reboot.

The C2 host accepts owner-trusted native code without package signing or
userspace isolation. Firmware signing is separate. Upload and physical testing
follow the consuming firmware's authorization and storage procedures.

C++ Desktop App Pattern
***********************

``apps/cxx_hello`` demonstrates the supported C++ ``.mba`` pattern. It still
produces one ``.llext`` object that ``meshbus llext`` renames to ``.mba``
and then annotates with metadata. The sample compiles with
``arm-zephyr-eabi-g++`` and
filters C-only standard flags from the EDK-provided compile flags.

Keep C++ apps narrow:

- export the app entry point as ``extern "C"`` and list the same symbol in
  ``llext.yaml``
- use ``LL_EXTENSION_SYMBOL(<entry>)`` after the entry definition
- compile without exceptions, RTTI, weak/COMDAT sections, thread-safe statics,
  unwind tables, or ``__cxa_atexit``; in practice this means using
  ``-fno-exceptions -fno-rtti -fno-weak -fno-threadsafe-statics``
- prefer explicit initialization inside the app entry; do not depend on dynamic
  global constructors or destructors being called by the LLEXT loader
- prefer a unity-style C++ source or another single-output build path unless the
  host firmware explicitly accepts a different LLEXT binary type
- keep generated C++ sections contiguous enough for the Zephyr LLEXT loader.
  Grouped ``.text.*`` COMDAT sections may be placed after data/rodata in a
  relocatable object and can make the loader reject the app for overlapping ELF
  file ranges.

This is the expected starting point for larger source-level C++ app ports.
``apps/microcity`` and ``apps/castleboy`` use this pattern for source-level
game ports.
