Meshbus Settings test support
=============================

``test_settings.c`` contains reusable Settings helper tests compiled by the
Channel test application at ``../channel``. This directory is not itself a
Zephyr test application root. The separate ``performance/`` application owns
Settings timing and storage measurements.

Do not add ``CMakeLists.txt``, ``prj.conf``, or ``testcase.yaml`` here.
Runnable tests live under the subsystem they exercise and may explicitly
compile the helpers they need from this directory.
