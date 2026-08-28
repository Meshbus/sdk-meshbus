# Meshbus LLEXT Compatibility Policy

Meshbus does not assign or enforce an LLEXT ABI version. Each published
firmware release has a matching EDK with the same application version, and each
`.mba` or `.mbs` package records that EDK version as build provenance.

The loader applies these hard metadata gates:

- metadata magic, format version, size, strings, resources, and reserved bytes;
- exact target equality with `CONFIG_BOARD_TARGET`;
- normal Zephyr ELF loading, symbol resolution, relocation, and entry lookup.

The recorded `edk_version` is intentionally not compared with the running
firmware version. A package built with another release may run when all symbols
and layouts it uses remain compatible. Missing symbols and unsupported
relocations fail through the normal loader path.

This best-effort model cannot detect a same-name function signature change or a
public structure layout change. Exported APIs should therefore avoid changing
an existing symbol in place. Add a new versioned symbol, such as `_v2`, for an
incompatible replacement and preserve the old symbol while it remains needed.

The metadata wire-format version is tracked independently in
`METADATA_VERSION`. It starts at 1 and changes only when the binary metadata
layout or interpretation changes incompatibly.
