Meshbus Service Samples
#######################

These samples are board-facing manual validation surfaces for Meshbus services.
They are intentionally different from
``meshbus/tests/subsys/*``.

Service implementation and public headers live in ``subsys/<module>/`` and
``include/<module>/``. These sample directories retain their current
``samples/subsys/meshbus/services/`` layout. Select services with ``CONFIG_MBS``
and ``CONFIG_MBS_*``; use ``mbs_<module>_*`` functions, types and ZBus objects
from ``<module/module.h>``. Protobuf-generated identifiers and the device Shell
root command ``meshbus`` retain their names.

Purpose
*******

Use these samples to start one real service on a real board, then validate it
through the serial shell, logs, and simple ZBus listeners.

They answer questions such as:

- does the service build for the board?
- are the board overlay and chosen nodes wired correctly?
- do the real drivers become ready?
- does the shell command tree work?
- do live events or status values appear in logs?

They are not exhaustive automated contract tests. Public API and public ZBus
contract assertions belong under
``meshbus/tests/subsys/*``.

Primary Board
*************

The primary board-facing target for these service samples is::

  idea_mesh_tracker_c2/nrf54l15/cpuapp

Build one firmware service sample from the west workspace root (``llext``
contains MBA examples with a separate EDK build flow)::

  source ~/.zephyr/env/bin/activate
  west build -p always -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
    meshbus/samples/subsys/meshbus/services/<service>

Flash and inspect the serial shell/logs for manual validation::

  west flash -d build

Available Services
******************

``bluetooth``
  Starts Meshbus Bluetooth, BLE notify, and MCUmgr surfaces.

``channel``
  Starts Meshbus channel slot storage and shell commands.

``clock``
  Starts Meshbus clock and exposes runtime config shell commands.

``contact``
  Starts Contact storage and logs Contact and MeshCore request/response events.

``display``
  Starts Meshbus display and logs display state events.

``gnss``
  Starts Meshbus GNSS using the board GNSS chosen node.

``indicator``
  Starts Meshbus indicator using board LED/buzzer chosen nodes.

``input``
  Starts Meshbus input and logs raw/action ZBus events.

``llext``
  Contains Desktop MBA source examples compiled with an EDK. This directory
  is not a standalone firmware application; see its ``README.rst``.

``message``
  Starts Meshbus message with Contact/Channel stores and logs message channels.

``notify``
  Starts Meshbus notify and logs notification events.

``power``
  Starts Meshbus power using board fuel-gauge/charger/power-button devices.

``radio``
  Starts Meshbus radio using the board LoRa radio chosen node.

``telemetry``
  Starts Meshbus telemetry and logs live sensor channel events.
