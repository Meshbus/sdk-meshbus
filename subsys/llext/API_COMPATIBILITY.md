# Meshbus LLEXT Compatibility Policy

Service APIs use the `mbs_` namespace. Applications importing former
`meshbus_` service symbols must be rebuilt with an EDK exported from the new
firmware. No legacy service symbol aliases are exported. This source/symbol
migration preserves metadata layout, magic, channel IDs, and protobuf schemas;
it does not change the metadata wire-format version.

Meshbus metadata v2 enforces an exact interface ABI requirement, independently
tracked in `INTERFACE_ABI`. The EDK exports `interface-abi`; the CLI writes it as
LE32 at byte 216 in the existing 368-byte metadata record. Bytes 220..263 remain
zero. The host rejects a missing/zero or different ABI with `-EPROTONOSUPPORT`
before ELF loading, constructors, or creation of an MBA Session. Metadata v2 is
not understood by old hosts and is rejected as an unknown format version.

Each firmware release has a matching EDK, and each `.mba` records that EDK
version as build provenance. The interface ABI is not the firmware commit,
EDK content digest, application version, or Arduboy SDK semantic revision.

The loader applies these hard metadata gates:

- metadata magic, format version, size, strings, resources, and reserved bytes;
- exact target equality with `CONFIG_BOARD_TARGET`;
- exact interface ABI equality for metadata v2;
- normal Zephyr ELF loading, symbol resolution, relocation, and entry lookup.

The recorded `edk_version` is intentionally not compared with the running
firmware version. A package built with another release may run when all symbols
and layouts it uses remain compatible. Missing symbols and unsupported
relocations fail through the normal loader path.

New hosts also accept metadata v1 with all 48 compatibility bytes zero, using
the original best-effort behavior. v1 cannot detect a same-name signature or
public structure layout change. Preserve v1 symbols/layouts while accepting
these packages. Prefer adding a new versioned symbol such as `_v2` instead of
changing an exported interface in place. If an incompatible shared contract
must change, increment `INTERFACE_ABI` and review the v1 acceptance policy;
never assume matching symbol names prove compatibility. Additive symbols do
not require an ABI increment: unavailable imports still fail symbol checks.

The metadata wire-format version is tracked independently in
`METADATA_VERSION`. v2 changes interpretation of four formerly reserved bytes;
it does not change record size or any other offset. This gate does not restrict
authors, add package signing, or change the owner-supplied MBA trust model.
