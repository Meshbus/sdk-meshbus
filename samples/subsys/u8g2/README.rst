U8g2 rendering sample
#####################

This application cycles through drawing workloads and reports drawing and
flush timing. It uses ``CONFIG_U8G2`` with the board's ``zephyr,display`` chosen
device. Rendering and panel behavior require manual hardware validation.

Build from the west workspace root::

   source ~/.zephyr/env/bin/activate
   west build -p always -b frdm_k64f meshbus/samples/subsys/u8g2 \
     -d build/sample-u8g2 -- -DSHIELD=ssd1306_128x64

The other platform declared in ``sample.yaml`` is ``reel_board`` (no shield).
The existing ``sample.display.cfb.*`` scenario IDs are retained for compatibility;
the application uses U8g2 rather than CFB.

``CONFIG_U8G2_STRESS_LEVEL`` controls workload intensity. Enable
``CONFIG_U8G2_STRESS_MODE_CYCLE`` to cycle workloads, and set
``CONFIG_U8G2_STRESS_MODE_FRAMES`` to adjust the cycle duration.
The rendering loop supports both full-buffer and
``CONFIG_U8G2_BUFFER_MODE_PAGE8`` modes.
