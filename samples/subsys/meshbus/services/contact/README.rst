Meshbus Contact Sample
######################

This sample starts the Meshbus Contact service for manual board validation. It
logs contact store capacity and subscribes to public Contact request/response
ZBus channels plus MeshCore advert/config request channels.

Build from the west workspace root::

   source .venv/bin/activate
   west build -p always -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
     sdk-meshbus/samples/subsys/meshbus/services/contact

After flashing, use the serial shell to inspect and modify local MeshCore and
Contact state::

   meshbus meshcore config get
   meshbus contact count
   meshbus contact get 0

Expected logs include Contact request events and response events when another
Meshbus component publishes them.
