MBA Session lifecycle integration
=================================

This qemu_x86 test composes the real Desktop app runtime, Registry, LLEXT
loader, extension entry, kernel threads and simulated flash/LittleFS. Calls
enter through the same private lifecycle interface used by Launcher and the
Desktop event loop. Assertions observe launch results, active/busy state,
resource reuse and UI restore/error delivery.

UI operations use a recording adapter. Linker wraps inject an LLEXT unload
error and delay thread termination after the real exit notification, proving
that join precedes unload. A second scheduling wrap consumes an early exit
notification before launch returns and checks that Desktop is awakened again.
The test does not emulate the lifecycle itself.

Coverage includes load and start failure cleanup, retained resources after
unload failure, retry only on an explicit new launch, duplicate launch/exit,
and continued operation of generic external and built-in apps. The shuffled
scenario checks that each test completely reclaims its own session.

Run from the west workspace::

  west twister -T meshbus/tests/subsys/meshbus/services/desktop/mba_lifecycle \
    -p qemu_x86 -O twister-out/<task>/mba-lifecycle --inline-logs -j 1

This is integration evidence, not physical display/input observation or C2
product qualification. Arbitrary native MBA code remains owner-trusted;
the host cannot automatically stop its untracked asynchronous work.

The separate ``service_dut/`` application runs these assertions on DevKit
nRF54L15 with real RRAM/LittleFS. Read its README before device execution:
it erases dedicated test storage and still substitutes UI presentation.
