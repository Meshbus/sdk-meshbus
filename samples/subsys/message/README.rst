Meshbus Message Sample
######################

This sample starts the Meshbus message service with Contact and Channel stores
enabled. It logs public message request, response, ACK, and received channels.

Build from the west workspace root::

   source ~/.zephyr/env/bin/activate
   west build -p always -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
     meshbus/samples/subsys/message

After flashing, use the serial shell to inspect local message state and publish
requests through the service shell::

   meshbus message next
   meshbus message send_to_channel 0 hello

Expected logs include outbound request events and inbound response events when
another Meshbus component publishes them.
