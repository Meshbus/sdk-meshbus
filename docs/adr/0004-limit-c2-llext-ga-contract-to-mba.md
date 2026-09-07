---
status: accepted
---

# Limit the C2 LLEXT GA contract to MBA

The C2 firmware 1.0.0 LLEXT contract includes an App EDK and MBA packages, but
does not include MBS packages. MBS is investigated with one dedicated
`devkit_nrf54l15` Service-host Qualification Fixture rather than by changing the
Repeater, Room, or Sensor role configurations. It remains outside the C2 GA
contract until a service-host contract is separately qualified.

## Consequences

- The C2 GA package does not need a Service EDK or MBS artifact.
- MBS fixture evidence cannot be presented as C2 product qualification.
- The dedicated fixture exports a Service EDK and validates representative
  official MBS examples through compilation and runtime checks.
- The fixture does not alter or qualify the Repeater, Room, or Sensor partition
  layouts.
- Future MBS support requires an explicit Product Target or Qualification
  Fixture contract, a Service EDK, and separate runtime evidence.
