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
