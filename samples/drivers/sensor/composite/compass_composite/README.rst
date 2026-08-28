Compass Composite Sample
########################

This sample demonstrates the ``zephyr,compass-composite`` sensor driver on
``idea_mesh_tracker_c2/nrf54l15/cpuapp``.

It instantiates a composite compass device using:

- an overlay-local LSM6DS3TR-C node for ``SENSOR_CHAN_ACCEL_XYZ``
- the same LSM6DS3TR-C node for ``SENSOR_CHAN_GYRO_XYZ``
- ``mmc5603`` for ``SENSOR_CHAN_MAGN_XYZ``

These optional sensors are declared by the sample overlay rather than the base
``idea_mesh_tracker_c2`` board DTS.

Driver model
************

The current driver implementation uses:

- tilt-compensated magnetic heading from accel + magn
- heading-domain complementary fusion with gyro short-term prediction
- runtime min/max magnet calibration estimation (hard/soft-iron approximation)

No NVS/settings dependency is included in this driver. Runtime tuning values are
RAM-only and can be persisted by upper layers if needed.

Runtime attributes
******************

Supported runtime attributes through Sensor API:

- ``SENSOR_ATTR_COMPASS_DECLINATION`` (set/get)
- ``SENSOR_ATTR_COMPASS_MOUNT_OFFSET`` (set/get)
- ``SENSOR_ATTR_COMPASS_FUSION_ALPHA`` (set/get)
- ``SENSOR_ATTR_COMPASS_MAG_BIAS`` (set/get, per ``SENSOR_CHAN_MAGN_X/Y/Z``)
- ``SENSOR_ATTR_COMPASS_MAG_SCALE`` (set/get, per ``SENSOR_CHAN_MAGN_X/Y/Z``)
- ``SENSOR_ATTR_COMPASS_MAG_BIAS_EST`` (get-only, per axis)
- ``SENSOR_ATTR_COMPASS_MAG_SCALE_EST`` (get-only, per axis)
- ``SENSOR_ATTR_COMPASS_CAL_HINT`` / ``SENSOR_ATTR_COMPASS_ACCURACY`` (get-only)

``MAG_SCALE <= 0`` is treated as a clear signal for that axis manual override,
falling back to the runtime estimated calibration.

Sample shell commands
*********************

This sample adds a dedicated ``compass`` shell command set for tuning:

- ``compass show``
- ``compass reset`` (reset runtime estimates, preserving manual overrides)
- ``compass reset all`` (also clear every manual bias/scale override)
- ``compass stream on <ms>`` / ``compass stream off``
- ``compass set decl <deg>``
- ``compass set mount <deg>``
- ``compass set alpha <0..1>``
- ``compass set bias <x|y|z> <gauss>``
- ``compass set scale <x|y|z> <ratio>``
- ``compass clear <x|y|z>``
- ``compass dump`` (prints replay commands for current runtime tuning)

Build
*****

Build the sample as a regular Zephyr application:

.. code-block:: shell

   west build -p auto \
     -d build/compass-composite-c2 \
     -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
     sdk-meshbus/samples/drivers/sensor/composite/compass_composite

This SDK sample intentionally does not import product-firmware MCUboot settings
or partition overlays. Provisioning, recovery images, and product sysbuild
configuration belong to the separate Meshbus firmware application repository.

Run
***

Flash as usual for your board and observe UART logs. Default stream output
includes one-line fields for heading/decl/mount/alpha/hint/accuracy plus
active and estimated magnet bias/scale vectors.

The C2 UART settings are 115200 baud, 8 data bits, no parity, and 1 stop bit.
After boot, ``uart:~$`` and ``Compass device ready`` confirm that the shell and
composite device are available.

Calibration notes
*****************

- If ``hint=figure-8`` or ``accuracy=unreliable`` persists, perform slow 3D
  figure-eight motion to improve min/max coverage.
- Start a repeatable comparison run with ``compass reset all`` so no estimate or
  manual override from the current boot remains.
- For level-only use, rotate on a flat surface; this may stabilize heading but
  can still report lower confidence than full 3D coverage.
- Use ``compass dump`` after full 3D calibration to capture replay commands.
  Axes without a valid estimate and without a non-default manual value are
  omitted rather than being promoted to manual calibration.
- For production trim, use ``compass set mount`` and/or magnet bias/scale,
  then persist the chosen values in your own upper-layer storage.

iPhone comparison
*****************

1. Keep the C2 and iPhone level, parallel, and separated far enough that the
   phone's magnets do not disturb the MMC5603.
2. With driver declination at its default ``0``, disable "Use True North" on
   the iPhone so both devices report magnetic north. Alternatively, enable
   true north on the phone and set the local declination with
   ``compass set decl <deg>``.
3. Run ``compass reset all`` and perform a slow 3D figure-eight until the log
   reports ``accuracy=high(3)``. Then run ``compass dump`` to capture the
   calibration values used for the comparison.
4. Run ``compass stream on 500``. Compare at least eight headings around a
   full turn, pausing two or three seconds at each heading.
5. Compare angles with wraparound: the error is
   ``min(abs(c2 - iphone), 360 - abs(c2 - iphone))``.

A nearly constant error at every direction is a mounting-yaw offset and can be
corrected with ``compass set mount``. Direction-dependent error points to
incomplete magnet calibration, magnetic interference, or an axis-map issue.
For an initial bench acceptance target, aim for repeatability within 3 degrees
and median error within 5 to 10 degrees; characterize several physical units
before choosing a tighter production limit.
