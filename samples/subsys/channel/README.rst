Meshbus Channel Sample
######################

Select ``<qualified-board-target>`` from this sample's ``sample.yaml`` and
inspect the matching configuration/overlays. Additional boards require their
own integration and validation.

This sample starts the Meshbus channel service for manual board validation. It
enables the service shell for fixed slot get/set/reset validation.

Activate your Zephyr development environment, then build from the west
workspace root::

   west build -p always -b '<qualified-board-target>' \
     meshbus/samples/subsys/channel

After flashing, use the serial shell to inspect and modify channel state::

   meshbus channel count
   meshbus channel size
   meshbus channel set 0 00112233445566778899aabbccddeeff demo
   meshbus channel get 0
   meshbus channel reset 0

Expected logs include the current channel store count and capacity.
