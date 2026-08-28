Meshbus Channel Sample
######################

This sample starts the Meshbus channel service for manual board validation. It
enables the service shell for fixed slot get/set/reset validation.

Build from the west workspace root::

   source .venv/bin/activate
   west build -p always -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
     sdk-meshbus/samples/subsys/meshbus/services/channel

After flashing, use the serial shell to inspect and modify channel state::

   meshbus channel count
   meshbus channel size
   meshbus channel set 0 00112233445566778899aabbccddeeff demo
   meshbus channel get 0
   meshbus channel reset 0

Expected logs include the current channel store count and capacity.
