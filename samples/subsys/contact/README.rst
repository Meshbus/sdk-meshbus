Meshbus Contact Sample
######################

Select ``<qualified-board-target>`` from this sample's ``sample.yaml`` and
inspect the matching configuration/overlays. Additional boards require their
own integration and validation.

This sample starts the Meshbus Contact service for manual board validation. It
logs contact store capacity and subscribes to public Contact request/response
ZBus channels plus the MeshCore advert request channel.

Activate your Zephyr development environment, then build from the west
workspace root::

   west build -p always -b '<qualified-board-target>' \
     meshbus/samples/subsys/contact

After flashing, use the serial shell to inspect and modify local MeshCore and
Contact state::

   meshbus meshcore config get
   meshbus contact count
   meshbus contact get 0

Expected logs include Contact request events and response events when another
Meshbus component publishes them.
