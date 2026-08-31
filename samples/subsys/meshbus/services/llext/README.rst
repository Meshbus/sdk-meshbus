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

The SDK sample can be built without a Firmware source checkout. Host tools
are provided by the separately installed Rust ``meshbus`` CLI.

Build Extensions From An EDK
***************************

Use an immutable EDK exported for the exact host target and profile. Set
``ZEPHYR_SDK_INSTALL_DIR`` to an externally installed compatible Zephyr SDK;
CMake and Ninja must also be on PATH. No Firmware checkout, host build,
Python, west or protoc is needed to consume a released EDK.

A service-profile EDK is required for the boot-service examples::

  meshbus llext --llext-sdk /path/to/service-edk.tar.xz -o build/llext \
    sdk-meshbus/samples/subsys/meshbus/services/llext/services/blinky

  meshbus llext --llext-sdk /path/to/service-edk.tar.xz -o build/llext \
    sdk-meshbus/samples/subsys/meshbus/services/llext/services/beeper

The outputs are ``build/llext/blinky.mbs`` and ``build/llext/beeper.mbs``.
A service EDK must come from a service-enabled build; the current C2 product
EDK has the app profile and cannot substitute for it.

Desktop apps require an app-profile EDK from Desktop-capable firmware::

  meshbus llext --llext-sdk /path/to/app-edk.tar.xz -o build/llext \
    sdk-meshbus/samples/subsys/meshbus/services/llext/apps/snake

  meshbus llext --llext-sdk /path/to/app-edk.tar.xz -o build/llext \
    sdk-meshbus/samples/subsys/meshbus/services/llext/apps/cxx_hello

Other examples under ``apps/`` use the same command with their own source
directory. Outputs are ``build/llext/<id>.mba`` and are installed under
``/extra/apps``. Desktop scans that directory when the app menu opens.

The CLI verifies the EDK digest and profile and injects metadata format
version, EDK version, exact target and estimated heap requirements. EDK
version records build provenance; it is not a runtime version-equality gate.
The authoritative export and distribution procedure is in the
meshbus-firmware repository's ``docs/distribution.md``. The SDK does not
import tools or source directories from that repository.

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

Runtime Validation
******************

Install packages only into the locations supported by the running host:
``/extra/svcs`` for boot services and ``/extra/apps`` for Desktop apps.
Use packages built with an EDK matching that host's target and profile.
Services are discovered after reboot. Device upload, reset and flash operations
require separate authorization and the host's own storage/partition procedure;
this SDK sample does not prescribe product flash addresses.

Then check the device boot log or device shell commands::

  meshbus llext service list
  meshbus llext service status blinky
  meshbus llext service status beeper

These are device shell commands, not host CLI subcommands.

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

``meshbus llext`` injects metadata format version, EDK version, exact
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
- ``heap-size`` estimated by ``meshbus llext`` from the built ELF
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

``meshbus llext`` injects ``metadata-version``, ``edk-version``, ``target``,
``heap-size``, and ``icon-data``. App source directories may contain a
fixed-name ``icon.png``;
when present it must convert to exactly 10x10 raw mono bitmap data. Missing
``icon.png`` uses the default app icon bundled with ``meshbus-cli``.
