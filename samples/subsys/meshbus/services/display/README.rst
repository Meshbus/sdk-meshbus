Meshbus Display Sample
######################

This sample starts the Meshbus display service for manual board validation. It
logs the current display config and subscribes to ``mbs_display_state_chan``.

Build from the west workspace root::

   source ~/.zephyr/env/bin/activate
   west build -p always -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
     meshbus/samples/subsys/meshbus/services/display

After flashing, use the serial shell to inspect and change display state::

   meshbus display get
   meshbus display active on
   meshbus display active off
   meshbus display reset

Expected logs include display active-state changes published on the public ZBus
channel.
