.. code-block:: none

   Meshbus LLEXT Boot Services Sample

This sample enables the Meshbus LLEXT boot service manager. It mounts
``/extra`` as LittleFS, scans ``/extra/svcs`` during boot, and exposes
read-only diagnostics under::

  meshbus llext service ...

Use it to upload and validate service packages:

- ``/extra/svcs/<service>.mbs``
- newly installed services require a device reboot before they run

Build Host Firmware
*******************

From workspace root::

  source ~/.zephyr/env/bin/activate
  BOARD=idea_mesh_tracker_c2/nrf54l15/cpuapp

  west build -p auto -b ${BOARD} \
    -d build.meshbus_llext_service_hw \
    -s sdk-meshbus/samples/subsys/meshbus/services/llext

The host firmware build is the image you flash to the device. ``west meshbus llext``
uses this same build directory for EDK generation, EDK extraction, and service
extension outputs.

Build Service Extension
***********************

Build the sample services with the west helper command::

  west meshbus llext -d build.meshbus_llext_service_hw \
    sdk-meshbus/samples/subsys/meshbus/services/llext/services/blinky

  west meshbus llext -d build.meshbus_llext_service_hw \
    sdk-meshbus/samples/subsys/meshbus/services/llext/services/beeper

On first use, ``west meshbus llext`` runs the host build's ``llext-edk`` target,
copies the EDK tarball to ``build.meshbus_llext_service_hw/zephyr/llext/``,
and extracts it to
``build.meshbus_llext_service_hw/zephyr/llext/llext-edk``. Repeated builds
reuse this managed EDK cache.

Each package records the metadata format version, the EDK version used for the
build, the exact target, and estimated heap requirements. EDK version is build
provenance, not a runtime compatibility gate.

Expected artifact:

- ``build.meshbus_llext_service_hw/zephyr/llext/blinky.mbs``
- ``build.meshbus_llext_service_hw/zephyr/llext/beeper.mbs``

Build Desktop App Extension
***************************

Desktop apps use ``type: app`` metadata, the ``.mba`` suffix, and are installed
under ``/extra/apps``. They are not scanned at boot; Desktop scans the app
directory when the app menu opens. Build these apps against a Desktop-capable
host firmware from the separate Meshbus ``app/`` so the required ZUI
exports are present.

Build the Desktop-capable host firmware first::

  west build -p auto -b ${BOARD} \
    -d build.meshbus_llext_desktop_host \
    -s app

Build the sample apps with a Desktop host firmware build directory::

  west meshbus llext -d build.meshbus_llext_desktop_host \
    sdk-meshbus/samples/subsys/meshbus/services/llext/apps/snake

  west meshbus llext -d build.meshbus_llext_desktop_host \
    sdk-meshbus/samples/subsys/meshbus/services/llext/apps/rtttl

  west meshbus llext -d build.meshbus_llext_desktop_host \
    sdk-meshbus/samples/subsys/meshbus/services/llext/apps/cxx_hello

  west meshbus llext -d build.meshbus_llext_desktop_host \
    sdk-meshbus/samples/subsys/meshbus/services/llext/apps/microcity

  west meshbus llext -d build.meshbus_llext_desktop_host \
    sdk-meshbus/samples/subsys/meshbus/services/llext/apps/castleboy

  west meshbus llext -d build.meshbus_llext_desktop_host \
    sdk-meshbus/samples/subsys/meshbus/services/llext/apps/ard_drivin

  west meshbus llext -d build.meshbus_llext_desktop_host \
    sdk-meshbus/samples/subsys/meshbus/services/llext/apps/arduboy3d

  west meshbus llext -d build.meshbus_llext_desktop_host \
    sdk-meshbus/samples/subsys/meshbus/services/llext/apps/hollow

  west meshbus llext -d build.meshbus_llext_desktop_host \
    sdk-meshbus/samples/subsys/meshbus/services/llext/apps/hopper

Expected artifacts:

- ``build.meshbus_llext_desktop_host/zephyr/llext/snake.mba``
- ``build.meshbus_llext_desktop_host/zephyr/llext/rtttl.mba``
- ``build.meshbus_llext_desktop_host/zephyr/llext/cxx_hello.mba``
- ``build.meshbus_llext_desktop_host/zephyr/llext/microcity.mba``
- ``build.meshbus_llext_desktop_host/zephyr/llext/castleboy.mba``
- ``build.meshbus_llext_desktop_host/zephyr/llext/ard_drivin.mba``
- ``build.meshbus_llext_desktop_host/zephyr/llext/arduboy3d.mba``
- ``build.meshbus_llext_desktop_host/zephyr/llext/hollow.mba``
- ``build.meshbus_llext_desktop_host/zephyr/llext/hopper.mba``

Release EDK Consumption
***********************

The host-managed EDK used by plain
``west meshbus llext -d <host-build>`` is a local development convenience.
Public packages must be built from an immutable, extracted release EDK with
the separately installed ``meshbus-cli`` package. A consumer does not need the
private firmware checkout or host build::

  meshbus llext --llext-sdk <extracted-release-edk>/llext-edk \
    -o build/release-llext \
    sdk-meshbus/samples/subsys/meshbus/services/llext/apps/snake

Set ``ZEPHYR_SDK_INSTALL_DIR`` or pass ``--zephyr-sdk`` when needed. The
packager verifies the EDK's SDK digest, requires the package to use the EDK's
``app`` or ``service`` profile, and injects the EDK version, metadata format
version, and exact target. Firmware version equality is deliberately not a
package compatibility gate. The authoritative firmware build, repeated EDK
export, official-package qualification, checksum, and publication procedure is
``subsys/meshbus/services/llext/EDK_RELEASE.md``.

Package Publication
*******************

Retain the package hash and source revision, release EDK ``sdk-sha256``, exact
target, profile, metadata format version, and EDK version in release records.
Host firmware, Zephyr, and toolchain identities may also be retained for
diagnosis; they are provenance, not runtime equality checks.

Describe validation precisely: package generation, successful relocation/load,
and successful runtime behavior are separate levels of evidence. Metadata
compatibility does not guarantee that allocation, relocation, initialization,
or execution will succeed. Packages built with an older metadata layout are
unsupported and must be rebuilt; do not edit or relabel old artifacts. A
package for one board target must not be published for another.

``apps/rtttl/icon.png`` overrides the default fixed-name 10x10 mono launcher
icon. ``apps/snake``, ``apps/cxx_hello``, ``apps/microcity``, and
``apps/castleboy`` have no icon file, so the packager injects its bundled
default app icon.

C++ Desktop App Pattern
***********************

``apps/cxx_hello`` demonstrates the supported C++ ``.mba`` pattern. It still
produces one ``.llext`` object that ``west meshbus llext`` renames to ``.mba``
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

Runtime Validation
******************

Install extensions into a LittleFS image for the ``extra`` partition. If
services and Desktop apps are installed together, build every extension against
the same host build directory so metadata matches the running firmware. The
example below assumes ``blinky.mbs`` and ``beeper.mbs`` were also rebuilt with
``-d build.meshbus_llext_desktop_host``::

  python -m pip install --target /tmp/littlefs-python-pkg littlefs-python
  PYTHONPATH=/tmp/littlefs-python-pkg west mklfs \
    -d build.meshbus_llext_desktop_host \
    -o /tmp/meshbus-extra-lfs.bin \
    --file build.meshbus_llext_desktop_host/zephyr/llext/blinky.mbs:/svcs/blinky.mbs \
    --file build.meshbus_llext_desktop_host/zephyr/llext/beeper.mbs:/svcs/beeper.mbs \
    --file build.meshbus_llext_desktop_host/zephyr/llext/snake.mba:/apps/games/snake.mba \
    --file build.meshbus_llext_desktop_host/zephyr/llext/rtttl.mba:/apps/tools/rtttl.mba

Flash the generated partition image and reboot::

  pyocd commander -t nrf54l -M halt \
    -c "load /tmp/meshbus-extra-lfs.bin 0x146000" \
    -c reset \
    -c exit

Then check the boot log or run shell commands::

     meshbus llext service list
     meshbus llext service status blinky
     meshbus llext service status beeper

LLEXT Metadata Contract
***********************

Service author metadata comes from ``llext.yaml``:

.. code-block:: yaml

   id: blinky
   type: service
   name: Blinky Service
   description: Periodic LED heartbeat service.
   version: 1.0.0
   entry-point: blink_thread_entry
   stack-size: 2048

``west meshbus llext`` injects metadata format version, EDK version, exact
target, and heap estimate into section ``.meshbus.llext.meta`` (``non-alloc``,
no ``SHF_ALLOC``).

The manager expects ``struct meshbus_llext_service_metadata`` with:

- ``magic == MESHBUS_LLEXT_SERVICE_METADATA_MAGIC``
- ``metadata_version == MESHBUS_LLEXT_SERVICE_METADATA_VERSION``
- ``size == sizeof(struct meshbus_llext_service_metadata)``
- non-empty ``id``, ``name``, ``version``, ``entry-point``
- optional ``description``
- non-empty ``edk_version`` build provenance
- ``target`` matching the running firmware exactly
- ``heap-size`` estimated by ``west meshbus llext`` from the built ELF
- ``stack-size`` required and greater than zero

The helper reads target, profile, metadata format version, and EDK version from
the EDK. It also estimates service LLEXT heap usage from allocatable ELF
sections and loader metadata overhead. At boot the manager sums valid service
estimates, adds
``CONFIG_MESHBUS_LLEXT_DYNAMIC_HEAP_MARGIN_PERCENT``, and initializes the
dynamic Zephyr LLEXT heap once before loading services.

Service ``id`` is authoritative and comes from metadata. Filename basename does
not need to match ``id``.

Desktop app author metadata is intentionally smaller:

.. code-block:: yaml

   id: snake
   type: app
   name: Snake
   version: 1.0.0
   entry-point: snake_app_main
   stack-size: 4096

``west meshbus llext`` injects ``metadata-version``, ``edk-version``, ``target``,
``heap-size``, and ``icon-data``. App source directories may contain a
fixed-name ``icon.png``;
when present it must convert to exactly 10x10 raw mono bitmap data. Missing
``icon.png`` uses the default app icon bundled with ``meshbus-cli``.
