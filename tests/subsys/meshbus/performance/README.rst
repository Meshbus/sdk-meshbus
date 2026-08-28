Meshbus performance tests
=========================

Applications in this directory measure timing, throughput, memory, storage,
or other resource behavior.  They report measurements and explicit thresholds
separately from service contract results.

Performance applications must document the platform, configuration, sample
count, warm-up, cache policy, and whether a result is diagnostic or a release
gate.

The settings application runs measurements on QEMU.  Its C2 entries are
build-only compile checks until a reviewed test-owned storage partition,
fixture, cleanup procedure, and device-execution authority exist.
