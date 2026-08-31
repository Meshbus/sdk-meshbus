# Meshbus Service Implementations

Services own runtime state, synchronization, persistence, device/PM policy,
ZBus wiring, and thin shell/MCUmgr adapters. Public declarations live under
`include/zephyr/meshbus/`; local CMake/Kconfig and tests define enabled behavior.

Apply the consuming Spec Kit project's SDK and verification standards. Keep
service-specific keys, migrations, and lifecycle details next to their owning
code or public contract rather than introducing another rule hierarchy here.
