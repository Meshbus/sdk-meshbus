Meshbus Notify Sample
#####################

This sample starts the Meshbus notify service for manual board validation. It
subscribes to ``mbs_notify_chan`` and logs published notification events.

Build from the west workspace root::

   source ~/.zephyr/env/bin/activate
   west build -p always -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
     meshbus/samples/subsys/meshbus/services/notify

After flashing, use the serial shell to publish notification events through the
service shell and observe logs from the public ZBus channel.
