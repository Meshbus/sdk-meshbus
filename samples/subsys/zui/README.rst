ZUI samples
###########

These standalone applications demonstrate ZUI assets, components, drawing,
host/router state, input, screen lifecycle, and toast animation. Their source
layout follows ``subsys/zui/``; the Meshbus Display service sample is separate
at ``samples/subsys/display/``.

Build a sample from the west workspace root::

   source ~/.zephyr/env/bin/activate
   west build -p always -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
     meshbus/samples/subsys/zui/draw -d build/sample-zui-draw

Replace ``draw`` with ``assets``, ``component``, ``host``, ``input``, ``screen``,
or ``toast`` for the other applications. Each directory contains its own
``sample.yaml``, configuration, and source. All declare the C2 target above.
Use fresh build directories after relocation. A successful build does not
verify rendering, hardware input, or screen transitions on the device.
