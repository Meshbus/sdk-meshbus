---
status: accepted
---

# Allow unrestricted owner-supplied MBA packages

The C2 firmware 1.0.0 allows an owner to load and launch any target-compatible
MBA package. MBA packages do not require a FoBE signature, publisher approval,
or a separate developer mode. Launch remains an explicit foreground-app action;
this decision does not add MBS boot services to the C2 product contract.

An MBA is native code running without an MPU or userspace isolation boundary.
The owner therefore grants it the same practical trust as the running base
application. MCUboot image authorization establishes the authenticity of the
base application before launch, but it does not make an MBA trusted by FoBE or
preserve an official runtime-integrity claim after the MBA starts.

## Consequences

- The C2 product does not use an Extension Package Key or reject an MBA because
  of its author or distribution source.
- Installation transports remain authenticated where they are remotely
  reachable. Open authorship does not authorize an unrelated party to install
  or launch code on the owner's device.
- Firmware must not rely on an MBA being unable to inspect memory, alter state,
  access exported capabilities, crash the device, or cause data loss.
- Package hashes and EDK provenance support transfer integrity and diagnosis;
  they are not publisher authorization.
- Qualification covers the host loader, resource admission, explicit launch,
  unload, failure recovery, and official examples. It cannot qualify arbitrary
  third-party MBA behavior.
