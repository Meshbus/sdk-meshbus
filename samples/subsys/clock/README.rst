Meshbus Clock Sample
####################

Select ``<qualified-board-target>`` from this sample's ``sample.yaml`` and
inspect the matching configuration/overlays. Additional boards require their
own integration and validation.

This sample starts the Meshbus clock service for manual board validation. It
logs the current public configuration at boot and enables the clock shell.

Activate your Zephyr development environment, then build from the west
workspace root::

   west build -p always -b '<qualified-board-target>' \
     meshbus/samples/subsys/clock

After flashing, use the serial shell to inspect and change clock config::

   meshbus clock config get
   meshbus clock config set 24h 480
   meshbus clock config reset
