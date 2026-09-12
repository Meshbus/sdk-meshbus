Meshbus Clock Sample
####################

This sample starts the Meshbus clock service for manual board validation. It
logs the current public configuration at boot and enables the clock shell.

Build from the west workspace root::

   source ~/.zephyr/env/bin/activate
   west build -p always -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
     meshbus/samples/subsys/meshbus/services/clock

After flashing, use the serial shell to inspect and change clock config::

   meshbus clock get
   meshbus clock set time-format 24h
   meshbus clock set utc-offset 480
   meshbus clock reset
