Meshbus Notify Sample
#####################

Select ``<qualified-board-target>`` from this sample's ``sample.yaml`` and
inspect the matching configuration/overlays. Additional boards require their
own integration and validation.

This sample starts the Meshbus notify service for manual board validation. It
subscribes to ``mbs_notify_chan`` and logs published notification events.

Activate your Zephyr development environment, then build from the west
workspace root::

   west build -p always -b '<qualified-board-target>' \
     meshbus/samples/subsys/notify

After flashing, use the serial shell to publish notification events through the
service shell and observe logs from the public ZBus channel.
