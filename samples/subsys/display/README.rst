Meshbus Display Sample
######################

Select ``<qualified-board-target>`` from this sample's ``sample.yaml`` and
inspect the matching configuration/overlays. Additional boards require their
own integration and validation.

This sample starts the Meshbus display service for manual board validation. It
logs the current display config and subscribes to ``mbs_display_state_chan``.

Activate your Zephyr development environment, then build from the west
workspace root::

   west build -p always -b '<qualified-board-target>' \
     meshbus/samples/subsys/display

After flashing, use the serial shell to inspect and change display state::

   meshbus display config get
   meshbus display wake
   meshbus display sleep
   meshbus display config reset

Expected logs include display active-state changes published on the public ZBus
channel.
