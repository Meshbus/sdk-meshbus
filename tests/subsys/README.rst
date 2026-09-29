Meshbus validation guidance
===========================

Tests are grouped directly by subsystem, matching ``subsys/<name>/``.
Each module owns its nested integration, service-DUT, system, and performance
scenarios. Meshbus scenario IDs use the ``subsys.meshbus`` namespace;
these identify tests independently of their source directories.

Test code consumes ``mbs_*`` APIs from ``<module/module.h>`` and selects services
with ``CONFIG_MBS`` / ``CONFIG_MBS_*``. Tests use the ``meshbus`` selection tag
and ``ZEPHYR_MESHBUS_MODULE_DIR`` for west module discovery. Use the public API
names in fakes, linker wrappers and expected ZBus object names.

Sources of truth
----------------

Use the existing Zephyr surfaces directly:

* ``docs/testing.md`` describes public-contract, hardware, and claim boundaries.
* The nearest ``testcase.yaml`` defines runnable scenarios, platforms,
  fixtures, harnesses, and configuration variants.
* ``CMakeLists.txt``, ``prj.conf``, overlays, and test sources define the
  compiled backend and assertions.
* Twister output is the authoritative automated test result.
* Serial transcripts, instrument output, and explicit operator notes support
  hardware and physical claims.

Do not duplicate scenario ownership or platform selection in another committed
matrix.  Add a new scenario to the nearest ``testcase.yaml`` and keep its
README current when fixtures, destructive actions, storage layout, RF use, or
manual observations need explanation.

Evidence boundary
-----------------

Record only what is needed to reproduce and classify a run:

* source revision and relevant dirty state;
* testcase ID, board, configuration, build or Twister command, and artifact;
* probe, serial endpoint, fixture, and approved hardware action when used;
* expected marker or observation, timeout, result, and relevant transcript;
* cleanup action and final device state;
* claims proved and higher layers still unverified.

A QEMU contract pass does not prove hardware.  A service-DUT pass does not
prove physical output, RF delivery, peer interoperability, product-role
composition, power, soak, upgrade, or release readiness.  Manual observations
must include the prompt, expected behavior, operator answer, timestamp, and
device identity, and must never include credentials or private keys.

Keep run artifacts outside the source tree or under an ignored build/output
directory.  Do not add a custom collector or schema unless repeated real-world
failures show that Twister output and concise run records are insufficient.
