# Third-party notices

This file records source and license ownership for third-party content embedded
in `sdk-meshbus`. It does not replace license texts or notices retained in the
referenced files and directories. External west projects are not copied into
this repository and remain governed by their own licenses.

## U8G2

- Source: <https://github.com/olikraus/u8g2>
- Location: `subsys/u8g2/`, `include/zephyr/display/u8g2.h`, and
  `include/zephyr/display/u8x8.h`
- License: two-clause BSD-style license

The upstream copyright and redistribution notice is retained at the beginning
of the U8G2 source and header files. Font records in
`subsys/u8g2/u8g2_fonts.c` carry their individual source and copyright notes.
The Zephyr adapter files that explicitly use `SPDX-License-Identifier:
Apache-2.0` are SDK-owned integration code.

## ZUI predictive dictionary

- Source: SUBTLEX word frequencies, as recorded in the bundled notice
- Location: `subsys/zui/dicts/`
- License: see `subsys/zui/dicts/LICENSE.subtlex-word-frequencies`

The ZUI implementation outside that dictionary is SDK-owned Apache-2.0 code.

## LLEXT sample applications

The following sample ports include fixed upstream source snapshots. Each
subdirectory retains its license text and records its exact source snapshot in
its README:

- `ard_drivin`: MIT
- `arduboy3d`: MIT
- `castleboy`: MIT
- `hollow`: MIT
- `hopper`: MIT
- `microcity`: GNU GPL v3

The GPL-licensed MicroCity content is an optional sample and is not linked into
the SDK libraries or product firmware by default.

## MeshCore test fixtures

`tests/lib/meshcore/common/lib/` contains third-party fixtures used only by
MeshCore compatibility tests:

- `ed25519`: license retained in `tests/lib/meshcore/common/lib/ed25519/license.txt`
- `Crypto`: permissive license text retained in each source header
- `CayenneLpp`: MIT license attribution retained in each source header

These fixtures are not part of the SDK's public MeshCore dependency. The
runtime MeshCore library is supplied as a separate west project and carries
its own license and provenance.

## Migration provenance

The initial source selection for this repository was copied from FoBE commit
`748e79923cd091e0c99e9701af19fcc598811d8a` into a new Git history. Later SDK
changes are tracked by this repository and must not be attributed to that
baseline commit.
