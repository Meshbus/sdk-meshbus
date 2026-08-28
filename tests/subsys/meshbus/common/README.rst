Meshbus test support
====================

This directory contains reusable test sources and helpers.  It is not a
Zephyr test application root.

Do not add ``CMakeLists.txt``, ``prj.conf``, or ``testcase.yaml`` here.
Runnable tests belong to ``services/``, ``system/``, or ``performance/`` and
may compile the helpers they need from this directory.
