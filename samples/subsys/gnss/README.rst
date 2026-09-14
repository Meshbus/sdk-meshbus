Meshbus GNSS Sample
###################

This sample demonstrates the Meshbus GNSS service (``CONFIG_MBS_GNSS``):
periodic position acquisition through a Zephyr GNSS driver (e.g., Quectel L76K),
shell commands for status/configuration/force-update, and optional system time
synchronization using GNSS UTC.

Features
********

- Periodic acquisition: runs fix cycles at ``update_interval``; puts GNSS into low-power sleep
  between cycles when possible
- Persistent config: stores GNSS config in Settings/ZMS and reapplies it on boot
- Shell commands: ``meshbus gnss status/config/sats/update/enable/disable``
- Satellite cache: caches visible satellites (when ``CONFIG_GNSS_SATELLITES`` is enabled)
- Optional time sync: when ``time_sync`` is enabled, updates ``SYS_CLOCK_REALTIME`` using GNSS UTC
  with a threshold

Requirements
************

- A GNSS device node and a devicetree chosen binding:

  - ``chosen { meshbus,gnss = &<your_gnss_node>; }``
  - This sample provides ``boards/idea_mesh_tracker_c2_nrf54l15_cpuapp.overlay`` which binds
    ``meshbus,gnss`` to ``&l76k``.

- A Zephyr GNSS driver that implements the GNSS driver API (``zephyr/drivers/gnss``).
  The SDK L76K driver uses ``compatible = "quectel,l76k"``.

- For satellite listing: enable ``CONFIG_GNSS_SATELLITES`` (platform-dependent).

Building
********

From the west workspace root run:

.. code-block:: shell

   west build -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
     meshbus/samples/subsys/gnss

Flash a compatible board using the resulting image in ``build/zephyr/zephyr.bin``
and monitor the shell over UART/serial.

Running
*******

On boot, the GNSS service loads Settings and applies the configuration (enabled by default).
Use the shell:

.. code-block:: shell

   meshbus gnss status
   meshbus gnss sats
   meshbus gnss update

View and set configuration (``config set`` expects the full field list):

.. code-block:: shell

   meshbus gnss config get
   # enabled nav_mode fix_rate_hz system_mask update_interval_ms min_active_time_ms time_sync electronic_compass
   meshbus gnss config set yes 0 1 0xff 30000 15000 no no

Command notes:

- ``meshbus gnss status``: prints fix status, HDOP, and satellite summary (``tracked``=locked,
  ``visible``=satellite cache size)
- ``meshbus gnss sats``: prints the cached satellite table
- ``meshbus gnss update``: forces an immediate acquisition (returns busy if already acquiring)
- ``meshbus gnss enable/disable``: toggles GNSS (disable suspends and releases the runtime-PM
  claim so the power domain may TURN_OFF)

Time Sync
=========

When ``time_sync`` is enabled, the service updates the system time using GNSS UTC:

- The service calls ``sys_clock_settime(SYS_CLOCK_REALTIME, ...)`` only when
  ``|gnss_time - system_time|`` exceeds ``CONFIG_MBS_GNSS_TIME_SYNC_THRESHOLD_S`` (seconds).
- In the initial phase, it checks for the first valid fix every
  ``CONFIG_MBS_GNSS_TIME_SYNC_INIT_INTERVAL`` (ms). In the calibration phase, it checks every
  ``CONFIG_MBS_GNSS_TIME_SYNC_CAL_INTERVAL`` (ms) and only syncs when a new valid-fix sample is
  observed.

Example enabling time sync from the shell:

.. code-block:: shell

   meshbus gnss config set yes 0 1 0xff 30000 15000 yes no

Sample Output
*************

Example console logs on boot:

.. code-block:: console

   [00:00:00.000] <inf> mbs_test: Meshbus test application started
   [00:00:00.000] <inf> mbs_test: Build timestamp: Feb  8 2026 00:00:00
   [00:00:00.050] <inf> mbs_gnss: Settings apply: enabled=1 nav_mode=0 fix_rate=1 system_mask=0xff update_interval=30000 min_active_time=15000 time_sync=0
   [00:00:00.060] <inf> mbs_gnss: Starting GNSS fix acquisition

When a fix is acquired:

.. code-block:: console

   <inf> mbs_gnss: GNSS position update: (lat=22.758344, lon=114.147359, alt=90.800m, sats=18, hdop=1.400)
