Meshbus Service Samples
#######################

Select ``<qualified-board-target>`` from the chosen sample's ``sample.yaml`` and
inspect the matching configuration/overlays. Additional boards require their
own integration and validation.

These samples are board-facing manual validation surfaces for Meshbus services.
They are intentionally different from
``meshbus/tests/subsys/*``.

Service implementation and public headers live in ``subsys/<module>/`` and
``include/<module>/``. Sample directories live directly under
``samples/subsys/<module>/``, matching the service layout.
Select services with ``CONFIG_MBS`` and ``CONFIG_MBS_*``; use ``mbs_<module>_*`` functions, types and ZBus objects
from ``<module/module.h>``. Protobuf-generated C identifiers use ``meshbus_*``;
the device Shell root command is ``meshbus``.

Purpose
*******

Use these samples to start a service and its required dependencies on a board,
then validate it through the serial shell, logs, and simple ZBus listeners.
The ``meshbus <service> ...`` commands in these sample guides run in the
firmware's Zephyr shell. The host CLI's ``meshbus connect`` console uses its
own command syntax, including named ``--field`` configuration options; see
the CLI's ``help <service> config`` before adapting a device-shell command.

They answer questions such as:

- does the service build for the board?
- are the board overlay and chosen nodes wired correctly?
- do the real drivers become ready?
- does the shell command tree work?
- do live events or status values appear in logs?

They are not exhaustive automated contract tests. Public API and public ZBus
contract assertions belong under
``meshbus/tests/subsys/*``.

Target selection
****************

Choose a target and scenario declared in the sample's ``sample.yaml``. Inspect
its ``boards/`` overlays and required devicetree nodes before building; the SDK
does not imply that every sample supports every board.

With your Zephyr development environment active, build one firmware service
sample from the west workspace root (``llext`` contains MBA examples with a
separate EDK build flow)::

  west build -p always -b '<qualified-board-target>' \
    meshbus/samples/subsys/<service> -d build/sample-<service>

Flash and inspect the serial shell/logs for manual validation::

  west flash -d build/sample-<service>

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

The external ``modules/lib/zui/samples`` directory contains ZUI library samples.

U8g2 rendering examples are maintained in the independent ``sdk-u8g2`` module
at ``modules/lib/u8g2/samples/display`` in the west workspace.
