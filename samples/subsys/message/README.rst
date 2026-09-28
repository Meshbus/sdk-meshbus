Meshbus Message Sample
######################

Select ``<qualified-board-target>`` from this sample's ``sample.yaml`` and
inspect the matching configuration/overlays. Additional boards require their
own integration and validation.

This sample starts the Meshbus message service with Contact and Channel stores
enabled. It logs public message request, response, ACK, and received channels.

Activate your Zephyr development environment, then build from the west
workspace root::

   west build -p always -b '<qualified-board-target>' \
     meshbus/samples/subsys/message

After flashing, use the serial shell to inspect local message state and publish
requests through the service shell::

   meshbus message next
   meshbus message send_to_channel 0 hello

Expected logs include outbound request events and inbound response events when
another Meshbus component publishes them.
