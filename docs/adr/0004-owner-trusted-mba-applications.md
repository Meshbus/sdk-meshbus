---
status: accepted
---

# Run owner-trusted native MBA applications

Desktop profiles with LLEXT support allow owners to install and explicitly
launch target-compatible MBA applications without a FoBE signature, publisher
approval or a developer mode. This permits independent application authorship.
The supported host contract is a foreground MBA application and its App EDK;
resident MBS services and a Service EDK require a separate host design and
qualification rather than being implied by LLEXT support.

An MBA runs native code without an application isolation boundary. It is not
sandboxed from the base firmware, even on a platform with hardware stack or
memory protection. Owners grant it the same practical trust as the base
application. It can access memory and exported capabilities, alter state, crash
the device or cause data loss. Base-image authentication does not authenticate
MBA authors or establish runtime integrity after an MBA starts.

Remotely reachable installation transports still require authentication;
physical UART access is a separate owner access path. Package hashes and EDK
provenance support integrity and compatibility checks, not publisher approval.
Qualification covers loader admission, explicit launch, resource cleanup and
failure recovery, rather than arbitrary third-party application behavior.

Use the [MBA metadata contract](../../subsys/llext/METADATA.md)
for metadata, target and required-symbol checks, and the
[MBA development guide](../../scripts/meshbus/APP_DEVELOPMENT.md) for the
build, install and session workflow.
