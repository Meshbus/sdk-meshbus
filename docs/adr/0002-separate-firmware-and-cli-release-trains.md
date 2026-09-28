---
status: accepted
---

# Separate firmware and CLI release trains

Firmware and the Meshbus host CLI use independent versions and publication
cycles because device qualification and host-platform packaging have different
release constraints. A firmware release records compatible CLI versions without
bundling or versioning them as one product.

Firmware assembly may require a verified CLI executable for its build tools,
but it does not require publishing every supported CLI platform archive.
CLI code signing, notarization and platform coverage belong to the CLI release
train. The [distribution guide](../../DISTRIBUTION.md) owns both workflows.
