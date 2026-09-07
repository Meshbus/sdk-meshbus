---
status: accepted
---

# Separate firmware and CLI release trains

Firmware and the Meshbus host CLI will use independent versions and publication
cycles. Six-platform CLI packaging must not block a firmware GA Release; a
firmware release may identify compatible CLI versions without bundling or
versioning them as one product.

## Consequences

- Firmware assembly and qualification cannot require all CLI platform archives.
- CLI code signing, notarization, and platform coverage belong to the CLI
  release train.
